// chess_engine_v2.cpp
#include "chess_engine_v2.hpp"
#include <algorithm>
#include <climits>
#include <chrono>
#include <iostream>
#include <cassert>
#include <vector>
#include <tuple>
#include <iomanip>
//#include <bits/stdc++.h>

// Static lookup table declarations
uint64_t ChessEngine::king_attacks[64];
uint64_t ChessEngine::knight_attacks[64];
uint64_t ChessEngine::pawn_attacks[2][64];

uint64_t ChessEngine::rook_masks[64];
uint64_t ChessEngine::bishop_masks[64];
int ChessEngine::rook_shifts[64] = {
    12, 11, 11, 11, 11, 11, 11, 12,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    12, 11, 11, 11, 11, 11, 11, 12
};
int ChessEngine::bishop_shifts[64] = {
    6, 5, 5, 5, 5, 5, 5, 6,
    5, 5, 7, 7, 7, 7, 5, 5,
    5, 7, 7, 7, 7, 7, 7, 5,
    5, 7, 9, 9, 9, 9, 7, 5,
    5, 7, 9, 9, 9, 9, 7, 5,
    5, 7, 7, 7, 7, 7, 7, 5,
    5, 5, 7, 7, 7, 7, 5, 5,
    6, 5, 5, 5, 5, 5, 5, 6
};
bool ChessEngine::tables_initialized = false;
MagicTable ChessEngine::rook_table;
MagicTable ChessEngine::bishop_table;
// ========== CONSTRUCTOR ==========
ChessEngine::ChessEngine() {
    if (!tables_initialized) {
        init_lookup_tables();
        init_magic_bitboards(); // safe stub; magic tables not used by default attack generation
        tables_initialized = true;
    }
    
    // Initialize empty bitboards
    for (int color = 0; color < 2; ++color) {
        for (int piece = 0; piece < 6; ++piece) {
            piece_bb[color][piece] = 0ULL;
            ability_bb[color][piece] = 0ULL;
        }
        has_moved_bb[color] = 0ULL;
    }
    
    occupancy_white = occupancy_black = occupancy_all = 0ULL;
    white_to_move = true;
    white_king_castled = black_king_castled = false;
    en_passant_col = en_passant_row = -1;
    eval_cache_valid = false;
    nodes_searched = quiescence_nodes = 0;
}

// ========== LOOKUP TABLE INITIALIZATION ==========
void ChessEngine::init_lookup_tables() {
    // King attacks
    for (int sq = 0; sq < 64; ++sq) {
        int r = row_of(sq), c = col_of(sq);
        uint64_t atk = 0ULL;
        for (int dr = -1; dr <= 1; ++dr) {
            for (int dc = -1; dc <= 1; ++dc) {
                if (dr == 0 && dc == 0) continue;
                int nr = r + dr, nc = c + dc;
                if (nr >= 0 && nr < 8 && nc >= 0 && nc < 8) atk |= square_bb(nr, nc);
            }
        }
        king_attacks[sq] = atk;
    }
    
    // Knight attacks
    const int knight_moves[8][2] = {{-2,-1}, {-2,1}, {-1,-2}, {-1,2}, {1,-2}, {1,2}, {2,-1}, {2,1}};
    for (int sq = 0; sq < 64; ++sq) {
        int r = row_of(sq), c = col_of(sq);
        uint64_t atk = 0ULL;
        for (int i = 0; i < 8; ++i) {
            int nr = r + knight_moves[i][0];
            int nc = c + knight_moves[i][1];
            if (nr >= 0 && nr < 8 && nc >= 0 && nc < 8) atk |= square_bb(nr, nc);
        }
        knight_attacks[sq] = atk;
    }
    
    // Pawn attacks: index 0 = white (moves up / row-1), 1 = black (moves down / row+1)
    for (int sq = 0; sq < 64; ++sq) {
        int r = row_of(sq), c = col_of(sq);
        uint64_t w_atk = 0ULL, b_atk = 0ULL;
        if (r > 0) {
            if (c > 0) w_atk |= square_bb(r - 1, c - 1);
            if (c < 7) w_atk |= square_bb(r - 1, c + 1);
        }
        if (r < 7) {
            if (c > 0) b_atk |= square_bb(r + 1, c - 1);
            if (c < 7) b_atk |= square_bb(r + 1, c + 1);
        }
        pawn_attacks[0][sq] = w_atk;
        pawn_attacks[1][sq] = b_atk;
    }
    
    // Masks for sliding pieces (exclude edges)
    for (int sq = 0; sq < 64; ++sq) {
        int r = row_of(sq), c = col_of(sq);
        uint64_t rm = 0ULL, bm = 0ULL;
        
        // Rook masks: add squares along rank/file but stop before edges (1..6)
        // North (r+1..6)
        for (int rr = r + 1; rr <= 6; ++rr) rm |= square_bb(rr, c);
        // South (r-1..1)
        for (int rr = r - 1; rr >= 1; --rr) rm |= square_bb(rr, c);
        // East (c+1..6)
        for (int cc = c + 1; cc <= 6; ++cc) rm |= square_bb(r, cc);
        // West (c-1..1)
        for (int cc = c - 1; cc >= 1; --cc) rm |= square_bb(r, cc);
        rook_masks[sq] = rm;
        
        // Bishop masks: diagonal squares excluding edges
        // NE
        for (int rr = r + 1, cc = c + 1; rr <= 6 && cc <= 6; ++rr, ++cc) bm |= square_bb(rr, cc);
        // NW
        for (int rr = r + 1, cc = c - 1; rr <= 6 && cc >= 1; ++rr, --cc) bm |= square_bb(rr, cc);
        // SE
        for (int rr = r - 1, cc = c + 1; rr >= 1 && cc <= 6; --rr, ++cc) bm |= square_bb(rr, cc);
        // SW
        for (int rr = r - 1, cc = c - 1; rr >= 1 && cc >= 1; --rr, --cc) bm |= square_bb(rr, cc);
        bishop_masks[sq] = bm;
    }
}

// ========== MAGIC BITBOARD INITIALIZATION (STUB SAFE) ==========
void ChessEngine::init_magic_bitboards() {
    if (tables_initialized) return;

    for (int sq = 0; sq < 64; ++sq) {
        // ============ ROOK ============
        uint64_t mask = rook_masks[sq];   // 你自己生成的 mask
        rook_table.masks[sq] = mask;
        int bits = popcount(mask);
        int table_size = 1 << bits;
        rook_table.table_sizes[sq] = table_size;
        rook_table.attacks_ptr[sq] = new uint64_t[table_size];

        for (int i = 0; i < table_size; ++i) {
            uint64_t blockers = 0ULL;
            int bit_pos = 0;
            for (uint64_t bb = mask; bb; bb = clear_lsb(bb)) {
                int b = bitscan_forward(bb);
                if (i & (1 << bit_pos)) blockers |= (1ULL << b);
                bit_pos++;
            }
            rook_table.attacks_ptr[sq][i] = slow_rook_attacks(sq, blockers);
        }

        // ============ BISHOP ============
        mask = bishop_masks[sq];
        bishop_table.masks[sq] = mask;
        bits = popcount(mask);
        table_size = 1 << bits;
        bishop_table.table_sizes[sq] = table_size;
        bishop_table.attacks_ptr[sq] = new uint64_t[table_size];

        for (int i = 0; i < table_size; ++i) {
            uint64_t blockers = 0ULL;
            int bit_pos = 0;
            for (uint64_t bb = mask; bb; bb = clear_lsb(bb)) {
                int b = bitscan_forward(bb);
                if (i & (1 << bit_pos)) blockers |= (1ULL << b);
                bit_pos++;
            }
            bishop_table.attacks_ptr[sq][i] = slow_bishop_attacks(sq, blockers);
        }
    }

    tables_initialized = true;
}
uint64_t ChessEngine::get_rook_attacks(int sq, uint64_t occupancy) const {
    uint64_t mask = rook_table.masks[sq];
    uint64_t occ = occupancy & mask;
    int idx = 0;
    int bit_pos = 0;
    for (uint64_t bb = mask; bb; bb = clear_lsb(bb)) {
        int b = bitscan_forward(bb);
        if (occ & (1ULL << b)) idx |= (1 << bit_pos);
        bit_pos++;
    }
    return rook_table.attacks_ptr[sq][idx];
}

uint64_t ChessEngine::get_bishop_attacks(int sq, uint64_t occupancy) const {
    uint64_t mask = bishop_table.masks[sq];
    uint64_t occ = occupancy & mask;
    int idx = 0;
    int bit_pos = 0;
    for (uint64_t bb = mask; bb; bb = clear_lsb(bb)) {
        int b = bitscan_forward(bb);
        if (occ & (1ULL << b)) idx |= (1 << bit_pos);
        bit_pos++;
    }
    return bishop_table.attacks_ptr[sq][idx];
}

uint64_t ChessEngine::get_queen_attacks(int sq, uint64_t occupancy) const {
    // queen = rook + bishop
    return get_rook_attacks(sq, occupancy) | get_bishop_attacks(sq, occupancy);
}

// ========== OCCUPANCY UPDATE / BOARD CONVERSION ==========
void ChessEngine::update_occupancy() {
    occupancy_white = occupancy_black = 0ULL;
    for (int piece = 0; piece < 6; ++piece) {
        occupancy_white |= piece_bb[0][piece];
        occupancy_black |= piece_bb[1][piece];
    }
    occupancy_all = occupancy_white | occupancy_black;
}

void ChessEngine::update_from_legacy_board(const std::vector<std::vector<uint32_t>>& board) {
    // Clear
    for (int color = 0; color < 2; ++color) {
        for (int p = 0; p < 6; ++p) {
            piece_bb[color][p] = 0ULL;
            ability_bb[color][p] = 0ULL;
        }
        has_moved_bb[color] = 0ULL;
    }

    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            uint32_t pd = board[r][c];
            if (pd == 0) continue;
            bool is_white = (pd & IS_WHITE) != 0;
            int color = is_white ? 0 : 1;
            int sq = square(r, c);
            uint64_t sq_bb = square_bb(sq);
            
            if (pd & HAS_MOVED) has_moved_bb[color] |= sq_bb;
            
            if (pd & PIECE_PAWN) piece_bb[color][0] |= sq_bb;
            else if (pd & PIECE_KNIGHT) piece_bb[color][1] |= sq_bb;
            else if (pd & PIECE_BISHOP) piece_bb[color][2] |= sq_bb;
            else if (pd & PIECE_ROOK) piece_bb[color][3] |= sq_bb;
            else if (pd & PIECE_QUEEN) piece_bb[color][4] |= sq_bb;
            else if (pd & PIECE_KING) piece_bb[color][5] |= sq_bb;
            
            if (pd & ABILITY_PAWN) ability_bb[color][0] |= sq_bb;
            if (pd & ABILITY_KNIGHT) ability_bb[color][1] |= sq_bb;
            if (pd & ABILITY_BISHOP) ability_bb[color][2] |= sq_bb;
            if (pd & ABILITY_ROOK) ability_bb[color][3] |= sq_bb;
            if (pd & ABILITY_QUEEN) ability_bb[color][4] |= sq_bb;
            if (pd & ABILITY_KING) ability_bb[color][5] |= sq_bb;
        }
    }
    update_occupancy();
}

std::vector<std::vector<uint32_t>> ChessEngine::convert_to_legacy_board() const {
    std::vector<std::vector<uint32_t>> board(8, std::vector<uint32_t>(8, 0));
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            int sq = square(r, c);
            uint64_t sq_bb = square_bb(sq);
            uint32_t data = 0;
            for (int color = 0; color < 2; ++color) {
                if ((occupancy_white | occupancy_black) & sq_bb) {
                    // only set if a piece exists
                    if (piece_bb[color][0] & sq_bb) data |= PIECE_PAWN;
                    else if (piece_bb[color][1] & sq_bb) data |= PIECE_KNIGHT;
                    else if (piece_bb[color][2] & sq_bb) data |= PIECE_BISHOP;
                    else if (piece_bb[color][3] & sq_bb) data |= PIECE_ROOK;
                    else if (piece_bb[color][4] & sq_bb) data |= PIECE_QUEEN;
                    else if (piece_bb[color][5] & sq_bb) data |= PIECE_KING;
                    
                    if (ability_bb[color][0] & sq_bb) data |= ABILITY_PAWN;
                    if (ability_bb[color][1] & sq_bb) data |= ABILITY_KNIGHT;
                    if (ability_bb[color][2] & sq_bb) data |= ABILITY_BISHOP;
                    if (ability_bb[color][3] & sq_bb) data |= ABILITY_ROOK;
                    if (ability_bb[color][4] & sq_bb) data |= ABILITY_QUEEN;
                    if (ability_bb[color][5] & sq_bb) data |= ABILITY_KING;
                    
                    if (has_moved_bb[color] & sq_bb) data |= HAS_MOVED;
                    
                    if (color == 0) data |= IS_WHITE; // white bit
                    else data &= ~IS_WHITE; // black - keep it 0
                }
            }
            board[r][c] = data;
        }
    }
    return board;
}

// ========== ATTACK GENERATION ==========
uint64_t ChessEngine::get_attacks_by_piece_type(int square, int piece_type, bool white, uint64_t blockers) const {
    // piece_type: 0 pawn, 1 knight, 2 bishop, 3 rook, 4 queen, 5 king
    switch (piece_type) {
        case 0: return pawn_attacks[white ? 0 : 1][square];
        case 1: return knight_attacks[square];
        case 2: return get_bishop_attacks(square, occupancy_all );
        case 3: return get_rook_attacks(square, blockers);
        case 4: return get_queen_attacks(square, blockers);
        case 5: return king_attacks[square];
        default: return 0ULL;
    }
}

uint64_t ChessEngine::get_all_attacks(bool white) const {
    uint64_t attacks = 0ULL;
    int color = white ? 0 : 1;
    // regular pieces
    for (int pt = 0; pt < 6; ++pt) {
        uint64_t pieces = piece_bb[color][pt];
        while (pieces) {
            int sq = bitscan_forward(pieces);
            pieces = clear_lsb(pieces);
            attacks |= get_attacks_by_piece_type(sq, pt, white, occupancy_all);
        }
        // absorbed abilities
        uint64_t abilities = ability_bb[color][pt];
        while (abilities) {
            int sq = bitscan_forward(abilities);
            abilities = clear_lsb(abilities);
            // ensure there is a piece at this square (ability only meaningful if a piece occupies)
            if (occupancy_all & square_bb(sq)) attacks |= get_attacks_by_piece_type(sq, pt, white, occupancy_all);
        }
    }
    return attacks;
}

bool ChessEngine::is_square_attacked(int square, bool by_white) const {
    int color = by_white ? 0 : 1;
    for (int pt = 0; pt < 6; ++pt) {
        uint64_t pieces = piece_bb[color][pt];
        while (pieces) {
            int sq = bitscan_forward(pieces);
            pieces = clear_lsb(pieces);
            uint64_t attacks = get_attacks_by_piece_type(sq, pt, by_white, occupancy_all);
            if (attacks & square_bb(square)) return true;
        }
        uint64_t abilities = ability_bb[color][pt];
        while (abilities) {
            int sq = bitscan_forward(abilities);
            abilities = clear_lsb(abilities);
            if (!(occupancy_all & square_bb(sq))) continue;
            uint64_t attacks = get_attacks_by_piece_type(sq, pt, by_white, occupancy_all);
            if (attacks & square_bb(square)) return true;
        }
    }
    return false;
}

bool ChessEngine::is_in_check(bool white_king) const {
    int color = white_king ? 0 : 1;
    uint64_t kbb = piece_bb[color][5];
    if (!kbb) return false;
    int ksq = bitscan_forward(kbb);
    return is_square_attacked(ksq, !white_king);
}

// ========== MOVE GENERATION HELPERS ==========
void ChessEngine::add_moves_from_bitboard(int from_square, uint64_t targets, std::vector<Move>& moves, uint32_t flags) const {
    while (targets) {
        int to = bitscan_forward(targets);
        targets = clear_lsb(targets);
        moves.emplace_back(row_of(from_square), col_of(from_square), row_of(to), col_of(to), flags);
    }
}

void ChessEngine::add_pawn_moves(int from_square, uint64_t targets, bool white, std::vector<Move>& moves) const {
    int promotion_rank = white ? 7 : 0; // white promotes to rank 7 (row 7), black to rank 0 (row 0)
    while (targets) {
        int to = bitscan_forward(targets);
        targets = clear_lsb(targets);
        int to_row = row_of(to);
        // promotion flags: 4=queen,5=rook,6=bishop,7=knight (lose pawn ability, gain promoted piece ability)
        if (to_row == promotion_rank) {
            moves.emplace_back(row_of(from_square), col_of(from_square), to_row, col_of(to), 4); // promote to queen
            moves.emplace_back(row_of(from_square), col_of(from_square), to_row, col_of(to), 5); // promote to rook
            moves.emplace_back(row_of(from_square), col_of(from_square), to_row, col_of(to), 4); // promote to queen
            moves.emplace_back(row_of(from_square), col_of(from_square), to_row, col_of(to), 5); // promote to rook
            moves.emplace_back(row_of(from_square), col_of(from_square), to_row, col_of(to), 6); // promote to bishop
            moves.emplace_back(row_of(from_square), col_of(from_square), to_row, col_of(to), 7); // promote to knight
        } else {
            moves.emplace_back(row_of(from_square), col_of(from_square), to_row, col_of(to), 0);
        }
    }
}

// ========== MOVE GENERATION ==========
void ChessEngine::generate_pawn_moves_bb(bool white, std::vector<Move>& moves) const {
    int color = white ? 0 : 1;
    uint64_t pawns = piece_bb[color][0];
    int dir = white ? -1 : 1;
    int start_rank = white ? 6 : 1;
    
    // normal pawns
    while (pawns) {
        int from_sq = bitscan_forward(pawns);
        pawns = clear_lsb(pawns);
        int fr = row_of(from_sq), fc = col_of(from_sq);
        
        // single push
        int tr = fr + dir;
        if (tr >= 0 && tr < 8) {
            uint64_t single = square_bb(tr, fc);
            if (!(occupancy_all & single)) {
                add_pawn_moves(from_sq, single, white, moves);
                
                // double push
                if (fr == start_rank) {
                    int dtr = tr + dir;
                    if (dtr >= 0 && dtr < 8) {
                        uint64_t dbl = square_bb(dtr, fc);
                        if (!(occupancy_all & dbl)) add_pawn_moves(from_sq, dbl, white, moves);
                    }
                }
            }
        }
        
        // captures
        uint64_t attacks = pawn_attacks[color][from_sq];
        uint64_t caps = attacks & (white ? occupancy_black : occupancy_white);
        add_pawn_moves(from_sq, caps, white, moves);
        
        // en passant
        if (en_passant_col >= 0 && en_passant_row >= 0) {
            uint64_t ep_target = square_bb(en_passant_row, en_passant_col);
            if (attacks & ep_target) moves.emplace_back(fr, fc, en_passant_row, en_passant_col, 1);
        }
    }
    
    // absorbed-pawn ability pieces: captures + en-passant, no forward pushes (unless the piece is actually a pawn too)
    uint64_t ability_pawns = ability_bb[color][0];
    while (ability_pawns) {
        int from_sq = bitscan_forward(ability_pawns);
        ability_pawns = clear_lsb(ability_pawns);
        if (!(occupancy_all & square_bb(from_sq))) continue;
        uint64_t attacks = pawn_attacks[color][from_sq];
        uint64_t caps = attacks & (white ? occupancy_black : occupancy_white);
        add_pawn_moves(from_sq, caps, white, moves);
        if (en_passant_col >= 0 && en_passant_row >= 0) {
            if (attacks & square_bb(en_passant_row, en_passant_col)) {
                moves.emplace_back(row_of(from_sq), col_of(from_sq), en_passant_row, en_passant_col, 1);
            }
        }
    }
}

void ChessEngine::generate_knight_moves_bb(bool white, std::vector<Move>& moves) const {
    int color = white ? 0 : 1;
    uint64_t knights = piece_bb[color][1];
    while (knights) {
        int from = bitscan_forward(knights);
        knights = clear_lsb(knights);
        uint64_t atk = knight_attacks[from];
        uint64_t targets = atk & ~(white ? occupancy_white : occupancy_black);
        add_moves_from_bitboard(from, targets, moves);
    }
    uint64_t ab_knights = ability_bb[color][1];
    while (ab_knights) {
        int from = bitscan_forward(ab_knights);
        ab_knights = clear_lsb(ab_knights);
        if (!(occupancy_all & square_bb(from))) continue;
        uint64_t atk = knight_attacks[from];
        uint64_t targets = atk & ~(white ? occupancy_white : occupancy_black);
        add_moves_from_bitboard(from, targets, moves);
    }
}

void ChessEngine::generate_bishop_moves_bb(bool white, std::vector<Move>& moves) const {
    int color = white ? 0 : 1;
    uint64_t bishops = piece_bb[color][2];
    while (bishops) {
        int from = bitscan_forward(bishops);
        bishops = clear_lsb(bishops);
        uint64_t atk = get_bishop_attacks(from, occupancy_all);
        uint64_t targets = atk & ~(white ? occupancy_white : occupancy_black);
        //std::cerr << "[DEBUG][BISHOP] from sq=" << from << " (row=" << row_of(from) << ",col=" << col_of(from) << ")\n";
        //std::cerr << "  occupancy_all=0x" << std::hex << occupancy_all << ", atk=0x" << atk << ", targets=0x" << targets << std::dec << std::endl;
        add_moves_from_bitboard(from, targets, moves);
    }
    uint64_t ab_bishops = ability_bb[color][2];
    while (ab_bishops) {
        int from = bitscan_forward(ab_bishops);
        ab_bishops = clear_lsb(ab_bishops);
        if (!(occupancy_all & square_bb(from))) continue;
        uint64_t atk = get_bishop_attacks(from, occupancy_all );
        uint64_t targets = atk & ~(white ? occupancy_white : occupancy_black);
        //std::cerr << "[DEBUG] Ability bishop at sq=" << from << ": atk=0x" << std::hex << atk << ", targets=0x" << targets << std::dec << std::endl;
        add_moves_from_bitboard(from, targets, moves);
    }
}

void ChessEngine::generate_rook_moves_bb(bool white, std::vector<Move>& moves) const {
    int color = white ? 0 : 1;
    uint64_t rooks = piece_bb[color][3];
    while (rooks) {
        int from = bitscan_forward(rooks);
        rooks = clear_lsb(rooks);
        uint64_t atk = get_rook_attacks(from, occupancy_all);
        uint64_t targets = atk & ~(white ? occupancy_white : occupancy_black);
        //std::cerr << "[DEBUG][ROOK] from sq=" << from << " (row=" << row_of(from) << ",col=" << col_of(from) << ")\n";
        //std::cerr << "  occupancy_all=0x" << std::hex << occupancy_all << ", atk=0x" << atk << ", targets=0x" << targets << std::dec << std::endl;
        add_moves_from_bitboard(from, targets, moves);
    }
    uint64_t ab_rooks = ability_bb[color][3];
    while (ab_rooks) {
        int from = bitscan_forward(ab_rooks);
        ab_rooks = clear_lsb(ab_rooks);
        if (!(occupancy_all & square_bb(from))) continue;
        uint64_t atk = get_rook_attacks(from, occupancy_all);
        uint64_t targets = atk & ~(white ? occupancy_white : occupancy_black);
        add_moves_from_bitboard(from, targets, moves);
    }
}

void ChessEngine::generate_queen_moves_bb(bool white, std::vector<Move>& moves) const {
    int color = white ? 0 : 1;
    uint64_t queens = piece_bb[color][4];
    while (queens) {
        int from = bitscan_forward(queens);
        queens = clear_lsb(queens);
        uint64_t atk = get_queen_attacks(from, occupancy_all);
        uint64_t targets = atk & ~(white ? occupancy_white : occupancy_black);
        add_moves_from_bitboard(from, targets, moves);
    }
    uint64_t ab_queens = ability_bb[color][4];
    while (ab_queens) {
        int from = bitscan_forward(ab_queens);
        ab_queens = clear_lsb(ab_queens);
        if (!(occupancy_all & square_bb(from))) continue;
        uint64_t atk = get_queen_attacks(from, occupancy_all);
        uint64_t targets = atk & ~(white ? occupancy_white : occupancy_black);
        add_moves_from_bitboard(from, targets, moves);
    }
}

void ChessEngine::generate_king_moves_bb(bool white, std::vector<Move>& moves) const {
    int color = white ? 0 : 1;
    uint64_t kings = piece_bb[color][5];
    while (kings) {
        int from = bitscan_forward(kings);
        kings = clear_lsb(kings);
        uint64_t atk = king_attacks[from];
        uint64_t targets = atk & ~(white ? occupancy_white : occupancy_black);
        add_moves_from_bitboard(from, targets, moves);
    }
    uint64_t ab_kings = ability_bb[color][5];
    while (ab_kings) {
        int from = bitscan_forward(ab_kings);
        ab_kings = clear_lsb(ab_kings);
        if (!(occupancy_all & square_bb(from))) continue;
        uint64_t atk = king_attacks[from];
        uint64_t targets = atk & ~(white ? occupancy_white : occupancy_black);
        add_moves_from_bitboard(from, targets, moves);
    }
    // Castling handled separately
    generate_castling_moves_bb(white, moves);
}

void ChessEngine::generate_castling_moves_bb(bool white, std::vector<Move>& moves) const {
    // Check castling rights and occupancy / attacked squares
    if (white) {
        if (white_king_castled) return;
    } else {
        if (black_king_castled) return;
    }
    if (is_in_check(white)) return;
    
    int color = white ? 0 : 1;
    int king_row = white ? 7 : 0;
    uint64_t king_bb = piece_bb[color][5];
    if (!king_bb) return;
    int king_sq = bitscan_forward(king_bb);
    if (row_of(king_sq) != king_row || col_of(king_sq) != 4) return; // must be on e1/e8
    if (has_moved_bb[color] & square_bb(king_sq)) return;
    
    // Kingside
    uint64_t kingside_rook = piece_bb[color][3] & square_bb(king_row*8 + 7);
    if (kingside_rook && !(has_moved_bb[color] & kingside_rook)) {
        uint64_t between = square_bb(king_row, 5) | square_bb(king_row, 6);
        if (!(occupancy_all & between)) {
            if (!is_square_attacked(square(king_row, 5), !white) && !is_square_attacked(square(king_row, 6), !white)) {
                moves.emplace_back(king_row, 4, king_row, 6, 2);
            }
        }
    }
    
    // Queenside
    uint64_t queenside_rook = piece_bb[color][3] & square_bb(king_row*8 + 0);
    if (queenside_rook && !(has_moved_bb[color] & queenside_rook)) {
        uint64_t between = square_bb(king_row, 1) | square_bb(king_row, 2) | square_bb(king_row, 3);
        if (!(occupancy_all & between)) {
            if (!is_square_attacked(square(king_row, 3), !white) && !is_square_attacked(square(king_row, 2), !white)) {
                moves.emplace_back(king_row, 4, king_row, 2, 3);
            }
        }
    }
}
// ========== SLOW SLIDING ATTACKS (reference implementation) ==========
uint64_t ChessEngine::slow_rook_attacks(int sq, uint64_t blockers) const {
    int r = row_of(sq), c = col_of(sq);
    uint64_t attacks = 0ULL;
    // North
    for (int rr = r + 1; rr < 8; ++rr) {
        attacks |= square_bb(rr, c);
        if (blockers & square_bb(rr, c)) break;
    }
    // South
    for (int rr = r - 1; rr >= 0; --rr) {
        attacks |= square_bb(rr, c);
        if (blockers & square_bb(rr, c)) break;
    }
    // East
    for (int cc = c + 1; cc < 8; ++cc) {
        attacks |= square_bb(r, cc);
        if (blockers & square_bb(r, cc)) break;
    }
    // West
    for (int cc = c - 1; cc >= 0; --cc) {
        attacks |= square_bb(r, cc);
        if (blockers & square_bb(r, cc)) break;
    }
    return attacks;
}

uint64_t ChessEngine::slow_bishop_attacks(int sq, uint64_t blockers) const {
    int r = row_of(sq), c = col_of(sq);
    uint64_t attacks = 0ULL;
    // NE
    for (int rr = r + 1, cc = c + 1; rr < 8 && cc < 8; ++rr, ++cc) {
        attacks |= square_bb(rr, cc);
        if (blockers & square_bb(rr, cc)) break;
    }
    // NW
    for (int rr = r + 1, cc = c - 1; rr < 8 && cc >= 0; ++rr, --cc) {
        attacks |= square_bb(rr, cc);
        if (blockers & square_bb(rr, cc)) break;
    }
    // SE
    for (int rr = r - 1, cc = c + 1; rr >= 0 && cc < 8; --rr, ++cc) {
        attacks |= square_bb(rr, cc);
        if (blockers & square_bb(rr, cc)) break;
    }
    // SW
    for (int rr = r - 1, cc = c - 1; rr >= 0 && cc >= 0; --rr, --cc) {
        attacks |= square_bb(rr, cc);
        if (blockers & square_bb(rr, cc)) break;
    }
    return attacks;
}
// ========== FAST CHECK DETECTION USING MAGIC BITBOARDS ==========
bool ChessEngine::is_square_attacked_fast(int square, bool by_white) const {
    int attacking_color = by_white ? 0 : 1;

    // Pawn ability (including non-pawns with pawn ability)
    uint64_t pawn_attackers = ability_bb[attacking_color][0];
    uint64_t pawn_attacks_mask = pawn_attacks[by_white ? 1 : 0][square];
    if (pawn_attacks_mask & pawn_attackers) return true;

    // Knight ability
    uint64_t knight_attackers = ability_bb[attacking_color][1];
    if (knight_attacks[square] & knight_attackers) return true;

    // Bishop ability
    uint64_t bishop_attackers = ability_bb[attacking_color][2];
    uint64_t bishop_attacks_mask = get_bishop_attacks(square, occupancy_all);
    if (bishop_attacks_mask & bishop_attackers) return true;

    // Rook ability
    uint64_t rook_attackers = ability_bb[attacking_color][3];
    uint64_t rook_attacks_mask = get_rook_attacks(square, occupancy_all);
    if (rook_attacks_mask & rook_attackers) return true;

    // Queen ability
    uint64_t queen_attackers = ability_bb[attacking_color][4];
    // Queen can attack as both rook and bishop
    if ((rook_attacks_mask | bishop_attacks_mask) & queen_attackers) return true;

    // King ability
    uint64_t king_attackers = ability_bb[attacking_color][5];
    if (king_attacks[square] & king_attackers) return true;

    return false;
}

bool ChessEngine::is_in_check_fast(bool white_king) const {
    int king_color = white_king ? 0 : 1;
    uint64_t king_bb = piece_bb[king_color][5];
    if (!king_bb) return false;
    int king_square = bitscan_forward(king_bb);
    return is_square_attacked_fast(king_square, !white_king);
}

// ========== OPTIMIZED LEGAL MOVE GENERATION ==========
std::vector<Move> ChessEngine::generate_legal_moves() {
    std::vector<Move> moves;
    generate_pawn_moves_bb(white_to_move, moves);
    generate_knight_moves_bb(white_to_move, moves);
    generate_bishop_moves_bb(white_to_move, moves);
    generate_rook_moves_bb(white_to_move, moves);
    generate_queen_moves_bb(white_to_move, moves);
    generate_king_moves_bb(white_to_move, moves);
    generate_castling_moves_bb(white_to_move, moves);
    
    // Fast legality filtering - avoid vector.erase by building new vector
    std::vector<Move> legal;
    legal.reserve(moves.size()); // Pre-allocate to avoid reallocations
    
    int king_color = white_to_move ? 0 : 1;
    uint64_t king_bb = piece_bb[king_color][5];
    if (!king_bb) return legal; // No king, no legal moves
    
    int king_square = bitscan_forward(king_bb);
    bool in_check = is_in_check_fast(white_to_move);
    
    // Get checkers and pins for optimized filtering
    uint64_t checkers = get_checkers(white_to_move);
    uint64_t pinned_pieces = get_pinned_pieces(white_to_move);
    
    int num_checkers = popcount(checkers);
    
    for (const Move& m : moves) {
        if (is_legal_move_fast(m, king_square, in_check, num_checkers, checkers, pinned_pieces)) {
            legal.push_back(m);
        }
    }
    return legal;
}

bool ChessEngine::is_legal_move_fast(const Move& move, int king_square, bool in_check, 
                                    int num_checkers, uint64_t checkers, uint64_t pinned_pieces)  {
    int from_sq = move.from_row * 8 + move.from_col;
    int to_sq = move.to_row * 8 + move.to_col;
    
    // King moves - must not move to attacked square
    if (from_sq == king_square) {
        // For king moves, we need to check if destination is safe
        // If capturing a piece, temporarily remove both king and captured piece
        return is_king_capture_safe(to_sq);
    }
    
    // If not in check, only need to verify piece is not pinned or moves along pin ray
    if (!in_check) {
        if (pinned_pieces & (1ULL << from_sq)) {
            return is_move_along_pin_ray(from_sq, to_sq, king_square);
        }
        return true; // Not pinned, not in check - legal
    }
    
    // In check - different rules based on number of checkers
    if (num_checkers > 1) {
        // Double check - only king moves allowed (already handled above)
        return false;
    }
    
    // Single check - can capture checker, block, or move king
    int checker_square = bitscan_forward(checkers);
    
    // Check if move captures the checking piece
    if (to_sq == checker_square) {
        // Verify the moving piece is not pinned (unless moving along pin ray)
        if (pinned_pieces & (1ULL << from_sq)) {
            return is_move_along_pin_ray(from_sq, to_sq, king_square);
        }
        return true;
    }
    
    // Check if move blocks the check (only possible for sliding piece checks)
    if (is_sliding_check(checker_square, king_square)) {
        uint64_t block_squares = get_squares_between(checker_square, king_square);
        if (block_squares & (1ULL << to_sq)) {
            // Verify the moving piece is not pinned
            if (pinned_pieces & (1ULL << from_sq)) {
                return is_move_along_pin_ray(from_sq, to_sq, king_square);
            }
            return true;
        }
    }
    
    return false; // Move doesn't resolve check
}

uint64_t ChessEngine::get_checkers(bool white_king) const {
    int king_color = white_king ? 0 : 1;
    uint64_t king_bb = piece_bb[king_color][5];
    if (!king_bb) return 0;
    
    int king_square = bitscan_forward(king_bb);
    uint64_t checkers = 0;
    int enemy_color = white_king ? 1 : 0;
    
    // Check for pawn attacks
    uint64_t pawn_attackers = ability_bb[enemy_color][0];
    uint64_t pawn_attack_mask = pawn_attacks[white_king ? 1 : 0][king_square];
    checkers |= (pawn_attack_mask & pawn_attackers);
    
    // Check for knight attacks
    uint64_t knight_attackers = ability_bb[enemy_color][1];
    checkers |= (knight_attacks[king_square] & knight_attackers);
    
    // Check for sliding piece attacks
    uint64_t bishop_attackers = ability_bb[enemy_color][2];
    uint64_t bishop_attacks_mask = get_bishop_attacks(king_square, occupancy_all);
    checkers |= (bishop_attacks_mask & bishop_attackers);
    
    uint64_t rook_attackers = ability_bb[enemy_color][3];
    uint64_t rook_attacks_mask = get_rook_attacks(king_square, occupancy_all);
    checkers |= (rook_attacks_mask & rook_attackers);
    
    uint64_t queen_attackers = ability_bb[enemy_color][4];
    checkers |= ((rook_attacks_mask | bishop_attacks_mask) & queen_attackers);
    
    return checkers;
}

uint64_t ChessEngine::get_pinned_pieces(bool white_king) const {
    int king_color = white_king ? 0 : 1;
    uint64_t king_bb = piece_bb[king_color][5];
    if (!king_bb) return 0;
    
    int king_square = bitscan_forward(king_bb);
    uint64_t pinned = 0;
    int enemy_color = white_king ? 1 : 0;
    
    // Check for pins by sliding pieces (rooks, bishops, queens)
    uint64_t enemy_rooks = ability_bb[enemy_color][3];
    uint64_t enemy_bishops = ability_bb[enemy_color][2];
    uint64_t enemy_queens = ability_bb[enemy_color][4];
    
    // Check rook-like pins (horizontal and vertical)
    uint64_t rook_attackers = enemy_rooks | enemy_queens;
    while (rook_attackers) {
        int attacker_sq = bitscan_forward(rook_attackers);
        rook_attackers &= rook_attackers - 1; // Clear LSB
        
        if (are_aligned_rank_or_file(king_square, attacker_sq)) {
            uint64_t between = get_squares_between(king_square, attacker_sq);
            int piece_count = popcount(between & occupancy_all);
            if (piece_count == 1) {
                // Exactly one piece between - it's pinned
                pinned |= (between & occupancy_all);
            }
        }
    }
    
    // Check bishop-like pins (diagonals)
    uint64_t bishop_attackers = enemy_bishops | enemy_queens;
    while (bishop_attackers) {
        int attacker_sq = bitscan_forward(bishop_attackers);
        bishop_attackers &= bishop_attackers - 1; // Clear LSB
        
        if (are_aligned_diagonal(king_square, attacker_sq)) {
            uint64_t between = get_squares_between(king_square, attacker_sq);
            int piece_count = popcount(between & occupancy_all);
            if (piece_count == 1) {
                // Exactly one piece between - it's pinned
                pinned |= (between & occupancy_all);
            }
        }
    }
    
    return pinned & (white_king ? occupancy_white : occupancy_black); // Only our pieces can be pinned
}

std::vector<Move> ChessEngine::generate_legal_moves() const {
    return const_cast<ChessEngine*>(this)->generate_legal_moves();
}

std::vector<Move> ChessEngine::generate_capture_moves() {
    std::vector<Move> moves;
    generate_pawn_moves_bb(white_to_move, moves);
    generate_knight_moves_bb(white_to_move, moves);
    generate_bishop_moves_bb(white_to_move, moves);
    generate_rook_moves_bb(white_to_move, moves);
    generate_queen_moves_bb(white_to_move, moves);
    generate_king_moves_bb(white_to_move, moves);
    
    // Filter only capture moves
    std::vector<Move> captures;
    for (const Move& m : moves) {
        if (get_piece_at_square(m.to_row, m.to_col) != EMPTY) {
            bool original_turn = white_to_move;
            MoveUndoBB undo = apply_move_bb(m);
            if (!is_in_check_fast(original_turn)) captures.push_back(m);
            undo_move_bb(m, undo);
        }
    }
    return captures;
}

int ChessEngine::get_piece_value(int piece) const {
    switch (piece & PIECE_MASK) {
        case PAWN: return 100;
        case KNIGHT: return 300;
        case BISHOP: return 300;
        case ROOK: return 500;
        case QUEEN: return 900;
        case KING: return 0; // King cannot be captured
        default: return 0;
    }
}

bool ChessEngine::is_king_move_safe(int to_square) {
    // Check if to_square is attacked by enemy pieces
    // We need to temporarily remove our king from occupancy to check if the square is safe
    
    int king_color = white_to_move ? 0 : 1;
    uint64_t king_bb = piece_bb[king_color][5];
    if (!king_bb) return false; // No king
    
    int king_sq = bitscan_forward(king_bb);
    
    // Temporarily remove king and update occupancy
    piece_bb[king_color][5] = 0;
    update_occupancy();
    
    bool safe = !is_square_attacked_fast(to_square, !white_to_move);
    
    // Restore king and update occupancy
    piece_bb[king_color][5] = king_bb;
    update_occupancy();
    
    return safe;
}

bool ChessEngine::is_king_capture_safe(int to_square) {
    // Check if king can safely move to to_square, accounting for captured piece
    
    int king_color = white_to_move ? 0 : 1;
    uint64_t king_bb = piece_bb[king_color][5];
    if (!king_bb) return false; // No king
    
    // Check if there's a piece being captured
    int to_row = row_of(to_square);
    int to_col = col_of(to_square);
    uint32_t captured_piece = get_piece_at_square(to_row, to_col);
    
    // Temporarily remove king from current position
    piece_bb[king_color][5] = 0;
    
    // If capturing a piece, temporarily remove it too
    uint64_t* captured_piece_bb = nullptr;
    uint64_t original_captured_bb = 0;
    
    if (captured_piece != EMPTY) {
        int captured_color = (captured_piece & IS_WHITE) ? 0 : 1;
        int captured_type = -1;
        
        // Find which piece type is being captured
        for (int piece_type = 0; piece_type < 6; piece_type++) {
            uint64_t test_bb = piece_bb[captured_color][piece_type];
            if (test_bb & (1ULL << to_square)) {
                captured_piece_bb = &piece_bb[captured_color][piece_type];
                original_captured_bb = test_bb;
                *captured_piece_bb &= ~(1ULL << to_square); // Remove captured piece
                break;
            }
        }
    }
    
    update_occupancy();
    
    bool safe = !is_square_attacked_fast(to_square, !white_to_move);
    
    // Restore king
    piece_bb[king_color][5] = king_bb;
    
    // Restore captured piece if there was one
    if (captured_piece_bb) {
        *captured_piece_bb = original_captured_bb;
    }
    
    update_occupancy();
    
    return safe;
}

bool ChessEngine::is_move_along_pin_ray(int from_sq, int to_sq, int king_square) const {
    // Check if the move is along the same line as the pin
    if (are_aligned_rank_or_file(from_sq, king_square)) {
        return are_aligned_rank_or_file(to_sq, king_square) && 
               are_aligned_rank_or_file(from_sq, to_sq);
    } else if (are_aligned_diagonal(from_sq, king_square)) {
        return are_aligned_diagonal(to_sq, king_square) && 
               are_aligned_diagonal(from_sq, to_sq);
    }
    return false;
}

bool ChessEngine::is_sliding_check(int checker_square, int king_square) const {
    int checker_row = row_of(checker_square);
    int checker_col = col_of(checker_square);
    int checker_piece = get_piece_at_square(checker_row, checker_col) & PIECE_MASK;
    return (checker_piece == PIECE_ROOK || checker_piece == PIECE_BISHOP || checker_piece == PIECE_QUEEN);
}

uint64_t ChessEngine::get_squares_between(int sq1, int sq2) const {
    if (sq1 == sq2) return 0;
    
    int r1 = row_of(sq1), c1 = col_of(sq1);
    int r2 = row_of(sq2), c2 = col_of(sq2);
    
    uint64_t between = 0;
    
    // Determine direction
    int dr = (r2 > r1) ? 1 : (r2 < r1) ? -1 : 0;
    int dc = (c2 > c1) ? 1 : (c2 < c1) ? -1 : 0;
    
    // Make sure they are aligned
    if (dr != 0 && dc != 0 && abs(r2 - r1) != abs(c2 - c1)) return 0; // Not diagonal
    if (dr == 0 && dc == 0) return 0; // Same square
    if (dr != 0 && dc != 0 && dr * dc == 0) return 0; // Not aligned
    
    int r = r1 + dr, c = c1 + dc;
    while (r != r2 || c != c2) {
        if (r >= 0 && r < 8 && c >= 0 && c < 8) {
            between |= (1ULL << (r * 8 + c));
        }
        r += dr;
        c += dc;
    }
    
    return between;
}

bool ChessEngine::are_aligned_rank_or_file(int sq1, int sq2) const {
    return (row_of(sq1) == row_of(sq2)) || (col_of(sq1) == col_of(sq2));
}

bool ChessEngine::are_aligned_diagonal(int sq1, int sq2) const {
    int dr = abs(row_of(sq1) - row_of(sq2));
    int dc = abs(col_of(sq1) - col_of(sq2));
    return dr == dc && dr > 0;
}

uint32_t ChessEngine::get_piece_at_square(int row, int col) const {
    int square = row * 8 + col;
    uint64_t mask = 1ULL << square;
    
    // Check white pieces
    for (int piece = 0; piece < 6; piece++) {
        if (piece_bb[0][piece] & mask) {
            uint32_t result = 0;
            switch (piece) {
                case 0: result |= PIECE_PAWN; break;
                case 1: result |= PIECE_KNIGHT; break;
                case 2: result |= PIECE_BISHOP; break;
                case 3: result |= PIECE_ROOK; break;
                case 4: result |= PIECE_QUEEN; break;
                case 5: result |= PIECE_KING; break;
            }
            result |= IS_WHITE;
            if (has_moved_bb[0] & mask) result |= HAS_MOVED;
            
            // Add abilities
            for (int ability = 0; ability < 6; ability++) {
                if (ability_bb[0][ability] & mask) {
                    switch (ability) {
                        case 0: result |= ABILITY_PAWN; break;
                        case 1: result |= ABILITY_KNIGHT; break;
                        case 2: result |= ABILITY_BISHOP; break;
                        case 3: result |= ABILITY_ROOK; break;
                        case 4: result |= ABILITY_QUEEN; break;
                        case 5: result |= ABILITY_KING; break;
                    }
                }
            }
            return result;
        }
    }
    
    // Check black pieces
    for (int piece = 0; piece < 6; piece++) {
        if (piece_bb[1][piece] & mask) {
            uint32_t result = 0;
            switch (piece) {
                case 0: result |= PIECE_PAWN; break;
                case 1: result |= PIECE_KNIGHT; break;
                case 2: result |= PIECE_BISHOP; break;
                case 3: result |= PIECE_ROOK; break;
                case 4: result |= PIECE_QUEEN; break;
                case 5: result |= PIECE_KING; break;
            }
            // Black pieces don't have IS_WHITE flag
            if (has_moved_bb[1] & mask) result |= HAS_MOVED;
            
            // Add abilities
            for (int ability = 0; ability < 6; ability++) {
                if (ability_bb[1][ability] & mask) {
                    switch (ability) {
                        case 0: result |= ABILITY_PAWN; break;
                        case 1: result |= ABILITY_KNIGHT; break;
                        case 2: result |= ABILITY_BISHOP; break;
                        case 3: result |= ABILITY_ROOK; break;
                        case 4: result |= ABILITY_QUEEN; break;
                        case 5: result |= ABILITY_KING; break;
                    }
                }
            }
            return result;
        }
    }
    
    return EMPTY; // No piece at this square
}

// ========== EVALUATION HELPERS ==========

int ChessEngine::calculate_piece_ability_value(uint32_t piece, uint32_t abilities) const {
    if (piece == 0) return 0;
    int total_value = 0;
    bool has_rook_ability = false;
    bool has_bishop_ability = false;
    bool has_queen_ability = false;
    
    // Check what abilities this piece has
    if ((piece & PIECE_ROOK) || (abilities & ABILITY_ROOK)) has_rook_ability = true;
    if ((piece & PIECE_BISHOP) || (abilities & ABILITY_BISHOP)) has_bishop_ability = true;
    if ((piece & PIECE_QUEEN) || (abilities & ABILITY_QUEEN)) has_queen_ability = true;
    
    // Base piece value (primary type)
    if (piece & PIECE_PAWN) total_value += 100;
    else if (piece & PIECE_KNIGHT) total_value += 300;
    else if (piece & PIECE_BISHOP) total_value += 300;
    else if (piece & PIECE_ROOK) total_value += 500;
    else if (piece & PIECE_QUEEN) total_value += 900;
    else if (piece & PIECE_KING) total_value += 10000;
    
    // Add unique ability values (avoid duplicates)
    if (has_queen_ability) {
        // Queen already includes rook and bishop, so add queen value only
        if (!(piece & PIECE_QUEEN)) {
            total_value += 900; // Bonus for gaining queen ability
            if ((abilities & ABILITY_ROOK)) {
                total_value -= 500;
            }
            if ((abilities & ABILITY_BISHOP)) {
                total_value -= 300;
            }
        }
    } else {
        // Add individual abilities if no queen ability
        if (has_rook_ability && !(piece & PIECE_ROOK) && !(abilities & ABILITY_QUEEN)) {
            total_value += 500;
        }
        if (has_bishop_ability && !(piece & PIECE_BISHOP) && !(abilities & ABILITY_QUEEN)) {
            total_value += 300;
        }
    }
    
    // Other abilities
    if (abilities & ABILITY_KNIGHT && !(piece & PIECE_KNIGHT)) {
        total_value += 300;
    }
    if (abilities & ABILITY_PAWN && !(piece & PIECE_PAWN)) {
        if (abilities & (ABILITY_QUEEN) || (has_bishop_ability && has_rook_ability)) {
            total_value += 10;
        } else {
            total_value += 100;
        }
    }
    return total_value;
}


int ChessEngine::calculate_piece_ability_value_bb(int square, bool white) const {
    int color = white ? 0 : 1;
    uint64_t sq_bb = square_bb(square);
    uint32_t piece = 0, abilities = 0;
    for (int pt = 0; pt < 6; ++pt) {
        if (piece_bb[color][pt] & sq_bb) {
            switch (pt) {
                case 0: piece |= PIECE_PAWN; break;
                case 1: piece |= PIECE_KNIGHT; break;
                case 2: piece |= PIECE_BISHOP; break;
                case 3: piece |= PIECE_ROOK; break;
                case 4: piece |= PIECE_QUEEN; break;
                case 5: piece |= PIECE_KING; break;
            }
        }
    }
    for (int at = 0; at < 6; ++at) {
        if (ability_bb[color][at] & sq_bb) {
            switch (at) {
                case 0: abilities |= ABILITY_PAWN; break;
                case 1: abilities |= ABILITY_KNIGHT; break;
                case 2: abilities |= ABILITY_BISHOP; break;
                case 3: abilities |= ABILITY_ROOK; break;
                case 4: abilities |= ABILITY_QUEEN; break;
                case 5: abilities |= ABILITY_KING; break;
            }
        }
    }
    return calculate_piece_ability_value(piece, abilities);
}

int ChessEngine::evaluate_material_bb() const {
    int score = 0;
    // iterate occupancy (both colors) and add/sub values
    for (int sq = 0; sq < 64; ++sq) {
        uint64_t bb = square_bb(sq);
        if (occupancy_white & bb) score += calculate_piece_ability_value_bb(sq, true);
        if (occupancy_black & bb) score -= calculate_piece_ability_value_bb(sq, false);
    }
    return score;
}

int ChessEngine::evaluate_mobility_bb() const {
    uint64_t w_atk = get_all_attacks(true);
    uint64_t b_atk = get_all_attacks(false);
    int w_mob = popcount(w_atk & ~occupancy_white);
    int b_mob = popcount(b_atk & ~occupancy_black);
    
    int mobility_score = (w_mob - b_mob) * 5;
    
    // Enhanced development evaluation
    int development_panelty = evaluate_development();
    
    return mobility_score + development_panelty;
}
int ChessEngine::evaluate_development() const {
    int development_delta = 0;

    int white_developed = count_developed_pieces(true);
    int black_developed = count_developed_pieces(false);
    int development_diff = white_developed - black_developed;

    // Positive -> white is more developed; negative -> black more developed.
    // We want to reward development for white (so positive * +) and penalize for white lagging.
    development_delta += development_diff * 25; // 25 points per development difference

    // White queen early penalty: we subtract (bad for white)
    uint64_t white_queen = piece_bb[0][4];
    if (white_queen) {
        int queen_sq = bitscan_forward(white_queen);
        int queen_row = row_of(queen_sq);
        if (queen_row != 7) {
            uint64_t white_knights = piece_bb[0][1];
            uint64_t white_bishops = piece_bb[0][2];
            int undeveloped_minors = 0;
            if (white_knights & (1ULL << 57)) undeveloped_minors++;
            if (white_knights & (1ULL << 62)) undeveloped_minors++;
            if (white_bishops & (1ULL << 58)) undeveloped_minors++;
            if (white_bishops & (1ULL << 61)) undeveloped_minors++;
            development_delta -= undeveloped_minors * 40; // subtract for white (penalty)
            if (queen_row >= 4 && queen_row <= 5) development_delta -= 30;
        }
    }

    // Black queen early penalty: this should affect evaluation with opposite sign.
    uint64_t black_queen = piece_bb[1][4];
    if (black_queen) {
        int queen_sq = bitscan_forward(black_queen);
        int queen_row = row_of(queen_sq);
        if (queen_row != 0) {
            uint64_t black_knights = piece_bb[1][1];
            uint64_t black_bishops = piece_bb[1][2];
            int undeveloped_minors = 0;
            if (black_knights & (1ULL << 1)) undeveloped_minors++;
            if (black_knights & (1ULL << 6)) undeveloped_minors++;
            if (black_bishops & (1ULL << 2)) undeveloped_minors++;
            if (black_bishops & (1ULL << 5)) undeveloped_minors++;
            development_delta += undeveloped_minors * 40; // add (good for white if black is undeveloped)
            if (queen_row >= 2 && queen_row <= 3) development_delta += 30;
        }
    }


    return development_delta;
}


int ChessEngine::count_developed_pieces(bool white) const {
    int developed = 0;
    int color = white ? 0 : 1;
    
    // Count developed knights (not on starting squares)
    uint64_t knights = piece_bb[color][1];
    if (white) {
        // White knights start on b1(57) and g1(62)
        if (knights & ~((1ULL << 57) | (1ULL << 62))) {
            developed += popcount(knights & ~((1ULL << 57) | (1ULL << 62)));
        }
    } else {
        // Black knights start on b8(1) and g8(6)
        if (knights & ~((1ULL << 1) | (1ULL << 6))) {
            developed += popcount(knights & ~((1ULL << 1) | (1ULL << 6)));
        }
    }
    
    // Count developed bishops (not on starting squares)
    uint64_t bishops = piece_bb[color][2];
    if (white) {
        // White bishops start on c1(58) and f1(61)
        if (bishops & ~((1ULL << 58) | (1ULL << 61))) {
            developed += popcount(bishops & ~((1ULL << 58) | (1ULL << 61)));
        }
    } else {
        // Black bishops start on c8(2) and f8(5)
        if (bishops & ~((1ULL << 2) | (1ULL << 5))) {
            developed += popcount(bishops & ~((1ULL << 2) | (1ULL << 5)));
        }
    }
    
    // Bonus for castling (completed development)
    if (white && white_king_castled) developed += 2;
    if (!white && black_king_castled) developed += 2;
    
    return developed;
}

int ChessEngine::evaluate_king_safety_bb() const {
    int score = 0;
    if (is_in_check(true)) score -= 100;
    if (is_in_check(false)) score += 100;
    if (white_king_castled) score -= 50;
    if (black_king_castled) score += 50;
    
    uint64_t wk = piece_bb[0][5];
    if (wk) {
        int ks = bitscan_forward(wk);
        int kr = row_of(ks), kc = col_of(ks);
        if (kr != 7) score -= 30;
        if (kc >= 3 && kc <= 4) score -= 20;
        score += calculate_piece_ability_value_bb(ks, true);
    }
    uint64_t bk = piece_bb[1][5];
    if (bk) {
        int ks = bitscan_forward(bk);
        int kr = row_of(ks), kc = col_of(ks);
        if (kr != 0) score += 30;
        if (kc >= 3 && kc <= 4) score += 20;
        score -= calculate_piece_ability_value_bb(ks, false);
    }
    return score;
}

int ChessEngine::evaluate_position() const {
    if (!eval_cache_valid) {
        cached_material_eval = evaluate_material_bb();
        cached_mobility_eval = evaluate_mobility_bb();
        cached_king_safety_eval = evaluate_king_safety_bb();
        eval_cache_valid = true;
    }
    return cached_material_eval + cached_mobility_eval + cached_king_safety_eval;
}

int ChessEngine::get_evaluation() {
    return evaluate_position();
}

// ========== MOVE APPLICATION (bitboard) ==========
ChessEngine::MoveUndoBB ChessEngine::apply_move_bb(const Move& move) {
    MoveUndoBB undo;
    
    // Save everything required to undo
    for (int color = 0; color < 2; ++color) {
        for (int p = 0; p < 6; ++p) {
            undo.captured_piece_bb[color][p] = piece_bb[color][p];
            undo.captured_ability_bb[color][p] = ability_bb[color][p];
        }
        undo.old_has_moved[color] = has_moved_bb[color];
    }
    undo.old_white_castled = white_king_castled;
    undo.old_black_castled = black_king_castled;
    undo.old_en_passant_col = en_passant_col;
    undo.old_en_passant_row = en_passant_row;
    undo.old_eval_cache_valid = eval_cache_valid;
    undo.old_material_eval = cached_material_eval;
    undo.old_king_safety_eval = cached_king_safety_eval;
    undo.old_mobility_eval = cached_mobility_eval;
    
    // Basic move application
    int from_sq = square(move.from_row, move.from_col);
    int to_sq = square(move.to_row, move.to_col);
    uint64_t from_bb = square_bb(from_sq);
    uint64_t to_bb = square_bb(to_sq);
    int color = white_to_move ? 0 : 1;
    int enemy = 1 - color;
    
    // Find moving piece type (first match)
    int moving_pt = -1;
    for (int pt = 0; pt < 6; ++pt) {
        if (piece_bb[color][pt] & from_bb) { moving_pt = pt; break; }
    }
    // If nothing found, treat it as invalid (but we'll try to proceed)
    if (moving_pt >= 0) {
        piece_bb[color][moving_pt] &= ~from_bb;
        piece_bb[color][moving_pt] |= to_bb;
    }
    
    // Handle captures on destination
    for (int pt = 0; pt < 6; ++pt) {
        if (piece_bb[enemy][pt] & to_bb) {
            piece_bb[enemy][pt] &= ~to_bb;
            break;
        }
    }
    
    // Move abilities from source to dest (if present)
    for (int at = 0; at < 6; ++at) {
        if (ability_bb[color][at] & from_bb) {
            ability_bb[color][at] &= ~from_bb;
            ability_bb[color][at] |= to_bb;
        }
        // Remove enemy abilities captured on destination
        ability_bb[enemy][at] &= ~to_bb;
    }
    
    // Set has_moved for moved piece
    has_moved_bb[color] |= to_bb;
    
    // Handle special flags
    if (move.flags == 1) {
        // En passant: remove the pawn that was bypassed
        int captured_row = undo.old_en_passant_row;
        int captured_col = undo.old_en_passant_col;
        // If en passant coordinates are valid, remove pawn there
        if (captured_row >= 0 && captured_col >= 0) {
            int cap_sq = square(captured_row, captured_col);
            uint64_t cap_bb = square_bb(cap_sq);
            piece_bb[enemy][0] &= ~cap_bb;
            for (int at = 0; at < 6; ++at) ability_bb[enemy][at] &= ~cap_bb;
        } else {
            // fallback: if the moving pawn moved diagonally and destination empty (ep), remove pawn behind
            if (!(undo.captured_piece_bb[enemy][0] & to_bb)) {
                int cap_r = move.from_row;
                int cap_c = move.to_col;
                int cap_sq = square(cap_r, cap_c);
                uint64_t cap_bb = square_bb(cap_sq);
                piece_bb[enemy][0] &= ~cap_bb;
                for (int at = 0; at < 6; ++at) ability_bb[enemy][at] &= ~cap_bb;
            }
        }
    } else if (move.flags == 2) {
        // Kingside castle: rook moves from h-file to f-file
        uint64_t rook_from = square_bb(move.from_row, 7);
        uint64_t rook_to   = square_bb(move.from_row, 5);
        piece_bb[color][3] &= ~rook_from;
        piece_bb[color][3] |= rook_to;
        has_moved_bb[color] |= rook_to;
        if (white_to_move) white_king_castled = true; else black_king_castled = true;
    } else if (move.flags == 3) {
        // Queenside castle: rook moves from a-file to d-file
        uint64_t rook_from = square_bb(move.from_row, 0);
        uint64_t rook_to   = square_bb(move.from_row, 3);
        piece_bb[color][3] &= ~rook_from;
        piece_bb[color][3] |= rook_to;
        has_moved_bb[color] |= rook_to;
        if (white_to_move) white_king_castled = true; else black_king_castled = true;
    } else if (move.flags >= 4 && move.flags <= 7) {
        // Promotion: after the pawn has been moved to destination, replace it by promoted piece
        // First remove pawn at destination
        piece_bb[color][0] &= ~to_bb;
        // Map flags: 4=queen,5=rook,6=bishop,7=knight
        switch (move.flags) {
            case 4: piece_bb[color][4] |= to_bb; break; // queen
            case 5: piece_bb[color][3] |= to_bb; break; // rook
            case 6: piece_bb[color][2] |= to_bb; break; // bishop
            case 7: piece_bb[color][1] |= to_bb; break; // knight
            default: break;
        }
    }

    // Update en passant: if moved double pawn, set en passant target; else clear
    en_passant_col = en_passant_row = -1;
    if (moving_pt == 0 && abs(move.to_row - move.from_row) == 2) {
        en_passant_col = move.to_col;
        en_passant_row = (move.from_row + move.to_row) / 2;
    }
    
    // Toggle side
    white_to_move = !white_to_move;
    
    // Update occupancy and invalidate eval cache
    update_occupancy();
    eval_cache_valid = false;
    
    return undo;
}

void ChessEngine::undo_move_bb(const Move& move, const MoveUndoBB& undo) {
    // Restore everything
    for (int color = 0; color < 2; ++color) {
        for (int p = 0; p < 6; ++p) {
            piece_bb[color][p] = undo.captured_piece_bb[color][p];
            ability_bb[color][p] = undo.captured_ability_bb[color][p];
        }
        has_moved_bb[color] = undo.old_has_moved[color];
    }
    white_king_castled = undo.old_white_castled;
    black_king_castled = undo.old_black_castled;
    en_passant_col = undo.old_en_passant_col;
    en_passant_row = undo.old_en_passant_row;
    eval_cache_valid = undo.old_eval_cache_valid;
    cached_material_eval = undo.old_material_eval;
    cached_king_safety_eval = undo.old_king_safety_eval;
    cached_mobility_eval = undo.old_mobility_eval;
    
    // toggle side back
    white_to_move = !white_to_move;
    update_occupancy();
}

// ========== LEGACY API ==========
ChessEngine::MoveUndo ChessEngine::apply_move(const Move& move) {
    MoveUndoBB bb_undo = apply_move_bb(move);
    MoveUndo legacy;
    // Not fully implemented: if you need exact legacy undo details, I'll fill them (requires mapping)
    legacy.old_en_passant_valid = (bb_undo.old_en_passant_col >= 0 && bb_undo.old_en_passant_row >= 0);
    legacy.old_en_passant_col = bb_undo.old_en_passant_col;
    legacy.old_en_passant_row = bb_undo.old_en_passant_row;
    legacy.old_white_castled = bb_undo.old_white_castled;
    legacy.old_black_castled = bb_undo.old_black_castled;
    return legacy;
}

void ChessEngine::undo_move(const Move& move, const MoveUndo& undo_info) {
    // No-op wrapper currently (we require the bitboard undo to really restore)
    // If needed, convert 'undo_info' to MoveUndoBB; but for now legacy API users should not call this.
}

// ========== SEARCH ==========
int ChessEngine::minimax_bb(int depth, int alpha, int beta, bool maximizing) {
    nodes_searched++;
    if (depth == 0) return quiescence_search_bb(alpha, beta);
    
    std::vector<Move> moves = generate_legal_moves();
    if (moves.empty()) {
        if (is_in_check(white_to_move)) {
            return maximizing ? -30000 + (5 - depth) : 30000 - (5 - depth);
        } else {
            return 0;
        }
    }
    
    int current_eval = evaluate_position();
    
    if (maximizing) {
        int max_eval = INT_MIN;
        for (const Move& m : moves) {
            MoveUndoBB undo = apply_move_bb(m);
            int eval;
            if (depth > 1) eval = minimax_bb(depth - 1, alpha, beta, false);
            else eval = evaluate_position();
            undo_move_bb(m, undo);
            max_eval = std::max(max_eval, eval);
            alpha = std::max(alpha, eval);
            if (beta <= alpha) break;
        }
        return max_eval;
    } else {
        int min_eval = INT_MAX;
        for (const Move& m : moves) {
            MoveUndoBB undo = apply_move_bb(m);
            int eval;
            if (depth > 1) eval = minimax_bb(depth - 1, alpha, beta, true);
            else eval = evaluate_position();
            undo_move_bb(m, undo);
            min_eval = std::min(min_eval, eval);
            beta = std::min(beta, eval);
            if (beta <= alpha) break;
        }
        return min_eval;
    }
}

int ChessEngine::quiescence_search_bb(int alpha, int beta) {
    quiescence_nodes++;
    int stand_pat = evaluate_position();
    
    // Fail-high cutoff
    if (stand_pat >= beta) return beta;
    
    // Update alpha if stand-pat is better
    if (alpha < stand_pat) alpha = stand_pat;

    // Generate only capture moves for quiescence search
    std::vector<Move> captures = generate_capture_moves();
    
    for (const Move& cap : captures) {
        // Delta pruning: if even capturing the most valuable piece won't improve alpha enough
        int captured_value = get_piece_value(get_piece_at_square(cap.to_row, cap.to_col));
        if (stand_pat + captured_value + 200 < alpha) continue; // 200 margin for safety
        
        MoveUndoBB undo = apply_move_bb(cap);
        int score = -quiescence_search_bb(-beta, -alpha);
        undo_move_bb(cap, undo);
        
        if (score >= beta) return beta; // Beta cutoff
        if (score > alpha) alpha = score; // Update alpha
    }
    
    return alpha;
}

std::pair<Move,int> ChessEngine::get_best_move(int depth) {
    Move invalid(255,255,255,255,0);
    int best_score = white_to_move ? INT_MIN : INT_MAX;
    std::vector<Move> moves = generate_legal_moves();
    if (moves.empty()) return {invalid, best_score};
    Move best = moves[0];
    for (const Move& m : moves) {
        MoveUndoBB undo = apply_move_bb(m);
        int score = minimax_bb(depth - 1, INT_MIN, INT_MAX, white_to_move);
        undo_move_bb(m, undo);
        if ((white_to_move && score > best_score) || (!white_to_move && score < best_score)) {
            best_score = score; 
            best = m; // Keep original flags including promotion
        }
    }
    return {best, best_score};
}

std::vector<Move> ChessEngine::get_legal_moves() { return generate_legal_moves(); }

std::tuple<std::vector<std::vector<uint32_t>>, bool, bool, bool, int, int> ChessEngine::get_board_state() {
    return std::make_tuple(convert_to_legacy_board(), white_to_move, white_king_castled, black_king_castled, en_passant_col, en_passant_row);
}

std::vector<std::vector<uint32_t>> ChessEngine::get_board_state() const {
    return convert_to_legacy_board();
}

bool ChessEngine::is_valid_move(int fr, int fc, int tr, int tc) {
    auto moves = generate_legal_moves();
    for (auto &m : moves) if (m.from_row == fr && m.from_col == fc && m.to_row == tr && m.to_col == tc) return true;
    return false;
}

std::pair<uint32_t, uint32_t> ChessEngine::get_piece_at(int row, int col) {
    int sq = square(row, col);
    uint64_t sq_bb = square_bb(sq);
    for (int color = 0; color < 2; ++color) {
        for (int p = 0; p < 6; ++p) {
            if (piece_bb[color][p] & sq_bb) {
                uint32_t piece_code = (p + 1) | (color == 1 ? 0x80 : 0x00);
                uint32_t abilities = 0;
                for (int a = 0; a < 6; ++a) if (ability_bb[color][a] & sq_bb) abilities |= (1u << a);
                return {piece_code, abilities};
            }
        }
    }
    return {0,0};
}

void ChessEngine::print_board() {
    auto board = convert_to_legacy_board();
    std::cout << "  a b c d e f g h\n";
    for (int r = 0; r < 8; ++r) {
        std::cout << (8 - r) << " ";
        for (int c = 0; c < 8; ++c) {
            uint32_t p = board[r][c];
            char ch = '.';
            if (p != 0) {
                bool is_black = !(p & IS_WHITE);
                uint32_t type = p & (PIECE_PAWN | PIECE_KNIGHT | PIECE_BISHOP | PIECE_ROOK | PIECE_QUEEN | PIECE_KING);
                if (type & PIECE_PAWN) ch = is_black ? 'p' : 'P';
                else if (type & PIECE_KNIGHT) ch = is_black ? 'n' : 'N';
                else if (type & PIECE_BISHOP) ch = is_black ? 'b' : 'B';
                else if (type & PIECE_ROOK) ch = is_black ? 'r' : 'R';
                else if (type & PIECE_QUEEN) ch = is_black ? 'q' : 'Q';
                else if (type & PIECE_KING) ch = is_black ? 'k' : 'K';
            }
            std::cout << ch << " ";
        }
        std::cout << " " << (8 - r) << "\n";
    }
    std::cout << "  a b c d e f g h\n";
}

int ChessEngine::performance_test(int depth) {
    nodes_searched = 0;
    auto start = std::chrono::high_resolution_clock::now();
    uint64_t nodes = perft(depth);
    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "Perft depth " << depth << ": " << nodes << " nodes in " << ms << " ms\n";
    return (int)nodes;
}

// Helper to initialize tables if caller wants to explicitly call it
void init_chess_engine__tables() {
    if (!ChessEngine::tables_initialized) {
        ChessEngine tmp;
    }
}

// ========== PUBLIC API BRIDGE ==========
void ChessEngine::set_board_state(const std::vector<std::vector<uint32_t>>& board,
                        bool white_to_move_in, bool white_castled_in, bool black_castled_in,
                        int en_passant_col_in, int en_passant_row_in) {
    update_from_legacy_board(board);
    white_to_move = white_to_move_in;
    white_king_castled = white_castled_in;
    black_king_castled = black_castled_in;
    en_passant_col = en_passant_col_in;
    en_passant_row = en_passant_row_in;
    update_occupancy();
    eval_cache_valid = false;
    
    
}

std::string ChessEngine::bitboard_to_string(uint64_t bb) {
    std::string out;
    for (int r = 7; r >= 0; --r) {
        for (int c = 0; c < 8; ++c) {
            int sq = r*8 + c; // LSB=a1
            out += (bb & (1ULL << sq)) ? 'X' : '.';
        }
        out += '\n';
    }
    return out;
}
void ChessEngine::verify_magic_tables() {
    init_magic_bitboards(); // make sure tables are built

    for (int sq = 0; sq < 64; ++sq) {
        // ===== ROOK =====
        {
            uint64_t mask = rook_table.masks[sq];
            int bits = popcount(mask);
            int table_size = 1 << bits;

            for (int i = 0; i < table_size; ++i) {
                uint64_t blockers = 0ULL;
                int bit_pos = 0;
                for (uint64_t bb = mask; bb; bb = clear_lsb(bb)) {
                    int b = bitscan_forward(bb);
                    if (i & (1 << bit_pos)) blockers |= (1ULL << b);
                    bit_pos++;
                }

                uint64_t slow = slow_rook_attacks(sq, blockers);
                uint64_t table_val = rook_table.attacks_ptr[sq][i];

                if (slow != table_val) {
                    std::cerr << "ROOK mismatch at square " << sq
                              << " (row=" << row_of(sq) << ", col=" << col_of(sq) << ")\n";
                    std::cerr << "mask:\n" << bitboard_to_string(mask);
                    std::cerr << "blockers:\n" << bitboard_to_string(blockers);
                    std::cerr << "slow:\n" << bitboard_to_string(slow);
                    std::cerr << "table_val:\n" << bitboard_to_string(table_val);
                    return;
                }
            }
        }

        // ===== BISHOP =====
        {
            uint64_t mask = bishop_table.masks[sq];
            int bits = popcount(mask);
            int table_size = 1 << bits;

            for (int i = 0; i < table_size; ++i) {
                uint64_t blockers = 0ULL;
                int bit_pos = 0;
                for (uint64_t bb = mask; bb; bb = clear_lsb(bb)) {
                    int b = bitscan_forward(bb);
                    if (i & (1 << bit_pos)) blockers |= (1ULL << b);
                    bit_pos++;
                }

                uint64_t slow = slow_bishop_attacks(sq, blockers);
                uint64_t table_val = bishop_table.attacks_ptr[sq][i];

                if (slow != table_val) {
                    std::cerr << "BISHOP mismatch at square " << sq
                              << " (row=" << row_of(sq) << ", col=" << col_of(sq) << ")\n";
                    std::cerr << "mask:\n" << bitboard_to_string(mask);
                    std::cerr << "blockers:\n" << bitboard_to_string(blockers);
                    std::cerr << "slow:\n" << bitboard_to_string(slow);
                    std::cerr << "table_val:\n" << bitboard_to_string(table_val);
                    return;
                }
            }
        }
    }

    std::cerr << "verify_dynamic_tables: all good (no mismatches found)\n";
}


void ChessEngine::dump_square_indices() {
    std::cerr << "square indices (top row -> bottom row):\n";
    for (int r = 7; r >= 0; --r) {
        for (int c = 0; c < 8; ++c) {
            std::cerr << std::setw(3) << square(r, c);
        }
        std::cerr << '\n';
    }
}

void ChessEngine::quick_mapping_test() {
    std::cerr << "single-bit tests (should show bottom-left for sq0):\n";
    std::cerr << "sq 0:\n" << bitboard_to_string(1ULL << 0) << '\n';
    std::cerr << "sq 1:\n" << bitboard_to_string(1ULL << 1) << '\n';
    std::cerr << "sq 8:\n" << bitboard_to_string(1ULL << 8) << '\n';
    std::cerr << "sq 56:\n" << bitboard_to_string(1ULL << 56) << '\n';
}

// generate canonical masks using your square(), row_of(), col_of()
uint64_t ChessEngine::gen_rook_mask_local(int sq) {
    int r = row_of(sq), c = col_of(sq);
    uint64_t mask = 0ULL;

    for (int i = r+1; i < 7; ++i) mask |= 1ULL << square(i, c);
    for (int i = r-1; i > 0; --i) mask |= 1ULL << square(i, c);
    for (int i = c+1; i < 7; ++i) mask |= 1ULL << square(r, i);
    for (int i = c-1; i > 0; --i) mask |= 1ULL << square(r, i);

    return mask;
}

uint64_t ChessEngine::gen_bishop_mask_local(int sq) {
    int r = row_of(sq), c = col_of(sq);
    uint64_t mask = 0ULL;

    for (int dr = 1, dc = 1; r+dr < 7 && c+dc < 7; ++dr, ++dc) mask |= 1ULL << square(r+dr, c+dc);
    for (int dr = 1, dc = 1; r+dr < 7 && c-dc > 0; ++dr, ++dc) mask |= 1ULL << square(r+dr, c-dc);
    for (int dr = 1, dc = 1; r-dr > 0 && c+dc < 7; ++dr, ++dc) mask |= 1ULL << square(r-dr, c+dc);
    for (int dr = 1, dc = 1; r-dr > 0 && c-dc > 0; ++dr, ++dc) mask |= 1ULL << square(r-dr, c-dc);

    return mask;
}
void ChessEngine::print_mask_comparison(int sq) {
    uint64_t given_rook_mask = rook_masks[sq];
    uint64_t local_rook_mask = gen_rook_mask_local(sq);
    std::cerr << "sq=" << sq << " (row=" << row_of(sq) << ",col=" << col_of(sq) << ")\n";
    std::cerr << "given rook_masks[sq] hex: 0x" << std::hex << given_rook_mask << std::dec << "\n";
    std::cerr << "generated local rook mask hex: 0x" << std::hex << local_rook_mask << std::dec << "\n";
    std::cerr << "given rook mask visual:\n" << bitboard_to_string(given_rook_mask);
    std::cerr << "generated local mask visual:\n" << bitboard_to_string(local_rook_mask);

    uint64_t given_bishop_mask = bishop_masks[sq];
    uint64_t local_bishop_mask = gen_bishop_mask_local(sq);
    std::cerr << "given bishop_masks[sq] hex: 0x" << std::hex << given_bishop_mask << std::dec << "\n";
    std::cerr << "generated local bishop mask hex: 0x" << std::hex << local_bishop_mask << std::dec << "\n";
    std::cerr << "given bishop mask visual:\n" << bitboard_to_string(given_bishop_mask);
    std::cerr << "generated local bishop mask visual:\n" << bitboard_to_string(local_bishop_mask);
}

// A compact debug of the mismatch you saw (print raw hex and visual)
void ChessEngine::debug_one_mismatch(int sq, uint64_t mask, uint64_t blockers, uint64_t slow, uint64_t magic) {
    std::cerr << "DEBUG MISMATCH sq=" << sq << "\n";
    std::cerr << "mask hex:  0x" << std::hex << mask << std::dec << "\n" << bitboard_to_string(mask);
    std::cerr << "blockers hex: 0x" << std::hex << blockers << std::dec << "\n" << bitboard_to_string(blockers);
    std::cerr << "slow hex:  0x" << std::hex << slow << std::dec << "\n" << bitboard_to_string(slow);
    std::cerr << "magic hex: 0x" << std::hex << magic << std::dec << "\n" << bitboard_to_string(magic);
}

// ========== UTIL & API ==========
bool ChessEngine::is_white_to_move() const { return white_to_move; }

bool ChessEngine::is_checkmate() const {
    if (!is_in_check(white_to_move)) return false;
    return generate_legal_moves().empty();
}

bool ChessEngine::is_stalemate() const {
    if (is_in_check(white_to_move)) return false;
    return generate_legal_moves().empty();
}

bool ChessEngine::is_game_over() const { return is_checkmate() || is_stalemate(); }


#ifdef TEST_MAGIC_VERIFY
#include <cstdlib>
int main() {
    ChessEngine engine;
    engine.dump_square_indices();

    engine.quick_mapping_test();

    engine.print_mask_comparison(0);
    engine.verify_magic_tables();
    //std::cout << (ok ? "[PASS] Magic table verification succeeded\n" : "[FAIL] Magic table verification failed\n");
    //return ok ? 0 : 1;
}
#endif