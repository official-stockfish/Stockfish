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

namespace Learner{

    // Sfen reader
    struct SfenReader
    {
        // Number of phases buffered by each thread 0.1M phases. 4M phase at 40HT
        static constexpr size_t THREAD_BUFFER_SIZE = 10 * 1000;

        // Buffer for reading files (If this is made larger,
        // the shuffle becomes larger and the phases may vary.
        // If it is too large, the memory consumption will increase.
        // SFEN_READ_SIZE is a multiple of THREAD_BUFFER_SIZE.
        static constexpr const size_t SFEN_READ_SIZE = LEARN_SFEN_READ_SIZE;

        // Do not use std::random_device().
        // Because it always the same integers on MinGW.
        SfenReader(int thread_num, const std::string& seed) :
            prng(seed)
        {
            packed_sfens.resize(thread_num);
            total_read = 0;
            end_of_files = false;
            shuffle = true;
            stop_flag = false;
        }

        ~SfenReader()
        {
            if (file_worker_thread.joinable())
                file_worker_thread.join();
        }

        // Load the phase for calculation such as mse.
        PSVector read_for_mse(uint64_t count)
        {
            PSVector sfen_for_mse;
            sfen_for_mse.reserve(count);

            for (uint64_t i = 0; i < count; ++i)
            {
                PackedSfenValue ps;
                if (!read_to_thread_buffer(0, ps))
                {
                    std::cout << "Error! read packed sfen , failed." << std::endl;
                    return sfen_for_mse;
                }

                sfen_for_mse.push_back(ps);
            }

            return sfen_for_mse;
        }

        PSVector read_validation_set(const std::string& file_name, int eval_limit, bool use_draw_games)
        {
            PSVector sfen_for_mse;

            auto input = open_sfen_input_file(file_name);

            while(!input->eof())
            {
                std::optional<PackedSfenValue> p_opt = input->next();
                if (p_opt.has_value())
                {
                    auto& p = *p_opt;

                    if (eval_limit < abs(p.score))
                        continue;

                    if (!use_draw_games && p.game_result == 0)
                        continue;

                    sfen_for_mse.push_back(p);
                }
                else
                {
                    break;
                }
            }

            return sfen_for_mse;
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

                        total_read += THREAD_BUFFER_SIZE;

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

        // Start a thread that loads the phase file in the background.
        void start_file_read_worker()
        {
            file_worker_thread = std::thread([&] {
                this->file_read_worker();
                });
        }

        void file_read_worker()
        {
            auto open_next_file = [&]() {
                // no more
                for(;;)
                {
                    sfen_input_stream.reset();

                    if (filenames.empty())
                        return false;

                    // Get the next file name.
                    std::string filename = filenames.front();
                    filenames.pop_front();

                    sfen_input_stream = open_sfen_input_file(filename);

                    if (sfen_input_stream == nullptr)
                    {
                        std::cout << "File does not exist: " << filename << '\n';
                    }
                    else
                    {
                        std::cout << "Opened file for reading: " << filename << '\n';

                        // in case the file is empty or was deleted.
                        if (sfen_input_stream->eof())
                        {
                            std::cout << "File empty, nothing to read.\n";
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
                std::cout << "..end of files." << std::endl;
                end_of_files = true;
                return;
            }

            while (true)
            {
                // Wait for the buffer to run out.
                // This size() is read only, so you don't need to lock it.
                while (!stop_flag && packed_sfens_pool.size() >= SFEN_READ_SIZE / THREAD_BUFFER_SIZE)
                    sleep(100);

                if (stop_flag)
                    return;

                PSVector sfens;
                sfens.reserve(SFEN_READ_SIZE);

                // Read from the file into the file buffer.
                while (sfens.size() < SFEN_READ_SIZE)
                {
                    std::optional<PackedSfenValue> p = sfen_input_stream->next();
                    if (p.has_value())
                    {
                        sfens.push_back(*p);
                    }
                    else if(!open_next_file())
                    {
                        // There was no next file. Abort.
                        std::cout << "..end of files." << std::endl;
                        end_of_files = true;
                        return;
                    }
                }

                // Shuffle the read phase data.
                if (shuffle)
                {
                    Algo::shuffle(sfens, prng);
                }

                // Divide this by THREAD_BUFFER_SIZE. There should be size pieces.
                // SFEN_READ_SIZE shall be a multiple of THREAD_BUFFER_SIZE.
                assert((SFEN_READ_SIZE % THREAD_BUFFER_SIZE) == 0);

                auto size = size_t(SFEN_READ_SIZE / THREAD_BUFFER_SIZE);
                std::vector<std::unique_ptr<PSVector>> buffers;
                buffers.reserve(size);

                for (size_t i = 0; i < size; ++i)
                {
                    // Delete this pointer on the receiving side.
                    auto buf = std::make_unique<PSVector>();
                    buf->resize(THREAD_BUFFER_SIZE);
                    memcpy(
                        buf->data(),
                        &sfens[i * THREAD_BUFFER_SIZE],
                        sizeof(PackedSfenValue) * THREAD_BUFFER_SIZE);

                    buffers.emplace_back(std::move(buf));
                }

                {
                    std::unique_lock<std::mutex> lk(mutex);

                    // The mutex lock is required because the%
                    // contents of packed_sfens_pool are changed.

                    for (auto& buf : buffers)
                        packed_sfens_pool.emplace_back(std::move(buf));
                }
            }
        }

        void stop()
        {
            stop_flag = true;
        }

        void set_do_shuffle(bool v)
        {
            shuffle = v;
        }

        void add_file(const std::string& filename)
        {
            filenames.push_back(filename);
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
    };
}
