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

#ifndef CHESSTB_CACHE_H_INCLUDED
#define CHESSTB_CACHE_H_INCLUDED

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace Stockfish::ChessTB {

inline constexpr size_t TL_OBJECT_CACHE_SLOTS = 16;
inline constexpr size_t TL_BLOCK_SHARDS       = 64;
static_assert((TL_OBJECT_CACHE_SLOTS & (TL_OBJECT_CACHE_SLOTS - 1)) == 0);
static_assert(TL_OBJECT_CACHE_SLOTS >= 1);
static_assert((TL_BLOCK_SHARDS & (TL_BLOCK_SHARDS - 1)) == 0);

inline uint64_t next_epoch() {
    static std::atomic<uint64_t> ctr{0};
    return ctr.fetch_add(1, std::memory_order_relaxed) + 1;
}

using Block_Ptr = std::shared_ptr<const std::vector<uint8_t>>;

class Block_Pool {
   public:
    // Null if not pooled.
    Block_Ptr find(const void* owner, size_t block_id, uint64_t shard_key) {
        const Key                           key{owner, block_id};
        Shard&                              shard = shard_for(shard_key);
        std::shared_lock<std::shared_mutex> lk(shard.mu);
        auto                                it = shard.index.find(key);
        if (it == shard.index.end())
            return nullptr;
        it->second->used.store(true, std::memory_order_relaxed);
        return it->second->blk;
    }

    Block_Ptr
    try_insert_or_get(const void* owner, size_t block_id, uint64_t shard_key, Block_Ptr blk) {
        const Key    key{owner, block_id};
        const size_t bytes = blk->size();
        Shard&       shard = shard_for(shard_key);

        std::unique_lock<std::shared_mutex> lk(shard.mu, std::try_to_lock);
        if (!lk.owns_lock())
            return blk;

        auto it = shard.index.find(key);
        if (it != shard.index.end())
        {
            it->second->used.store(true, std::memory_order_relaxed);
            return it->second->blk;
        }

        shard.ring.emplace_back();
        Entry& e = shard.ring.back();
        e.key    = key;
        e.blk    = blk;
        e.bytes  = bytes;
        shard.index.emplace(key, std::prev(shard.ring.end()));
        m_cur_bytes.fetch_add(bytes, std::memory_order_relaxed);
        evict_locked(shard);
        return blk;
    }

    void drop_owner(const void* owner) {
        for (Shard& shard : m_shards)
        {
            std::lock_guard<std::shared_mutex> lk(shard.mu);
            for (auto it = shard.ring.begin(); it != shard.ring.end();)
            {
                if (it->key.first != owner)
                {
                    ++it;
                    continue;
                }
                m_cur_bytes.fetch_sub(it->bytes, std::memory_order_relaxed);
                shard.index.erase(it->key);
                it = shard.ring.erase(it);
            }
        }
    }

    // Empties the pool, as a resize of the hash table clears it.
    void set_max_bytes(size_t n) {
        m_max_bytes.store(n, std::memory_order_relaxed);
        for (Shard& shard : m_shards)
        {
            std::lock_guard<std::shared_mutex> lk(shard.mu);
            for (const Entry& e : shard.ring)
                m_cur_bytes.fetch_sub(e.bytes, std::memory_order_relaxed);
            shard.index.clear();
            shard.ring.clear();
        }
    }

   private:
    using Key = std::pair<const void*, size_t>;

    struct Key_Hash {
        size_t operator()(const Key& k) const {
            return std::hash<const void*>()(k.first) * 1099511628211ull + k.second;
        }
    };

    struct Entry {
        Key               key;
        Block_Ptr         blk;
        size_t            bytes = 0;
        std::atomic<bool> used{false};
    };

    struct alignas(64) Shard {
        mutable std::shared_mutex                                     mu;
        std::list<Entry>                                              ring;  // front = the hand
        std::unordered_map<Key, std::list<Entry>::iterator, Key_Hash> index;
    };

    Shard& shard_for(uint64_t key) { return m_shards[key & (TL_BLOCK_SHARDS - 1)]; }

    void evict_locked(Shard& shard) {
        while (m_cur_bytes.load(std::memory_order_relaxed)
                 > m_max_bytes.load(std::memory_order_relaxed)
               && shard.ring.size() > 1)
        {
            Entry& e = shard.ring.front();
            if (e.used.exchange(false, std::memory_order_relaxed))
            {
                shard.ring.splice(shard.ring.end(), shard.ring, shard.ring.begin());
                continue;
            }
            m_cur_bytes.fetch_sub(e.bytes, std::memory_order_relaxed);
            shard.index.erase(e.key);
            shard.ring.pop_front();
        }
    }

    std::array<Shard, TL_BLOCK_SHARDS> m_shards;
    std::atomic<size_t>                m_max_bytes{0};
    std::atomic<size_t>                m_cur_bytes{0};
};

template<size_t N>
struct TL_Block_FIFO {
    static_assert(N >= 1);
    static_assert((N & (N - 1)) == 0);

    static constexpr size_t slots       = N;
    uint64_t                epoch[N]    = {};
    size_t                  block_id[N] = {};
    Block_Ptr               bytes[N];
    size_t                  next = 0;
};

template<typename K, typename V>
struct TL_Cache {
    static constexpr size_t N        = TL_OBJECT_CACHE_SLOTS;
    uint64_t                epoch[N] = {};
    K                       key[N]   = {};
    V                       val[N]   = {};
    size_t                  rr       = 0;

    const V* find(uint64_t impl_epoch, K k) const {
        for (size_t n = 1; n <= N; ++n)
        {
            const size_t i = (rr - n) & (N - 1);
            if (epoch[i] == impl_epoch && key[i] == k)
                return &val[i];
        }
        return nullptr;
    }

    void insert(uint64_t impl_epoch, K k, V v) {
        const size_t i = (rr++) & (N - 1);
        epoch[i]       = impl_epoch;
        key[i]         = k;
        val[i]         = std::move(v);
    }
};

struct Block_Cache {
    const uint64_t epoch = next_epoch();

    const std::shared_ptr<Block_Pool> pool;

    explicit Block_Cache(std::shared_ptr<Block_Pool> p) :
        pool(std::move(p)) {}

    ~Block_Cache() { pool->drop_owner(this); }

    Block_Cache(const Block_Cache&)            = delete;
    Block_Cache& operator=(const Block_Cache&) = delete;

    const void* pool_key() const { return this; }
};

template<typename Tag, bool Thread_Local_Only>
TL_Block_FIFO<Tag::TL_BLOCK_SLOTS>& tl_block_fifo(uint64_t key) {
    if constexpr (Thread_Local_Only)
    {
        thread_local std::array<TL_Block_FIFO<Tag::TL_BLOCK_SLOTS>, TL_BLOCK_SHARDS> tls;
        return tls[key & (TL_BLOCK_SHARDS - 1)];
    }
    else
    {
        thread_local TL_Block_FIFO<Tag::TL_BLOCK_SLOTS> tl;
        return tl;
    }
}

// The block's bytes: the thread's FIFO, else the shared pool (unless
// Thread_Local_Only), else build(cache, block_id).
template<typename Tag, bool Thread_Local_Only = false, typename Cache, typename Build>
const uint8_t* fetch_block_cached(Cache& cache, size_t block_id, Build&& build) {
    const Block_Cache& blocks = cache.cache;
    const uint64_t     key    = blocks.epoch ^ block_id;
    auto&              tl     = tl_block_fifo<Tag, Thread_Local_Only>(key);
    for (size_t n = 1; n <= tl.slots; ++n)
    {
        const size_t i = (tl.next - n) & (tl.slots - 1);
        if (tl.epoch[i] == blocks.epoch && tl.block_id[i] == block_id)
            return tl.bytes[i]->data();
    }

    Block_Ptr blk;
    if constexpr (Thread_Local_Only)
        blk = build(cache, block_id);
    else
    {
        blk = blocks.pool->find(blocks.pool_key(), block_id, key);
        if (!blk)
            blk = blocks.pool->try_insert_or_get(blocks.pool_key(), block_id, key,
                                                 build(cache, block_id));
    }

    const size_t s = (tl.next++) & (tl.slots - 1);
    tl.epoch[s]    = blocks.epoch;
    tl.block_id[s] = block_id;
    tl.bytes[s]    = blk;
    return blk->data();
}

}  // namespace Stockfish::ChessTB

#endif  // CHESSTB_CACHE_H_INCLUDED
