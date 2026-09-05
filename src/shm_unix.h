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

#ifndef SHM_UNIX_H_INCLUDED
#define SHM_UNIX_H_INCLUDED

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
#include <thread>
#include <type_traits>
#include <utility>

#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>
#include <limits.h>

#define SF_MAX_SEM_NAME_LEN NAME_MAX

#include "memory.h"
#include "misc.h"
#include "thread_native.h"

#if defined(__linux__) && !defined(MADV_COLLAPSE)
    #define MADV_COLLAPSE 25
#endif

namespace Stockfish::shm {

namespace detail {

inline void* map_shared(int fd, usize size) noexcept {
#if defined(__linux__) && !defined(__ANDROID__)
    return Stockfish::mmap_huge_aligned(size, MAP_SHARED, fd);
#else
    return mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
#endif
}

class SharedMemoryRegistry {
   private:
    inline static std::once_flag           register_atexit_once_;
    inline static std::mutex               socket_paths_mutex_;
    inline static std::vector<std::string> socket_paths_;

   public:
    static void register_socket_path(std::string path) {
        std::call_once(register_atexit_once_,
                       []() { std::atexit(SharedMemoryRegistry::cleanup_at_exit); });

        std::scoped_lock lock(socket_paths_mutex_);
        socket_paths_.push_back(std::move(path));
    }

    static void unlink_socket_path(const std::string& path) {
        std::scoped_lock lock(socket_paths_mutex_);
        unlink(path.c_str());
        socket_paths_.erase(std::remove(socket_paths_.begin(), socket_paths_.end(), path),
                            socket_paths_.end());
    }

    static void cleanup_at_exit() noexcept {
        std::scoped_lock lock(socket_paths_mutex_);
        // TODO: we litter .sock files upon an abnormal exit (e.g. Ctrl-C)
        // but it's unsafe to acquire the lock in a signal handler
        for (auto const& path : socket_paths_)
            unlink(path.c_str());
    }
};

}  // namespace detail


[[maybe_unused]] static void set_cloexec(int fd) {
    if (fd != -1)
        (void) fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
}

struct UniqueFd {
    UniqueFd() noexcept = default;
    explicit UniqueFd(int fd) noexcept :
        fd_{fd} {}
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd(UniqueFd&& other) noexcept :
        fd_{other.release()} {}
    UniqueFd& operator=(const UniqueFd&) = delete;
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        std::swap(fd_, other.fd_);
        return *this;
    }
    ~UniqueFd() { reset(); }

    int  get() const noexcept { return fd_; }
    bool is_valid() const noexcept { return fd_ != -1; }

    int release() noexcept { return std::exchange(fd_, -1); }

    void reset(int fd = -1) noexcept {
        if (fd_ != -1)
            ::close(fd_);
        fd_ = fd;
    }

   private:
    int fd_ = -1;
};

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

// Wrapper around flock() on a file
struct InitLock {
    InitLock() = default;

    ~InitLock() {
        if (lock_fd_.is_valid())
            flock(lock_fd_.get(), LOCK_UN);
    }

    static InitLock wait_for_init_lock(const std::string& path) noexcept {
        UniqueFd lock_fd(::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0666));
        if (!lock_fd.is_valid())
            return {};

        // Blocks here if another process is currently initializing
        while (flock(lock_fd.get(), LOCK_EX) == -1)
        {
            if (errno != EINTR)
                return {};  // failed to acquire
        }

        return InitLock(std::move(lock_fd));
    }

    bool is_valid() const { return lock_fd_.is_valid(); }

   private:
    UniqueFd lock_fd_;
    explicit InitLock(UniqueFd lock_fd) :
        lock_fd_(std::move(lock_fd)) {}
};

template<typename T>
class SharedMemory {
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    static_assert(!std::is_pointer_v<T>, "T cannot be a pointer type");

   private:
    std::string name_;
    void*       mapped_ptr_ = nullptr;
    T*          data_ptr_   = nullptr;  // = mapped_ptr_

    // Fishes will put their .sock files in this folder, and each folder is associated with a single underlying
    // shared memfd. Therefore in a NUMA setting, we may have multiple such folders
    std::string shared_dir_;

    // Threads need to successfully and exclusively lock this file to initialize the memfd. If another process has
    // a lock on it, then we wait for it to finish initializing (or die) and release the lock
    std::string init_lock_path_;

    // serve requests for the shared segment on this .sock
    std::string  socket_path_;
    NativeThread server_thread_;
    UniqueFd     shutdown_;  // close to signal server thread shutdown

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
        socket_path_(shared_dir_ + "/" + std::to_string(getpid()) + ".sock") {}

    ~SharedMemory() noexcept {
        if (!socket_path_.empty())
            detail::SharedMemoryRegistry::unlink_socket_path(socket_path_);

        shutdown_.reset();
        if (server_thread_.joinable())
            server_thread_.join();

        if (mapped_ptr_)
            munmap(mapped_ptr_, sizeof(T));
    }

    SharedMemory(const SharedMemory&)            = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;

    SharedMemory(SharedMemory&& other) noexcept { swap(other); }

    SharedMemory& operator=(SharedMemory&& other) noexcept {
        swap(other);
        return *this;
    }

    bool is_mapped() const noexcept { return mapped_ptr_ != nullptr; }

    bool is_serving() const noexcept { return server_thread_.joinable(); }

    const std::string& name() const noexcept { return name_; }

    const T& get() const noexcept {
        assert(data_ptr_ != nullptr);

        return *data_ptr_;
    }

   private:
    void swap(SharedMemory& other) noexcept {
        std::swap(name_, other.name_);
        std::swap(mapped_ptr_, other.mapped_ptr_);
        std::swap(data_ptr_, other.data_ptr_);
        std::swap(shared_dir_, other.shared_dir_);
        std::swap(init_lock_path_, other.init_lock_path_);
        std::swap(socket_path_, other.socket_path_);
        std::swap(server_thread_, other.server_thread_);
        std::swap(shutdown_, other.shutdown_);
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

    static UniqueFd create_unix_socket() {
#ifdef SOCK_CLOEXEC
        UniqueFd fd(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
#else
        UniqueFd fd(socket(AF_UNIX, SOCK_STREAM, 0));
        set_cloexec(fd.get());
#endif
        return fd;
    }

    UniqueFd try_receive_memfd(const std::string& their_path) noexcept {
        auto peer_fd = create_unix_socket();
        if (!peer_fd.is_valid())
            return {};

        // 1-second timeout for connect and receive
        struct timeval tv = {1, 0};
        setsockopt(peer_fd.get(), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        setsockopt(peer_fd.get(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        struct sockaddr_un addr = {};
        addr.sun_family         = AF_UNIX;
        std::strncpy(addr.sun_path, their_path.c_str(), sizeof(addr.sun_path) - 1);

        // Connect to peer socket and request access to the memfd
        int ret;
        do
            ret = connect(peer_fd.get(), reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
        while (ret < 0 && errno == EINTR);

        if (ret == 0)
        {
            struct msghdr   msg                = {};
            const usize     space              = CMSG_SPACE(sizeof(int));
            constexpr usize alignement         = alignof(struct cmsghdr);
            auto            v                  = std::make_unique<std::byte[]>(space + alignement);
            std::byte*      control_msg_buffer = align_ptr_up<alignement>(v.get());

            char         buf[1];
            struct iovec iov[1];
            iov[0].iov_base = buf;
            iov[0].iov_len  = 1;

            msg.msg_iov        = iov;
            msg.msg_iovlen     = 1;
            msg.msg_control    = control_msg_buffer;
            msg.msg_controllen = space;

            ssize_t bytes_recv;
#ifdef MSG_CMSG_CLOEXEC
            int flags = MSG_CMSG_CLOEXEC;
#else
            int flags = 0;
#endif
            do
                bytes_recv = recvmsg(peer_fd.get(), &msg, flags);
            while (bytes_recv < 0 && errno == EINTR);

            if (bytes_recv > 0)
            {
                cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
                // Receive rights to the memfd from the peer; see make_server_thread
                if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS)
                {
                    int received_fd;
                    memcpy(&received_fd, CMSG_DATA(cmsg), sizeof(received_fd));
#ifndef MSG_CMSG_CLOEXEC
                    set_cloexec(received_fd);
#endif
                    return UniqueFd(received_fd);
                }
            }
        }
        else if (errno == ECONNREFUSED || errno == ENOENT)
        {
            // Failed to connect, clean up dead peer
            unlink(their_path.c_str());
        }

        return {};
    }

    // Server thread:
    //  - Forwards the file descriptor fd
    //  - Exits when shutdown_receiver is hung up on
    //  - Listens on server_fd
    static NativeThread
    make_server_thread(UniqueFd fd, UniqueFd shutdown_receiver, UniqueFd server_fd) {
        return create_native_thread(
          NativeThreadOptions{},
          [fd = std::move(fd), shutdown_receiver = std::move(shutdown_receiver),
           server_fd = std::move(server_fd)]() {
              enum {
                  FdServer,
                  FdShutdown,
                  FdCount,
              };

              struct pollfd fds[FdCount];
              fds[FdServer].fd     = server_fd.get();
              fds[FdServer].events = POLLIN;

              fds[FdShutdown].fd     = shutdown_receiver.get();
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

                  if (fds[FdShutdown].revents)
                      break;  // shutdown requested by main thread

                  if (fds[FdServer].revents & POLLIN)
                  {
                    // Another fish wants access
#if !defined(__APPLE__)
                      UniqueFd client_fd(accept4(server_fd.get(), nullptr, nullptr, SOCK_CLOEXEC));
#else
                      UniqueFd client_fd(accept(server_fd.get(), nullptr, nullptr));
                      set_cloexec(client_fd.get());
#endif
                      if (!client_fd.is_valid())
                          continue;  // including EINTR

                      struct msghdr   msg        = {};
                      const usize     space      = CMSG_SPACE(sizeof(int));
                      constexpr usize alignement = alignof(struct cmsghdr);
                      auto            v = std::make_unique<std::byte[]>(space + alignement);
                      std::byte*      control_msg_buffer = align_ptr_up<alignement>(v.get());


                      char  buf[1] = {};
                      iovec iov[1];
                      iov[0].iov_base = buf;
                      iov[0].iov_len  = 1;

                      msg.msg_iov        = iov;
                      msg.msg_iovlen     = 1;
                      msg.msg_control    = control_msg_buffer;
                      msg.msg_controllen = space;

                      // Send over rights to the memfd (SCM_RIGHTS). The fd may be given a different number, but
                      // will refer to the same underlying file. Once it's mmapped then it will share physical memory
                      // between the processes.
                      // See https://man7.org/linux/man-pages/man7/unix.7.html for more information on SCM_RIGHTS
                      int             raw_fd = fd.get();
                      struct cmsghdr* cmsg   = CMSG_FIRSTHDR(&msg);
                      cmsg->cmsg_level       = SOL_SOCKET;
                      cmsg->cmsg_type        = SCM_RIGHTS;
                      cmsg->cmsg_len         = CMSG_LEN(sizeof(raw_fd));
                      memcpy(CMSG_DATA(cmsg), &raw_fd, sizeof(raw_fd));

#ifdef SO_NOSIGPIPE
                      int yes = 1;
                      setsockopt(client_fd.get(), SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif

#ifdef MSG_NOSIGNAL
                      int flags = MSG_NOSIGNAL;
#else
                      int flags = 0;
#endif
                      while (sendmsg(client_fd.get(), &msg, flags) < 0 && errno == EINTR)
                      {}
                  }
              }
          });
    }

   public:
    [[nodiscard]] bool open(const T& initial_value) noexcept {
        if (socket_path_.size() >= sizeof(sockaddr_un::sun_path))
            return false;

        if (mkdir(shared_dir_.c_str(), 0700) != 0 && errno != EEXIST)
            return false;

        {
            auto initLock = InitLock::wait_for_init_lock(init_lock_path_);
            if (!initLock.is_valid())
                return false;

            // Try to receive the shared memfd
            UniqueFd                 memfd;
            std::vector<std::string> peer_sockets = get_peer_sockets();
            for (const auto& sock_path : peer_sockets)
            {
                memfd = try_receive_memfd(sock_path);
                if (memfd.is_valid())
                    break;
            }

            const bool creator = !memfd.is_valid();  // We must create it

            if (creator)
            {
#if defined(MFD_CLOEXEC)
                // Failed to get it from a peer (no peers, or only dead peers), so create
                memfd.reset(memfd_create("replicated_data", MFD_CLOEXEC));
                if (!memfd.is_valid())
                    return false;
#else
                char temp_path[PATH_MAX];
                strncpy(temp_path, "/tmp/stockfish_replicated_data.XXXXXX", PATH_MAX);

                memfd.reset(mkstemp(temp_path));
                if (!memfd.is_valid())
                    return false;
                set_cloexec(memfd.get());
                unlink(temp_path);
#endif

                if (ftruncate(memfd.get(), sizeof(T)) != 0)
                    return false;
            }

            assert(memfd.is_valid());

            // Try to map the memfd
            T* mapped_mem = static_cast<T*>(detail::map_shared(memfd.get(), sizeof(T)));
            if (mapped_mem == MAP_FAILED)
                return false;

#ifdef MADV_HUGEPAGE
            (void) madvise(mapped_mem, sizeof(T), MADV_HUGEPAGE);
#endif

            if (creator)
            {
                // Creator is responsible for initialization
                *mapped_mem = initial_value;

#if defined(__linux__)
                (void) madvise(mapped_mem, sizeof(T), MADV_COLLAPSE);
#endif
            }

            mapped_ptr_ = data_ptr_ = mapped_mem;

            int shutdown_pipe[2];
#if !defined(__APPLE__)
            if (pipe2(shutdown_pipe, O_CLOEXEC) != 0)
                return false;
#else
            if (pipe(shutdown_pipe) != 0)
                return false;
            set_cloexec(shutdown_pipe[0]);
            set_cloexec(shutdown_pipe[1]);
#endif
            UniqueFd shutdown_receiver(shutdown_pipe[0]);
            shutdown_ = UniqueFd(shutdown_pipe[1]);

            auto server_fd = create_unix_socket();
            if (!server_fd.is_valid())
                return false;

            struct sockaddr_un addr = {};
            addr.sun_family         = AF_UNIX;
            strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

            detail::SharedMemoryRegistry::register_socket_path(socket_path_);
            unlink(socket_path_.c_str());
            if (bind(server_fd.get(), reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == -1
                || listen(server_fd.get(), 5) == -1)
            {
                return false;
            }

            // Don't release the init lock until we've actually made a socket that
            // other fishes can use.
            server_thread_ = make_server_thread(std::move(memfd), std::move(shutdown_receiver),
                                                std::move(server_fd));
            if (!server_thread_.joinable())
            {
                return false;
            }
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

#endif  // #ifndef SHM_UNIX_H_INCLUDED
