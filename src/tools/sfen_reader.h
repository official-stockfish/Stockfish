#include "sfen_stream.h"

#include "packed_sfen.h"

#include "misc.h"

#include <string>
#include <vector>
#include <deque>
#include <memory>
#include <mutex>
#include <list>
#include <atomic>
#include <optional>
#include <iostream>
#include <cstdint>
#include <thread>
#include <functional>

namespace Stockfish::Tools{

    enum struct SfenReaderMode
    {
        Sequential,
        Cyclic
    };

    // Sfen reader
    struct SfenReader
    {
        // Number of phases buffered by each thread 0.1M phases. 4M phase at 40HT
        static constexpr size_t DEFAULT_THREAD_BUFFER_SIZE = 10 * 1000;

        // Buffer for reading files (If this is made larger,
        // the shuffle becomes larger and the phases may vary.
        // If it is too large, the memory consumption will increase.
        // SFEN_READ_SIZE is a multiple of THREAD_BUFFER_SIZE.
        static constexpr const size_t DEFAULT_SFEN_READ_SIZE = 1000 * 1000 * 10;

        // Do not use std::random_device().
        // Because it always the same integers on MinGW.
        SfenReader(
            const std::vector<std::string>& filenames_,
            bool do_shuffle,
            SfenReaderMode mode_,
            int thread_num,
            const std::string& seed,
            size_t read_size = DEFAULT_SFEN_READ_SIZE,
            size_t buffer_size = DEFAULT_THREAD_BUFFER_SIZE
        ) :
            filenames(filenames_.begin(), filenames_.end()),
            mode(mode_),
            // Due to the implementation of waiting for buffer empty a bit
            // the read size must be at least twice the buffer size.
            sfen_read_size(std::max(read_size, buffer_size * 2)),
            thread_buffer_size(buffer_size),
            prng(seed)
        {
            packed_sfens.resize(thread_num);
            total_read = 0;
            end_of_files = false;
            shuffle = do_shuffle;
            stop_flag = false;
            num_buffers_in_pool.store(0);

            file_worker_thread = std::thread([&] {
                this->file_read_worker();
            });
        }

        ~SfenReader()
        {
            stop_flag = true;

            if (file_worker_thread.joinable())
                file_worker_thread.join();
        }

        // Load the phase for calculation such as mse.
        PSVector read_some(uint64_t count, uint64_t count_tries, std::function<bool(const PackedSfenValue&)> do_take)
        {
            PSVector psv;
            psv.reserve(count);

            for (uint64_t i = 0; i < count_tries; ++i)
            {
                PackedSfenValue ps;
                if (!read_to_thread_buffer(0, ps))
                {
                    std::cout << "ERROR (sfen_reader): Reading failed." << std::endl;
                    return psv;
                }

                if (do_take(ps))
                {
                    psv.push_back(ps);

                    if (psv.size() >= count)
                        break;
                }
            }

            return psv;
        }

        // [ASYNC] Thread returns one aspect. Otherwise returns false.
        bool read_to_thread_buffer(size_t thread_id, PackedSfenValue& ps)
        {
            // If there are any positions left in the thread buffer
            // then retrieve one and return it.
            auto& thread_ps = packed_sfens[thread_id];

            // Fill the read buffer if there is no remaining buffer,
            // but if it doesn't even exist, finish.
            // If the buffer is empty, fill it.
            if ((thread_ps == nullptr || thread_ps->empty())
                && !read_to_thread_buffer_impl(thread_id))
                return false;

            // read_to_thread_buffer_impl() returned true,
            // Since the filling of the thread buffer with the
            // phase has been completed successfully
            // thread_ps->rbegin() is alive.

            ps = thread_ps->back();
            thread_ps->pop_back();

            // If you've run out of buffers, call delete yourself to free this buffer.
            if (thread_ps->empty())
            {
                thread_ps.reset();
            }

            return true;
        }

        // [ASYNC] Read some aspects into thread buffer.
        bool read_to_thread_buffer_impl(size_t thread_id)
        {
            while (true)
            {
                {
                    std::unique_lock<std::mutex> lk(mutex);
                    // If you can fill from the file buffer, that's fine.
                    if (packed_sfens_pool.size() != 0)
                    {
                        // It seems that filling is possible, so fill and finish.

                        packed_sfens[thread_id] = std::move(packed_sfens_pool.front());
                        packed_sfens_pool.pop_front();
                        num_buffers_in_pool.fetch_sub(1);

                        total_read += thread_buffer_size;

                        return true;
                    }
                }

                // The file to read is already gone. No more use.
                if (end_of_files)
                    return false;

                // Waiting for file worker to fill packed_sfens_pool.
                // The mutex isn't locked, so it should fill up soon.
                // Poor man's condition variable.
                sleep(1);
            }

        }

        void file_read_worker()
        {
            std::string currentFilename;
            uint64_t numEntriesReadFromCurrentFile = 0;

            auto open_next_file = [&]() {
                // no more
                for(;;)
                {
                    sfen_input_stream.reset();

                    if (filenames.empty())
                        return false;

                    // Get the next file name.
                    currentFilename = filenames.front();
                    filenames.pop_front();

                    numEntriesReadFromCurrentFile = 0;

                    sfen_input_stream = open_sfen_input_file(currentFilename);

                    auto out = sync_region_cout.new_region();
                    if (sfen_input_stream == nullptr)
                    {
                        out << "INFO (sfen_reader): File does not exist: " << currentFilename << '\n';
                    }
                    else
                    {
                        out << "INFO (sfen_reader): Opened file for reading: " << currentFilename << '\n';

                        // in case the file is empty or was deleted.
                        if (sfen_input_stream->eof())
                        {
                            out << "  - File empty, nothing to read.\n";
                        }
                        else
                        {
                            return true;
                        }
                    }
                }
            };

            if (sfen_input_stream == nullptr && !open_next_file())
            {
                auto out = sync_region_cout.new_region();
                out << "INFO (sfen_reader): End of files." << std::endl;
                end_of_files = true;
                return;
            }

            // We want to set the `end_of_files` only after we read everything AND copy to the buffer pool.
            bool local_end_of_files = false;
            while (!local_end_of_files)
            {
                // Wait for the buffer to run out.
                // This size() is read only, so you don't need to lock it.
                while (!stop_flag && num_buffers_in_pool.load() >= sfen_read_size / thread_buffer_size)
                    sleep(100);

                if (stop_flag)
                    return;

                PSVector sfens;
                sfens.reserve(sfen_read_size);

                // Read from the file into the file buffer.
                while (sfens.size() < sfen_read_size)
                {
                    std::optional<PackedSfenValue> p = sfen_input_stream->next();
                    if (p.has_value())
                    {
                        sfens.push_back(*p);
                        ++numEntriesReadFromCurrentFile;
                    }
                    else
                    {
                        if (mode == SfenReaderMode::Cyclic
                            && numEntriesReadFromCurrentFile > 0)
                        {
                            // The file contained data so we add it again to the end of the queue.
                            filenames.emplace_back(currentFilename);
                        }

                        if(!open_next_file())
                        {
                            // There was no next file. Abort.
                            auto out = sync_region_cout.new_region();
                            out << "INFO (sfen_reader): End of files." << std::endl;
                            local_end_of_files = true;
                            break;
                        }
                    }
                }

                // Shuffle the read phase data.
                if (shuffle)
                {
                    Algo::shuffle(sfens, prng);
                }

                std::vector<std::unique_ptr<PSVector>> buffers;
                for (size_t offset = 0; offset < sfens.size(); offset += thread_buffer_size)
                {
                    const size_t count =
                        offset + thread_buffer_size > sfens.size()
                        ? sfens.size() - offset
                        : thread_buffer_size;

                    // Delete this pointer on the receiving side.
                    auto buf = std::make_unique<PSVector>();
                    buf->resize(count);
                    memcpy(
                        buf->data(),
                        &sfens[offset],
                        sizeof(PackedSfenValue) * count);

                    buffers.emplace_back(std::move(buf));
                }

                {
                    std::unique_lock<std::mutex> lk(mutex);

                    // The mutex lock is required because the%
                    // contents of packed_sfens_pool are changed.

                    for (auto& buf : buffers)
                    {
                        num_buffers_in_pool.fetch_add(1);
                        packed_sfens_pool.emplace_back(std::move(buf));
                    }
                }
            }

            end_of_files = true;
        }

    protected:

        // worker thread reading file in background
        std::thread file_worker_thread;

        // sfen files
        std::deque<std::string> filenames;

        std::atomic<bool> stop_flag;

        // number of phases read (file to memory buffer)
        std::atomic<uint64_t> total_read;

        // Do not shuffle when reading the phase.
        bool shuffle;

        SfenReaderMode mode;

        size_t sfen_read_size;
        size_t thread_buffer_size;

        // Random number to shuffle when reading the phase
        PRNG prng;

        // Did you read the files and reached the end?
        std::atomic<bool> end_of_files;

        // handle of sfen file
        std::unique_ptr<BasicSfenInputStream> sfen_input_stream;

        // sfen for each thread
        // (When the thread is used up, the thread should call delete to release it.)
        std::vector<std::unique_ptr<PSVector>> packed_sfens;

        // Mutex when accessing packed_sfens_pool
        std::mutex mutex;

        // pool of sfen. The worker thread read from the file is added here.
        // Each worker thread fills its own packed_sfens[thread_id] from here.
        // * Lock and access the mutex.
        std::list<std::unique_ptr<PSVector>> packed_sfens_pool;
        std::atomic<size_t> num_buffers_in_pool;
    };
}
