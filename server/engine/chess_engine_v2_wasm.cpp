/*
 * Chess Engine V2 WASM Interface
 * Provides C++ chess engine V2 functionality for WebAssembly compilation
 * API-compatible with the original chess_engine_wasm.cpp
 */

#include "chess_engine_v2.hpp"
#include <emscripten/emscripten.h>
#include <emscripten/bind.h>
#include <string>
#include <vector>
#include <sstream>
#include <chrono>

using namespace emscripten;

class WasmChessEngine {
private:
    ChessEngine engine;
    
public:
    WasmChessEngine() {
        // Initialize with standard starting position
        std::vector<std::vector<uint32_t>> starting_board(8, std::vector<uint32_t>(8));
        
        // Set up standard chess starting position
        // This is a simplified initialization - you may want to set up the actual starting position
        for (int i = 0; i < 8; i++) {
            for (int j = 0; j < 8; j++) {
                starting_board[i][j] = 0; // Empty squares
            }
        }
        
        engine.set_board_state(starting_board, true, false, false, -1, -1);
    }
    
    // Set board state from JavaScript array
    bool setBoardState(const val& js_board, bool white_to_move, 
                      bool white_king_castled, bool black_king_castled,
                      int en_passant_col, int en_passant_row) {
        try {
            // Convert JavaScript 2D array to C++ vector
            std::vector<std::vector<uint32_t>> board_vector(8, std::vector<uint32_t>(8));
            
            for (int i = 0; i < 8; i++) {
                val row = js_board[i];
                for (int j = 0; j < 8; j++) {
                    board_vector[i][j] = row[j].as<uint32_t>();
                }
            }
            
            // Set the engine's board state
            engine.set_board_state(board_vector, white_to_move, white_king_castled, 
                                 black_king_castled, en_passant_col, en_passant_row);
            
            return true;
        } catch (...) {
            return false;
        }
    }
    
    // Find best move and return as JavaScript object
    val findBestMove(int depth, int time_limit_ms) {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        auto [best_move, score] = engine.get_best_move(depth);
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        // Get evaluation for the current position
        int evaluation = engine.evaluate_position();
        
        // Create JavaScript object to return
        val result = val::object();
        result.set("from_row", best_move.from_row);
        result.set("from_col", best_move.from_col);
        result.set("to_row", best_move.to_row);
        result.set("to_col", best_move.to_col);
        result.set("flags", best_move.flags);  // Always include flags
        result.set("evaluation", evaluation);
        result.set("time_taken_ms", (int)duration.count());
        result.set("depth_reached", depth);
        
        // Check for promotion
        if (best_move.flags >= 4 && best_move.flags <= 7) {
            std::string promotion_piece;
            switch (best_move.flags) {
                case 4: promotion_piece = "Q"; break;
                case 5: promotion_piece = "R"; break;
                case 6: promotion_piece = "B"; break;
                case 7: promotion_piece = "N"; break;
            }
            result.set("promotion_piece", promotion_piece);
        }
        
        return result;
    }
    
    // Get legal moves as JavaScript array
    val getLegalMoves() {
        auto moves = engine.get_legal_moves();
        
        val js_moves = val::array();
        int index = 0;
        
        for (const auto& move : moves) {
            val js_move = val::object();
            js_move.set("from_row", move.from_row);
            js_move.set("from_col", move.from_col);
            js_move.set("to_row", move.to_row);
            js_move.set("to_col", move.to_col);
            js_move.set("flags", move.flags);
            
            js_moves.set(index++, js_move);
        }
        
        return js_moves;
    }
    
    // Get position evaluation
    int getEvaluation() {
        return engine.evaluate_position();
    }
    
    // Check if position is in check
    bool isInCheck() {
        return engine.is_in_check(engine.is_white_to_move()); // Check for current player's king
    }
    
    // Check if position is checkmate
    bool isCheckmate() {
        return engine.is_checkmate();
    }
    
    // Check if position is stalemate
    bool isStalemate() {
        return engine.is_stalemate();
    }
    
    // Apply a move to the current state
    bool applyMove(int from_row, int from_col, int to_row, int to_col, int flags = 0) {
        Move move(static_cast<uint8_t>(from_row), static_cast<uint8_t>(from_col), 
                  static_cast<uint8_t>(to_row), static_cast<uint8_t>(to_col), 
                  static_cast<uint32_t>(flags));
        
        // Check if move is legal
        auto legal_moves = engine.get_legal_moves();
        bool is_legal = false;
        for (const auto& legal_move : legal_moves) {
            if (legal_move.from_row == move.from_row && 
                legal_move.from_col == move.from_col &&
                legal_move.to_row == move.to_row && 
                legal_move.to_col == move.to_col) {
                is_legal = true;
                move.flags = legal_move.flags;  // Use the correct flags
                break;
            }
        }
        
        if (!is_legal) {
            return false;
        }
        
        // Apply the move
        engine.apply_move(move);
        return true;
    }
};

// Bind the class to JavaScript
EMSCRIPTEN_BINDINGS(chess_engine) {
    class_<WasmChessEngine>("WasmChessEngine")
        .constructor<>()
        .function("setBoardState", &WasmChessEngine::setBoardState)
        .function("findBestMove", &WasmChessEngine::findBestMove)
        .function("getLegalMoves", &WasmChessEngine::getLegalMoves)
        .function("getEvaluation", &WasmChessEngine::getEvaluation)
        .function("isInCheck", &WasmChessEngine::isInCheck)
        .function("isCheckmate", &WasmChessEngine::isCheckmate)
        .function("isStalemate", &WasmChessEngine::isStalemate)
        .function("applyMove", &WasmChessEngine::applyMove);
}