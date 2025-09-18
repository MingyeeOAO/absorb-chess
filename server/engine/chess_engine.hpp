#pragma once
#include <vector>
#include <array>
#include <cstdint>

// Piece encoding using bit flags
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

class ChessEngine {
private:
    GameState state;
    std::vector<Piece> white_pieces;
    std::vector<Piece> black_pieces;
    // instrumentation
    mutable uint64_t nodes_searched = 0;
    mutable uint64_t quiescence_nodes = 0;

    // Incremental evaluation cache
    mutable int cached_material_eval = 0;
    mutable int cached_king_safety_eval = 0;
    mutable int cached_mobility_eval = 0;
    mutable bool eval_cache_valid = false;

    // Piece values for evaluation
    static constexpr int PIECE_VALUES[7] = {0, 100, 320, 330, 500, 900, 20000}; // indexed by piece type bit position
    
    void update_piece_lists();
    bool is_square_attacked(uint8_t row, uint8_t col, bool by_white) const;
    bool is_in_check(bool white_king) const;
    bool can_castle_kingside(bool white) const;
    bool can_castle_queenside(bool white) const;
    
    // Move generation for each piece type
    void generate_pawn_moves(const Piece& piece, std::vector<Move>& moves) const;
    void generate_knight_moves(const Piece& piece, std::vector<Move>& moves) const;
    void generate_bishop_moves(const Piece& piece, std::vector<Move>& moves) const;
    void generate_rook_moves(const Piece& piece, std::vector<Move>& moves) const;
    void generate_queen_moves(const Piece& piece, std::vector<Move>& moves) const;
    void generate_king_moves(const Piece& piece, std::vector<Move>& moves) const;
    
    void generate_sliding_moves(const Piece& piece, std::vector<Move>& moves, 
                               const std::vector<std::pair<int, int>>& directions) const;
    
    bool is_valid_square(int row, int col) const { return row >= 0 && row < 8 && col >= 0 && col < 8; }
    bool is_enemy_piece(uint32_t piece_data, bool current_player_white) const;
    bool is_friendly_piece(uint32_t piece_data, bool current_player_white) const;

    bool is_square_attacked_board_based(uint8_t row, uint8_t col, bool by_white) const;
    int count_pseudolegal_moves_for_color(bool white) const;

    // Move application and undo
    struct MoveUndo {
        uint32_t captured_piece;
        uint8_t captured_row, captured_col;
        bool old_en_passant_valid;
        int old_en_passant_col, old_en_passant_row;
        bool old_white_castled, old_black_castled;
        uint32_t original_moving_piece;  // Store original piece before promotion
        
        // Incremental evaluation deltas
        int material_delta = 0;        // Material evaluation change
        int king_safety_delta = 0;     // King safety evaluation change
        int mobility_delta = 0;        // Mobility evaluation change
        bool old_eval_cache_valid = false;  // Was eval cache valid before move?
    };
    struct MoveUndoBoard {
        uint32_t captured_piece;
        int old_en_passant_col, old_en_passant_row;
        bool old_white_castled, old_black_castled;
        uint32_t moving_piece_before; // copy of moving piece
    };
    MoveUndo apply_move(const Move& move);
    void undo_move(const Move& move, const MoveUndo& undo_info);
    
    // Evaluation
    int evaluate_position() const;
    int evaluate_material() const;
    int evaluate_mobility() const;
    int evaluate_king_safety() const;
    
    // Incremental evaluation helpers
    int calculate_material_delta(const Move& move) const;
    int calculate_king_safety_delta(const Move& move) const;
    int calculate_mobility_delta(const Move& move) const;
    void invalidate_eval_cache() { eval_cache_valid = false; }
    void update_eval_cache() const;
    
    // Piece ability evaluation for improved move ordering
    int calculate_piece_ability_value(uint32_t piece) const;
    int calculate_capture_value(const Move& move) const;
    int calculate_promotion_value(const Move& move, uint32_t promotion_type) const;
    
    // Search
    int minimax(int depth, int alpha, int beta, bool maximizing);
    int quiescence_search(int alpha, int beta);
    void undo_move_board_only(const Move& move, const MoveUndoBoard& undo);
    void apply_move_board_only(const Move& move, MoveUndoBoard& undo);
    void generate_pseudolegal_captures_for_side(bool white, std::vector<Move>& out) const;

public:
    ChessEngine();
    
    // Interface with Python
    void set_board_state(const std::vector<std::vector<uint32_t>>& board, 
                        bool white_to_move, bool white_castled, bool black_castled,
                        int en_passant_col, int en_passant_row);
    
    std::vector<Move> generate_legal_moves();
    Move find_best_move(int depth, int time_limit_ms);
    int get_evaluation();
    
    // Utility functions
    std::vector<std::vector<uint32_t>> get_board_state() const;
    bool is_game_over() const;
    bool is_checkmate() const;
    bool is_stalemate() const;
};