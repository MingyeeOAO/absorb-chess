#include <iostream>
#include "position.h"
#include "thread.h"
#include "uci.h"
#include "evaluate.h"
#include "bitboard.h"

int main() {
    // Initialize the engine
    std::cout << "Initializing engine..." << std::endl;
    
    Bitboards::init();
    //UCI::init(Options);



    // // Set the starting position
    // std::string startPos = "startpos";
    // Position pos;
    // StateListPtr states(new std::deque<StateInfo>(1));
    // std::cout << "Setting starting position: " << startPos << std::endl;
    // pos.set(startPos, false, &states->back(), nullptr); // Pass nullptr for Thread*

    // // Print the board
    // std::cout << "Board setup:" << std::endl;
    // std::cout << pos << std::endl;

    // // Print all legal moves
    // std::cout << "Legal moves:" << std::endl;
    // for (const auto& move : MoveList<LEGAL>(pos)) {
    //     std::cout << UCI::move(move.move, false) << " "; // Added chess960 argument
    // }
    // std::cout << std::endl;

    // // Evaluate the position
    // std::cout << "Evaluation: " << Eval::evaluate(pos) << std::endl; // Corrected evaluation logic

    return 0;
}