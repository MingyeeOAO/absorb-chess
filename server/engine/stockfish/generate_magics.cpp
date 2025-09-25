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


// Sliding attack logic adapted from the engine implementation. The generator
// uses plain ints/U64 for compatibility while keeping the same stepping
// behaviour (prevents wrap-around across files).
// Re-implement sliding attack behaviour using the exact stepping logic
// used by the engine: use Direction arrays and the "while(safe_destination(s,d) && !(occupied & s)) attacks |= (s += d);"
// This keeps variable names and loop structure similar to the engine implementation.
static inline bool is_ok_sq(int s) { return (s >= 0 && s < 64); }
static inline int sq_distance(int s1, int s2) {
    int f1 = file_of(s1), r1 = rank_of(s1);
    int f2 = file_of(s2), r2 = rank_of(s2);
    return std::max(std::abs(f1 - f2), std::abs(r1 - r2));
}
static inline bool safe_destination_int(int s, int step) {
    int to = s + step;
    return is_ok_sq(to) && (sq_distance(s, to) <= 2);
}

static inline U64 sliding_attack_on_the_fly(bool rook, int sq, U64 occupied) {
    U64 attacks = 0ULL;

    const int RookDirections[4]   = {  8, -8,  1, -1 }; // N, S, E, W
    const int BishopDirections[4] = {  9, -7, -9,  7 }; // NE, SE, SW, NW

    const int *dirs = rook ? RookDirections : BishopDirections;

    for (int i = 0; i < 4; ++i) {
        int d = dirs[i];
        int s = sq;
        // Mirror engine loop: while(safe_destination(s, d) && !(occupied & s)) attacks |= (s += d);
        while (safe_destination_int(s, d) && !(occupied & (1ULL << s))) {
            s += d;
            attacks |= (1ULL << s);
        }
    }

    return attacks;
}

// Generator helper wrappers expected by the rest of this program
static inline U64 rook_attacks_on_the_fly(int sq, U64 occupied) {
    return sliding_attack_on_the_fly(true, sq, occupied);
}

static inline U64 bishop_attacks_on_the_fly(int sq, U64 occupied) {
    return sliding_attack_on_the_fly(false, sq, occupied);
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

    // Precompute occupancies and reference attacks
    vector<U64> occupancy(subsets), reference(subsets);
    for (int i=0;i<subsets;++i){
        occupancy[i] = index_to_occ(i, bits);
        reference[i] = rook ? rook_attacks_on_the_fly(sq, occupancy[i])
                            : bishop_attacks_on_the_fly(sq, occupancy[i]);
    }

    unsigned shift = 64 - B;

    // Engine-like epoch verification to avoid clearing table each attempt
    const int tableSize = 1 << B;
    vector<int> epoch(tableSize, 0);
    vector<U64> table(tableSize, 0ULL);

    // Seeded RNG to mirror engine behaviour (per-rank seed could be used)
    std::mt19937_64 local_rng((unsigned)chrono::high_resolution_clock::now().time_since_epoch().count());

    const int MAX_ATTEMPTS = 20000000; // hard cap to avoid infinite loops
    int attempts = 0;
    int counter = 1;

    while (attempts < MAX_ATTEMPTS) {
        ++attempts;
        U64 magic = local_rng();

        if (popcount64((mask * magic) & 0xFF00000000000000ULL) < 4) continue;

        ++counter;
        bool fail = false;

        for (int i = 0; i < subsets; ++i) {
            unsigned idx = transform_idx(occupancy[i], magic, shift);
            if (epoch[idx] < counter) {
                epoch[idx] = counter;
                table[idx] = reference[i];
            } else if (table[idx] != reference[i]) {
                fail = true;
                break;
            }
        }

        if (!fail) return magic;
    }

    // Fallback: warn and return a pseudo-random magic so generation can continue
    cerr << "Warning: failed to find perfect magic for square " << sq << " (rook=" << rook
         << ") after " << attempts << " attempts; returning fallback magic.\n";
    return local_rng();
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
