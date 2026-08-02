/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2026 The Stockfish developers (see AUTHORS file)

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

#if !defined(__linux__) || defined(__ANDROID__)
    #error shm_linux.h should not be included on this platform.
#endif

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

#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>
#include <limits.h>

#define SF_MAX_SEM_NAME_LEN NAME_MAX

#include "misc.h"

namespace Stockfish::shm {

namespace detail {

class SharedMemoryBase {
   public:
    enum class CloseType {
        Normal,
        AtExit,
    };

    virtual ~SharedMemoryBase()                          = default;
    virtual void               close(CloseType) noexcept = 0;
    virtual const std::string& name() const noexcept     = 0;
};

class SharedMemoryRegistry {
   private:
    static std::mutex                     registry_mutex_;
    static std::vector<SharedMemoryBase*> active_instances_;

   public:
    static void register_instance(SharedMemoryBase* instance) {
        std::scoped_lock lock(registry_mutex_);
        active_instances_.push_back(instance);
    }

    static void unregister_instance(SharedMemoryBase* instance) {
        std::scoped_lock lock(registry_mutex_);
        active_instances_.erase(
          std::remove(active_instances_.begin(), active_instances_.end(), instance),
          active_instances_.end());
    }

    static void cleanup_at_exit() noexcept {
        std::scoped_lock lock(registry_mutex_);
        // TODO: we litter .sock files upon an abnormal exit (e.g. Ctrl-C)
        // but it's unsafe to acquire the lock in a signal handler
        for (auto* instance : active_instances_)
            instance->close(SharedMemoryBase::CloseType::AtExit);
    }
};

inline std::mutex                     SharedMemoryRegistry::registry_mutex_;
inline std::vector<SharedMemoryBase*> SharedMemoryRegistry::active_instances_;

class CleanupHooks {
   private:
    static std::once_flag register_once_;

    static void register_atexit() noexcept { std::atexit(SharedMemoryRegistry::cleanup_at_exit); }

   public:
    static void ensure_registered() noexcept { std::call_once(register_once_, register_atexit); }
};

inline std::once_flag CleanupHooks::register_once_;

}  // namespace detail


struct TempRoot {
    // /tmp/stockfish-[uid], with appropriate permissions
    std::string prefix;

    static const std::optional<TempRoot>& get_temp_root() {
        static auto temp_root = []() -> std::optional<TempRoot> {
            auto proposed = std::string("/tmp/stockfish-") + std::to_string(getuid());

            if (mkdir(proposed.c_str(), 0700) == 0)
            {
                return {{proposed}};
            }

            if (errno == EEXIST)
            {
                // Temp root already exists, check perms
                struct stat st;
                if (lstat(proposed.c_str(), &st) == 0 && S_ISDIR(st.st_mode)
                    && st.st_uid == getuid() && (st.st_mode & 07777) == 0700)
                {
                    return {{proposed}};
                }
            }

            return std::nullopt;
        }();

        return temp_root;
    }
};

// Allows notifying the background thread that it should close
struct EventNotifier {
    static std::unique_ptr<EventNotifier> create() noexcept {
        int fd = eventfd(0, EFD_CLOEXEC);
        if (fd == -1)
            return nullptr;
        return std::unique_ptr<EventNotifier>(new EventNotifier(fd));
    }

    EventNotifier(const EventNotifier&)             = delete;
    EventNotifier& operator=(const EventNotifier&)  = delete;
    EventNotifier& operator=(EventNotifier&& other) = delete;
    EventNotifier(EventNotifier&& other)            = delete;

    int  get_listener_fd() const noexcept { return event_fd_; }
    void send_shutdown() const {
        uint64_t                 value = 1;
        [[maybe_unused]] ssize_t r     = write(event_fd_, &value, sizeof(value));
    }

    ~EventNotifier() noexcept { ::close(event_fd_); }

   private:
    EventNotifier(int fd) :
        event_fd_(fd) {}
    int event_fd_;
};

// Wrapper around flock() on a file
struct InitLock {
    InitLock(const InitLock&)            = delete;
    InitLock& operator=(const InitLock&) = delete;
    InitLock(InitLock&&)                 = delete;
    InitLock& operator=(InitLock&&)      = delete;

    ~InitLock() {
        if (lock_fd != -1)
        {
            flock(lock_fd, LOCK_UN);
            ::close(lock_fd);
        }
    }

    static std::unique_ptr<InitLock> wait_for_init_lock(const std::string& path) noexcept {
        int lock_fd = ::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0666);
        if (lock_fd == -1)
            return nullptr;

        // Blocks here if another process is currently initializing
        while (flock(lock_fd, LOCK_EX) == -1)
        {
            if (errno != EINTR)
            {
                ::close(lock_fd);  // failed to acquire
                return nullptr;
            }
        }

        return std::unique_ptr<InitLock>(new InitLock(lock_fd));
    }

   private:
    int lock_fd;
    InitLock(int fd) :
        lock_fd(fd) {}
};

template<typename T>
class SharedMemory: public detail::SharedMemoryBase {
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    static_assert(!std::is_pointer_v<T>, "T cannot be a pointer type");

   private:
    std::string name_;
    int         fd_         = -1;
    void*       mapped_ptr_ = nullptr;
    T*          data_ptr_   = nullptr;  // = mapped_ptr_

    // Fishes will put their .sock files in this folder, and each folder is associated with a single underlying
    // shared memfd. Therefore in a NUMA setting, we may have multiple such folders
    std::string shared_dir_;

    // Threads need to successfully and exclusively lock this file to initialize the memfd. If another process has
    // a lock on it, then we wait for it to finish initializing (or die) and release the lock
    std::string init_lock_path_;

    // serve requests for the shared segment on this .sock
    std::string                    socket_path_;
    std::optional<std::thread>     server_thread_;
    std::unique_ptr<EventNotifier> shutdown_;  // used to shut down the server thread

    static std::string make_sentinel_base(const std::string& name) {
        char buf[32];
        // Using std::to_string here causes non-deterministic PGO builds.
        // snprintf, being part of libc, is insensitive to the formatted values.
        std::snprintf(buf, sizeof(buf), "sfshm_%016" PRIu64, hash_string(name));
        return buf;
    }

   public:
    explicit SharedMemory(const std::string& name, const TempRoot& tempRoot) noexcept :
        name_(name),
        shared_dir_(tempRoot.prefix + "/" + make_sentinel_base(name)),
        init_lock_path_(shared_dir_ + "/init_lock"),
        socket_path_(shared_dir_ + "/" + std::to_string(getpid()) + ".sock"),
        server_thread_(std::nullopt) {}

    ~SharedMemory() noexcept override {
        detail::SharedMemoryRegistry::unregister_instance(this);
        SharedMemory::close(CloseType::Normal);
    }

    SharedMemory(const SharedMemory&)            = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;

    SharedMemory(SharedMemory&& other) noexcept :
        name_(std::move(other.name_)),
        fd_(other.fd_),
        mapped_ptr_(other.mapped_ptr_),
        data_ptr_(other.data_ptr_),
        shared_dir_(std::move(other.shared_dir_)),
        init_lock_path_(std::move(other.init_lock_path_)),
        socket_path_(std::move(other.socket_path_)),
        server_thread_(std::move(other.server_thread_)),
        shutdown_(std::move(other.shutdown_)) {

        detail::SharedMemoryRegistry::unregister_instance(&other);
        detail::SharedMemoryRegistry::register_instance(this);

        other.fd_         = -1;
        other.mapped_ptr_ = nullptr;
        other.data_ptr_   = nullptr;
    }

    SharedMemory& operator=(SharedMemory&& other) noexcept {
        if (this != &other)
        {
            detail::SharedMemoryRegistry::unregister_instance(this);
            close(CloseType::Normal);

            name_           = std::move(other.name_);
            fd_             = other.fd_;
            mapped_ptr_     = other.mapped_ptr_;
            data_ptr_       = other.data_ptr_;
            shared_dir_     = std::move(other.shared_dir_);
            init_lock_path_ = std::move(other.init_lock_path_);
            socket_path_    = std::move(other.socket_path_);
            server_thread_  = std::move(other.server_thread_);
            shutdown_       = std::move(other.shutdown_);

            detail::SharedMemoryRegistry::unregister_instance(&other);
            detail::SharedMemoryRegistry::register_instance(this);

            other.fd_         = -1;
            other.mapped_ptr_ = nullptr;
            other.data_ptr_   = nullptr;
        }
        return *this;
    }

    void close(CloseType closeType) noexcept override {
        if (closeType == CloseType::AtExit)
        {
            // Don't unmap on exit as this may cause currently searching threads to segfault.
            // Also, don't join() the server thread on exit.
            if (server_thread_)
            {
                server_thread_->detach();
                server_thread_ = std::nullopt;
            }
        }
        else
        {
            unmap_region();
        }
        reset();
    }

    bool is_mapped() const noexcept { return mapped_ptr_ != nullptr; }

    bool is_serving() const noexcept { return server_thread_.has_value(); }

    const std::string& name() const noexcept override { return name_; }

    const T& get() const noexcept {
        assert(data_ptr_ != nullptr);

        return *data_ptr_;
    }

   private:
    void reset() noexcept {
        if (!socket_path_.empty())
        {
            unlink(socket_path_.c_str());
        }

        if (shutdown_)
            shutdown_->send_shutdown();

        if (server_thread_ && server_thread_->joinable())
        {
            server_thread_->join();
            server_thread_ = std::nullopt;
        }

        shutdown_.reset();
        if (fd_ != -1)
            ::close(fd_);

        fd_         = -1;
        mapped_ptr_ = nullptr;
        data_ptr_   = nullptr;
    }

    void unmap_region() noexcept {
        if (mapped_ptr_)
        {
            munmap(mapped_ptr_, sizeof(T));
            mapped_ptr_ = nullptr;
            data_ptr_   = nullptr;
        }
    }

    // Discover all peers in the shared dir
    std::vector<std::string> get_peer_sockets() noexcept {
        std::vector<std::string> peer_sockets;
        if (DIR* dir = opendir(shared_dir_.c_str()))
        {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr)
            {
                std::string name = entry->d_name;
                if (name.size() >= 5 && name.compare(name.size() - 5, 5, ".sock") == 0)
                    peer_sockets.push_back(shared_dir_ + "/" + name);
            }
            closedir(dir);
        }
        return peer_sockets;
    }

    std::optional<int> try_receive_memfd(const std::string& their_path) noexcept {
        int peer_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (peer_fd == -1)
            return std::nullopt;

        struct sockaddr_un addr = {};
        addr.sun_family         = AF_UNIX;
        std::strncpy(addr.sun_path, their_path.c_str(), sizeof(addr.sun_path) - 1);

        // Connect to peer socket and request access to the memfd
        int ret;
        while ((ret = connect(peer_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr))) < 0
               && errno == EINTR)
        {}

        if (ret == 0)
        {
            msghdr msg = {};

            char         buf[1];
            struct iovec iov[1];
            iov[0].iov_base = buf;
            iov[0].iov_len  = 1;
            msg.msg_iov     = iov;
            msg.msg_iovlen  = 1;

            union {
                char           buf[CMSG_SPACE(sizeof(int))];
                struct cmsghdr align;
            } control_msg = {};

            msg.msg_control    = control_msg.buf;
            msg.msg_controllen = sizeof(control_msg.buf);

            // 1-second timeout in case the peer is stopped
            struct timeval tv = {1, 0};
            setsockopt(peer_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

            ssize_t bytes_recv;
            while ((bytes_recv = recvmsg(peer_fd, &msg, MSG_CMSG_CLOEXEC)) < 0 && errno == EINTR)
            {}

            if (bytes_recv > 0)
            {
                cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
                // Receive rights to the memfd from the peer; see make_server_thread
                if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS)
                {
                    int received_fd;
                    memcpy(&received_fd, CMSG_DATA(cmsg), sizeof(received_fd));
                    ::close(peer_fd);
                    return received_fd;
                }
            }
        }
        else if (errno == ECONNREFUSED || errno == ENOENT)
        {
            // Failed to connect, clean up dead peer
            unlink(their_path.c_str());
        }

        ::close(peer_fd);
        return std::nullopt;
    }

    // Server thread:
    //  - Listens on server_fd
    //  - Forwards the file descriptor fd
    //  - Exits early upon receiving data (from the main thread) on shutdown_fd
    static std::thread make_server_thread(int fd, int shutdown_fd, int server_fd) {
        return std::thread([fd, shutdown_fd, server_fd]() {
            enum {
                FdServer,
                FdShutdown,
                FdCount,
            };

            struct pollfd fds[FdCount];
            fds[FdServer].fd     = server_fd;
            fds[FdServer].events = POLLIN;

            fds[FdShutdown].fd     = shutdown_fd;
            fds[FdShutdown].events = POLLIN;

            while (true)
            {
                int ret = poll(fds, FdCount, -1);
                if (ret < 0)
                {
                    if (errno == EINTR)
                        continue;

                    break;
                }

                if (fds[FdShutdown].revents
                    & (POLLIN | POLLERR | POLLNVAL))  // shutdown requested by main thread
                    break;

                if (fds[FdServer].revents & POLLIN)
                {
                    // Another fish wants access
                    int client_fd;
                    while ((client_fd = accept4(server_fd, NULL, NULL, SOCK_CLOEXEC)) < 0
                           && errno == EINTR)
                    {}
                    if (client_fd < 0)
                        continue;

                    msghdr msg    = {};
                    char   buf[1] = {};
                    iovec  iov[1];
                    iov[0].iov_base = buf;
                    iov[0].iov_len  = 1;
                    msg.msg_iov     = iov;
                    msg.msg_iovlen  = 1;

                    union {
                        char           buf[CMSG_SPACE(sizeof(fd))];
                        struct cmsghdr align;
                    } control_msg = {};

                    msg.msg_control    = control_msg.buf;
                    msg.msg_controllen = sizeof(control_msg.buf);

                    // Send over rights to the memfd (SCM_RIGHTS). The fd may be given a different number, but
                    // will refer to the same underlying file. Once it's mmapped then it will share physical memory
                    // between the processes.
                    // See https://man7.org/linux/man-pages/man7/unix.7.html for more information on SCM_RIGHTS
                    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
                    cmsg->cmsg_level     = SOL_SOCKET;
                    cmsg->cmsg_type      = SCM_RIGHTS;
                    cmsg->cmsg_len       = CMSG_LEN(sizeof(fd));
                    memcpy(CMSG_DATA(cmsg), &fd, sizeof(fd));

                    while (sendmsg(client_fd, &msg, 0) < 0 && errno == EINTR)
                    {}

                    ::close(client_fd);
                }
            }

            ::close(server_fd);
        });
    }

   public:
    [[nodiscard]] bool open(const T& initial_value) noexcept {
        detail::CleanupHooks::ensure_registered();

        if (socket_path_.size() >= sizeof(sockaddr_un::sun_path))
            return false;

        if (mkdir(shared_dir_.c_str(), 0700) != 0 && errno != EEXIST)
            return false;

        {
            auto initLock = InitLock::wait_for_init_lock(init_lock_path_);
            if (!initLock)
                return false;

            // Candidates for receiving the shared memfd
            std::vector<std::string> peer_sockets = get_peer_sockets();

            // Assume we must create it until proven otherwise, i.e., we get it from a peer
            fd_          = -1;
            bool creator = true;

            for (const auto& sock_path : peer_sockets)
            {
                std::optional<int> maybe_memfd = try_receive_memfd(sock_path);

                if (maybe_memfd.has_value() && *maybe_memfd != -1)
                {
                    fd_     = maybe_memfd.value();
                    creator = false;
                    break;
                }
            }

            if (creator)
            {
                // Failed to get it from a peer (no peers, or only dead peers), so create
                fd_ = memfd_create("replicated_data", MFD_CLOEXEC);
                if (fd_ == -1)
                    return false;

                if (ftruncate(fd_, sizeof(T)) != 0)
                {
                    ::close(fd_);
                    fd_ = -1;
                    return false;
                }
            }

            assert(fd_ != -1);

            // Try to map the memfd
            T* mapped_mem =
              static_cast<T*>(mmap(NULL, sizeof(T), PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0));
            if (mapped_mem == MAP_FAILED)
            {
                ::close(fd_);
                fd_ = -1;
                return false;
            }

            (void) madvise(mapped_mem, sizeof(T), MADV_HUGEPAGE);

            if (creator)
            {
                // Creator is responsible for initialization
                *mapped_mem = initial_value;
            }

            mapped_ptr_ = data_ptr_ = mapped_mem;

            detail::SharedMemoryRegistry::register_instance(this);  // register for cleanup at exit

            shutdown_ = EventNotifier::create();
            if (!shutdown_)
                return false;

            int server_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
            if (server_fd == -1)
                return false;

            struct sockaddr_un addr = {};
            addr.sun_family         = AF_UNIX;
            strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

            unlink(socket_path_.c_str());
            if (bind(server_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == -1
                || listen(server_fd, 5) == -1)
            {
                ::close(server_fd);
                return false;
            }

            // Don't release the init lock until we've actually made a socket that
            // other fishes can use.
            server_thread_ = make_server_thread(fd_, shutdown_->get_listener_fd(), server_fd);
        }

        return true;
    }
};

template<typename T>
[[nodiscard]] std::optional<SharedMemory<T>> create_shared(const std::string& name,
                                                           const T& initial_value) noexcept {
    auto tempRoot = TempRoot::get_temp_root();
    if (!tempRoot.has_value())
        return std::nullopt;
    SharedMemory<T> shm(name, *tempRoot);
    if (shm.open(initial_value))
        return shm;
    return std::nullopt;
}

}  // namespace Stockfish::shm

#endif  // #ifndef SHM_LINUX_H_INCLUDED
