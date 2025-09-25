/*
  Absorb Chess Stockfish WASM Wrapper
  A wrapper around the modified Stockfish engine for absorb chess
  that provides a JavaScript interface compatible with the original chess engine
*/

#include <emscripten/emscripten.h>
#include <emscripten/bind.h>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <chrono>
#include <functional>

#include "position.h"
#include "search.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"
#include "movegen.h"
#include "evaluate.h"

using namespace emscripten;

class WasmChessEngine {
private:
    Position pos;
    StateListPtr states;
    Thread* mainThread;
    bool initialized;
    
    // Convert frontend coordinates (0-7, top-left origin) to Stockfish squares
    Square frontendToSquare(int row, int col) {
        // Frontend: row 0 is top (rank 8), col 0 is left (file a)
        // Stockfish: Square enumeration starts from a1 (bottom-left)
        int stockfish_rank = 7 - row;  // Flip row: 0->7, 1->6, ..., 7->0
        int stockfish_file = col;      // File stays the same: 0->a, 1->b, ..., 7->h
        return Square(stockfish_rank * 8 + stockfish_file);
    }
    
    // Convert Stockfish square to frontend coordinates
    void squareToFrontend(Square sq, int& row, int& col) {
        int stockfish_rank = sq / 8;
        int stockfish_file = sq % 8;
        row = 7 - stockfish_rank;  // Flip rank back
        col = stockfish_file;
    }
    
    // Convert frontend piece encoding to Stockfish piece
    Piece frontendPieceToStockfish(uint32_t frontend_piece) {
        if (frontend_piece == 0) return NO_PIECE;
        
        // Frontend encoding:
        // Bits 0-5: Base piece type flags (PIECE_PAWN=1, PIECE_KNIGHT=2, etc.)
        // Bits 6-11: Ability flags (ABILITY_PAWN=64, ABILITY_KNIGHT=128, etc.)
        // Bit 12: HAS_MOVED flag (4096)
        // Bit 13: IS_WHITE flag (8192)
        
        bool isWhite = frontend_piece & 8192; // Check bit 13
        Color color = isWhite ? WHITE : BLACK;
        
        // Extract base piece type from bits 0-5
        uint32_t baseTypeBits = frontend_piece & 63; // Mask bits 0-5
        PieceType pieceType;
        
        if (baseTypeBits & 1) pieceType = PAWN;      // PIECE_PAWN = 1
        else if (baseTypeBits & 2) pieceType = KNIGHT;  // PIECE_KNIGHT = 2
        else if (baseTypeBits & 4) pieceType = BISHOP;  // PIECE_BISHOP = 4
        else if (baseTypeBits & 8) pieceType = ROOK;    // PIECE_ROOK = 8
        else if (baseTypeBits & 16) pieceType = QUEEN;  // PIECE_QUEEN = 16
        else if (baseTypeBits & 32) pieceType = KING;   // PIECE_KING = 32
        else return NO_PIECE;
        
        return make_piece(color, pieceType);
    }
    
    // Extract abilities from frontend piece encoding
    Abilities frontendPieceToAbilities(uint32_t frontend_piece) {
        // Frontend encoding uses bits at specific positions:
        // ABILITY_PAWN = 64 (bit 6), ABILITY_KNIGHT = 128 (bit 7), etc.
        
        Abilities abilities = 0;
        if (frontend_piece & 64) abilities |= ABILITY_PAWN;      // Frontend bit 6 -> Stockfish bit 0
        if (frontend_piece & 128) abilities |= ABILITY_KNIGHT;   // Frontend bit 7 -> Stockfish bit 1  
        if (frontend_piece & 256) abilities |= ABILITY_BISHOP;   // Frontend bit 8 -> Stockfish bit 2
        if (frontend_piece & 512) abilities |= ABILITY_ROOK;     // Frontend bit 9 -> Stockfish bit 3
        if (frontend_piece & 1024) abilities |= ABILITY_QUEEN;   // Frontend bit 10 -> Stockfish bit 4
        if (frontend_piece & 2048) abilities |= ABILITY_KING;    // Frontend bit 11 -> Stockfish bit 5
        
        return abilities;
    }
    
public:
    WasmChessEngine() : initialized(false) {
        try {
            EM_ASM(console.log("🚀 [WASM] Starting WasmChessEngine constructor..."));
            
            EM_ASM(console.log("⏳ [WASM] Step 1: UCI::init(Options)..."));
            UCI::init(Options);
            EM_ASM(console.log("✅ [WASM] Step 1 completed: UCI::init"));
            
            EM_ASM(console.log("⏳ [WASM] Step 2: AbsorbChess::init_tables()..."));
            AbsorbChess::init_tables();  // Initialize our lookup tables
            EM_ASM(console.log("✅ [WASM] Step 2 completed: AbsorbChess::init_tables"));
            
            EM_ASM(console.log("⏳ [WASM] Step 3: Bitboards::init()..."));
            Bitboards::init();
            EM_ASM(console.log("✅ [WASM] Step 3 completed: Bitboards::init"));
            
            EM_ASM(console.log("⏳ [WASM] Step 4: Position::init()..."));
            Position::init();
            EM_ASM(console.log("✅ [WASM] Step 4 completed: Position::init"));
            
            EM_ASM(console.log("⏳ [WASM] Step 5: Threads (disabled in WASM single-thread)..."));
            // Threads are disabled in this WASM build; we'll run searches synchronously
            EM_ASM(console.log("✅ [WASM] Step 5 completed: Threads disabled"));
            
            EM_ASM(console.log("⏳ [WASM] Step 6: Search::clear()..."));
            Search::clear();
            EM_ASM(console.log("✅ [WASM] Step 6 completed: Search::clear"));
            
            EM_ASM(console.log("⏳ [WASM] Step 7: TT.resize(16)..."));
            TT.resize(16);  // Small hash table for WASM
            EM_ASM(console.log("✅ [WASM] Step 7 completed: TT.resize"));
            
            EM_ASM(console.log("⏳ [WASM] Step 8: Threads.main() (skipped in WASM single-thread)..."));
            mainThread = nullptr;
            EM_ASM(console.log("✅ [WASM] Step 8 completed: mainThread set to nullptr"));
            
            EM_ASM(console.log("⏳ [WASM] Step 9: Creating states..."));
            states = StateListPtr(new std::deque<StateInfo>(1));
            EM_ASM(console.log("✅ [WASM] Step 9 completed: states created"));
            
            EM_ASM(console.log("⏳ [WASM] Step 10: Setting position..."));
            // Use standard starting position FEN
            const std::string startingFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
            pos.set(startingFEN, false, &states->back(), nullptr);
            EM_ASM(console.log("✅ [WASM] Step 10 completed: position set"));
            
            initialized = true;
            EM_ASM(console.log("🎉 [WASM] Constructor completed successfully!"));
        } catch (const std::exception& e) {
            EM_ASM({
                console.error("❌ [WASM] Constructor failed with exception: " + UTF8ToString($0));
            }, e.what());
            initialized = false;
        } catch (...) {
            EM_ASM(console.error("❌ [WASM] Constructor failed with unknown exception"));
            initialized = false;
        }
    }
    
    ~WasmChessEngine() {
        if (initialized) {
            // No threads to tear down in single-threaded WASM build
        }
    }
    
    // Set board state from JavaScript 2D array (matching original API)
    bool setBoardState(const val& js_board, bool white_to_move) {
        return setBoardState(js_board, white_to_move, false, false, -1, -1);
    }
    
    bool setBoardState(const val& js_board, bool white_to_move, 
                      bool white_king_castled, bool black_king_castled,
                      int en_passant_col, int en_passant_row) {
        if (!initialized) return false;
        
        try {
            // Create FEN string from the board
            std::string fen = createFENFromBoard(js_board, white_to_move, 
                                               white_king_castled, black_king_castled,
                                               en_passant_col, en_passant_row);
            
            // Create new state and set position
            states = StateListPtr(new std::deque<StateInfo>(1));
            pos.set(fen, false, &states->back(), nullptr);
            
            // Apply absorbed abilities after setting position
            applyAbilitiesFromBoard(js_board);
            
            return true;
        } catch (...) {
            return false;
        }
    }
    
private:
    // Helper function to create FEN string from frontend board
    std::string createFENFromBoard(const val& js_board, bool white_to_move,
                                  bool white_king_castled, bool black_king_castled,
                                  int en_passant_col, int en_passant_row) {
        std::ostringstream fen;
        
        // Board position (ranks 8 to 1)
        for (int row = 0; row < 8; row++) {
            val js_row = js_board[row];
            int empty_count = 0;
            
            for (int col = 0; col < 8; col++) {
                uint32_t frontend_piece = js_row[col].as<uint32_t>();
                
                if (frontend_piece == 0) {
                    empty_count++;
                } else {
                    if (empty_count > 0) {
                        fen << empty_count;
                        empty_count = 0;
                    }
                    
                    // Convert to FEN piece character
                    char piece_char = frontendPieceToFENChar(frontend_piece);
                    fen << piece_char;
                }
            }
            
            if (empty_count > 0) {
                fen << empty_count;
            }
            
            if (row < 7) fen << "/";
        }
        
        // Side to move
        fen << (white_to_move ? " w " : " b ");
        
        // Castling rights (simplified)
        std::string castling = "";
        if (!white_king_castled) castling += "KQ";
        if (!black_king_castled) castling += "kq";
        if (castling.empty()) castling = "-";
        fen << castling << " ";
        
        // En passant
        if (en_passant_col >= 0 && en_passant_row >= 0) {
            fen << char('a' + en_passant_col) << char('1' + (7 - en_passant_row));
        } else {
            fen << "-";
        }
        
        // Halfmove and fullmove (default values)
        fen << " 0 1";
        
        return fen.str();
    }
    
    // Convert frontend piece to FEN character
    char frontendPieceToFENChar(uint32_t frontend_piece) {
        bool isWhite = frontend_piece & 8192;
        uint32_t baseTypeBits = frontend_piece & 63;
        
        char piece;
        if (baseTypeBits & 1) piece = 'p';      // PAWN
        else if (baseTypeBits & 2) piece = 'n';  // KNIGHT
        else if (baseTypeBits & 4) piece = 'b';  // BISHOP
        else if (baseTypeBits & 8) piece = 'r';  // ROOK
        else if (baseTypeBits & 16) piece = 'q'; // QUEEN
        else if (baseTypeBits & 32) piece = 'k'; // KING
        else return '?';
        
        return isWhite ? toupper(piece) : piece;
    }
    
    // Apply absorbed abilities after position is set
    void applyAbilitiesFromBoard(const val& js_board) {
        for (int row = 0; row < 8; row++) {
            val js_row = js_board[row];
            for (int col = 0; col < 8; col++) {
                uint32_t frontend_piece = js_row[col].as<uint32_t>();
                
                if (frontend_piece != 0) {
                    Abilities abilities = frontendPieceToAbilities(frontend_piece);
                    Square sq = frontendToSquare(row, col);
                    
                    // Set abilities for this piece
                    pos.set_abilities(sq, abilities);
                    
                    // Debug: Log abilities being set
                    if (abilities != 0) {
                        std::cout << "[DEBUG] Setting abilities for square " << sq 
                                  << " (row=" << row << ", col=" << col << ") "
                                  << "abilities=" << abilities << std::endl;
                    }
                }
            }
        }
    }
    
public:
    
    // Find best move (matching original API)
    val findBestMove(int depth, int time_limit_ms) {
        if (!initialized) {
            val result = val::object();
            result.set("from_row", -1);
            result.set("from_col", -1);
            result.set("to_row", -1);
            result.set("to_col", -1);
            result.set("flags", 0);
            result.set("evaluation", 0);
            result.set("time_taken_ms", 0);
            result.set("depth_reached", 0);
            return result;
        }
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        try {
            // Debug: Log position and limits
            std::cout << "[DEBUG] Starting search with position: " << pos.fen() << std::endl;
            std::cout << "[DEBUG] Search limits: depth=" << depth << ", time_limit_ms=" << time_limit_ms << std::endl;
            
            // Validate position before search
            if (!pos.pos_is_ok()) {
                std::cout << "[ERROR] Position is invalid, cannot search" << std::endl;
                val result = val::object();
                result.set("from_row", -1);
                result.set("from_col", -1);
                result.set("to_row", -1);
                result.set("to_col", -1);
                result.set("flags", 0);
                result.set("evaluation", 0);
                result.set("time_taken_ms", 0);
                result.set("depth_reached", 0);
                return result;
            }
            // Single-threaded fallback: simple evaluation
            Move best_move = MOVE_NONE;
            Value best_score = -VALUE_INFINITE;
            Color us = pos.side_to_move();
            int considered = 0;
            
            // Debug: Check if any pieces have abilities (but limit output)
            std::cout << "[DEBUG] Checking for pieces with abilities..." << std::endl;
            int ability_count = 0;
            for (Square sq = SQ_A1; sq <= SQ_H8 && ability_count < 5; ++sq) {
                if (pos.piece_on(sq) != NO_PIECE) {
                    for (PieceType pt = PAWN; pt <= KING; ++pt) {
                        if (pos.has_ability(sq, pt)) {
                            std::cout << "[DEBUG] Square " << sq << " has " << pt << " ability" << std::endl;
                            ability_count++;
                            if (ability_count >= 5) break;
                        }
                    }
                }
            }
            
            // Simple 1-ply evaluation
            for (const ExtMove& em : MoveList<LEGAL>(pos)) {
                StateInfo st;
                pos.do_move(em.move, st);
                Value sc = Eval::evaluate(pos);
                // Flip score if it's the opponent's turn after the move
                if (pos.side_to_move() != us)
                    sc = -sc;
                pos.undo_move(em.move);
                
                if (sc > best_score) {
                    best_score = sc;
                    best_move = em.move;
                }
                ++considered;
            }
            
            std::cout << "[DEBUG] Considered " << considered << " moves, best_score: " << best_score << std::endl;
            
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            
            val result = val::object();
            
            if (best_move != MOVE_NONE) {
                // Convert move to frontend coordinates
                int from_row, from_col, to_row, to_col;
                squareToFrontend(from_sq(best_move), from_row, from_col);
                squareToFrontend(to_sq(best_move), to_row, to_col);
                
                result.set("from_row", from_row);
                result.set("from_col", from_col);
                result.set("to_row", to_row);
                result.set("to_col", to_col);
                result.set("flags", type_of(best_move));
                result.set("evaluation", int(best_score));
                result.set("time_taken_ms", int(duration.count()));
                result.set("depth_reached", 1);
            } else {
                result.set("from_row", -1);
                result.set("from_col", -1);
                result.set("to_row", -1);
                result.set("to_col", -1);
                result.set("flags", 0);
                result.set("evaluation", 0);
                result.set("time_taken_ms", int(duration.count()));
                result.set("depth_reached", 0);
            }
            
            // Handle promotion
            if (best_move != MOVE_NONE && type_of(best_move) == PROMOTION) {
                PieceType promotion_piece = promotion_type(best_move);
                std::string promotion_str;
                switch (promotion_piece) {
                    case QUEEN: promotion_str = "Q"; break;
                    case ROOK: promotion_str = "R"; break;
                    case BISHOP: promotion_str = "B"; break;
                    case KNIGHT: promotion_str = "N"; break;
                    default: promotion_str = "Q"; break;
                }
                result.set("promotion_piece", promotion_str);
            }
            
            return result;
            
        } catch (...) {
            val result = val::object();
            result.set("from_row", -1);
            result.set("from_col", -1);
            result.set("to_row", -1);
            result.set("to_col", -1);
            result.set("flags", 0);
            result.set("evaluation", 0);
            result.set("time_taken_ms", 0);
            result.set("depth_reached", 0);
            return result;
        }
    }
        
    
    
    // Get current position as FEN
    std::string getFEN() const {
        return pos.fen();
    }
    
    // Make a move given in UCI format (e.g., "e2e4")
    bool makeMove(const std::string& moveStr) {
        for (const auto& move : MoveList<LEGAL>(pos)) {
            if (UCI::move(move.move, pos.is_chess960()) == moveStr) {
                states->emplace_back();
                pos.do_move(move.move, states->back());
                return true;
            }
        }
        return false;
    }
    
    // Get the best move using search (WASM-safe single-threaded version)
    std::string getBestMove(int depth = 5) {
        if (!initialized) return "";
        
        try {
            // WASM-safe single-threaded search
            Move best_move = MOVE_NONE;
            Value best_score = -VALUE_INFINITE;
            
            for (const ExtMove& em : MoveList<LEGAL>(pos)) {
                StateInfo st;
                pos.do_move(em.move, st);
                Value sc = -Eval::evaluate(pos); // Negate since opponent to move after our move
                pos.undo_move(em.move);
                
                if (sc > best_score) {
                    best_score = sc;
                    best_move = em.move;
                }
            }
            
            return best_move != MOVE_NONE ? UCI::move(best_move, pos.is_chess960()) : "";
        } catch (...) {
            return "";
        }
    }
    
    // Evaluate current position
    int evaluate() const {
        return Eval::evaluate(pos);
    }
    
    // Check if current position is check/checkmate/stalemate
    bool isInCheck() const {
        return pos.checkers();
    }
    
    bool isCheckmate() const {
        return pos.checkers() && MoveList<LEGAL>(pos).size() == 0;
    }
    
    bool isStalemate() const {
        return !pos.checkers() && MoveList<LEGAL>(pos).size() == 0;
    }
    
    // Get piece at square (returns empty string if no piece)
    std::string getPieceAt(const std::string& square) const {
        if (square.length() != 2) return "";
        
        File file = File(square[0] - 'a');
        Rank rank = Rank(square[1] - '1');
        
        if (file < FILE_A || file > FILE_H || rank < RANK_1 || rank > RANK_8)
            return "";
            
        Square sq = make_square(file, rank);
        Piece piece = pos.piece_on(sq);
        
        if (piece == NO_PIECE) return "";
        
        // Convert piece to string representation
        std::string pieceStr;
        PieceType pt = type_of(piece);
        Color c = color_of(piece);
        
        switch (pt) {
            case PAWN: pieceStr = "p"; break;
            case KNIGHT: pieceStr = "n"; break;
            case BISHOP: pieceStr = "b"; break;
            case ROOK: pieceStr = "r"; break;
            case QUEEN: pieceStr = "q"; break;
            case KING: pieceStr = "k"; break;
            default: return "";
        }
        
        if (c == WHITE) {
            pieceStr[0] = toupper(pieceStr[0]);
        }
        
        return pieceStr;
    }
    
    // Absorb Chess: Get abilities of piece at square
    std::vector<std::string> getAbilitiesAt(const std::string& square) const {
        std::vector<std::string> abilities;
        
        if (square.length() != 2) return abilities;
        
        File file = File(square[0] - 'a');
        Rank rank = Rank(square[1] - '1');
        
        if (file < FILE_A || file > FILE_H || rank < RANK_1 || rank > RANK_8)
            return abilities;
            
        Square sq = make_square(file, rank);
        
        // Check each ability type
        for (PieceType pt = PAWN; pt <= KING; ++pt) {
            if (pos.has_ability(sq, pt)) {
                switch (pt) {
                    case PAWN: abilities.push_back("pawn"); break;
                    case KNIGHT: abilities.push_back("knight"); break;
                    case BISHOP: abilities.push_back("bishop"); break;
                    case ROOK: abilities.push_back("rook"); break;
                    case QUEEN: abilities.push_back("queen"); break;
                    case KING: abilities.push_back("king"); break;
                    default: break; // Handle NO_PIECE_TYPE and PIECE_TYPE_NB
                }
            }
        }
        
        return abilities;
    }
    
    // Missing methods to match API
    val getLegalMoves() {
        if (!initialized) {
            return val::array();
        }
        
        try {
            // Debug: Print MoveType enum values
            std::cout << "[DEBUG] MoveType enum values - NORMAL: " << static_cast<int>(NORMAL) 
                      << ", PROMOTION: " << static_cast<int>(PROMOTION) 
                      << ", ENPASSANT: " << static_cast<int>(ENPASSANT) 
                      << ", CASTLING: " << static_cast<int>(CASTLING) << std::endl;
            
            val js_moves = val::array();
            int index = 0;
            
            for (const ExtMove& move : MoveList<LEGAL>(pos)) {
                Move m = move.move;
                
                val js_move = val::object();
                
                // Convert to frontend coordinates
                int from_row, from_col, to_row, to_col;
                squareToFrontend(from_sq(m), from_row, from_col);
                squareToFrontend(to_sq(m), to_row, to_col);
                
                js_move.set("from_row", from_row);
                js_move.set("from_col", from_col);
                js_move.set("to_row", to_row);
                js_move.set("to_col", to_col);
                
                int move_flags = static_cast<int>(type_of(m));
                js_move.set("flags", move_flags);
                
                // Debug: Log castling moves specifically
                if (from_row == 7 && from_col == 4 && (to_col == 2 || to_col == 6)) {
                    std::cout << "[DEBUG] Potential castling move: [" << from_row << "," << from_col 
                              << "] -> [" << to_row << "," << to_col << "] flags: " << move_flags 
                              << " raw_move: " << m << std::endl;
                }
                
                js_moves.set(index++, js_move);
            }
            
            return js_moves;
        } catch (...) {
            return val::array();
        }
    }
    
    int getEvaluation() {
        if (!initialized) return 0;
        return evaluate();
    }
    
    bool applyMove(int from_row, int from_col, int to_row, int to_col, int flags = 0) {
        if (!initialized) return false;
        
        try {
            // Convert frontend coordinates to UCI format
            std::string from_square = std::string(1, 'a' + from_col) + std::string(1, '1' + (7 - from_row));
            std::string to_square = std::string(1, 'a' + to_col) + std::string(1, '1' + (7 - to_row));
            std::string move_str = from_square + to_square;
            
            return makeMove(move_str);
        } catch (...) {
            return false;
        }
    }
    
    val getAbilitiesAtCoords(int row, int col) {
        if (!initialized) {
            return val::array();
        }
        
        try {
            std::string square = std::string(1, 'a' + col) + std::string(1, '1' + (7 - row));
            std::vector<std::string> abilities = getAbilitiesAt(square);
            
            val js_abilities = val::array();
            for (size_t i = 0; i < abilities.size(); ++i) {
                js_abilities.set(i, abilities[i]);
            }
            
            return js_abilities;
        } catch (...) {
            return val::array();
        }
    }
    
    // Get board state as JSON-like string for debugging
    std::string getBoardState() const {
        std::ostringstream oss;
        oss << "{";
        oss << "\"fen\":\"" << pos.fen() << "\",";
        oss << "\"turn\":\"" << (pos.side_to_move() == WHITE ? "white" : "black") << "\",";
        oss << "\"inCheck\":" << (pos.checkers() ? "true" : "false") << ",";
        oss << "\"isCheckmate\":" << (isCheckmate() ? "true" : "false") << ",";
        oss << "\"isStalemate\":" << (isStalemate() ? "true" : "false") << ",";
        oss << "\"evaluation\":" << evaluate();
        oss << "}";
        return oss.str();
    }
};

// Bind the class to JavaScript (matching original API)
EMSCRIPTEN_BINDINGS(chess_engine) {
    enum_<MoveType>("MoveType")
        .value("NORMAL", NORMAL)
        .value("PROMOTION", PROMOTION)
        .value("ENPASSANT", ENPASSANT)
        .value("CASTLING", CASTLING);

    class_<WasmChessEngine>("WasmChessEngine")
        .constructor<>()
        .function("setBoardState", static_cast<bool(WasmChessEngine::*)(const val&, bool)>(&WasmChessEngine::setBoardState))
        .function("setBoardStateFull", static_cast<bool(WasmChessEngine::*)(const val&, bool, bool, bool, int, int)>(&WasmChessEngine::setBoardState))
        .function("findBestMove", &WasmChessEngine::findBestMove)
        .function("getLegalMoves", &WasmChessEngine::getLegalMoves)
        .function("getEvaluation", &WasmChessEngine::getEvaluation)
        .function("isInCheck", &WasmChessEngine::isInCheck)
        .function("isCheckmate", &WasmChessEngine::isCheckmate)
        .function("isStalemate", &WasmChessEngine::isStalemate)
        .function("applyMove", &WasmChessEngine::applyMove)
        .function("getAbilitiesAt", &WasmChessEngine::getAbilitiesAtCoords)
        .function("getBoardState", &WasmChessEngine::getBoardState);
}