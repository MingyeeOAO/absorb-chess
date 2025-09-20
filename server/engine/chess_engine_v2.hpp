    // Piece value calculation (for eval)
    int calculate_piece_ability_value(uint32_t piece, uint32_t abilities) const;
#pragma once
#include <vector>
#include <array>
#include <cstdint>

constexpr uint32_t PIECE_PAWN = 1;
constexpr uint32_t PIECE_KNIGHT = 2;
constexpr uint32_t PIECE_BISHOP = 4;
constexpr uint32_t PIECE_ROOK = 8;
constexpr uint32_t PIECE_QUEEN = 16;
constexpr uint32_t PIECE_KING = 32;

// Ability flags (absorbed abilities)
constexpr uint32_t ABILITY_PAWN = 64;
constexpr uint32_t ABILITY_KNIGHT = 128;
constexpr uint32_t ABILITY_BISHOP = 256;
constexpr uint32_t ABILITY_ROOK = 512;
constexpr uint32_t ABILITY_QUEEN = 1024;
constexpr uint32_t ABILITY_KING = 2048;

// State flags
constexpr uint32_t HAS_MOVED = 4096;
constexpr uint32_t IS_WHITE = 8192;

// Color mask
constexpr uint32_t COLOR_MASK = IS_WHITE;

struct Move {
    uint8_t from_row, from_col, to_row, to_col;
    uint32_t flags;  // For special moves (castle, en passant, promotion)
    
    Move(uint8_t fr, uint8_t fc, uint8_t tr, uint8_t tc, uint32_t f = 0)
        : from_row(fr), from_col(fc), to_row(tr), to_col(tc), flags(f) {}
};

struct GameState {
    std::array<std::array<uint32_t, 8>, 8> board;
    bool white_to_move;
    bool white_king_castled;
    bool black_king_castled;
    int en_passant_col;  // -1 if no en passant, otherwise column
    int en_passant_row;  // row where en passant capture can happen
    
    GameState() {
        // Initialize empty board
        for (auto& row : board) {
            row.fill(0);
        }
        white_to_move = true;
        white_king_castled = false;
        black_king_castled = false;
        en_passant_col = -1;
        en_passant_row = -1;
    }
};

struct Piece {
    uint8_t row, col;
    uint32_t data;
    
    Piece(uint8_t r, uint8_t c, uint32_t d) : row(r), col(c), data(d) {}
    
    bool is_white() const { return data & IS_WHITE; }
    bool has_moved() const { return data & HAS_MOVED; }
    uint32_t get_type() const { return data & (PIECE_PAWN | PIECE_KNIGHT | PIECE_BISHOP | PIECE_ROOK | PIECE_QUEEN | PIECE_KING); }
    uint32_t get_abilities() const { return data & (ABILITY_PAWN | ABILITY_KNIGHT | ABILITY_BISHOP | ABILITY_ROOK | ABILITY_QUEEN | ABILITY_KING); }
};
/**
 * ChessEngine - Bitboard-based chess engine
 * 
 * This is a high-performance version of ChessEngine that uses bitboards
 * for internal representation while maintaining full API compatibility.
 * 
 * Key optimizations:
 * - 64-bit bitboards for piece positions instead of 8x8 array scanning
 * - Magic bitboards for sliding piece attack generation
 * - Lookup tables for knight, king, and pawn attacks
 * - Fast bit operations (popcount, bitscan) for evaluation
 * - Absorb-based mobility evaluation using bitboard unions
 */
class ChessEngine {
private:
    // ========== BITBOARD REPRESENTATION ==========
    
    // Piece bitboards [color][piece_type]
    uint64_t piece_bb[2][6];  // [white/black][pawn/knight/bishop/rook/queen/king]
    
    // Ability bitboards [color][ability_type] - for absorbed abilities
    uint64_t ability_bb[2][6];  // [white/black][pawn/knight/bishop/rook/queen/king abilities]
    
    // Occupancy bitboards
    uint64_t occupancy_white;
    uint64_t occupancy_black; 
    uint64_t occupancy_all;
    
    // Has-moved bitboards (for castling and initial pawn moves)
    uint64_t has_moved_bb[2];  // [white/black]
    
    // Game state (same as original for API compatibility)
    bool white_to_move;
    bool white_king_castled;
    bool black_king_castled;
    int en_passant_col;
    int en_passant_row;
    
    // ========== LOOKUP TABLES ==========
    
    // Attack lookup tables
    static uint64_t king_attacks[64];
    static uint64_t knight_attacks[64];
    static uint64_t pawn_attacks[2][64];  // [color][square]
    
    // Magic bitboard tables for sliding pieces
    static uint64_t rook_magics[64];
    static uint64_t bishop_magics[64];
    static uint64_t* rook_attacks[64];
    static uint64_t* bishop_attacks[64];
    static uint64_t rook_masks[64];
    static uint64_t bishop_masks[64];
    static int rook_shifts[64];
    static int bishop_shifts[64];
    static bool tables_initialized;
    
    // Square conversion
    static constexpr int square(int row, int col) { return row * 8 + col; }
    static constexpr int row_of(int sq) { return sq / 8; }
    static constexpr int col_of(int sq) { return sq % 8; }
    static constexpr uint64_t square_bb(int row, int col) { return 1ULL << square(row, col); }
    static constexpr uint64_t square_bb(int sq) { return 1ULL << sq; }
    
    // ========== EVALUATION CACHE ==========
    mutable uint64_t nodes_searched = 0;
    mutable uint64_t quiescence_nodes = 0;
    mutable int cached_material_eval = 0;
    mutable int cached_king_safety_eval = 0;
    mutable int cached_mobility_eval = 0;
    mutable bool eval_cache_valid = false;
    
    static constexpr int PIECE_VALUES[7] = {0, 100, 320, 330, 500, 900, 20000};
    
    // ========== INTERNAL HELPER FUNCTIONS ==========
    
    // Bitboard initialization and updates
    void init_lookup_tables();
    void init_magic_bitboards();
    uint64_t slow_rook_attacks(int square, uint64_t blockers);
    uint64_t slow_bishop_attacks(int square, uint64_t blockers);
    void update_occupancy();
    void update_from_legacy_board(const std::vector<std::vector<uint32_t>>& board);
    std::vector<std::vector<uint32_t>> convert_to_legacy_board() const;
    
    // Magic bitboard helpers
    uint64_t get_rook_attacks(int square, uint64_t blockers) const;
    uint64_t get_bishop_attacks(int square, uint64_t blockers) const;
    uint64_t get_queen_attacks(int square, uint64_t blockers) const;
    
    // Attack generation
    uint64_t get_attacks_by_piece_type(int square, int piece_type, bool white, uint64_t blockers) const;
    uint64_t get_all_attacks(bool white) const;
    bool is_square_attacked(int square, bool by_white) const;
    
    // Move generation (bitboard-based)
    void generate_pawn_moves_bb(bool white, std::vector<Move>& moves) const;
    void generate_knight_moves_bb(bool white, std::vector<Move>& moves) const;
    void generate_bishop_moves_bb(bool white, std::vector<Move>& moves) const;
    void generate_rook_moves_bb(bool white, std::vector<Move>& moves) const;
    void generate_queen_moves_bb(bool white, std::vector<Move>& moves) const;
    void generate_king_moves_bb(bool white, std::vector<Move>& moves) const;
    void generate_castling_moves_bb(bool white, std::vector<Move>& moves) const;
    
    // Add moves from bitboard to move list
    void add_moves_from_bitboard(int from_square, uint64_t targets, std::vector<Move>& moves, uint32_t flags = 0) const;
    void add_pawn_moves(int from_square, uint64_t targets, bool white, std::vector<Move>& moves) const;
    
    // Move application (bitboard-based)
    struct MoveUndoBB {
        uint64_t captured_piece_bb[2][6];  // What was captured
        uint64_t captured_ability_bb[2][6];
        uint64_t old_has_moved[2];
        bool old_white_castled, old_black_castled;
        int old_en_passant_col, old_en_passant_row;
        bool old_eval_cache_valid;
        int old_material_eval, old_king_safety_eval, old_mobility_eval;
    };
    
    MoveUndoBB apply_move_bb(const Move& move);
    void undo_move_bb(const Move& move, const MoveUndoBB& undo);
    
    // Evaluation (bitboard-optimized)
    int evaluate_material_bb() const;
    int evaluate_mobility_bb() const;
    int evaluate_king_safety_bb() const;
    int calculate_piece_ability_value_bb(int square, bool white) const;
    
    // Search
    int minimax_bb(int depth, int alpha, int beta, bool maximizing);
    int quiescence_search_bb(int alpha, int beta);
    
    // Utility
    int popcount(uint64_t bb) const { return __builtin_popcountll(bb); }
    int bitscan_forward(uint64_t bb) const { return __builtin_ctzll(bb); }
    int bitscan_reverse(uint64_t bb) const { return 63 - __builtin_clzll(bb); }
    uint64_t clear_lsb(uint64_t bb) const { return bb & (bb - 1); }

public:
    ChessEngine();
    
    // ========== PUBLIC API (IDENTICAL TO ORIGINAL) ==========
    
    // Interface with Python and WASM - same signatures as ChessEngine
    void set_board_state(const std::vector<std::vector<uint32_t>>& board, 
                        bool white_to_move, bool white_castled, bool black_castled,
                        int en_passant_col, int en_passant_row);
    
    std::vector<Move> generate_legal_moves();
    std::vector<Move> generate_legal_moves() const;
    Move find_best_move(int depth, int time_limit_ms);
    int get_evaluation();
    
    // New public API functions
    std::pair<Move, int> get_best_move(int depth);
    std::vector<Move> get_legal_moves();
    std::tuple<std::vector<std::vector<uint32_t>>, bool, bool, bool, int, int> get_board_state();
    bool is_valid_move(int from_row, int from_col, int to_row, int to_col);
    std::pair<uint32_t, uint32_t> get_piece_at(int row, int col);
    void print_board();
    int performance_test(int depth);
    
    // Previously private, now public for WASM access
    int evaluate_position() const;
    bool is_in_check(bool white_king) const;
    
    // Legacy compatibility - these convert between bitboards and arrays internally
    struct MoveUndo {
        uint32_t captured_piece;
        uint8_t captured_row, captured_col;
        bool old_en_passant_valid;
        int old_en_passant_col, old_en_passant_row;
        bool old_white_castled, old_black_castled;
        uint32_t original_moving_piece;
        
        int material_delta = 0;
        int king_safety_delta = 0;
        int mobility_delta = 0;
        bool old_eval_cache_valid = false;
    };
    
    MoveUndo apply_move(const Move& move);
    void undo_move(const Move& move, const MoveUndo& undo_info);
    
    // Utility functions
    std::vector<std::vector<uint32_t>> get_board_state() const;
    bool is_white_to_move() const;
    bool is_game_over() const;
    bool is_checkmate() const;
    bool is_stalemate() const;
    
    // ========== DEBUG/TESTING ==========
    void print_bitboards() const;
    uint64_t perft(int depth);  // For move generation testing
};

// Helper function to initialize static lookup tables
void init_chess_engine__tables();