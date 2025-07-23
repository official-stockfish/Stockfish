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

#ifndef MEMORY_H_INCLUDED
#define MEMORY_H_INCLUDED

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <new>

#include <iostream>

#include "types.h"

#if defined(_WIN64)

    #if _WIN32_WINNT < 0x0601
        #undef _WIN32_WINNT
        #define _WIN32_WINNT 0x0601  // Force to include needed API prototypes
    #endif

    #if !defined(NOMINMAX)
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <psapi.h>

extern "C" {
using OpenProcessToken_t      = bool (*)(HANDLE, DWORD, PHANDLE);
using LookupPrivilegeValueA_t = bool (*)(LPCSTR, LPCSTR, PLUID);
using AdjustTokenPrivileges_t =
  bool (*)(HANDLE, BOOL, PTOKEN_PRIVILEGES, DWORD, PTOKEN_PRIVILEGES, PDWORD);
}
#endif

namespace Stockfish {

void* std_aligned_alloc(size_t alignment, size_t size);
void  std_aligned_free(void* ptr);

// Memory aligned by page size, min alignment: 4096 bytes
void* aligned_large_pages_alloc(size_t size);
void  aligned_large_pages_free(void* mem);

bool has_large_pages();

// Frees memory which was placed there with placement new.
// Works for both single objects and arrays of unknown bound.
template<typename T, typename FREE_FUNC>
void memory_deleter(T* ptr, FREE_FUNC free_func) {
    if (!ptr)
        return;

    // Explicitly needed to call the destructor
    if constexpr (!std::is_trivially_destructible_v<T>)
        ptr->~T();

    free_func(ptr);
}

// Frees memory which was placed there with placement new.
// Works for both single objects and arrays of unknown bound.
template<typename T, typename FREE_FUNC>
void memory_deleter_array(T* ptr, FREE_FUNC free_func) {
    if (!ptr)
        return;


    // Move back on the pointer to where the size is allocated
    const size_t array_offset = std::max(sizeof(size_t), alignof(T));
    char*        raw_memory   = reinterpret_cast<char*>(ptr) - array_offset;

    if constexpr (!std::is_trivially_destructible_v<T>)
    {
        const size_t size = *reinterpret_cast<size_t*>(raw_memory);

        // Explicitly call the destructor for each element in reverse order
        for (size_t i = size; i-- > 0;)
            ptr[i].~T();
    }

    free_func(raw_memory);
}

// Allocates memory for a single object and places it there with placement new
template<typename T, typename ALLOC_FUNC, typename... Args>
inline std::enable_if_t<!std::is_array_v<T>, T*> memory_allocator(ALLOC_FUNC alloc_func,
                                                                  Args&&... args) {
    void* raw_memory = alloc_func(sizeof(T));
    ASSERT_ALIGNED(raw_memory, alignof(T));
    return new (raw_memory) T(std::forward<Args>(args)...);
}

// Allocates memory for an array of unknown bound and places it there with placement new
template<typename T, typename ALLOC_FUNC>
inline std::enable_if_t<std::is_array_v<T>, std::remove_extent_t<T>*>
memory_allocator(ALLOC_FUNC alloc_func, size_t num) {
    using ElementType = std::remove_extent_t<T>;

    const size_t array_offset = std::max(sizeof(size_t), alignof(ElementType));

    // Save the array size in the memory location
    char* raw_memory =
      reinterpret_cast<char*>(alloc_func(array_offset + num * sizeof(ElementType)));
    ASSERT_ALIGNED(raw_memory, alignof(T));

    new (raw_memory) size_t(num);

    for (size_t i = 0; i < num; ++i)
        new (raw_memory + array_offset + i * sizeof(ElementType)) ElementType();

    // Need to return the pointer at the start of the array so that
    // the indexing in unique_ptr<T[]> works.
    return reinterpret_cast<ElementType*>(raw_memory + array_offset);
}

//
//
// aligned large page unique ptr
//
//

template<typename T>
struct LargePageDeleter {
    void operator()(T* ptr) const { return memory_deleter<T>(ptr, aligned_large_pages_free); }
};

template<typename T>
struct LargePageArrayDeleter {
    void operator()(T* ptr) const { return memory_deleter_array<T>(ptr, aligned_large_pages_free); }
};

template<typename T>
using LargePagePtr =
  std::conditional_t<std::is_array_v<T>,
                     std::unique_ptr<T, LargePageArrayDeleter<std::remove_extent_t<T>>>,
                     std::unique_ptr<T, LargePageDeleter<T>>>;

// make_unique_large_page for single objects
template<typename T, typename... Args>
std::enable_if_t<!std::is_array_v<T>, LargePagePtr<T>> make_unique_large_page(Args&&... args) {
    static_assert(alignof(T) <= 4096,
                  "aligned_large_pages_alloc() may fail for such a big alignment requirement of T");

    T* obj = memory_allocator<T>(aligned_large_pages_alloc, std::forward<Args>(args)...);

    return LargePagePtr<T>(obj);
}

// make_unique_large_page for arrays of unknown bound
template<typename T>
std::enable_if_t<std::is_array_v<T>, LargePagePtr<T>> make_unique_large_page(size_t num) {
    using ElementType = std::remove_extent_t<T>;

    static_assert(alignof(ElementType) <= 4096,
                  "aligned_large_pages_alloc() may fail for such a big alignment requirement of T");

    ElementType* memory = memory_allocator<T>(aligned_large_pages_alloc, num);

    return LargePagePtr<T>(memory);
}

//
//
// aligned unique ptr
//
//

template<typename T>
struct AlignedDeleter {
    void operator()(T* ptr) const { return memory_deleter<T>(ptr, std_aligned_free); }
};

template<typename T>
struct AlignedArrayDeleter {
    void operator()(T* ptr) const { return memory_deleter_array<T>(ptr, std_aligned_free); }
};

template<typename T>
using AlignedPtr =
  std::conditional_t<std::is_array_v<T>,
                     std::unique_ptr<T, AlignedArrayDeleter<std::remove_extent_t<T>>>,
                     std::unique_ptr<T, AlignedDeleter<T>>>;

// make_unique_aligned for single objects
template<typename T, typename... Args>
std::enable_if_t<!std::is_array_v<T>, AlignedPtr<T>> make_unique_aligned(Args&&... args) {
    const auto func = [](size_t size) { return std_aligned_alloc(alignof(T), size); };
    T*         obj  = memory_allocator<T>(func, std::forward<Args>(args)...);

    return AlignedPtr<T>(obj);
}

// make_unique_aligned for arrays of unknown bound
template<typename T>
std::enable_if_t<std::is_array_v<T>, AlignedPtr<T>> make_unique_aligned(size_t num) {
    using ElementType = std::remove_extent_t<T>;

    const auto   func   = [](size_t size) { return std_aligned_alloc(alignof(ElementType), size); };
    ElementType* memory = memory_allocator<T>(func, num);

    return AlignedPtr<T>(memory);
}


// Get the first aligned element of an array.
// ptr must point to an array of size at least `sizeof(T) * N + alignment` bytes,
// where N is the number of elements in the array.
template<uintptr_t Alignment, typename T>
T* align_ptr_up(T* ptr) {
    static_assert(alignof(T) < Alignment);

    const uintptr_t ptrint = reinterpret_cast<uintptr_t>(reinterpret_cast<char*>(ptr));
    return reinterpret_cast<T*>(
      reinterpret_cast<char*>((ptrint + (Alignment - 1)) / Alignment * Alignment));
}

inline std::string GetLastErrorAsString(DWORD error)
{
    //Get the error message ID, if any.
    DWORD errorMessageID = error;
    if(errorMessageID == 0) {
        return std::string(); //No error message has been recorded
    }
    
    LPSTR messageBuffer = nullptr;

    //Ask Win32 to give us the string version of that message ID.
    //The parameters we pass in, tell Win32 to create the buffer that holds the message for us (because we don't yet know how long the message string will be).
    size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                 NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);
    
    //Copy the error message into a std::string.
    std::string message(messageBuffer, size);
    
    //Free the Win32's string's buffer.
    LocalFree(messageBuffer);
            
    return message;
}

#if defined(_WIN32)

template <typename FuncYesT, typename FuncNoT>
auto windows_try_with_large_page_priviliges(FuncYesT&& fyes, FuncNoT&& fno) {

    #if !defined(_WIN64)
    return fno();
    #else

    HANDLE hProcessToken{};
    LUID   luid{};

    const size_t largePageSize = GetLargePageMinimum();
    if (!largePageSize)
        return fno();

    // Dynamically link OpenProcessToken, LookupPrivilegeValue and AdjustTokenPrivileges

    HMODULE hAdvapi32 = GetModuleHandle(TEXT("advapi32.dll"));

    if (!hAdvapi32)
        hAdvapi32 = LoadLibrary(TEXT("advapi32.dll"));

    auto OpenProcessToken_f =
      OpenProcessToken_t((void (*)()) GetProcAddress(hAdvapi32, "OpenProcessToken"));
    if (!OpenProcessToken_f)
        return fno();
    auto LookupPrivilegeValueA_f =
      LookupPrivilegeValueA_t((void (*)()) GetProcAddress(hAdvapi32, "LookupPrivilegeValueA"));
    if (!LookupPrivilegeValueA_f)
        return fno();
    auto AdjustTokenPrivileges_f =
      AdjustTokenPrivileges_t((void (*)()) GetProcAddress(hAdvapi32, "AdjustTokenPrivileges"));
    if (!AdjustTokenPrivileges_f)
        return fno();

    // We need SeLockMemoryPrivilege, so try to enable it for the process

    if (!OpenProcessToken_f(  // OpenProcessToken()
          GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hProcessToken))
        return fno();

    if (!LookupPrivilegeValueA_f(nullptr, "SeLockMemoryPrivilege", &luid))
        return fno();

    TOKEN_PRIVILEGES tp{};
    TOKEN_PRIVILEGES prevTp{};
    DWORD            prevTpLen = 0;

    tp.PrivilegeCount           = 1;
    tp.Privileges[0].Luid       = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    // Try to enable SeLockMemoryPrivilege. Note that even if AdjustTokenPrivileges()
    // succeeds, we still need to query GetLastError() to ensure that the privileges
    // were actually obtained.

    if (!AdjustTokenPrivileges_f(hProcessToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), &prevTp,
                                &prevTpLen)
        || GetLastError() != ERROR_SUCCESS)
        return fno();

    auto&& ret = fyes(largePageSize);

    // Privilege no longer needed, restore previous state
    AdjustTokenPrivileges_f(hProcessToken, FALSE, &prevTp, 0, nullptr, nullptr);

    CloseHandle(hProcessToken);

    return std::forward<decltype(ret)>(ret);

    #endif
}

#endif

template <typename T>
struct SystemWideSharedConstant {
    // We can't run the destructor because it may be in a completely different process.
    // The object stored must also be obviously in-line but we can't check for that, other than some basic checks that cover most cases.
    static_assert(std::is_trivially_destructible_v<T>);
    static_assert(std::is_trivially_move_constructible_v<T>);
    static_assert(std::is_trivially_copy_constructible_v<T>);

    static constexpr DWORD IS_INITIALIZED_VALUE = 1;

    SystemWideSharedConstant() = default;

    SystemWideSharedConstant(const T& value, std::size_t discriminator = 0) {

        const size_t total_size = sizeof(T) + sizeof(DWORD);

        std::size_t content_hash = std::hash<T>{}(value);

        char executable_path[4096] = {0};
        GetProcessImageFileName(GetCurrentProcess(), executable_path, sizeof(executable_path));
        std::size_t executable_hash = std::hash<std::string>{}(std::string(executable_path, 4096));

        std::string shm_name = std::string("Local\\") + std::to_string(content_hash) + "$" + std::to_string(executable_hash) + "$" + std::to_string(discriminator);

        hMapFile = windows_try_with_large_page_priviliges([&](size_t largePageSize){
            const size_t total_size_aligned = (total_size + largePageSize - 1) / largePageSize * largePageSize;
            return CreateFileMappingA(
                INVALID_HANDLE_VALUE,
                NULL,
                PAGE_READWRITE | SEC_COMMIT | SEC_LARGE_PAGES,
                static_cast<DWORD>(total_size_aligned >> 32u),
                static_cast<DWORD>(total_size_aligned & 0xFFFFFFFFu),
                shm_name.c_str()
            );
        }, []() { return (void*)nullptr; });

        if (!hMapFile) {
            hMapFile = CreateFileMappingA(
                INVALID_HANDLE_VALUE,
                NULL,
                PAGE_READWRITE,
                0,
                static_cast<DWORD>(total_size),
                shm_name.c_str()
            );
        }

        if (!hMapFile) {
            const DWORD err = GetLastError();
            std::cerr << "Failed to create file mapping: " << GetLastErrorAsString(err) << std::endl;
            std::terminate();
        }

        pMap = MapViewOfFile(
            hMapFile,
            FILE_MAP_ALL_ACCESS,
            0, 0,
            total_size
        );
        
        if (!pMap) {
            const DWORD err = GetLastError();
            std::cerr << "Failed to map view: " << GetLastErrorAsString(err) << std::endl;
            CloseHandle(hMapFile);
            std::terminate();
        }

        volatile DWORD* pInitialized = std::launder(reinterpret_cast<DWORD*>(reinterpret_cast<char*>(pMap) + sizeof(T)));
        T* pObject = std::launder(reinterpret_cast<T*>(pMap));

        // Use named mutex to ensure only one initializer
        std::string mutex_name = shm_name + "$mutex";
        HANDLE hMutex = CreateMutexA(NULL, FALSE, mutex_name.c_str());
        if (!hMutex) {
            const DWORD err = GetLastError();
            std::cerr << "Failed to create mutex: " << GetLastErrorAsString(err) << std::endl;
            UnmapViewOfFile(pMap);
            CloseHandle(hMapFile);
            std::terminate();
        }

        WaitForSingleObject(hMutex, INFINITE);

        if (*pInitialized != IS_INITIALIZED_VALUE) {
            // First time initialization, message for debug purposes
            std::cout << "initializing: " << shm_name << "\n";
            new (pObject) T{value};
            *pInitialized = IS_INITIALIZED_VALUE;
        } else {
            std::cout << "already initialized: " << shm_name << "\n";
        }

        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    SystemWideSharedConstant(const SystemWideSharedConstant& other) = delete;
    SystemWideSharedConstant(SystemWideSharedConstant&& other) : 
        pMap(other.pMap),
        hMapFile(other.hMapFile) {

        other.pMap = nullptr;
        other.hMapFile = 0;
    }
    SystemWideSharedConstant& operator=(const SystemWideSharedConstant& other) = delete;
    SystemWideSharedConstant& operator=(SystemWideSharedConstant&& other) {
        pMap = other.pMap;
        hMapFile = other.hMapFile;

        other.pMap = nullptr;
        other.hMapFile = 0;

        return *this;
    }

    const T& operator*() const {
        const T* pObject = std::launder(reinterpret_cast<const T*>(pMap));
        return *pObject;
    }

    // Should not exist but we're not really RAII with the network
    T& operator*() {
        T* pObject = std::launder(reinterpret_cast<T*>(pMap));
        return *pObject;
    }

    bool operator==(std::nullptr_t) const noexcept {
        return pMap == nullptr;
    }

    bool operator!=(std::nullptr_t) const noexcept {
        return pMap != nullptr;
    }

    ~SystemWideSharedConstant() {
        if (pMap != nullptr)
            UnmapViewOfFile(pMap);
        
        if (hMapFile)
            CloseHandle(hMapFile);
    }
private:
    // use shared_ptr for now for type-erased deleter
    void* pMap;
    HANDLE hMapFile;
};


}  // namespace Stockfish

#endif  // #ifndef MEMORY_H_INCLUDED
