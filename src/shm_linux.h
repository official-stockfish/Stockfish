/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2025 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef SHM_LINUX_H_INCLUDED
#define SHM_LINUX_H_INCLUDED

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <mutex>
#include <new>
#include <optional>
#include <signal.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <type_traits>
#include <unistd.h>
#include <unordered_set>

namespace Stockfish {

namespace shm {

namespace detail {
// Header placed after the data
struct ShmHeader {
    std::atomic<uint32_t> ref_count{0};
    std::atomic<bool>     initialized{false};
    uint32_t              magic = 0xDEADBEEF;
};

class SharedMemoryBase {
   public:
    virtual ~SharedMemoryBase()                      = default;
    virtual void               close() noexcept      = 0;
    virtual const std::string& name() const noexcept = 0;
};

// Static registry for cleanup
class SharedMemoryRegistry {
   private:
    static std::mutex                            registry_mutex_;
    static std::unordered_set<SharedMemoryBase*> active_instances_;

   public:
    static void register_instance(SharedMemoryBase* instance) {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        active_instances_.insert(instance);
    }

    static void unregister_instance(SharedMemoryBase* instance) {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        active_instances_.erase(instance);
    }

    static void cleanup_all() noexcept {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        for (auto* instance : active_instances_)
            instance->close();

        active_instances_.clear();
    }
};

inline std::mutex                            SharedMemoryRegistry::registry_mutex_;
inline std::unordered_set<SharedMemoryBase*> SharedMemoryRegistry::active_instances_;

}  // namespace detail

template<typename T>
class SharedMemory: public detail::SharedMemoryBase {
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    static_assert(!std::is_pointer_v<T>, "T cannot be a pointer type");

   private:
    std::string        name_;
    int                fd_         = -1;
    void*              mapped_ptr_ = nullptr;
    T*                 data_ptr_   = nullptr;
    detail::ShmHeader* header_ptr_ = nullptr;
    size_t             total_size_ = 0;

    static constexpr size_t calculate_total_size() noexcept {
        return sizeof(T) + sizeof(detail::ShmHeader);
    }

   public:
    explicit SharedMemory(const std::string& name) noexcept :
        name_(name),
        total_size_(calculate_total_size()) {}

    ~SharedMemory() noexcept {
        detail::SharedMemoryRegistry::unregister_instance(this);
        close();
    }

    // Non-copyable but movable
    SharedMemory(const SharedMemory&)            = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;

    SharedMemory(SharedMemory&& other) noexcept :
        name_(std::move(other.name_)),
        fd_(other.fd_),
        mapped_ptr_(other.mapped_ptr_),
        data_ptr_(other.data_ptr_),
        header_ptr_(other.header_ptr_),
        total_size_(other.total_size_) {

        // Update registry
        detail::SharedMemoryRegistry::unregister_instance(&other);
        detail::SharedMemoryRegistry::register_instance(this);

        other.reset();
    }

    SharedMemory& operator=(SharedMemory&& other) noexcept {
        if (this != &other)
        {
            detail::SharedMemoryRegistry::unregister_instance(this);
            close();

            name_       = std::move(other.name_);
            fd_         = other.fd_;
            mapped_ptr_ = other.mapped_ptr_;
            data_ptr_   = other.data_ptr_;
            header_ptr_ = other.header_ptr_;
            total_size_ = other.total_size_;

            detail::SharedMemoryRegistry::unregister_instance(&other);
            detail::SharedMemoryRegistry::register_instance(this);

            other.reset();
        }
        return *this;
    }

    // Create or open shared memory region
    [[nodiscard]] bool open(const T& initial_value) noexcept {
        if (is_open())
            return false;

        // Try to create new shared memory
        fd_ = shm_open(name_.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666);
        if (fd_ != -1)
        {
            if (!setup_new_region(initial_value))
            {
                close();
                shm_unlink(name_.c_str());
                return false;
            }
            detail::SharedMemoryRegistry::register_instance(this);
            return true;
        }

        // If creation failed, try to open existing
        fd_ = shm_open(name_.c_str(), O_RDWR, 0666);
        if (fd_ == -1)
        {
            return false;
        }

        bool success = setup_existing_region(initial_value);
        if (success)
            detail::SharedMemoryRegistry::register_instance(this);
        return success;
    }

    void close() noexcept override {
        if (mapped_ptr_ != nullptr)
        {
            if (header_ptr_ != nullptr)
            {
                auto old_count = header_ptr_->ref_count.fetch_sub(1, std::memory_order_acq_rel);
                if (old_count == 1)
                {
                    // Last reference, destroy the object
                    if (header_ptr_->initialized.load(std::memory_order_acquire))
                    {
                        data_ptr_->~T();
                    }

                    // remove shm
                    shm_unlink(name_.c_str());
                }
            }

            munmap(mapped_ptr_, total_size_);

            mapped_ptr_ = nullptr;
            data_ptr_   = nullptr;
            header_ptr_ = nullptr;
        }

        if (fd_ != -1)
        {
            ::close(fd_);
        }

        reset();
    }

    const std::string& name() const noexcept override { return name_; }

    [[nodiscard]] bool is_open() const noexcept {
        return fd_ != -1 && mapped_ptr_ != nullptr && data_ptr_ != nullptr;
    }

    [[nodiscard]] const T& get() const noexcept { return *data_ptr_; }

    [[nodiscard]] const T* operator->() const noexcept { return data_ptr_; }

    [[nodiscard]] const T& operator*() const noexcept { return *data_ptr_; }

    [[nodiscard]] uint32_t ref_count() const noexcept {
        return header_ptr_ ? header_ptr_->ref_count.load(std::memory_order_acquire) : 0;
    }

    [[nodiscard]] bool is_initialized() const noexcept {
        return header_ptr_ ? header_ptr_->initialized.load(std::memory_order_acquire) : false;
    }

    // Static cleanup method for atexit
    static void cleanup_all_instances() noexcept { detail::SharedMemoryRegistry::cleanup_all(); }

   private:
    void reset() noexcept {
        fd_         = -1;
        mapped_ptr_ = nullptr;
        data_ptr_   = nullptr;
        header_ptr_ = nullptr;
    }

    [[nodiscard]] bool setup_new_region(const T& initial_value) noexcept {
        // Set the size of the shared memory region
        if (ftruncate(fd_, static_cast<off_t>(total_size_)) == -1)
        {
            return false;
        }

        // Map the memory
        auto flags  = PROT_READ | PROT_WRITE;
        mapped_ptr_ = mmap(nullptr, total_size_, flags, MAP_SHARED, fd_, 0);

        if (mapped_ptr_ == MAP_FAILED)
        {
            mapped_ptr_ = nullptr;
            return false;
        }

        // Set up pointers - data first, header immediately after
        data_ptr_ = static_cast<T*>(mapped_ptr_);
        header_ptr_ =
          reinterpret_cast<detail::ShmHeader*>(static_cast<char*>(mapped_ptr_) + sizeof(T));

        // Initialize header
        new (header_ptr_) detail::ShmHeader{};
        header_ptr_->ref_count.store(1, std::memory_order_release);

        // Use placement new to construct the object
        new (data_ptr_) T(initial_value);
        header_ptr_->initialized.store(true, std::memory_order_release);

        return true;
    }

    [[nodiscard]] bool setup_existing_region(const T& initial_value) noexcept {
        // Map the memory
        auto flags  = PROT_READ | PROT_WRITE;
        mapped_ptr_ = mmap(nullptr, total_size_, flags, MAP_SHARED, fd_, 0);

        if (mapped_ptr_ == MAP_FAILED)
        {
            mapped_ptr_ = nullptr;
            return false;
        }

        data_ptr_ = static_cast<T*>(mapped_ptr_);
        header_ptr_ =
          reinterpret_cast<detail::ShmHeader*>(static_cast<char*>(mapped_ptr_) + sizeof(T));

        if (header_ptr_->magic != 0xDEADBEEF)
        {
            return false;
        }

        header_ptr_->ref_count.fetch_add(1, std::memory_order_acq_rel);

        bool expected = false;
        if (header_ptr_->initialized.compare_exchange_strong(expected, true,
                                                             std::memory_order_acq_rel))
        {
            // We won the race to initialize
            new (data_ptr_) T(initial_value);
        }
        else
        {
            // Already initialized by another process, wait for completion
            std::cout << "Waiting for initialization of shared memory: " << name_ << std::endl;
            while (!header_ptr_->initialized.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            std::cout << "Shared memory initialized: " << name_ << std::endl;
        }

        return true;
    }
};

// Convenience function to create shared memory
template<typename T>
[[nodiscard]] std::optional<SharedMemory<T>> create_shared(const std::string& name,
                                                           const T& initial_value) noexcept {
    SharedMemory<T> shm(name);
    if (shm.open(initial_value))
    {
        return std::move(shm);
    }
    return std::nullopt;
}

}  // namespace shm
}  // namespace Stockfish

#endif  // #ifndef SHM_LINUX_H_INCLUDED
