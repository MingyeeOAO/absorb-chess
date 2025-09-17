/*
 * Chess Engine Executable
 * Communicates via stdin/stdout using simple text protocol
 */

#include "chess_engine.hpp"
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <chrono>
#include <iomanip>

class ChessEngineExecutable {
private:
    ChessEngine engine;
    GameState current_state;

public:
    ChessEngineExecutable() {
        // Initialize empty board state
        for (int i = 0; i < 8; i++) {
            for (int j = 0; j < 8; j++) {
                current_state.board[i][j] = 0;
            }
        }
        current_state.white_to_move = true;
        current_state.white_king_castled = false;
        current_state.black_king_castled = false;
        current_state.en_passant_col = -1;
        current_state.en_passant_row = -1;
    }

    void processCommand(const std::string& line) {
        std::istringstream iss(line);
        std::string command;
        iss >> command;

        try {
            if (command == "FIND_BEST_MOVE") {
                int depth, time_limit;
                iss >> depth >> time_limit;
                
                // Read board state
                if (readBoardState(iss)) {
                    auto result = findBestMove(depth, time_limit);
                    
                    // Check for invalid move (no legal moves found)
                    if (result.from_row == 0 && result.from_col == 0 && 
                        result.to_row == 0 && result.to_col == 0) {
                        std::cout << "ERROR No legal moves found" << std::endl;
                    } else {
                        std::cout << "MOVE " << result.from_row << " " << result.from_col 
                                 << " " << result.to_row << " " << result.to_col 
                                 << " " << result.evaluation << " " << result.time_taken_ms << std::endl;
                    }
                } else {
                    std::cout << "ERROR Invalid board state" << std::endl;
                }
            }
            else if (command == "SET_BOARD") {
                if (readBoardState(iss)) {
                    std::cout << "OK Board set" << std::endl;
                } else {
                    std::cout << "ERROR Invalid board state" << std::endl;
                }
            }
            else if (command == "GET_LEGAL_MOVES") {
                if (readBoardState(iss)) {
                    auto moves = getLegalMoves();
                    std::cout << "MOVES " << moves.size();
                    for (const auto& move_str : moves) {
                        std::cout << " " << move_str;
                    }
                    std::cout << std::endl;
                } else {
                    std::cout << "ERROR Invalid board state" << std::endl;
                }
            }
            else {
                std::cout << "ERROR Unknown command: " << command << std::endl;
            }
        }
        catch (const std::exception& e) {
            std::cout << "ERROR " << e.what() << std::endl;
        }
    }

private:
    bool readBoardState(std::istringstream& iss) {
        // Read white_to_move
        int white_to_move;
        if (!(iss >> white_to_move)) return false;
        current_state.white_to_move = white_to_move != 0;
        
        // Read castling flags
        int white_castled, black_castled;
        if (!(iss >> white_castled >> black_castled)) return false;
        current_state.white_king_castled = white_castled != 0;
        current_state.black_king_castled = black_castled != 0;
        
        // Read en passant
        if (!(iss >> current_state.en_passant_col >> current_state.en_passant_row)) return false;
        
        // Read 64 board values
        for (int row = 0; row < 8; row++) {
            for (int col = 0; col < 8; col++) {
                uint32_t piece;
                if (!(iss >> piece)) return false;
                current_state.board[row][col] = piece;
            }
        }
        
        // Sync with engine
        std::vector<std::vector<uint32_t>> board_vec(8, std::vector<uint32_t>(8));
        for (int i = 0; i < 8; i++) {
            for (int j = 0; j < 8; j++) {
                board_vec[i][j] = current_state.board[i][j];
            }
        }
        engine.set_board_state(board_vec, current_state.white_to_move, 
                              current_state.white_king_castled, current_state.black_king_castled,
                              current_state.en_passant_col, current_state.en_passant_row);
        
        return true;
    }

    struct MoveResult {
        int evaluation;
        int from_row, from_col, to_row, to_col;
        int depth_reached;
        int nodes_searched;
        int time_taken_ms;
    };

    MoveResult findBestMove(int depth, int time_limit_ms) {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        auto best_move = engine.find_best_move(depth, time_limit_ms);
        int evaluation = engine.get_evaluation();
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        MoveResult result;
        result.evaluation = evaluation;
        result.from_row = best_move.from_row;
        result.from_col = best_move.from_col;
        result.to_row = best_move.to_row;
        result.to_col = best_move.to_col;
        result.depth_reached = depth;
        result.nodes_searched = 0; // Engine doesn't track this yet
        result.time_taken_ms = static_cast<int>(duration.count());

        return result;
    }

    std::vector<std::string> getLegalMoves() {
        auto moves = engine.generate_legal_moves();
        std::vector<std::string> move_strings;
        
        for (const auto& move : moves) {
            std::stringstream ss;
            ss << static_cast<int>(move.from_row) << "," << static_cast<int>(move.from_col) 
               << "," << static_cast<int>(move.to_row) << "," << static_cast<int>(move.to_col);
            move_strings.push_back(ss.str());
        }
        
        return move_strings;
    }
};

int main() {
    ChessEngineExecutable engine;
    std::string line;
    
    // Send ready signal
    std::cout << "READY 1.0" << std::endl;
    std::cout.flush();
    
    // Process commands
    while (std::getline(std::cin, line)) {
        if (line == "QUIT" || line == "EXIT") {
            break;
        }
        
        if (!line.empty()) {
            engine.processCommand(line);
            std::cout.flush();
        }
    }
    
    return 0;
}