/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2016 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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

namespace {

  namespace Trace {

    enum Term { // The first 8 entries are for PieceType
      MATERIAL = 8, IMBALANCE, MOBILITY, THREAT, PASSED, SPACE, TOTAL, TERM_NB
    };

    double scores[TERM_NB][COLOR_NB][PHASE_NB];

    double to_cp(Value v) { return double(v) / PawnValueEg; }

    void add(int idx, Color c, Score s) {
      scores[idx][c][MG] = to_cp(mg_value(s));
      scores[idx][c][EG] = to_cp(eg_value(s));
    }

    void add(int idx, Score w, Score b = SCORE_ZERO) {
      add(idx, WHITE, w); add(idx, BLACK, b);
    }

    std::ostream& operator<<(std::ostream& os, Term t) {

      if (t == MATERIAL || t == IMBALANCE || t == Term(PAWN) || t == TOTAL)
          os << "  ---   --- |   ---   --- | ";
      else
          os << std::setw(5) << scores[t][WHITE][MG] << " "
             << std::setw(5) << scores[t][WHITE][EG] << " | "
             << std::setw(5) << scores[t][BLACK][MG] << " "
             << std::setw(5) << scores[t][BLACK][EG] << " | ";

      os << std::setw(5) << scores[t][WHITE][MG] - scores[t][BLACK][MG] << " "
         << std::setw(5) << scores[t][WHITE][EG] - scores[t][BLACK][EG] << " \n";

      return os;
    }
  }

  using namespace Trace;

  // Struct EvalInfo contains various information computed and collected
  // by the evaluation functions.
  struct EvalInfo {

    // attackedBy[color][piece type] is a bitboard representing all squares
    // attacked by a given color and piece type (can be also ALL_PIECES).
    Bitboard attackedBy[COLOR_NB][PIECE_TYPE_NB];

    // attackedBy2[color] are the squares attacked by 2 pieces of a given color,
    // possibly via x-ray or by one pawn and one piece. Diagonal x-ray through
    // pawn or squares attacked by 2 pawns are not explicitly added.
    Bitboard attackedBy2[COLOR_NB];

    // kingRing[color] is the zone around the king which is considered
    // by the king safety evaluation. This consists of the squares directly
    // adjacent to the king, and the three (or two, for a king on an edge file)
    // squares two ranks in front of the king. For instance, if black's king
    // is on g8, kingRing[BLACK] is a bitboard containing the squares f8, h8,
    // f7, g7, h7, f6, g6 and h6.
    Bitboard kingRing[COLOR_NB];

    // kingAttackersCount[color] is the number of pieces of the given color
    // which attack a square in the kingRing of the enemy king.
    int kingAttackersCount[COLOR_NB];

    // kingAttackersWeight[color] is the sum of the "weights" of the pieces of the
    // given color which attack a square in the kingRing of the enemy king. The
    // weights of the individual piece types are given by the elements in the
    // KingAttackWeights array.
    int kingAttackersWeight[COLOR_NB];

    // kingAdjacentZoneAttacksCount[color] is the number of attacks by the given
    // color to squares directly adjacent to the enemy king. Pieces which attack
    // more than one square are counted multiple times. For instance, if there is
    // a white knight on g5 and black's king is on g8, this white knight adds 2
    // to kingAdjacentZoneAttacksCount[WHITE].
    int kingAdjacentZoneAttacksCount[COLOR_NB];

    Bitboard pinnedPieces[COLOR_NB];
    Material::Entry* me;
    Pawns::Entry* pi;
  };

  #define V(v) Value(v)
  #define S(mg, eg) make_score(mg, eg)

  // MobilityBonus[PieceType][attacked] contains bonuses for middle and end
  // game, indexed by piece type and number of attacked squares in the MobilityArea.
  const Score MobilityBonus[][32] = {
    {}, {},
    { S(-75,-76), S(-56,-54), S( -9,-26), S( -2,-10), S(  6,  5), S( 15, 11), // Knights
      S( 22, 26), S( 30, 28), S( 36, 29) },
    { S(-48,-58), S(-21,-19), S( 16, -2), S( 26, 12), S( 37, 22), S( 51, 42), // Bishops
      S( 54, 54), S( 63, 58), S( 65, 63), S( 71, 70), S( 79, 74), S( 81, 86),
      S( 92, 90), S( 97, 94) },
    { S(-56,-78), S(-25,-18), S(-11, 26), S( -5, 55), S( -4, 70), S( -1, 81), // Rooks
      S(  8,109), S( 14,120), S( 21,128), S( 23,143), S( 31,154), S( 32,160),
      S( 43,165), S( 49,168), S( 59,169) },
    { S(-40,-35), S(-25,-12), S(  2,  7), S(  4, 19), S( 14, 37), S( 24, 55), // Queens
      S( 25, 62), S( 40, 76), S( 43, 79), S( 47, 87), S( 54, 94), S( 56,102),
      S( 60,111), S( 70,116), S( 72,118), S( 73,122), S( 75,128), S( 77,130),
      S( 85,133), S( 94,136), S( 99,140), S(108,157), S(112,158), S(113,161),
      S(118,174), S(119,177), S(123,191), S(128,199) }
  };

  // Outpost[knight/bishop][supported by pawn] contains bonuses for knights and
  // bishops outposts, bigger if outpost piece is supported by a pawn.
  const Score Outpost[][2] = {
    { S(43,11), S(65,20) }, // Knights
    { S(20, 3), S(29, 8) }  // Bishops
  };

  // ReachableOutpost[knight/bishop][supported by pawn] contains bonuses for
  // knights and bishops which can reach an outpost square in one move, bigger
  // if outpost square is supported by a pawn.
  const Score ReachableOutpost[][2] = {
    { S(21, 5), S(35, 8) }, // Knights
    { S( 8, 0), S(14, 4) }  // Bishops
  };

  // RookOnFile[semiopen/open] contains bonuses for each rook when there is no
  // friendly pawn on the rook file.
  const Score RookOnFile[2] = { S(20, 7), S(45, 20) };

  // ThreatBySafePawn[PieceType] contains bonuses according to which piece
  // type is attacked by a pawn which is protected or is not attacked.
  const Score ThreatBySafePawn[PIECE_TYPE_NB] = {
    S(0, 0), S(0, 0), S(176, 139), S(131, 127), S(217, 218), S(203, 215)
  };

  // Threat[by minor/by rook][attacked PieceType] contains
  // bonuses according to which piece type attacks which one.
  // Attacks on lesser pieces which are pawn-defended are not considered.
  const Score Threat[][PIECE_TYPE_NB] = {
    { S(0, 0), S(0, 33), S(45, 43), S(46, 47), S(72,107), S(48,118) }, // by Minor
    { S(0, 0), S(0, 25), S(40, 62), S(40, 59), S( 0, 34), S(35, 48) }  // by Rook
  };

  // ThreatByKing[on one/on many] contains bonuses for King attacks on
  // pawns or pieces which are not pawn-defended.
  const Score ThreatByKing[2] = { S(3, 62), S(9, 138) };

  // Passed[mg/eg][Rank] contains midgame and endgame bonuses for passed pawns.
  // We don't use a Score because we process the two components independently.
  const Value Passed[][RANK_NB] = {
    { V(5), V( 5), V(31), V(73), V(166), V(252) },
    { V(7), V(14), V(38), V(73), V(166), V(252) }
  };

  // PassedFile[File] contains a bonus according to the file of a passed pawn
  const Score PassedFile[FILE_NB] = {
    S(  9, 10), S( 2, 10), S( 1, -8), S(-20,-12),
    S(-20,-12), S( 1, -8), S( 2, 10), S( 9, 10)
  };

  // Assorted bonuses and penalties used by evaluation
  const Score MinorBehindPawn     = S(16,  0);
  const Score BishopPawns         = S( 8, 12);
  const Score RookOnPawn          = S( 8, 24);
  const Score TrappedRook         = S(92,  0);
  const Score CloseEnemies        = S( 7,  0);
  const Score SafeCheck           = S(20, 20);
  const Score OtherCheck          = S(10, 10);
  const Score ThreatByHangingPawn = S(71, 61);
  const Score LooseEnemies        = S( 0, 25);
  const Score WeakQueen           = S(35,  0);
  const Score Hanging             = S(48, 27);
  const Score ThreatByPawnPush    = S(38, 22);
  const Score Unstoppable         = S( 0, 20);
  const Score PawnlessFlank       = S(20, 80);
  const Score HinderPassedPawn    = S( 7,  0);

  // Penalty for a bishop on a1/h1 (a8/h8 for black) which is trapped by
  // a friendly pawn on b2/g2 (b7/g7 for black). This can obviously only
  // happen in Chess960 games.
  const Score TrappedBishopA1H1 = S(50, 50);

  #undef S
  #undef V

  // KingAttackWeights[PieceType] contains king attack weights by piece type
  const int KingAttackWeights[PIECE_TYPE_NB] = { 0, 0, 78, 56, 45, 11 };

  // Penalties for enemy's safe checks
  const int QueenContactCheck = 997;
  const int QueenCheck        = 695;
  const int RookCheck         = 638;
  const int BishopCheck       = 538;
  const int KnightCheck       = 874;


  // eval_init() initializes king and attack bitboards for a given color
  // adding pawn attacks. To be done at the beginning of the evaluation.

  template<Color Us>
  void eval_init(const Position& pos, EvalInfo& ei) {

    const Color  Them = (Us == WHITE ? BLACK : WHITE);
    const Square Down = (Us == WHITE ? SOUTH : NORTH);

    ei.pinnedPieces[Us] = pos.pinned_pieces(Us);
    Bitboard b = ei.attackedBy[Them][KING];
    ei.attackedBy[Them][ALL_PIECES] |= b;
    ei.attackedBy[Us][ALL_PIECES] |= ei.attackedBy[Us][PAWN] = ei.pi->pawn_attacks(Us);
    ei.attackedBy2[Us] = ei.attackedBy[Us][PAWN] & ei.attackedBy[Us][KING];

    // Init king safety tables only if we are going to use them
    if (pos.non_pawn_material(Us) >= QueenValueMg)
    {
        ei.kingRing[Them] = b | shift<Down>(b);
        b &= ei.attackedBy[Us][PAWN];
        ei.kingAttackersCount[Us] = popcount(b);
        ei.kingAdjacentZoneAttacksCount[Us] = ei.kingAttackersWeight[Us] = 0;
    }
    else
        ei.kingRing[Them] = ei.kingAttackersCount[Us] = 0;
  }


  // evaluate_pieces() assigns bonuses and penalties to the pieces of a given
  // color and type.

  template<bool DoTrace, Color Us = WHITE, PieceType Pt = KNIGHT>
  Score evaluate_pieces(const Position& pos, EvalInfo& ei, Score* mobility,
                        const Bitboard* mobilityArea) {
    Bitboard b, bb;
    Square s;
    Score score = SCORE_ZERO;

    const PieceType NextPt = (Us == WHITE ? Pt : PieceType(Pt + 1));
    const Color Them = (Us == WHITE ? BLACK : WHITE);
    const Bitboard OutpostRanks = (Us == WHITE ? Rank4BB | Rank5BB | Rank6BB
                                               : Rank5BB | Rank4BB | Rank3BB);
    const Square* pl = pos.squares<Pt>(Us);

    ei.attackedBy[Us][Pt] = 0;

    while ((s = *pl++) != SQ_NONE)
    {
        // Find attacked squares, including x-ray attacks for bishops and rooks
        b = Pt == BISHOP ? attacks_bb<BISHOP>(s, pos.pieces() ^ pos.pieces(Us, QUEEN))
          : Pt ==   ROOK ? attacks_bb<  ROOK>(s, pos.pieces() ^ pos.pieces(Us, ROOK, QUEEN))
                         : pos.attacks_from<Pt>(s);

        if (ei.pinnedPieces[Us] & s)
            b &= LineBB[pos.square<KING>(Us)][s];

        ei.attackedBy2[Us] |= ei.attackedBy[Us][ALL_PIECES] & b;
        ei.attackedBy[Us][ALL_PIECES] |= ei.attackedBy[Us][Pt] |= b;

        if (b & ei.kingRing[Them])
        {
            ei.kingAttackersCount[Us]++;
            ei.kingAttackersWeight[Us] += KingAttackWeights[Pt];
            ei.kingAdjacentZoneAttacksCount[Us] += popcount(b & ei.attackedBy[Them][KING]);
        }

        if (Pt == QUEEN)
            b &= ~(  ei.attackedBy[Them][KNIGHT]
                   | ei.attackedBy[Them][BISHOP]
                   | ei.attackedBy[Them][ROOK]);

        int mob = popcount(b & mobilityArea[Us]);

        mobility[Us] += MobilityBonus[Pt][mob];

        if (Pt == BISHOP || Pt == KNIGHT)
        {
            // Bonus for outpost squares
            bb = OutpostRanks & ~ei.pi->pawn_attacks_span(Them);
            if (bb & s)
                score += Outpost[Pt == BISHOP][!!(ei.attackedBy[Us][PAWN] & s)];
            else
            {
                bb &= b & ~pos.pieces(Us);
                if (bb)
                   score += ReachableOutpost[Pt == BISHOP][!!(ei.attackedBy[Us][PAWN] & bb)];
            }

            // Bonus when behind a pawn
            if (    relative_rank(Us, s) < RANK_5
                && (pos.pieces(PAWN) & (s + pawn_push(Us))))
                score += MinorBehindPawn;

            // Penalty for pawns on the same color square as the bishop
            if (Pt == BISHOP)
                score -= BishopPawns * ei.pi->pawns_on_same_color_squares(Us, s);

            // An important Chess960 pattern: A cornered bishop blocked by a friendly
            // pawn diagonally in front of it is a very serious problem, especially
            // when that pawn is also blocked.
            if (   Pt == BISHOP
                && pos.is_chess960()
                && (s == relative_square(Us, SQ_A1) || s == relative_square(Us, SQ_H1)))
            {
                Square d = pawn_push(Us) + (file_of(s) == FILE_A ? EAST : WEST);
                if (pos.piece_on(s + d) == make_piece(Us, PAWN))
                    score -= !pos.empty(s + d + pawn_push(Us))                ? TrappedBishopA1H1 * 4
                            : pos.piece_on(s + d + d) == make_piece(Us, PAWN) ? TrappedBishopA1H1 * 2
                                                                              : TrappedBishopA1H1;
            }
        }

        if (Pt == ROOK)
        {
            // Bonus for aligning with enemy pawns on the same rank/file
            if (relative_rank(Us, s) >= RANK_5)
                score += RookOnPawn * popcount(pos.pieces(Them, PAWN) & PseudoAttacks[ROOK][s]);

            // Bonus when on an open or semi-open file
            if (ei.pi->semiopen_file(Us, file_of(s)))
                score += RookOnFile[!!ei.pi->semiopen_file(Them, file_of(s))];

            // Penalize when trapped by the king, even more if the king cannot castle
            else if (mob <= 3)
            {
                Square ksq = pos.square<KING>(Us);

                if (   ((file_of(ksq) < FILE_E) == (file_of(s) < file_of(ksq)))
                    && (rank_of(ksq) == rank_of(s) || relative_rank(Us, ksq) == RANK_1)
                    && !ei.pi->semiopen_side(Us, file_of(ksq), file_of(s) < file_of(ksq)))
                    score -= (TrappedRook - make_score(mob * 22, 0)) * (1 + !pos.can_castle(Us));
            }
        }

        if (Pt == QUEEN)
        {
            // Penalty if any relative pin or discovered attack against the queen
            Bitboard pinners;
            if (pos.slider_blockers(pos.pieces(Them, ROOK, BISHOP), s, pinners))
                score -= WeakQueen;
        }
    }

    if (DoTrace)
        Trace::add(Pt, Us, score);

    // Recursively call evaluate_pieces() of next piece type until KING is excluded
    return score - evaluate_pieces<DoTrace, Them, NextPt>(pos, ei, mobility, mobilityArea);
  }

  template<>
  Score evaluate_pieces<false, WHITE, KING>(const Position&, EvalInfo&, Score*, const Bitboard*) { return SCORE_ZERO; }
  template<>
  Score evaluate_pieces< true, WHITE, KING>(const Position&, EvalInfo&, Score*, const Bitboard*) { return SCORE_ZERO; }


  // evaluate_king() assigns bonuses and penalties to a king of a given color

  const Bitboard WhiteCamp = Rank1BB | Rank2BB | Rank3BB | Rank4BB | Rank5BB;
  const Bitboard BlackCamp = Rank8BB | Rank7BB | Rank6BB | Rank5BB | Rank4BB;
  const Bitboard QueenSide   = FileABB | FileBBB | FileCBB | FileDBB;
  const Bitboard CenterFiles = FileCBB | FileDBB | FileEBB | FileFBB;
  const Bitboard KingSide    = FileEBB | FileFBB | FileGBB | FileHBB;

  const Bitboard KingFlank[COLOR_NB][FILE_NB] = {
    { QueenSide   & WhiteCamp, QueenSide & WhiteCamp, QueenSide & WhiteCamp, CenterFiles & WhiteCamp,
      CenterFiles & WhiteCamp, KingSide  & WhiteCamp, KingSide  & WhiteCamp, KingSide    & WhiteCamp },
    { QueenSide   & BlackCamp, QueenSide & BlackCamp, QueenSide & BlackCamp, CenterFiles & BlackCamp,
      CenterFiles & BlackCamp, KingSide  & BlackCamp, KingSide  & BlackCamp, KingSide    & BlackCamp },
  };

  template<Color Us, bool DoTrace>
  Score evaluate_king(const Position& pos, const EvalInfo& ei) {

    const Color Them = (Us == WHITE ? BLACK : WHITE);
    const Square  Up = (Us == WHITE ? NORTH : SOUTH);

    Bitboard undefended, b, b1, b2, safe, other;
    int kingDanger;
    const Square ksq = pos.square<KING>(Us);

    // King shelter and enemy pawns storm
    Score score = ei.pi->king_safety<Us>(pos, ksq);

    // Main king safety evaluation
    if (ei.kingAttackersCount[Them])
    {
        // Find the attacked squares which are defended only by the king...
        undefended =   ei.attackedBy[Them][ALL_PIECES]
                    &  ei.attackedBy[Us][KING]
                    & ~ei.attackedBy2[Us];

        // ... and those which are not defended at all in the larger king ring
        b =  ei.attackedBy[Them][ALL_PIECES] & ~ei.attackedBy[Us][ALL_PIECES]
           & ei.kingRing[Us] & ~pos.pieces(Them);

        // Initialize the 'kingDanger' variable, which will be transformed
        // later into a king danger score. The initial value is based on the
        // number and types of the enemy's attacking pieces, the number of
        // attacked and undefended squares around our king and the quality of
        // the pawn shelter (current 'score' value).
        kingDanger =  std::min(807, ei.kingAttackersCount[Them] * ei.kingAttackersWeight[Them])
                    + 101 * ei.kingAdjacentZoneAttacksCount[Them]
                    + 235 * popcount(undefended)
                    + 134 * (popcount(b) + !!ei.pinnedPieces[Us])
                    - 717 * !pos.count<QUEEN>(Them)
                    -   7 * mg_value(score) / 5 - 5;

        // Analyse the enemy's safe queen contact checks. Firstly, find the
        // undefended squares around the king reachable by the enemy queen...
        b = undefended & ei.attackedBy[Them][QUEEN] & ~pos.pieces(Them);

        // ...and keep squares supported by another enemy piece
        kingDanger += QueenContactCheck * popcount(b & ei.attackedBy2[Them]);

        // Analyse the safe enemy's checks which are possible on next move...
        safe  = ~(ei.attackedBy[Us][ALL_PIECES] | pos.pieces(Them));

        // ... and some other potential checks, only requiring the square to be
        // safe from pawn-attacks, and not being occupied by a blocked pawn.
        other = ~(   ei.attackedBy[Us][PAWN]
                  | (pos.pieces(Them, PAWN) & shift<Up>(pos.pieces(PAWN))));

        b1 = pos.attacks_from<ROOK  >(ksq);
        b2 = pos.attacks_from<BISHOP>(ksq);

        // Enemy queen safe checks
        if ((b1 | b2) & ei.attackedBy[Them][QUEEN] & safe)
            kingDanger += QueenCheck, score -= SafeCheck;

        // For other pieces, also consider the square safe if attacked twice,
        // and only defended by a queen.
        safe |=  ei.attackedBy2[Them]
               & ~(ei.attackedBy2[Us] | pos.pieces(Them))
               & ei.attackedBy[Us][QUEEN];

        // Enemy rooks safe and other checks
        if (b1 & ei.attackedBy[Them][ROOK] & safe)
            kingDanger += RookCheck, score -= SafeCheck;

        else if (b1 & ei.attackedBy[Them][ROOK] & other)
            score -= OtherCheck;

        // Enemy bishops safe and other checks
        if (b2 & ei.attackedBy[Them][BISHOP] & safe)
            kingDanger += BishopCheck, score -= SafeCheck;

        else if (b2 & ei.attackedBy[Them][BISHOP] & other)
            score -= OtherCheck;

        // Enemy knights safe and other checks
        b = pos.attacks_from<KNIGHT>(ksq) & ei.attackedBy[Them][KNIGHT];
        if (b & safe)
            kingDanger += KnightCheck, score -= SafeCheck;

        else if (b & other)
            score -= OtherCheck;

        // Compute the king danger score and subtract it from the evaluation
        if (kingDanger > 0)
            score -= make_score(std::min(kingDanger * kingDanger / 4096,  2 * int(BishopValueMg)), 0);
    }

    // King tropism: firstly, find squares that opponent attacks in our king flank
    File kf = file_of(ksq);
    b = ei.attackedBy[Them][ALL_PIECES] & KingFlank[Us][kf];

    assert(((Us == WHITE ? b << 4 : b >> 4) & b) == 0);
    assert(popcount(Us == WHITE ? b << 4 : b >> 4) == popcount(b));

    // Secondly, add the squares which are attacked twice in that flank and
    // which are not defended by our pawns.
    b =  (Us == WHITE ? b << 4 : b >> 4)
       | (b & ei.attackedBy2[Them] & ~ei.attackedBy[Us][PAWN]);

    score -= CloseEnemies * popcount(b);

    // Penalty when our king is on a pawnless flank
    if (!(pos.pieces(PAWN) & (KingFlank[WHITE][kf] | KingFlank[BLACK][kf])))
        score -= PawnlessFlank;

    if (DoTrace)
        Trace::add(KING, Us, score);

    return score;
  }


  // evaluate_threats() assigns bonuses according to the types of the attacking
  // and the attacked pieces.

  template<Color Us, bool DoTrace>
  Score evaluate_threats(const Position& pos, const EvalInfo& ei) {

    const Color Them        = (Us == WHITE ? BLACK      : WHITE);
    const Square Up         = (Us == WHITE ? NORTH      : SOUTH);
    const Square Left       = (Us == WHITE ? NORTH_WEST : SOUTH_EAST);
    const Square Right      = (Us == WHITE ? NORTH_EAST : SOUTH_WEST);
    const Bitboard TRank2BB = (Us == WHITE ? Rank2BB    : Rank7BB);
    const Bitboard TRank7BB = (Us == WHITE ? Rank7BB    : Rank2BB);

    enum { Minor, Rook };

    Bitboard b, weak, defended, safeThreats;
    Score score = SCORE_ZERO;

    // Small bonus if the opponent has loose pawns or pieces
    if (   (pos.pieces(Them) ^ pos.pieces(Them, QUEEN, KING))
        & ~(ei.attackedBy[Us][ALL_PIECES] | ei.attackedBy[Them][ALL_PIECES]))
        score += LooseEnemies;

    // Non-pawn enemies attacked by a pawn
    weak = (pos.pieces(Them) ^ pos.pieces(Them, PAWN)) & ei.attackedBy[Us][PAWN];

    if (weak)
    {
        b = pos.pieces(Us, PAWN) & ( ~ei.attackedBy[Them][ALL_PIECES]
                                    | ei.attackedBy[Us][ALL_PIECES]);

        safeThreats = (shift<Right>(b) | shift<Left>(b)) & weak;

        if (weak ^ safeThreats)
            score += ThreatByHangingPawn;

        while (safeThreats)
            score += ThreatBySafePawn[type_of(pos.piece_on(pop_lsb(&safeThreats)))];
    }

    // Non-pawn enemies defended by a pawn
    defended = (pos.pieces(Them) ^ pos.pieces(Them, PAWN)) & ei.attackedBy[Them][PAWN];

    // Enemies not defended by a pawn and under our attack
    weak =   pos.pieces(Them)
          & ~ei.attackedBy[Them][PAWN]
          &  ei.attackedBy[Us][ALL_PIECES];

    // Add a bonus according to the kind of attacking pieces
    if (defended | weak)
    {
        b = (defended | weak) & (ei.attackedBy[Us][KNIGHT] | ei.attackedBy[Us][BISHOP]);
        while (b)
            score += Threat[Minor][type_of(pos.piece_on(pop_lsb(&b)))];

        b = (pos.pieces(Them, QUEEN) | weak) & ei.attackedBy[Us][ROOK];
        while (b)
            score += Threat[Rook ][type_of(pos.piece_on(pop_lsb(&b)))];

        score += Hanging * popcount(weak & ~ei.attackedBy[Them][ALL_PIECES]);

        b = weak & ei.attackedBy[Us][KING];
        if (b)
            score += ThreatByKing[more_than_one(b)];
    }

    // Bonus if some pawns can safely push and attack an enemy piece
    b = pos.pieces(Us, PAWN) & ~TRank7BB;
    b = shift<Up>(b | (shift<Up>(b & TRank2BB) & ~pos.pieces()));

    b &=  ~pos.pieces()
        & ~ei.attackedBy[Them][PAWN]
        & (ei.attackedBy[Us][ALL_PIECES] | ~ei.attackedBy[Them][ALL_PIECES]);

    b =  (shift<Left>(b) | shift<Right>(b))
       &  pos.pieces(Them)
       & ~ei.attackedBy[Us][PAWN];

    score += ThreatByPawnPush * popcount(b);

    if (DoTrace)
        Trace::add(THREAT, Us, score);

    return score;
  }


  // evaluate_passed_pawns() evaluates the passed pawns of the given color

  template<Color Us, bool DoTrace>
  Score evaluate_passed_pawns(const Position& pos, const EvalInfo& ei) {

    const Color Them = (Us == WHITE ? BLACK : WHITE);

    Bitboard b, bb, squaresToQueen, defendedSquares, unsafeSquares;
    Score score = SCORE_ZERO;

    b = ei.pi->passed_pawns(Us);

    while (b)
    {
        Square s = pop_lsb(&b);

        assert(pos.pawn_passed(Us, s));
        assert(!(pos.pieces(PAWN) & forward_bb(Us, s)));

        bb = forward_bb(Us, s) & (ei.attackedBy[Them][ALL_PIECES] | pos.pieces(Them));
        score -= HinderPassedPawn * popcount(bb);

        int r = relative_rank(Us, s) - RANK_2;
        int rr = r * (r - 1);

        Value mbonus = Passed[MG][r], ebonus = Passed[EG][r];

        if (rr)
        {
            Square blockSq = s + pawn_push(Us);

            // Adjust bonus based on the king's proximity
            ebonus +=  distance(pos.square<KING>(Them), blockSq) * 5 * rr
                     - distance(pos.square<KING>(Us  ), blockSq) * 2 * rr;

            // If blockSq is not the queening square then consider also a second push
            if (relative_rank(Us, blockSq) != RANK_8)
                ebonus -= distance(pos.square<KING>(Us), blockSq + pawn_push(Us)) * rr;

            // If the pawn is free to advance, then increase the bonus
            if (pos.empty(blockSq))
            {
                // If there is a rook or queen attacking/defending the pawn from behind,
                // consider all the squaresToQueen. Otherwise consider only the squares
                // in the pawn's path attacked or occupied by the enemy.
                defendedSquares = unsafeSquares = squaresToQueen = forward_bb(Us, s);

                bb = forward_bb(Them, s) & pos.pieces(ROOK, QUEEN) & pos.attacks_from<ROOK>(s);

                if (!(pos.pieces(Us) & bb))
                    defendedSquares &= ei.attackedBy[Us][ALL_PIECES];

                if (!(pos.pieces(Them) & bb))
                    unsafeSquares &= ei.attackedBy[Them][ALL_PIECES] | pos.pieces(Them);

                // If there aren't any enemy attacks, assign a big bonus. Otherwise
                // assign a smaller bonus if the block square isn't attacked.
                int k = !unsafeSquares ? 18 : !(unsafeSquares & blockSq) ? 8 : 0;

                // If the path to the queen is fully defended, assign a big bonus.
                // Otherwise assign a smaller bonus if the block square is defended.
                if (defendedSquares == squaresToQueen)
                    k += 6;

                else if (defendedSquares & blockSq)
                    k += 4;

                mbonus += k * rr, ebonus += k * rr;
            }
            else if (pos.pieces(Us) & blockSq)
                mbonus += rr + r * 2, ebonus += rr + r * 2;
        } // rr != 0

        score += make_score(mbonus, ebonus) + PassedFile[file_of(s)];
    }

    if (DoTrace)
        Trace::add(PASSED, Us, score);

    // Add the scores to the middlegame and endgame eval
    return score;
  }


  // evaluate_space() computes the space evaluation for a given side. The
  // space evaluation is a simple bonus based on the number of safe squares
  // available for minor pieces on the central four files on ranks 2--4. Safe
  // squares one, two or three squares behind a friendly pawn are counted
  // twice. Finally, the space bonus is multiplied by a weight. The aim is to
  // improve play on game opening.
  template<Color Us>
  Score evaluate_space(const Position& pos, const EvalInfo& ei) {

    const Color Them = (Us == WHITE ? BLACK : WHITE);
    const Bitboard SpaceMask =
      Us == WHITE ? (FileCBB | FileDBB | FileEBB | FileFBB) & (Rank2BB | Rank3BB | Rank4BB)
                  : (FileCBB | FileDBB | FileEBB | FileFBB) & (Rank7BB | Rank6BB | Rank5BB);

    // Find the safe squares for our pieces inside the area defined by
    // SpaceMask. A square is unsafe if it is attacked by an enemy
    // pawn, or if it is undefended and attacked by an enemy piece.
    Bitboard safe =   SpaceMask
                   & ~pos.pieces(Us, PAWN)
                   & ~ei.attackedBy[Them][PAWN]
                   & (ei.attackedBy[Us][ALL_PIECES] | ~ei.attackedBy[Them][ALL_PIECES]);

    // Find all squares which are at most three squares behind some friendly pawn
    Bitboard behind = pos.pieces(Us, PAWN);
    behind |= (Us == WHITE ? behind >>  8 : behind <<  8);
    behind |= (Us == WHITE ? behind >> 16 : behind << 16);

    // Since SpaceMask[Us] is fully on our half of the board...
    assert(unsigned(safe >> (Us == WHITE ? 32 : 0)) == 0);

    // ...count safe + (behind & safe) with a single popcount
    int bonus = popcount((Us == WHITE ? safe << 32 : safe >> 32) | (behind & safe));
    bonus = std::min(16, bonus);
    int weight = pos.count<ALL_PIECES>(Us) - 2 * ei.pi->open_files();

    return make_score(bonus * weight * weight / 18, 0);
  }


  // evaluate_initiative() computes the initiative correction value for the
  // position, i.e., second order bonus/malus based on the known attacking/defending
  // status of the players.
  Score evaluate_initiative(const Position& pos, int asymmetry, Value eg) {

    int kingDistance =  distance<File>(pos.square<KING>(WHITE), pos.square<KING>(BLACK))
                      - distance<Rank>(pos.square<KING>(WHITE), pos.square<KING>(BLACK));
    int pawns = pos.count<PAWN>(WHITE) + pos.count<PAWN>(BLACK);

    // Compute the initiative bonus for the attacking side
    int initiative = 8 * (asymmetry + kingDistance - 15) + 12 * pawns;

    // Now apply the bonus: note that we find the attacking side by extracting
    // the sign of the endgame value, and that we carefully cap the bonus so
    // that the endgame score will never be divided by more than two.
    int value = ((eg > 0) - (eg < 0)) * std::max(initiative, -abs(eg / 2));

    return make_score(0, value);
  }


  // evaluate_scale_factor() computes the scale factor for the winning side
  ScaleFactor evaluate_scale_factor(const Position& pos, const EvalInfo& ei, Value eg) {

    Color strongSide = eg > VALUE_DRAW ? WHITE : BLACK;
    ScaleFactor sf = ei.me->scale_factor(pos, strongSide);

    // If we don't already have an unusual scale factor, check for certain
    // types of endgames, and use a lower scale for those.
    if (    ei.me->game_phase() < PHASE_MIDGAME
        && (sf == SCALE_FACTOR_NORMAL || sf == SCALE_FACTOR_ONEPAWN))
    {
        if (pos.opposite_bishops())
        {
            // Endgame with opposite-colored bishops and no other pieces (ignoring pawns)
            // is almost a draw, in case of KBP vs KB, it is even more a draw.
            if (   pos.non_pawn_material(WHITE) == BishopValueMg
                && pos.non_pawn_material(BLACK) == BishopValueMg)
                sf = more_than_one(pos.pieces(PAWN)) ? ScaleFactor(31) : ScaleFactor(9);

            // Endgame with opposite-colored bishops, but also other pieces. Still
            // a bit drawish, but not as drawish as with only the two bishops.
            else
                sf = ScaleFactor(46);
        }
        // Endings where weaker side can place his king in front of the opponent's
        // pawns are drawish.
        else if (    abs(eg) <= BishopValueEg
                 &&  pos.count<PAWN>(strongSide) <= 2
                 && !pos.pawn_passed(~strongSide, pos.square<KING>(~strongSide)))
            sf = ScaleFactor(37 + 7 * pos.count<PAWN>(strongSide));
    }

    return sf;
  }

} // namespace


/// evaluate() is the main evaluation function. It returns a static evaluation
/// of the position from the point of view of the side to move.

template<bool DoTrace>
Value Eval::evaluate(const Position& pos) {

  assert(!pos.checkers());

  Score mobility[COLOR_NB] = { SCORE_ZERO, SCORE_ZERO };
  EvalInfo ei;

  // Probe the material hash table
  ei.me = Material::probe(pos);

  // If we have a specialized evaluation function for the current material
  // configuration, call it and return.
  if (ei.me->specialized_eval_exists())
      return ei.me->evaluate(pos);

  // Initialize score by reading the incrementally updated scores included in
  // the position object (material + piece square tables) and the material
  // imbalance. Score is computed internally from the white point of view.
  Score score = pos.psq_score() + ei.me->imbalance();

  // Probe the pawn hash table
  ei.pi = Pawns::probe(pos);
  score += ei.pi->pawns_score();

  // Initialize attack and king safety bitboards
  ei.attackedBy[WHITE][ALL_PIECES] = ei.attackedBy[BLACK][ALL_PIECES] = 0;
  ei.attackedBy[WHITE][KING] = pos.attacks_from<KING>(pos.square<KING>(WHITE));
  ei.attackedBy[BLACK][KING] = pos.attacks_from<KING>(pos.square<KING>(BLACK));
  eval_init<WHITE>(pos, ei);
  eval_init<BLACK>(pos, ei);

  // Pawns blocked or on ranks 2 and 3 will be excluded from the mobility area
  Bitboard blockedPawns[] = {
    pos.pieces(WHITE, PAWN) & (shift<SOUTH>(pos.pieces()) | Rank2BB | Rank3BB),
    pos.pieces(BLACK, PAWN) & (shift<NORTH>(pos.pieces()) | Rank7BB | Rank6BB)
  };

  // Do not include in mobility area squares protected by enemy pawns, or occupied
  // by our blocked pawns or king.
  Bitboard mobilityArea[] = {
    ~(ei.attackedBy[BLACK][PAWN] | blockedPawns[WHITE] | pos.square<KING>(WHITE)),
    ~(ei.attackedBy[WHITE][PAWN] | blockedPawns[BLACK] | pos.square<KING>(BLACK))
  };

  // Evaluate all pieces but king and pawns
  score += evaluate_pieces<DoTrace>(pos, ei, mobility, mobilityArea);
  score += mobility[WHITE] - mobility[BLACK];

  // Evaluate kings after all other pieces because we need full attack
  // information when computing the king safety evaluation.
  score +=  evaluate_king<WHITE, DoTrace>(pos, ei)
          - evaluate_king<BLACK, DoTrace>(pos, ei);

  // Evaluate tactical threats, we need full attack information including king
  score +=  evaluate_threats<WHITE, DoTrace>(pos, ei)
          - evaluate_threats<BLACK, DoTrace>(pos, ei);

  // Evaluate passed pawns, we need full attack information including king
  score +=  evaluate_passed_pawns<WHITE, DoTrace>(pos, ei)
          - evaluate_passed_pawns<BLACK, DoTrace>(pos, ei);

  // If both sides have only pawns, score for potential unstoppable pawns
  if (!pos.non_pawn_material(WHITE) && !pos.non_pawn_material(BLACK))
  {
      Bitboard b;
      if ((b = ei.pi->passed_pawns(WHITE)) != 0)
          score += Unstoppable * int(relative_rank(WHITE, frontmost_sq(WHITE, b)));

      if ((b = ei.pi->passed_pawns(BLACK)) != 0)
          score -= Unstoppable * int(relative_rank(BLACK, frontmost_sq(BLACK, b)));
  }

  // Evaluate space for both sides, only during opening
  if (pos.non_pawn_material(WHITE) + pos.non_pawn_material(BLACK) >= 12222)
      score +=  evaluate_space<WHITE>(pos, ei)
              - evaluate_space<BLACK>(pos, ei);

  // Evaluate position potential for the winning side
  score += evaluate_initiative(pos, ei.pi->pawn_asymmetry(), eg_value(score));

  // Evaluate scale factor for the winning side
  ScaleFactor sf = evaluate_scale_factor(pos, ei, eg_value(score));

  // Interpolate between a middlegame and a (scaled by 'sf') endgame score
  Value v =  mg_value(score) * int(ei.me->game_phase())
           + eg_value(score) * int(PHASE_MIDGAME - ei.me->game_phase()) * sf / SCALE_FACTOR_NORMAL;

  v /= int(PHASE_MIDGAME);

  // In case of tracing add all remaining individual evaluation terms
  if (DoTrace)
  {
      Trace::add(MATERIAL, pos.psq_score());
      Trace::add(IMBALANCE, ei.me->imbalance());
      Trace::add(PAWN, ei.pi->pawns_score());
      Trace::add(MOBILITY, mobility[WHITE], mobility[BLACK]);
      Trace::add(SPACE, evaluate_space<WHITE>(pos, ei)
                      , evaluate_space<BLACK>(pos, ei));
      Trace::add(TOTAL, score);
  }

  return (pos.side_to_move() == WHITE ? v : -v) + Eval::Tempo; // Side to move point of view
}

// Explicit template instantiations
template Value Eval::evaluate<true >(const Position&);
template Value Eval::evaluate<false>(const Position&);


/// trace() is like evaluate(), but instead of returning a value, it returns
/// a string (suitable for outputting to stdout) that contains the detailed
/// descriptions and values of each evaluation term. Useful for debugging.

std::string Eval::trace(const Position& pos) {

  std::memset(scores, 0, sizeof(scores));

  Value v = evaluate<true>(pos);
  v = pos.side_to_move() == WHITE ? v : -v; // White's point of view

  std::stringstream ss;
  ss << std::showpoint << std::noshowpos << std::fixed << std::setprecision(2)
     << "      Eval term |    White    |    Black    |    Total    \n"
     << "                |   MG    EG  |   MG    EG  |   MG    EG  \n"
     << "----------------+-------------+-------------+-------------\n"
     << "       Material | " << Term(MATERIAL)
     << "      Imbalance | " << Term(IMBALANCE)
     << "          Pawns | " << Term(PAWN)
     << "        Knights | " << Term(KNIGHT)
     << "         Bishop | " << Term(BISHOP)
     << "          Rooks | " << Term(ROOK)
     << "         Queens | " << Term(QUEEN)
     << "       Mobility | " << Term(MOBILITY)
     << "    King safety | " << Term(KING)
     << "        Threats | " << Term(THREAT)
     << "   Passed pawns | " << Term(PASSED)
     << "          Space | " << Term(SPACE)
     << "----------------+-------------+-------------+-------------\n"
     << "          Total | " << Term(TOTAL);

  ss << "\nTotal Evaluation: " << to_cp(v) << " (white side)\n";

  return ss.str();
}
