// chess_engine_v2.hpp
#pragma once

#include <vector>
#include <array>
#include <cstdint>
#include <tuple>
#include <string>

// Basic piece and ability flags (same semantics as your current code)
constexpr uint32_t PIECE_PAWN   = 1;
constexpr uint32_t PIECE_KNIGHT = 2;
constexpr uint32_t PIECE_BISHOP = 4;
constexpr uint32_t PIECE_ROOK   = 8;
constexpr uint32_t PIECE_QUEEN  = 16;
constexpr uint32_t PIECE_KING   = 32;

// Ability flags (absorbed abilities)
constexpr uint32_t ABILITY_PAWN   = 64;
constexpr uint32_t ABILITY_KNIGHT = 128;
constexpr uint32_t ABILITY_BISHOP = 256;
constexpr uint32_t ABILITY_ROOK   = 512;
constexpr uint32_t ABILITY_QUEEN  = 1024;
constexpr uint32_t ABILITY_KING   = 2048;

// State flags
constexpr uint32_t HAS_MOVED = 4096;
constexpr uint32_t IS_WHITE  = 8192;

// Color mask
constexpr uint32_t COLOR_MASK = IS_WHITE;

// Piece type mask and empty square constant
constexpr uint32_t PIECE_MASK = PIECE_PAWN | PIECE_KNIGHT | PIECE_BISHOP | PIECE_ROOK | PIECE_QUEEN | PIECE_KING;
constexpr uint32_t EMPTY = 0;

// Simple piece constants for compatibility
constexpr uint32_t PAWN = PIECE_PAWN;
constexpr uint32_t KNIGHT = PIECE_KNIGHT;
constexpr uint32_t BISHOP = PIECE_BISHOP;
constexpr uint32_t ROOK = PIECE_ROOK;
constexpr uint32_t QUEEN = PIECE_QUEEN;
constexpr uint32_t KING = PIECE_KING;

struct Move {
    uint8_t from_row, from_col, to_row, to_col;
    uint32_t flags;  // For special moves (castle, en passant, promotion)
    Move(uint8_t fr = 0, uint8_t fc = 0, uint8_t tr = 0, uint8_t tc = 0, uint32_t f = 0)
        : from_row(fr), from_col(fc), to_row(tr), to_col(tc), flags(f) {}
};

struct GameState {
    std::array<std::array<uint32_t, 8>, 8> board;
    bool white_to_move;
    bool white_king_castled;
    bool black_king_castled;
    int en_passant_col;  // -1 if no en passant
    int en_passant_row;

    GameState() {
        for (auto &r : board) r.fill(0);
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
    Piece(uint8_t r = 0, uint8_t c = 0, uint32_t d = 0) : row(r), col(c), data(d) {}
    bool is_white() const { return data & IS_WHITE; }
    bool has_moved() const { return data & HAS_MOVED; }
    uint32_t get_type() const { return data & (PIECE_PAWN | PIECE_KNIGHT | PIECE_BISHOP | PIECE_ROOK | PIECE_QUEEN | PIECE_KING); }
    uint32_t get_abilities() const { return data & (ABILITY_PAWN | ABILITY_KNIGHT | ABILITY_BISHOP | ABILITY_ROOK | ABILITY_QUEEN | ABILITY_KING); }
};

// Forward-declare U64 for SlidingTable
using U64 = uint64_t;

// SlidingTable: helper container for sliding attack tables used by magic bitboards.
// We expose it in the header so implementation (.cpp) can define and fill it.
struct MagicTable {
    uint64_t masks[64];        // attack mask
    int table_sizes[64];       // 2^bits(mask)
    uint64_t* attacks_ptr[64]; // pointer to attack table
    int shifts[64];          // right shift for magic index
};

// ChessEngine class declaration (public API same as your previous engine)
class ChessEngine {
private:
    // -------- Bitboard state --------
    // piece_bb[color][piece_index] where color 0 = white, 1 = black
    // piece_index order: 0=pawn,1=knight,2=bishop,3=rook,4=queen,5=king
    uint64_t piece_bb[2][6];
    uint64_t ability_bb[2][6]; // absorbed ability bitboards
    uint64_t occupancy_white;
    uint64_t occupancy_black;
    uint64_t occupancy_all;
    uint64_t has_moved_bb[2];

    // Game state mirrors
    bool white_to_move;
    bool white_king_castled;
    bool black_king_castled;
    int en_passant_col;
    int en_passant_row;

    // -------- Lookup tables / magic structures (declared extern in .cpp) --------
    static uint64_t king_attacks[64];
    static uint64_t knight_attacks[64];
    static uint64_t pawn_attacks[2][64];



    // Magic helpers (declare as extern arrays in .cpp)
    static uint64_t rook_magics[64];
    static uint64_t bishop_magics[64];
    static uint64_t rook_masks[64];
    static uint64_t bishop_masks[64];
    static int rook_shifts[64];
    static int bishop_shifts[64];

        
    static MagicTable rook_table;
    static MagicTable bishop_table;
    
    // -------- Evaluation cache --------
    mutable uint64_t nodes_searched;
    mutable uint64_t quiescence_nodes;
    mutable int cached_material_eval;
    mutable int cached_king_safety_eval;
    mutable int cached_mobility_eval;
    mutable bool eval_cache_valid;
    
    static constexpr int PIECE_VALUES[7] = {0, 100, 320, 330, 500, 900, 20000};
    
    // -------- Internal helpers (defined in .cpp) --------
    static int bitscan_forward(uint64_t bb) {return __builtin_ctzll(bb);}

    static int popcount(uint64_t bb) {return __builtin_popcountll(bb);}
    void init_lookup_tables();
    void init_magic_bitboards(); // builds masks and attack tables
    uint64_t slow_rook_attacks(int square, uint64_t blockers) const;
    uint64_t slow_bishop_attacks(int square, uint64_t blockers) const;
    void update_occupancy();
    void update_from_legacy_board(const std::vector<std::vector<uint32_t>>& board);
    std::vector<std::vector<uint32_t>> convert_to_legacy_board() const;
    
    uint64_t get_rook_attacks(int square, uint64_t blockers) const;
    uint64_t get_bishop_attacks(int square, uint64_t blockers) const;
    uint64_t get_queen_attacks(int square, uint64_t blockers) const;
    
    uint64_t get_attacks_by_piece_type(int square, int piece_type, bool white, uint64_t blockers) const;
    uint64_t get_all_attacks(bool white) const;
    bool is_square_attacked(int square, bool by_white) const;
    bool is_square_attacked_fast(int square, bool by_white) const;
    bool is_in_check_fast(bool white_king) const;
    
    // Bitboard move generation
    void generate_pawn_moves_bb(bool white, std::vector<Move>& moves) const;
    void generate_knight_moves_bb(bool white, std::vector<Move>& moves) const;
    void generate_bishop_moves_bb(bool white, std::vector<Move>& moves) const;
    void generate_rook_moves_bb(bool white, std::vector<Move>& moves) const;
    void generate_queen_moves_bb(bool white, std::vector<Move>& moves) const;
    void generate_king_moves_bb(bool white, std::vector<Move>& moves) const;
    void generate_castling_moves_bb(bool white, std::vector<Move>& moves) const;
    
    void add_moves_from_bitboard(int from_square, uint64_t targets, std::vector<Move>& moves, uint32_t flags = 0) const;
    void add_pawn_moves(int from_square, uint64_t targets, bool white, std::vector<Move>& moves) const;
    
    // Move application
    struct MoveUndoBB {
        uint64_t captured_piece_bb[2][6];
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
    int calculate_piece_ability_value(uint32_t piece, uint32_t abilities) const;
    
    
    // Search
    int minimax_bb(int depth, int alpha, int beta, bool maximizing);
    int quiescence_search_bb(int alpha, int beta);
    
    // Utilities
    inline int bitscan_reverse(uint64_t bb) const { return 63 - __builtin_clzll(bb); }
    inline uint64_t clear_lsb(uint64_t bb) const { return bb & (bb - 1); }
    static constexpr int square(int row, int col) { return row * 8 + col; }
    static constexpr inline uint64_t square_bb_from_rc(int row /*0..7 bottom->top*/, int col /*0..7 a->h*/) { return 1ULL << (row * 8 + col); }
    static constexpr inline int row_of(int sq) { return sq / 8; }   // 0..7 bottom->top
    static constexpr inline int col_of(int sq) { return sq % 8; }
    static constexpr uint64_t square_bb(int row, int col) { return 1ULL << square(row, col); }
    static constexpr uint64_t square_bb(int sq) { return 1ULL << sq; }
    
public:
    ChessEngine();

    // Attack tables will be allocated in .cpp and pointers placed into SlidingTable.attacks_ptr
    static bool tables_initialized;
    // Public API (same as original)
    void set_board_state(const std::vector<std::vector<uint32_t>>& board,
                        bool white_to_move, bool white_castled, bool black_castled,
                        int en_passant_col, int en_passant_row);

    std::vector<Move> generate_legal_moves();
    std::vector<Move> generate_legal_moves() const;
    std::vector<Move> generate_capture_moves();
    int get_evaluation();

    // New optimization helpers
    bool is_legal_move_fast(const Move& move, int king_square, bool in_check, 
                           int num_checkers, uint64_t checkers, uint64_t pinned_pieces);
    uint64_t get_checkers(bool white_king) const;
    uint64_t get_pinned_pieces(bool white_king) const;
    bool is_king_move_safe(int to_square);  // Remove const - needs to modify state temporarily
    bool is_king_capture_safe(int to_square);  // New function for king captures
    bool is_move_along_pin_ray(int from_sq, int to_sq, int king_square) const;
    bool is_sliding_check(int checker_square, int king_square) const;
    uint64_t get_squares_between(int sq1, int sq2) const;
    bool are_aligned_rank_or_file(int sq1, int sq2) const;
    bool are_aligned_diagonal(int sq1, int sq2) const;
    int get_piece_value(int piece) const;
    uint32_t get_piece_at_square(int row, int col) const;  // Helper to get piece from bitboards
    int evaluate_development() const;  // New function for development evaluation
    int count_developed_pieces(bool white) const;  // Helper to count developed pieces
    //bool is_square_attacked_by_enemy_manual(int square, bool by_white, uint64_t modified_occupancy) const;
    uint64_t get_rook_attacks_manual(int square, uint64_t blockers) const;
    uint64_t get_bishop_attacks_manual(int square, uint64_t blockers) const;

    // New helpers
    std::pair<Move, int> get_best_move(int depth);
    std::vector<Move> get_legal_moves();
    std::tuple<std::vector<std::vector<uint32_t>>, bool, bool, bool, int, int> get_board_state();
    bool is_valid_move(int from_row, int from_col, int to_row, int to_col);
    std::pair<uint32_t, uint32_t> get_piece_at(int row, int col);
    void print_board();
    int performance_test(int depth);
    
    // Utility API functions for WASM interface
    bool is_white_to_move() const;
    bool is_checkmate() const;
    bool is_stalemate() const;
    bool is_game_over() const;

    // Public helpers used by UI / WASM
    int evaluate_position() const;
    bool is_in_check(bool white_king) const;

    // Legacy compatibility apply/undo
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

    std::vector<std::vector<uint32_t>> get_board_state() const;

    // Debug/testing
    void print_bitboards() const;
    uint64_t perft(int depth);
    void verify_magic_tables();
    std::string bitboard_to_string(uint64_t bb);
    void dump_square_indices();
    void quick_mapping_test();
    void print_mask_comparison(int square);
    uint64_t gen_bishop_mask_local(int sq);
    uint64_t gen_rook_mask_local(int sq);
    void debug_one_mismatch(int sq, uint64_t mask, uint64_t blockers, uint64_t slow, uint64_t magic);
};

// Initialize the engine tables (call once from .cpp or from main)
void init_chess_engine__tables();
