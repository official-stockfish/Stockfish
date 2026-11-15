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

#include "history.h"
#include "memory.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace {

using namespace Stockfish;

int checks   = 0;
int failures = 0;

void check(bool condition, const char* name) {
    ++checks;
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << name << '\n';
    }
}

struct alignas(128) Trivial {
    int value;
};

struct alignas(64) Constructed {
    inline static int constructions = 0;
    int               value;

    Constructed() :
        value(73) {
        ++constructions;
    }
};

struct alignas(64) Destructed {
    inline static int                constructions = 0;
    inline static int                destructions  = 0;
    inline static std::array<int, 3> order{};
    int                              id;

    Destructed() :
        id(constructions++) {}
    ~Destructed() { order[destructions++] = id; }
};

void test_trivial_array() {
    void* base      = nullptr;
    usize requested = 0;
    auto  allocate  = [&](usize bytes) {
        requested = bytes;
        base      = std_aligned_alloc(64, (bytes + 63) / 64 * 64);
        return base;
    };
    auto* values = memory_allocator<i16[]>(allocate, 32);
    check(requested == 64, "trivial arrays need no size header");
    check(values == base, "trivial arrays preserve the allocation base");
    for (usize i = 0; i < 32; ++i)
        check(values[i] == 0, "trivial array elements are value initialized");
    values[0]  = 17;
    values[31] = 23;
    check(values[0] + values[31] == 40, "first and last elements remain writable");
    memory_deleter_array(values, [&](void* ptr) {
        check(ptr == base, "trivial array deletion frees the allocation base");
        std_aligned_free(ptr);
    });
}

void test_overaligned_array() {
    auto values = make_unique_large_page<Trivial[]>(2);
    check(reinterpret_cast<std::uintptr_t>(values.get()) % 128 == 0,
          "overaligned trivial arrays preserve type alignment");
    check(reinterpret_cast<std::uintptr_t>(values.get()) % 4096 == 0,
          "overaligned trivial arrays preserve page alignment");
    check(values[0].value == 0 && values[1].value == 0,
          "overaligned trivial elements are value initialized");
}

void test_trivial_destructor() {
    Constructed::constructions = 0;
    auto values                = make_unique_large_page<Constructed[]>(3);
    check(Constructed::constructions == 3, "trivial destructors do not skip constructors");
    check(values[0].value == 73 && values[2].value == 73,
          "constructors initialize the first and last elements");
    check(reinterpret_cast<std::uintptr_t>(values.get()) % 4096 == 0,
          "user constructors do not require an array size header");
}

void test_nontrivial_array() {
    Destructed::constructions = 0;
    Destructed::destructions  = 0;
    Destructed::order         = {};
    {
        auto values = make_unique_aligned<Destructed[]>(3);
        check(reinterpret_cast<std::uintptr_t>(values.get()) % 64 == 0,
              "nontrivial arrays retain type alignment");
        check(Destructed::constructions == 3 && Destructed::destructions == 0,
              "nontrivial elements are constructed exactly once");
        check(values[0].id == 0 && values[2].id == 2,
              "nontrivial elements retain their array order");
    }
    check(Destructed::destructions == 3, "every nontrivial element is destroyed");
    check(Destructed::order == std::array<int, 3>{2, 1, 0},
          "nontrivial elements are destroyed in reverse order");
}

void test_empty_arrays() {
    auto trivial = make_unique_large_page<i16[]>(0);
    check(trivial != nullptr, "empty trivial arrays remain allocatable");
    Destructed::constructions = 0;
    Destructed::destructions  = 0;
    {
        auto nontrivial = make_unique_aligned<Destructed[]>(0);
        check(nontrivial != nullptr, "empty nontrivial arrays remain allocatable");
    }
    check(Destructed::constructions == 0 && Destructed::destructions == 0,
          "empty arrays neither construct nor destroy elements");
    memory_deleter_array<i16>(
      nullptr, [](void*) { check(false, "null array deletion does not call the deallocator"); });
}

void test_history_alignment(usize threads) {
    SharedHistories histories(threads);
    check(reinterpret_cast<std::uintptr_t>(&histories.pawnHistory[0]) % 4096 == 0,
          "PawnHistory preserves page alignment");
    check(
      reinterpret_cast<std::uintptr_t>(&histories.pawnHistory[histories.pawnHistory.get_size() - 1])
          % 64
        == 0,
      "PawnHistory rows preserve cache line alignment");
    check(reinterpret_cast<std::uintptr_t>(&histories.correctionHistory[0]) % 4096 == 0,
          "UnifiedCorrectionHistory preserves page alignment");
    check(histories.pawnHistory.get_size() == threads * 8192,
          "PawnHistory retains its configured capacity");
    check(histories.correctionHistory.get_size() == threads * 65536,
          "UnifiedCorrectionHistory retains its configured capacity");
    check(reinterpret_cast<std::uintptr_t>(histories.continuationHistoryBlock.get()) % 4096 == 0,
          "scalar allocations retain page alignment");
}

}

int main() {
    test_trivial_array();
    test_overaligned_array();
    test_trivial_destructor();
    test_nontrivial_array();
    test_history_alignment(1);
    test_history_alignment(2);
    test_empty_arrays();
    std::cout << "Memory tests: " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
