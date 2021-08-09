#include "packed_sfen.h"
#include "sfen_stream.h"

#include "misc.h"

#include "extra/nnue_data_binpack_format.h"

#include "syzygy/tbprobe.h"

#include <cstring>
#include <fstream>
#include <limits>
#include <list>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <thread>
#include <atomic>

namespace Stockfish::Tools {

    // Helper class for exporting Sfen
    struct SfenWriter
    {
        // Amount of sfens required to flush the buffer.
        static constexpr size_t SFEN_WRITE_SIZE = 5000;

        // File name to write and number of threads to create
        SfenWriter(std::string filename_, int thread_num, uint64_t save_count, SfenOutputType sfen_output_type)
        {
            sfen_buffers_pool.reserve((size_t)thread_num * 10);
            sfen_buffers.resize(thread_num);

            auto out = sync_region_cout.new_region();
            out << "INFO (sfen_writer): Creating new data file at " << filename_ << std::endl;

            sfen_format = sfen_output_type;
            output_file_stream = create_new_sfen_output(filename_, sfen_format);
            filename = filename_;
            save_every = save_count;

            finished = false;

            file_worker_thread = std::thread([&] { this->file_write_worker(); });
        }

        ~SfenWriter()
        {
            flush();

            finished = true;
            file_worker_thread.join();
            output_file_stream.reset();

#if !defined(NDEBUG)
            {
                // All buffers should be empty since file_worker_thread
                // should have written everything before exiting.
                for (const auto& p : sfen_buffers) { assert(p == nullptr); (void)p ; }
                assert(sfen_buffers_pool.empty());
            }
#endif
        }

        void write(size_t thread_id, const PackedSfenValue& psv)
        {
            // We have a buffer for each thread and add it there.
            // If the buffer overflows, write it to a file.

            // This buffer is prepared for each thread.
            auto& buf = sfen_buffers[thread_id];

            // Secure since there is no buf at the first time
            // and immediately after writing the thread buffer.
            if (!buf)
            {
                buf = std::make_unique<PSVector>();
                buf->reserve(SFEN_WRITE_SIZE);
            }

            // Buffer is exclusive to this thread.
            // There is no need for a critical section.
            buf->push_back(psv);

            if (buf->size() >= SFEN_WRITE_SIZE)
            {
                // If you load it in sfen_buffers_pool, the worker will do the rest.

                // Critical section since sfen_buffers_pool is shared among threads.
                std::unique_lock<std::mutex> lk(mutex);
                sfen_buffers_pool.emplace_back(std::move(buf));
            }
        }

        void flush()
        {
            for (size_t i = 0; i < sfen_buffers.size(); ++i)
            {
                flush(i);
            }
        }

        // Move what remains in the buffer for your thread to a buffer for writing to a file.
        void flush(size_t thread_id)
        {
            std::unique_lock<std::mutex> lk(mutex);

            auto& buf = sfen_buffers[thread_id];

            // There is a case that buf==nullptr, so that check is necessary.
            if (buf && buf->size() != 0)
            {
                sfen_buffers_pool.emplace_back(std::move(buf));
            }
        }

        // Dedicated thread to write to file
        void file_write_worker()
        {
            while (!finished || sfen_buffers_pool.size())
            {
                std::vector<std::unique_ptr<PSVector>> buffers;
                {
                    std::unique_lock<std::mutex> lk(mutex);

                    // Atomically swap take the filled buffers and
                    // create a new buffer pool for threads to fill.
                    buffers = std::move(sfen_buffers_pool);
                    sfen_buffers_pool = std::vector<std::unique_ptr<PSVector>>();
                }

                if (!buffers.size())
                {
                    // Poor man's condition variable.
                    sleep(100);
                }
                else
                {
                    for (auto& buf : buffers)
                    {
                        output_file_stream->write(*buf);

                        sfen_write_count += buf->size();

                        // Add the processed number here, and if it exceeds save_every,
                        // change the file name and reset this counter.
                        sfen_write_count_current_file += buf->size();
                        if (sfen_write_count_current_file >= save_every)
                        {
                            sfen_write_count_current_file = 0;

                            // Sequential number attached to the file
                            int n = (int)(sfen_write_count / save_every);

                            // Rename the file and open it again.
                            // Add ios::app in consideration of overwriting.
                            // (Depending on the operation, it may not be necessary.)
                            std::string new_filename = filename + "_" + std::to_string(n);
                            output_file_stream = create_new_sfen_output(new_filename, sfen_format);

                            auto out = sync_region_cout.new_region();
                            out << "INFO (sfen_writer): Creating new data file at " << new_filename << std::endl;
                        }
                    }
                }
            }
        }

    private:

        std::unique_ptr<BasicSfenOutputStream> output_file_stream;

        // A new net is saved after every save_every sfens are processed.
        uint64_t save_every = std::numeric_limits<uint64_t>::max();

        // File name passed in the constructor
        std::string filename;

        // Thread to write to the file
        std::thread file_worker_thread;

        // Flag that all threads have finished
        std::atomic<bool> finished;

        SfenOutputType sfen_format;

        // buffer before writing to file
        // sfen_buffers is the buffer for each thread
        // sfen_buffers_pool is a buffer for writing.
        // After loading the phase in the former buffer by SFEN_WRITE_SIZE,
        // transfer it to the latter.
        std::vector<std::unique_ptr<PSVector>> sfen_buffers;
        std::vector<std::unique_ptr<PSVector>> sfen_buffers_pool;

        // Mutex required to access sfen_buffers_pool
        std::mutex mutex;

        // Number of sfens written in total, and the
        // number of sfens written in the current file.
        uint64_t sfen_write_count = 0;
        uint64_t sfen_write_count_current_file = 0;
    };
}
