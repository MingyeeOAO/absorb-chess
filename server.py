import asyncio
import websockets
import json
import uuid
import datetime
from typing import Dict, List, Optional
from dataclasses import dataclass, asdict
from enum import Enum

# Feature flags
CANCEL_PROMOTE = False

class PieceType(Enum):
    PAWN = "pawn"
    ROOK = "rook"
    KNIGHT = "knight"
    BISHOP = "bishop"
    QUEEN = "queen"
    KING = "king"

class Color(Enum):
    WHITE = "white"
    BLACK = "black"

@dataclass
class Piece:
    type: PieceType
    color: Color
    abilities: List[PieceType]  # List of movement abilities this piece has
    position: tuple  # (row, col)
    has_moved: bool = False
    
    def __post_init__(self):
        if not self.abilities:
            self.abilities = [self.type]  # Start with own movement type

@dataclass
class Player:
    id: str
    name: str
    color: Color
    websocket: websockets.WebSocketServerProtocol

@dataclass
class Lobby:
    code: str
    owner_id: str
    players: List[Player]
    game_state: Optional[Dict]
    settings: Dict
    created_at: datetime.datetime

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
        self._initialize_board()
    
    def _initialize_board(self):
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
    
    def move_piece(self, from_pos: tuple, to_pos: tuple) -> bool:
        from_row, from_col = from_pos
        to_row, to_col = to_pos
        
        print(f"Attempting move from {from_pos} to {to_pos}")
        print(f"Current turn: {self.current_turn.value}")
        
        piece = self.get_piece_at(from_row, from_col)
        if not piece:
            print("No piece at from position")
            return False
        
        if piece.color != self.current_turn:
            print(f"Wrong turn: piece is {piece.color.value}, current turn is {self.current_turn.value}")
            return False
        
        print(f"Moving {piece.type.value} with abilities: {[a.value for a in piece.abilities]}")
        
        # Check if move is valid
        if not self._is_valid_move(piece, from_pos, to_pos):
            print("Move is invalid according to chess rules")
            return False
        
        # Handle capture and ability transfer
        captured_piece = self.get_piece_at(to_row, to_col)
        if captured_piece:
            print(f"Capturing {captured_piece.type.value}")
            # Check if king is captured
            if captured_piece.type == PieceType.KING:
                print(f"GAME OVER! {piece.color.value} captured the {captured_piece.color.value} king!")
                self.game_over = True
                self.winner = piece.color
            self._transfer_abilities(piece, captured_piece)
            print(f"Piece now has abilities: {[a.value for a in piece.abilities]}")
        
        # Handle castling move (king moves two squares)
        if piece.type == PieceType.KING and abs(to_col - from_col) == 2 and from_row == to_row:
            direction = 1 if to_col > from_col else -1
            rook_col = 7 if direction == 1 else 0
            rook = self.get_piece_at(from_row, rook_col)
            if rook and rook.type == PieceType.ROOK and not rook.has_moved and self._is_valid_king_move(piece, from_pos, to_pos):
                # Move rook to the square next to king's destination
                new_rook_col = to_col - direction
                self.board[from_row][new_rook_col] = rook
                self.board[from_row][rook_col] = None
                rook.position = (from_row, new_rook_col)
                rook.has_moved = True

        # Handle en passant capture
        en_passant_captured = None
        if (piece.type == PieceType.PAWN and 
            self.en_passant_target and 
            to_pos == self.en_passant_target):
            # Capture the pawn that moved two squares
            en_passant_row = to_row + (1 if piece.color == Color.WHITE else -1)
            en_passant_captured = self.get_piece_at(en_passant_row, to_col)
            if en_passant_captured:
                self.board[en_passant_row][to_col] = None
                print(f"En passant capture: removed pawn at ({en_passant_row}, {to_col})")
        
        # Move the piece
        self.board[to_row][to_col] = piece
        self.board[from_row][from_col] = None
        piece.position = to_pos
        piece.has_moved = True
        
        # Update en passant target for next move
        self._update_en_passant_target(piece, from_pos, to_pos)
        
        # Detect promotion eligibility before recording move/turn switch
        became_promotion = False
        if piece.type == PieceType.PAWN and (to_row == 0 or to_row == 7):
            # Mark promotion pending; don't switch turn until resolved
            self.promotion_pending = {
                'row': to_row,
                'col': to_col,
                'color': piece.color.value,
                'from': (from_row, from_col)
            }
            became_promotion = True

        # Record move
        self.move_history.append({
            'from': from_pos,
            'to': to_pos,
            'piece': piece.type.value,
            'captured': captured_piece.type.value if captured_piece else None,
            'en_passant_captured': en_passant_captured.type.value if en_passant_captured else None,
            'abilities_gained': captured_piece.type.value if captured_piece else None
        })
        
        # Check for check after the move
        self._update_check_status()
        
        # Switch turns unless promotion is pending (promotion keeps turn until choice)
        if not became_promotion:
            self.current_turn = Color.BLACK if self.current_turn == Color.WHITE else Color.WHITE
            print(f"Turn switched to: {self.current_turn.value}")
        
        return True
    
    def _is_valid_move(self, piece: Piece, from_pos: tuple, to_pos: tuple) -> bool:
        from_row, from_col = from_pos
        to_row, to_col = to_pos
        
        # Basic bounds checking
        if not (0 <= to_row < 8 and 0 <= to_col < 8):
            return False
        
        # Can't move to same position
        if from_pos == to_pos:
            return False
        
        # Can't capture own piece
        target_piece = self.get_piece_at(to_row, to_col)
        if target_piece and target_piece.color == piece.color:
            return False
        
        # Check if any of the piece's abilities allow this move
        for ability in piece.abilities:
            if self._is_valid_move_for_ability(piece, from_pos, to_pos, ability):
                return True
        
        return False
    
    def _is_valid_move_for_ability(self, piece: Piece, from_pos: tuple, to_pos: tuple, ability: PieceType) -> bool:
        from_row, from_col = from_pos
        to_row, to_col = to_pos
        
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
        
        # Pawn moves forward - FIXED DIRECTION
        # White pawns move UP (decreasing row numbers), Black pawns move DOWN (increasing row numbers)
        direction = -1 if piece.color == Color.WHITE else 1
        start_row = 6 if piece.color == Color.WHITE else 1
        
        print(f"Pawn move validation: {piece.color.value} pawn from {from_pos} to {to_pos}")
        print(f"Direction: {direction}, Start row: {start_row}")
        
        # Forward move
        if to_col == from_col:
            if to_row == from_row + direction:
                target_piece = self.get_piece_at(to_row, to_col)
                print(f"Single forward move: target piece at {to_pos}: {target_piece}")
                return target_piece is None
            elif from_row == start_row and to_row == from_row + 2 * direction:
                target_piece = self.get_piece_at(to_row, to_col)
                intermediate_piece = self.get_piece_at(from_row + direction, to_col)
                print(f"Double forward move: target piece at {to_pos}: {target_piece}")
                print(f"Intermediate piece at {from_row + direction, to_col}: {intermediate_piece}")
                return (target_piece is None and intermediate_piece is None)
        # Diagonal capture
        elif abs(to_col - from_col) == 1 and to_row == from_row + direction:
            target_piece = self.get_piece_at(to_row, to_col)
            print(f"Diagonal capture: target piece at {to_pos}: {target_piece}")
            if target_piece:
                print(f"Target piece color: {target_piece.color.value}, piece color: {piece.color.value}")
                return target_piece.color != piece.color
            # Check for en passant (ensure stored as tuple)
            elif self.en_passant_target and tuple(self.en_passant_target) == (to_row, to_col):
                print(f"En passant capture at {to_pos}")
                return True
        
        print("Pawn move not matching any valid pattern")
        return False
    
    def _is_valid_rook_move(self, piece: Piece, from_pos: tuple, to_pos: tuple) -> bool:
        from_row, from_col = from_pos
        to_row, to_col = to_pos
        
        # Rook moves horizontally or vertically
        if from_row != to_row and from_col != to_col:
            return False
        
        # Check for pieces in the path
        return self._is_path_clear(from_pos, to_pos)
    
    def _is_valid_knight_move(self, piece: Piece, from_pos: tuple, to_pos: tuple) -> bool:
        from_row, from_col = from_pos
        to_row, to_col = to_pos
        
        # Knight moves in L-shape
        row_diff = abs(to_row - from_row)
        col_diff = abs(to_col - from_col)
        return (row_diff == 2 and col_diff == 1) or (row_diff == 1 and col_diff == 2)
    
    def _is_valid_bishop_move(self, piece: Piece, from_pos: tuple, to_pos: tuple) -> bool:
        from_row, from_col = from_pos
        to_row, to_col = to_pos
        
        # Bishop moves diagonally
        if abs(to_row - from_row) != abs(to_col - from_col):
            return False
        
        # Check for pieces in the path
        return self._is_path_clear(from_pos, to_pos)
    
    def _is_valid_queen_move(self, piece: Piece, from_pos: tuple, to_pos: tuple) -> bool:
        # Queen moves like rook or bishop
        return (self._is_valid_rook_move(piece, from_pos, to_pos) or 
                self._is_valid_bishop_move(piece, from_pos, to_pos))
    
    def _is_valid_king_move(self, piece: Piece, from_pos: tuple, to_pos: tuple) -> bool:
        from_row, from_col = from_pos
        to_row, to_col = to_pos
        
        # King moves one square in any direction
        row_diff = abs(to_row - from_row)
        col_diff = abs(to_col - from_col)
        if row_diff <= 1 and col_diff <= 1 and (row_diff + col_diff > 0):
            return True
        
        # Castling logic
        # Conditions:
        # - King has not moved
        # - Rook has not moved
        # - Same row, col change is 2 (to_col = from_col ± 2)
        # - Squares between are empty
        # - King not in check, squares it passes through not attacked, destination not attacked
        if piece.has_moved:
            return False
        if from_row != to_row:
            return False
        if abs(to_col - from_col) != 2:
            return False
        
        direction = 1 if to_col > from_col else -1
        rook_col = 7 if direction == 1 else 0
        rook = self.get_piece_at(from_row, rook_col)
        if not rook or rook.type != PieceType.ROOK or rook.color != piece.color or rook.has_moved:
            return False
        
        # Check empty squares between king and rook (excluding their current squares)
        step_col = from_col + direction
        while step_col != rook_col:
            if self.get_piece_at(from_row, step_col) is not None:
                return False
            step_col += direction
        
        # Check king is not in check, and squares king passes through are not attacked
        if self._is_king_in_check(piece.color):
            return False
        # simulate squares
        path_cols = [from_col + direction, from_col + 2*direction]
        for c in path_cols:
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
    
    def _transfer_abilities(self, capturing_piece: Piece, captured_piece: Piece):
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
                print(f"En passant target set to: {self.en_passant_target}")
    
    def _update_check_status(self):
        """Update check status for both kings"""
        self.white_king_in_check = self._is_king_in_check(Color.WHITE)
        self.black_king_in_check = self._is_king_in_check(Color.BLACK)
        
        if self.white_king_in_check:
            print("White king is in check!")
        if self.black_king_in_check:
            print("Black king is in check!")
    
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

    def _revert_pawn_after_cancel(self, game: 'ChessGame', to_pos: tuple, from_pos: tuple):
        tr, tc = to_pos
        fr, fc = from_pos
        pawn = game.get_piece_at(tr, tc)
        # Move the pawn back
        game.board[fr][fc] = pawn
        game.board[tr][tc] = None
        if pawn:
            pawn.position = (fr, fc)
    
    def get_board_state(self) -> Dict:
        """Convert board to JSON-serializable format"""
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
            'promotion_cancel_allowed': CANCEL_PROMOTE
        }

class GameServer:
    def __init__(self):
        self.lobbies: Dict[str, Lobby] = {}
        self.connected_clients: Dict[str, websockets.WebSocketServerProtocol] = {}
        self.draw_offer_history: Dict[str, list] = {}
    
    async def register_client(self, websocket):
        print('DEBUG connection received.')
        client_id = str(uuid.uuid4())
        self.connected_clients[client_id] = websocket
        
        try:
            await self.handle_client(client_id, websocket)
        except websockets.exceptions.ConnectionClosed:
            pass
        finally:
            # Clean up when client disconnects
            await self.handle_disconnect(client_id)
    
    async def handle_client(self, client_id: str, websocket):
        async for message in websocket:
            try:
                data = json.loads(message)
                await self.process_message(client_id, websocket, data)
            except json.JSONDecodeError:
                await self.send_error(websocket, "Invalid JSON")
            except Exception as e:
                await self.send_error(websocket, f"Server error: {str(e)}")
    
    async def process_message(self, client_id: str, websocket, data: dict):
        message_type = data.get('type')
        
        if message_type == 'create_lobby':
            await self.create_lobby(client_id, websocket, data)
        elif message_type == 'join_lobby':
            await self.join_lobby(client_id, websocket, data)
        elif message_type == 'leave_lobby':
            await self.leave_lobby(client_id, websocket, data)
        elif message_type == 'start_game':
            await self.start_game(client_id, websocket, data)
        elif message_type == 'move_piece':
            await self.move_piece(client_id, websocket, data)
        elif message_type == 'promotion_choice':
            await self.apply_promotion(client_id, websocket, data)
        elif message_type == 'resign':
            await self.handle_resign(client_id, websocket)
        elif message_type == 'offer_draw':
            await self.handle_offer_draw(client_id, websocket)
        elif message_type == 'accept_draw':
            await self.handle_accept_draw(client_id, websocket)
        elif message_type == 'timeout':
            await self.handle_timeout(client_id, websocket)
        elif message_type == 'decline_draw':
            await self.handle_decline_draw(client_id, websocket, data)
        elif message_type == 'Heartbeat':
            pass
        else:
            await self.send_error(websocket, f"Unknown message type: {message_type}")
    
    async def create_lobby(self, client_id: str, websocket, data: dict):
        lobby_code = self.generate_lobby_code()
        player_name = data.get('player_name', 'Player')
        
        lobby = Lobby(
            code=lobby_code,
            owner_id=client_id,
            players=[Player(client_id, player_name, Color.WHITE, websocket)],
            game_state=None,
            settings=data.get('settings', {}),
            created_at=datetime.datetime.now()
        )
        
        self.lobbies[lobby_code] = lobby
        
        # Send lobby data to the creator
        await self.send_message(websocket, {
            'type': 'lobby_created',
            'lobby_code': lobby_code,
            'player_id': client_id,
            'is_owner': True,
            'lobby_data': {
                'lobby_code': lobby_code,
                'players': [{'id': p.id, 'name': p.name, 'color': p.color.value} for p in lobby.players],
                'settings': lobby.settings,
                'is_owner': True
            }
        })
    
    async def join_lobby(self, client_id: str, websocket, data: dict):
        lobby_code = data.get('lobby_code')
        player_name = data.get('player_name', 'Player')
        
        if lobby_code not in self.lobbies:
            await self.send_error(websocket, "Lobby not found")
            return
        
        lobby = self.lobbies[lobby_code]
        
        if len(lobby.players) >= 2:
            await self.send_error(websocket, "Lobby is full")
            return
        
        # Add player to lobby
        player_color = Color.BLACK if len(lobby.players) == 1 else Color.WHITE
        player = Player(client_id, player_name, player_color, websocket)
        lobby.players.append(player)
        
        # Notify all players in lobby
        for player in lobby.players:
            await self.send_message(player.websocket, {
                'type': 'lobby_update',
                'lobby_code': lobby_code,
                'players': [{'id': p.id, 'name': p.name, 'color': p.color.value} for p in lobby.players],
                'settings': lobby.settings,
                'is_owner': player.id == lobby.owner_id
            })
        
        await self.send_message(websocket, {
            'type': 'lobby_joined',
            'lobby_code': lobby_code,
            'player_id': client_id,
            'is_owner': client_id == lobby.owner_id,
            'lobby_data': {
                'lobby_code': lobby_code,
                'players': [{'id': p.id, 'name': p.name, 'color': p.color.value} for p in lobby.players],
                'settings': lobby.settings,
                'is_owner': client_id == lobby.owner_id
            }
        })
    
    async def leave_lobby(self, client_id: str, websocket, data: dict):
        # Find lobby containing this client
        for lobby_code, lobby in self.lobbies.items():
            if any(player.id == client_id for player in lobby.players):
                # Remove player from lobby
                lobby.players = [p for p in lobby.players if p.id != client_id]
                
                # Notify remaining players
                if lobby.players:  # If there are still players left
                    for player in lobby.players:
                        await self.send_message(player.websocket, {
                            'type': 'lobby_update',
                            'lobby_code': lobby_code,
                            'players': [{'id': p.id, 'name': p.name, 'color': p.color.value} for p in lobby.players],
                            'settings': lobby.settings,
                            'is_owner': player.id == lobby.owner_id
                        })
                else:
                    # Clean up empty lobby
                    del self.lobbies[lobby_code]
                
                break
    
    async def start_game(self, client_id: str, websocket, data: dict):
        # Find lobby where client is owner
        for lobby_code, lobby in self.lobbies.items():
            if lobby.owner_id == client_id and len(lobby.players) == 2:
                # Initialize game
                game = ChessGame()
                lobby.game_state = game.get_board_state()

                # Initialize clocks based on lobby settings
                settings = lobby.settings or {}
                minutes = int(settings.get('time_minutes', 0) or 0)
                increment_seconds = int(settings.get('time_increment_seconds', 0) or 0)
                total_ms = minutes * 60 * 1000
                inc_ms = increment_seconds * 1000

                now_iso = datetime.datetime.utcnow().isoformat() + 'Z'
                lobby.game_state['clock'] = {
                    'white_ms': total_ms,
                    'black_ms': total_ms,
                    'increment_ms': inc_ms,
                    'last_turn_start': now_iso
                }
                
                # Notify all players
                await self.broadcast_to_lobby(lobby, {
                    'type': 'game_started',
                    'game_state': lobby.game_state
                })
                break
    
    async def move_piece(self, client_id: str, websocket, data: dict):
        # Find lobby and game
        for lobby_code, lobby in self.lobbies.items():
            if any(player.id == client_id for player in lobby.players) and lobby.game_state:
                # Find the player making the move
                moving_player = None
                for player in lobby.players:
                    if player.id == client_id:
                        moving_player = player
                        break
                
                if not moving_player:
                    await self.send_error(websocket, "Player not found")
                    return
                
                # Reconstruct game from state
                game = self._reconstruct_game_from_state(lobby.game_state)
                
                from_pos = tuple(data['from'])
                to_pos = tuple(data['to'])
                
                # Handle clocks: subtract elapsed from current mover
                clock = lobby.game_state.get('clock') or {}
                last_turn_start = clock.get('last_turn_start')
                if last_turn_start:
                    try:
                        started = datetime.datetime.fromisoformat(last_turn_start.replace('Z','+00:00'))
                        now = datetime.datetime.utcnow().replace(tzinfo=datetime.timezone.utc)
                        elapsed_ms = int((now - started).total_seconds() * 1000)
                    except Exception:
                        elapsed_ms = 0
                else:
                    elapsed_ms = 0

                if game.current_turn == Color.WHITE:
                    clock['white_ms'] = max(0, int(clock.get('white_ms', 0)) - elapsed_ms)
                    mover_ms = clock['white_ms']
                else:
                    clock['black_ms'] = max(0, int(clock.get('black_ms', 0)) - elapsed_ms)
                    mover_ms = clock['black_ms']

                # Timeout check before move validation
                if mover_ms <= 0 and not game.game_over:
                    game.game_over = True
                    game.winner = Color.BLACK if game.current_turn == Color.WHITE else Color.WHITE
                    lobby.game_state = game.get_board_state()
                    lobby.game_state['clock'] = clock
                    await self.broadcast_to_lobby(lobby, {
                        'type': 'move_made',
                        'from': from_pos,
                        'to': to_pos,
                        'game_state': lobby.game_state
                    })
                    return

                # Check if it's the player's turn
                if game.current_turn != moving_player.color:
                    await self.send_error(websocket, "Not your turn")
                    return
                
                # Check if the piece belongs to the moving player
                piece = game.get_piece_at(from_pos[0], from_pos[1])
                if not piece or piece.color != moving_player.color:
                    await self.send_error(websocket, "Invalid piece")
                    return
                
                if game.move_piece(from_pos, to_pos):
                    lobby.game_state = game.get_board_state()

                    # If promotion pending, do not switch turn/clock yet; just notify
                    if game.promotion_pending:
                        # Keep clock running while awaiting promotion choice
                        lobby.game_state['clock'] = clock
                        await self.broadcast_to_lobby(lobby, {
                            'type': 'promotion_pending',
                            'game_state': lobby.game_state
                        })
                        return

                    # Apply increment to the mover after a successful move
                    if clock is not None:
                        inc_ms = int(clock.get('increment_ms', 0))
                        if moving_player.color == Color.WHITE:
                            clock['white_ms'] = max(0, int(clock.get('white_ms', 0)) + inc_ms)
                        else:
                            clock['black_ms'] = max(0, int(clock.get('black_ms', 0)) + inc_ms)

                        # Set last_turn_start for the next player
                        clock['last_turn_start'] = datetime.datetime.utcnow().isoformat() + 'Z'
                        lobby.game_state['clock'] = clock
                    
                    # Broadcast move to all players
                    await self.broadcast_to_lobby(lobby, {
                        'type': 'move_made',
                        'from': from_pos,
                        'to': to_pos,
                        'game_state': lobby.game_state
                    })
                else:
                    await self.send_error(websocket, "Invalid move")
                break
    async def apply_promotion(self, client_id: str, websocket, data: dict):
        # Find lobby and game
        for lobby_code, lobby in self.lobbies.items():
            if any(player.id == client_id for player in lobby.players) and lobby.game_state:
                game = self._reconstruct_game_from_state(lobby.game_state)
                choice = data.get('choice')  # 'queen', 'rook', 'bishop', 'knight'
                pending = game.promotion_pending
                if not pending:
                    await self.send_error(websocket, "No promotion pending")
                    return
                row, col = pending['row'], pending['col']
                pawn = game.get_piece_at(row, col)
                if not pawn or pawn.type != PieceType.PAWN:
                    await self.send_error(websocket, "Invalid promotion state")
                    return

                # --- Handle Cancel ---
                if (choice or '').lower() == 'cancel':
                    if not CANCEL_PROMOTE:
                        await self.send_error(websocket, "Promotion cancel disabled")
                        return
                    orig_row, orig_col = pending.get('from')
                    game.revert_pawn_after_cancel((row, col), (orig_row, orig_col))
                    game.promotion_pending = None
                    lobby.game_state = game.get_board_state()
                    await self.broadcast_to_lobby(lobby, {
                        'type': 'promotion_canceled',
                        'game_state': lobby.game_state
                    })
                    break

                # --- Normal Promotion ---
                mapping = {
                    'queen': PieceType.QUEEN,
                    'rook': PieceType.ROOK,
                    'bishop': PieceType.BISHOP,
                    'knight': PieceType.KNIGHT
                }
                new_type = mapping.get((choice or '').lower())
                if new_type is None:
                    await self.send_error(websocket, "Invalid promotion choice")
                    return

                # Replace pawn
                pawn.type = new_type
                pawn.abilities = [new_type]
                game.promotion_pending = None

                # --- Update move history ---
                if game.move_history:
                    last_move = game.move_history[-1]
                    last_move['promotion'] = new_type.value
                    # captured / abilities_gained 已經在 move_piece 時處理過，這裡只補 promotion

                # Switch turn
                game.current_turn = Color.BLACK if game.current_turn == Color.WHITE else Color.WHITE

                # Update check
                game._update_check_status()

                # Update clocks
                clock = lobby.game_state.get('clock') or {}
                inc_ms = int(clock.get('increment_ms', 0))
                if (pending['color'] == Color.WHITE.value) or (pending['color'] == 'white'):
                    clock['white_ms'] = int(clock.get('white_ms', 0)) + inc_ms
                else:
                    clock['black_ms'] = int(clock.get('black_ms', 0)) + inc_ms
                clock['last_turn_start'] = datetime.datetime.utcnow().isoformat() + 'Z'

                lobby.game_state = game.get_board_state()
                lobby.game_state['clock'] = clock

                await self.broadcast_to_lobby(lobby, {
                    'type': 'promotion_applied',
                    'game_state': lobby.game_state
                })
                break
    
    def _reconstruct_game_from_state(self, game_state: dict) -> ChessGame:
        """Reconstruct a ChessGame object from the game state"""
        game = ChessGame()
        
        # Clear the board
        game.board = [[None for _ in range(8)] for _ in range(8)]
        
        # Reconstruct pieces from state
        for row in range(8):
            for col in range(8):
                piece_data = game_state['board'][row][col]
                if piece_data:
                    piece = Piece(
                        type=PieceType(piece_data['type']),
                        color=Color(piece_data['color']),
                        abilities=[PieceType(ability) for ability in piece_data['abilities']],
                        position=(row, col),
                        has_moved=bool(piece_data.get('has_moved', False))
                    )
                    game.board[row][col] = piece
        
        # Restore game state
        game.current_turn = Color(game_state['current_turn'])
        game.game_over = game_state['game_over']
        game.winner = Color(game_state['winner']) if game_state['winner'] else None
        game.move_history = game_state['move_history']
        # Restore en passant target if present, normalize to tuple
        ep = game_state.get('en_passant_target')
        if ep is not None:
            try:
                game.en_passant_target = (int(ep[0]), int(ep[1]))
            except Exception:
                game.en_passant_target = None
        # Restore promotion pending if present
        pp = game_state.get('promotion_pending')
        if pp:
            try:
                game.promotion_pending = {
                    'row': int(pp['row']),
                    'col': int(pp['col']),
                    'color': pp['color'],  # string
                    'from': tuple(pp.get('from')) if pp.get('from') else None
                }
            except Exception:
                game.promotion_pending = None
        
        return game

    def is_promotion_pending(self) -> bool:
        return self.promotion_pending is not None

    def revert_pawn_after_cancel(self, to_pos: tuple, from_pos: tuple):
        tr, tc = to_pos
        fr, fc = from_pos
        pawn = self.get_piece_at(tr, tc)
        # Move the pawn back
        self.board[fr][fc] = pawn
        self.board[tr][tc] = None
        if pawn:
            pawn.position = (fr, fc)
            # Clear any capture recorded implicitly
    
    async def broadcast_to_lobby(self, lobby: Lobby, message: dict):
        for player in lobby.players:
            try:
                await self.send_message(player.websocket, message)
            except websockets.exceptions.ConnectionClosed:
                pass

    async def handle_resign(self, client_id: str, websocket):
        for lobby_code, lobby in self.lobbies.items():
            if any(p.id == client_id for p in lobby.players) and lobby.game_state:
                # Determine winner as the opponent of resigning player
                resigning = next((p for p in lobby.players if p.id == client_id), None)
                if not resigning:
                    return
                winner_color = Color.BLACK if resigning.color == Color.WHITE else Color.WHITE
                # Mark game over
                lobby.game_state['game_over'] = True
                lobby.game_state['winner'] = winner_color.value
                await self.broadcast_to_lobby(lobby, {
                    'type': 'game_over',
                    'reason': 'resign',
                    'game_state': lobby.game_state
                })
                break

    async def handle_offer_draw(self, client_id: str, websocket):
        now = datetime.datetime.utcnow()
        # Rate limit: max 3 offers per minute per player
        history = self.draw_offer_history.setdefault(client_id, [])
        history = [t for t in history if (now - t).total_seconds() <= 60]
        self.draw_offer_history[client_id] = history
        if len(history) >= 3:
            # Tell sender rate limited
            await self.send_message(websocket, {
                'type': 'draw_offer_rate_limited',
                'retry_after': 60 - int((now - history[0]).total_seconds())
            })
            return
        # Record this offer time
        history.append(now)
        self.draw_offer_history[client_id] = history

        for lobby_code, lobby in self.lobbies.items():
            if any(p.id == client_id for p in lobby.players) and lobby.game_state:
                # Send draw offer to the opponent
                for p in lobby.players:
                    if p.id != client_id:
                        await self.send_message(p.websocket, {
                            'type': 'draw_offered',
                            'from': client_id
                        })
                # Acknowledge to sender
                await self.send_message(websocket, {
                    'type': 'draw_offer_ack'
                })
                break

    async def handle_accept_draw(self, client_id: str, websocket):
        for lobby_code, lobby in self.lobbies.items():
            if any(p.id == client_id for p in lobby.players) and lobby.game_state:
                lobby.game_state['game_over'] = True
                lobby.game_state['winner'] = None
                await self.broadcast_to_lobby(lobby, {
                    'type': 'game_over',
                    'reason': 'draw',
                    'game_state': lobby.game_state
                })
                break

    async def handle_decline_draw(self, client_id: str, websocket, data: dict):
        # Notify the original offerer of the decline if specified
        offerer_id = data.get('to')
        if not offerer_id:
            # Try to find opponent id
            for lobby_code, lobby in self.lobbies.items():
                if any(p.id == client_id for p in lobby.players):
                    for p in lobby.players:
                        if p.id != client_id:
                            offerer_id = p.id
                            break
                    break
        if offerer_id and offerer_id in self.connected_clients:
            try:
                await self.send_message(self.connected_clients[offerer_id], {
                    'type': 'draw_declined',
                    'from': client_id
                })
            except websockets.exceptions.ConnectionClosed:
                pass

    async def handle_timeout(self, client_id: str, websocket):
        # End game immediately when the active player's clock reaches 0
        for lobby_code, lobby in self.lobbies.items():
            if any(p.id == client_id for p in lobby.players) and lobby.game_state:
                if lobby.game_state.get('game_over'):
                    return
                # Winner is opponent of current turn
                current_turn = lobby.game_state.get('current_turn')
                winner = 'black' if current_turn == 'white' else 'white'
                lobby.game_state['game_over'] = True
                lobby.game_state['winner'] = winner
                await self.broadcast_to_lobby(lobby, {
                    'type': 'game_over',
                    'reason': 'timeout',
                    'game_state': lobby.game_state
                })
                break
    
    async def send_message(self, websocket, message: dict):
        await websocket.send(json.dumps(message))
    
    async def send_error(self, websocket, error_message: str):
        await self.send_message(websocket, {
            'type': 'error',
            'message': error_message
        })
    
    async def handle_disconnect(self, client_id: str):
        # Remove from connected clients
        if client_id in self.connected_clients:
            del self.connected_clients[client_id]
        
        # Handle lobby cleanup
        await self.leave_lobby(client_id, None, {})
    
    def generate_lobby_code(self) -> str:
        """Generate a unique 6-character lobby code"""
        import random
        import string
        while True:
            code = ''.join(random.choices(string.ascii_uppercase + string.digits, k=6))
            if code not in self.lobbies:
                return code

# Start the server
async def main():
    server = GameServer()
    print("Chess server starting on ws://localhost:8765")
    
    async with websockets.serve(server.register_client, "0.0.0.0", 8765):
        await asyncio.Future()  # Run forever

if __name__ == "__main__":
    asyncio.run(main())
