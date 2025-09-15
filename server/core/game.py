from typing import Dict, List, Optional, Tuple
from .enums import PieceType, Color
from .piece import Piece
import datetime

class ChessGame:
    def __init__(self):
        self.board = [[None for _ in range(8)] for _ in range(8)]
        self.current_turn = Color.WHITE
        self.game_over = False
        self.winner = None
        self.move_history = []
        self.white_king_in_check = False
        self.black_king_in_check = False
        self.en_passant_target = None  # (row, col) of pawn that can be captured en passant
        self.promotion_pending: Optional[Dict] = None  # {'row': int, 'col': int, 'color': Color}
        self.valid_moves = {}  # Dictionary to store valid moves for each piece
        self._initialize_board()
        self.white_ms = 0
        self.black_ms = 0

    def load_from_state(self, game_state: Dict):
        """Load game state from a dictionary, ensuring clock fields are always initialized"""
        self.board = [[None for _ in range(8)] for _ in range(8)]
        for row in range(8):
            for col in range(8):
                pdata = game_state['board'][row][col]
                self.board[row][col] = Piece.from_dict(pdata) if pdata else None

        self.current_turn = Color(game_state['current_turn']) if isinstance(game_state['current_turn'], str) else game_state['current_turn']
        self.game_over = game_state.get('game_over', False)
        winner = game_state.get('winner')
        self.winner = Color(winner) if winner else None
        self.move_history = game_state.get('move_history', [])
        self.white_king_in_check = game_state.get('white_king_in_check', False)
        self.black_king_in_check = game_state.get('black_king_in_check', False)
        ep = game_state.get('en_passant_target')
        self.en_passant_target = tuple(ep) if ep else None
        pp = game_state.get('promotion_pending')
        self.promotion_pending = pp if pp else None
        self.white_ms = game_state.get('white_ms', 0)
        self.black_ms = game_state.get('black_ms', 0)
        # Ensure last_turn_start is always a valid timestamp (float)
        self.last_turn_start = game_state.get('last_turn_start')
        if self.last_turn_start is None:
            self.last_turn_start = datetime.datetime.now().timestamp() * 1000  # ms since epoch
        return self

    def calculate_moves(self) -> Dict[tuple, list]:
        """Calculate all valid moves for all pieces on the board.
        Returns:
            Dict[tuple, list]: Dictionary mapping piece positions (row,col) to list of valid moves [(to_row,to_col),...]
        """
        moves = {}
        for row in range(8):
            for col in range(8):
                piece = self.get_piece_at(row, col)
                if piece and piece.color == self.current_turn:
                    valid_moves = []
                    for to_row in range(8):
                        for to_col in range(8):
                            if (to_row, to_col) == (row, col):
                                continue
                            for ability in piece.abilities:
                                if self._is_valid_move_for_ability(piece, (row, col), (to_row, to_col), ability):
                                    if self._would_move_put_king_in_check(piece, (row, col), (to_row, to_col)):
                                        continue
                                    valid_moves.append((to_row, to_col))
                                    break
                    if valid_moves:
                        moves[(row, col)] = valid_moves
        return moves

    def _would_move_put_king_in_check(self, piece: 'Piece', from_pos: tuple, to_pos: tuple) -> bool:
        """Check if making this move would put the player's own king in check"""
        from_row, from_col = from_pos
        to_row, to_col = to_pos
        
        # Simulate the move
        captured_piece = self.get_piece_at(to_row, to_col)
        self.board[to_row][to_col] = piece
        self.board[from_row][from_col] = None
        original_position = piece.position
        piece.position = (to_row, to_col)
        
        # Check if king is in check after the move
        king_in_check = self._is_king_in_check(piece.color)
        
        # Revert the move
        self.board[from_row][from_col] = piece
        self.board[to_row][to_col] = captured_piece
        piece.position = original_position
        
        return king_in_check

    def _initialize_board(self):
        """Initialize the chess board with pieces in their starting positions"""
        # Initialize pawns
        for col in range(8):
            self.board[1][col] = Piece(PieceType.PAWN, Color.BLACK, [PieceType.PAWN], (1, col))
            self.board[6][col] = Piece(PieceType.PAWN, Color.WHITE, [PieceType.PAWN], (6, col))
        
        # Initialize other pieces
        piece_order = [PieceType.ROOK, PieceType.KNIGHT, PieceType.BISHOP, PieceType.QUEEN, 
                      PieceType.KING, PieceType.BISHOP, PieceType.KNIGHT, PieceType.ROOK]
        
        for col, piece_type in enumerate(piece_order):
            self.board[0][col] = Piece(piece_type, Color.BLACK, [piece_type], (0, col))
            self.board[7][col] = Piece(piece_type, Color.WHITE, [piece_type], (7, col))

    def get_piece_at(self, row: int, col: int) -> Optional[Piece]:
        if 0 <= row < 8 and 0 <= col < 8:
            return self.board[row][col]
        return None

    def set_piece_at(self, row: int, col: int, piece: Optional[Piece]):
        if 0 <= row < 8 and 0 <= col < 8:
            self.board[row][col] = piece

    def move_piece(self, from_pos: tuple, to_pos: tuple) -> bool:
        from_row, from_col = from_pos
        to_row, to_col = to_pos
        piece = self.get_piece_at(from_row, from_col)
        if not piece or piece.color != self.current_turn:
            return False
        
        # Basic validation: bounds check and not capturing own piece
        if not (0 <= to_row < 8 and 0 <= to_col < 8) or from_pos == to_pos:
            return False
        
        target_piece = self.get_piece_at(to_row, to_col)
        if target_piece and target_piece.color == piece.color:
            return False
        
        # Check piece-specific movement rules
        move_allowed = False
        for ability in piece.abilities:
            if self._is_valid_move_for_ability(piece, from_pos, to_pos, ability):
                move_allowed = True
                break
        
        if not move_allowed:
            return False
            
        # Simulate move for king-in-check validation
        captured_piece = self.get_piece_at(to_row, to_col)
        self.board[to_row][to_col] = piece
        self.board[from_row][from_col] = None
        original_position = piece.position
        piece.position = (to_row, to_col)
        king_in_check = self._is_king_in_check(piece.color)
        # Revert
        self.board[from_row][from_col] = piece
        self.board[to_row][to_col] = captured_piece
        piece.position = original_position
        if king_in_check:
            return False

        # Now do the move for real
        piece = self.get_piece_at(from_row, from_col)
        captured_piece = self.get_piece_at(to_row, to_col)
        en_passant_captured = None

        # Ability transfer
        if captured_piece:
            if captured_piece.type == PieceType.KING:
                self.game_over = True
                self.winner = piece.color
            self._transfer_abilities(piece, captured_piece)

        # Castling
        if piece.type == PieceType.KING and abs(to_col - from_col) == 2 and from_row == to_row:
            direction = 1 if to_col > from_col else -1
            rook_col = 7 if direction == 1 else 0
            rook = self.get_piece_at(from_row, rook_col)
            if rook and rook.type == PieceType.ROOK and not rook.has_moved and self._is_valid_king_move(piece, from_pos, to_pos):
                new_rook_col = to_col - direction
                self.board[from_row][new_rook_col] = rook
                self.board[from_row][rook_col] = None
                rook.position = (from_row, new_rook_col)
                rook.has_moved = True

        # En passant
        self.valid_moves.clear()
        if (piece.type == PieceType.PAWN and self.en_passant_target and to_pos == self.en_passant_target):
            en_passant_row = to_row + (1 if piece.color == Color.WHITE else -1)
            en_passant_captured = self.get_piece_at(en_passant_row, to_col)
            if en_passant_captured:
                self.board[en_passant_row][to_col] = None

        # Move the piece
        self.board[to_row][to_col] = piece
        self.board[from_row][from_col] = None
        piece.position = to_pos
        piece.has_moved = True

        # En passant target
        self._update_en_passant_target(piece, from_pos, to_pos)

        # Promotion
        became_promotion = False
        if piece.type == PieceType.PAWN and (to_row == 0 or to_row == 7):
            self.promotion_pending = {
                'row': to_row,
                'col': to_col,
                'color': piece.color.value,
                'from': (from_row, from_col)
            }
            became_promotion = True

        # Move history
        move_record = {
            'from': from_pos,
            'to': to_pos,
            'piece': piece.type.value,
            'captured': captured_piece.type.value if captured_piece else None,
            'en_passant_captured': en_passant_captured.type.value if en_passant_captured else None,
            'abilities_gained': captured_piece.type.value if captured_piece else None
        }
        if became_promotion:
            move_record['promoted_to'] = None
            move_record['final_piece'] = None
        self.move_history.append(move_record)

        # Check for check after the move
        self._update_check_status()

        # Switch turns unless promotion is pending
        if not became_promotion:
            self.current_turn = Color.BLACK if self.current_turn == Color.WHITE else Color.WHITE
        return True
    def calculate_valid_moves_for_turn(self):
        """Calculate and store valid moves for all pieces of the current turn."""
        self.valid_moves.clear()
        for row in range(8):
            for col in range(8):
                piece = self.get_piece_at(row, col)
                if piece and piece.color == self.current_turn:
                    valid_moves = []
                    for ability in piece.abilities:
                        valid_moves.extend(self._get_valid_moves_for_ability((row, col), ability))
                    if valid_moves:
                        self.valid_moves[(row, col)] = valid_moves

    def _get_valid_moves_for_ability(self, from_pos, ability):
        moves = []
        row, col = from_pos
        piece = self.get_piece_at(row, col)
        for to_row in range(8):
            for to_col in range(8):
                if (to_row, to_col) == (row, col):
                    continue
                if self._is_valid_move_for_ability(piece, (row, col), (to_row, to_col), ability):
                    moves.append((to_row, to_col))
        return moves
    
    def serialize_board(self):
        return [[piece.to_dict() if piece else None for piece in row] for row in self.board]

    def deserialize_board(self, board_data):
        for row in range(8):
            for col in range(8):
                pdata = board_data[row][col]
                self.board[row][col] = Piece.from_dict(pdata) if pdata else None

    def _is_valid_move(self, piece: Piece, from_pos: tuple, to_pos: tuple) -> bool:
        """Check if a move is valid according to chess rules"""
        from_row, from_col = from_pos
        to_row, to_col = to_pos
        
        if not (0 <= to_row < 8 and 0 <= to_col < 8) or from_pos == to_pos:
            return False
        
        target_piece = self.get_piece_at(to_row, to_col)
        if target_piece and target_piece.color == piece.color:
            return False
        
        # Check piece-specific movement rules
        move_allowed = False
        for ability in piece.abilities:
            if self._is_valid_move_for_ability(piece, from_pos, to_pos, ability):
                move_allowed = True
                break
        
        if not move_allowed:
            return False

        # Check if move would put own king in check
        return not self._is_king_in_check(piece.color)

    # ... (Rest of the movement validation methods from server.py)
    
    def get_board_state(self) -> Dict:
        board_state = []
        for row in self.board:
            row_state = []
            for piece in row:
                if piece:
                    row_state.append({
                        'type': piece.type.value,
                        'color': piece.color.value,
                        'abilities': [ability.value for ability in piece.abilities],
                        'position': piece.position,
                        'has_moved': piece.has_moved
                    })
                else:
                    row_state.append(None)
            board_state.append(row_state)
        return {
            'board': board_state,
            'current_turn': self.current_turn.value,
            'game_over': self.game_over,
            'winner': self.winner.value if self.winner else None,
            'move_history': self.move_history,
            'white_king_in_check': getattr(self, 'white_king_in_check', False),
            'black_king_in_check': getattr(self, 'black_king_in_check', False),
            'en_passant_target': self.en_passant_target,
            'promotion_pending': self.promotion_pending,
            'promotion_cancel_allowed': True
        }
        
    def _is_valid_move_for_ability(self, piece: Piece, from_pos: tuple, to_pos: tuple, ability: PieceType) -> bool:
        """Check if a move is valid for a specific ability"""
        if ability == PieceType.PAWN:
            return self._is_valid_pawn_move(piece, from_pos, to_pos)
        elif ability == PieceType.ROOK:
            return self._is_valid_rook_move(piece, from_pos, to_pos)
        elif ability == PieceType.KNIGHT:
            return self._is_valid_knight_move(piece, from_pos, to_pos)
        elif ability == PieceType.BISHOP:
            return self._is_valid_bishop_move(piece, from_pos, to_pos)
        elif ability == PieceType.QUEEN:
            return self._is_valid_queen_move(piece, from_pos, to_pos)
        elif ability == PieceType.KING:
            return self._is_valid_king_move(piece, from_pos, to_pos)
        return False
    
    
    def _is_valid_pawn_move(self, piece: Piece, from_pos: tuple, to_pos: tuple) -> bool:
        from_row, from_col = from_pos
        to_row, to_col = to_pos
        direction = -1 if piece.color == Color.WHITE else 1
        start_row = 6 if piece.color == Color.WHITE else 1
        # Forward one
        if to_col == from_col and to_row == from_row + direction:
            return self.get_piece_at(to_row, to_col) is None
        # Forward two from start
        if to_col == from_col and from_row == start_row and to_row == from_row + 2 * direction:
            return (self.get_piece_at(to_row, to_col) is None and
                    self.get_piece_at(from_row + direction, to_col) is None)
        # Diagonal capture
        if abs(to_col - from_col) == 1 and to_row == from_row + direction:
            target_piece = self.get_piece_at(to_row, to_col)
            if target_piece and target_piece.color != piece.color:
                return True
            # En passant
            if self.en_passant_target and tuple(self.en_passant_target) == (to_row, to_col):
                return True
        return False
    
    def _is_valid_rook_move(self, piece: Piece, from_pos: tuple, to_pos: tuple) -> bool:
        from_row, from_col = from_pos
        to_row, to_col = to_pos
        # Must move in straight line
        if from_row != to_row and from_col != to_col:
            return False
        # Path must be clear
        if not self._is_path_clear(from_pos, to_pos):
            return False
        # Can't capture own piece
        target_piece = self.get_piece_at(to_row, to_col)
        if target_piece and target_piece.color == piece.color:
            return False
        return True
    
    def _is_valid_knight_move(self, piece: Piece, from_pos: tuple, to_pos: tuple) -> bool:
        from_row, from_col = from_pos
        to_row, to_col = to_pos
        row_diff = abs(to_row - from_row)
        col_diff = abs(to_col - from_col)
        if (row_diff, col_diff) not in [(2, 1), (1, 2)]:
            return False
        if not (0 <= to_row < 8 and 0 <= to_col < 8):
            return False
        target_piece = self.get_piece_at(to_row, to_col)
        if target_piece and target_piece.color == piece.color:
            return False
        return True
    
    def _is_valid_bishop_move(self, piece: Piece, from_pos: tuple, to_pos: tuple) -> bool:
        from_row, from_col = from_pos
        to_row, to_col = to_pos
        if abs(to_row - from_row) != abs(to_col - from_col):
            return False
        if not self._is_path_clear(from_pos, to_pos):
            return False
        target_piece = self.get_piece_at(to_row, to_col)
        if target_piece and target_piece.color == piece.color:
            return False
        return True
    
    def _is_valid_queen_move(self, piece: Piece, from_pos: tuple, to_pos: tuple) -> bool:
        # Queen moves like rook or bishop
        return self._is_valid_rook_move(piece, from_pos, to_pos) or self._is_valid_bishop_move(piece, from_pos, to_pos)
    
    def _is_valid_king_move(self, piece: Piece, from_pos: tuple, to_pos: tuple) -> bool:
        from_row, from_col = from_pos
        to_row, to_col = to_pos
        row_diff = abs(to_row - from_row)
        col_diff = abs(to_col - from_col)
        # Single square move
        if row_diff <= 1 and col_diff <= 1 and (row_diff + col_diff > 0):
            target_piece = self.get_piece_at(to_row, to_col)
            if target_piece and target_piece.color == piece.color:
                return False
            return True
        # Castling
        if piece.has_moved or from_row != to_row or abs(to_col - from_col) != 2:
            return False
        direction = 1 if to_col > from_col else -1
        rook_col = 7 if direction == 1 else 0
        rook = self.get_piece_at(from_row, rook_col)
        if not rook or rook.type != PieceType.ROOK or rook.color != piece.color or rook.has_moved:
            return False
        # Squares between king and rook must be empty
        for c in range(min(from_col, rook_col) + 1, max(from_col, rook_col)):
            if self.get_piece_at(from_row, c) is not None:
                return False
        # King must not be in check, and squares it passes through must not be attacked
        if self._is_king_in_check(piece.color):
            return False
        for c in [from_col + direction, from_col + 2 * direction]:
            if self._square_attacked((from_row, c), piece.color):
                return False
        return True

    def _square_attacked(self, pos: tuple, defender_color: Color) -> bool:
        # attacker is the opposite color
        attacker_color = Color.BLACK if defender_color == Color.WHITE else Color.WHITE
        r, c = pos
        for row in range(8):
            for col in range(8):
                attacker = self.get_piece_at(row, col)
                if attacker and attacker.color == attacker_color:
                    if self._is_valid_move_for_ability(attacker, (row, col), (r, c), attacker.type):
                        return True
                    # Also check other abilities if attacker has multiple
                    for ability in attacker.abilities:
                        if ability != attacker.type and self._is_valid_move_for_ability(attacker, (row, col), (r, c), ability):
                            return True
        return False
    
    def _has_legal_moves(self, color: Color) -> bool:
        """Check if the given color has any legal moves available"""
        for row in range(8):
            for col in range(8):
                piece = self.get_piece_at(row, col)
                if piece and piece.color == color:
                    # Try all possible moves for this piece
                    for to_row in range(8):
                        for to_col in range(8):
                            if self._is_valid_move(piece, (row, col), (to_row, to_col)):
                                return True
        return False
    
    def _is_path_clear(self, from_pos: tuple, to_pos: tuple) -> bool:
        from_row, from_col = from_pos
        to_row, to_col = to_pos
        
        # Determine direction
        row_step = 0 if to_row == from_row else (1 if to_row > from_row else -1)
        col_step = 0 if to_col == from_col else (1 if to_col > from_col else -1)
        
        # Check each square along the path
        current_row, current_col = from_row + row_step, from_col + col_step
        while current_row != to_row or current_col != to_col:
            if self.get_piece_at(current_row, current_col) is not None:
                return False
            current_row += row_step
            current_col += col_step
        
        return True
    
    def _transfer_abilities(self, capturing_piece: 'Piece', captured_piece: 'Piece'):
        """Transfer abilities from captured piece to capturing piece"""
        captured_type = captured_piece.type
        
        # Don't add duplicate abilities
        if captured_type not in capturing_piece.abilities:
            capturing_piece.abilities.append(captured_type)
    
    def _update_en_passant_target(self, piece: Piece, from_pos: tuple, to_pos: tuple):
        """Update en passant target after a move"""
        # Clear previous en passant target
        self.en_passant_target = None
        
        # If a pawn moved two squares, set en passant target
        if piece.type == PieceType.PAWN:
            from_row, from_col = from_pos
            to_row, to_col = to_pos
            direction = -1 if piece.color == Color.WHITE else 1
            start_row = 6 if piece.color == Color.WHITE else 1
            
            if (from_row == start_row and 
                to_row == from_row + 2 * direction and 
                from_col == to_col):
                # Set en passant target to the square behind the pawn
                self.en_passant_target = (to_row - direction, to_col)
                # print(f"En passant target set to: {self.en_passant_target}")
    
    def _update_check_status(self):
        """Update check status for both kings and check for checkmate/stalemate"""
        self.white_king_in_check = self._is_king_in_check(Color.WHITE)
        self.black_king_in_check = self._is_king_in_check(Color.BLACK)
        
        if self.white_king_in_check:
            print("White king is in check!")
        if self.black_king_in_check:
            print("Black king is in check!")
        
        # Check for checkmate/stalemate
        if not self._has_legal_moves(self.current_turn):
            self.game_over = True
            if ((self.current_turn == Color.WHITE and self.white_king_in_check) or 
                (self.current_turn == Color.BLACK and self.black_king_in_check)):
                # Checkmate
                self.winner = Color.BLACK if self.current_turn == Color.WHITE else Color.WHITE
            else:
                # Stalemate
                self.winner = None
    
    def _is_king_in_check(self, king_color: Color) -> bool:
        """Check if the king of the given color is in check"""
        # Find the king
        king_pos = None
        for row in range(8):
            for col in range(8):
                piece = self.get_piece_at(row, col)
                if piece and piece.type == PieceType.KING and piece.color == king_color:
                    king_pos = (row, col)
                    break
            if king_pos:
                break
        
        if not king_pos:
            return False
        
        # Check if any opponent piece can attack the king
        opponent_color = Color.BLACK if king_color == Color.WHITE else Color.WHITE
        for row in range(8):
            for col in range(8):
                piece = self.get_piece_at(row, col)
                if piece and piece.color == opponent_color:
                    # Check if this piece can attack the king
                    if self._can_attack_king(piece, (row, col), king_pos):
                        return True
        
        return False
    
    def _can_attack_king(self, piece: Piece, from_pos: tuple, king_pos: tuple) -> bool:
        """Check if a piece can attack the king at the given position"""
        # Use the same move validation logic but check if it can reach the king
        for ability in piece.abilities:
            if self._is_valid_move_for_ability(piece, from_pos, king_pos, ability):
                return True
        return False
    
 
    def apply_promotion(self, row: int, col: int, new_type: PieceType):
        pawn = self.get_piece_at(row, col)
        if not pawn or pawn.type != PieceType.PAWN or not self.promotion_pending:
            return False
        
        # Store original abilities before promotion
        original_abilities = pawn.abilities.copy()
        
        # Apply promotion: change type and add new ability if not already present
        pawn.type = new_type
        if new_type not in pawn.abilities:
            pawn.abilities.append(new_type)
        
        # Check if this was a capture move by looking at the last move in history
        last_move = self.move_history[-1] if self.move_history else None
        if last_move and last_move.get('captured'):
            captured_piece_type = PieceType(last_move['captured'])
            
            # Add captured piece ability if not already present
            if captured_piece_type not in pawn.abilities:
                pawn.abilities.append(captured_piece_type)
                
            # Check if king was captured during promotion
            if captured_piece_type == PieceType.KING:
                self.game_over = True
                self.winner = pawn.color
        
        # Update move history to reflect the final piece type after promotion
        if last_move:
            last_move['promoted_to'] = new_type.value
            last_move['final_piece'] = new_type.value
            last_move['abilities_gained'] = [ability.value for ability in pawn.abilities]
        
        self.promotion_pending = None
        
        # After promotion, switch turn (unless game is over)
        if not self.game_over:
            self.current_turn = Color.BLACK if pawn.color == Color.WHITE else Color.WHITE
            
        # Re-evaluate check status after new piece power
        self._update_check_status()
        
        return True

    def cancel_promotion(self):
        if not self.promotion_pending:
            return False
        row = self.promotion_pending['row']
        col = self.promotion_pending['col']
        from_row, from_col = self.promotion_pending['from']
        pawn = self.get_piece_at(row, col)
        # Move the pawn back to original position
        self.board[from_row][from_col] = pawn
        self.board[row][col] = None
        if pawn:
            pawn.position = (from_row, from_col)
        self.promotion_pending = None
        return True

    def is_promotion_pending(self) -> bool:
        """Check if there is a promotion pending"""
        return self.promotion_pending is not None

    def get_board_state(self) -> Dict:
        board_state = []
        for row in self.board:
            row_state = []
            for piece in row:
                if piece:
                    row_state.append({
                        'type': piece.type.value,
                        'color': piece.color.value,
                        'abilities': [ability.value for ability in piece.abilities],
                        'position': piece.position,
                        'has_moved': piece.has_moved
                    })
                else:
                    row_state.append(None)
            board_state.append(row_state)
        return {
            'board': board_state,
            'current_turn': self.current_turn.value,
            'game_over': self.game_over,
            'winner': self.winner.value if self.winner else None,
            'move_history': self.move_history,
            'white_king_in_check': self.white_king_in_check,
            'black_king_in_check': self.black_king_in_check,
            'en_passant_target': self.en_passant_target,
            'promotion_pending': self.promotion_pending,
            'promotion_cancel_allowed': True
        }
    
