#include "src/bitboard.h"
#include "src/types.h"
#include <iostream>
#include <chrono>

// Simple test to check if Bitboards::init() completes
int main() {
    std::cout << "=== Minimal Bitboard Init Test ===" << std::endl;
    
    // Test: Try Bitboards::init() directly 
    std::cout << "\nAttempting Bitboards::init()..." << std::endl;
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        std::cout << "Starting Bitboards::init()..." << std::endl;
        Bitboards::init();
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        std::cout << "✅ Bitboards::init() completed successfully!" << std::endl;
        std::cout << "Time taken: " << duration.count() << " ms" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "❌ ERROR in Bitboards::init(): " << e.what() << std::endl;
        return 1;
    }
    
    // Now test square_bb after init
    std::cout << "\nTesting square_bb after init..." << std::endl;
    try {
        Square sq = SQ_E4;
        std::cout << "Square E4 = " << sq << std::endl;
        
        Bitboard bb = square_bb(sq);
        std::cout << "square_bb(E4) = " << bb << std::endl;
        
        // Should be 1ULL << 28 = 268435456
        Bitboard expected = 1ULL << 28;
        if (bb == expected) {
            std::cout << "✅ square_bb works correctly" << std::endl;
        } else {
            std::cout << "❌ square_bb returned " << bb << ", expected " << expected << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cout << "❌ ERROR testing square_bb: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "\n✅ Test completed!" << std::endl;
    return 0;
}