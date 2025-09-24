// gen_magics.cpp
// Generates rook and bishop magic numbers compatible with:
// index = unsigned(((occupied & mask) * magic) >> shift);
// Assumes 64-bit Bitboard, no PEXT (HasPext=false)
#include <bits/stdc++.h>
using namespace std;
using U64 = unsigned long long;
int popcount64(U64 x){ return __builtin_popcountll(x); }
static std::mt19937_64 rng((unsigned)chrono::high_resolution_clock::now().time_since_epoch().count());
U64 rnd64(){ return rng(); }
U64 rnd64_fewbits(){ return rnd64() & rnd64() & rnd64(); }
inline int rank_of(int s){ return s/8; }
inline int file_of(int s){ return s%8; }
inline U64 square_bb(int s){ return 1ULL << s; }

U64 rook_mask(int sq){
    int r = rank_of(sq), f = file_of(sq);
    U64 m = 0ULL;
    for (int rr = r+1; rr <= 6; ++rr) m |= square_bb(rr*8 + f);
    for (int rr = r-1; rr >= 1; --rr)   m |= square_bb(rr*8 + f);
    for (int ff = f+1; ff <= 6; ++ff)   m |= square_bb(r*8 + ff);
    for (int ff = f-1; ff >= 1; --ff)   m |= square_bb(r*8 + ff);
    return m;
}
U64 bishop_mask(int sq) {
    int r = rank_of(sq), f = file_of(sq);
    U64 m = 0ULL;

    // Diagonals, but stop one before the edge (like Stockfish does)
    for (int rr = r+1, ff=f+1; rr<=6 && ff<=6; ++rr, ++ff) m |= square_bb(rr*8 + ff);
    for (int rr = r+1, ff=f-1; rr<=6 && ff>=1; ++rr, --ff) m |= square_bb(rr*8 + ff);
    for (int rr = r-1, ff=f+1; rr>=1 && ff<=6; --rr, ++ff) m |= square_bb(rr*8 + ff);
    for (int rr = r-1, ff=f-1; rr>=1 && ff>=1; --rr, --ff) m |= square_bb(rr*8 + ff);

    return m;
}

U64 rook_attacks_on_the_fly(int sq, U64 blockers){
    U64 att = 0ULL;
    int r = rank_of(sq), f = file_of(sq);
    for (int rr=r+1; rr<=7; ++rr){ att |= square_bb(rr*8 + f); if(blockers & square_bb(rr*8 + f)) break; }
    for (int rr=r-1; rr>=0; --rr){ att |= square_bb(rr*8 + f); if(blockers & square_bb(rr*8 + f)) break; }
    for (int ff=f+1; ff<=7; ++ff){ att |= square_bb(r*8 + ff); if(blockers & square_bb(r*8 + ff)) break; }
    for (int ff=f-1; ff>=0; --ff){ att |= square_bb(r*8 + ff); if(blockers & square_bb(r*8 + ff)) break; }
    return att;
}
U64 bishop_attacks_on_the_fly(int sq, U64 blockers){
    U64 att = 0ULL;
    int r = rank_of(sq), f = file_of(sq);
    for (int rr=r+1, ff=f+1; rr<=7 && ff<=7; ++rr, ++ff){ att |= square_bb(rr*8 + ff); if(blockers & square_bb(rr*8 + ff)) break; }
    for (int rr=r+1, ff=f-1; rr<=7 && ff>=0; ++rr, --ff){ att |= square_bb(rr*8 + ff); if(blockers & square_bb(rr*8 + ff)) break; }
    for (int rr=r-1, ff=f+1; rr>=0 && ff<=7; --rr, ++ff){ att |= square_bb(rr*8 + ff); if(blockers & square_bb(rr*8 + ff)) break; }
    for (int rr=r-1, ff=f-1; rr>=0 && ff>=0; --rr, --ff){ att |= square_bb(rr*8 + ff); if(blockers & square_bb(rr*8 + ff)) break; }
    return att;
}

vector<int> mask_bits(U64 mask){
    vector<int> bits;
    U64 m = mask;
    while (m) {
        int lsb = __builtin_ctzll(m);
        bits.push_back(lsb);
        m &= m - 1;
    }
    return bits;
}
U64 index_to_occ(unsigned idx, const vector<int>& bits){
    U64 occ = 0ULL;
    for (size_t i=0;i<bits.size();++i) if (idx & (1u<<i)) occ |= (1ULL << bits[i]);
    return occ;
}
unsigned transform_idx(U64 occ, U64 magic, unsigned shift){
    return unsigned((occ * magic) >> shift);
}

U64 find_magic_for_square(int sq, bool rook){
    U64 mask = rook ? rook_mask(sq) : bishop_mask(sq);
    auto bits = mask_bits(mask);
    int B = (int)bits.size();
    int subsets = 1<<B;
    vector<U64> ref(subsets);
    for (int i=0;i<subsets;++i){
        U64 occ = index_to_occ(i, bits);
        ref[i] = rook ? rook_attacks_on_the_fly(sq, occ) : bishop_attacks_on_the_fly(sq, occ);
    }
    unsigned shift = 64 - B;
    for (int attempt=0; attempt < 2000000; ++attempt){
        U64 magic = rnd64_fewbits();
        if (popcount64((mask * magic) & 0xFF00000000000000ULL) < 6) continue;
        unordered_map<unsigned, U64> table;
        bool fail=false;
        for (int i=0;i<subsets && !fail;++i){
            U64 occ = index_to_occ(i, bits);
            unsigned idx = transform_idx(occ, magic, shift);
            auto it = table.find(idx);
            if (it == table.end()) table[idx] = ref[i];
            else if (it->second != ref[i]) fail = true;
        }
        if (!fail) return magic;
    }
    return 0ULL;
}

int main(){
    ios::fmtflags f = cout.flags();
    cout << hex;
    cout << "const uint64_t RookMagicNumbers[64] = {\n";
    for (int s=0;s<64;++s){
        U64 m = find_magic_for_square(s, true);
        if (m == 0ULL){ cerr << "Failed rook " << s << "\n"; return 1; }
        cout << "  0x" << m << "ULL,\n";
    }
    cout << "};\n\n";
    cout << "const uint64_t BishopMagicNumbers[64] = {\n";
    for (int s=0;s<64;++s){
        U64 m = find_magic_for_square(s, false);
        if (m == 0ULL){ cerr << "Failed bishop " << s << "\n"; return 1; }
        cout << "  0x" << m << "ULL,\n";
    }
    cout << "};\n";
    cout.flags(f);

    // for(int x=0; x<64; x++){
    //     cout << "Square " << x << " mask = 0x" << hex << rook_mask(x) << dec << " (" << popcount64(rook_mask(x)) << " bits)\n";
    // }
    // for(int x=0; x<64; x++)
    // cout << "Square " << x << " mask = 0x" << hex << bishop_mask(x) << dec << " (" << popcount64(bishop_mask(x)) << " bits)\n";
    return 0;
}
