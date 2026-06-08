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

// Standalone NNUE embedding for universal binary builds

#include "../evaluate.h"

#ifdef UNIVERSAL_BINARY_MACOS_X86_SLICE

// In a macOS universal binary the network is embedded only in the arm64 slice,
// and the x86-64 slice mmaps it from the arm64 slice.

    #include <climits>
    #include <cstdint>
    #include <fcntl.h>
    #include <mach-o/dyld.h>
    #include <stdlib.h>
    #include <sys/mman.h>
    #include <unistd.h>

// Must be kept in sync with patch_x86_slice.sh
extern const volatile Stockfish::u64 gUniversalNNUEOffset = 0xCAFE0FF5E70FF5E7ULL;
extern const volatile Stockfish::u64 gUniversalNNUESize   = 0xCAFE512ECAFE512EULL;

static const unsigned char* map_embedded_nnue() {
    char           path[PATH_MAX];
    Stockfish::u32 len = sizeof(path);
    if (_NSGetExecutablePath(path, &len) != 0)
        return nullptr;

    char        resolved[PATH_MAX];
    const char* file = realpath(path, resolved) ? resolved : path;

    int fd = open(file, O_RDONLY);
    if (fd < 0)
        return nullptr;

    // Align down to page size for mmap
    const Stockfish::u64 pageSize = Stockfish::u64(sysconf(_SC_PAGESIZE));
    const Stockfish::u64 base     = gUniversalNNUEOffset & ~(pageSize - 1);
    const Stockfish::u64 pad      = gUniversalNNUEOffset - base;

    void* p =
      mmap(nullptr, size_t(gUniversalNNUESize + pad), PROT_READ, MAP_PRIVATE, fd, off_t(base));
    close(fd);
    if (p == MAP_FAILED)
        return nullptr;

    return reinterpret_cast<const unsigned char*>(p) + pad;
}

extern const unsigned char* const gEmbeddedNNUEData = map_embedded_nnue();
extern const unsigned int         gEmbeddedNNUESize = static_cast<unsigned int>(gUniversalNNUESize);

#else

extern const unsigned char gEmbeddedNNUEData[] =
    #ifdef __has_embed
  {
        #embed EvalFileDefaultName
};
const unsigned int padding = 0;
    #else
        #include "network_dump.inc"
  ;
const unsigned int padding = 1;  // trailing NUL byte
    #endif
extern const unsigned int gEmbeddedNNUESize = sizeof(gEmbeddedNNUEData) - padding;

#endif
