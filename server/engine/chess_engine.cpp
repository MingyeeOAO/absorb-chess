#include "chess_engine.hpp"
#include <algorithm>
#include <climits>
#include <chrono>
#include <iostream>

// Define the static constexpr member
constexpr int ChessEngine::PIECE_VALUES[7];

ChessEngine::ChessEngine() {
    update_piece_lists();
}

void ChessEngine::set_board_state(const std::vector<std::vector<uint32_t>>& board, 
                                 bool white_to_move, bool white_castled, bool black_castled,
                                 int en_passant_col, int en_passant_row) {
    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 8; ++j) {
            state.board[i][j] = board[i][j];
        }
    }
    state.white_to_move = white_to_move;
    state.white_king_castled = white_castled;
    state.black_king_castled = black_castled;
    state.en_passant_col = en_passant_col;
    state.en_passant_row = en_passant_row;
    update_piece_lists();
    
    // Invalidate evaluation cache when board state changes
    eval_cache_valid = false;
}

// Minimal undo info for board-only moves


void ChessEngine::apply_move_board_only(const Move& move, MoveUndoBoard& undo) {
    undo.captured_piece = state.board[move.to_row][move.to_col];
    undo.old_en_passant_col = state.en_passant_col;
    undo.old_en_passant_row = state.en_passant_row;
    undo.old_white_castled = state.white_king_castled;
    undo.old_black_castled = state.black_king_castled;
    undo.moving_piece_before = state.board[move.from_row][move.from_col];

    uint32_t moving_piece = state.board[move.from_row][move.from_col];

    // Special moves (handle captured squares on board only)
    if (move.flags == 1) { // en passant
        undo.captured_piece = state.board[state.en_passant_row][state.en_passant_col];
        state.board[state.en_passant_row][state.en_passant_col] = 0;
    } else if (move.flags == 2) { // kingside
        uint32_t rook = state.board[move.from_row][7];
        state.board[move.from_row][7] = 0;
        state.board[move.from_row][5] = rook | HAS_MOVED;
        if (state.white_to_move) state.white_king_castled = true; else state.black_king_castled = true;
    } else if (move.flags == 3) { // queenside
        uint32_t rook = state.board[move.from_row][0];
        state.board[move.from_row][0] = 0;
        state.board[move.from_row][3] = rook | HAS_MOVED;
        if (state.white_to_move) state.white_king_castled = true; else state.black_king_castled = true;
    }

    // Move piece on board
    state.board[move.to_row][move.to_col] = moving_piece | HAS_MOVED;
    state.board[move.from_row][move.from_col] = 0;

    // Update en passant
    state.en_passant_col = -1;
    state.en_passant_row = -1;
    if ((moving_piece & PIECE_PAWN) && abs(move.to_row - move.from_row) == 2) {
        state.en_passant_col = move.to_col;
        state.en_passant_row = (move.from_row + move.to_row) / 2;
    }

    // flip side
    state.white_to_move = !state.white_to_move;
}

void ChessEngine::undo_move_board_only(const Move& move, const MoveUndoBoard& undo) {
    // restore moved piece (remove HAS_MOVED)
    state.board[move.from_row][move.from_col] = undo.moving_piece_before;

    state.board[move.to_row][move.to_col] = undo.captured_piece;

    if (move.flags == 1) { // en passant
        // restore captured pawn
        state.board[undo.old_en_passant_row][undo.old_en_passant_col] = undo.captured_piece;
        state.board[move.to_row][move.to_col] = 0; // destination should be empty for ep undo
    } else if (move.flags == 2) { // kingside
        uint32_t rook = state.board[move.from_row][5];
        state.board[move.from_row][5] = 0;
        state.board[move.from_row][7] = rook & ~HAS_MOVED;
    } else if (move.flags == 3) { // queenside
        uint32_t rook = state.board[move.from_row][3];
        state.board[move.from_row][3] = 0;
        state.board[move.from_row][0] = rook & ~HAS_MOVED;
    }

    // restore state
    state.en_passant_col = undo.old_en_passant_col;
    state.en_passant_row = undo.old_en_passant_row;
    state.white_king_castled = undo.old_white_castled;
    state.black_king_castled = undo.old_black_castled;
    state.white_to_move = !state.white_to_move;
}

bool ChessEngine::is_square_attacked_board_based(uint8_t row, uint8_t col, bool by_white) const {
    // scan entire board looking for an enemy piece that attacks (row,col)
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            uint32_t p = state.board[r][c];
            if (p == 0) continue;
            bool pwhite = (p & IS_WHITE) != 0;
            if (pwhite != by_white) continue;
            uint32_t type = p & (PIECE_PAWN | PIECE_KNIGHT | PIECE_BISHOP | PIECE_ROOK | PIECE_QUEEN | PIECE_KING);

            int dr = row - r;
            int dc = col - c;
            // Pawn attacks
            if (type & PIECE_PAWN) {
                int dir = by_white ? -1 : 1;
                if (r + dir == row && abs(c - col) == 1) return true;
            }
            // Knight
            if (type & PIECE_KNIGHT) {
                int adr = abs(dr), adc = abs(dc);
                if ((adr == 1 && adc == 2) || (adr == 2 && adc == 1)) return true;
            }
            // King
            if (type & PIECE_KING) {
                if (std::max(abs(dr), abs(dc)) == 1) return true;
            }
            // Sliding: bishop/queen diagonals
            if ((type & PIECE_BISHOP) || (type & PIECE_QUEEN)) {
                if (abs(dr) == abs(dc) && dr != 0) {
                    int step_r = (dr > 0) ? 1 : -1;
                    int step_c = (dc > 0) ? 1 : -1;
                    int rr = r + step_r, cc = c + step_c;
                    bool blocked = false;
                    while (rr != row || cc != col) {
                        if (state.board[rr][cc] != 0) { blocked = true; break; }
                        rr += step_r; cc += step_c;
                    }
                    if (!blocked) return true;
                }
            }
            // Sliding: rook/queen straight
            if ((type & PIECE_ROOK) || (type & PIECE_QUEEN)) {
                if (r == row || c == col) {
                    int step_r = (r == row) ? 0 : ((row > r) ? 1 : -1);
                    int step_c = (c == col) ? 0 : ((col > c) ? 1 : -1);
                    int rr = r + step_r, cc = c + step_c;
                    bool blocked = false;
                    while (rr != row || cc != col) {
                        if (state.board[rr][cc] != 0) { blocked = true; break; }
                        rr += step_r; cc += step_c;
                    }
                    if (!blocked) return true;
                }
            }
        }
    }
    return false;
}

void ChessEngine::update_piece_lists() {
    white_pieces.clear();
    black_pieces.clear();
    
    for (uint8_t row = 0; row < 8; ++row) {
        for (uint8_t col = 0; col < 8; ++col) {
            uint32_t piece_data = state.board[row][col];
            if (piece_data != 0) {
                Piece piece(row, col, piece_data);
                if (piece.is_white()) {
                    white_pieces.emplace_back(piece);
                } else {
                    black_pieces.emplace_back(piece);
                }
            }
        }
    }
}

std::vector<Move> ChessEngine::generate_legal_moves() {
    std::vector<Move> moves;
    const auto& pieces = state.white_to_move ? white_pieces : black_pieces;
    
    for (const auto& piece : pieces) {
        uint32_t piece_type = piece.get_type();
        uint32_t abilities = piece.get_abilities();
        
        // Generate moves for the piece's main type
        if (piece_type & PIECE_PAWN) generate_pawn_moves(piece, moves);
        else if (piece_type & PIECE_KNIGHT) generate_knight_moves(piece, moves);
        else if (piece_type & PIECE_BISHOP) generate_bishop_moves(piece, moves);
        else if (piece_type & PIECE_ROOK) generate_rook_moves(piece, moves);
        else if (piece_type & PIECE_QUEEN) generate_queen_moves(piece, moves);
        else if (piece_type & PIECE_KING) generate_king_moves(piece, moves);
        
        // Generate moves for absorbed abilities
        if (abilities & ABILITY_PAWN && !(piece_type & PIECE_PAWN)) generate_pawn_moves(piece, moves);
        if (abilities & ABILITY_KNIGHT && !(piece_type & PIECE_KNIGHT)) generate_knight_moves(piece, moves);
        if (abilities & ABILITY_BISHOP && !(piece_type & PIECE_BISHOP)) generate_bishop_moves(piece, moves);
        if (abilities & ABILITY_ROOK && !(piece_type & PIECE_ROOK)) generate_rook_moves(piece, moves);
        if (abilities & ABILITY_QUEEN && !(piece_type & PIECE_QUEEN)) generate_queen_moves(piece, moves);
        if (abilities & ABILITY_KING && !(piece_type & PIECE_KING)) generate_king_moves(piece, moves);
    }
    // Filter out moves that leave the mover's king in check (use board-only apply/undo)
    std::vector<Move> legal_moves;
    for (const auto& move : moves) {
        MoveUndoBoard undob;
        apply_move_board_only(move, undob);

        // side that JUST moved is the opposite of state.white_to_move (because apply flips)
        bool mover_is_white = !state.white_to_move;
        bool leaves_mover_in_check = is_in_check(mover_is_white);

        if (!leaves_mover_in_check) {
            legal_moves.push_back(move);
        }

        undo_move_board_only(move, undob);
    }
    return legal_moves;
}

int ChessEngine::count_pseudolegal_moves_for_color(bool white) const {
    // quick count: generate pseudolegal moves (no king-safety check), iterate board and
    // for each piece call movement generators but use board-state only.
    int count = 0;
    for (int r=0;r<8;++r) for (int c=0;c<8;++c) {
        uint32_t p = state.board[r][c];
        if (p == 0) continue;
        bool pwhite = (p & IS_WHITE) != 0;
        if (pwhite != white) continue;
        // reuse your existing small generators but adapt them to accept raw board and only count moves
        // For a quick win: call the existing generate_* functions but with a flag `legal_check=false`
        // Implementation detail: create generate_pseudolegal_moves_for_piece(piece, moves_out, only_captures=false)
    }
    return count;
}
void ChessEngine::generate_pseudolegal_captures_for_side(bool white, std::vector<Move>& out) const {
    out.clear();
    std::vector<Move> tmp;
    tmp.reserve(16);
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            uint32_t p = state.board[r][c];
            if (p == 0) continue;
            bool pwhite = (p & IS_WHITE) != 0;
            if (pwhite != white) continue;
            Piece piece(r, c, p);
            tmp.clear();
            // generate into tmp...
            // then filter tmp for captures
        }
    }
}

void ChessEngine::generate_pawn_moves(const Piece& piece, std::vector<Move>& moves) const {
    bool is_white = piece.is_white();
    int direction = is_white ? -1 : 1;  // White moves up (decreasing row)
    int start_row = is_white ? 6 : 1;
    int promotion_row = is_white ? 0 : 7;  // Row where promotion happens
    uint8_t row = piece.row;
    uint8_t col = piece.col;
    
    // Forward moves
    int new_row = row + direction;
    if (is_valid_square(new_row, col) && state.board[new_row][col] == 0) {
        if (new_row == promotion_row) {
            // Generate all promotion moves
            moves.emplace_back(row, col, new_row, col, 4); // Queen promotion
            moves.emplace_back(row, col, new_row, col, 5); // Rook promotion
            moves.emplace_back(row, col, new_row, col, 6); // Bishop promotion
            moves.emplace_back(row, col, new_row, col, 7); // Knight promotion
        } else {
            moves.emplace_back(row, col, new_row, col);
        }
        
        // Double move from starting position (no promotion possible on double move)
        if (row == start_row) {
            new_row = row + 2 * direction;
            if (is_valid_square(new_row, col) && state.board[new_row][col] == 0) {
                moves.emplace_back(row, col, new_row, col);
            }
        }
    }
    
    // Captures
    for (int dc : {-1, 1}) {
        int new_col = col + dc;
        new_row = row + direction;
        if (is_valid_square(new_row, new_col)) {
            uint32_t target = state.board[new_row][new_col];
            if (target != 0 && is_enemy_piece(target, is_white)) {
                if (new_row == promotion_row) {
                    // Generate all promotion captures
                    moves.emplace_back(row, col, new_row, new_col, 4); // Queen promotion
                    moves.emplace_back(row, col, new_row, new_col, 5); // Rook promotion
                    moves.emplace_back(row, col, new_row, new_col, 6); // Bishop promotion
                    moves.emplace_back(row, col, new_row, new_col, 7); // Knight promotion
                } else {
                    moves.emplace_back(row, col, new_row, new_col);
                }
            }
        }
    }
    
    // En passant
    if (state.en_passant_col != -1 && row == state.en_passant_row) {
        int ep_col = state.en_passant_col;
        if (abs(col - ep_col) == 1) {
            moves.emplace_back(row, col, row + direction, ep_col, 1); // flag = 1 for en passant
        }
    }
}

void ChessEngine::generate_knight_moves(const Piece& piece, std::vector<Move>& moves) const {
    static const std::vector<std::pair<int, int>> knight_moves = {
        {-2, -1}, {-2, 1}, {-1, -2}, {-1, 2}, {1, -2}, {1, 2}, {2, -1}, {2, 1}
    };
    
    bool is_white = piece.is_white();
    for (const auto& a : knight_moves) {
        int dr = a.first, dc = a.second;
        int new_row = piece.row + dr;
        int new_col = piece.col + dc;
        if (is_valid_square(new_row, new_col)) {
            uint32_t target = state.board[new_row][new_col];
            if (target == 0 || is_enemy_piece(target, is_white)) {
                moves.emplace_back(piece.row, piece.col, new_row, new_col);
            }
        }
    }
}

void ChessEngine::generate_bishop_moves(const Piece& piece, std::vector<Move>& moves) const {
    static const std::vector<std::pair<int, int>> bishop_dirs = {{-1, -1}, {-1, 1}, {1, -1}, {1, 1}};
    generate_sliding_moves(piece, moves, bishop_dirs);
}

void ChessEngine::generate_rook_moves(const Piece& piece, std::vector<Move>& moves) const {
    static const std::vector<std::pair<int, int>> rook_dirs = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    generate_sliding_moves(piece, moves, rook_dirs);
}

void ChessEngine::generate_queen_moves(const Piece& piece, std::vector<Move>& moves) const {
    static const std::vector<std::pair<int, int>> queen_dirs = {
        {-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1}
    };
    generate_sliding_moves(piece, moves, queen_dirs);
}

void ChessEngine::generate_sliding_moves(const Piece& piece, std::vector<Move>& moves, 
                                        const std::vector<std::pair<int, int>>& directions) const {
    bool is_white = piece.is_white();
    
    for (const auto& a : directions) {
        int dr = a.first, dc = a.second;
        int row = piece.row + dr;
        int col = piece.col + dc;
        
        while (is_valid_square(row, col)) {
            uint32_t target = state.board[row][col];
            if (target == 0) {
                moves.emplace_back(piece.row, piece.col, row, col);
            } else {
                if (is_enemy_piece(target, is_white)) {
                    moves.emplace_back(piece.row, piece.col, row, col);
                }
                break;  // Blocked by piece
            }
            row += dr;
            col += dc;
        }
    }
}

void ChessEngine::generate_king_moves(const Piece& piece, std::vector<Move>& moves) const {
    bool is_white = piece.is_white();
    
    // Regular king moves
    for (int dr = -1; dr <= 1; ++dr) {
        for (int dc = -1; dc <= 1; ++dc) {
            if (dr == 0 && dc == 0) continue;
            int new_row = piece.row + dr;
            int new_col = piece.col + dc;
            if (is_valid_square(new_row, new_col)) {
                uint32_t target = state.board[new_row][new_col];
                if (target == 0 || is_enemy_piece(target, is_white)) {
                    moves.emplace_back(piece.row, piece.col, new_row, new_col);
                }
            }
        }
    }
    
    // Castling
    if (!piece.has_moved() && !is_in_check(is_white)) {
        // Kingside castling
        if (can_castle_kingside(is_white)) {
            moves.emplace_back(piece.row, piece.col, piece.row, piece.col + 2, 2); // flag = 2 for kingside castle
        }
        // Queenside castling
        if (can_castle_queenside(is_white)) {
            moves.emplace_back(piece.row, piece.col, piece.row, piece.col - 2, 3); // flag = 3 for queenside castle
        }
    }
}

bool ChessEngine::can_castle_kingside(bool white) const {
    int row = white ? 7 : 0;
    
    // Check if rook is in place and hasn't moved
    uint32_t rook = state.board[row][7];
    if ((rook & PIECE_ROOK) == 0 || (rook & HAS_MOVED) != 0 || ((rook & IS_WHITE) != 0) != white) {
        return false;
    }
    
    // Check if squares between king and rook are empty
    for (int col = 5; col <= 6; ++col) {
        if (state.board[row][col] != 0) return false;
    }
    
    // Check if king would pass through or end up in check
    for (int col = 4; col <= 6; ++col) {
        if (is_square_attacked(row, col, !white)) return false;
    }
    
    return true;
}

bool ChessEngine::can_castle_queenside(bool white) const {
    int row = white ? 7 : 0;
    
    // Check if rook is in place and hasn't moved
    uint32_t rook = state.board[row][0];
    if ((rook & PIECE_ROOK) == 0 || (rook & HAS_MOVED) != 0 || ((rook & IS_WHITE) != 0) != white) {
        return false;
    }
    
    // Check if squares between king and rook are empty
    for (int col = 1; col <= 3; ++col) {
        if (state.board[row][col] != 0) return false;
    }
    
    // Check if king would pass through or end up in check
    for (int col = 2; col <= 4; ++col) {
        if (is_square_attacked(row, col, !white)) return false;
    }
    
    return true;
}

bool ChessEngine::is_enemy_piece(uint32_t piece_data, bool current_player_white) const {
    return piece_data != 0 && ((piece_data & IS_WHITE) != 0) != current_player_white;
}

bool ChessEngine::is_friendly_piece(uint32_t piece_data, bool current_player_white) const {
    return piece_data != 0 && ((piece_data & IS_WHITE) != 0) == current_player_white;
}

bool ChessEngine::is_square_attacked(uint8_t row, uint8_t col, bool by_white) const {
    const auto& attacking_pieces = by_white ? white_pieces : black_pieces;
    
    for (const auto& piece : attacking_pieces) {
        uint32_t piece_type = piece.get_type();
        uint32_t abilities = piece.get_abilities();
        
        // Check pawn attacks
        if (piece_type & PIECE_PAWN || abilities & ABILITY_PAWN) {
            int direction = by_white ? -1 : 1;
            if (piece.row + direction == row && abs(static_cast<int>(piece.col) - static_cast<int>(col)) == 1) {
                return true;
            }
        }
        
        // Check knight attacks
        if (piece_type & PIECE_KNIGHT || abilities & ABILITY_KNIGHT) {
            int dr = abs(static_cast<int>(piece.row) - static_cast<int>(row));
            int dc = abs(static_cast<int>(piece.col) - static_cast<int>(col));
            if ((dr == 2 && dc == 1) || (dr == 1 && dc == 2)) {
                return true;
            }
        }
        
        // Check bishop/queen diagonal attacks
        if (piece_type & (PIECE_BISHOP | PIECE_QUEEN) || abilities & (ABILITY_BISHOP | ABILITY_QUEEN)) {
            int dr = static_cast<int>(row) - static_cast<int>(piece.row);
            int dc = static_cast<int>(col) - static_cast<int>(piece.col);
            if (abs(dr) == abs(dc) && dr != 0) {
                // Check if path is clear
                int step_r = (dr > 0) ? 1 : -1;
                int step_c = (dc > 0) ? 1 : -1;
                int check_r = piece.row + step_r;
                int check_c = piece.col + step_c;
                bool path_clear = true;
                while (check_r != row || check_c != col) {
                    if (state.board[check_r][check_c] != 0) {
                        path_clear = false;
                        break;
                    }
                    check_r += step_r;
                    check_c += step_c;
                }
                if (path_clear) return true;
            }
        }
        
        // Check rook/queen straight attacks
        if (piece_type & (PIECE_ROOK | PIECE_QUEEN) || abilities & (ABILITY_ROOK | ABILITY_QUEEN)) {
            if (piece.row == row || piece.col == col) {
                // Check if path is clear
                int step_r = (piece.row == row) ? 0 : ((row > piece.row) ? 1 : -1);
                int step_c = (piece.col == col) ? 0 : ((col > piece.col) ? 1 : -1);
                int check_r = piece.row + step_r;
                int check_c = piece.col + step_c;
                bool path_clear = true;
                while (check_r != row || check_c != col) {
                    if (state.board[check_r][check_c] != 0) {
                        path_clear = false;
                        break;
                    }
                    check_r += step_r;
                    check_c += step_c;
                }
                if (path_clear) return true;
            }
        }
        
        // Check king attacks
        if (piece_type & PIECE_KING || abilities & ABILITY_KING) {
            int dr = abs(static_cast<int>(piece.row) - static_cast<int>(row));
            int dc = abs(static_cast<int>(piece.col) - static_cast<int>(col));
            if (dr <= 1 && dc <= 1 && (dr != 0 || dc != 0)) {
                return true;
            }
        }
    }
    
    return false;
}

// ------------------------
// is_in_check by scanning board for king pos
// ------------------------
bool ChessEngine::is_in_check(bool white_king) const {
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            uint32_t p = state.board[r][c];
            if (p != 0 && (p & PIECE_KING)) {
                bool pwhite = (p & IS_WHITE) != 0;
                if (pwhite == white_king) {
                    return is_square_attacked_board_based(r, c, !white_king);
                }
            }
        }
    }
    return false;
}

ChessEngine::MoveUndo ChessEngine::apply_move(const Move& move) {
    MoveUndo undo_info;
    undo_info.captured_piece = state.board[move.to_row][move.to_col];
    undo_info.captured_row = move.to_row;
    undo_info.captured_col = move.to_col;
    undo_info.old_en_passant_valid = (state.en_passant_col != -1);
    undo_info.old_en_passant_col = state.en_passant_col;
    undo_info.old_en_passant_row = state.en_passant_row;
    undo_info.old_white_castled = state.white_king_castled;
    undo_info.old_black_castled = state.black_king_castled;
    
    // Store incremental evaluation state
    undo_info.old_eval_cache_valid = eval_cache_valid;
    
    uint32_t& moving_piece = state.board[move.from_row][move.from_col];
    undo_info.original_moving_piece = moving_piece;  // Store original piece for promotions
    
    // Calculate incremental evaluation deltas
    if (eval_cache_valid) {
        undo_info.material_delta = calculate_material_delta(move);
        undo_info.king_safety_delta = calculate_king_safety_delta(move);
        undo_info.mobility_delta = calculate_mobility_delta(move);
        
        // Update cached evaluation incrementally
        cached_material_eval += undo_info.material_delta;
        cached_king_safety_eval += undo_info.king_safety_delta;
        cached_mobility_eval += undo_info.mobility_delta;
    }
    
    // Handle special moves
    if (move.flags == 1) {  // En passant
        state.board[state.en_passant_row][state.en_passant_col] = 0;
    } else if (move.flags == 2) {  // Kingside castle
        uint32_t rook = state.board[move.from_row][7];
        state.board[move.from_row][7] = 0;
        state.board[move.from_row][5] = rook | HAS_MOVED;
        if (state.white_to_move) state.white_king_castled = true;
        else state.black_king_castled = true;
    } else if (move.flags == 3) {  // Queenside castle
        uint32_t rook = state.board[move.from_row][0];
        state.board[move.from_row][0] = 0;
        state.board[move.from_row][3] = rook | HAS_MOVED;
        if (state.white_to_move) state.white_king_castled = true;
        else state.black_king_castled = true;
    } else if (move.flags >= 4 && move.flags <= 7) {  // Promotion
        // Handle pawn promotion - transform the piece
        uint32_t promotion_type;
        if (move.flags == 4) promotion_type = PIECE_QUEEN;
        else if (move.flags == 5) promotion_type = PIECE_ROOK;
        else if (move.flags == 6) promotion_type = PIECE_BISHOP;
        else promotion_type = PIECE_KNIGHT;  // flags == 7
        
        // Keep absorbed abilities and color, but change the piece type
        uint32_t abilities = moving_piece & (ABILITY_PAWN | ABILITY_KNIGHT | ABILITY_BISHOP | ABILITY_ROOK | ABILITY_QUEEN | ABILITY_KING);
        uint32_t color = moving_piece & IS_WHITE;
        moving_piece = promotion_type | abilities | color | HAS_MOVED;
    }
    
    // Handle absorption: when capturing, gain the base type of the captured piece as an ability
    uint32_t captured_piece = state.board[move.to_row][move.to_col];
    if (captured_piece != 0) {
        // Extract the base piece type from the captured piece (not its abilities)
        uint32_t captured_base_type = captured_piece & (PIECE_PAWN | PIECE_KNIGHT | PIECE_BISHOP | PIECE_ROOK | PIECE_QUEEN | PIECE_KING);
        
        // Convert base piece type to corresponding ability
        uint32_t gained_ability = 0;
        if (captured_base_type == PIECE_PAWN) gained_ability = ABILITY_PAWN;
        else if (captured_base_type == PIECE_KNIGHT) gained_ability = ABILITY_KNIGHT;
        else if (captured_base_type == PIECE_BISHOP) gained_ability = ABILITY_BISHOP;
        else if (captured_base_type == PIECE_ROOK) gained_ability = ABILITY_ROOK;
        else if (captured_base_type == PIECE_QUEEN) gained_ability = ABILITY_QUEEN;
        else if (captured_base_type == PIECE_KING) gained_ability = ABILITY_KING;
        
        // Add the gained ability to the moving piece
        if (gained_ability != 0) {
            moving_piece |= gained_ability;
        }
    }
    
    // Move the piece
    state.board[move.to_row][move.to_col] = moving_piece | HAS_MOVED;
    state.board[move.from_row][move.from_col] = 0;
    
    // Update en passant
    state.en_passant_col = -1;
    state.en_passant_row = -1;
    if ((moving_piece & PIECE_PAWN) && abs(move.to_row - move.from_row) == 2) {
        state.en_passant_col = move.to_col;
        state.en_passant_row = (move.from_row + move.to_row) / 2;
    }
    
    // Switch turn
    state.white_to_move = !state.white_to_move;
    update_piece_lists();
    
    return undo_info;
}

void ChessEngine::undo_move(const Move& move, const MoveUndo& undo_info) {
    // For promotions, restore the original piece (pawn); otherwise restore moved piece
    if (move.flags >= 4 && move.flags <= 7) {  // Promotion
        state.board[move.from_row][move.from_col] = undo_info.original_moving_piece;
    } else {
        // Restore piece positions
        uint32_t moving_piece = state.board[move.to_row][move.to_col];
        moving_piece &= ~HAS_MOVED;  // Remove has_moved flag if it was added by this move
        state.board[move.from_row][move.from_col] = moving_piece;
    }
    state.board[move.to_row][move.to_col] = undo_info.captured_piece;
    
    // Restore special move effects
    if (move.flags == 1) {  // En passant
        // Restore captured pawn
        state.board[undo_info.old_en_passant_row][undo_info.old_en_passant_col] = undo_info.captured_piece;
    } else if (move.flags == 2) {  // Kingside castle
        uint32_t rook = state.board[move.from_row][5];
        state.board[move.from_row][5] = 0;
        state.board[move.from_row][7] = rook & ~HAS_MOVED;
    } else if (move.flags == 3) {  // Queenside castle
        uint32_t rook = state.board[move.from_row][3];
        state.board[move.from_row][3] = 0;
        state.board[move.from_row][0] = rook & ~HAS_MOVED;
    }
    
    // Restore game state
    state.en_passant_col = undo_info.old_en_passant_col;
    state.en_passant_row = undo_info.old_en_passant_row;
    state.white_king_castled = undo_info.old_white_castled;
    state.black_king_castled = undo_info.old_black_castled;
    state.white_to_move = !state.white_to_move;
    
    // Restore incremental evaluation cache
    if (undo_info.old_eval_cache_valid) {
        cached_material_eval -= undo_info.material_delta;
        cached_king_safety_eval -= undo_info.king_safety_delta;
        cached_mobility_eval -= undo_info.mobility_delta;
        eval_cache_valid = true;
    } else {
        eval_cache_valid = false;
    }
    
    update_piece_lists();
}

// Calculate piece value based on unique abilities (avoiding duplicates)
int ChessEngine::calculate_piece_ability_value(uint32_t piece) const {
    if (piece == 0) return 0;
    
    uint32_t type = piece & (PIECE_PAWN | PIECE_KNIGHT | PIECE_BISHOP | PIECE_ROOK | PIECE_QUEEN | PIECE_KING);
    uint32_t abilities = piece & (ABILITY_PAWN | ABILITY_KNIGHT | ABILITY_BISHOP | ABILITY_ROOK | ABILITY_QUEEN | ABILITY_KING);
    
    int total_value = 0;
    bool has_rook_ability = false;
    bool has_bishop_ability = false;
    bool has_queen_ability = false;
    
    // Check what abilities this piece has
    if (type & PIECE_ROOK || abilities & ABILITY_ROOK) has_rook_ability = true;
    if (type & PIECE_BISHOP || abilities & ABILITY_BISHOP) has_bishop_ability = true;
    if (type & PIECE_QUEEN || abilities & ABILITY_QUEEN) has_queen_ability = true;
    
    // Base piece value (primary type)
    if (type & PIECE_PAWN) total_value += 100;
    else if (type & PIECE_KNIGHT) total_value += 320;
    else if (type & PIECE_BISHOP) total_value += 330;
    else if (type & PIECE_ROOK) total_value += 500;
    else if (type & PIECE_QUEEN) total_value += 900;
    else if (type & PIECE_KING) total_value += 20000;
    
    // Add unique ability values (avoid duplicates)
    if (has_queen_ability) {
        // Queen already includes rook and bishop, so add queen value only
        if (!(type & PIECE_QUEEN)) {
            total_value += 900; // Bonus for gaining queen ability
            if((abilities & ABILITY_ROOK)){
                total_value -= 500; 
            }
            if((abilities & PIECE_BISHOP)){
                total_value -= 330; 
            }
        }
    } else {
        // Add individual abilities if no queen ability
        if (has_rook_ability && !(type & PIECE_ROOK) && !(abilities & ABILITY_QUEEN)) {
            total_value += 500; // Half rook value for gaining rook ability
        }
        if (has_bishop_ability && !(type & PIECE_BISHOP) && !(abilities & ABILITY_QUEEN)) {
            total_value += 330; // Half bishop value for gaining bishop ability
        }
    }
    
    // Other abilities
    if (abilities & ABILITY_KNIGHT && !(type & PIECE_KNIGHT)) {
        total_value += 320; // Half knight value
    }
    if (abilities & ABILITY_PAWN && !(type & PIECE_PAWN)) {
        if(abilities & (ABILITY_QUEEN) || (has_bishop_ability && has_rook_ability)){ 
            total_value += 10; // Reduced value if already has major piece abilities
        } else {
            total_value += 100; // Full pawn ability value
        }
    
        
    }
    return total_value;
}

// Calculate value gained/lost from a capture considering ability changes
int ChessEngine::calculate_capture_value(const Move& move) const {
    uint32_t attacker = state.board[move.from_row][move.from_col];
    uint32_t victim = state.board[move.to_row][move.to_col];
    
    if (victim == 0) return 0; // Not a capture
    
    // Value of piece being captured
    int victim_value = calculate_piece_ability_value(victim);
    
    // Check if attacker gains abilities from victim
    uint32_t victim_abilities = victim & (ABILITY_PAWN | ABILITY_KNIGHT | ABILITY_BISHOP | ABILITY_ROOK | ABILITY_QUEEN | ABILITY_KING);
    uint32_t new_attacker = attacker | victim_abilities;
    
    int attacker_old_value = calculate_piece_ability_value(attacker);
    int attacker_new_value = calculate_piece_ability_value(new_attacker);
    int ability_gain = attacker_new_value - attacker_old_value;
    
    // Total capture value = victim value + ability gain
    return victim_value + ability_gain;
}

// Calculate value of a promotion considering ability transformations
int ChessEngine::calculate_promotion_value(const Move& move, uint32_t promotion_type) const {
    uint32_t pawn = state.board[move.from_row][move.from_col];
    if (!(pawn & PIECE_PAWN)) return 0; // Not a pawn
    
    // Pawn loses its pawn abilities but gains new piece type + retains absorbed abilities
    uint32_t pawn_abilities = pawn & (ABILITY_KNIGHT | ABILITY_BISHOP | ABILITY_ROOK | ABILITY_QUEEN | ABILITY_KING);
    uint32_t new_piece = promotion_type | pawn_abilities | (pawn & IS_WHITE) | HAS_MOVED;
    
    int old_value = calculate_piece_ability_value(pawn);
    int new_value = calculate_piece_ability_value(new_piece);
    
    return new_value - old_value;
}



int ChessEngine::evaluate_position() const {
    // Use incremental evaluation when cache is valid
    update_eval_cache();  // Ensure cache is populated
    
    int material_score = cached_material_eval;
    int king_safety_score = cached_king_safety_eval;
    int mobility_score = cached_mobility_eval;  // Now using cached mobility
    
    int total_score = material_score + mobility_score + king_safety_score;
    
    // For proper negamax, return evaluation from current player's perspective
    if (state.white_to_move) {
        return total_score;  // White's perspective
    } else {
        return -total_score;  // Black's perspective (negate White's perspective)
    }
}

int ChessEngine::get_evaluation() {
    int eval = evaluate_position();
    if (state.white_to_move) {
        return eval; // From White's perspective
    } else {
        return -eval; // From Black's perspective
    }
}
int ChessEngine::evaluate_material() const {
    int score = 0;
    
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            uint32_t piece = state.board[row][col];
            if (piece == 0) continue;
            
            // Use the same ability-based calculation as calculate_piece_ability_value
            int piece_value = calculate_piece_ability_value(piece);
            
            if (piece & IS_WHITE) {
                score += piece_value;
            } else {
                score -= piece_value;
            }
        }
    }
    
    return score;
}

// ------------------------
// Modified evaluate_mobility (use pseudolegal counts, no state toggle)
// ------------------------
int ChessEngine::evaluate_mobility() const {
    int white_moves = count_pseudolegal_moves_for_color(true);
    int black_moves = count_pseudolegal_moves_for_color(false);
    return (white_moves - black_moves) * 3;
}

int ChessEngine::evaluate_king_safety() const {
    int score = 0;
    
    // Penalty for being in check (reduced from 200 to 100)
    if (is_in_check(true)) {
        score -= 100;  // Penalty for White king in check
    }
    if (is_in_check(false)) {
        score += 100;  // Penalty for Black king in check (positive for White's perspective)
    }
    
    // King ability bonus - kings with more abilities are safer (×5 multiplier)
    for (const auto& piece : white_pieces) {
        if (piece.get_type() & PIECE_KING) {
            uint32_t king_piece = state.board[piece.row][piece.col];
            int king_ability_value = calculate_piece_ability_value(king_piece);
            int base_king_value = 100;  // Base king value without abilities
            int ability_bonus = (king_ability_value - base_king_value) * 5;  // ×5 multiplier for safety
            score += ability_bonus;
            break;
        }
    }
    
    for (const auto& piece : black_pieces) {
        if (piece.get_type() & PIECE_KING) {
            uint32_t king_piece = state.board[piece.row][piece.col];
            int king_ability_value = calculate_piece_ability_value(king_piece);
            int base_king_value = 100;  // Base king value without abilities
            int ability_bonus = (king_ability_value - base_king_value) * 5;  // ×5 multiplier for safety
            score -= ability_bonus;  // Negative for black (positive for White's perspective)
            break;
        }
    }
    
    // Castling bonus
    if (state.white_king_castled) {
        score += 80;
    }
    else {
        // Check if white can still castle
        bool can_castle = false;
        //if already castled, return more bonus so we don't penalize losing castling right after castling
        for (const auto& piece : white_pieces) {
            if ((piece.get_type() & PIECE_KING) && !piece.has_moved()) {
                if (can_castle_kingside(true) || can_castle_queenside(true)) {
                    can_castle = true;
                    break;
                }
            }
        }
        if (can_castle) score += 30;
    }
    
    if(state.black_king_castled) {
        score -= 80;
    }
    else {
        // Check if black can still castle
        bool can_castle = false;
        for (const auto& piece : black_pieces) {
            if ((piece.get_type() & PIECE_KING) && !piece.has_moved()) {
                if (can_castle_kingside(false) || can_castle_queenside(false)) {
                    can_castle = true;
                    break;
                }
            }
        }
        if (can_castle) score -= 30;
    }
    
    return score;
}

// Incremental evaluation functions for performance optimization
int ChessEngine::calculate_material_delta(const Move& move) const {
    /*
     * Fast material evaluation delta calculation:
     * Case 1: Normal move (piece moves to empty square) -> no material change
     * Case 2: Capture (piece captures enemy) -> add captured piece value
     * Case 3: Promotion (pawn becomes another piece) -> add promotion value
     * Case 4: Absorption (capture with ability gain) -> add captured value + ability bonus
     * 
     * This avoids rescanning all 64 squares -> O(1) instead of O(64)
     */
    int delta = 0;
    uint32_t moving_piece = state.board[move.from_row][move.from_col];
    uint32_t captured_piece = state.board[move.to_row][move.to_col];
    bool is_white_move = (moving_piece & IS_WHITE) != 0;
    
    // Case 2: Capture - remove captured piece's value
    if (captured_piece != 0) {
        int captured_value = calculate_piece_ability_value(captured_piece);
        if (captured_piece & IS_WHITE) {
            delta -= captured_value;  // White piece captured
        } else {
            delta += captured_value;  // Black piece captured
        }
        
        // Case 4: Absorption - attacker gains abilities from victim
        uint32_t victim_abilities = captured_piece & (ABILITY_PAWN | ABILITY_KNIGHT | ABILITY_BISHOP | ABILITY_ROOK | ABILITY_QUEEN | ABILITY_KING);
        if (victim_abilities != 0) {
            uint32_t new_attacker = moving_piece | victim_abilities;
            int old_value = calculate_piece_ability_value(moving_piece);
            int new_value = calculate_piece_ability_value(new_attacker);
            int ability_gain = new_value - old_value;
            
            if (is_white_move) {
                delta += ability_gain;  // White gains abilities
            } else {
                delta -= ability_gain;  // Black gains abilities
            }
        }
    }
    
    // Case 3: Promotion - pawn transforms to another piece
    if (move.flags >= 4 && move.flags <= 7) {
        uint32_t promotion_type;
        if (move.flags == 4) promotion_type = PIECE_QUEEN;
        else if (move.flags == 5) promotion_type = PIECE_ROOK;
        else if (move.flags == 6) promotion_type = PIECE_BISHOP;
        else promotion_type = PIECE_KNIGHT;
        
        int old_value = calculate_piece_ability_value(moving_piece);
        uint32_t abilities = moving_piece & (ABILITY_KNIGHT | ABILITY_BISHOP | ABILITY_ROOK | ABILITY_QUEEN | ABILITY_KING);
        uint32_t new_piece = promotion_type | abilities | (moving_piece & IS_WHITE) | HAS_MOVED;
        int new_value = calculate_piece_ability_value(new_piece);
        int promotion_gain = new_value - old_value;
        
        if (is_white_move) {
            delta += promotion_gain;
        } else {
            delta -= promotion_gain;
        }
    }
    
    return delta;
}

int ChessEngine::calculate_king_safety_delta(const Move& move) const {
    /*
     * Fast king safety evaluation delta:
     * - If castling rights change -> adjust castling bonus
     * - If castling move -> add castling bonus
     * - If king moves -> may lose castling rights
     * - If king gains abilities through absorption -> apply ability bonus (×5)
     * - Check status changes are handled in separate check penalty logic
     */
    int delta = 0;
    uint32_t moving_piece = state.board[move.from_row][move.from_col];
    uint32_t captured_piece = state.board[move.to_row][move.to_col];
    bool is_white_move = (moving_piece & IS_WHITE) != 0;
    
    // King ability changes from absorption
    if ((moving_piece & PIECE_KING) && captured_piece != 0) {
        uint32_t victim_abilities = captured_piece & (ABILITY_PAWN | ABILITY_KNIGHT | ABILITY_BISHOP | ABILITY_ROOK | ABILITY_QUEEN | ABILITY_KING);
        if (victim_abilities != 0) {
            uint32_t old_king = moving_piece;
            uint32_t new_king = moving_piece | victim_abilities;
            
            int old_ability_value = calculate_piece_ability_value(old_king);
            int new_ability_value = calculate_piece_ability_value(new_king);
            int base_king_value = 100;  // Base king value without abilities
            
            int old_safety_bonus = (old_ability_value - base_king_value) * 5;
            int new_safety_bonus = (new_ability_value - base_king_value) * 5;
            int safety_delta = new_safety_bonus - old_safety_bonus;
            
            if (is_white_move) {
                delta += safety_delta;  // White king gains safety from abilities
            } else {
                delta -= safety_delta;  // Black king gains safety (negative for White's perspective)
            }
        }
    }
    
    // Castling move bonuses
    if (move.flags == 2 || move.flags == 3) {  // Castling
        if (is_white_move) {
            delta += 80;  // White castled
        } else {
            delta -= 80;  // Black castled
        }
    }
    
    // Loss of castling rights (when king or rook moves for first time)
    if ((moving_piece & PIECE_KING) && !(moving_piece & HAS_MOVED)) {
        // King moving for first time - loses castling rights
        if (is_white_move && !state.white_king_castled) {
            delta -= 30;  // White loses castling potential
        } else if (!is_white_move && !state.black_king_castled) {
            delta += 30;  // Black loses castling potential (positive for White)
        }
    }
    
    // Rook moves from starting position - may lose castling rights
    if ((moving_piece & PIECE_ROOK) && !(moving_piece & HAS_MOVED)) {
        int rook_start_row = is_white_move ? 7 : 0;
        if (move.from_row == rook_start_row && (move.from_col == 0 || move.from_col == 7)) {
            // Rook moving from starting corner
            if (is_white_move && !state.white_king_castled) {
                delta -= 15;  // White loses some castling potential
            } else if (!is_white_move && !state.black_king_castled) {
                delta += 15;  // Black loses some castling potential
            }
        }
    }
    
    return delta;
}

int ChessEngine::calculate_mobility_delta(const Move& move) const {
    /*
     * Fast mobility evaluation delta calculation:
     * Mobility changes are complex to calculate incrementally because:
     * 1. Moving a piece changes its mobility
     * 2. Moving a piece may unblock/block other pieces' mobility
     * 3. Capturing a piece removes its mobility but may open lines for others
     * 
     * For performance, we approximate the delta by calculating:
     * - Change in mobility of the moving piece
     * - Change in mobility due to capture (if any)
     * - Simple approximation of blocking/unblocking effects
     */
    
    int delta = 0;
    uint32_t moving_piece = state.board[move.from_row][move.from_col];
    uint32_t captured_piece = state.board[move.to_row][move.to_col];
    bool is_white_move = (moving_piece & IS_WHITE) != 0;
    
    // Estimate mobility change for the moving piece
    // This is a simplified approximation - exact calculation would require generating all moves
    int piece_mobility_bonus = 0;
    uint32_t piece_type = moving_piece & (PIECE_PAWN | PIECE_KNIGHT | PIECE_BISHOP | PIECE_ROOK | PIECE_QUEEN | PIECE_KING);
    
    // Sliding pieces gain more mobility in center, lose mobility on edges
    if (piece_type & (PIECE_BISHOP | PIECE_ROOK | PIECE_QUEEN)) {
        // Center squares give more mobility
        int from_centrality = std::min((int)move.from_row, 7 - (int)move.from_row) + std::min((int)move.from_col, 7 - (int)move.from_col);
        int to_centrality = std::min((int)move.to_row, 7 - (int)move.to_row) + std::min((int)move.to_col, 7 - (int)move.to_col);
        piece_mobility_bonus = (to_centrality - from_centrality) * 2;
    }
    
    // Knight mobility based on distance from center
    if (piece_type & PIECE_KNIGHT) {
        int from_center_dist = abs(move.from_row - 3) + abs(move.from_col - 3) + abs(move.from_row - 4) + abs(move.from_col - 4);
        int to_center_dist = abs(move.to_row - 3) + abs(move.to_col - 3) + abs(move.to_row - 4) + abs(move.to_col - 4);
        piece_mobility_bonus = (from_center_dist - to_center_dist) * 1;  // Knights prefer center
    }
    
    if (is_white_move) {
        delta += piece_mobility_bonus;
    } else {
        delta -= piece_mobility_bonus;
    }
    
    // If capturing, remove captured piece's mobility
    if (captured_piece != 0) {
        int captured_mobility_loss = 0;
        uint32_t captured_type = captured_piece & (PIECE_PAWN | PIECE_KNIGHT | PIECE_BISHOP | PIECE_ROOK | PIECE_QUEEN | PIECE_KING);
        
        // Estimate lost mobility based on piece type and position
        if (captured_type & PIECE_QUEEN) captured_mobility_loss = 15;
        else if (captured_type & PIECE_ROOK) captured_mobility_loss = 8;
        else if (captured_type & PIECE_BISHOP) captured_mobility_loss = 7;
        else if (captured_type & PIECE_KNIGHT) captured_mobility_loss = 4;
        else if (captured_type & PIECE_PAWN) captured_mobility_loss = 2;
        else if (captured_type & PIECE_KING) captured_mobility_loss = 3;
        
        if (captured_piece & IS_WHITE) {
            delta -= captured_mobility_loss;  // White piece captured
        } else {
            delta += captured_mobility_loss;  // Black piece captured
        }
    }
    
    // Simple approximation: multiply by mobility weight factor (3)
    return delta * 3;
}

void ChessEngine::update_eval_cache() const {
    if (!eval_cache_valid) {
        cached_material_eval = evaluate_material();
        cached_king_safety_eval = evaluate_king_safety();
        cached_mobility_eval = evaluate_mobility();
        eval_cache_valid = true;
    }
}

Move ChessEngine::find_best_move(int depth, int time_limit_ms) {
    auto start_time = std::chrono::steady_clock::now();
    
    std::vector<Move> legal_moves = generate_legal_moves();
    if (legal_moves.empty()) {
        return Move(0, 0, 0, 0);  // No legal moves
    }
    
    // Apply the same advanced move ordering as in minimax
    std::sort(legal_moves.begin(), legal_moves.end(), [&](const Move &a, const Move &b){
        int score_a = 0, score_b = 0;
        
        // 1. Evaluate captures using ability-based system
        score_a += calculate_capture_value(a);
        score_b += calculate_capture_value(b);
        
        // 2. Evaluate promotions
        if (a.flags >= 4 && a.flags <= 7) { // Promotion flags
            uint32_t promotion_type = PIECE_QUEEN; // Default to queen
            if (a.flags == 5) promotion_type = PIECE_ROOK;
            else if (a.flags == 6) promotion_type = PIECE_BISHOP;
            else if (a.flags == 7) promotion_type = PIECE_KNIGHT;
            score_a += calculate_promotion_value(a, promotion_type);
        }
        
        if (b.flags >= 4 && b.flags <= 7) { // Promotion flags
            uint32_t promotion_type = PIECE_QUEEN; // Default to queen
            if (b.flags == 5) promotion_type = PIECE_ROOK;
            else if (b.flags == 6) promotion_type = PIECE_BISHOP;
            else if (b.flags == 7) promotion_type = PIECE_KNIGHT;
            score_b += calculate_promotion_value(b, promotion_type);
        }
        
        // 3. Positional bonuses
        // Center control bonus
        if (a.to_row >= 3 && a.to_row <= 4 && a.to_col >= 3 && a.to_col <= 4) score_a += 30;
        else if (a.to_row >= 2 && a.to_row <= 5 && a.to_col >= 2 && a.to_col <= 5) score_a += 15;
        
        if (b.to_row >= 3 && b.to_row <= 4 && b.to_col >= 3 && b.to_col <= 4) score_b += 30;
        else if (b.to_row >= 2 && b.to_row <= 5 && b.to_col >= 2 && b.to_col <= 5) score_b += 15;
        
        // 4. Penalize moving same piece to same area repeatedly
        // Simple hash based on piece position to break move cycles
        uint32_t piece_a = state.board[a.from_row][a.from_col];
        uint32_t piece_b = state.board[b.from_row][b.from_col];
        
        // Slight randomization to avoid repetitive patterns
        score_a += (piece_a * a.to_row * 7 + a.to_col * 13) % 8;
        score_b += (piece_b * b.to_row * 7 + b.to_col * 13) % 8;
        
        // 5. Castle early bonus
        if (a.flags == 2 || a.flags == 3) score_a += 40; // Castling
        if (b.flags == 2 || b.flags == 3) score_b += 40; // Castling
        
        return score_a > score_b;
    });
    
    Move best_move = legal_moves[0];
    int best_score = INT_MIN;
    
    // With negamax, we don't need to track maximizing/minimizing - evaluation is always from current player's perspective
    for (const auto& move : legal_moves) {
        auto undo_info = apply_move(move);
        int score = -minimax(depth - 1, INT_MIN, INT_MAX, false);  // Negamax: negate opponent's score
        undo_move(move, undo_info);
        
        // No additional perspective adjustment needed with negamax
        if (score > best_score) {
            best_score = score;
            best_move = move;
        }
        
        // Check time limit
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time);
        if (elapsed.count() >= time_limit_ms) {
            break;
        }
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time);
    std::cerr << "[ENGINE] nodes=" << nodes_searched << " qnodes=" << quiescence_nodes << " time_ms=" << elapsed.count() << " nodes/sec=" << (nodes_searched*1000.0/elapsed.count()) << "\n";
    return best_move;
}

int ChessEngine::minimax(int depth, int alpha, int beta, bool maximizing) {
    ++nodes_searched;
    if (depth == 0) {
        return quiescence_search(alpha, beta);
    }
    
    std::vector<Move> moves = generate_legal_moves();
    
    // Advanced move ordering: Consider ability values, captures, and promotions
    std::sort(moves.begin(), moves.end(), [&](const Move &a, const Move &b){
        int score_a = 0, score_b = 0;
        
        // 1. Evaluate captures using ability-based system
        score_a += calculate_capture_value(a);
        score_b += calculate_capture_value(b);
        
        // 2. Evaluate promotions
        if (a.flags >= 4 && a.flags <= 7) { // Promotion flags
            uint32_t promotion_type = PIECE_QUEEN; // Default to queen
            if (a.flags == 5) promotion_type = PIECE_ROOK;
            else if (a.flags == 6) promotion_type = PIECE_BISHOP;
            else if (a.flags == 7) promotion_type = PIECE_KNIGHT;
            score_a += calculate_promotion_value(a, promotion_type);
        }
        
        if (b.flags >= 4 && b.flags <= 7) { // Promotion flags
            uint32_t promotion_type = PIECE_QUEEN; // Default to queen
            if (b.flags == 5) promotion_type = PIECE_ROOK;
            else if (b.flags == 6) promotion_type = PIECE_BISHOP;
            else if (b.flags == 7) promotion_type = PIECE_KNIGHT;
            score_b += calculate_promotion_value(b, promotion_type);
        }
        
        // 3. Positional bonuses
        // Center control bonus
        if (a.to_row >= 3 && a.to_row <= 4 && a.to_col >= 3 && a.to_col <= 4) score_a += 30;
        else if (a.to_row >= 2 && a.to_row <= 5 && a.to_col >= 2 && a.to_col <= 5) score_a += 15;
        
        if (b.to_row >= 3 && b.to_row <= 4 && b.to_col >= 3 && b.to_col <= 4) score_b += 30;
        else if (b.to_row >= 2 && b.to_row <= 5 && b.to_col >= 2 && b.to_col <= 5) score_b += 15;
        
        // 4. Penalize moving same piece to same area repeatedly
        // Simple hash based on piece position to break move cycles
        uint32_t piece_a = state.board[a.from_row][a.from_col];
        uint32_t piece_b = state.board[b.from_row][b.from_col];
        
        // Slight randomization to avoid repetitive patterns
        score_a += (piece_a * a.to_row * 7 + a.to_col * 13) % 8;
        score_b += (piece_b * b.to_row * 7 + b.to_col * 13) % 8;
        
        // 5. Castle early bonus
        if (a.flags == 2 || a.flags == 3) score_a += 40; // Castling
        if (b.flags == 2 || b.flags == 3) score_b += 40; // Castling
        
        return score_a > score_b;
    });
    
    if (moves.empty()) {
        // No legal moves - checkmate or stalemate
        if (is_in_check(state.white_to_move)) {
            return -20000 + depth;  // Checkmate (prefer shorter checkmates)
        } else {
            return 0;  // Stalemate
        }
    }

    // Negamax algorithm - evaluation is always from current player's perspective
    int best = INT_MIN;
    for (const auto& move : moves) {
        auto undo_info = apply_move(move);  // Use consistent apply/undo throughout
        int score = -minimax(depth - 1, -beta, -alpha, false);  // Pure negamax recursion
        undo_move(move, undo_info);
        
        best = std::max(best, score);
        alpha = std::max(alpha, score);
        if (alpha >= beta) {
            break;  // Alpha-beta cutoff
        }
    }
    return best;
}
// ------------------------
// Modified quiescence_search (uses board-only apply/undo and pseudolegal captures)
// ------------------------
int ChessEngine::quiescence_search(int alpha, int beta) {
    ++quiescence_nodes;
    int stand_pat = evaluate_position();

    if (stand_pat >= beta) return beta;
    if (alpha < stand_pat) alpha = stand_pat;

    std::vector<Move> captures;
    generate_pseudolegal_captures_for_side(state.white_to_move, captures);

    for (const auto& move : captures) {
        MoveUndoBoard undob;
        apply_move_board_only(move, undob);

        int score = -quiescence_search(-beta, -alpha);

        undo_move_board_only(move, undob);

        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }

    return alpha;
}



std::vector<std::vector<uint32_t>> ChessEngine::get_board_state() const {
    std::vector<std::vector<uint32_t>> board(8, std::vector<uint32_t>(8));
    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 8; ++j) {
            board[i][j] = state.board[i][j];
        }
    }
    return board;
}

bool ChessEngine::is_game_over() const {
    return is_checkmate() || is_stalemate();
}

bool ChessEngine::is_checkmate() const {
    return const_cast<ChessEngine*>(this)->generate_legal_moves().empty() && is_in_check(state.white_to_move);
}

bool ChessEngine::is_stalemate() const {
    return const_cast<ChessEngine*>(this)->generate_legal_moves().empty() && !is_in_check(state.white_to_move);
}