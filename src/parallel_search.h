#ifndef PARALLEL_SEARCH_H_INCLUDED
#define PARALLEL_SEARCH_H_INCLUDED

#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include "types.h"
#include "position.h"
#include "thread.h"

namespace Stockfish {

class ParallelSearchManager {
    std::vector<std::unique_ptr<Thread>> threads;
    std::atomic<bool> searching{false};
    Depth splitDepth;
    
public:
    ParallelSearchManager(size_t numThreads = 1) : splitDepth(4) {
        threads.reserve(numThreads);
        for (size_t i = 0; i < numThreads; ++i)
            threads.emplace_back(std::make_unique<Thread>(i));
    }

    void startSearch(Position& pos, const Search::LimitsType& limits) {
        searching = true;
        
        for (auto& thread : threads) {
            thread->startSearching(pos, limits, searching);
        }
    }

    void waitForSearchFinish() {
        searching = false;
        for (auto& thread : threads)
            if (thread->stdThread.joinable())
                thread->stdThread.join();
    }

    void setSplitDepth(Depth depth) {
        splitDepth = depth;
    }

    Depth getSplitDepth() const {
        return splitDepth;
    }

    size_t getThreadCount() const {
        return threads.size();
    }

    void resizeThreadPool(size_t newSize) {
        if (searching)
            return;

        threads.clear();
        threads.reserve(newSize);
        for (size_t i = 0; i < newSize; ++i)
            threads.emplace_back(std::make_unique<Thread>(i));
    }
};

}
