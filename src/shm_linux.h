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
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <dirent.h>
#include <mutex>
#include <new>
#include <optional>
#include <pthread.h>
#include <string>
#include <inttypes.h>
#include <type_traits>
#include <unordered_set>

#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__NetBSD__) || defined(__DragonFly__) || defined(__linux__)
    #include <limits.h>
    #define SF_MAX_SEM_NAME_LEN NAME_MAX
#elif defined(__APPLE__)
    #define SF_MAX_SEM_NAME_LEN 31
#else
    #define SF_MAX_SEM_NAME_LEN 255
#endif


namespace Stockfish::shm {

namespace detail {

struct ShmHeader {
    static constexpr uint32_t SHM_MAGIC = 0xAD5F1A12;
    pthread_mutex_t           mutex;
    std::atomic<uint32_t>     ref_count{0};
    std::atomic<bool>         initialized{false};
    uint32_t                  magic = SHM_MAGIC;
};

class SharedMemoryBase {
   public:
    virtual ~SharedMemoryBase()                      = default;
    virtual void               close() noexcept      = 0;
    virtual const std::string& name() const noexcept = 0;
};

class SharedMemoryRegistry {
   private:
    static std::mutex                            registry_mutex_;
    static std::unordered_set<SharedMemoryBase*> active_instances_;

   public:
    static void register_instance(SharedMemoryBase* instance) {
        std::scoped_lock lock(registry_mutex_);
        active_instances_.insert(instance);
    }

    static void unregister_instance(SharedMemoryBase* instance) {
        std::scoped_lock lock(registry_mutex_);
        active_instances_.erase(instance);
    }

    static void cleanup_all() noexcept {
        std::scoped_lock lock(registry_mutex_);
        for (auto* instance : active_instances_)
            instance->close();
        active_instances_.clear();
    }
};

inline std::mutex                            SharedMemoryRegistry::registry_mutex_;
inline std::unordered_set<SharedMemoryBase*> SharedMemoryRegistry::active_instances_;

class CleanupHooks {
   private:
    static std::once_flag register_once_;

    static void handle_signal(int sig) noexcept {
        SharedMemoryRegistry::cleanup_all();
        _Exit(128 + sig);
    }

    static void register_signal_handlers() noexcept {
        std::atexit([]() { SharedMemoryRegistry::cleanup_all(); });

        constexpr int signals[] = {SIGHUP,  SIGINT,  SIGQUIT, SIGILL, SIGABRT, SIGFPE,
                                   SIGSEGV, SIGTERM, SIGBUS,  SIGSYS, SIGXCPU, SIGXFSZ};

        struct sigaction sa;
        sa.sa_handler = handle_signal;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;

        for (int sig : signals)
            sigaction(sig, &sa, nullptr);
    }

   public:
    static void ensure_registered() noexcept {
        std::call_once(register_once_, register_signal_handlers);
    }
};

inline std::once_flag CleanupHooks::register_once_;


inline int portable_fallocate(int fd, off_t offset, off_t length) {
#ifdef __APPLE__
    fstore_t store = {F_ALLOCATECONTIG, F_PEOFPOSMODE, offset, length, 0};
    int      ret   = fcntl(fd, F_PREALLOCATE, &store);
    if (ret == -1)
    {
        store.fst_flags = F_ALLOCATEALL;
        ret             = fcntl(fd, F_PREALLOCATE, &store);
    }
    if (ret != -1)
        ret = ftruncate(fd, offset + length);
    return ret;
#else
    return posix_fallocate(fd, offset, length);
#endif
}

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
    std::string        sentinel_base_;
    std::string        sentinel_path_;

    static constexpr size_t calculate_total_size() noexcept {
        return sizeof(T) + sizeof(detail::ShmHeader);
    }

    static std::string make_sentinel_base(const std::string& name) {
        uint64_t hash = std::hash<std::string>{}(name);
        char     buf[32];
        std::snprintf(buf, sizeof(buf), "sfshm_%016" PRIx64, static_cast<uint64_t>(hash));
        return buf;
    }

   public:
    explicit SharedMemory(const std::string& name) noexcept :
        name_(name),
        total_size_(calculate_total_size()),
        sentinel_base_(make_sentinel_base(name)) {}

    ~SharedMemory() noexcept override {
        detail::SharedMemoryRegistry::unregister_instance(this);
        close();
    }

    SharedMemory(const SharedMemory&)            = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;

    SharedMemory(SharedMemory&& other) noexcept :
        name_(std::move(other.name_)),
        fd_(other.fd_),
        mapped_ptr_(other.mapped_ptr_),
        data_ptr_(other.data_ptr_),
        header_ptr_(other.header_ptr_),
        total_size_(other.total_size_),
        sentinel_base_(std::move(other.sentinel_base_)),
        sentinel_path_(std::move(other.sentinel_path_)) {

        detail::SharedMemoryRegistry::unregister_instance(&other);
        detail::SharedMemoryRegistry::register_instance(this);
        other.reset();
    }

    SharedMemory& operator=(SharedMemory&& other) noexcept {
        if (this != &other)
        {
            detail::SharedMemoryRegistry::unregister_instance(this);
            close();

            name_          = std::move(other.name_);
            fd_            = other.fd_;
            mapped_ptr_    = other.mapped_ptr_;
            data_ptr_      = other.data_ptr_;
            header_ptr_    = other.header_ptr_;
            total_size_    = other.total_size_;
            sentinel_base_ = std::move(other.sentinel_base_);
            sentinel_path_ = std::move(other.sentinel_path_);

            detail::SharedMemoryRegistry::unregister_instance(&other);
            detail::SharedMemoryRegistry::register_instance(this);

            other.reset();
        }
        return *this;
    }

    [[nodiscard]] bool open(const T& initial_value) noexcept {
        detail::CleanupHooks::ensure_registered();

        bool retried_stale = false;

        while (true)
        {
            if (is_open())
                return false;

            bool created_new = false;
            fd_              = shm_open(name_.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666);

            if (fd_ == -1)
            {
                fd_ = shm_open(name_.c_str(), O_RDWR, 0666);
                if (fd_ == -1)
                    return false;
            }
            else
                created_new = true;

            if (!lock_file(LOCK_EX))
            {
                ::close(fd_);
                reset();
                return false;
            }

            bool invalid_header = false;
            bool success =
              created_new ? setup_new_region(initial_value) : setup_existing_region(invalid_header);

            if (!success)
            {
                if (created_new || invalid_header)
                    shm_unlink(name_.c_str());
                if (mapped_ptr_)
                    unmap_region();
                unlock_file();
                ::close(fd_);
                reset();

                if (!created_new && invalid_header && !retried_stale)
                {
                    retried_stale = true;
                    continue;
                }
                return false;
            }

            if (!lock_shared_mutex())
            {
                if (created_new)
                    shm_unlink(name_.c_str());
                if (mapped_ptr_)
                    unmap_region();
                unlock_file();
                ::close(fd_);
                reset();

                if (!created_new && !retried_stale)
                {
                    retried_stale = true;
                    continue;
                }
                return false;
            }

            if (!create_sentinel_file_locked())
            {
                unlock_shared_mutex();
                unmap_region();
                if (created_new)
                    shm_unlink(name_.c_str());
                unlock_file();
                ::close(fd_);
                reset();
                return false;
            }

            header_ptr_->ref_count.fetch_add(1, std::memory_order_acq_rel);

            unlock_shared_mutex();
            unlock_file();
            detail::SharedMemoryRegistry::register_instance(this);
            return true;
        }
    }

    void close() noexcept override {
        if (fd_ == -1 && mapped_ptr_ == nullptr)
            return;

        bool remove_region = false;
        bool file_locked   = lock_file(LOCK_EX);
        bool mutex_locked  = false;

        if (file_locked && header_ptr_ != nullptr)
            mutex_locked = lock_shared_mutex();

        if (mutex_locked)
        {
            if (header_ptr_)
            {
                header_ptr_->ref_count.fetch_sub(1, std::memory_order_acq_rel);
            }
            remove_sentinel_file();
            remove_region = !has_other_live_sentinels_locked();
            unlock_shared_mutex();
        }
        else
        {
            remove_sentinel_file();
            decrement_refcount_relaxed();
        }

        unmap_region();

        if (remove_region)
            shm_unlink(name_.c_str());

        if (file_locked)
            unlock_file();

        if (fd_ != -1)
        {
            ::close(fd_);
            fd_ = -1;
        }

        reset();
    }

    const std::string& name() const noexcept override { return name_; }

    [[nodiscard]] bool is_open() const noexcept { return fd_ != -1 && mapped_ptr_ && data_ptr_; }

    [[nodiscard]] const T& get() const noexcept { return *data_ptr_; }

    [[nodiscard]] const T* operator->() const noexcept { return data_ptr_; }

    [[nodiscard]] const T& operator*() const noexcept { return *data_ptr_; }

    [[nodiscard]] uint32_t ref_count() const noexcept {
        return header_ptr_ ? header_ptr_->ref_count.load(std::memory_order_acquire) : 0;
    }

    [[nodiscard]] bool is_initialized() const noexcept {
        return header_ptr_ ? header_ptr_->initialized.load(std::memory_order_acquire) : false;
    }

    static void cleanup_all_instances() noexcept { detail::SharedMemoryRegistry::cleanup_all(); }

   private:
    void reset() noexcept {
        fd_         = -1;
        mapped_ptr_ = nullptr;
        data_ptr_   = nullptr;
        header_ptr_ = nullptr;
        sentinel_path_.clear();
    }

    void unmap_region() noexcept {
        if (mapped_ptr_)
        {
            munmap(mapped_ptr_, total_size_);
            mapped_ptr_ = nullptr;
            data_ptr_   = nullptr;
            header_ptr_ = nullptr;
        }
    }

    [[nodiscard]] bool lock_file(int operation) noexcept {
        if (fd_ == -1)
            return false;

        while (flock(fd_, operation) == -1)
        {
            if (errno == EINTR)
                continue;
            return false;
        }
        return true;
    }

    void unlock_file() noexcept {
        if (fd_ == -1)
            return;

        while (flock(fd_, LOCK_UN) == -1)
        {
            if (errno == EINTR)
                continue;
            break;
        }
    }

    std::string sentinel_full_path(pid_t pid) const {
        std::string path = "/dev/shm/";
        path += sentinel_base_;
        path.push_back('.');
        path += std::to_string(pid);
        return path;
    }

    void decrement_refcount_relaxed() noexcept {
        if (!header_ptr_)
            return;

        uint32_t expected = header_ptr_->ref_count.load(std::memory_order_relaxed);
        while (expected != 0
               && !header_ptr_->ref_count.compare_exchange_weak(
                 expected, expected - 1, std::memory_order_acq_rel, std::memory_order_relaxed))
        {}
    }

    bool create_sentinel_file_locked() noexcept {
        if (!header_ptr_)
            return false;

        const pid_t self_pid = getpid();
        sentinel_path_       = sentinel_full_path(self_pid);

        for (int attempt = 0; attempt < 2; ++attempt)
        {
            int fd = ::open(sentinel_path_.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
            if (fd != -1)
            {
                ::close(fd);
                return true;
            }

            if (errno == EEXIST)
            {
                ::unlink(sentinel_path_.c_str());
                decrement_refcount_relaxed();
                continue;
            }

            break;
        }

        sentinel_path_.clear();
        return false;
    }

    void remove_sentinel_file() noexcept {
        if (!sentinel_path_.empty())
        {
            ::unlink(sentinel_path_.c_str());
            sentinel_path_.clear();
        }
    }

    static bool pid_is_alive(pid_t pid) noexcept {
        if (pid <= 0)
            return false;

        if (kill(pid, 0) == 0)
            return true;

        return errno == EPERM;
    }

    [[nodiscard]] bool initialize_shared_mutex() noexcept {
        if (!header_ptr_)
            return false;

        pthread_mutexattr_t attr;
        if (pthread_mutexattr_init(&attr) != 0)
            return false;

        bool success = pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED) == 0;
#ifdef PTHREAD_MUTEX_ROBUST
        if (success)
            success = pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST) == 0;
#endif

        if (success)
            success = pthread_mutex_init(&header_ptr_->mutex, &attr) == 0;

        pthread_mutexattr_destroy(&attr);
        return success;
    }

    [[nodiscard]] bool lock_shared_mutex() noexcept {
        if (!header_ptr_)
            return false;

        while (true)
        {
            int rc = pthread_mutex_lock(&header_ptr_->mutex);
            if (rc == 0)
                return true;

#ifdef PTHREAD_MUTEX_ROBUST
            if (rc == EOWNERDEAD)
            {
                if (pthread_mutex_consistent(&header_ptr_->mutex) == 0)
                    return true;
                return false;
            }
#endif

            if (rc == EINTR)
                continue;

            return false;
        }
    }

    void unlock_shared_mutex() noexcept {
        if (header_ptr_)
            pthread_mutex_unlock(&header_ptr_->mutex);
    }

    bool has_other_live_sentinels_locked() const noexcept {
        DIR* dir = opendir("/dev/shm");
        if (!dir)
            return false;

        std::string prefix = sentinel_base_ + ".";
        bool        found  = false;

        while (dirent* entry = readdir(dir))
        {
            std::string name = entry->d_name;
            if (name.rfind(prefix, 0) != 0)
                continue;

            auto  pid_str = name.substr(prefix.size());
            char* end     = nullptr;
            long  value   = std::strtol(pid_str.c_str(), &end, 10);
            if (!end || *end != '\0')
                continue;

            pid_t pid = static_cast<pid_t>(value);
            if (pid_is_alive(pid))
            {
                found = true;
                break;
            }

            std::string stale_path = std::string("/dev/shm/") + name;
            ::unlink(stale_path.c_str());
            const_cast<SharedMemory*>(this)->decrement_refcount_relaxed();
        }

        closedir(dir);
        return found;
    }

    [[nodiscard]] bool setup_new_region(const T& initial_value) noexcept {
        if (ftruncate(fd_, static_cast<off_t>(total_size_)) == -1)
            return false;

        if (detail::portable_fallocate(fd_, 0, static_cast<off_t>(total_size_)) != 0)
            return false;

        mapped_ptr_ = mmap(nullptr, total_size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (mapped_ptr_ == MAP_FAILED)
        {
            mapped_ptr_ = nullptr;
            return false;
        }

        data_ptr_ = static_cast<T*>(mapped_ptr_);
        header_ptr_ =
          reinterpret_cast<detail::ShmHeader*>(static_cast<char*>(mapped_ptr_) + sizeof(T));

        new (header_ptr_) detail::ShmHeader{};
        new (data_ptr_) T{initial_value};

        if (!initialize_shared_mutex())
            return false;

        header_ptr_->ref_count.store(0, std::memory_order_release);
        header_ptr_->initialized.store(true, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool setup_existing_region(bool& invalid_header) noexcept {
        invalid_header = false;

        struct stat st;
        fstat(fd_, &st);
        if (static_cast<size_t>(st.st_size) < total_size_)
        {
            invalid_header = true;
            return false;
        }

        mapped_ptr_ = mmap(nullptr, total_size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (mapped_ptr_ == MAP_FAILED)
        {
            mapped_ptr_ = nullptr;
            return false;
        }

        data_ptr_   = static_cast<T*>(mapped_ptr_);
        header_ptr_ = std::launder(
          reinterpret_cast<detail::ShmHeader*>(static_cast<char*>(mapped_ptr_) + sizeof(T)));

        if (!header_ptr_->initialized.load(std::memory_order_acquire)
            || header_ptr_->magic != detail::ShmHeader::SHM_MAGIC)
        {
            invalid_header = true;
            unmap_region();
            return false;
        }

        return true;
    }
};

template<typename T>
[[nodiscard]] std::optional<SharedMemory<T>> create_shared(const std::string& name,
                                                           const T& initial_value) noexcept {
    SharedMemory<T> shm(name);
    if (shm.open(initial_value))
        return shm;
    return std::nullopt;
}

}  // namespace Stockfish::shm

#endif  // #ifndef SHM_LINUX_H_INCLUDED
