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

#include "../tablebases.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <ratio>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../bitboard.h"
#include "../misc.h"
#include "../movegen.h"
#include "../position.h"
#include "../search.h"
#include "../types.h"
#include "../ucioption.h"
#include "cache.h"
#include "common.h"
#include "filesystem.h"
#include "index.h"
#include "table_files.h"

namespace Stockfish::ChessTB {

namespace {

struct Canonical_Root {
    Piece_Config ps;
    View         pos;
};

bool is_symmetric_material(const Piece_Config& ps) {
    const auto [mat_key, mir_key] = ps.material_keys();
    return mat_key == mir_key;
}

// The frame to read and the position as it indexes it: a dropped frame of
// symmetric material is read as its mirror.
struct Frame_Read {
    Color frame;
    View  pos;
    bool  readable;
};

template<typename File>
Frame_Read locate_frame(const File& f, const Piece_Config& ps, const View& pos, WDL_Entry wdl) {
    const Color stm    = pos.turn();
    const auto  priced = [&](Color frame) {
        return !(f.is_loss_only[frame] && (wdl == WDL_Entry::WIN || wdl == WDL_Entry::CURSED_WIN));
    };
    if (!f.is_dropped[stm])
        return {stm, pos, priced(stm)};
    if (!is_symmetric_material(ps))
        return {stm, pos, false};
    return {~stm, pos.mirror(), priced(~stm)};
}

std::atomic<uint64_t>       epoch{next_epoch()};
std::shared_ptr<Block_Pool> blocks = std::make_shared<Block_Pool>();

std::vector<std::filesystem::path> wdl_dirs;
std::vector<std::filesystem::path> dtz_dirs;
std::vector<std::filesystem::path> dtc_dirs;
std::vector<std::filesystem::path> dtm_dirs;
std::vector<std::filesystem::path> dtm50_dirs;

std::mutex                                                                       tables_mu;
std::unordered_map<Material_Key, std::shared_ptr<WDL_File>, Material_Key_Hash>   wdl_cache;
std::unordered_map<Material_Key, std::shared_ptr<DTZ_File>, Material_Key_Hash>   dtz_cache;
std::unordered_map<Material_Key, std::shared_ptr<DTC_File>, Material_Key_Hash>   dtc_cache;
std::unordered_map<Material_Key, std::shared_ptr<DTM_File>, Material_Key_Hash>   dtm_cache;
std::unordered_map<Material_Key, std::shared_ptr<DTM50_File>, Material_Key_Hash> dtm50_cache;

// The table for ps, opened once per epoch: from the thread's own cache, else
// the shared one, else from disk.
template<typename File>
File* open_table(std::unordered_map<Material_Key, std::shared_ptr<File>, Material_Key_Hash>& cache,
                 const std::vector<std::filesystem::path>&                                   dirs,
                 const char*                                                                 ext,
                 const Piece_Config&                                                         ps) {
    using Handle = std::shared_ptr<File>;
    thread_local TL_Cache<Material_Key, Handle> tl;
    const Material_Key                          k = ps.min_material_key();

    for (;;)
    {
        const uint64_t e = epoch.load(std::memory_order_acquire);

        if (const Handle* hit = tl.find(e, k))
            return hit->get();
        {
            std::lock_guard<std::mutex> lk(tables_mu);
            auto                        it = cache.find(k);
            if (it != cache.end())
            {
                tl.insert(e, k, it->second);
                return it->second.get();
            }
        }

        Handle            built;
        const std::string name = ps.name() + ext;
        for (const auto& d : dirs)
            if (file_exists_case_exact(d / name))
            {
                built = std::make_shared<File>(blocks);
                if (!built->load(ps, d / name))
                    built = nullptr;
                break;
            }

        std::lock_guard<std::mutex> lk(tables_mu);
        if (epoch.load(std::memory_order_relaxed) != e)
            continue;

        auto [it, inserted] = cache.try_emplace(k, std::move(built));
        tl.insert(e, k, it->second);
        return it->second.get();
    }
}

// Pieces in the largest table found, kings included.
size_t scan_paths() {
    size_t lg       = 0;
    auto   scan_dir = [&](const std::filesystem::path& dir, const char* ext) {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec))
            return;
        const size_t ext_len = std::strlen(ext);
        for (auto& e : std::filesystem::directory_iterator(dir, ec))
        {
            if (ec)
                break;
            const std::string n = e.path().filename().string();
            if (n.size() < ext_len)
                continue;
            if (n.compare(n.size() - ext_len, ext_len, ext) != 0)
                continue;
            lg = std::max(lg, std::min(n.find('.'), n.size()));
        }
    };
    for (const auto& d : wdl_dirs)
        scan_dir(d, WDL_EXT);
    for (const auto& d : dtz_dirs)
        scan_dir(d, DTZ_EXT);
    for (const auto& d : dtc_dirs)
        scan_dir(d, DTC_EXT);
    for (const auto& d : dtm_dirs)
        scan_dir(d, DTM_EXT);
    for (const auto& d : dtm50_dirs)
        scan_dir(d, DTM50_EXT);
    return lg;
}

void add_search_dir(std::vector<std::filesystem::path>& dirs, std::filesystem::path dir) {
    const std::filesystem::path key = dir.lexically_normal();
    for (const std::filesystem::path& d : dirs)
        if (d.lexically_normal() == key)
            return;
    dirs.emplace_back(std::move(dir));
}

// Adds one directory: each metric's subdirectory, or the directory itself.
void init_dir(const std::filesystem::path& dir) {
    std::error_code ec;
    auto            try_sub = [&](const char* sub) -> std::filesystem::path {
        auto p = dir / sub;
        return std::filesystem::is_directory(p, ec) ? p : dir;
    };
    add_search_dir(wdl_dirs, try_sub("wdl"));
    add_search_dir(dtz_dirs, try_sub("dtz"));
    add_search_dir(dtc_dirs, try_sub("dtc"));
    add_search_dir(dtm_dirs, try_sub("dtm"));
    add_search_dir(dtm50_dirs, try_sub("dtm50"));
}

// The table's material for pos, less the castling rooks (castling) and the pair
// pw/pb (SQ_NONE: none), the colours flipped where the literal key is the
// mirrored one.
Canonical_Root root_for(const View& pos, bool castling, Square pw, Square pb) {
    Piece_Config::Castling_Rights_Counts rights{0, 0};
    Bitboard excluded = (pw != SQ_NONE ? square_bb(pw) | pb : Bitboard(0));
    if (castling)
        for (const Color c : {WHITE, BLACK})
        {
            const Bitboard rooks = pos.castling_rooks(c);
            rights[c]            = size_t(popcount(rooks));
            excluded |= rooks;
        }

    std::array<Piece, MAX_MAN> pieces;
    size_t                     n = 0;
    Material_Key               key;
    for (Piece pc : TABLE_PIECES)
        for (int k = popcount(pos.piece_bb(pc) & ~excluded); k > 0; --k)
        {
            pieces[n++] = pc;
            key.add_piece(pc);
        }

    Piece_Config ps(Const_Span<Piece>(pieces.data(), n), rights);
    if (pw != SQ_NONE)
    {
        ps.mark_opposing_pair();
        key.add_pair();
    }
    if (rights[WHITE] + rights[BLACK] > 0)
        key.add_castling(rights[WHITE], rights[BLACK]);
    const View v = key == ps.base_material_key() ? pos : pos.mirror();
    return {std::move(ps), v};
}

WDL_File* open_wdl(const Piece_Config& ps) { return open_table(wdl_cache, wdl_dirs, WDL_EXT, ps); }
DTZ_File* open_dtz(const Piece_Config& ps) { return open_table(dtz_cache, dtz_dirs, DTZ_EXT, ps); }
DTC_File* open_dtc(const Piece_Config& ps) { return open_table(dtc_cache, dtc_dirs, DTC_EXT, ps); }
DTM_File* open_dtm(const Piece_Config& ps) { return open_table(dtm_cache, dtm_dirs, DTM_EXT, ps); }
DTM50_File* open_dtm50(const Piece_Config& ps) {
    return open_table(dtm50_cache, dtm50_dirs, DTM50_EXT, ps);
}


}  // namespace

// Directories as ChessTBPath; each metric in its wdl/ ... dtm50/ subdirectory
// or itself, and so its castling/.
void init(const std::string& paths) {
    {
        std::lock_guard<std::mutex> lk(tables_mu);
        for (auto* dirs : {&wdl_dirs, &dtz_dirs, &dtc_dirs, &dtm_dirs, &dtm50_dirs})
            dirs->clear();
        wdl_cache.clear();
        dtz_cache.clear();
        dtc_cache.clear();
        dtm_cache.clear();
        dtm50_cache.clear();
        epoch.store(next_epoch(), std::memory_order_release);
    }

#ifdef _WIN32
    const char sep = ';';
#else
    const char sep = ':';
#endif
    std::error_code   ec;
    std::stringstream ss(paths);
    std::string       path;
    while (std::getline(ss, path, sep))
    {
        if (path.empty() || path == "<empty>")
            continue;
        const std::filesystem::path dir(path);
        init_dir(dir);
        if (std::filesystem::is_directory(dir / "castling", ec))
            init_dir(dir / "castling");
    }
    Tablebases::MaxCardinality = int(scan_paths());
}

}  // namespace Stockfish::ChessTB

namespace Stockfish {

namespace {

using namespace ChessTB;

constexpr int MAX_DERIVE_DEPTH = 64;
constexpr int MAX_DTZ          = 1 << 18;

constexpr Value WDL_to_value[] = {-VALUE_MATE + MAX_PLY + 1, VALUE_DRAW - 2, VALUE_DRAW,
                                  VALUE_DRAW + 2, VALUE_MATE - MAX_PLY - 1};

// The position as its tables index it: castling rooks apart, and the opposing
// pair too where its table exists. That turns only on the material, the
// castling rights held and whether there is a pair, so each thread keeps the
// answers by those, per epoch.
Canonical_Root canonical_root(const Position& pos) {
    struct Root {
        std::optional<Piece_Config> ps;
        bool                        mirrored = false;
    };
    thread_local TL_Cache<Key, Root> cache;

    const View v(pos);
    Square     pw = SQ_NONE, pb = SQ_NONE;
    const bool pair   = Pair_Group::canonical_pair(v.piece_bb(W_PAWN), v.piece_bb(B_PAWN), pw, pb);
    int        rights = 0;
    for (const CastlingRights cr : {WHITE_OO, WHITE_OOO, BLACK_OO, BLACK_OOO})
        rights |= pos.can_castle(cr) ? cr : 0;
    const Key tag = pos.material_key() ^ (Key(rights) * 0x9E3779B97F4A7C15ull)
                  ^ (pair ? 0xD6E8FEB86659FD93ull : 0);
    const uint64_t e = ChessTB::epoch.load(std::memory_order_relaxed);

    if (const Root* hit = cache.find(e, tag))
        return {*hit->ps, View(pos, hit->mirrored)};

    std::optional<Canonical_Root> r;
    if (pair)
        r = root_for(v, rights != 0, pw, pb);
    if (!r || !open_wdl(r->ps))
        r = root_for(v, rights != 0, SQ_NONE, SQ_NONE);
    cache.insert(e, tag, Root{r->ps, r->pos.turn() != pos.side_to_move()});
    return *r;
}

int wdl_rank(WDL_Entry w) {
    switch (w)
    {
    case WDL_Entry::WIN :
        return 4;
    case WDL_Entry::CURSED_WIN :
        return 3;
    case WDL_Entry::DRAW :
        return 2;
    case WDL_Entry::BLESSED_LOSS :
        return 1;
    case WDL_Entry::LOSE :
        return 0;
    case WDL_Entry::ILLEGAL :
        return -1;
    }
    return -1;
}

int stored_rank(WDL_Stored s) {
    switch (s)
    {
    case WDL_Stored::LOSE :
        return 0;
    case WDL_Stored::BOUNDARY_LOSS :
        return 1;
    case WDL_Stored::BLESSED_LOSS :
        return 2;
    case WDL_Stored::DRAW :
        return 3;
    case WDL_Stored::CURSED_WIN :
        return 4;
    case WDL_Stored::BOUNDARY_WIN :
        return 5;
    case WDL_Stored::WIN :
        return 6;
    case WDL_Stored::ILLEGAL :
        break;
    }
    return -1;
}

WDL_Entry invert_wdl(WDL_Entry w) {
    switch (w)
    {
    case WDL_Entry::WIN :
        return WDL_Entry::LOSE;
    case WDL_Entry::CURSED_WIN :
        return WDL_Entry::BLESSED_LOSS;
    case WDL_Entry::DRAW :
        return WDL_Entry::DRAW;
    case WDL_Entry::BLESSED_LOSS :
        return WDL_Entry::CURSED_WIN;
    case WDL_Entry::LOSE :
        return WDL_Entry::WIN;
    case WDL_Entry::ILLEGAL :
        return WDL_Entry::ILLEGAL;
    }
    return WDL_Entry::ILLEGAL;
}

// A stored code from the opponent's side, boundary codes included: a win or
// loss at the 50-move edge is the cursed side of it one ply earlier.
WDL_Entry invert_stored(WDL_Stored s) {
    switch (s)
    {
    case WDL_Stored::WIN :
        return WDL_Entry::LOSE;
    case WDL_Stored::CURSED_WIN :
        return WDL_Entry::BLESSED_LOSS;
    case WDL_Stored::DRAW :
        return WDL_Entry::DRAW;
    case WDL_Stored::BLESSED_LOSS :
        return WDL_Entry::CURSED_WIN;
    case WDL_Stored::LOSE :
        return WDL_Entry::WIN;
    case WDL_Stored::BOUNDARY_LOSS :
        return WDL_Entry::CURSED_WIN;
    case WDL_Stored::BOUNDARY_WIN :
        return WDL_Entry::BLESSED_LOSS;
    case WDL_Stored::ILLEGAL :
        return WDL_Entry::ILLEGAL;
    }
    return WDL_Entry::ILLEGAL;
}

WDL_Entry fold_dtm_wdl(WDL_Entry w) {
    if (w == WDL_Entry::CURSED_WIN)
        return WDL_Entry::WIN;
    if (w == WDL_Entry::BLESSED_LOSS)
        return WDL_Entry::LOSE;
    return w;
}

WDL_Entry fold_50mr_wdl(WDL_Entry w) {
    if (w == WDL_Entry::CURSED_WIN)
        return WDL_Entry::DRAW;
    if (w == WDL_Entry::BLESSED_LOSS)
        return WDL_Entry::DRAW;
    return w;
}

bool prefer_new(WDL_Entry new_wdl, uint16_t new_dtz, WDL_Entry old_wdl, uint16_t old_dtz) {
    const int rn = wdl_rank(new_wdl), ro = wdl_rank(old_wdl);
    if (rn != ro)
        return rn > ro;
    const bool win_side  = (new_wdl == WDL_Entry::WIN || new_wdl == WDL_Entry::CURSED_WIN);
    const bool loss_side = (new_wdl == WDL_Entry::LOSE || new_wdl == WDL_Entry::BLESSED_LOSS);
    if (win_side)
        return new_dtz < old_dtz;
    if (loss_side)
        return new_dtz > old_dtz;
    return false;
}

bool below_pinned_class(WDL_Entry pinned, WDL_Entry offer) {
    return pinned != WDL_Entry::ILLEGAL && wdl_rank(offer) < wdl_rank(pinned);
}

// A move to child_wdl at dtz, offered to the mover's best: past 100 plies a
// decisive class turns cursed.
void offer_dtz(WDL_Entry child_wdl, uint16_t dtz, WDL_Entry& best_wdl, uint16_t& best_dtz) {
    WDL_Entry my_wdl = invert_wdl(child_wdl);
    if (dtz > DTZ_MAX_NON_CURSED)
    {
        if (my_wdl == WDL_Entry::WIN)
            my_wdl = WDL_Entry::CURSED_WIN;
        if (my_wdl == WDL_Entry::LOSE)
            my_wdl = WDL_Entry::BLESSED_LOSS;
    }
    if (prefer_new(my_wdl, dtz, best_wdl, best_dtz))
    {
        best_wdl = my_wdl;
        best_dtz = dtz;
    }
}

WDL_Entry dtz_lift(WDL_Entry my_wdl) {
    return my_wdl == WDL_Entry::LOSE ? WDL_Entry::BLESSED_LOSS : my_wdl;
}

bool is_win_class(WDL_Entry w) { return w == WDL_Entry::WIN || w == WDL_Entry::CURSED_WIN; }

bool child_class_is_forced(WDL_Entry pinned) { return pinned == WDL_Entry::LOSE; }

bool dtm50_layer_pinned_by_dtm(WDL_Entry wdl, uint16_t dtm, unsigned rule50) {
    if (wdl != WDL_Entry::WIN && wdl != WDL_Entry::LOSE)
        return false;
    return rule50 + static_cast<unsigned>(dtm) <= DTZ_MAX_NON_CURSED;
}

bool layer_busted_by_dtz(WDL_Entry wdl, uint16_t dtz, unsigned rule50) {
    if (wdl != WDL_Entry::WIN && wdl != WDL_Entry::LOSE)
        return false;
    return rule50 + static_cast<unsigned>(dtz) > DTZ_MAX_NON_CURSED;
}

struct Skipped_Children {
    void of_class(WDL_Entry my_wdl) { best_rank = std::max(best_rank, wdl_rank(my_wdl)); }
    void of_dtz_class(WDL_Entry my_wdl) { of_class(dtz_lift(my_wdl)); }
    void unknown() { any_unknown = true; }
    bool unpin(WDL_Entry best_wdl) const { return any_unknown || best_rank >= wdl_rank(best_wdl); }

   private:
    int  best_rank   = -1;
    bool any_unknown = false;
};

bool is_bare_kings(const Position& pos) { return pos.count<ALL_PIECES>() == 2; }

bool is_zeroing(const Position& pos, Move m) {
    return pos.capture(m) || type_of(pos.moved_piece(m)) == PAWN;
}

bool is_checkmate(Position& pos) { return pos.checkers() && MoveList<ALL>(pos).size() == 0; }

int castling_count(const Position& pos) {
    return pos.can_castle(WHITE_OO) + pos.can_castle(WHITE_OOO) + pos.can_castle(BLACK_OO)
         + pos.can_castle(BLACK_OOO);
}

// The child is on the board; parent_castling is the parent's count of rights.
bool is_cursed_exit(int parent_castling, const Position& child, WDL_Entry child_wdl) {
    return (child_wdl == WDL_Entry::CURSED_WIN || child_wdl == WDL_Entry::BLESSED_LOSS)
        && castling_count(child) < parent_castling;
}

uint16_t quiet_move_dtz(uint16_t child_dtz, bool cursed_exit, bool child_rounded) {
    const uint16_t v = static_cast<uint16_t>(child_dtz + 1u);
    if (!cursed_exit)
        return v;
    return child_rounded ? static_cast<uint16_t>(v + 1u) : static_cast<uint16_t>(v | 1u);
}

// Calls f(m, is_zeroing) with each legal move made on pos, but en passant
// captures, which the tables (no en passant square) don't have; true if f
// stopped.
template<typename F>
bool visit_legal_moves(Position& pos, F&& f) {
    for (const Move m : MoveList<ALL>(pos))
    {
        if (m.type_of() == EN_PASSANT)
            continue;
        const bool is_zeroing_move = is_zeroing(pos, m);
        pos.do_move(m);
        const bool stop = f(m, is_zeroing_move);
        pos.undo_move(m);
        if (stop)
            return true;
    }
    return false;
}

// Where en passant captures are the only moves, the table entry (no en passant
// state) isn't one of the options: has_non_ep_move.
bool has_non_ep_move(Position& pos) {
    return visit_legal_moves(pos, [](Move, bool) { return true; });
}

// Calls f(m, has_non_ep_move) with each en passant capture made on pos; true
// if f stopped. The parent's has_non_ep_move is visited once, and only if
// there is a legal en passant capture (WithNonEp false: never).
template<bool WithNonEp = true, typename F>
bool visit_legal_ep_captures(Position& pos, F&& f) {
    if (pos.ep_square() == SQ_NONE)
        return false;
    std::optional<bool> non_ep;
    for (const Move m : MoveList<ALL>(pos))
    {
        if (m.type_of() != EN_PASSANT)
            continue;
        if (WithNonEp && !non_ep)
            non_ep = has_non_ep_move(pos);
        pos.do_move(m);
        const bool stop = f(m, non_ep.value_or(false));
        pos.undo_move(m);
        if (stop)
            return true;
    }
    return false;
}

struct Probe_Result {
    WDL_Entry wdl         = WDL_Entry::ILLEGAL;
    bool      has_dtz     = false;
    uint16_t  dtz         = 0;
    bool      dtz_rounded = false;
    bool      has_dtc     = false;
    WDL_Entry dtc_wdl     = WDL_Entry::ILLEGAL;
    uint16_t  dtc_order   = 0;
    uint16_t  dtc         = 0;
    bool      has_dtm     = false;
    uint16_t  dtm         = 0;
    bool      has_dtm50   = false;
    WDL_Entry dtm50_wdl   = WDL_Entry::ILLEGAL;
    uint16_t  dtm50       = 0;
};

// The top-level calls, one per answer, en passant captures included: each
// reads only what its answer needs and fills only its fields.
Probe_Result probe_dtc_impl(Position& pos, unsigned rule50, int depth);
Probe_Result probe_dtm_impl(Position& pos, unsigned rule50, int depth);

struct DTM50_Result {
    WDL_Entry wdl     = WDL_Entry::ILLEGAL;
    uint16_t  dtm     = 0;
    bool      has_dtz = false;
    uint16_t  dtz     = 0;
};

WDL_Entry probe_wdl_impl(Position& pos, int depth);
WDL_Entry probe_wdl_internal(Position& pos, int depth);

// The best capture or promotion: what a relaxed frame's stored lower bound
// leaves out.
WDL_Entry relax_bound_wdl(Position& pos, int depth) {
    if (depth >= MAX_DERIVE_DEPTH)
        return WDL_Entry::ILLEGAL;
    WDL_Entry best = WDL_Entry::LOSE;
    for (const Move m : MoveList<ALL>(pos))
    {
        if (m.type_of() != PROMOTION && !pos.capture(m))
            continue;
        pos.do_move(m);
        const WDL_Entry cw = is_bare_kings(pos) ? WDL_Entry::DRAW : probe_wdl_impl(pos, depth + 1);
        pos.undo_move(m);
        if (cw == WDL_Entry::ILLEGAL)
        {
            best = WDL_Entry::ILLEGAL;
            continue;
        }
        const WDL_Entry mine = invert_wdl(cw);
        if (mine == WDL_Entry::WIN)
            return WDL_Entry::WIN;
        if (best != WDL_Entry::ILLEGAL && wdl_rank(mine) > wdl_rank(best))
            best = mine;
    }
    return best;
}

// The stored code, boundary codes kept, raised by a relaxed frame's bound: what
// a derive reads for a quiet child (the same material, so a stored frame).
WDL_Stored read_wdl_stored(Position& pos, int depth) {
    const Canonical_Root c = canonical_root(pos);
    WDL_File*            w = open_wdl(c.ps);
    if (!w)
        return WDL_Stored::ILLEGAL;
    const Color      stm = c.pos.turn();
    const WDL_Stored s   = w->read(stm, c.pos);
    if (!w->is_relaxed[stm] || s == WDL_Stored::ILLEGAL || s == WDL_Stored::WIN)
        return s;
    const WDL_Entry bound = relax_bound_wdl(pos, depth);
    if (bound == WDL_Entry::ILLEGAL)
        return WDL_Stored::ILLEGAL;
    const WDL_Stored b = static_cast<WDL_Stored>(bound);
    return stored_rank(b) > stored_rank(s) ? b : s;
}

WDL_Entry derive_wdl(Position& pos, int depth) {
    if (depth >= MAX_DERIVE_DEPTH)
        return WDL_Entry::ILLEGAL;

    bool             any_legal = false;
    WDL_Entry        best      = WDL_Entry::LOSE;
    Skipped_Children skipped;

    if (visit_legal_moves(pos, [&](Move, bool c_is_zeroing) {
            any_legal = true;
            WDL_Entry mw;
            if (is_bare_kings(pos))
                mw = WDL_Entry::DRAW;
            else if (c_is_zeroing)
            {
                const WDL_Entry cw = probe_wdl_impl(pos, depth + 1);
                if (cw == WDL_Entry::ILLEGAL)
                {
                    skipped.unknown();
                    return false;
                }
                mw = invert_wdl(cw);
            }
            else
            {
                const WDL_Stored cs = read_wdl_stored(pos, depth + 1);
                if (cs == WDL_Stored::ILLEGAL)
                {
                    skipped.unknown();
                    return false;
                }
                mw = invert_stored(cs);
            }
            if (wdl_rank(mw) > wdl_rank(best))
                best = mw;
            return best == WDL_Entry::WIN;
        }))
        return best;
    if (!any_legal)
        return pos.checkers() ? WDL_Entry::LOSE : WDL_Entry::DRAW;
    if (skipped.unpin(best))
        return WDL_Entry::ILLEGAL;
    return best;
}

WDL_Entry raise_by_bound(WDL_File& w, Color frame, WDL_Entry stored, Position& pos, int depth) {
    if (!w.is_relaxed[frame] || stored == WDL_Entry::ILLEGAL || stored == WDL_Entry::WIN)
        return stored;
    const WDL_Entry bound = relax_bound_wdl(pos, depth);
    if (bound == WDL_Entry::ILLEGAL)
        return WDL_Entry::ILLEGAL;
    return wdl_rank(bound) > wdl_rank(stored) ? bound : stored;
}

WDL_Entry probe_wdl_internal(Position& pos, int depth) {
    const Canonical_Root c = canonical_root(pos);
    WDL_File*            w = open_wdl(c.ps);
    if (!w)
        return WDL_Entry::ILLEGAL;

    const Color stm = c.pos.turn();
    if (w->is_dropped[stm])
    {
        if (!is_symmetric_material(c.ps))
            return derive_wdl(pos, depth);
        const View mp = c.pos.mirror();
        return raise_by_bound(*w, mp.turn(), wdl_from_storage(w->read(mp.turn(), mp)), pos, depth);
    }
    return raise_by_bound(*w, stm, wdl_from_storage(w->read(stm, c.pos)), pos, depth);
}

// With the position's en passant captures. Where they are the only moves, the
// stored result (which has no en passant square) isn't one of the options.
WDL_Entry probe_wdl_impl(Position& pos, int depth) {
    WDL_Entry best = probe_wdl_internal(pos, depth);
    if (best == WDL_Entry::ILLEGAL || best == WDL_Entry::WIN || pos.ep_square() == SQ_NONE)
        return best;

    bool any_ep = false, unresolved_ep = false;
    if (!visit_legal_ep_captures(pos,
                                 [&](Move, bool non_ep) {
                                     const WDL_Entry cw = is_bare_kings(pos)
                                                          ? WDL_Entry::DRAW
                                                          : probe_wdl_internal(pos, depth + 1);
                                     if (cw == WDL_Entry::ILLEGAL)
                                     {
                                         unresolved_ep = true;
                                         return false;
                                     }
                                     const WDL_Entry mine = invert_wdl(cw);
                                     if ((!non_ep && !any_ep) || wdl_rank(mine) > wdl_rank(best))
                                         best = mine;
                                     any_ep = true;
                                     return best == WDL_Entry::WIN;
                                 })
        && unresolved_ep)
        return WDL_Entry::ILLEGAL;
    return best;
}

std::optional<DTZ_Value> probe_dtz_internal(Position& pos, WDL_Entry wdl, int depth);

// A DTZ frame that isn't stored (or, zeroing_only, a relaxed winning frame's
// check for a zeroing move that gives 1): one ply of search.
std::optional<DTZ_Value> derive_dtz(Position& pos, WDL_Entry wdl, int depth, bool zeroing_only) {
    if (depth >= MAX_DERIVE_DEPTH)
        return std::nullopt;

    bool             any_legal = false;
    WDL_Entry        best_wdl  = WDL_Entry::LOSE;
    uint16_t         best_dtz  = 0;
    bool             rounded   = false;
    Skipped_Children skipped;
    const int        castling = castling_count(pos);

    if (visit_legal_moves(pos, [&](Move, bool c_is_zeroing) {
            any_legal = true;

            WDL_Entry cw;
            uint16_t  my_dtz;
            if (is_bare_kings(pos))
            {
                cw     = WDL_Entry::DRAW;
                my_dtz = 1;
            }
            else if (c_is_zeroing)
            {
                if (child_class_is_forced(wdl))
                    cw = WDL_Entry::WIN;
                else
                {
                    cw = probe_wdl_impl(pos, depth + 1);
                    if (cw == WDL_Entry::ILLEGAL)
                    {
                        skipped.unknown();
                        return false;
                    }
                }
                my_dtz = 1;
            }
            else
            {
                if (zeroing_only)
                    return false;
                if (child_class_is_forced(wdl))
                    cw = WDL_Entry::WIN;
                else
                {
                    cw = probe_wdl_internal(pos, depth + 1);
                    if (cw == WDL_Entry::ILLEGAL)
                    {
                        skipped.unknown();
                        return false;
                    }
                    if (below_pinned_class(wdl, dtz_lift(invert_wdl(cw))))
                        return false;
                }
                const auto child_dtz = probe_dtz_internal(pos, cw, depth + 1);
                if (!child_dtz)
                {
                    skipped.of_dtz_class(invert_wdl(cw));
                    return false;
                }
                my_dtz = quiet_move_dtz(child_dtz->dtz, is_cursed_exit(castling, pos, cw),
                                        child_dtz->rounded);
                rounded |= child_dtz->rounded;
            }

            offer_dtz(cw, my_dtz, best_wdl, best_dtz);
            return is_win_class(wdl) && best_wdl == wdl && best_dtz == 1;
        }))
        return DTZ_Value{best_dtz, false};
    if (!any_legal)
        return DTZ_Value{0, false};
    if (skipped.unpin(best_wdl))
        return std::nullopt;
    if (zeroing_only || best_wdl == WDL_Entry::DRAW)
        return DTZ_Value{0, false};
    return DTZ_Value{best_dtz, rounded};
}

// The DTZ file; a frame that isn't stored is derived, a relaxed winning frame
// checked for a zeroing move that gives 1.
std::optional<DTZ_Value> probe_dtz_internal(Position& pos, WDL_Entry wdl, int depth) {
    const Canonical_Root c = canonical_root(pos);
    DTZ_File*            d = open_dtz(c.ps);
    if (!d)
        return std::nullopt;

    const Frame_Read fr = locate_frame(*d, c.ps, c.pos, wdl);
    if (!fr.readable)
        return derive_dtz(pos, wdl, depth, false);

    if (d->is_relaxed[fr.frame] && is_win_class(wdl))
    {
        const auto derived = derive_dtz(pos, wdl, depth, true);
        if (!derived || derived->dtz == 1)
            return derived;
    }
    return d->read(fr.frame, fr.pos, wdl);
}

std::optional<uint16_t> probe_dtm_internal(Position& pos, WDL_Entry wdl, int depth);
DTM50_Result            probe_dtm50_internal(
             Position& pos, WDL_Entry wdl, unsigned rule50, int depth, bool with_dtz = false);

// A stored unclocked DTM50 cell's DTZ, from h, the first clock at which the
// position turns drawn: 102 - h; h == 0 (never drawn) a zeroing move now, or
// mate; h == 1 nothing. Cursed classes have none.
std::optional<uint16_t> dtz_from_draw_flip(uint16_t h, WDL_Entry wdl, Position& pos) {
    if (wdl == WDL_Entry::DRAW || wdl == WDL_Entry::ILLEGAL)
        return uint16_t{0};
    if (wdl != WDL_Entry::WIN && wdl != WDL_Entry::LOSE)
        return std::nullopt;
    if (h == 0)
        return (wdl == WDL_Entry::LOSE && is_checkmate(pos)) ? uint16_t{0} : uint16_t{1};
    if (h == 1)
        return std::nullopt;
    return static_cast<uint16_t>(DTZ_MAX_NON_CURSED + 2u - h);
}

struct DTZ_Minimax {
    void zeroing_child(WDL_Entry child_wdl) { offer_dtz(child_wdl, 1, best_wdl, best_dtz); }

    void quiet_child(WDL_Entry child_wdl, const DTM50_Result& child, bool cursed_exit) {
        if (child.has_dtz)
            offer_dtz(child_wdl, quiet_move_dtz(child.dtz, cursed_exit, false), best_wdl, best_dtz);
        else
            skipped.of_dtz_class(invert_wdl(child_wdl));
    }

    void unwalked() { skipped.unknown(); }

    void finish(DTM50_Result& out, bool any_legal) const {
        if (!any_legal)
        {
            out.has_dtz = true;
            return;
        }
        if (skipped.unpin(best_wdl))
            return;
        out.has_dtz = true;
        out.dtz     = best_wdl == WDL_Entry::DRAW ? 0 : best_dtz;
    }

   private:
    WDL_Entry        best_wdl = WDL_Entry::LOSE;
    uint16_t         best_dtz = 0;
    Skipped_Children skipped;
};

std::optional<uint16_t> derive_dtm(Position& pos, WDL_Entry wdl, int depth) {
    if (depth >= MAX_DERIVE_DEPTH)
        return std::nullopt;
    const WDL_Entry pinned = fold_dtm_wdl(wdl);

    bool             any_legal = false;
    WDL_Entry        best_wdl  = WDL_Entry::LOSE;
    uint16_t         best_dtm  = 0;
    Skipped_Children skipped;

    if (visit_legal_moves(pos, [&](Move, bool) {
            any_legal = true;
            WDL_Entry cw;
            uint16_t  cd;
            if (is_bare_kings(pos))
            {
                cw = WDL_Entry::DRAW;
                cd = 0;
            }
            else if (pos.ep_square() != SQ_NONE)
            {
                const Probe_Result cr = probe_dtm_impl(pos, IGNORE_50MR, depth + 1);
                if (cr.wdl == WDL_Entry::ILLEGAL)
                {
                    skipped.unknown();
                    return false;
                }
                if (!cr.has_dtm)
                {
                    skipped.of_class(invert_wdl(fold_dtm_wdl(cr.wdl)));
                    return false;
                }
                cw = cr.wdl;
                cd = cr.dtm;
            }
            else
            {
                if (child_class_is_forced(pinned))
                    cw = WDL_Entry::WIN;
                else
                {
                    cw = probe_wdl_internal(pos, depth + 1);
                    if (cw == WDL_Entry::ILLEGAL)
                    {
                        skipped.unknown();
                        return false;
                    }
                    if (below_pinned_class(pinned, invert_wdl(fold_dtm_wdl(cw))))
                        return false;
                }
                const auto child_dtm = probe_dtm_internal(pos, cw, depth + 1);
                if (!child_dtm)
                {
                    skipped.of_class(invert_wdl(fold_dtm_wdl(cw)));
                    return false;
                }
                cd = *child_dtm;
            }

            const WDL_Entry my_wdl = invert_wdl(fold_dtm_wdl(cw));
            const uint16_t  my_dtm = static_cast<uint16_t>(1u + cd);
            if (prefer_new(my_wdl, my_dtm, best_wdl, best_dtm))
            {
                best_wdl = my_wdl;
                best_dtm = my_dtm;
            }
            return is_win_class(pinned) && best_wdl == pinned && best_dtm == 1;
        }))
        return best_dtm;
    if (!any_legal)
        return uint16_t{0};
    if (skipped.unpin(best_wdl))
        return std::nullopt;
    if (best_wdl == WDL_Entry::WIN || best_wdl == WDL_Entry::LOSE)
        return best_dtm;
    return uint16_t{0};
}

DTM50_Result derive_dtm50(Position& pos, WDL_Entry wdl, unsigned rule50, int depth) {
    if (depth >= MAX_DERIVE_DEPTH)
        return {};
    const WDL_Entry pinned = fold_50mr_wdl(wdl);

    bool             any_legal = false;
    WDL_Entry        best_wdl  = WDL_Entry::LOSE;
    uint16_t         best_dtm  = 0;
    Skipped_Children skipped;

    if (visit_legal_moves(pos, [&](Move, bool c_is_zeroing) {
            any_legal                   = true;
            const unsigned child_rule50 = c_is_zeroing ? 0u : rule50 + 1u;

            DTM50_Result cd;
            if (is_bare_kings(pos))
                cd = {WDL_Entry::DRAW, 0};
            else if (child_rule50 >= DTM50_HMC_COUNT)
                cd = is_checkmate(pos) ? DTM50_Result{WDL_Entry::LOSE, 0}
                                       : DTM50_Result{WDL_Entry::DRAW, 0};
            else if (pos.ep_square() != SQ_NONE)
            {
                const Probe_Result cr = probe_dtm_impl(pos, child_rule50, depth + 1);
                if (cr.wdl == WDL_Entry::ILLEGAL)
                {
                    skipped.unknown();
                    return false;
                }
                if (!cr.has_dtm50)
                {
                    skipped.of_class(invert_wdl(fold_50mr_wdl(cr.wdl)));
                    return false;
                }
                cd = {cr.dtm50_wdl, cr.dtm50};
            }
            else
            {
                WDL_Entry cw;
                if (child_class_is_forced(wdl))
                    cw = WDL_Entry::WIN;
                else
                {
                    cw = probe_wdl_internal(pos, depth + 1);
                    if (cw == WDL_Entry::ILLEGAL)
                    {
                        skipped.unknown();
                        return false;
                    }
                    if (below_pinned_class(wdl, invert_wdl(cw)))
                        return false;
                }
                cd = probe_dtm50_internal(pos, cw, child_rule50, depth + 1);
                if (cd.wdl == WDL_Entry::ILLEGAL)
                {
                    skipped.of_class(invert_wdl(fold_50mr_wdl(cw)));
                    return false;
                }
            }

            const WDL_Entry my_wdl = invert_wdl(fold_50mr_wdl(cd.wdl));
            const uint16_t  my_dtm = static_cast<uint16_t>(1u + cd.dtm);
            if (prefer_new(my_wdl, my_dtm, best_wdl, best_dtm))
            {
                best_wdl = my_wdl;
                best_dtm = my_dtm;
            }
            return is_win_class(pinned) && best_wdl == pinned && best_dtm == 1;
        }))
        return {best_wdl, best_dtm};
    if (!any_legal)
        return {pos.checkers() ? WDL_Entry::LOSE : WDL_Entry::DRAW, 0};
    if (skipped.unpin(best_wdl))
        return {};
    if (best_wdl == WDL_Entry::WIN || best_wdl == WDL_Entry::LOSE)
        return {best_wdl, best_dtm};
    return {WDL_Entry::DRAW, 0};
}

// The DTZ minimax needs every child, so a caller after DTM alone (with_dtz
// false) skips it and stops at a mate in one.
DTM50_Result derive_dtm50_flat(Position& pos, WDL_Entry wdl, int depth, bool with_dtz) {
    if (depth >= MAX_DERIVE_DEPTH)
        return {};
    const WDL_Entry pinned   = fold_dtm_wdl(wdl);
    const int       castling = castling_count(pos);

    bool             any_legal = false;
    WDL_Entry        best_wdl  = WDL_Entry::LOSE;
    uint16_t         best_dtm  = 0;
    DTZ_Minimax      dtz;
    Skipped_Children skipped;

    const bool pinned_mate = visit_legal_moves(pos, [&](Move, bool c_is_zeroing) {
        any_legal = true;
        WDL_Entry cw;
        uint16_t  cd;
        if (is_bare_kings(pos))
        {
            cw = WDL_Entry::DRAW;
            cd = 0;
            dtz.zeroing_child(WDL_Entry::DRAW);
        }
        else if (pos.ep_square() != SQ_NONE)
        {
            const Probe_Result cr = probe_dtm_impl(pos, IGNORE_50MR, depth + 1);
            if (cr.wdl == WDL_Entry::ILLEGAL)
            {
                skipped.unknown();
                dtz.unwalked();
                return false;
            }
            dtz.zeroing_child(cr.wdl);
            if (!cr.has_dtm)
            {
                skipped.of_class(invert_wdl(fold_dtm_wdl(cr.wdl)));
                return false;
            }
            cw = cr.wdl;
            cd = cr.dtm;
        }
        else
        {
            WDL_Entry raw_wdl;
            if (child_class_is_forced(wdl))
                raw_wdl = WDL_Entry::WIN;
            else
            {
                raw_wdl = probe_wdl_internal(pos, depth + 1);
                if (raw_wdl == WDL_Entry::ILLEGAL)
                {
                    skipped.unknown();
                    dtz.unwalked();
                    return false;
                }
                if (below_pinned_class(pinned, invert_wdl(fold_dtm_wdl(raw_wdl))))
                    return false;
            }
            const DTM50_Result child =
              probe_dtm50_internal(pos, raw_wdl, IGNORE_50MR, depth + 1, with_dtz);
            if (c_is_zeroing)
                dtz.zeroing_child(raw_wdl);
            else
                dtz.quiet_child(raw_wdl, child, is_cursed_exit(castling, pos, raw_wdl));
            if (child.wdl == WDL_Entry::ILLEGAL)
            {
                skipped.of_class(invert_wdl(fold_dtm_wdl(raw_wdl)));
                return false;
            }
            cw = child.wdl;
            cd = child.dtm;
        }

        const WDL_Entry my_wdl = invert_wdl(fold_dtm_wdl(cw));
        const uint16_t  my_dtm = static_cast<uint16_t>(1u + cd);
        if (prefer_new(my_wdl, my_dtm, best_wdl, best_dtm))
        {
            best_wdl = my_wdl;
            best_dtm = my_dtm;
        }
        return !with_dtz && is_win_class(pinned) && best_wdl == pinned && best_dtm == 1;
    });

    DTM50_Result out;
    if (pinned_mate)
        out = {best_wdl, best_dtm};
    else if (!any_legal)
        out.wdl = pos.checkers() ? WDL_Entry::LOSE : WDL_Entry::DRAW;
    else if (!skipped.unpin(best_wdl))
    {
        const bool decisive = best_wdl == WDL_Entry::WIN || best_wdl == WDL_Entry::LOSE;
        out.wdl             = decisive ? best_wdl : WDL_Entry::DRAW;
        out.dtm             = decisive ? best_dtm : 0;
    }
    if (with_dtz)
        dtz.finish(out, any_legal);
    return out;
}

std::optional<uint16_t> probe_dtm_internal(Position& pos, WDL_Entry wdl, int depth) {
    const Canonical_Root c = canonical_root(pos);
    DTM_File*            d = open_dtm(c.ps);
    if (!d)
        return std::nullopt;

    const Frame_Read fr = locate_frame(*d, c.ps, c.pos, wdl);
    if (!fr.readable)
        return derive_dtm(pos, wdl, depth);
    return d->read(fr.frame, fr.pos, wdl);
}

// The DTM50 file at rule50, or (IGNORE_50MR) its unclocked layer: DTM, and the
// DTZ from the same cell; an unstored frame derives the DTZ only with_dtz.
DTM50_Result
probe_dtm50_internal(Position& pos, WDL_Entry wdl, unsigned rule50, int depth, bool with_dtz) {
    const bool flat = rule50 == IGNORE_50MR;
    assert(flat || rule50 < DTM50_HMC_COUNT);
    const Canonical_Root c = canonical_root(pos);
    DTM50_File*          m = open_dtm50(c.ps);
    if (!m)
        return {};

    auto from_cell = [&](const DTM50_Cell& cell) {
        DTM50_Result r = cell.value == DRAWN_AT_LAYER
                         ? DTM50_Result{WDL_Entry::DRAW, 0}
                         : DTM50_Result{flat ? wdl : fold_50mr_wdl(wdl), cell.value};
        if (!flat)
            return r;
        if (const auto dtz = dtz_from_draw_flip(cell.draw_flip, wdl, pos))
        {
            r.has_dtz = true;
            r.dtz     = *dtz;
        }
        return r;
    };

    const Frame_Read fr = locate_frame(*m, c.ps, c.pos, wdl);
    if (!fr.readable)
        return flat ? derive_dtm50_flat(pos, wdl, depth, with_dtz)
                    : derive_dtm50(pos, wdl, rule50, depth);
    return from_cell(m->read(fr.frame, fr.pos, wdl, rule50));
}

std::optional<DTC_Cell>
probe_dtc_internal(Position& pos, WDL_Entry wdl, unsigned rule50, int depth);

bool prefer_new_dtc(WDL_Entry new_wdl,
                    uint16_t  new_order,
                    uint16_t  new_value,
                    WDL_Entry old_wdl,
                    uint16_t  old_order,
                    uint16_t  old_value) {
    const int nr = wdl_rank(new_wdl), orank = wdl_rank(old_wdl);
    if (nr != orank)
        return nr > orank;
    const bool win_side  = new_wdl == WDL_Entry::WIN || new_wdl == WDL_Entry::CURSED_WIN;
    const bool loss_side = new_wdl == WDL_Entry::LOSE || new_wdl == WDL_Entry::BLESSED_LOSS;
    if (win_side)
        return (new_order != old_order) ? new_order < old_order : new_value < old_value;
    if (loss_side)
        return (new_order != old_order) ? new_order > old_order : new_value > old_value;
    return false;
}

struct Unbounded_Fold {
    explicit Unbounded_Fold(WDL_Entry mover_class) :
        winning(mover_class == WDL_Entry::WIN || mover_class == WDL_Entry::CURSED_WIN) {}

    void zeroing_child() { offer(1); }
    void quiet_child(uint16_t child_dtz, bool cursed_exit, bool child_rounded) {
        if (child_dtz == DRAWN_AT_LAYER)
            drawn = true;
        else
        {
            offer(quiet_move_dtz(child_dtz, cursed_exit, child_rounded));
            any_rounded |= child_rounded;
        }
    }

    uint16_t value() const { return (have && !drawn) ? best : DRAWN_AT_LAYER; }
    bool     rounded() const { return any_rounded; }

   private:
    void offer(uint16_t v) {
        if (!have || (winning ? v < best : v > best))
            best = v;
        have = true;
    }

    bool     winning;
    bool     have        = false;
    bool     drawn       = false;
    bool     any_rounded = false;
    uint16_t best        = 0;
};

bool read_dtc_curve(Position& pos, WDL_Entry wdl, DTC_Curve& out) {
    const Canonical_Root c = canonical_root(pos);
    DTC_File*            d = open_dtc(c.ps);
    if (!d)
        return false;
    const Frame_Read fr = locate_frame(*d, c.ps, c.pos, wdl);
    if (!fr.readable)
        return false;
    d->read_curve(fr.frame, fr.pos, wdl, out);
    return true;
}

// The child is on the board.
std::optional<DTC_Cell>
child_dtc_cell(Position& pos, WDL_Entry cw, unsigned child_rule50, int depth) {
    if (pos.ep_square() == SQ_NONE)
        return probe_dtc_internal(pos, cw, child_rule50, depth);

    const Probe_Result cr = probe_dtc_impl(pos, child_rule50, depth);
    if (!cr.has_dtc || !cr.has_dtz)
        return std::nullopt;
    DTC_Cell cell;
    cell.dtz         = cr.dtz;
    cell.dtz_rounded = cr.dtz_rounded;
    if (cr.dtc_wdl != WDL_Entry::DRAW)
    {
        cell.order = cr.dtc_order;
        cell.value = cr.dtc;
    }
    return cell;
}

// Whether the side to move wins (a cursed win doesn't count) by one of its en
// passant captures; nullopt if none does and one can't be probed.
std::optional<bool> ep_conversion_wins(Position& pos, int depth) {
    bool unknown = false;
    if (visit_legal_ep_captures<false>(pos, [&](Move, bool) {
            const WDL_Entry gw =
              is_bare_kings(pos) ? WDL_Entry::DRAW : probe_wdl_impl(pos, depth + 1);
            if (gw == WDL_Entry::ILLEGAL)
            {
                unknown = true;
                return false;
            }
            return fold_50mr_wdl(invert_wdl(gw)) == WDL_Entry::WIN;
        }))
        return true;
    if (unknown)
        return std::nullopt;
    return false;
}

bool is_conversion(const Position& child, Move m) {
    return m.type_of() == PROMOTION || child.captured_piece() != NO_PIECE;
}

std::optional<DTC_Cell> derive_dtc_win(Position& pos, unsigned rule50, int depth) {
    bool             have         = false;
    uint16_t         best_order   = 0;
    uint16_t         best_value   = 0;
    const unsigned   budget_plies = dtc_budget_plies(rule50);
    Unbounded_Fold   unbounded(WDL_Entry::WIN);
    Skipped_Children skipped;

    const bool pinned = visit_legal_moves(pos, [&](Move m, bool c_is_zeroing) {
        const bool conversion = is_conversion(pos, m);
        const bool push       = !conversion && c_is_zeroing;
        if (is_bare_kings(pos))
            return false;

        const WDL_Entry cw = probe_wdl_impl(pos, depth + 1);
        if (cw == WDL_Entry::ILLEGAL)
        {
            skipped.unknown();
            return false;
        }
        if (cw != WDL_Entry::LOSE)
            return false;

        uint16_t order = 0;
        uint16_t value = 1;
        if (conversion)
            unbounded.zeroing_child();
        else
        {
            const unsigned child_rule50 = push ? 0u : (rule50 == IGNORE_50MR ? 1u : rule50 + 1u);
            const auto     cell         = child_dtc_cell(pos, cw, child_rule50, depth + 1);
            if (!cell)
            {
                skipped.unknown();
                return false;
            }
            if (push)
                unbounded.zeroing_child();
            else
                unbounded.quiet_child(cell->dtz, false, false);
            if (!cell->priced())
                return false;
            order = static_cast<uint16_t>(cell->order + (push ? 1 : 0));
            value = push ? uint16_t{1} : static_cast<uint16_t>(cell->value + 1);
        }
        if (value > budget_plies)
            return false;

        if (!have || order < best_order || (order == best_order && value < best_value))
        {
            best_order = order;
            best_value = value;
            have       = true;
        }
        return best_order == 0 && best_value == 1;
    });
    if (!pinned && skipped.unpin(WDL_Entry::WIN))
        return std::nullopt;

    DTC_Cell out;
    out.dtz = pinned ? uint16_t{1} : unbounded.value();
    if (have)
    {
        out.order = best_order;
        out.value = best_value;
    }
    return out;
}

std::optional<DTC_Cell> derive_dtc_loss(Position& pos, unsigned rule50, int depth) {
    uint16_t       worst[DTC_PACK_LAYERS] = {};
    bool           drawn[DTC_PACK_LAYERS] = {};
    Unbounded_Fold unbounded(WDL_Entry::LOSE);

    const auto raise = [&](size_t k, uint16_t val) {
        if (val > DTZ_MAX_NON_CURSED)
            drawn[k] = true;
        else if (val > worst[k])
            worst[k] = val;
    };

    bool any_legal = false;

    if (visit_legal_moves(pos, [&](Move m, bool c_is_zeroing) {
            any_legal = true;

            // Only a capture can bare the kings, so `conversion` already covers that.
            const bool conversion = is_conversion(pos, m);
            if (conversion)
            {
                for (size_t k = 0; k < DTC_PACK_LAYERS; ++k)
                    raise(k, 1);
                unbounded.zeroing_child();
                return false;
            }

            const bool push = c_is_zeroing;

            if (pos.ep_square() != SQ_NONE)
            {
                const std::optional<bool> ep_wins = ep_conversion_wins(pos, depth + 1);
                if (!ep_wins)
                    return true;
                if (*ep_wins)
                {
                    for (size_t k = 0; k < DTC_PACK_LAYERS; ++k)
                        raise(k, 1);
                    unbounded.zeroing_child();
                    return false;
                }
            }

            DTC_Curve curve;
            if (!read_dtc_curve(pos, WDL_Entry::WIN, curve))
                return true;

            if (push)
                unbounded.zeroing_child();
            else
                unbounded.quiet_child(curve.value[DTC_BUDGET_LAYERS], false, false);
            for (size_t k = 0; k < DTC_PACK_LAYERS; ++k)
            {
                const uint16_t v = curve.value[k];
                if (v == DRAWN_AT_LAYER)
                    drawn[k] = true;
                else
                    raise(k, push ? uint16_t{1} : static_cast<uint16_t>(v + 1));
            }
            return false;
        }))
        return std::nullopt;

    if (!any_legal)
        return DTC_Cell{0, 0, 0};

    DTC_Cell out;
    out.dtz                     = unbounded.value();
    const unsigned budget_plies = dtc_budget_plies(rule50);
    for (size_t k = 0; k < DTC_PACK_LAYERS; ++k)
        if (!drawn[k] && worst[k] <= budget_plies)
        {
            out.order = static_cast<uint16_t>(k);
            out.value = worst[k];
            break;
        }
    return out;
}

std::optional<DTC_Cell> derive_dtc_cursed(Position& pos, WDL_Entry wdl, int depth) {
    const bool       winning  = wdl == WDL_Entry::CURSED_WIN;
    const int        castling = castling_count(pos);
    Unbounded_Fold   unbounded(wdl);
    bool             any_legal = false;
    Skipped_Children skipped;

    const bool pinned = visit_legal_moves(pos, [&](Move m, bool c_is_zeroing) {
        any_legal = true;

        const bool conversion = is_conversion(pos, m);
        const bool push       = !conversion && c_is_zeroing;
        if (is_bare_kings(pos))
        {
            // Baring the kings takes a capture, and from a blessed loss such a
            // capture would be an outright draw, not a loss.
            return false;
        }

        const WDL_Entry cw = probe_wdl_impl(pos, depth + 1);
        if (cw == WDL_Entry::ILLEGAL)
        {
            skipped.unknown();
            return false;
        }
        if (winning && cw != WDL_Entry::LOSE && cw != WDL_Entry::BLESSED_LOSS)
            return false;
        // A blessed loss counts only the moves that keep it blessed: into a
        // cursed win, or quietly into a win already at the horizon.
        if (!winning && cw != WDL_Entry::CURSED_WIN && (conversion || push))
            return false;

        if (conversion || push)
        {
            unbounded.zeroing_child();
            return winning && invert_wdl(cw) == wdl;
        }
        const auto cell = probe_dtc_internal(pos, cw, IGNORE_50MR, depth + 1);
        if (!cell)
        {
            skipped.unknown();
            return false;
        }
        if (!winning && cw == WDL_Entry::WIN && cell->dtz < DTZ_MAX_NON_CURSED)
            return false;
        unbounded.quiet_child(cell->dtz, is_cursed_exit(castling, pos, cw), cell->dtz_rounded);
        return false;
    });
    if (!pinned && skipped.unpin(wdl))
        return std::nullopt;

    DTC_Cell out;
    if (any_legal)
        out.dtz = pinned ? uint16_t{1} : unbounded.value();
    out.dtz_rounded = !pinned && unbounded.rounded();
    return out;
}

std::optional<DTC_Cell> derive_dtc(Position& pos, WDL_Entry wdl, unsigned rule50, int depth) {
    if (depth >= MAX_DERIVE_DEPTH)
        return std::nullopt;
    if (wdl == WDL_Entry::WIN)
        return derive_dtc_win(pos, rule50, depth);
    if (wdl == WDL_Entry::LOSE)
        return derive_dtc_loss(pos, rule50, depth);
    return derive_dtc_cursed(pos, wdl, depth);
}

std::optional<DTC_Cell>
probe_dtc_internal(Position& pos, WDL_Entry wdl, unsigned rule50, int depth) {
    const Canonical_Root c = canonical_root(pos);
    DTC_File*            d = open_dtc(c.ps);
    if (!d)
        return std::nullopt;

    const Frame_Read fr = locate_frame(*d, c.ps, c.pos, wdl);
    if (!fr.readable)
        return derive_dtc(pos, wdl, rule50, depth);
    return d->read(fr.frame, fr.pos, wdl, rule50);
}

void fill_dtm50_from_bounds(Probe_Result& r, unsigned rule50) {
    assert(rule50 < DTM50_HMC_COUNT);
    if (fold_50mr_wdl(r.wdl) == WDL_Entry::DRAW
        || (r.has_dtz && layer_busted_by_dtz(r.wdl, r.dtz, rule50)))
    {
        r.dtm50_wdl = WDL_Entry::DRAW;
    }
    else if (r.has_dtm && dtm50_layer_pinned_by_dtm(r.wdl, r.dtm, rule50))
    {
        r.dtm50_wdl = r.wdl;
        r.dtm50     = r.dtm;
    }
    else
        return;
    r.has_dtm50 = true;
}

void fill_dtc_from_bounds(Probe_Result& r, bool has_pawns, unsigned rule50) {
    if (rule50 == IGNORE_50MR)
        return;
    const bool clean = r.wdl == WDL_Entry::WIN || r.wdl == WDL_Entry::LOSE;
    if (!clean)
    {
        r.has_dtc = true;
        r.dtc_wdl = WDL_Entry::DRAW;
        return;
    }
    if (!r.has_dtz)
        return;

    const bool busted = r.dtz > dtc_budget_plies(rule50);
    if (has_pawns && !busted)
        return;
    r.has_dtc = true;
    r.dtc_wdl = busted ? WDL_Entry::DRAW : r.wdl;
    r.dtc     = busted ? 0 : r.dtz;
}

// DTM, and DTM50 at rule50 (none at IGNORE_50MR). The unclocked layer first
// (DTM, and the DTZ with it): most clocks are settled by its bounds (the mate
// fits in what the clock leaves, or the conversion doesn't); only the rest read
// the clocked layer. Without DTM50 only the bounds can answer, from DTM and DTZ.
Probe_Result probe_dtm_impl(Position& pos, unsigned rule50, int depth) {
    Probe_Result         r;
    const Canonical_Root c            = canonical_root(pos);
    const bool           clocked      = rule50 != IGNORE_50MR;
    const bool           rule50_drawn = clocked && rule50 >= DTM50_HMC_COUNT;
    const bool           live         = clocked && !rule50_drawn;
    r.wdl                             = probe_wdl_internal(pos, depth);
    if (r.wdl == WDL_Entry::ILLEGAL)
        return r;

    if (r.wdl == WDL_Entry::DRAW)
        r.has_dtm = true;
    else if (open_dtm50(c.ps))
    {
        const DTM50_Result d50 = probe_dtm50_internal(pos, r.wdl, IGNORE_50MR, depth, live);
        r.dtm                  = d50.dtm;
        r.has_dtm              = d50.wdl != WDL_Entry::ILLEGAL;
        r.dtz                  = d50.dtz;
        r.has_dtz              = d50.has_dtz;
        if (live)
            fill_dtm50_from_bounds(r, rule50);
        if (live && !r.has_dtm50)
        {
            const DTM50_Result d50r = probe_dtm50_internal(pos, r.wdl, rule50, depth);
            r.dtm50_wdl             = d50r.wdl;
            r.dtm50                 = d50r.dtm;
            r.has_dtm50             = d50r.wdl != WDL_Entry::ILLEGAL;
        }
    }
    else
    {
        if (const auto dtm = probe_dtm_internal(pos, r.wdl, depth))
            r.dtm = *dtm, r.has_dtm = true;
        if (live)
            fill_dtm50_from_bounds(r, rule50);
        if (live && !r.has_dtm50)
        {
            if (pos.pieces(PAWN))
                if (const auto cell = probe_dtc_internal(pos, r.wdl, IGNORE_50MR, depth))
                    r.dtz = cell->dtz, r.has_dtz = true;
            if (!r.has_dtz)
                if (const auto dtz = probe_dtz_internal(pos, r.wdl, depth))
                    r.dtz = dtz->dtz, r.has_dtz = true;
            fill_dtm50_from_bounds(r, rule50);
        }
    }
    if (clocked && (r.wdl == WDL_Entry::DRAW || rule50_drawn))
    {
        // Past the clock only mate is decided.
        r.dtm50_wdl =
          r.wdl == WDL_Entry::LOSE && is_checkmate(pos) ? WDL_Entry::LOSE : WDL_Entry::DRAW;
        r.has_dtm50 = true;
    }
    if (pos.ep_square() == SQ_NONE)
        return r;

    Probe_Result best           = r;
    WDL_Entry    best_dtm_wdl   = fold_dtm_wdl(r.wdl);
    uint16_t     best_dtm       = r.has_dtm ? r.dtm : 0;
    WDL_Entry    best_dtm50_wdl = r.has_dtm50 ? r.dtm50_wdl : fold_50mr_wdl(r.wdl);
    uint16_t     best_dtm50     = r.has_dtm50 ? r.dtm50 : 0;
    bool         any_ep         = false;
    if (rule50_drawn)
    {
        // Even an ep capture costs a ply; the claim is already there.
        best.dtm50_wdl = WDL_Entry::DRAW;
        best.has_dtm50 = true;
    }
    if (visit_legal_ep_captures(pos, [&](Move, bool non_ep) {
            Probe_Result cr;
            if (is_bare_kings(pos))
            {
                cr.wdl       = WDL_Entry::DRAW;
                cr.has_dtm   = best.has_dtm;
                cr.dtm50_wdl = WDL_Entry::DRAW;
                cr.has_dtm50 = best.has_dtm50;
            }
            else
                cr = probe_dtm_impl(pos, live ? 0u : IGNORE_50MR, depth + 1);
            if (cr.wdl == WDL_Entry::ILLEGAL)
                return true;
            const WDL_Entry my_wdl = invert_wdl(cr.wdl);
            if (!non_ep && !any_ep)
            {
                best.wdl     = my_wdl;
                best_dtm_wdl = fold_dtm_wdl(my_wdl);
                best_dtm     = cr.has_dtm ? static_cast<uint16_t>(1u + cr.dtm) : 0;
                best.dtm =
                  cr.has_dtm && (best_dtm_wdl == WDL_Entry::WIN || best_dtm_wdl == WDL_Entry::LOSE)
                    ? best_dtm
                    : 0;
                best.has_dtm = cr.has_dtm;
                if (live)
                {
                    best_dtm50_wdl = cr.has_dtm50 ? invert_wdl(cr.dtm50_wdl) : WDL_Entry::ILLEGAL;
                    best_dtm50     = cr.has_dtm50 ? static_cast<uint16_t>(1u + cr.dtm50) : 0;
                    best.dtm50_wdl = best_dtm50_wdl;
                    best.dtm50 =
                      cr.has_dtm50
                          && (best_dtm50_wdl == WDL_Entry::WIN || best_dtm50_wdl == WDL_Entry::LOSE)
                        ? best_dtm50
                        : 0;
                    best.has_dtm50 = cr.has_dtm50;
                }
                any_ep = true;
                return false;
            }
            any_ep = true;
            if (wdl_rank(my_wdl) > wdl_rank(best.wdl))
                best.wdl = my_wdl;
            if (!cr.has_dtm)
                best.has_dtm = false;
            else if (best.has_dtm)
            {
                const WDL_Entry my_dtm_wdl = fold_dtm_wdl(my_wdl);
                const uint16_t  my_dtm     = static_cast<uint16_t>(1u + cr.dtm);
                if (prefer_new(my_dtm_wdl, my_dtm, best_dtm_wdl, best_dtm))
                {
                    best_dtm_wdl = my_dtm_wdl;
                    best_dtm     = my_dtm;
                    best.dtm =
                      (my_dtm_wdl == WDL_Entry::WIN || my_dtm_wdl == WDL_Entry::LOSE) ? my_dtm : 0;
                }
            }
            if (!live)
                return false;
            if (!cr.has_dtm50)
                best.has_dtm50 = false;
            else if (best.has_dtm50)
            {
                const WDL_Entry my_dtm50_wdl = invert_wdl(cr.dtm50_wdl);
                const uint16_t  my_dtm50     = static_cast<uint16_t>(1u + cr.dtm50);
                if (prefer_new(my_dtm50_wdl, my_dtm50, best_dtm50_wdl, best_dtm50))
                {
                    best_dtm50_wdl = my_dtm50_wdl;
                    best_dtm50     = my_dtm50;
                    best.dtm50_wdl = my_dtm50_wdl;
                    best.dtm50 = (my_dtm50_wdl == WDL_Entry::WIN || my_dtm50_wdl == WDL_Entry::LOSE)
                                 ? my_dtm50
                                 : 0;
                }
            }
            return false;
        }))
        return {};
    return any_ep ? best : r;
}

// DTZ, and DTC at rule50 (none at IGNORE_50MR). A pawn material's DTC cell
// carries both; otherwise the DTC comes from DTZ's bounds.
Probe_Result probe_dtc_impl(Position& pos, unsigned rule50, int depth) {
    Probe_Result r;
    const bool   has_pawns    = pos.pieces(PAWN);
    const bool   clocked      = rule50 != IGNORE_50MR;
    const bool   rule50_drawn = clocked && rule50 >= DTM50_HMC_COUNT;
    const bool   live         = clocked && !rule50_drawn;
    r.wdl                     = probe_wdl_internal(pos, depth);
    if (r.wdl == WDL_Entry::ILLEGAL)
        return r;

    // A DTC cell serves its DTZ row too. Clocked, it is read first for its DTC;
    // unclocked, after DTM50's exact DTZ.
    auto from_cell = [&](unsigned at) {
        if (const auto cell = probe_dtc_internal(pos, r.wdl, at, depth))
        {
            if (clocked)
            {
                r.dtc_wdl   = cell->priced() ? r.wdl : WDL_Entry::DRAW;
                r.dtc_order = cell->priced() ? cell->order : 0;
                r.dtc       = cell->priced() ? cell->value : 0;
                r.has_dtc   = true;
            }
            r.dtz         = cell->dtz;
            r.dtz_rounded = cell->dtz_rounded;
            r.has_dtz     = true;
        }
    };
    if (r.wdl == WDL_Entry::DRAW)
    {
        r.has_dtz = true;
        if (clocked)
        {
            r.dtc_wdl = WDL_Entry::DRAW;
            r.has_dtc = true;
        }
    }
    else
    {
        if (has_pawns && clocked)
            from_cell(rule50);
        if (!r.has_dtz)
        {
            const DTM50_Result d50 = probe_dtm50_internal(pos, r.wdl, IGNORE_50MR, depth, true);
            r.dtz                  = d50.dtz;
            r.has_dtz              = d50.has_dtz;
        }
        if (!r.has_dtz && has_pawns && !clocked)
            from_cell(IGNORE_50MR);
        if (!r.has_dtz)
            if (const auto dtz = probe_dtz_internal(pos, r.wdl, depth))
            {
                r.dtz         = dtz->dtz;
                r.dtz_rounded = dtz->rounded;
                r.has_dtz     = true;
            }
        if (!r.has_dtc)
            fill_dtc_from_bounds(r, has_pawns, rule50);
    }
    if (pos.ep_square() == SQ_NONE)
        return r;

    Probe_Result best           = r;
    WDL_Entry    best_dtz_wdl   = r.wdl;
    uint16_t     best_dtz       = r.has_dtz ? r.dtz : 0;
    WDL_Entry    best_dtc_wdl   = r.has_dtc ? r.dtc_wdl : r.wdl;
    uint16_t     best_dtc_order = r.has_dtc ? r.dtc_order : 0;
    uint16_t     best_dtc       = r.has_dtc ? r.dtc : 0;
    bool         any_ep         = false;
    if (rule50_drawn)
    {
        // Even an ep capture costs a ply; the claim is already there.
        best.dtc_wdl = WDL_Entry::DRAW;
        best.has_dtc = true;
    }
    if (visit_legal_ep_captures(pos, [&](Move, bool non_ep) {
            const WDL_Entry cw =
              is_bare_kings(pos) ? WDL_Entry::DRAW : probe_wdl_internal(pos, depth + 1);
            if (cw == WDL_Entry::ILLEGAL)
                return true;
            const WDL_Entry my_wdl = invert_wdl(cw);
            if (!non_ep && !any_ep)
            {
                best.wdl         = my_wdl;
                best_dtz_wdl     = my_wdl;
                best_dtz         = 1;
                best.dtz         = my_wdl == WDL_Entry::DRAW ? 0 : 1;
                best.dtz_rounded = false;
                best.has_dtz     = true;
                if (live)
                {
                    best_dtc_wdl   = fold_50mr_wdl(my_wdl);
                    best_dtc_order = 0;
                    best_dtc       = 1;
                    best.dtc_wdl   = best_dtc_wdl;
                    best.dtc_order = 0;
                    best.dtc       = best_dtc_wdl == WDL_Entry::DRAW ? 0 : 1;
                    best.has_dtc   = true;
                }
                any_ep = true;
                return false;
            }
            any_ep = true;
            if (wdl_rank(my_wdl) > wdl_rank(best.wdl))
                best.wdl = my_wdl;
            if (wdl_rank(my_wdl) > wdl_rank(best_dtz_wdl)
                || (best.has_dtz && prefer_new(my_wdl, 1, best_dtz_wdl, best_dtz)))
            {
                best_dtz_wdl     = my_wdl;
                best_dtz         = 1;
                best.dtz         = my_wdl == WDL_Entry::DRAW ? 0 : 1;
                best.dtz_rounded = false;
                best.has_dtz     = true;
            }
            if (!live)
                return false;
            const WDL_Entry my_dtc_wdl = fold_50mr_wdl(my_wdl);
            if (wdl_rank(my_dtc_wdl) > wdl_rank(best_dtc_wdl)
                || (best.has_dtc
                    && prefer_new_dtc(my_dtc_wdl, 0, 1, best_dtc_wdl, best_dtc_order, best_dtc)))
            {
                best_dtc_wdl   = my_dtc_wdl;
                best_dtc_order = 0;
                best_dtc       = 1;
                best.dtc_wdl   = my_dtc_wdl;
                best.dtc_order = 0;
                best.dtc       = my_dtc_wdl != WDL_Entry::DRAW ? 1 : 0;
                best.has_dtc   = true;
            }
            return false;
        }))
        return {};
    return any_ep ? best : r;
}

// The engine's metric in a probe (DTM50 under the 50-move rule, DTM without
// it): Win, Loss or Draw, and the plies to mate.
bool mate_of(const Probe_Result& r, bool rule50, WDL_Entry& wdl, int& plies) {
    if (rule50 ? !r.has_dtm50 : !r.has_dtm)
        return false;
    wdl   = rule50 ? r.dtm50_wdl : fold_dtm_wdl(r.wdl);
    plies = wdl == WDL_Entry::DRAW ? 0 : rule50 ? r.dtm50 : r.dtm;
    return true;
}

// The child's answer after m. A draw by repetition at the child is a known draw
// in either metric.
Probe_Result probe_mate_after_move(Position& pos, Move m, bool rule50) {
    pos.do_move(m);
    Probe_Result r;
    if (!pos.is_repetition(1) && !is_bare_kings(pos))
        r = probe_dtm_impl(pos, rule50 ? unsigned(pos.rule50_count()) : IGNORE_50MR, 0);
    else
    {
        r.wdl       = WDL_Entry::DRAW;
        r.has_dtm   = true;
        r.dtm50_wdl = WDL_Entry::DRAW;
        r.has_dtm50 = true;
    }
    pos.undo_move(m);
    return r;
}

// A move's distance to mate from the mover's side: the child's, one ply more.
bool move_mate_of(const Probe_Result& child, bool rule50, WDL_Entry& wdl, int& plies) {
    WDL_Entry child_wdl;
    int       child_plies;
    if (!mate_of(child, rule50, child_wdl, child_plies))
        return false;
    wdl   = invert_wdl(child_wdl);
    plies = child_wdl == WDL_Entry::DRAW ? 0 : child_plies + 1;
    return true;
}

int signed_dtz(WDL_Entry w, int dtz) {
    const int v = dtz;
    switch (w)
    {
    case WDL_Entry::WIN :
        return v;
    case WDL_Entry::CURSED_WIN :
        return v;
    case WDL_Entry::BLESSED_LOSS :
        return -v;
    case WDL_Entry::LOSE :
        return -v;
    case WDL_Entry::DRAW :
    case WDL_Entry::ILLEGAL :
    default :
        return 0;
    }
}

int zeroing_signed_dtz(WDL_Entry my_wdl) {
    switch (my_wdl)
    {
    case WDL_Entry::WIN :
        return 1;
    case WDL_Entry::CURSED_WIN :
        return 101;
    case WDL_Entry::DRAW :
        return 0;
    case WDL_Entry::BLESSED_LOSS :
        return -101;
    case WDL_Entry::LOSE :
        return -1;
    case WDL_Entry::ILLEGAL :
    default :
        return 0;
    }
}

Value mate_value(WDL_Entry wdl, int plies) {
    if (wdl == WDL_Entry::DRAW)
        return VALUE_DRAW;
    const Value v = plies < MAX_PLY ? VALUE_MATE - plies : VALUE_MATE - MAX_PLY - 1;
    return wdl == WDL_Entry::WIN ? v : -v;
}

}  // namespace

namespace ChessTB {

using namespace Tablebases;

namespace {
// Every DTM and DTM50 is at most 2045 plies.
constexpr usize MAX_LINE = 2048;

// extend_pv's walk so far, kept between outputs by the thread that outputs;
// a walk from an older epoch is dropped.
struct Walk {
    uint64_t                  epoch    = 0;
    Key                       history  = 0;
    bool                      rule50   = false;
    bool                      seekMate = false;  // whether the line takes distance steps only
    int                       clock    = 0;
    ValueList<Move, MAX_LINE> line;
    bool                      known = false;  // the distance at the line's end
    WDL_Entry                 wdl   = WDL_Entry::DRAW;
    int                       plies = 0;
};
thread_local Walk walk;
}
void set_cache_size(size_t mb) { blocks->set_max_bytes(mb << 20); }

WDLScore probe_wdl(Position& pos, ProbeState* result) {
    const WDL_Entry wdl = probe_wdl_impl(pos, 0);
    *result             = wdl == WDL_Entry::ILLEGAL ? FAIL : OK;
    return wdl == WDL_Entry::ILLEGAL ? WDLDraw : WDLScore(wdl_rank(wdl) - 2);
}

// Ranks root moves by DTZ. DTZ is exact but for a cursed one, which may read a
// ply short: only an order among moves that rank after every clean one.
static bool root_probe(Position&                    pos,
                       Search::RootMoves&           rootMoves,
                       bool                         rule50,
                       bool                         rankDistance,
                       const std::function<bool()>& time_abort) {
    // The clock counts with the rule off too, as in Syzygy, so play keeps
    // converting; only the score bound depends on the rule.
    const int  cnt50  = pos.rule50_count();
    const bool rep    = pos.has_repeated();
    const int  bound  = rule50 ? (MAX_DTZ / 2 - 100) : 1;
    const int  rights = castling_count(pos);

    // Each move's DTZ from the root (positive wins), and DTC's pawn pushes
    // owed and plies to the next zeroing move, where it has them.
    struct MoveDtz {
        int       dtz   = 0;
        WDL_Entry mine  = WDL_Entry::DRAW;
        int       order = -1;
        int       wait  = 0;
    };
    std::vector<MoveDtz> moves(rootMoves.size());

    for (usize k = 0; k < rootMoves.size(); ++k)
    {
        const Move move   = rootMoves[k].pv[0];
        const bool zeroes = is_zeroing(pos, move);
        MoveDtz&   md     = moves[k];
        pos.do_move(move);

        bool ok = true;
        if (zeroes)
        {
            const WDL_Entry childWdl =
              is_bare_kings(pos) ? WDL_Entry::DRAW : probe_wdl_impl(pos, 0);
            ok      = childWdl != WDL_Entry::ILLEGAL;
            md.mine = invert_wdl(childWdl);
            md.dtz  = zeroing_signed_dtz(md.mine);
        }
        else if (pos.is_repetition(1))
            md.dtz = 0;
        else
        {
            std::optional<DTZ_Value> cd = DTZ_Value{0, false};
            WDL_Entry                cw = WDL_Entry::DRAW;
            if (!is_bare_kings(pos))
            {
                const Probe_Result cr = probe_dtc_impl(pos, IGNORE_50MR, 0);
                cw                    = cr.wdl;
                cd = cr.has_dtz ? std::optional<DTZ_Value>(DTZ_Value{cr.dtz, cr.dtz_rounded})
                                : std::nullopt;
            }
            ok              = cw != WDL_Entry::ILLEGAL && cd;
            md.mine         = invert_wdl(cw);
            const int child = ok ? -signed_dtz(cw, cd->dtz) : 0;
            if (ok && cw == WDL_Entry::LOSE && cd->dtz == 0)
                md.dtz = 1;  // mate
            else if (child != 0)
            {
                const bool exit = is_cursed_exit(rights, pos, cw);
                const int  dtz =
                  quiet_move_dtz(static_cast<uint16_t>(std::abs(child)), exit, cd->rounded);
                const bool cursed =
                  cw == WDL_Entry::CURSED_WIN || cw == WDL_Entry::BLESSED_LOSS || dtz > 100;
                // Cursed moves rank after every clean one, by their distance.
                md.dtz = (child > 0 ? 1 : -1) * (cursed ? 100 + dtz : dtz);
                if (dtz > int(DTZ_MAX_NON_CURSED))
                    md.mine = md.mine == WDL_Entry::WIN  ? WDL_Entry::CURSED_WIN
                            : md.mine == WDL_Entry::LOSE ? WDL_Entry::BLESSED_LOSS
                                                         : md.mine;
            }
        }
        if (rankDistance && ok && md.dtz != 0)
        {
            const bool conversion = is_conversion(pos, move);
            // A push to mate is preferred at any budget.
            const bool mate = is_checkmate(pos);
            if ((conversion || mate) && (md.mine == WDL_Entry::WIN || md.mine == WDL_Entry::LOSE))
            {
                md.order = 0;
                md.wait  = 1;
            }
            else if (!conversion)
            {
                const unsigned     child_rule50 = zeroes ? 0u
                                                : rule50 ? static_cast<unsigned>(cnt50) + 1u
                                                         : 1u;
                const Probe_Result cr           = probe_dtc_impl(pos, child_rule50, 0);
                if (cr.has_dtc && (cr.dtc_wdl == WDL_Entry::WIN || cr.dtc_wdl == WDL_Entry::LOSE))
                {
                    const bool spends = zeroes && md.mine == WDL_Entry::WIN;
                    md.order          = int(cr.dtc_order) + (spends ? 1 : 0);
                    md.wait           = zeroes ? 1 : int(cr.dtc) + 1;
                }
            }
        }
        pos.undo_move(move);

        if (time_abort() || !ok)
            return false;
    }

    for (usize k = 0; k < rootMoves.size(); ++k)
    {
        auto&     m   = rootMoves[k];
        const int dtz = moves[k].dtz;
        // Ranking by distance: by DTC's pushes owed, then plies (DTC's, else
        // DTZ). ChessTB sorts by the pair; here one key (plies <= 100), as
        // tbRank is search's group.
        const int order = std::max(moves[k].order, 0);
        const int plies = moves[k].order >= 0 ? moves[k].wait : std::abs(dtz);
        const int key   = rankDistance ? order * 128 + plies : 0;
        // A win is certain while it wins at this clock, a loss unless a
        // 50-move draw is in sight. DTZ 1 (zeroing or mate) decides at any
        // root clock, repeated or not, as a push to mate is preferred at any
        // budget.
        const int got =
          dtz > 0 ? (dtz == 1 || (dtz + cnt50 <= 100 && !rep) ? MAX_DTZ - key
                                                              : MAX_DTZ / 2 - (dtz + cnt50))
          : dtz < 0
            ? (dtz == -1 || -dtz * 2 + cnt50 < 100 ? -MAX_DTZ + key : -MAX_DTZ / 2 + (-dtz + cnt50))
            : 0;
        m.tbRank  = got;
        m.tbScore = got >= bound ? VALUE_MATE - MAX_PLY - 1
                  : got > 0 ? Value((std::max(3, got - (MAX_DTZ / 2 - 200)) * int(PawnValue)) / 200)
                  : got == 0 ? VALUE_DRAW
                  : got > -bound
                    ? Value((std::min(-3, got + (MAX_DTZ / 2 - 200)) * int(PawnValue)) / 200)
                    : -VALUE_MATE + MAX_PLY + 1;
    }
    return true;
}

// Ranks root moves by DTM50 (50-move rule) or DTM, each distance its own rank.
// False when a child has no answer.
static bool root_probe_mate(Position&                    pos,
                            Search::RootMoves&           rootMoves,
                            bool                         rule50,
                            const std::function<bool()>& time_abort) {
    for (auto& m : rootMoves)
    {
        const Probe_Result cr = probe_mate_after_move(pos, m.pv[0], rule50);
        WDL_Entry          wdl;
        int                plies;
        if (!move_mate_of(cr, rule50, wdl, plies) || time_abort())
            return false;
        // No root clock check: as in search, each child stands at its own
        // clock.
        m.tbRank  = wdl == WDL_Entry::WIN  ? MAX_DTZ - plies
                  : wdl == WDL_Entry::LOSE ? -MAX_DTZ + plies
                                           : 0;
        m.tbScore = mate_value(wdl, plies);

        // A win the clock draws stays just above a draw, ranked by DTM; such
        // a loss just below.
        if (wdl == WDL_Entry::DRAW && rule50)
        {
            const WDL_Entry cls = fold_dtm_wdl(invert_wdl(cr.wdl));
            if (cls == WDL_Entry::DRAW)
                continue;
            if (!cr.has_dtm)
                return false;
            const int  flat = int(cr.dtm) + 1;
            const bool win  = cls == WDL_Entry::WIN;
            m.tbRank  = win ? std::max(1, MAX_DTZ / 2 - flat) : std::min(-1, flat - MAX_DTZ / 2);
            m.tbScore = win ? VALUE_DRAW + 2 : VALUE_DRAW - 2;
        }
    }
    return true;
}

void probe_dtm(Position& pos, bool rule50, int& dtm, int& dtm50) {
    const Probe_Result r =
      probe_dtm_impl(pos, rule50 ? static_cast<unsigned>(pos.rule50_count()) : IGNORE_50MR, 0);
    auto read = [&](bool clocked) {
        WDL_Entry w;
        int       d;
        if (!mate_of(r, clocked, w, d))
            return 0;
        return w == WDL_Entry::WIN ? d : w == WDL_Entry::LOSE ? -d : 0;
    };
    dtm   = read(false);
    dtm50 = read(true);
}

void probe_dtc(Position& pos, int& dtz, int& dtc, int& order) {
    const Probe_Result r  = probe_dtc_impl(pos, static_cast<unsigned>(pos.rule50_count()), 0);
    const bool         ok = r.wdl != WDL_Entry::ILLEGAL;
    dtz                   = ok && r.has_dtz ? signed_dtz(r.wdl, r.dtz) : 0;
    const bool c          = ok && r.has_dtc;
    order                 = c ? r.dtc_order : 0;
    dtc                   = !c                           ? 0
                          : r.dtc_wdl == WDL_Entry::WIN  ? int(r.dtc)
                          : r.dtc_wdl == WDL_Entry::LOSE ? -int(r.dtc)
                                                         : 0;
}

// From the PV's first position in the tables, the tables' line until mate, a
// draw or the deadline (a share of Move Overhead: a read can decode a block).
// v becomes the mate when the line proves one.
void extend_pv(const OptionsMap& options,
               const Search::LimitsType&,
               Position&         root,
               Search::RootMove& rootMove,
               Value&            v,
               std::size_t       multiPV) {
    if (rootMove.pv.empty())
        return;
    // The line is walked on a copy and never undone: it can outrun the plies
    // a position keeps states for.
    const auto walker = std::make_unique<Position>();
    Position&  pos    = *walker;
    const auto replay = [&](const auto& moves, usize n) {
        pos.set(root.fen(), root.is_chess960());
        pos.copy_state_infos(root);
        for (usize i = 0; i < n; ++i)
            pos.do_move(moves[i]);
    };
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::microseconds(1000 * int(options["Move Overhead"]) / multiPV);
    const bool rule50   = bool(options["ChessTB50MoveRule"]);
    const bool seekMate = is_mate_or_mated(v);
    const auto late     = [deadline] { return std::chrono::steady_clock::now() >= deadline; };
    // Search's line up to its first position within ChessTBProbeLimit.
    const int  limit    = std::min(int(options["ChessTBProbeLimit"]), MaxCardinality);
    const auto searched = rootMove.pv;
    usize      k        = 0;
    replay(searched, 0);
    while (k < searched.size() && (k == 0 || pos.count<ALL_PIECES>() > limit))
        pos.do_move(searched[k++]);
    if (pos.count<ALL_PIECES>() > limit || late())
        return;
    rootMove.pv.resize(k);

    // The line depends on the keys a repetition check sees: an ordered digest
    // of them, as trustworthy as the 64-bit keys the checks compare.
    Key       history = 0;
    const int seen    = pos.rep_window();
    for (int i = 0; i <= seen; ++i)
        history = history * 0x9E3779B97F4A7C15ULL + pos.key_before(i);

    // The line proves a mate only if the distance probe right after the root
    // move succeeded.
    WDL_Entry wdl   = WDL_Entry::DRAW;
    int       plies = 0;
    bool      known =
      mate_of(probe_dtm_impl(pos, rule50 ? unsigned(pos.rule50_count()) : IGNORE_50MR, 0), rule50,
              wdl, plies);
    const bool exact = k == 1 && known;
    // A draw at the clock there outranks the root's TB score: a WDL-ranked root
    // can't see the clock (as Syzygy's extension, which sets VALUE_DRAW too).
    const bool drawn = exact && wdl == WDL_Entry::DRAW;

    // Outputs mostly enter at the same position: continue the kept walk. Its
    // moves are checked as TT moves are: a digest collision must not replay an
    // illegal one.
    const uint64_t e    = epoch.load(std::memory_order_relaxed);
    bool           kept = walk.epoch == e && walk.history == history && walk.rule50 == rule50
             && walk.seekMate == seekMate && walk.clock == pos.rule50_count();
    if (kept)
        for (const Move m : walk.line)
        {
            if (!pos.legal(m))
            {
                replay(searched, k);
                rootMove.pv.resize(k);
                kept = false;
                break;
            }
            pos.do_move(m);
            rootMove.pv.push_back(m);
        }
    if (kept)
        known = walk.known, wdl = walk.wdl, plies = walk.plies;
    else
        walk = {e, history, rule50, seekMate, pos.rule50_count(), {}};

    // Each ply, play the first move that is one ply closer to mate. Where the
    // tables have no distance, play the root ranking's top move instead,
    // unless only a mate is sought.
    while (walk.line.size() < MAX_LINE && !late())
    {
        Move next = Move::none();
        if (known)
        {
            if (wdl == WDL_Entry::DRAW || plies <= 0)
                break;
            for (const Move m : MoveList<ALL>(pos))
            {
                if (late())
                    break;
                WDL_Entry move_wdl;
                int       move_plies;
                if (move_mate_of(probe_mate_after_move(pos, m, rule50), rule50, move_wdl,
                                 move_plies)
                    && move_wdl == wdl && move_plies == plies)
                {
                    next = m;
                    break;
                }
            }
        }
        else if (!seekMate && !(rule50 && pos.is_draw(0)))
        {
            Search::RootMoves moves;
            for (const Move m : MoveList<ALL>(pos))
                moves.emplace_back(m);
            if (moves.empty() || !root_probe(pos, moves, rule50, true, late))
                break;
            const auto top = std::max_element(
              moves.begin(), moves.end(), [](const Search::RootMove& a, const Search::RootMove& b) {
                  return a.tbRank < b.tbRank;
              });
            if (top->tbRank != 0)
                next = top->pv[0];
        }
        if (next == Move::none())
            break;
        pos.do_move(next);
        rootMove.pv.push_back(next);
        walk.line.push_back(next);
        if (known)
            wdl = invert_wdl(wdl), plies -= 1;
        else
            known =
              mate_of(probe_dtm_impl(pos, rule50 ? unsigned(pos.rule50_count()) : IGNORE_50MR, 0),
                      rule50, wdl, plies);
    }
    walk.known = known, walk.wdl = wdl, walk.plies = plies;

    const bool mated = is_checkmate(pos);
    if (rootMove.pv.size() == k)  // no table move: search's line stays
        rootMove.pv = searched;
    if (drawn)
    {
        v = VALUE_DRAW;
        return;
    }
    const int length = int(rootMove.pv.size());
    if (!mated || !exact || length >= MAX_PLY)  // past MAX_PLY no mate score
        return;
    v = length % 2 ? mate_in(length) : mated_in(length);  // odd: the root's side gave mate
}

static bool root_probe_wdl(Position& pos, Search::RootMoves& rootMoves, bool rule50) {
    static const int WDL_to_rank[] = {-MAX_DTZ, -MAX_DTZ + 101, 0, MAX_DTZ - 101, MAX_DTZ};

    for (auto& m : rootMoves)
    {
        pos.do_move(m.pv[0]);
        // With WDL alone a quiet win at clock 100 would rank as a win.
        WDL_Entry cls = WDL_Entry::DRAW;
        if (!(rule50 && pos.is_draw(1)) && !pos.is_repetition(1))
            cls = is_bare_kings(pos) ? WDL_Entry::DRAW : invert_wdl(probe_wdl_impl(pos, 0));
        pos.undo_move(m.pv[0]);
        if (cls == WDL_Entry::ILLEGAL)
            return false;

        if (!rule50)
            cls = fold_dtm_wdl(cls);
        m.tbRank  = WDL_to_rank[wdl_rank(cls)];
        m.tbScore = WDL_to_value[wdl_rank(cls)];
    }
    return true;
}

Config rank_root_moves(const OptionsMap&            options,
                       Position&                    pos,
                       Search::RootMoves&           rootMoves,
                       bool                         rankDistance,
                       const std::function<bool()>& time_abort) {
    Config config;
    if (rootMoves.empty())
        return config;

    config.useRule50      = bool(options["ChessTB50MoveRule"]);
    config.probeDepth     = int(options["ChessTBProbeDepth"]);
    config.castling       = true;
    config.mates          = true;
    config.mateProbeDepth = int(options["ChessTBMateProbeDepth"]);
    config.cardinality    = int(options["ChessTBProbeLimit"]);

    // Tables with fewer pieces than the probe limit are searched with
    // probeDepth == DEPTH_ZERO
    if (config.cardinality > MaxCardinality)
    {
        config.cardinality = MaxCardinality;
        config.probeDepth  = 0;
    }

    bool dtzAvailable = true;
    if (config.cardinality >= pos.count<ALL_PIECES>())
    {
        // Rank by distance if checkmate is the only zeroing move
        rankDistance = rankDistance || pos.dtz_is_dtm();

        // Exact distance to mate where there is one, else DTZ
        config.rootInTB = root_probe_mate(pos, rootMoves, config.useRule50, time_abort)
                       || root_probe(pos, rootMoves, config.useRule50, rankDistance, time_abort);
        if (!config.rootInTB && !time_abort())
        {
            // DTZ is missing: rank by WDL
            dtzAvailable    = false;
            config.rootInTB = root_probe_wdl(pos, rootMoves, config.useRule50);
        }
    }

    if (config.rootInTB)
    {
        std::stable_sort(
          rootMoves.begin(), rootMoves.end(),
          [](const Search::RootMove& a, const Search::RootMove& b) { return a.tbRank > b.tbRank; });
        // Probe during search only if DTZ is not available and we are winning
        if (dtzAvailable || rootMoves[0].tbScore <= VALUE_DRAW)
            config.cardinality = 0;
    }
    else
        for (auto& m : rootMoves)
            m.tbRank = 0;

    return config;
}

}  // namespace ChessTB

}  // namespace Stockfish
