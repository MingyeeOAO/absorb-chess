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
#include <iostream>
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
    0x1100400000808020ULL,
    0x1100400000808020ULL,
    0x200a10e0800890ULL,
    0x10a00c000800410ULL,
    0x9080084080810404ULL,
    0x4081a0481000201ULL,
    0x48600480102008a1ULL,
    0x8201228080801249ULL,
    0x100500000440204ULL,
    0x1020031000200804ULL,
    0x2010802000082008ULL,
    0x2010802000082008ULL,
    0x20500806801a0022ULL,
    0x20500806801a0022ULL,
    0x38421000a008022ULL,
    0x108442002200811ULL,
    0x8002c02009010202ULL,
    0x2041200441100040ULL,
    0x2400300100004420ULL,
    0x400090210004042ULL,
    0x580100800080102ULL,
    0x3100c0020020202ULL,
    0x5020048820101ULL,
    0x2491040100000201ULL,
    0x1080010200424021ULL,
    0x3042050080908022ULL,
    0x4820802c020212ULL,
    0x1010006420000921ULL,
    0x58cc050008229801ULL,
    0x14400200408901ULL,
    0xc008104230680104ULL,
    0xd00048201380041ULL,
    0x40105040900823ULL,
    0x40105040900823ULL,
    0x80220600008610ULL,
    0x80502010008289ULL,
    0x1640040011120008ULL,
    0x80048000a41102ULL,
    0x40010000028c4aULL,
    0x81004000009601ULL,
    0x20800000049050ULL,
    0x2020200802409009ULL,
    0x184202200080441ULL,
    0x821000800210010ULL,
    0x302040201006208ULL,
    0x400402220054302ULL,
    0x4020808200e001ULL,
    0x400404030110081ULL,
    0x40302000900080ULL,
    0x60108080c0086941ULL,
    0x41010200c002106ULL,
    0x801180800810400aULL,
    0x41010200c002106ULL,
    0x890c80401002004ULL,
    0x11b0201000104082ULL,
    0x180028090800871ULL,
    0x280006104304013ULL,
    0xa1405140040221ULL,
    0x2011482520086005ULL,
    0x404405290881822ULL,
    0x12508c220a640482ULL,
    0x818211260000402ULL,
    0x12008104000a85ULL,
    0x20009023018000c1ULL
};

const uint64_t BishopMagicNumbers[64] = {
0x31010a0044021521ULL,
    0x80200710301002ULL,
    0x4221080080049122ULL,
    0x1000124640080581ULL,
    0x84084410001450c0ULL,
    0x900808020a060104ULL,
    0x848401c04c0d808ULL,
    0x1100a40c3808528ULL,
    0x4801304440803027ULL,
    0x24081202006901bULL,
    0x8606120002000401ULL,
    0x880102091a82404ULL,
    0x1040002a20030a32ULL,
    0x44201a0160021091ULL,
    0x1008080104402244ULL,
    0x182203100450909ULL,
    0x12100c4302280010ULL,
    0x9a58410212580017ULL,
    0x142058800102009ULL,
    0x620a00400008104ULL,
    0x301148200010002ULL,
    0x8900900800204026ULL,
    0x105200108024202ULL,
    0x420a0410804092ULL,
    0x4802086023601201ULL,
    0x1811040840b00600ULL,
    0x900c20004031000ULL,
    0x2010201840004400ULL,
    0x80805008101440ULL,
    0x80a00c11006100ULL,
    0x424010600114904ULL,
    0x424010600114904ULL,
    0x1220200802021804ULL,
    0x814040000015102ULL,
    0x6c10180040c04ULL,
    0x401880a000000208ULL,
    0x812480883820042ULL,
    0x80808025149011ULL,
    0x6c10180040c04ULL,
    0x101c2007000812aULL,
    0x2402120200880202ULL,
    0x863244230004108ULL,
    0x120820000114108ULL,
    0x2090110022400099ULL,
    0x1410020240000202ULL,
    0xb040822001411001ULL,
    0x20031000204012aULL,
    0x81420500109001c1ULL,
    0x828000078040105ULL,
    0x402063624084424ULL,
    0x40b0000124240049ULL,
    0x504400000c040252ULL,
    0x20a050102880092ULL,
    0x100220000130a004ULL,
    0x8108540051302bULL,
    0x708028a2008d1044ULL,
    0x10940401000a0101ULL,
    0x118244024002821ULL,
    0x8406062000441221ULL,
    0x20a020000030108ULL,
    0x10020225200102a0ULL,
    0x2c6220020400120ULL,
    0x80e910800104144ULL,
    0x50c200800a982129ULL
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

    std::cout << (pt == ROOK ? "Rook" : "Bishop") << "MagicNumbers[64] = {\n";
    for(int i=0; i<64; i++) {
        std::cout << "    0x" << std::hex << magics[i].magic << std::dec << "ULL" << (i==63 ? "" : ",") << "\n";
    }
    std::cout << "};\n";
  }

}
