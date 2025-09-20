#include "chess_engine_v2.hpp"
#include <algorithm>
#include <climits>
#include <chrono>
#include <iostream>
#include <cassert>
#include <vector>
#include <tuple>

// Static lookup table declarations
uint64_t ChessEngine::king_attacks[64];
uint64_t ChessEngine::knight_attacks[64];
uint64_t ChessEngine::pawn_attacks[2][64];
uint64_t ChessEngine::rook_magics[64];
uint64_t ChessEngine::bishop_magics[64];
uint64_t* ChessEngine::rook_attacks[64];
uint64_t* ChessEngine::bishop_attacks[64];
uint64_t ChessEngine::rook_masks[64];
uint64_t ChessEngine::bishop_masks[64];
int ChessEngine::rook_shifts[64];
int ChessEngine::bishop_shifts[64];
bool ChessEngine::tables_initialized = false;

// Static initialization flag
static bool tables_initialized = false;

// ========== CONSTRUCTOR ==========

ChessEngine::ChessEngine() {
    if (!tables_initialized) {
        init_lookup_tables();
        init_magic_bitboards();
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
}

// ========== LOOKUP TABLE INITIALIZATION ==========

void ChessEngine::init_lookup_tables() {
    // Initialize king attacks
    for (int sq = 0; sq < 64; ++sq) {
        int row = row_of(sq), col = col_of(sq);
        uint64_t attacks = 0ULL;
        
        for (int dr = -1; dr <= 1; ++dr) {
            for (int dc = -1; dc <= 1; ++dc) {
                if (dr == 0 && dc == 0) continue;
                int new_row = row + dr, new_col = col + dc;
                if (new_row >= 0 && new_row < 8 && new_col >= 0 && new_col < 8) {
                    attacks |= square_bb(new_row, new_col);
                }
            }
        }
        king_attacks[sq] = attacks;
    }
    
    // Initialize knight attacks
    const int knight_moves[8][2] = {{-2,-1}, {-2,1}, {-1,-2}, {-1,2}, {1,-2}, {1,2}, {2,-1}, {2,1}};
    for (int sq = 0; sq < 64; ++sq) {
        int row = row_of(sq), col = col_of(sq);
        uint64_t attacks = 0ULL;
        
        for (int i = 0; i < 8; ++i) {
            int new_row = row + knight_moves[i][0];
            int new_col = col + knight_moves[i][1];
            if (new_row >= 0 && new_row < 8 && new_col >= 0 && new_col < 8) {
                attacks |= square_bb(new_row, new_col);
            }
        }
        knight_attacks[sq] = attacks;
    }
    
    // Initialize pawn attacks
    for (int sq = 0; sq < 64; ++sq) {
        int row = row_of(sq), col = col_of(sq);
        
        // White pawn attacks (moving up, so row - 1)
        uint64_t white_attacks = 0ULL;
        if (row > 0) {
            if (col > 0) white_attacks |= square_bb(row - 1, col - 1);
            if (col < 7) white_attacks |= square_bb(row - 1, col + 1);
        }
        pawn_attacks[0][sq] = white_attacks;  // 0 = white
        
        // Black pawn attacks (moving down, so row + 1)
        uint64_t black_attacks = 0ULL;
        if (row < 7) {
            if (col > 0) black_attacks |= square_bb(row + 1, col - 1);
            if (col < 7) black_attacks |= square_bb(row + 1, col + 1);
        }
        pawn_attacks[1][sq] = black_attacks;  // 1 = black
    }
    
    // Initialize magic bitboards for rook and bishop
    // Using pre-computed magic numbers for production performance
    init_magic_bitboards();
    
    // Simplified: initialize masks (occupied squares that can block the piece)
    for (int sq = 0; sq < 64; ++sq) {
        int row = row_of(sq), col = col_of(sq);
        
        // Rook mask: all squares on rank/file except edges
        uint64_t rook_mask = 0ULL;
        for (int r = 1; r < 7; ++r) if (r != row) rook_mask |= square_bb(r, col);
        for (int c = 1; c < 7; ++c) if (c != col) rook_mask |= square_bb(row, c);
        rook_masks[sq] = rook_mask;
        
        // Bishop mask: all squares on diagonals except edges
        uint64_t bishop_mask = 0ULL;
        for (int i = 1; i < 7; ++i) {
            if (row + i < 7 && col + i < 7) bishop_mask |= square_bb(row + i, col + i);
            if (row + i < 7 && col - i > 0) bishop_mask |= square_bb(row + i, col - i);
            if (row - i > 0 && col + i < 7) bishop_mask |= square_bb(row - i, col + i);
            if (row - i > 0 && col - i > 0) bishop_mask |= square_bb(row - i, col - i);
        }
        bishop_masks[sq] = bishop_mask;
    }
}

// ========== MAGIC BITBOARD INITIALIZATION ==========

// Pre-computed magic numbers (these are well-known values used in chess engines)
static const uint64_t rook_magic_numbers[64] = {
    0x8a80104000800020ULL, 0x140002000100040ULL, 0x2801880a0017001ULL, 0x100081001000420ULL,
    0x200020010080420ULL, 0x3001c0002010008ULL, 0x8480008002000100ULL, 0x2080088004402900ULL,
    0x800098204000ULL, 0x2024401000200040ULL, 0x100802000801000ULL, 0x120800800801000ULL,
    0x208808088000400ULL, 0x2802200800400ULL, 0x2200800100020080ULL, 0x801000060821100ULL,
    0x80044006422000ULL, 0x100808020004000ULL, 0x12108a0010204200ULL, 0x140848010000802ULL,
    0x481828014002800ULL, 0x8094004002004100ULL, 0x4010040010010802ULL, 0x20008806104ULL,
    0x100400080208000ULL, 0x2040002120081000ULL, 0x21200680100081ULL, 0x20100080080080ULL,
    0x2000a00200410ULL, 0x20080800400ULL, 0x80088400100102ULL, 0x80004600042881ULL,
    0x4040008040800020ULL, 0x440003000200801ULL, 0x4200011004500ULL, 0x188020010100100ULL,
    0x14800401802800ULL, 0x2080040080800200ULL, 0x124080204001001ULL, 0x200046502000484ULL,
    0x480400080088020ULL, 0x1000422010034000ULL, 0x30200100110040ULL, 0x100021010009ULL,
    0x2002080100110004ULL, 0x202008004008002ULL, 0x20020004010100ULL, 0x2048440040820001ULL,
    0x101002200408200ULL, 0x40802000401080ULL, 0x4008142004410100ULL, 0x2060820c0120200ULL,
    0x1001004080100ULL, 0x20c020080040080ULL, 0x2935610830022400ULL, 0x44440041009200ULL,
    0x280001040802101ULL, 0x2100190040002085ULL, 0x80c0084100102001ULL, 0x4024081001000421ULL,
    0x20030a0244872ULL, 0x12001008414402ULL, 0x2006104900a0804ULL, 0x1004081002402ULL
};

static const uint64_t bishop_magic_numbers[64] = {
    0x40040844404084ULL, 0x2004208a004208ULL, 0x10190041080202ULL, 0x108060845042010ULL,
    0x581104180800210ULL, 0x2112080446200010ULL, 0x1080820820060210ULL, 0x3c0808410220200ULL,
    0x4050404440404ULL, 0x21001420088ULL, 0x24d0080801082102ULL, 0x1020a0a020400ULL,
    0x40308200402ULL, 0x4011002100800ULL, 0x401484104104005ULL, 0x801010402020200ULL,
    0x400210c3880100ULL, 0x404022024108200ULL, 0x810018200204102ULL, 0x4002801a02003ULL,
    0x85040820080400ULL, 0x810102c808880400ULL, 0xe900410884800ULL, 0x8002020480840102ULL,
    0x220200865090201ULL, 0x2010100a02021202ULL, 0x152048408022401ULL, 0x20080002081110ULL,
    0x4001001021004000ULL, 0x800040400a011002ULL, 0xe4004081011002ULL, 0x1c004001012080ULL,
    0x8004200962a00220ULL, 0x8422100208500202ULL, 0x2000402200300c08ULL, 0x8646020080080080ULL,
    0x80020a0200100808ULL, 0x2010004880111000ULL, 0x623000a080011400ULL, 0x42008c0340209202ULL,
    0x209188240001000ULL, 0x400408a884001800ULL, 0x110400a6080400ULL, 0x1840060a44020800ULL,
    0x90080104000041ULL, 0x201011000808101ULL, 0x1a2208080504f080ULL, 0x8012020600211212ULL,
    0x500861011240000ULL, 0x180806108200800ULL, 0x4000020e01040044ULL, 0x300000261044000aULL,
    0x802241102020002ULL, 0x20906061210001ULL, 0x5a84841004010310ULL, 0x4010801011c04ULL,
    0xa010109502200ULL, 0x4a02012000ULL, 0x500201010098b028ULL, 0x8040002811040900ULL,
    0x28000010020204ULL, 0x6000020202d0240ULL, 0x8918844842082200ULL, 0x4010011029020020ULL
};

// Attack table storage
static uint64_t rook_attack_table[102400];  // Enough space for all rook attack tables
static uint64_t bishop_attack_table[5248];  // Enough space for all bishop attack tables

uint64_t ChessEngine::slow_rook_attacks(int square, uint64_t blockers) {
    uint64_t attacks = 0ULL;
    int row = row_of(square), col = col_of(square);
    
    // North
    for (int r = row + 1; r < 8; ++r) {
        uint64_t sq_bb = square_bb(r, col);
        attacks |= sq_bb;
        if (blockers & sq_bb) break;
    }
    
    // South  
    for (int r = row - 1; r >= 0; --r) {
        uint64_t sq_bb = square_bb(r, col);
        attacks |= sq_bb;
        if (blockers & sq_bb) break;
    }
    
    // East
    for (int c = col + 1; c < 8; ++c) {
        uint64_t sq_bb = square_bb(row, c);
        attacks |= sq_bb;
        if (blockers & sq_bb) break;
    }
    
    // West
    for (int c = col - 1; c >= 0; --c) {
        uint64_t sq_bb = square_bb(row, c);
        attacks |= sq_bb;
        if (blockers & sq_bb) break;
    }
    
    return attacks;
}

uint64_t ChessEngine::slow_bishop_attacks(int square, uint64_t blockers) {
    uint64_t attacks = 0ULL;
    int row = row_of(square), col = col_of(square);
    
    // Northeast
    for (int i = 1; row + i < 8 && col + i < 8; ++i) {
        uint64_t sq_bb = square_bb(row + i, col + i);
        attacks |= sq_bb;
        if (blockers & sq_bb) break;
    }
    
    // Northwest
    for (int i = 1; row + i < 8 && col - i >= 0; ++i) {
        uint64_t sq_bb = square_bb(row + i, col - i);
        attacks |= sq_bb;
        if (blockers & sq_bb) break;
    }
    
    // Southeast
    for (int i = 1; row - i >= 0 && col + i < 8; ++i) {
        uint64_t sq_bb = square_bb(row - i, col + i);
        attacks |= sq_bb;
        if (blockers & sq_bb) break;
    }
    
    // Southwest
    for (int i = 1; row - i >= 0 && col - i >= 0; ++i) {
        uint64_t sq_bb = square_bb(row - i, col - i);
        attacks |= sq_bb;
        if (blockers & sq_bb) break;
    }
    
    return attacks;
}

void ChessEngine::init_magic_bitboards() {
    uint64_t* rook_table_ptr = rook_attack_table;
    uint64_t* bishop_table_ptr = bishop_attack_table;
    
    // Initialize rook magic bitboards
    for (int sq = 0; sq < 64; ++sq) {
        rook_magics[sq] = rook_magic_numbers[sq];
        rook_attacks[sq] = rook_table_ptr;
        
        uint64_t mask = rook_masks[sq];
        int shift = 64 - popcount(mask);
        rook_shifts[sq] = shift;
        
        // Generate all possible blocker configurations
        std::vector<uint64_t> blockers;
        std::vector<uint64_t> attacks;
        
        uint64_t subset = 0;
        do {
            blockers.push_back(subset);
            attacks.push_back(slow_rook_attacks(sq, subset));
            subset = (subset - mask) & mask;  // Next subset
        } while (subset != 0);
        
        // Fill attack table using magic indexing
        for (size_t i = 0; i < blockers.size(); ++i) {
            uint64_t index = (blockers[i] * rook_magics[sq]) >> shift;
            rook_attacks[sq][index] = attacks[i];
        }
        
        rook_table_ptr += (1ULL << popcount(mask));
    }
    
    // Initialize bishop magic bitboards
    for (int sq = 0; sq < 64; ++sq) {
        bishop_magics[sq] = bishop_magic_numbers[sq];
        bishop_attacks[sq] = bishop_table_ptr;
        
        uint64_t mask = bishop_masks[sq];
        int shift = 64 - popcount(mask);
        bishop_shifts[sq] = shift;
        
        // Generate all possible blocker configurations
        std::vector<uint64_t> blockers;
        std::vector<uint64_t> attacks;
        
        uint64_t subset = 0;
        do {
            blockers.push_back(subset);
            attacks.push_back(slow_bishop_attacks(sq, subset));
            subset = (subset - mask) & mask;  // Next subset
        } while (subset != 0);
        
        // Fill attack table using magic indexing
        for (size_t i = 0; i < blockers.size(); ++i) {
            uint64_t index = (blockers[i] * bishop_magics[sq]) >> shift;
            bishop_attacks[sq][index] = attacks[i];
        }
        
        bishop_table_ptr += (1ULL << popcount(mask));
    }
}

// ========== BITBOARD UTILITY FUNCTIONS ==========

void ChessEngine::update_occupancy() {
    occupancy_white = occupancy_black = 0ULL;
    
    for (int piece = 0; piece < 6; ++piece) {
        occupancy_white |= piece_bb[0][piece];
        occupancy_black |= piece_bb[1][piece];
    }
    
    occupancy_all = occupancy_white | occupancy_black;
}

// ========== SLIDING PIECE ATTACKS (SIMPLIFIED MAGIC) ==========

uint64_t ChessEngine::get_rook_attacks(int square, uint64_t blockers) const {
    // Simplified rook attack generation (not using full magic bitboards)
    // In production, this would use magic multiplication and lookup table
    
    uint64_t attacks = 0ULL;
    int row = row_of(square), col = col_of(square);
    
    // North
    for (int r = row + 1; r < 8; ++r) {
        uint64_t sq_bb = square_bb(r, col);
        attacks |= sq_bb;
        if (blockers & sq_bb) break;
    }
    
    // South  
    for (int r = row - 1; r >= 0; --r) {
        uint64_t sq_bb = square_bb(r, col);
        attacks |= sq_bb;
        if (blockers & sq_bb) break;
    }
    
    // East
    for (int c = col + 1; c < 8; ++c) {
        uint64_t sq_bb = square_bb(row, c);
        attacks |= sq_bb;
        if (blockers & sq_bb) break;
    }
    
    // West
    for (int c = col - 1; c >= 0; --c) {
        uint64_t sq_bb = square_bb(row, c);
        attacks |= sq_bb;
        if (blockers & sq_bb) break;
    }
    
    return attacks;
}

uint64_t ChessEngine::get_bishop_attacks(int square, uint64_t blockers) const {
    uint64_t attacks = 0ULL;
    int row = row_of(square), col = col_of(square);
    
    // Northeast
    for (int i = 1; row + i < 8 && col + i < 8; ++i) {
        uint64_t sq_bb = square_bb(row + i, col + i);
        attacks |= sq_bb;
        if (blockers & sq_bb) break;
    }
    
    // Northwest
    for (int i = 1; row + i < 8 && col - i >= 0; ++i) {
        uint64_t sq_bb = square_bb(row + i, col - i);
        attacks |= sq_bb;
        if (blockers & sq_bb) break;
    }
    
    // Southeast
    for (int i = 1; row - i >= 0 && col + i < 8; ++i) {
        uint64_t sq_bb = square_bb(row - i, col + i);
        attacks |= sq_bb;
        if (blockers & sq_bb) break;
    }
    
    // Southwest
    for (int i = 1; row - i >= 0 && col - i >= 0; ++i) {
        uint64_t sq_bb = square_bb(row - i, col - i);
        attacks |= sq_bb;
        if (blockers & sq_bb) break;
    }
    
    return attacks;
}

uint64_t ChessEngine::get_queen_attacks(int square, uint64_t blockers) const {
    return get_rook_attacks(square, blockers) | get_bishop_attacks(square, blockers);
}

// ========== BOARD STATE CONVERSION ==========

void ChessEngine::set_board_state(const std::vector<std::vector<uint32_t>>& board, 
                                    bool white_to_move, bool white_castled, bool black_castled,
                                    int en_passant_col, int en_passant_row) {
    update_from_legacy_board(board);
    this->white_to_move = white_to_move;
    this->white_king_castled = white_castled;
    this->black_king_castled = black_castled;
    this->en_passant_col = en_passant_col;
    this->en_passant_row = en_passant_row;
    update_occupancy();
    eval_cache_valid = false;
}

void ChessEngine::update_from_legacy_board(const std::vector<std::vector<uint32_t>>& board) {
    // Clear all bitboards
    for (int color = 0; color < 2; ++color) {
        for (int piece = 0; piece < 6; ++piece) {
            piece_bb[color][piece] = 0ULL;
            ability_bb[color][piece] = 0ULL;
        }
        has_moved_bb[color] = 0ULL;
    }
    
    // Convert from legacy 2D array format
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            uint32_t piece_data = board[row][col];
            if (piece_data == 0) continue;
            
            int sq = square(row, col);
            uint64_t sq_bb = square_bb(sq);
            bool is_white = (piece_data & IS_WHITE) != 0;
            int color = is_white ? 0 : 1;
            
            // Set has_moved flag
            if (piece_data & HAS_MOVED) {
                has_moved_bb[color] |= sq_bb;
            }
            
            // Set base piece type
            if (piece_data & PIECE_PAWN) piece_bb[color][0] |= sq_bb;
            else if (piece_data & PIECE_KNIGHT) piece_bb[color][1] |= sq_bb;
            else if (piece_data & PIECE_BISHOP) piece_bb[color][2] |= sq_bb;
            else if (piece_data & PIECE_ROOK) piece_bb[color][3] |= sq_bb;
            else if (piece_data & PIECE_QUEEN) piece_bb[color][4] |= sq_bb;
            else if (piece_data & PIECE_KING) piece_bb[color][5] |= sq_bb;
            
            // Set absorbed abilities
            if (piece_data & ABILITY_PAWN) ability_bb[color][0] |= sq_bb;
            if (piece_data & ABILITY_KNIGHT) ability_bb[color][1] |= sq_bb;
            if (piece_data & ABILITY_BISHOP) ability_bb[color][2] |= sq_bb;
            if (piece_data & ABILITY_ROOK) ability_bb[color][3] |= sq_bb;
            if (piece_data & ABILITY_QUEEN) ability_bb[color][4] |= sq_bb;
            if (piece_data & ABILITY_KING) ability_bb[color][5] |= sq_bb;
        }
    }
}

std::vector<std::vector<uint32_t>> ChessEngine::convert_to_legacy_board() const {
    std::vector<std::vector<uint32_t>> board(8, std::vector<uint32_t>(8, 0));
    
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            int sq = square(row, col);
            uint64_t sq_bb = square_bb(sq);
            uint32_t piece_data = 0;
            
            for (int color = 0; color < 2; ++color) {
                if (color == 0) piece_data |= IS_WHITE;  // White pieces
                
                // Check base piece types
                if (piece_bb[color][0] & sq_bb) piece_data |= PIECE_PAWN;
                else if (piece_bb[color][1] & sq_bb) piece_data |= PIECE_KNIGHT;
                else if (piece_bb[color][2] & sq_bb) piece_data |= PIECE_BISHOP;
                else if (piece_bb[color][3] & sq_bb) piece_data |= PIECE_ROOK;
                else if (piece_bb[color][4] & sq_bb) piece_data |= PIECE_QUEEN;
                else if (piece_bb[color][5] & sq_bb) piece_data |= PIECE_KING;
                
                if (piece_data != 0 && color == 1) piece_data &= ~IS_WHITE;  // Remove white flag for black
                
                if (piece_data != 0) {
                    // Add absorbed abilities
                    if (ability_bb[color][0] & sq_bb) piece_data |= ABILITY_PAWN;
                    if (ability_bb[color][1] & sq_bb) piece_data |= ABILITY_KNIGHT;
                    if (ability_bb[color][2] & sq_bb) piece_data |= ABILITY_BISHOP;
                    if (ability_bb[color][3] & sq_bb) piece_data |= ABILITY_ROOK;
                    if (ability_bb[color][4] & sq_bb) piece_data |= ABILITY_QUEEN;
                    if (ability_bb[color][5] & sq_bb) piece_data |= ABILITY_KING;
                    
                    // Add has_moved flag
                    if (has_moved_bb[color] & sq_bb) piece_data |= HAS_MOVED;
                    
                    break;  // Found piece, stop checking colors
                }
            }
            
            board[row][col] = piece_data;
        }
    }
    
    return board;
}

std::vector<std::vector<uint32_t>> ChessEngine::get_board_state() const {
    return convert_to_legacy_board();
}

// ========== ATTACK GENERATION ==========

uint64_t ChessEngine::get_attacks_by_piece_type(int square, int piece_type, bool white, uint64_t blockers) const {
    switch (piece_type) {
        case 0: return pawn_attacks[white ? 0 : 1][square];  // Pawn
        case 1: return knight_attacks[square];                // Knight
        case 2: return get_bishop_attacks(square, blockers);  // Bishop
        case 3: return get_rook_attacks(square, blockers);    // Rook
        case 4: return get_queen_attacks(square, blockers);   // Queen
        case 5: return king_attacks[square];                  // King
        default: return 0ULL;
    }
}

uint64_t ChessEngine::get_all_attacks(bool white) const {
    uint64_t attacks = 0ULL;
    int color = white ? 0 : 1;
    
    // For each piece type, get attacks from all pieces of that type
    for (int piece_type = 0; piece_type < 6; ++piece_type) {
        uint64_t pieces = piece_bb[color][piece_type];
        
        while (pieces) {
            int square = bitscan_forward(pieces);
            pieces = clear_lsb(pieces);
            
            attacks |= get_attacks_by_piece_type(square, piece_type, white, occupancy_all);
        }
        
        // Also check absorbed abilities
        uint64_t ability_pieces = ability_bb[color][piece_type];
        while (ability_pieces) {
            int square = bitscan_forward(ability_pieces);
            ability_pieces = clear_lsb(ability_pieces);
            
            // Make sure this square actually has a piece
            if (occupancy_all & square_bb(square)) {
                attacks |= get_attacks_by_piece_type(square, piece_type, white, occupancy_all);
            }
        }
    }
    
    return attacks;
}

bool ChessEngine::is_square_attacked(int square, bool by_white) const {
    int color = by_white ? 0 : 1;
    
    // Check attacks from each piece type
    for (int piece_type = 0; piece_type < 6; ++piece_type) {
        uint64_t pieces = piece_bb[color][piece_type];
        
        while (pieces) {
            int attacker_square = bitscan_forward(pieces);
            pieces = clear_lsb(pieces);
            
            uint64_t attacks = get_attacks_by_piece_type(attacker_square, piece_type, by_white, occupancy_all);
            if (attacks & square_bb(square)) {
                return true;
            }
        }
        
        // Check absorbed abilities
        uint64_t ability_pieces = ability_bb[color][piece_type];
        while (ability_pieces) {
            int attacker_square = bitscan_forward(ability_pieces);
            ability_pieces = clear_lsb(ability_pieces);
            
            // Make sure this square actually has a piece
            if (occupancy_all & square_bb(attacker_square)) {
                uint64_t attacks = get_attacks_by_piece_type(attacker_square, piece_type, by_white, occupancy_all);
                if (attacks & square_bb(square)) {
                    return true;
                }
            }
        }
    }
    
    return false;
}

bool ChessEngine::is_in_check(bool white_king) const {
    int color = white_king ? 0 : 1;
    uint64_t king_bb = piece_bb[color][5];  // King is piece type 5
    
    if (king_bb == 0) return false;  // No king found
    
    int king_square = bitscan_forward(king_bb);
    return is_square_attacked(king_square, !white_king);
}

// ========== MOVE GENERATION ==========

void ChessEngine::add_moves_from_bitboard(int from_square, uint64_t targets, std::vector<Move>& moves, uint32_t flags) const {
    while (targets) {
        int to_square = bitscan_forward(targets);
        targets = clear_lsb(targets);
        
        moves.emplace_back(row_of(from_square), col_of(from_square), 
                          row_of(to_square), col_of(to_square), flags);
    }
}

void ChessEngine::add_pawn_moves(int from_square, uint64_t targets, bool white, std::vector<Move>& moves) const {
    int promotion_rank = white ? 0 : 7;  // 8th rank for white, 1st rank for black
    
    while (targets) {
        int to_square = bitscan_forward(targets);
        targets = clear_lsb(targets);
        
        int to_row = row_of(to_square);
        
        if (to_row == promotion_rank) {
            // Promotion moves
            moves.emplace_back(row_of(from_square), col_of(from_square), to_row, col_of(to_square), 4);  // Queen
            moves.emplace_back(row_of(from_square), col_of(from_square), to_row, col_of(to_square), 5);  // Rook
            moves.emplace_back(row_of(from_square), col_of(from_square), to_row, col_of(to_square), 6);  // Bishop
            moves.emplace_back(row_of(from_square), col_of(from_square), to_row, col_of(to_square), 7);  // Knight
        } else {
            moves.emplace_back(row_of(from_square), col_of(from_square), to_row, col_of(to_square), 0);
        }
    }
}

void ChessEngine::generate_pawn_moves_bb(bool white, std::vector<Move>& moves) const {
    int color = white ? 0 : 1;
    uint64_t pawns = piece_bb[color][0];  // Pawn is piece type 0
    int direction = white ? -1 : 1;       // White moves up (decreasing row), black moves down
    int start_rank = white ? 6 : 1;       // Starting rank for double moves

    // True pawns: full pawn moves
    while (pawns) {
        int from_square = bitscan_forward(pawns);
        pawns = clear_lsb(pawns);

        int from_row = row_of(from_square);
        int from_col = col_of(from_square);

        // Single push
        int to_row = from_row + direction;
        if (to_row >= 0 && to_row < 8) {
            uint64_t single_push = square_bb(to_row, from_col);
            if (!(occupancy_all & single_push)) {
                add_pawn_moves(from_square, single_push, white, moves);

                // Double push
                if (from_row == start_rank) {
                    int double_to_row = to_row + direction;
                    if (double_to_row >= 0 && double_to_row < 8) {
                        uint64_t double_push = square_bb(double_to_row, from_col);
                        if (!(occupancy_all & double_push)) {
                            add_pawn_moves(from_square, double_push, white, moves);
                        }
                    }
                }
            }
        }

        // Captures
        uint64_t attacks = pawn_attacks[color][from_square];
        uint64_t captures = attacks & (white ? occupancy_black : occupancy_white);
        add_pawn_moves(from_square, captures, white, moves);

        // En passant
        if (en_passant_col >= 0 && en_passant_row >= 0) {
            uint64_t ep_target = square_bb(en_passant_row, en_passant_col);
            if (attacks & ep_target) {
                moves.emplace_back(from_row, from_col, en_passant_row, en_passant_col, 1);  // En passant flag
            }
        }
    }

    // Pieces with pawn ability: only pawn captures, no forward moves
    uint64_t ability_pawns = ability_bb[color][0];
    while (ability_pawns) {
        int from_square = bitscan_forward(ability_pawns);
        ability_pawns = clear_lsb(ability_pawns);

        // Make sure this square has a piece (not just an ability)
        if (!(occupancy_all & square_bb(from_square))) continue;

        int from_row = row_of(from_square);
        int from_col = col_of(from_square);

        // Captures only for absorbed pawn ability
        uint64_t attacks = pawn_attacks[color][from_square];
        uint64_t captures = attacks & (white ? occupancy_black : occupancy_white);
        add_pawn_moves(from_square, captures, white, moves);

        // En passant for pawn ability
        if (en_passant_col >= 0 && en_passant_row >= 0) {
            uint64_t ep_target = square_bb(en_passant_row, en_passant_col);
            if (attacks & ep_target) {
                moves.emplace_back(from_row, from_col, en_passant_row, en_passant_col, 1);  // En passant flag
            }
        }
    }
// --- New piece value logic adapted from old code ---
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
}

void ChessEngine::generate_knight_moves_bb(bool white, std::vector<Move>& moves) const {
    int color = white ? 0 : 1;
    
    // Base knight pieces
    uint64_t knights = piece_bb[color][1];
    while (knights) {
        int from_square = bitscan_forward(knights);
        knights = clear_lsb(knights);
        
        uint64_t attacks = knight_attacks[from_square];
        uint64_t targets = attacks & ~(white ? occupancy_white : occupancy_black);
        add_moves_from_bitboard(from_square, targets, moves);
    }
    
    // Absorbed knight abilities
    uint64_t ability_knights = ability_bb[color][1];
    while (ability_knights) {
        int from_square = bitscan_forward(ability_knights);
        ability_knights = clear_lsb(ability_knights);
        
        if (!(occupancy_all & square_bb(from_square))) continue;
        
        uint64_t attacks = knight_attacks[from_square];
        uint64_t targets = attacks & ~(white ? occupancy_white : occupancy_black);
        add_moves_from_bitboard(from_square, targets, moves);
    }
}

void ChessEngine::generate_bishop_moves_bb(bool white, std::vector<Move>& moves) const {
    int color = white ? 0 : 1;
    
    // Base bishop pieces
    uint64_t bishops = piece_bb[color][2];
    while (bishops) {
        int from_square = bitscan_forward(bishops);
        bishops = clear_lsb(bishops);
        
        uint64_t attacks = get_bishop_attacks(from_square, occupancy_all);
        uint64_t targets = attacks & ~(white ? occupancy_white : occupancy_black);
        add_moves_from_bitboard(from_square, targets, moves);
    }
    
    int color = white ? 0 : 1;
    int direction = white ? -1 : 1;
    int start_rank = white ? 6 : 1;

    // All pieces (not just pawns) with pawn ability can move/capture like a pawn
    uint64_t pawn_like = piece_bb[color][0] | ability_bb[color][0];
    while (pawn_like) {
        int from_square = bitscan_forward(pawn_like);
        pawn_like = clear_lsb(pawn_like);

        if (!(occupancy_all & square_bb(from_square))) continue; // must be a piece

        int from_row = row_of(from_square);
        int from_col = col_of(from_square);

        // Single push
        int to_row = from_row + direction;
        if (to_row >= 0 && to_row < 8) {
            uint64_t single_push = square_bb(to_row, from_col);
            if (!(occupancy_all & single_push)) {
                add_pawn_moves(from_square, single_push, white, moves);

                // Double push (only for true pawns on start rank)
                if ((piece_bb[color][0] & square_bb(from_square)) && from_row == start_rank) {
                    int double_to_row = to_row + direction;
                    if (double_to_row >= 0 && double_to_row < 8) {
                        uint64_t double_push = square_bb(double_to_row, from_col);
                        if (!(occupancy_all & double_push)) {
                            add_pawn_moves(from_square, double_push, white, moves);
                        }
                    }
                }
            }
        }

        // Captures
        uint64_t attacks = pawn_attacks[color][from_square];
        uint64_t captures = attacks & (white ? occupancy_black : occupancy_white);
        add_pawn_moves(from_square, captures, white, moves);

        // En passant
        if (en_passant_col >= 0 && en_passant_row >= 0) {
            uint64_t ep_target = square_bb(en_passant_row, en_passant_col);
            if (attacks & ep_target) {
                moves.emplace_back(from_row, from_col, en_passant_row, en_passant_col, 1);  // En passant flag
            }
        }
    }
    
    // Base king pieces
    uint64_t kings = piece_bb[color][5];
    while (kings) {
        int from_square = bitscan_forward(kings);
        kings = clear_lsb(kings);
        
        uint64_t attacks = king_attacks[from_square];
        uint64_t targets = attacks & ~(white ? occupancy_white : occupancy_black);
        add_moves_from_bitboard(from_square, targets, moves);
    }
    
    // Absorbed king abilities
    uint64_t ability_kings = ability_bb[color][5];
    while (ability_kings) {
        int from_square = bitscan_forward(ability_kings);
        ability_kings = clear_lsb(ability_kings);
        
        if (!(occupancy_all & square_bb(from_square))) continue;
        
        uint64_t attacks = king_attacks[from_square];
        uint64_t targets = attacks & ~(white ? occupancy_white : occupancy_black);
        add_moves_from_bitboard(from_square, targets, moves);
    }
    
    // Castling
    generate_castling_moves_bb(white, moves);
}

void ChessEngine::generate_castling_moves_bb(bool white, std::vector<Move>& moves) const {
    if ((white && white_king_castled) || (!white && black_king_castled)) return;
    if (is_in_check(white)) return;
    
    int color = white ? 0 : 1;
    int king_row = white ? 7 : 0;
    
    // Find king
    uint64_t king_bb = piece_bb[color][5];
    if (king_bb == 0) return;
    
    int king_square = bitscan_forward(king_bb);
    if (row_of(king_square) != king_row || col_of(king_square) != 4) return;  // King must be on e1/e8
    
    // Check if king has moved
    if (has_moved_bb[color] & square_bb(king_square)) return;
    
    // Kingside castling
    uint64_t kingside_rook = piece_bb[color][3] & square_bb(king_row, 7);
    if (kingside_rook && !(has_moved_bb[color] & kingside_rook)) {
        // Check if squares between king and rook are empty
        uint64_t between = square_bb(king_row, 5) | square_bb(king_row, 6);
        if (!(occupancy_all & between)) {
            // Check if king would pass through or land on attacked squares
            if (!is_square_attacked(square(king_row, 5), !white) && 
                !is_square_attacked(square(king_row, 6), !white)) {
                moves.emplace_back(king_row, 4, king_row, 6, 2);  // Kingside castle flag
            }
        }
    }
    
    // Queenside castling
    uint64_t queenside_rook = piece_bb[color][3] & square_bb(king_row, 0);
    if (queenside_rook && !(has_moved_bb[color] & queenside_rook)) {
        // Check if squares between king and rook are empty
        uint64_t between = square_bb(king_row, 1) | square_bb(king_row, 2) | square_bb(king_row, 3);
        if (!(occupancy_all & between)) {
            // Check if king would pass through or land on attacked squares
            if (!is_square_attacked(square(king_row, 2), !white) && 
                !is_square_attacked(square(king_row, 3), !white)) {
                moves.emplace_back(king_row, 4, king_row, 2, 3);  // Queenside castle flag
            }
        }
    }
}

std::vector<Move> ChessEngine::generate_legal_moves() {
    std::vector<Move> moves;
    
    generate_pawn_moves_bb(white_to_move, moves);
    generate_knight_moves_bb(white_to_move, moves);
    generate_bishop_moves_bb(white_to_move, moves);
    generate_rook_moves_bb(white_to_move, moves);
    generate_queen_moves_bb(white_to_move, moves);
    generate_king_moves_bb(white_to_move, moves);
    
    // Filter out moves that leave king in check
    std::vector<Move> legal_moves;
    bool original_turn = white_to_move;  // Store original turn before applying move
    for (const Move& move : moves) {
        MoveUndoBB undo = apply_move_bb(move);
        // After applying move, check if original player's king is NOT in check
        if (!is_in_check(original_turn)) {
            legal_moves.push_back(move);
        }
        undo_move_bb(move, undo);
    }
    
    return legal_moves;
}

std::vector<Move> ChessEngine::generate_legal_moves() const {
    // For const version, we need to cast away const temporarily since the internal
    // move generation and checking requires non-const operations
    return const_cast<ChessEngine*>(this)->generate_legal_moves();
}

// ========== EVALUATION ==========

int ChessEngine::calculate_piece_ability_value_bb(int square, bool white) const {
    int color = white ? 0 : 1;
    uint64_t sq_bb = square_bb(square);
    uint32_t piece = 0;
    uint32_t abilities = 0;
    // Find base piece type
    for (int piece_type = 0; piece_type < 6; ++piece_type) {
        if (piece_bb[color][piece_type] & sq_bb) {
            switch (piece_type) {
                case 0: piece |= PIECE_PAWN; break;
                case 1: piece |= PIECE_KNIGHT; break;
                case 2: piece |= PIECE_BISHOP; break;
                case 3: piece |= PIECE_ROOK; break;
                case 4: piece |= PIECE_QUEEN; break;
                case 5: piece |= PIECE_KING; break;
            }
        }
    }
    // Find abilities
    for (int ability_type = 0; ability_type < 6; ++ability_type) {
        if (ability_bb[color][ability_type] & sq_bb) {
            switch (ability_type) {
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
    
    // White pieces
    for (int sq = 0; sq < 64; ++sq) {
        uint64_t sq_bb = square_bb(sq);
        if (occupancy_white & sq_bb) {
            score += calculate_piece_ability_value_bb(sq, true);
        }
        if (occupancy_black & sq_bb) {
            score -= calculate_piece_ability_value_bb(sq, false);
        }
    }
    
    return score;
}

int ChessEngine::evaluate_mobility_bb() const {
    // Fast mobility evaluation using bitboard attack generation
    uint64_t white_attacks = get_all_attacks(true);
    uint64_t black_attacks = get_all_attacks(false);
    
    // Count attacked squares (mobility)
    int white_mobility = popcount(white_attacks & ~occupancy_white);
    int black_mobility = popcount(black_attacks & ~occupancy_black);
    
    return (white_mobility - black_mobility) * 10;
}

int ChessEngine::evaluate_king_safety_bb() const {
    int score = 0;
    
    // Penalty for being in check
    if (is_in_check(true)) score -= 100;
    if (is_in_check(false)) score += 100;
    
    // Penalty for losing castling rights (encourage keeping castling available)
    if (white_king_castled) score -= 50;  // White lost castling rights
    if (black_king_castled) score += 50;  // Black lost castling rights
    
    // Additional penalty for king being in center early game
    uint64_t white_king = piece_bb[0][5];
    if (white_king) {
        int king_square = bitscan_forward(white_king);
        int king_row = row_of(king_square);
        int king_col = col_of(king_square);
        
        // Penalty for white king not on back rank in opening/middlegame
        if (king_row != 7) {
            score -= 30;  // King moved from back rank penalty
        }
        // Extra penalty for king in center files
        if (king_col >= 3 && king_col <= 4) {
            score -= 20;  // King in center files
        }
        
        score += calculate_piece_ability_value_bb(king_square, true);
    }
    
    uint64_t black_king = piece_bb[1][5];
    if (black_king) {
        int king_square = bitscan_forward(black_king);
        int king_row = row_of(king_square);
        int king_col = col_of(king_square);
        
        // Penalty for black king not on back rank in opening/middlegame
        if (king_row != 0) {
            score += 30;  // King moved from back rank penalty (good for white)
        }
        // Extra penalty for king in center files
        if (king_col >= 3 && king_col <= 4) {
            score += 20;  // King in center files (good for white)
        }
        
        score -= calculate_piece_ability_value_bb(king_square, false);
    }
    
    return score;
}

int ChessEngine::evaluate_position() const {
    if (!eval_cache_valid) {
        //we are going to apply delta later
        cached_material_eval = evaluate_material_bb();
        cached_mobility_eval = evaluate_mobility_bb();
        cached_king_safety_eval = evaluate_king_safety_bb();
        eval_cache_valid = true;
    }
    
    return cached_material_eval + cached_mobility_eval + cached_king_safety_eval;
}

int ChessEngine::get_evaluation() {
    // Always return evaluation from white's perspective
    // Positive = good for white, Negative = good for black
    return evaluate_position();
}

// ========== MOVE APPLICATION ==========

ChessEngine::MoveUndoBB ChessEngine::apply_move_bb(const Move& move) {
    MoveUndoBB undo;
    
    // Save current state
    for (int color = 0; color < 2; ++color) {
        for (int piece = 0; piece < 6; ++piece) {
            undo.captured_piece_bb[color][piece] = piece_bb[color][piece];
            undo.captured_ability_bb[color][piece] = ability_bb[color][piece];
        }
        undo.old_has_moved[color] = has_moved_bb[color];
    }
    undo.old_white_castled = white_king_castled;
    undo.old_black_castled = black_king_castled;
    undo.old_en_passant_col = en_passant_col;
    undo.old_en_passant_row = en_passant_row;
    undo.old_eval_cache_valid = eval_cache_valid;
    
    // Apply move
    int from_sq = square(move.from_row, move.from_col);
    int to_sq = square(move.to_row, move.to_col);
    uint64_t from_bb = square_bb(from_sq);
    uint64_t to_bb = square_bb(to_sq);
    
    int color = white_to_move ? 0 : 1;
    
    // Find and move the piece
    for (int piece_type = 0; piece_type < 6; ++piece_type) {
        if (piece_bb[color][piece_type] & from_bb) {
            piece_bb[color][piece_type] &= ~from_bb;  // Remove from source
            piece_bb[color][piece_type] |= to_bb;     // Add to destination
            break;
        }
    }
    
    // Handle captures (remove captured piece)
    int enemy_color = 1 - color;
    for (int piece_type = 0; piece_type < 6; ++piece_type) {
        if (piece_bb[enemy_color][piece_type] & to_bb) {
            piece_bb[enemy_color][piece_type] &= ~to_bb;
            break;
        }
    }
    
    // Move abilities
    for (int ability_type = 0; ability_type < 6; ++ability_type) {
        if (ability_bb[color][ability_type] & from_bb) {
            ability_bb[color][ability_type] &= ~from_bb;
            ability_bb[color][ability_type] |= to_bb;
        }
        // Remove captured abilities
        ability_bb[enemy_color][ability_type] &= ~to_bb;
    }
    
    // Set has_moved flag
    has_moved_bb[color] |= to_bb;
    
    // Handle special moves
    if (move.flags == 1) {  // En passant
        int captured_sq = square(en_passant_row, en_passant_col);
        uint64_t captured_bb = square_bb(captured_sq);
        piece_bb[enemy_color][0] &= ~captured_bb;  // Remove captured pawn
        for (int i = 0; i < 6; ++i) {
            ability_bb[enemy_color][i] &= ~captured_bb;
        }
    } else if (move.flags == 2) {  // Kingside castling
        // Move rook
        uint64_t rook_from = square_bb(move.from_row, 7);
        uint64_t rook_to = square_bb(move.from_row, 5);
        piece_bb[color][3] &= ~rook_from;
        piece_bb[color][3] |= rook_to;
        has_moved_bb[color] |= rook_to;
        if (white_to_move) white_king_castled = true;
        else black_king_castled = true;
    } else if (move.flags == 3) {  // Queenside castling
        // Move rook
        uint64_t rook_from = square_bb(move.from_row, 0);
        uint64_t rook_to = square_bb(move.from_row, 3);
        piece_bb[color][3] &= ~rook_from;
        piece_bb[color][3] |= rook_to;
        has_moved_bb[color] |= rook_to;
        if (white_to_move) white_king_castled = true;
        else black_king_castled = true;
    } else if (move.flags >= 4 && move.flags <= 7) {  // Promotion
        // Remove pawn, add promoted piece
        piece_bb[color][0] &= ~to_bb;  // Remove pawn
        int promoted_piece = move.flags - 4;  // 0=queen, 1=rook, 2=bishop, 3=knight
        piece_bb[color][promoted_piece + 1] |= to_bb;  // Add promoted piece (offset by 1)
    }
    
    // Update en passant
    en_passant_col = en_passant_row = -1;
    if (piece_bb[color][0] & from_bb && abs(move.to_row - move.from_row) == 2) {  // Double pawn move
        en_passant_col = move.to_col;
        en_passant_row = (move.from_row + move.to_row) / 2;
    }
    
    // Switch turns
    white_to_move = !white_to_move;
    
    // Update occupancy
    update_occupancy();
    
    // Invalidate evaluation cache
    eval_cache_valid = false;
    
    return undo;
}

void ChessEngine::undo_move_bb(const Move& move, const MoveUndoBB& undo) {
    // Restore all state
    for (int color = 0; color < 2; ++color) {
        for (int piece = 0; piece < 6; ++piece) {
            piece_bb[color][piece] = undo.captured_piece_bb[color][piece];
            ability_bb[color][piece] = undo.captured_ability_bb[color][piece];
        }
        has_moved_bb[color] = undo.old_has_moved[color];
    }
    
    white_king_castled = undo.old_white_castled;
    black_king_castled = undo.old_black_castled;
    en_passant_col = undo.old_en_passant_col;
    en_passant_row = undo.old_en_passant_row;
    eval_cache_valid = undo.old_eval_cache_valid;
    
    white_to_move = !white_to_move;
    update_occupancy();
}

// ========== LEGACY API COMPATIBILITY ==========

ChessEngine::MoveUndo ChessEngine::apply_move(const Move& move) {
    MoveUndoBB bb_undo = apply_move_bb(move);
    
    // Convert to legacy format
    MoveUndo legacy_undo;
    // TODO: Fill in legacy undo structure if needed
    return legacy_undo;
}

void ChessEngine::undo_move(const Move& move, const MoveUndo& undo_info) {
    // TODO: Convert from legacy format and call undo_move_bb
}

// ========== SEARCH ==========

int ChessEngine::minimax_bb(int depth, int alpha, int beta, bool maximizing) {
    nodes_searched++;

    if (depth == 0) {
        return quiescence_search_bb(alpha, beta);
    }

    std::vector<Move> moves = generate_legal_moves();

    if (moves.empty()) {
        if (is_in_check(white_to_move)) {
            return maximizing ? -30000 + (5 - depth) : 30000 - (5 - depth);  // Checkmate
        } else {
            return 0;  // Stalemate
        }
    }

    // Use cached eval and delta for child nodes
    int current_eval = evaluate_position();

    if (maximizing) {
        int max_eval = INT_MIN;
        for (const Move& move : moves) {
            MoveUndoBB undo = apply_move_bb(move);
            int delta = evaluate_position() - current_eval;
            int eval = current_eval + delta;
            if (depth > 1) {
                eval = minimax_bb(depth - 1, alpha, beta, false);
            }
            undo_move_bb(move, undo);

            max_eval = std::max(max_eval, eval);
            alpha = std::max(alpha, eval);
            if (beta <= alpha) break;  // Alpha-beta pruning
        }
        return max_eval;
    } else {
        int min_eval = INT_MAX;
        for (const Move& move : moves) {
            MoveUndoBB undo = apply_move_bb(move);
            int delta = evaluate_position() - current_eval;
            int eval = current_eval + delta;
            if (depth > 1) {
                eval = minimax_bb(depth - 1, alpha, beta, true);
            }
            undo_move_bb(move, undo);

            min_eval = std::min(min_eval, eval);
            beta = std::min(beta, eval);
            if (beta <= alpha) break;  // Alpha-beta pruning
        }
        return min_eval;
    }
}

int ChessEngine::quiescence_search_bb(int alpha, int beta) {
    quiescence_nodes++;
    
    int stand_pat = evaluate_position();
    
    if (stand_pat >= beta) return beta;
    if (alpha < stand_pat) alpha = stand_pat;
    
    // Generate only capture moves for quiescence
    std::vector<Move> captures;
    // TODO: Implement capture-only move generation
    
    for (const Move& capture : captures) {
        MoveUndoBB undo = apply_move_bb(capture);
        int score = -quiescence_search_bb(-beta, -alpha);
        undo_move_bb(capture, undo);
        
        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }
    
    return alpha;
}

Move ChessEngine::find_best_move(int depth, int time_limit_ms) {
    nodes_searched = 0;
    quiescence_nodes = 0;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::vector<Move> moves = generate_legal_moves();
    if (moves.empty()) {
        return Move(0, 0, 0, 0);  // No legal moves
    }
    
    Move best_move = moves[0];
    int best_eval = white_to_move ? INT_MIN : INT_MAX;
    
    for (const Move& move : moves) {
        MoveUndoBB undo = apply_move_bb(move);
        // Get evaluation from white's perspective
        // White wants to maximize, black wants to minimize
        int eval = minimax_bb(depth - 1, INT_MIN, INT_MAX, white_to_move);
        undo_move_bb(move, undo);
        
        // White wants max eval, black wants min eval
        if ((white_to_move && eval > best_eval) || (!white_to_move && eval < best_eval)) {
            best_eval = eval;
            best_move = move;
        }
        
        // Check time limit
        auto current_time = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time);
        if (elapsed.count() >= time_limit_ms) break;
    }
    
    return best_move;
}

// ========== UTILITY FUNCTIONS ==========

bool ChessEngine::is_white_to_move() const {
    return white_to_move;
}

bool ChessEngine::is_checkmate() const {
    if (!is_in_check(white_to_move)) return false;
    return generate_legal_moves().empty();
}

bool ChessEngine::is_stalemate() const {
    if (is_in_check(white_to_move)) return false;
    return generate_legal_moves().empty();
}

bool ChessEngine::is_game_over() const {
    return is_checkmate() || is_stalemate();
}

void ChessEngine::print_bitboards() const {
    const char* piece_names[] = {"Pawn", "Knight", "Bishop", "Rook", "Queen", "King"};
    const char* colors[] = {"White", "Black"};
    
    for (int color = 0; color < 2; ++color) {
        std::cout << "\n" << colors[color] << " pieces:\n";
        for (int piece = 0; piece < 6; ++piece) {
            std::cout << piece_names[piece] << ": ";
            uint64_t bb = piece_bb[color][piece];
            while (bb) {
                int sq = bitscan_forward(bb);
                bb = clear_lsb(bb);
                std::cout << (char)('a' + col_of(sq)) << (char)('1' + row_of(sq)) << " ";
            }
            std::cout << "\n";
        }
    }
    
    std::cout << "\nOccupancy: White=" << popcount(occupancy_white) 
              << " Black=" << popcount(occupancy_black) 
              << " All=" << popcount(occupancy_all) << "\n";
}

uint64_t ChessEngine::perft(int depth) {
    if (depth == 0) return 1;
    
    uint64_t nodes = 0;
    std::vector<Move> moves = generate_legal_moves();
    
    for (const Move& move : moves) {
        MoveUndoBB undo = apply_move_bb(move);
        nodes += perft(depth - 1);
        undo_move_bb(move, undo);
    }
    
    return nodes;
}

// ========== PUBLIC API FUNCTIONS ==========

std::pair<Move, int> ChessEngine::get_best_move(int depth) {
    // Initialize to an invalid move using the constructor
    Move best_move(255, 255, 255, 255, 0);
    int best_score = white_to_move ? INT_MIN : INT_MAX;
    
    std::vector<Move> moves = generate_legal_moves();
    if (moves.empty()) {
        return {best_move, best_score};
    }
    
    for (const Move& move : moves) {
        MoveUndoBB undo = apply_move_bb(move);
        // White wants to maximize, black wants to minimize
        int score = minimax_bb(depth - 1, INT_MIN, INT_MAX, white_to_move);
        undo_move_bb(move, undo);
        
        if (white_to_move ? score > best_score : score < best_score) {
            best_score = score;
            best_move = move;
        }
    }
    
    return {best_move, best_score};
}

std::vector<Move> ChessEngine::get_legal_moves() {
    return generate_legal_moves();
}

std::tuple<std::vector<std::vector<uint32_t>>, bool, bool, bool, int, int> ChessEngine::get_board_state() {
    auto board = convert_to_legacy_board();
    return std::make_tuple(board, white_to_move, white_king_castled, black_king_castled, en_passant_col, en_passant_row);
}

bool ChessEngine::is_valid_move(int from_row, int from_col, int to_row, int to_col) {
    auto moves = generate_legal_moves();
    for (const Move& move : moves) {
        if (move.from_row == from_row && move.from_col == from_col &&
            move.to_row == to_row && move.to_col == to_col) {
            return true;
        }
    }
    return false;
}

std::pair<uint32_t, uint32_t> ChessEngine::get_piece_at(int row, int col) {
    int square = row * 8 + col;
    uint64_t sq_bb = square_bb(square);
    
    // Check for pieces
    for (int color = 0; color < 2; ++color) {
        for (int piece = 0; piece < 6; ++piece) {
            if (piece_bb[color][piece] & sq_bb) {
                uint32_t piece_code = (piece + 1) | (color == 0 ? 0x00 : 0x80);
                
                // Get abilities
                uint32_t abilities = 0;
                for (int ability = 0; ability < 6; ++ability) {
                    if (ability_bb[color][ability] & sq_bb) {
                        abilities |= (1 << ability);
                    }
                }
                
                return {piece_code, abilities};
            }
        }
    }
    
    return {0, 0};  // Empty square
}

void ChessEngine::print_board() {
    auto board = convert_to_legacy_board();
    
    std::cout << "  a b c d e f g h" << std::endl;
    for (int row = 0; row < 8; ++row) {
        std::cout << (8 - row) << " ";
        for (int col = 0; col < 8; ++col) {
            uint32_t piece = board[row][col];
            char c = '.';
            
            if (piece != 0) {
                int piece_type = piece & 0x7F;
                bool is_black = piece & 0x80;
                
                switch (piece_type) {
                    case 1: c = is_black ? 'p' : 'P'; break;
                    case 2: c = is_black ? 'n' : 'N'; break;
                    case 3: c = is_black ? 'b' : 'B'; break;
                    case 4: c = is_black ? 'r' : 'R'; break;
                    case 5: c = is_black ? 'q' : 'Q'; break;
                    case 6: c = is_black ? 'k' : 'K'; break;
                }
            }
            
            std::cout << c << " ";
        }
        std::cout << (8 - row) << std::endl;
    }
    std::cout << "  a b c d e f g h" << std::endl;
    std::cout << "Turn: " << (white_to_move ? "White" : "Black") << std::endl;
}

int ChessEngine::performance_test(int depth) {
    return perft(depth);
}

// ========== GLOBAL INITIALIZATION ==========

void init_chess_engine__tables() {
    // This function can be called to explicitly initialize tables
    // if needed for multi-threading or other purposes
    static ChessEngine dummy;  // Will trigger static initialization
}