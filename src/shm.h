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

#ifndef SHM_H_INCLUDED
#define SHM_H_INCLUDED

#include <algorithm>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#if defined(__linux__) && !defined(__ANDROID__)
    #include "shm_linux.h"
#endif

#if defined(__ANDROID__)
    #include <limits.h>
    #define SF_MAX_SEM_NAME_LEN NAME_MAX
#endif

#include "types.h"

#include "memory.h"

#if defined(_WIN32)

    #if _WIN32_WINNT < 0x0601
        #undef _WIN32_WINNT
        #define _WIN32_WINNT 0x0601  // Force to include needed API prototypes
    #endif

    #if !defined(NOMINMAX)
        #define NOMINMAX
    #endif
    #include <windows.h>
#elif defined(__linux__)
    #include <cstring>
    #include <fcntl.h>
    #include <pthread.h>
    #include <semaphore.h>
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif


#if defined(__APPLE__)
    #include <mach-o/dyld.h>
    #include <sys/syslimits.h>

#elif defined(__sun)
    #include <stdlib.h>

#elif defined(__FreeBSD__)
    #include <sys/sysctl.h>
    #include <sys/types.h>
    #include <unistd.h>

#elif defined(__NetBSD__) || defined(__DragonFly__) || defined(__linux__)
    #include <limits.h>
    #include <unistd.h>
#endif


namespace Stockfish {

// argv[0] CANNOT be used because we need to identify the executable.
// argv[0] contains the command used to invoke it, which does not involve the full path.
// Just using a path is not fully resilient either, as the executable could
// have changed if it wasn't locked by the OS. Ideally we would hash the executable
// but it's not really that important at this point.
// If the path is longer than 4095 bytes the hash will be computed from an unspecified
// amount of bytes of the path; in particular it can a hash of an empty string.

inline std::string getExecutablePathHash() {
    char        executable_path[4096] = {0};
    std::size_t path_length           = 0;

#if defined(_WIN32)
    path_length = GetModuleFileNameA(NULL, executable_path, sizeof(executable_path));

#elif defined(__APPLE__)
    uint32_t size = sizeof(executable_path);
    if (_NSGetExecutablePath(executable_path, &size) == 0)
    {
        path_length = std::strlen(executable_path);
    }

#elif defined(__sun)  // Solaris
    const char* path = getexecname();
    if (path)
    {
        std::strncpy(executable_path, path, sizeof(executable_path) - 1);
        path_length = std::strlen(executable_path);
    }

#elif defined(__FreeBSD__)
    size_t size   = sizeof(executable_path);
    int    mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};
    if (sysctl(mib, 4, executable_path, &size, NULL, 0) == 0)
    {
        path_length = std::strlen(executable_path);
    }

#elif defined(__NetBSD__) || defined(__DragonFly__)
    ssize_t len = readlink("/proc/curproc/exe", executable_path, sizeof(executable_path) - 1);
    if (len >= 0)
    {
        executable_path[len] = '\0';
        path_length          = len;
    }

#elif defined(__linux__)
    ssize_t len = readlink("/proc/self/exe", executable_path, sizeof(executable_path) - 1);
    if (len >= 0)
    {
        executable_path[len] = '\0';
        path_length          = len;
    }

#endif

    // In case of any error the path will be empty.
    return std::string(executable_path, path_length);
}

enum class SystemWideSharedConstantAllocationStatus {
    NoAllocation,
    LocalMemory,
    SharedMemory
};

#if defined(_WIN32)

inline std::string GetLastErrorAsString(DWORD error) {
    //Get the error message ID, if any.
    DWORD errorMessageID = error;
    if (errorMessageID == 0)
    {
        return std::string();  //No error message has been recorded
    }

    LPSTR messageBuffer = nullptr;

    //Ask Win32 to give us the string version of that message ID.
    //The parameters we pass in, tell Win32 to create the buffer that holds the message for us (because we don't yet know how long the message string will be).
    size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                                   | FORMAT_MESSAGE_IGNORE_INSERTS,
                                 NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                 (LPSTR) &messageBuffer, 0, NULL);

    //Copy the error message into a std::string.
    std::string message(messageBuffer, size);

    //Free the Win32's string's buffer.
    LocalFree(messageBuffer);

    return message;
}

// Utilizes shared memory to store the value. It is deduplicated system-wide (for the single user).
template<typename T>
class SharedMemoryBackend {
   public:
    enum class Status {
        Success,
        LargePageAllocationError,
        FileMappingError,
        MapViewError,
        MutexCreateError,
        MutexWaitError,
        MutexReleaseError,
        NotInitialized
    };

    static constexpr DWORD IS_INITIALIZED_VALUE = 1;

    SharedMemoryBackend() :
        status(Status::NotInitialized) {};

    SharedMemoryBackend(const std::string& shm_name, const T& value) :
        status(Status::NotInitialized) {

        initialize(shm_name, value);
    }

    bool is_valid() const { return status == Status::Success; }

    std::optional<std::string> get_error_message() const {
        switch (status)
        {
        case Status::Success :
            return std::nullopt;
        case Status::LargePageAllocationError :
            return "Failed to allocate large page memory";
        case Status::FileMappingError :
            return "Failed to create file mapping: " + last_error_message;
        case Status::MapViewError :
            return "Failed to map view: " + last_error_message;
        case Status::MutexCreateError :
            return "Failed to create mutex: " + last_error_message;
        case Status::MutexWaitError :
            return "Failed to wait on mutex: " + last_error_message;
        case Status::MutexReleaseError :
            return "Failed to release mutex: " + last_error_message;
        case Status::NotInitialized :
            return "Not initialized";
        default :
            return "Unknown error";
        }
    }

    void* get() const { return is_valid() ? pMap : nullptr; }

    ~SharedMemoryBackend() { cleanup(); }

    SharedMemoryBackend(const SharedMemoryBackend&)            = delete;
    SharedMemoryBackend& operator=(const SharedMemoryBackend&) = delete;

    SharedMemoryBackend(SharedMemoryBackend&& other) noexcept :
        pMap(other.pMap),
        hMapFile(other.hMapFile),
        status(other.status),
        last_error_message(std::move(other.last_error_message)) {

        other.pMap     = nullptr;
        other.hMapFile = 0;
        other.status   = Status::NotInitialized;
    }

    SharedMemoryBackend& operator=(SharedMemoryBackend&& other) noexcept {
        if (this != &other)
        {
            cleanup();
            pMap               = other.pMap;
            hMapFile           = other.hMapFile;
            status             = other.status;
            last_error_message = std::move(other.last_error_message);

            other.pMap     = nullptr;
            other.hMapFile = 0;
            other.status   = Status::NotInitialized;
        }
        return *this;
    }

    SystemWideSharedConstantAllocationStatus get_status() const {
        return status == Status::Success ? SystemWideSharedConstantAllocationStatus::SharedMemory
                                         : SystemWideSharedConstantAllocationStatus::NoAllocation;
    }

   private:
    void initialize(const std::string& shm_name, const T& value) {
        const size_t total_size = sizeof(T) + sizeof(IS_INITIALIZED_VALUE);

        // Try allocating with large pages first.
        hMapFile = windows_try_with_large_page_priviliges(
          [&](size_t largePageSize) {
              const size_t total_size_aligned =
                (total_size + largePageSize - 1) / largePageSize * largePageSize;

    #if defined(_WIN64)
              DWORD total_size_low  = total_size_aligned & 0xFFFFFFFFu;
              DWORD total_size_high = total_size_aligned >> 32u;
    #else
              DWORD total_size_low  = total_size_aligned;
              DWORD total_size_high = 0;
    #endif

              return CreateFileMappingA(INVALID_HANDLE_VALUE, NULL,
                                        PAGE_READWRITE | SEC_COMMIT | SEC_LARGE_PAGES,
                                        total_size_high, total_size_low, shm_name.c_str());
          },
          []() { return (void*) nullptr; });

        // Fallback to normal allocation if no large pages available.
        if (!hMapFile)
        {
            hMapFile = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                          static_cast<DWORD>(total_size), shm_name.c_str());
        }

        if (!hMapFile)
        {
            const DWORD err    = GetLastError();
            last_error_message = GetLastErrorAsString(err);
            status             = Status::FileMappingError;
            return;
        }

        pMap = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, total_size);
        if (!pMap)
        {
            const DWORD err    = GetLastError();
            last_error_message = GetLastErrorAsString(err);
            status             = Status::MapViewError;
            cleanup_partial();
            return;
        }

        // Use named mutex to ensure only one initializer
        std::string mutex_name = shm_name + "$mutex";
        HANDLE      hMutex     = CreateMutexA(NULL, FALSE, mutex_name.c_str());
        if (!hMutex)
        {
            const DWORD err    = GetLastError();
            last_error_message = GetLastErrorAsString(err);
            status             = Status::MutexCreateError;
            cleanup_partial();
            return;
        }

        DWORD wait_result = WaitForSingleObject(hMutex, INFINITE);
        if (wait_result != WAIT_OBJECT_0)
        {
            const DWORD err    = GetLastError();
            last_error_message = GetLastErrorAsString(err);
            status             = Status::MutexWaitError;
            CloseHandle(hMutex);
            cleanup_partial();
            return;
        }

        // Crucially, we place the object first to ensure alignment.
        volatile DWORD* is_initialized =
          std::launder(reinterpret_cast<DWORD*>(reinterpret_cast<char*>(pMap) + sizeof(T)));
        T* object = std::launder(reinterpret_cast<T*>(pMap));

        if (*is_initialized != IS_INITIALIZED_VALUE)
        {
            // First time initialization, message for debug purposes
            new (object) T{value};
            *is_initialized = IS_INITIALIZED_VALUE;
        }

        BOOL release_result = ReleaseMutex(hMutex);
        CloseHandle(hMutex);

        if (!release_result)
        {
            const DWORD err    = GetLastError();
            last_error_message = GetLastErrorAsString(err);
            status             = Status::MutexReleaseError;
            cleanup_partial();
            return;
        }

        status = Status::Success;
    }

    void cleanup_partial() {
        if (pMap != nullptr)
        {
            UnmapViewOfFile(pMap);
            pMap = nullptr;
        }
        if (hMapFile)
        {
            CloseHandle(hMapFile);
            hMapFile = 0;
        }
    }

    void cleanup() {
        if (pMap != nullptr)
        {
            UnmapViewOfFile(pMap);
            pMap = nullptr;
        }
        if (hMapFile)
        {
            CloseHandle(hMapFile);
            hMapFile = 0;
        }
    }

    void*       pMap     = nullptr;
    HANDLE      hMapFile = 0;
    Status      status   = Status::NotInitialized;
    std::string last_error_message;
};

#elif defined(__linux__) && !defined(__ANDROID__)

template<typename T>
class SharedMemoryBackend {
   public:
    SharedMemoryBackend() = default;

    SharedMemoryBackend(const std::string& shm_name, const T& value) :
        shm1(shm::create_shared<T>(shm_name, value)) {}

    void* get() const {
        const T* ptr = &shm1->get();
        return reinterpret_cast<void*>(const_cast<T*>(ptr));
    }

    bool is_valid() const { return shm1 && shm1->is_open() && shm1->is_initialized(); }

    SystemWideSharedConstantAllocationStatus get_status() const {
        return is_valid() ? SystemWideSharedConstantAllocationStatus::SharedMemory
                          : SystemWideSharedConstantAllocationStatus::NoAllocation;
    }

    std::optional<std::string> get_error_message() const {
        if (!shm1)
            return "Shared memory not initialized";

        if (!shm1->is_open())
            return "Shared memory is not open";

        if (!shm1->is_initialized())
            return "Not initialized";

        return std::nullopt;
    }

   private:
    std::optional<shm::SharedMemory<T>> shm1;
};

#else

// For systems that don't have shared memory, or support is troublesome.
// The way fallback is done is that we need a dummy backend.

template<typename T>
class SharedMemoryBackend {
   public:
    SharedMemoryBackend() = default;

    SharedMemoryBackend([[maybe_unused]] const std::string& shm_name,
                        [[maybe_unused]] const T&           value) {}

    void* get() const { return nullptr; }

    bool is_valid() const { return false; }

    SystemWideSharedConstantAllocationStatus get_status() const {
        return SystemWideSharedConstantAllocationStatus::NoAllocation;
    }

    std::optional<std::string> get_error_message() const { return "Dummy SharedMemoryBackend"; }
};

#endif

template<typename T>
struct SharedMemoryBackendFallback {
    SharedMemoryBackendFallback() = default;

    SharedMemoryBackendFallback(const std::string&, const T& value) :
        fallback_object(make_unique_large_page<T>(value)) {}

    void* get() const { return fallback_object.get(); }

    SharedMemoryBackendFallback(const SharedMemoryBackendFallback&)            = delete;
    SharedMemoryBackendFallback& operator=(const SharedMemoryBackendFallback&) = delete;

    SharedMemoryBackendFallback(SharedMemoryBackendFallback&& other) noexcept :
        fallback_object(std::move(other.fallback_object)) {}

    SharedMemoryBackendFallback& operator=(SharedMemoryBackendFallback&& other) noexcept {
        fallback_object = std::move(other.fallback_object);
        return *this;
    }

    SystemWideSharedConstantAllocationStatus get_status() const {
        return fallback_object == nullptr ? SystemWideSharedConstantAllocationStatus::NoAllocation
                                          : SystemWideSharedConstantAllocationStatus::LocalMemory;
    }

    std::optional<std::string> get_error_message() const {
        if (fallback_object == nullptr)
            return "Not initialized";

        return "Shared memory not supported by the OS. Local allocation fallback.";
    }

   private:
    LargePagePtr<T> fallback_object;
};

// Platform-independent wrapper
template<typename T>
struct SystemWideSharedConstant {
   private:
    static std::string createHashString(const std::string& input) {
        char buf[1024];
        std::snprintf(buf, sizeof(buf), "%016" PRIx64, hash_string(input));
        return buf;
    }

   public:
    // We can't run the destructor because it may be in a completely different process.
    // The object stored must also be obviously in-line but we can't check for that, other than some basic checks that cover most cases.
    static_assert(std::is_trivially_destructible_v<T>);
    static_assert(std::is_trivially_move_constructible_v<T>);
    static_assert(std::is_trivially_copy_constructible_v<T>);

    SystemWideSharedConstant() = default;


    // Content is addressed by its hash. An additional discriminator can be added to account for differences
    // that are not present in the content, for example NUMA node allocation.
    SystemWideSharedConstant(const T& value, std::size_t discriminator = 0) {
        std::size_t content_hash    = std::hash<T>{}(value);
        std::size_t executable_hash = hash_string(getExecutablePathHash());

        char buf[1024];
        std::snprintf(buf, sizeof(buf), "Local\\sf_%zu$%zu$%zu", content_hash, executable_hash, discriminator);
        std::string shm_name = buf;

#if defined(__linux__) && !defined(__ANDROID__)
        // POSIX shared memory names must start with a slash
        shm_name = "/sf_" + createHashString(shm_name);

        // hash name and make sure it is not longer than SF_MAX_SEM_NAME_LEN
        if (shm_name.size() > SF_MAX_SEM_NAME_LEN)
        {
            shm_name = shm_name.substr(0, SF_MAX_SEM_NAME_LEN - 1);
        }
#endif

        SharedMemoryBackend<T> shm_backend(shm_name, value);

        if (shm_backend.is_valid())
        {
            backend = std::move(shm_backend);
        }
        else
        {
            backend = SharedMemoryBackendFallback<T>(shm_name, value);
        }
    }

    SystemWideSharedConstant(const SystemWideSharedConstant&)            = delete;
    SystemWideSharedConstant& operator=(const SystemWideSharedConstant&) = delete;

    SystemWideSharedConstant(SystemWideSharedConstant&& other) noexcept :
        backend(std::move(other.backend)) {}

    SystemWideSharedConstant& operator=(SystemWideSharedConstant&& other) noexcept {
        backend = std::move(other.backend);
        return *this;
    }

    const T& operator*() const { return *std::launder(reinterpret_cast<const T*>(get_ptr())); }

    bool operator==(std::nullptr_t) const noexcept { return get_ptr() == nullptr; }

    bool operator!=(std::nullptr_t) const noexcept { return get_ptr() != nullptr; }

    SystemWideSharedConstantAllocationStatus get_status() const {
        return std::visit(
          [](const auto& end) -> SystemWideSharedConstantAllocationStatus {
              if constexpr (std::is_same_v<std::decay_t<decltype(end)>, std::monostate>)
              {
                  return SystemWideSharedConstantAllocationStatus::NoAllocation;
              }
              else
              {
                  return end.get_status();
              }
          },
          backend);
    }

    std::optional<std::string> get_error_message() const {
        return std::visit(
          [](const auto& end) -> std::optional<std::string> {
              if constexpr (std::is_same_v<std::decay_t<decltype(end)>, std::monostate>)
              {
                  return std::nullopt;
              }
              else
              {
                  return end.get_error_message();
              }
          },
          backend);
    }

   private:
    auto get_ptr() const {
        return std::visit(
          [](const auto& end) -> void* {
              if constexpr (std::is_same_v<std::decay_t<decltype(end)>, std::monostate>)
              {
                  return nullptr;
              }
              else
              {
                  return end.get();
              }
          },
          backend);
    }

    std::variant<std::monostate, SharedMemoryBackend<T>, SharedMemoryBackendFallback<T>> backend;
};


}  // namespace Stockfish

#endif  // #ifndef SHM_H_INCLUDED
