/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2020 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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
// #include <iostream>
#include <algorithm>
#include <bitset>

#include "bitboard.h"
#include "misc.h"

uint8_t PopCnt16[1 << 16];
uint8_t SquareDistance[SQUARE_NB][SQUARE_NB];

Bitboard SquareBB[SQUARE_NB];
Bitboard LineBB[SQUARE_NB][SQUARE_NB];
Bitboard PseudoAttacks[PIECE_TYPE_NB][SQUARE_NB];
Bitboard PawnAttacks[COLOR_NB][SQUARE_NB];

Magic RookMagics[SQUARE_NB];
Magic BishopMagics[SQUARE_NB];

namespace {

  Bitboard RookTable[0x19000];  // To store rook attacks
  Bitboard BishopTable[0x1480]; // To store bishop attacks

// Correct precomputed magics from Stockfish (64-bit)
const uint64_t RookMagicNumbers[64] = {
  0x80004000802010ULL,
  0x240005000412001ULL,
  0x82000a0080104020ULL,
  0x500100009000520ULL,
  0x880040080080002ULL,
  0x300080204000100ULL,
  0x820008189a004104ULL,
  0xc180008004ca2d00ULL,
  0x800080400024ULL,
  0x280804000802000ULL,
  0x800802000801006ULL,
  0x8002800800821002ULL,
  0x2000808004000800ULL,
  0xa204808004002200ULL,
  0x2103000a00010004ULL,
  0x800a0000904c2102ULL,
  0x52020021008040ULL,
  0xc0002010080021ULL,
  0x2810002008040020ULL,
  0x52808008061000ULL,
  0x1040050008005100ULL,
  0xa00808004000200ULL,
  0x8013540098101201ULL,
  0x4480220000408421ULL,
  0x481008200220040ULL,
  0x40008280200040ULL,
  0x8080200880100080ULL,
  0x10008080080010ULL,
  0x400040080800800ULL,
  0x1000040080800200ULL,
  0x2400420400080110ULL,
  0x800001020024408cULL,
  0x4080002004400042ULL,
  0x40004881002101ULL,
  0x90220082004010ULL,
  0x200801000800801ULL,
  0x508080a801800400ULL,
  0x6024000802020010ULL,
  0x49080104003062ULL,
  0x5082000104ULL,
  0x80800040008028ULL,
  0x10250002002400aULL,
  0x240801042020020ULL,
  0x308100100090023ULL,
  0x40040008008080ULL,
  0x15000204010008ULL,
  0x80018802040010ULL,
  0xc082010060820004ULL,
  0x10608600c0230600ULL,
  0x4024200840048180ULL,
  0x40100020008080ULL,
  0xa040100020090100ULL,
  0xb0080080040080ULL,
  0x2002009008840200ULL,
  0x9000820850010400ULL,
  0x140004100840200ULL,
  0x81004160800293ULL,
  0x8120080402102ULL,
  0x10200040081101ULL,
  0x2042100021000509ULL,
  0x89000402100801ULL,
  0x101000802040001ULL,
  0x4000180a10048504ULL,
  0x10050821840052ULL,
};

const uint64_t BishopMagicNumbers[64] = {
  0x4b00088019180ULL,
  0x419004088c005820ULL,
  0xc0848420860000a0ULL,
  0x24240090400801ULL,
  0xa040421000008a0ULL,
  0x2011088000592ULL,
  0x701010121200000ULL,
  0x9400220050084854ULL,
  0x4000092008220040ULL,
  0x120080888840c44ULL,
  0x108100400882000ULL,
  0x101204041a904220ULL,
  0x840420202500ULL,
  0x1000049010880005ULL,
  0x202821804040481ULL,
  0xc922262611042000ULL,
  0x411002060010104ULL,
  0x220014481140100ULL,
  0x2900168408a0210ULL,
  0x6880a80200c000ULL,
  0x81000820080140ULL,
  0x101014200808400ULL,
  0x1006000115092048ULL,
  0x2002020820840ULL,
  0x40208a810101002ULL,
  0x815002a814084a10ULL,
  0x9004020010008418ULL,
  0x10104024040002ULL,
  0x841010044104001ULL,
  0x1080644002011000ULL,
  0x8208088412020100ULL,
  0x810402044244ULL,
  0x2001200800200880ULL,
  0x8124100200042444ULL,
  0x1000109000080445ULL,
  0x40020080080082ULL,
  0x208060400081010ULL,
  0x2404600010080ULL,
  0x50c048881a21800ULL,
  0x828304110804100ULL,
  0x204100446831000ULL,
  0x4400a410020454ULL,
  0x10840041010800ULL,
  0x200202011080804ULL,
  0x210214001a00ULL,
  0x4481200808400082ULL,
  0x8830048114000040ULL,
  0x484081051001040ULL,
  0x42408c208200210ULL,
  0x43010841242800ULL,
  0x204a221042080000ULL,
  0x10844020a020644ULL,
  0x1000040250d0100ULL,
  0x81001421020ULL,
  0x4509002088214ULL,
  0x3224040402420c20ULL,
  0xc202402084064ULL,
  0x8a424048043130ULL,
  0x20042a0900a09004ULL,
  0x30002a10020a800ULL,
  0x2002100910120888ULL,
  0x104008990010a00ULL,
  0x1020b80218214aULL,
  0x50101024a810c240ULL,
};

  std::vector<int> mask_bit_positions(Bitboard mask) {
    std::vector<int> positions;
    while (mask) {
      int pos = __builtin_ctzll(mask);
      positions.push_back(pos);
      mask &= mask - 1;
    }
    return positions;
  }

  void init_magics(PieceType pt, Bitboard table[], Magic magics[]);
  void init_magic_with_precomputed_numbers(PieceType pt, Bitboard table[], Magic magics[], const uint64_t magic_numbers[]);
}


/// Bitboards::pretty() returns an ASCII representation of a bitboard suitable
/// to be printed to standard output. Useful for debugging.

const std::string Bitboards::pretty(Bitboard b) {

  std::string s = "+---+---+---+---+---+---+---+---+\n";

  for (Rank r = RANK_8; r >= RANK_1; --r)
  {
      for (File f = FILE_A; f <= FILE_H; ++f)
          s += b & make_square(f, r) ? "| X " : "|   ";

      s += "| " + std::to_string(1 + r) + "\n+---+---+---+---+---+---+---+---+\n";
  }
  s += "  a   b   c   d   e   f   g   h\n";

  return s;
}


/// Bitboards::init() initializes various bitboard tables. It is called at
/// startup and relies on global objects to be already zero-initialized.

void Bitboards::init() {

  for (unsigned i = 0; i < (1 << 16); ++i)
      PopCnt16[i] = uint8_t(std::bitset<16>(i).count());

  for (Square s = SQ_A1; s <= SQ_H8; ++s)
      SquareBB[s] = (1ULL << s);

  for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
      for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
          SquareDistance[s1][s2] = std::max(distance<File>(s1, s2), distance<Rank>(s1, s2));

  init_magic_with_precomputed_numbers(ROOK, RookTable, RookMagics, RookMagicNumbers);
  init_magic_with_precomputed_numbers(BISHOP, BishopTable, BishopMagics, BishopMagicNumbers);
  // init_magics(ROOK, RookTable, RookMagics);
  // init_magics(BISHOP, BishopTable, BishopMagics);

  for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
  {
      PawnAttacks[WHITE][s1] = pawn_attacks_bb<WHITE>(square_bb(s1));
      PawnAttacks[BLACK][s1] = pawn_attacks_bb<BLACK>(square_bb(s1));

      for (int step : {-9, -8, -7, -1, 1, 7, 8, 9} )
         PseudoAttacks[KING][s1] |= safe_destination(s1, step);

      for (int step : {-17, -15, -10, -6, 6, 10, 15, 17} )
         PseudoAttacks[KNIGHT][s1] |= safe_destination(s1, step);

      PseudoAttacks[QUEEN][s1]  = PseudoAttacks[BISHOP][s1] = attacks_bb<BISHOP>(s1, 0);
      PseudoAttacks[QUEEN][s1] |= PseudoAttacks[  ROOK][s1] = attacks_bb<  ROOK>(s1, 0);

      for (PieceType pt : { BISHOP, ROOK })
          for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
              if (PseudoAttacks[pt][s1] & s2)
                  LineBB[s1][s2] = (attacks_bb(pt, s1, 0) & attacks_bb(pt, s2, 0)) | s1 | s2;
  }
}


namespace {

  Bitboard sliding_attack(PieceType pt, Square sq, Bitboard occupied) {

    Bitboard attacks = 0;
    Direction   RookDirections[4] = {NORTH, SOUTH, EAST, WEST};
    Direction BishopDirections[4] = {NORTH_EAST, SOUTH_EAST, SOUTH_WEST, NORTH_WEST};

    for(Direction d : (pt == ROOK ? RookDirections : BishopDirections))
    {
        Square s = sq;
        while(safe_destination(s, d) && !(occupied & s))
            attacks |= (s += d);
    }

    return attacks;
  }


  // init_magics() computes all rook and bishop attacks at startup. Magic
  // bitboards are used to look up attacks of sliding pieces. As a reference see
  // www.chessprogramming.org/Magic_Bitboards. In particular, here we use the so
  // called "fancy" approach.

  void init_magics(PieceType pt, Bitboard table[], Magic magics[]) {

    // Optimal PRNG seeds to pick the correct magics in the shortest time
    int seeds[][RANK_NB] = { { 8977, 44560, 54343, 38998,  5731, 95205, 104912, 17020 },
                             {  728, 10316, 55013, 32803, 12281, 15100,  16645,   255 } };

    Bitboard occupancy[4096], reference[4096], edges, b;
    int epoch[4096] = {}, cnt = 0, size = 0;

    for (Square s = SQ_A1; s <= SQ_H8; ++s)
    {
        // Board edges are not considered in the relevant occupancies
        edges = ((Rank1BB | Rank8BB) & ~rank_bb(s)) | ((FileABB | FileHBB) & ~file_bb(s));

        // Given a square 's', the mask is the bitboard of sliding attacks from
        // 's' computed on an empty board. The index must be big enough to contain
        // all the attacks for each possible subset of the mask and so is 2 power
        // the number of 1s of the mask. Hence we deduce the size of the shift to
        // apply to the 64 or 32 bits word to get the index.
        Magic& m = magics[s];
        m.mask  = sliding_attack(pt, s, 0) & ~edges;
        m.shift = (Is64Bit ? 64 : 32) - popcount(m.mask);

        // Set the offset for the attacks table of the square. We have individual
        // table sizes for each square with "Fancy Magic Bitboards".
        m.attacks = s == SQ_A1 ? table : magics[s - 1].attacks + size;

        // Use Carry-Rippler trick to enumerate all subsets of masks[s] and
        // store the corresponding sliding attack bitboard in reference[].
        b = size = 0;
        do {
            occupancy[size] = b;
            reference[size] = sliding_attack(pt, s, b);

            if (HasPext)
                m.attacks[pext(b, m.mask)] = reference[size];

            size++;
            b = (b - m.mask) & m.mask;
        } while (b);

        if (HasPext)
            continue;

        PRNG rng(seeds[Is64Bit][rank_of(s)]);

        // Find a magic for square 's' picking up an (almost) random number
        // until we find the one that passes the verification test.
        for (int i = 0; i < size; )
        {
            for (m.magic = 0; popcount((m.magic * m.mask) >> 56) < 6; )
                m.magic = rng.sparse_rand<Bitboard>();

            // A good magic must map every possible occupancy to an index that
            // looks up the correct sliding attack in the attacks[s] database.
            // Note that we build up the database for square 's' as a side
            // effect of verifying the magic. Keep track of the attempt count
            // and save it in epoch[], little speed-up trick to avoid resetting
            // m.attacks[] after every failed attempt.
            for (++cnt, i = 0; i < size; ++i)
            {
                unsigned idx = m.index(occupancy[i]);

                if (epoch[idx] < cnt)
                {
                    epoch[idx] = cnt;
                    m.attacks[idx] = reference[i];
                }
                else if (m.attacks[idx] != reference[i])
                    break;
            }
        }
    }
  }

  void init_magic_with_precomputed_numbers(PieceType pt, Bitboard table[], Magic magics[], const uint64_t magic_numbers[]) {
    Bitboard edges, b;
    int size = 0;

    for (Square s = SQ_A1; s <= SQ_H8; ++s) {
      // Board edges are not considered in the relevant occupancies
      edges = ((Rank1BB | Rank8BB) & ~rank_bb(s)) | ((FileABB | FileHBB) & ~file_bb(s));

      Magic& m = magics[s];
      m.mask = sliding_attack(pt, s, 0) & ~edges;
      m.shift = (Is64Bit ? 64 : 32) - popcount(m.mask);
      m.magic = magic_numbers[s];
          // std::cout << "Square " << s
          //     << " mask = 0x" << std::hex << m.mask << std::dec
          //     << " (" << popcount(m.mask) << " bits)\n";
      // Set the offset for the attacks table of the square
      m.attacks = s == SQ_A1 ? table : magics[s - 1].attacks + size;

      // Use Carry-Rippler trick to enumerate all subsets of masks[s]
      b = size = 0;
      do {
        m.attacks[m.index(b)] = sliding_attack(pt, s, b);
        size++;
        b = (b - m.mask) & m.mask;
      } while (b);
    }
  }

}
