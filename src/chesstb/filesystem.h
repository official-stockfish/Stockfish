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

#ifndef CHESSTB_FILESYSTEM_H_INCLUDED
#define CHESSTB_FILESYSTEM_H_INCLUDED

#include "common.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

namespace Stockfish::ChessTB {

// Whether `path` exists spelled exactly as on disk (KrK is not KRK). The
// directory is scanned only when the case-flipped name exists too.
inline bool file_exists_case_exact(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return false;
    const std::string name    = path.filename().string();
    std::string       flipped = name;
    for (char& ch : flipped)
    {
        const unsigned char u = static_cast<unsigned char>(ch);
        ch = static_cast<char>(std::isupper(u) ? std::tolower(u) : std::toupper(u));
    }
    if (flipped == name || !std::filesystem::exists(path.parent_path() / flipped, ec))
        return true;
    for (std::filesystem::directory_iterator it(path.parent_path(), ec), end; !ec && it != end;
         it.increment(ec))
        if (it->path().filename().string() == name)
            return true;
    return false;
}

struct Memory_Mapped_File {
    Memory_Mapped_File()                                     = default;
    Memory_Mapped_File(const Memory_Mapped_File&)            = delete;
    Memory_Mapped_File& operator=(const Memory_Mapped_File&) = delete;
    ~Memory_Mapped_File() { close_file(); }

    bool open_readonly(const std::filesystem::path& path);
    void close_file();

    Const_Span<uint8_t> data_span() const { return Const_Span(m_data, m_size); }

   private:
    const uint8_t* m_data = nullptr;
    size_t         m_size = 0;
#ifdef _WIN32
    void* m_handle = nullptr;
#endif
};

}  // namespace Stockfish::ChessTB

#endif  // CHESSTB_FILESYSTEM_H_INCLUDED
