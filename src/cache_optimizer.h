#ifndef CACHE_OPTIMIZER_H_INCLUDED
#define CACHE_OPTIMIZER_H_INCLUDED

#include <cstddef>
#include <new>

namespace Stockfish {

template<typename T, size_t CacheLineSize = 64>
class CacheAlignedArray {
    static constexpr size_t alignment = CacheLineSize;
    T* data;
    size_t length;

public:
    explicit CacheAlignedArray(size_t size) : length(size) {
        data = static_cast<T*>(::operator new(size * sizeof(T) + alignment, std::align_val_t(alignment)));
    }

    ~CacheAlignedArray() {
        ::operator delete(data, std::align_val_t(alignment));
    }

    T& operator[](size_t index) { return data[index]; }
    const T& operator[](size_t index) const { return data[index]; }
    T* get() { return data; }
    const T* get() const { return data; }
    size_t size() const { return length; }
};

struct CacheOptimizer {
    static constexpr size_t CACHE_LINE_SIZE = 64;
    static constexpr size_t L1_CACHE_SIZE = 32768;
    static constexpr size_t L2_CACHE_SIZE = 262144;
    static constexpr size_t L3_CACHE_SIZE = 8388608;

    template<typename T>
    static void prefetchL1(const T* addr) {
        __builtin_prefetch(addr, 0, 3);
    }

    template<typename T>
    static void prefetchL2(const T* addr) {
        __builtin_prefetch(addr, 0, 2);
    }

    template<typename T>
    static void prefetchForModify(const T* addr) {
        __builtin_prefetch(addr, 1, 3);
    }
};

} 

#endif
