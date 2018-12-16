/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2019 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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

#include <algorithm>
#include <cassert>
#include <cstring>   // For std::memset
#include <iomanip>
#include <sstream>

#include "bitboard.h"
#include "evaluate.h"
#include "material.h"
#include "pawns.h"
#include "thread.h"

namespace Trace {

  enum Tracing { NO_TRACE, TRACE };

  enum Term { // The first 8 entries are reserved for PieceType
    MATERIAL = 8, IMBALANCE, MOBILITY, THREAT, PASSED, SPACE, INITIATIVE, TOTAL, TERM_NB
  };

  Score scores[TERM_NB][COLOR_NB];

  double to_cp(Value v) { return double(v) / PawnValueEg; }

  void add(int idx, Color c, Score s) {
    scores[idx][c] = s;
  }

  void add(int idx, Score w, Score b = SCORE_ZERO) {
    scores[idx][WHITE] = w;
    scores[idx][BLACK] = b;
  }

  std::ostream& operator<<(std::ostream& os, Score s) {
    os << std::setw(5) << to_cp(mg_value(s)) << " "
       << std::setw(5) << to_cp(eg_value(s));
    return os;
  }

  std::ostream& operator<<(std::ostream& os, Term t) {

    if (t == MATERIAL || t == IMBALANCE || t == INITIATIVE || t == TOTAL)
        os << " ----  ----"    << " | " << " ----  ----";
    else
        os << scores[t][WHITE] << " | " << scores[t][BLACK];

    os << " | " << scores[t][WHITE] - scores[t][BLACK] << "\n";
    return os;
  }
}

using namespace Trace;

namespace {

  constexpr Bitboard QueenSide   = FileABB | FileBBB | FileCBB | FileDBB;
  constexpr Bitboard CenterFiles = FileCBB | FileDBB | FileEBB | FileFBB;
  constexpr Bitboard KingSide    = FileEBB | FileFBB | FileGBB | FileHBB;
  constexpr Bitboard Center      = (FileDBB | FileEBB) & (Rank4BB | Rank5BB);

  constexpr Bitboard KingFlank[FILE_NB] = {
    QueenSide ^ FileDBB, QueenSide, QueenSide,
    CenterFiles, CenterFiles,
    KingSide, KingSide, KingSide ^ FileEBB
  };

  // Threshold for lazy and space evaluation
  constexpr Value LazyThreshold  = Value(1500);
  constexpr Value SpaceThreshold = Value(12222);

  // KingAttackWeights[PieceType] contains king attack weights by piece type
  constexpr int KingAttackWeights[PIECE_TYPE_NB] = { 0, 0, 77, 55, 44, 10 };

  // Penalties for enemy's safe checks
  constexpr int QueenSafeCheck  = 780;
  constexpr int RookSafeCheck   = 880;
  constexpr int BishopSafeCheck = 435;
  constexpr int KnightSafeCheck = 790;

#define S(mg, eg) make_score(mg, eg)

  // MobilityBonus[PieceType-2][attacked] contains bonuses for middle and end game,
  // indexed by piece type and number of attacked squares in the mobility area.
  constexpr Score MobilityBonus[][32] = {
    { S(-62,-81), S(-53,-56), S(-12,-30), S( -4,-14), S(  3,  8), S( 13, 15), // Knights
      S( 22, 23), S( 28, 27), S( 33, 33) },
    { S(-48,-59), S(-20,-23), S( 16, -3), S( 26, 13), S( 38, 24), S( 51, 42), // Bishops
      S( 55, 54), S( 63, 57), S( 63, 65), S( 68, 73), S( 81, 78), S( 81, 86),
      S( 91, 88), S( 98, 97) },
    { S(-58,-76), S(-27,-18), S(-15, 28), S(-10, 55), S( -5, 69), S( -2, 82), // Rooks
      S(  9,112), S( 16,118), S( 30,132), S( 29,142), S( 32,155), S( 38,165),
      S( 46,166), S( 48,169), S( 58,171) },
    { S(-39,-36), S(-21,-15), S(  3,  8), S(  3, 18), S( 14, 34), S( 22, 54), // Queens
      S( 28, 61), S( 41, 73), S( 43, 79), S( 48, 92), S( 56, 94), S( 60,104),
      S( 60,113), S( 66,120), S( 67,123), S( 70,126), S( 71,133), S( 73,136),
      S( 79,140), S( 88,143), S( 88,148), S( 99,166), S(102,170), S(102,175),
      S(106,184), S(109,191), S(113,206), S(116,212) }
  };

  // Outpost[knight/bishop][supported by pawn] contains bonuses for minor
  // pieces if they occupy or can reach an outpost square, bigger if that
  // square is supported by a pawn.
  constexpr Score Outpost[][2] = {
    { S(22, 6), S(36,12) }, // Knight
    { S( 9, 2), S(15, 5) }  // Bishop
  };

  // RookOnFile[semiopen/open] contains bonuses for each rook when there is
  // no (friendly) pawn on the rook file.
  constexpr Score RookOnFile[] = { S(18, 7), S(44, 20) };

  // ThreatByMinor/ByRook[attacked PieceType] contains bonuses according to
  // which piece type attacks which one. Attacks on lesser pieces which are
  // pawn-defended are not considered.
  constexpr Score ThreatByMinor[PIECE_TYPE_NB] = {
    S(0, 0), S(0, 31), S(39, 42), S(57, 44), S(68, 112), S(62, 120)
  };

  constexpr Score ThreatByRook[PIECE_TYPE_NB] = {
    S(0, 0), S(0, 24), S(38, 71), S(38, 61), S(0, 38), S(51, 38)
  };

  // PassedRank[Rank] contains a bonus according to the rank of a passed pawn
  constexpr Score PassedRank[RANK_NB] = {
    S(0, 0), S(5, 18), S(12, 23), S(10, 31), S(57, 62), S(163, 167), S(271, 250)
  };

  // PassedFile[File] contains a bonus according to the file of a passed pawn
  constexpr Score PassedFile[FILE_NB] = {
    S( -1,  7), S( 0,  9), S(-9, -8), S(-30,-14),
    S(-30,-14), S(-9, -8), S( 0,  9), S( -1,  7)
  };

  // Assorted bonuses and penalties
  constexpr Score BishopPawns        = S(  3,  7);
  constexpr Score CloseEnemies       = S(  8,  0);
  constexpr Score CorneredBishop     = S( 50, 50);
  constexpr Score Hanging            = S( 69, 36);
  constexpr Score KingProtector      = S(  7,  8);
  constexpr Score KnightOnQueen      = S( 16, 12);
  constexpr Score LongDiagonalBishop = S( 45,  0);
  constexpr Score MinorBehindPawn    = S( 18,  3);
  constexpr Score PawnlessFlank      = S( 17, 95);
  constexpr Score RestrictedPiece    = S(  7,  7);
  constexpr Score RookOnPawn         = S( 10, 32);
  constexpr Score SliderOnQueen      = S( 59, 18);
  constexpr Score ThreatByKing       = S( 24, 89);
  constexpr Score ThreatByPawnPush   = S( 48, 39);
  constexpr Score ThreatByRank       = S( 13,  0);
  constexpr Score ThreatBySafePawn   = S(173, 94);
  constexpr Score TrappedRook        = S( 96,  4);
  constexpr Score WeakQueen          = S( 49, 15);
  constexpr Score WeakUnopposedPawn  = S( 12, 23);

#undef S

  // Evaluation class computes and stores attacks tables and other working data
  template<Tracing T>
  class Evaluation {

  public:
    Evaluation() = delete;
    explicit Evaluation(const Position& p) : pos(p) {}
    Evaluation& operator=(const Evaluation&) = delete;
    Value value();

  private:
    template<Color Us> void initialize();
    template<Color Us, PieceType Pt> Score pieces();
    template<Color Us> Score king() const;
    template<Color Us> Score threats() const;
    template<Color Us> Score passed() const;
    template<Color Us> Score space() const;
    ScaleFactor scale_factor(Value eg) const;
    Score initiative(Value eg) const;

    const Position& pos;
    Material::Entry* me;
    Pawns::Entry* pe;
    Bitboard mobilityArea[COLOR_NB];
    Score mobility[COLOR_NB] = { SCORE_ZERO, SCORE_ZERO };

    // attackedBy[color][piece type] is a bitboard representing all squares
    // attacked by a given color and piece type. Special "piece types" which
    // is also calculated is ALL_PIECES.
    Bitboard attackedBy[COLOR_NB][PIECE_TYPE_NB];

    // attackedBy2[color] are the squares attacked by 2 pieces of a given color,
    // possibly via x-ray or by one pawn and one piece. Diagonal x-ray through
    // pawn or squares attacked by 2 pawns are not explicitly added.
    Bitboard attackedBy2[COLOR_NB];

    // kingRing[color] are the squares adjacent to the king, plus (only for a
    // king on its first rank) the squares two ranks in front. For instance,
    // if black's king is on g8, kingRing[BLACK] is f8, h8, f7, g7, h7, f6, g6
    // and h6. It is set to 0 when king safety evaluation is skipped.
    Bitboard kingRing[COLOR_NB];

    // kingAttackersCount[color] is the number of pieces of the given color
    // which attack a square in the kingRing of the enemy king.
    int kingAttackersCount[COLOR_NB];

    // kingAttackersWeight[color] is the sum of the "weights" of the pieces of
    // the given color which attack a square in the kingRing of the enemy king.
    // The weights of the individual piece types are given by the elements in
    // the KingAttackWeights array.
    int kingAttackersWeight[COLOR_NB];

    // kingAttacksCount[color] is the number of attacks by the given color to
    // squares directly adjacent to the enemy king. Pieces which attack more
    // than one square are counted multiple times. For instance, if there is
    // a white knight on g5 and black's king is on g8, this white knight adds 2
    // to kingAttacksCount[WHITE].
    int kingAttacksCount[COLOR_NB];
  };


  // Evaluation::initialize() computes king and pawn attacks, and the king ring
  // bitboard for a given color. This is done at the beginning of the evaluation.
  template<Tracing T> template<Color Us>
  void Evaluation<T>::initialize() {

    constexpr Color     Them = (Us == WHITE ? BLACK : WHITE);
    constexpr Direction Up   = (Us == WHITE ? NORTH : SOUTH);
    constexpr Direction Down = (Us == WHITE ? SOUTH : NORTH);
    constexpr Bitboard LowRanks = (Us == WHITE ? Rank2BB | Rank3BB: Rank7BB | Rank6BB);

    // Find our pawns that are blocked or on the first two ranks
    Bitboard b = pos.pieces(Us, PAWN) & (shift<Down>(pos.pieces()) | LowRanks);

    // Squares occupied by those pawns, by our king or queen, or controlled by enemy pawns
    // are excluded from the mobility area.
    mobilityArea[Us] = ~(b | pos.pieces(Us, KING, QUEEN) | pe->pawn_attacks(Them));

    // Initialise attackedBy bitboards for kings and pawns
    attackedBy[Us][KING] = pos.attacks_from<KING>(pos.square<KING>(Us));
    attackedBy[Us][PAWN] = pe->pawn_attacks(Us);
    attackedBy[Us][ALL_PIECES] = attackedBy[Us][KING] | attackedBy[Us][PAWN];
    attackedBy2[Us]            = attackedBy[Us][KING] & attackedBy[Us][PAWN];

    kingRing[Us] = kingAttackersCount[Them] = 0;

    // Init our king safety tables only if we are going to use them
    if (pos.non_pawn_material(Them) >= RookValueMg + KnightValueMg)
    {
        kingRing[Us] = attackedBy[Us][KING];
        if (relative_rank(Us, pos.square<KING>(Us)) == RANK_1)
            kingRing[Us] |= shift<Up>(kingRing[Us]);

        if (file_of(pos.square<KING>(Us)) == FILE_H)
            kingRing[Us] |= shift<WEST>(kingRing[Us]);

        else if (file_of(pos.square<KING>(Us)) == FILE_A)
            kingRing[Us] |= shift<EAST>(kingRing[Us]);

        kingAttackersCount[Them] = popcount(kingRing[Us] & pe->pawn_attacks(Them));
        kingRing[Us] &= ~double_pawn_attacks_bb<Us>(pos.pieces(Us, PAWN));
        kingAttacksCount[Them] = kingAttackersWeight[Them] = 0;
    }
  }


  // Evaluation::pieces() scores pieces of a given color and type
  template<Tracing T> template<Color Us, PieceType Pt>
  Score Evaluation<T>::pieces() {

    constexpr Color     Them = (Us == WHITE ? BLACK : WHITE);
    constexpr Direction Down = (Us == WHITE ? SOUTH : NORTH);
    constexpr Bitboard OutpostRanks = (Us == WHITE ? Rank4BB | Rank5BB | Rank6BB
                                                   : Rank5BB | Rank4BB | Rank3BB);
    const Square* pl = pos.squares<Pt>(Us);

    Bitboard b, bb;
    Square s;
    Score score = SCORE_ZERO;

    attackedBy[Us][Pt] = 0;

    while ((s = *pl++) != SQ_NONE)
    {
        // Find attacked squares, including x-ray attacks for bishops and rooks
        b = Pt == BISHOP ? attacks_bb<BISHOP>(s, pos.pieces() ^ pos.pieces(QUEEN))
          : Pt ==   ROOK ? attacks_bb<  ROOK>(s, pos.pieces() ^ pos.pieces(QUEEN) ^ pos.pieces(Us, ROOK))
                         : pos.attacks_from<Pt>(s);

        if (pos.blockers_for_king(Us) & s)
            b &= LineBB[pos.square<KING>(Us)][s];

        attackedBy2[Us] |= attackedBy[Us][ALL_PIECES] & b;
        attackedBy[Us][Pt] |= b;
        attackedBy[Us][ALL_PIECES] |= b;

        if (b & kingRing[Them])
        {
            kingAttackersCount[Us]++;
            kingAttackersWeight[Us] += KingAttackWeights[Pt];
            kingAttacksCount[Us] += popcount(b & attackedBy[Them][KING]);
        }

        int mob = popcount(b & mobilityArea[Us]);

        mobility[Us] += MobilityBonus[Pt - 2][mob];

        if (Pt == BISHOP || Pt == KNIGHT)
        {
            // Bonus if piece is on an outpost square or can reach one
            bb = OutpostRanks & ~pe->pawn_attacks_span(Them);
            if (bb & s)
                score += Outpost[Pt == BISHOP][bool(attackedBy[Us][PAWN] & s)] * 2;

            else if (bb &= b & ~pos.pieces(Us))
                score += Outpost[Pt == BISHOP][bool(attackedBy[Us][PAWN] & bb)];

            // Knight and Bishop bonus for being right behind a pawn
            if (shift<Down>(pos.pieces(PAWN)) & s)
                score += MinorBehindPawn;

            // Penalty if the piece is far from the king
            score -= KingProtector * distance(s, pos.square<KING>(Us));

            if (Pt == BISHOP)
            {
                // Penalty according to number of pawns on the same color square as the
                // bishop, bigger when the center files are blocked with pawns.
                Bitboard blocked = pos.pieces(Us, PAWN) & shift<Down>(pos.pieces());

                score -= BishopPawns * pe->pawns_on_same_color_squares(Us, s)
                                     * (1 + popcount(blocked & CenterFiles));

                // Bonus for bishop on a long diagonal which can "see" both center squares
                if (more_than_one(attacks_bb<BISHOP>(s, pos.pieces(PAWN)) & Center))
                    score += LongDiagonalBishop;
            }

            // An important Chess960 pattern: A cornered bishop blocked by a friendly
            // pawn diagonally in front of it is a very serious problem, especially
            // when that pawn is also blocked.
            if (   Pt == BISHOP
                && pos.is_chess960()
                && (s == relative_square(Us, SQ_A1) || s == relative_square(Us, SQ_H1)))
            {
                Direction d = pawn_push(Us) + (file_of(s) == FILE_A ? EAST : WEST);
                if (pos.piece_on(s + d) == make_piece(Us, PAWN))
                    score -= !pos.empty(s + d + pawn_push(Us))                ? CorneredBishop * 4
                            : pos.piece_on(s + d + d) == make_piece(Us, PAWN) ? CorneredBishop * 2
                                                                              : CorneredBishop;
            }
        }

        if (Pt == ROOK)
        {
            // Bonus for aligning rook with enemy pawns on the same rank/file
            if (relative_rank(Us, s) >= RANK_5)
                score += RookOnPawn * popcount(pos.pieces(Them, PAWN) & PseudoAttacks[ROOK][s]);

            // Bonus for rook on an open or semi-open file
            if (pe->semiopen_file(Us, file_of(s)))
                score += RookOnFile[bool(pe->semiopen_file(Them, file_of(s)))];

            // Penalty when trapped by the king, even more if the king cannot castle
            else if (mob <= 3)
            {
                File kf = file_of(pos.square<KING>(Us));
                if ((kf < FILE_E) == (file_of(s) < kf))
                    score -= (TrappedRook - make_score(mob * 22, 0)) * (1 + !pos.castling_rights(Us));
            }
        }

        if (Pt == QUEEN)
        {
            // Penalty if any relative pin or discovered attack against the queen
            Bitboard queenPinners;
            if (pos.slider_blockers(pos.pieces(Them, ROOK, BISHOP), s, queenPinners))
                score -= WeakQueen;
        }
    }
    if (T)
        Trace::add(Pt, Us, score);

    return score;
  }


  // Evaluation::king() assigns bonuses and penalties to a king of a given color
  template<Tracing T> template<Color Us>
  Score Evaluation<T>::king() const {

    constexpr Color    Them = (Us == WHITE ? BLACK : WHITE);
    constexpr Bitboard Camp = (Us == WHITE ? AllSquares ^ Rank6BB ^ Rank7BB ^ Rank8BB
                                           : AllSquares ^ Rank1BB ^ Rank2BB ^ Rank3BB);

    const Square ksq = pos.square<KING>(Us);
    Bitboard kingFlank, weak, b, b1, b2, safe, unsafeChecks;

    // King shelter and enemy pawns storm
    Score score = pe->king_safety<Us>(pos);

    // Find the squares that opponent attacks in our king flank, and the squares
    // which are attacked twice in that flank.
    kingFlank = KingFlank[file_of(ksq)];
    b1 = attackedBy[Them][ALL_PIECES] & kingFlank & Camp;
    b2 = b1 & attackedBy2[Them];

    int tropism = popcount(b1) + popcount(b2);

    // Main king safety evaluation
    if (kingAttackersCount[Them] > 1 - pos.count<QUEEN>(Them))
    {
        int kingDanger = 0;
        unsafeChecks = 0;

        // Attacked squares defended at most once by our queen or king
        weak =  attackedBy[Them][ALL_PIECES]
              & ~attackedBy2[Us]
              & (~attackedBy[Us][ALL_PIECES] | attackedBy[Us][KING] | attackedBy[Us][QUEEN]);

        // Analyse the safe enemy's checks which are possible on next move
        safe  = ~pos.pieces(Them);
        safe &= ~attackedBy[Us][ALL_PIECES] | (weak & attackedBy2[Them]);

        b1 = attacks_bb<ROOK  >(ksq, pos.pieces() ^ pos.pieces(Us, QUEEN));
        b2 = attacks_bb<BISHOP>(ksq, pos.pieces() ^ pos.pieces(Us, QUEEN));

        // Enemy queen safe checks
        if ((b1 | b2) & attackedBy[Them][QUEEN] & safe & ~attackedBy[Us][QUEEN])
            kingDanger += QueenSafeCheck;

        b1 &= attackedBy[Them][ROOK];
        b2 &= attackedBy[Them][BISHOP];

        // Enemy rooks checks
        if (b1 & safe)
            kingDanger += RookSafeCheck;
        else
            unsafeChecks |= b1;

        // Enemy bishops checks
        if (b2 & safe)
            kingDanger += BishopSafeCheck;
        else
            unsafeChecks |= b2;

        // Enemy knights checks
        b = pos.attacks_from<KNIGHT>(ksq) & attackedBy[Them][KNIGHT];
        if (b & safe)
            kingDanger += KnightSafeCheck;
        else
            unsafeChecks |= b;

        // Unsafe or occupied checking squares will also be considered, as long as
        // the square is in the attacker's mobility area.
        unsafeChecks &= mobilityArea[Them];

        kingDanger +=        kingAttackersCount[Them] * kingAttackersWeight[Them]
                     +  69 * kingAttacksCount[Them]
                     + 185 * popcount(kingRing[Us] & weak)
                     + 150 * popcount(pos.blockers_for_king(Us) | unsafeChecks)
                     +       tropism * tropism / 4
                     - 873 * !pos.count<QUEEN>(Them)
                     -   6 * mg_value(score) / 8
                     +       mg_value(mobility[Them] - mobility[Us])
                     -   30;

        // Transform the kingDanger units into a Score, and subtract it from the evaluation
        if (kingDanger > 0)
            score -= make_score(kingDanger * kingDanger / 4096, kingDanger / 16);
    }

    // Penalty when our king is on a pawnless flank
    if (!(pos.pieces(PAWN) & kingFlank))
        score -= PawnlessFlank;

    // King tropism bonus, to anticipate slow motion attacks on our king
    score -= CloseEnemies * tropism;

    if (T)
        Trace::add(KING, Us, score);

    return score;
  }


  // Evaluation::threats() assigns bonuses according to the types of the
  // attacking and the attacked pieces.
  template<Tracing T> template<Color Us>
  Score Evaluation<T>::threats() const {

    constexpr Color     Them     = (Us == WHITE ? BLACK   : WHITE);
    constexpr Direction Up       = (Us == WHITE ? NORTH   : SOUTH);
    constexpr Bitboard  TRank3BB = (Us == WHITE ? Rank3BB : Rank6BB);

    Bitboard b, weak, defended, nonPawnEnemies, stronglyProtected, safe, restricted;
    Score score = SCORE_ZERO;

    // Non-pawn enemies
    nonPawnEnemies = pos.pieces(Them) & ~pos.pieces(Them, PAWN);

    // Squares strongly protected by the enemy, either because they defend the
    // square with a pawn, or because they defend the square twice and we don't.
    stronglyProtected =  attackedBy[Them][PAWN]
                       | (attackedBy2[Them] & ~attackedBy2[Us]);

    // Non-pawn enemies, strongly protected
    defended = nonPawnEnemies & stronglyProtected;

    // Enemies not strongly protected and under our attack
    weak = pos.pieces(Them) & ~stronglyProtected & attackedBy[Us][ALL_PIECES];

    // Safe or protected squares
    safe = ~attackedBy[Them][ALL_PIECES] | attackedBy[Us][ALL_PIECES];

    // Bonus according to the kind of attacking pieces
    if (defended | weak)
    {
        b = (defended | weak) & (attackedBy[Us][KNIGHT] | attackedBy[Us][BISHOP]);
        while (b)
        {
            Square s = pop_lsb(&b);
            score += ThreatByMinor[type_of(pos.piece_on(s))];
            if (type_of(pos.piece_on(s)) != PAWN)
                score += ThreatByRank * (int)relative_rank(Them, s);
        }

        b = weak & attackedBy[Us][ROOK];
        while (b)
        {
            Square s = pop_lsb(&b);
            score += ThreatByRook[type_of(pos.piece_on(s))];
            if (type_of(pos.piece_on(s)) != PAWN)
                score += ThreatByRank * (int)relative_rank(Them, s);
        }

        if (weak & attackedBy[Us][KING])
            score += ThreatByKing;

        b =  ~attackedBy[Them][ALL_PIECES]
           | (nonPawnEnemies & attackedBy2[Us]);
        score += Hanging * popcount(weak & b);
    }

    // Bonus for restricting their piece moves
    restricted =   attackedBy[Them][ALL_PIECES]
                & ~stronglyProtected
                &  attackedBy[Us][ALL_PIECES];
    score += RestrictedPiece * popcount(restricted);

    // Bonus for enemy unopposed weak pawns
    if (pos.pieces(Us, ROOK, QUEEN))
        score += WeakUnopposedPawn * pe->weak_unopposed(Them);

    // Find squares where our pawns can push on the next move
    b  = shift<Up>(pos.pieces(Us, PAWN)) & ~pos.pieces();
    b |= shift<Up>(b & TRank3BB) & ~pos.pieces();

    // Keep only the squares which are relatively safe
    b &= ~attackedBy[Them][PAWN] & safe;

    // Bonus for safe pawn threats on the next move
    b = pawn_attacks_bb<Us>(b) & pos.pieces(Them);
    score += ThreatByPawnPush * popcount(b);

    // Our safe or protected pawns
    b = pos.pieces(Us, PAWN) & safe;

    b = pawn_attacks_bb<Us>(b) & nonPawnEnemies;
    score += ThreatBySafePawn * popcount(b);

    // Bonus for threats on the next moves against enemy queen
    if (pos.count<QUEEN>(Them) == 1)
    {
        Square s = pos.square<QUEEN>(Them);
        safe = mobilityArea[Us] & ~stronglyProtected;

        b = attackedBy[Us][KNIGHT] & pos.attacks_from<KNIGHT>(s);

        score += KnightOnQueen * popcount(b & safe);

        b =  (attackedBy[Us][BISHOP] & pos.attacks_from<BISHOP>(s))
           | (attackedBy[Us][ROOK  ] & pos.attacks_from<ROOK  >(s));

        score += SliderOnQueen * popcount(b & safe & attackedBy2[Us]);
    }

    if (T)
        Trace::add(THREAT, Us, score);

    return score;
  }

  // Evaluation::passed() evaluates the passed pawns and candidate passed
  // pawns of the given color.

  template<Tracing T> template<Color Us>
  Score Evaluation<T>::passed() const {

    constexpr Color     Them = (Us == WHITE ? BLACK : WHITE);
    constexpr Direction Up   = (Us == WHITE ? NORTH : SOUTH);

    auto king_proximity = [&](Color c, Square s) {
      return std::min(distance(pos.square<KING>(c), s), 5);
    };

    Bitboard b, bb, squaresToQueen, defendedSquares, unsafeSquares;
    Score score = SCORE_ZERO;

    b = pe->passed_pawns(Us);

    while (b)
    {
        Square s = pop_lsb(&b);

        assert(!(pos.pieces(Them, PAWN) & forward_file_bb(Us, s + Up)));

        int r = relative_rank(Us, s);

        Score bonus = PassedRank[r];

        if (r > RANK_3)
        {
            int w = (r-2) * (r-2) + 2;
            Square blockSq = s + Up;

            // Adjust bonus based on the king's proximity
            bonus += make_score(0, (  king_proximity(Them, blockSq) * 5
                                    - king_proximity(Us,   blockSq) * 2) * w);

            // If blockSq is not the queening square then consider also a second push
            if (r != RANK_7)
                bonus -= make_score(0, king_proximity(Us, blockSq + Up) * w);

            // If the pawn is free to advance, then increase the bonus
            if (pos.empty(blockSq))
            {
                // If there is a rook or queen attacking/defending the pawn from behind,
                // consider all the squaresToQueen. Otherwise consider only the squares
                // in the pawn's path attacked or occupied by the enemy.
                defendedSquares = unsafeSquares = squaresToQueen = forward_file_bb(Us, s);

                bb = forward_file_bb(Them, s) & pos.pieces(ROOK, QUEEN) & pos.attacks_from<ROOK>(s);

                if (!(pos.pieces(Us) & bb))
                    defendedSquares &= attackedBy[Us][ALL_PIECES];

                if (!(pos.pieces(Them) & bb))
                    unsafeSquares &= attackedBy[Them][ALL_PIECES] | pos.pieces(Them);

                // If there aren't any enemy attacks, assign a big bonus. Otherwise
                // assign a smaller bonus if the block square isn't attacked.
                int k = !unsafeSquares ? 20 : !(unsafeSquares & blockSq) ? 9 : 0;

                // If the path to the queen is fully defended, assign a big bonus.
                // Otherwise assign a smaller bonus if the block square is defended.
                if (defendedSquares == squaresToQueen)
                    k += 6;

                else if (defendedSquares & blockSq)
                    k += 4;

                bonus += make_score(k * w, k * w);
            }
        } // rank > RANK_3

        // Scale down bonus for candidate passers which need more than one
        // pawn push to become passed, or have a pawn in front of them.
        if (   !pos.pawn_passed(Us, s + Up)
            || (pos.pieces(PAWN) & forward_file_bb(Us, s)))
            bonus = bonus / 2;

        score += bonus + PassedFile[file_of(s)];
    }

    if (T)
        Trace::add(PASSED, Us, score);

    return score;
  }


  // Evaluation::space() computes the space evaluation for a given side. The
  // space evaluation is a simple bonus based on the number of safe squares
  // available for minor pieces on the central four files on ranks 2--4. Safe
  // squares one, two or three squares behind a friendly pawn are counted
  // twice. Finally, the space bonus is multiplied by a weight. The aim is to
  // improve play on game opening.

  template<Tracing T> template<Color Us>
  Score Evaluation<T>::space() const {

    if (pos.non_pawn_material() < SpaceThreshold)
        return SCORE_ZERO;

    constexpr Color Them = (Us == WHITE ? BLACK : WHITE);
    constexpr Bitboard SpaceMask =
      Us == WHITE ? CenterFiles & (Rank2BB | Rank3BB | Rank4BB)
                  : CenterFiles & (Rank7BB | Rank6BB | Rank5BB);

    // Find the available squares for our pieces inside the area defined by SpaceMask
    Bitboard safe =   SpaceMask
                   & ~pos.pieces(Us, PAWN)
                   & ~attackedBy[Them][PAWN];

    // Find all squares which are at most three squares behind some friendly pawn
    Bitboard behind = pos.pieces(Us, PAWN);
    behind |= (Us == WHITE ? behind >>  8 : behind <<  8);
    behind |= (Us == WHITE ? behind >> 16 : behind << 16);

    int bonus = popcount(safe) + popcount(behind & safe);
    int weight = pos.count<ALL_PIECES>(Us) - 2 * pe->open_files();

    Score score = make_score(bonus * weight * weight / 16, 0);

    if (T)
        Trace::add(SPACE, Us, score);

    return score;
  }


  // Evaluation::initiative() computes the initiative correction value
  // for the position. It is a second order bonus/malus based on the
  // known attacking/defending status of the players.

  template<Tracing T>
  Score Evaluation<T>::initiative(Value eg) const {

    int outflanking =  distance<File>(pos.square<KING>(WHITE), pos.square<KING>(BLACK))
                     - distance<Rank>(pos.square<KING>(WHITE), pos.square<KING>(BLACK));

    bool pawnsOnBothFlanks =   (pos.pieces(PAWN) & QueenSide)
                            && (pos.pieces(PAWN) & KingSide);

    // Compute the initiative bonus for the attacking side
    int complexity =   8 * pe->pawn_asymmetry()
                    + 12 * pos.count<PAWN>()
                    + 12 * outflanking
                    + 16 * pawnsOnBothFlanks
                    + 48 * !pos.non_pawn_material()
                    -118 ;

    // Now apply the bonus: note that we find the attacking side by extracting
    // the sign of the endgame value, and that we carefully cap the bonus so
    // that the endgame score will never change sign after the bonus.
    int v = ((eg > 0) - (eg < 0)) * std::max(complexity, -abs(eg));

    if (T)
        Trace::add(INITIATIVE, make_score(0, v));

    return make_score(0, v);
  }


  // Evaluation::scale_factor() computes the scale factor for the winning side

  template<Tracing T>
  ScaleFactor Evaluation<T>::scale_factor(Value eg) const {

    Color strongSide = eg > VALUE_DRAW ? WHITE : BLACK;
    int sf = me->scale_factor(pos, strongSide);

    // If scale is not already specific, scale down the endgame via general heuristics
    if (sf == SCALE_FACTOR_NORMAL)
    {
        if (   pos.opposite_bishops()
            && pos.non_pawn_material(WHITE) == BishopValueMg
            && pos.non_pawn_material(BLACK) == BishopValueMg)
            sf = 8 + 4 * pe->pawn_asymmetry();
        else
            sf = std::min(40 + (pos.opposite_bishops() ? 2 : 7) * pos.count<PAWN>(strongSide), sf);

    }

    return ScaleFactor(sf);
  }


  // Evaluation::value() is the main function of the class. It computes the various
  // parts of the evaluation and returns the value of the position from the point
  // of view of the side to move.

  template<Tracing T>
  Value Evaluation<T>::value() {

    assert(!pos.checkers());

    // Probe the material hash table
    me = Material::probe(pos);

    // If we have a specialized evaluation function for the current material
    // configuration, call it and return.
    if (me->specialized_eval_exists())
        return me->evaluate(pos);

    // Initialize score by reading the incrementally updated scores included in
    // the position object (material + piece square tables) and the material
    // imbalance. Score is computed internally from the white point of view.
    Score score = pos.psq_score() + me->imbalance() + pos.this_thread()->contempt;

    // Probe the pawn hash table
    pe = Pawns::probe(pos);
    score += pe->pawn_score(WHITE) - pe->pawn_score(BLACK);

    // Early exit if score is high
    Value v = (mg_value(score) + eg_value(score)) / 2;
    if (abs(v) > LazyThreshold)
       return pos.side_to_move() == WHITE ? v : -v;

    // Main evaluation begins here

    initialize<WHITE>();
    initialize<BLACK>();

    // Pieces should be evaluated first (populate attack tables)
    score +=  pieces<WHITE, KNIGHT>() - pieces<BLACK, KNIGHT>()
            + pieces<WHITE, BISHOP>() - pieces<BLACK, BISHOP>()
            + pieces<WHITE, ROOK  >() - pieces<BLACK, ROOK  >()
            + pieces<WHITE, QUEEN >() - pieces<BLACK, QUEEN >();

    score += mobility[WHITE] - mobility[BLACK];

    score +=  king<   WHITE>() - king<   BLACK>()
            + threats<WHITE>() - threats<BLACK>()
            + passed< WHITE>() - passed< BLACK>()
            + space<  WHITE>() - space<  BLACK>();

    score += initiative(eg_value(score));

    // Interpolate between a middlegame and a (scaled by 'sf') endgame score
    ScaleFactor sf = scale_factor(eg_value(score));
    v =  mg_value(score) * int(me->game_phase())
       + eg_value(score) * int(PHASE_MIDGAME - me->game_phase()) * sf / SCALE_FACTOR_NORMAL;

    v /= int(PHASE_MIDGAME);

    // In case of tracing add all remaining individual evaluation terms
    if (T)
    {
        Trace::add(MATERIAL, pos.psq_score());
        Trace::add(IMBALANCE, me->imbalance());
        Trace::add(PAWN, pe->pawn_score(WHITE), pe->pawn_score(BLACK));
        Trace::add(MOBILITY, mobility[WHITE], mobility[BLACK]);
        Trace::add(TOTAL, score);
    }

    return  (pos.side_to_move() == WHITE ? v : -v) // Side to move point of view
           + Eval::Tempo;
  }

} // namespace


/// evaluate() is the evaluator for the outer world. It returns a static
/// evaluation of the position from the point of view of the side to move.

Value Eval::evaluate(const Position& pos) {
  return Evaluation<NO_TRACE>(pos).value();
}


/// trace() is like evaluate(), but instead of returning a value, it returns
/// a string (suitable for outputting to stdout) that contains the detailed
/// descriptions and values of each evaluation term. Useful for debugging.

std::string Eval::trace(const Position& pos) {

  std::memset(scores, 0, sizeof(scores));

  pos.this_thread()->contempt = SCORE_ZERO; // Reset any dynamic contempt

  Value v = Evaluation<TRACE>(pos).value();

  v = pos.side_to_move() == WHITE ? v : -v; // Trace scores are from white's point of view

  std::stringstream ss;
  ss << std::showpoint << std::noshowpos << std::fixed << std::setprecision(2)
     << "     Term    |    White    |    Black    |    Total   \n"
     << "             |   MG    EG  |   MG    EG  |   MG    EG \n"
     << " ------------+-------------+-------------+------------\n"
     << "    Material | " << Term(MATERIAL)
     << "   Imbalance | " << Term(IMBALANCE)
     << "  Initiative | " << Term(INITIATIVE)
     << "       Pawns | " << Term(PAWN)
     << "     Knights | " << Term(KNIGHT)
     << "     Bishops | " << Term(BISHOP)
     << "       Rooks | " << Term(ROOK)
     << "      Queens | " << Term(QUEEN)
     << "    Mobility | " << Term(MOBILITY)
     << " King safety | " << Term(KING)
     << "     Threats | " << Term(THREAT)
     << "      Passed | " << Term(PASSED)
     << "       Space | " << Term(SPACE)
     << " ------------+-------------+-------------+------------\n"
     << "       Total | " << Term(TOTAL);

  ss << "\nTotal evaluation: " << to_cp(v) << " (white side)\n";

  return ss.str();
}
