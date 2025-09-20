#!/usr/bin/env python3
"""
Test script to verify engine fixes:
1. Legal move generation should allow moves that put enemy king in check
2. Piece evaluation should use improved values
3. Turn logic should work correctly
"""

import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), 'server'))

from engine.chess_engine_exe import ChessEngine

def test_check_moves():
    """Test that moves putting enemy king in check are allowed"""
    print("Testing legal move generation for check moves...")
    
    engine = ChessEngine()
    # Set up a position where white can give check
    # Simple position: white queen can check black king
    board = [[0 for _ in range(8)] for _ in range(8)]
    
    # Place white queen at d1 and black king at d8
    WHITE_QUEEN = 0x10 | 0x08 | 0x01  # white | queen | has_moved=0
    BLACK_KING = 0x20 | 0x01          # black | king
    
    board[7][3] = WHITE_QUEEN  # d1
    board[0][3] = BLACK_KING   # d8
    
    engine.set_board_state(board, True, False, False, -1, -1)
    
    legal_moves = engine.generate_legal_moves()
    
    # Check if queen can move to give check (e.g., d1-d2, d1-d3, etc.)
    check_moves = []
    for move in legal_moves:
        if move.from_row == 7 and move.from_col == 3:  # Queen moves
            # Any move along the d-file should be allowed
            if move.to_col == 3 and move.to_row < 7:
                check_moves.append(f"d{8-move.from_row} to d{8-move.to_row}")
    
    print(f"Found {len(check_moves)} queen moves along d-file: {check_moves}")
    
    if len(check_moves) > 0:
        print("✅ PASS: Legal moves include moves that can give check")
    else:
        print("❌ FAIL: No legal moves found that can give check")
    
    return len(check_moves) > 0

def test_piece_values():
    """Test that piece values are improved"""
    print("\nTesting piece value evaluation...")
    
    engine = ChessEngine()
    
    # Test with a simple position
    board = [[0 for _ in range(8)] for _ in range(8)]
    
    # Place pieces to test values
    WHITE_KNIGHT = 0x10 | 0x02 | 0x01  # white | knight 
    WHITE_BISHOP = 0x10 | 0x04 | 0x01  # white | bishop
    WHITE_ROOK = 0x10 | 0x08 | 0x01    # white | rook
    WHITE_QUEEN = 0x10 | 0x10 | 0x01   # white | queen
    WHITE_KING = 0x10 | 0x01           # white | king
    
    board[0][0] = WHITE_KNIGHT  # a8
    board[0][1] = WHITE_BISHOP  # b8
    board[0][2] = WHITE_ROOK    # c8
    board[0][3] = WHITE_QUEEN   # d8
    board[0][4] = WHITE_KING    # e8
    
    engine.set_board_state(board, True, False, False, -1, -1)
    
    evaluation = engine.get_evaluation()
    
    print(f"Board evaluation with knight, bishop, rook, queen, king: {evaluation}")
    
    # Expected values: Knight=300, Bishop=300, Rook=500, Queen=900, King=10000
    # Total = 300 + 300 + 500 + 900 + 10000 = 12000
    expected_min = 11000  # Allow some variance for other evaluation factors
    expected_max = 13000
    
    if expected_min <= evaluation <= expected_max:
        print(f"✅ PASS: Evaluation {evaluation} is in expected range [{expected_min}, {expected_max}]")
        return True
    else:
        print(f"❌ FAIL: Evaluation {evaluation} is outside expected range [{expected_min}, {expected_max}]")
        return False

def test_basic_functionality():
    """Test basic engine functionality"""
    print("\nTesting basic engine functionality...")
    
    engine = ChessEngine()
    
    # Set up starting position
    engine.set_board_state(
        [[0 for _ in range(8)] for _ in range(8)], 
        True, False, False, -1, -1
    )
    
    # Add some pieces for a simple game
    board = [[0 for _ in range(8)] for _ in range(8)]
    
    # White pieces
    WHITE_PAWN = 0x10 | 0x40 | 0x01
    WHITE_KING = 0x10 | 0x01
    
    # Black pieces  
    BLACK_PAWN = 0x20 | 0x40 | 0x01
    BLACK_KING = 0x20 | 0x01
    
    board[6][4] = WHITE_PAWN   # e2
    board[7][4] = WHITE_KING   # e1
    board[1][4] = BLACK_PAWN   # e7
    board[0][4] = BLACK_KING   # e8
    
    engine.set_board_state(board, True, False, False, -1, -1)
    
    legal_moves = engine.generate_legal_moves()
    print(f"Found {len(legal_moves)} legal moves in test position")
    
    if len(legal_moves) > 0:
        print("✅ PASS: Engine generates legal moves")
        return True
    else:
        print("❌ FAIL: Engine generates no legal moves")
        return False

def main():
    print("Testing Chess Engine Fixes")
    print("=" * 40)
    
    results = []
    
    try:
        results.append(test_basic_functionality())
        results.append(test_check_moves())
        results.append(test_piece_values())
        
        print("\n" + "=" * 40)
        print("SUMMARY:")
        print(f"Tests passed: {sum(results)}/{len(results)}")
        
        if all(results):
            print("🎉 All tests passed! Engine fixes appear to be working.")
        else:
            print("⚠️ Some tests failed. Check the output above for details.")
            
    except Exception as e:
        print(f"❌ Error running tests: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    main()