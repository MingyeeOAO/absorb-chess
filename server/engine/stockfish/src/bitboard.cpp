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
  0x80001020804002ULL,
  0x40001000402004ULL,
  0x4980092000801000ULL,
  0x200100806002040ULL,
  0x100041003000800ULL,
  0x600081001820044ULL,
  0x48000800a000100ULL,
  0x8200010030804204ULL,
  0x10800020400080ULL,
  0x1280c00120100045ULL,
  0x906002046801200ULL,
  0x801000800800ULL,
  0x20800800800400ULL,
  0x6000402000810ULL,
  0x242000104020048ULL,
  0x402000080440221ULL,
  0x828020800040008cULL,
  0x8420404010002000ULL,
  0x4200280300082a1ULL,
  0x90010010420ULL,
  0x208010009000410ULL,
  0x80120041040ULL,
  0x1408010100020004ULL,
  0x9002001b440081ULL,
  0x312088e080064000ULL,
  0x48200880400080ULL,
  0x100280200084ULL,
  0x8181300280280080ULL,
  0x88000880800400ULL,
  0x20020022003008a4ULL,
  0x1250810400081002ULL,
  0x2101000100008042ULL,
  0x804002800020ULL,
  0x200201008400041ULL,
  0x2791002001004010ULL,
  0x8008008801002ULL,
  0x4008800400800800ULL,
  0x2002001002000409ULL,
  0x4001311014002208ULL,
  0x405088412002445ULL,
  0x130800040008020ULL,
  0x111100408a020020ULL,
  0x2048200104110040ULL,
  0x8001000810010020ULL,
  0x40801010010ULL,
  0x4001000804010002ULL,
  0x3001a2040008ULL,
  0x400090493420004ULL,
  0x141003080004300ULL,
  0x804200040100440ULL,
  0x802000100080ULL,
  0x40480010018280ULL,
  0x8114810800240180ULL,
  0x100800200040080ULL,
  0x2402000401080200ULL,
  0x2310a401044200ULL,
  0xd000820020401502ULL,
  0x900204a02108102ULL,
  0x40102001000c41ULL,
  0x8410041001000821ULL,
  0x2082010820049002ULL,
  0x1001000208040001ULL,
  0x208130020804ULL,
  0x1000808401002042ULL,
};

const uint64_t BishopMagicNumbers[64] = {
  0x8010444120540100ULL,
  0x4010801110000ULL,
  0x28208302108000ULL,
  0x784040482000081ULL,
  0x1144042004000080ULL,
  0x22021005080441ULL,
  0x100041049040401cULL,
  0xc0440044026088ULL,
  0x1080214401420402ULL,
  0x5304142041c40ULL,
  0x1408020403b481ULL,
  0x80041052040040ULL,
  0x220445041008200ULL,
  0x8440008804400408ULL,
  0x4883008401884101ULL,
  0x8102404404010802ULL,
  0x18404058a80894ULL,
  0x1690002801011410ULL,
  0x808004420401200ULL,
  0x428000101490000ULL,
  0x840c00a00001ULL,
  0x2c4104600420816ULL,
  0x802100420101a040ULL,
  0x1000484201008823ULL,
  0x2184100022209101ULL,
  0xb002090a10010800ULL,
  0x111220814080200ULL,
  0x1040000440080ULL,
  0x11004104044000ULL,
  0x2d48020081888401ULL,
  0x830112051080ULL,
  0x2604001030800ULL,
  0x10020806101041ULL,
  0x218a040020600ULL,
  0x2020180208040400ULL,
  0x40020080080080ULL,
  0x2884150110040040ULL,
  0x108100490010048ULL,
  0x124088080240c0eULL,
  0x222004101c84401ULL,
  0x14008410400c0848ULL,
  0x12020a20010370ULL,
  0x1002011248004405ULL,
  0x2004208000080ULL,
  0x208180302400400ULL,
  0x1902300604880200ULL,
  0x480800c0988400ULL,
  0xc001011902000100ULL,
  0x412841c02408020ULL,
  0x2620540084104810ULL,
  0x408c09048080002ULL,
  0x2010002020880280ULL,
  0x480604242820400ULL,
  0x400100210144008ULL,
  0x80110202084a0000ULL,
  0x48201a5400408020ULL,
  0x40242410080800ULL,
  0x84108690b01000ULL,
  0x8080000044044400ULL,
  0x11e0101000208841ULL,
  0xa2001d0904208208ULL,
  0x12b04104281086ULL,
  0x23000430a4010410ULL,
  0x40100202004412ULL,
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
