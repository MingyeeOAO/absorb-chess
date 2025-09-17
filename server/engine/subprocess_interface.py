"""
Chess Engine Subprocess Interface
Communicates with C++ executable via stdin/stdout using simple text protocol
"""

import subprocess
import os
import threading
import queue
import time
import logging
from typing import List, Tuple, Optional, Dict, Any

logger = logging.getLogger(__name__)

class ChessEngineProcess:
    """Interface to C++ chess engine executable via subprocess"""
    
    def __init__(self, exe_path: Optional[str] = None):
        self.process = None
        self.exe_path = exe_path
        self._response_queue = queue.Queue()
        self._reader_thread = None
        self._is_ready = False
        
        if self.exe_path is None:
            # Look for executable in engine directory
            engine_dir = os.path.dirname(__file__)
            self.exe_path = os.path.join(engine_dir, "chess_engine.exe")
        
        self._start_process()
    
    def _start_process(self):
        """Start the C++ engine process"""
        try:
            self.process = subprocess.Popen(
                [self.exe_path],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                bufsize=0  # Unbuffered
            )
            
            # Start reader thread
            self._reader_thread = threading.Thread(target=self._read_responses, daemon=True)
            self._reader_thread.start()
            
            # Wait for ready signal
            ready_response = self._wait_for_response(timeout=5.0)
            if ready_response and ready_response.startswith('READY'):
                self._is_ready = True
                logger.info(f"C++ engine process started successfully: {self.exe_path}")
            else:
                raise RuntimeError(f"Engine did not send ready signal, got: {ready_response}")
                
        except Exception as e:
            logger.error(f"Failed to start C++ engine process: {e}")
            self._cleanup()
            raise
    
    def _read_responses(self):
        """Background thread to read responses from engine"""
        while self.process and self.process.poll() is None:
            try:
                line = self.process.stdout.readline()
                if line:
                    line = line.strip()
                    if line:
                        self._response_queue.put(line)
            except Exception as e:
                logger.error(f"Error reading from engine process: {e}")
                break
    
    def _send_command(self, command: str) -> bool:
        """Send command to engine"""
        if not self.process or self.process.poll() is not None:
            logger.error("Engine process is not running")
            return False
        
        try:
            command_str = command + '\n'
            self.process.stdin.write(command_str)
            self.process.stdin.flush()
            return True
        except Exception as e:
            logger.error(f"Failed to send command to engine: {e}")
            return False
    
    def _wait_for_response(self, timeout: float = 10.0) -> Optional[str]:
        """Wait for response from engine"""
        try:
            return self._response_queue.get(timeout=timeout)
        except queue.Empty:
            logger.error("Timeout waiting for engine response")
            return None
    
    def _format_board_command(self, board: List[List[int]], white_to_move: bool = True,
                             white_king_castled: bool = False, black_king_castled: bool = False,
                             en_passant_col: int = -1, en_passant_row: int = -1) -> str:
        """Format board state for command"""
        parts = [
            str(1 if white_to_move else 0),
            str(1 if white_king_castled else 0),
            str(1 if black_king_castled else 0),
            str(en_passant_col),
            str(en_passant_row)
        ]
        
        # Add all board pieces
        for row in board:
            for piece in row:
                parts.append(str(piece))
        
        return ' '.join(parts)
    
    def set_board_state(self, board: List[List[int]], white_to_move: bool = True,
                       white_king_castled: bool = False, black_king_castled: bool = False,
                       en_passant_col: int = -1, en_passant_row: int = -1) -> bool:
        """Set the current board state"""
        if not self._is_ready:
            return False
        
        board_data = self._format_board_command(board, white_to_move, white_king_castled, 
                                              black_king_castled, en_passant_col, en_passant_row)
        command = f"SET_BOARD {board_data}"
        
        if self._send_command(command):
            response = self._wait_for_response()
            return response and response.startswith('OK')
        return False
    
    def find_best_move(self, board: List[List[int]], depth: int = 4, time_limit_ms: int = 5000,
                      white_to_move: bool = True, white_king_castled: bool = False,
                      black_king_castled: bool = False, en_passant_col: int = -1,
                      en_passant_row: int = -1) -> Optional[Dict[str, Any]]:
        """Find the best move for current position"""
        if not self._is_ready:
            return None
        
        board_data = self._format_board_command(board, white_to_move, white_king_castled, 
                                              black_king_castled, en_passant_col, en_passant_row)
        command = f"FIND_BEST_MOVE {depth} {time_limit_ms} {board_data}"
        
        if self._send_command(command):
            response = self._wait_for_response(timeout=time_limit_ms/1000.0 + 5.0)
            if response and response.startswith('MOVE'):
                parts = response.split()
                if len(parts) >= 7:
                    try:
                        from_row, from_col = int(parts[1]), int(parts[2])
                        to_row, to_col = int(parts[3]), int(parts[4])
                        evaluation = int(parts[5])
                        time_taken = int(parts[6])
                        
                        return {
                            'move': ((from_row, from_col), (to_row, to_col)),
                            'evaluation': evaluation,
                            'depth_reached': depth,
                            'nodes_searched': 0,
                            'time_taken_ms': time_taken
                        }
                    except (ValueError, IndexError) as e:
                        logger.error(f"Failed to parse move response: {response}, error: {e}")
            elif response and response.startswith('ERROR'):
                logger.error(f"Engine error: {response}")
        return None
    
    def get_legal_moves(self, board: List[List[int]], white_to_move: bool = True,
                       white_king_castled: bool = False, black_king_castled: bool = False,
                       en_passant_col: int = -1, en_passant_row: int = -1) -> List[Tuple[Tuple[int, int], Tuple[int, int]]]:
        """Get all legal moves for current position"""
        if not self._is_ready:
            return []
        
        board_data = self._format_board_command(board, white_to_move, white_king_castled, 
                                              black_king_castled, en_passant_col, en_passant_row)
        command = f"GET_LEGAL_MOVES {board_data}"
        
        if self._send_command(command):
            response = self._wait_for_response()
            if response and response.startswith('MOVES'):
                parts = response.split()
                if len(parts) >= 2:
                    try:
                        move_count = int(parts[1])
                        moves = []
                        for i in range(2, 2 + move_count):
                            if i < len(parts):
                                move_parts = parts[i].split(',')
                                if len(move_parts) == 4:
                                    from_pos = (int(move_parts[0]), int(move_parts[1]))
                                    to_pos = (int(move_parts[2]), int(move_parts[3]))
                                    moves.append((from_pos, to_pos))
                        return moves
                    except (ValueError, IndexError) as e:
                        logger.error(f"Failed to parse moves response: {response}, error: {e}")
        return []
    
    def is_ready(self) -> bool:
        """Check if engine is ready"""
        return self._is_ready and self.process and self.process.poll() is None
    
    def _cleanup(self):
        """Clean up process and resources"""
        if self.process:
            try:
                if self.process.poll() is None:
                    self.process.stdin.write("QUIT\n")
                    self.process.stdin.flush()
                    self.process.wait(timeout=2.0)
            except:
                pass
            
            try:
                if self.process.poll() is None:
                    self.process.terminate()
                    self.process.wait(timeout=2.0)
            except:
                pass
            
            try:
                if self.process.poll() is None:
                    self.process.kill()
            except:
                pass
            
            self.process = None
        
        self._is_ready = False
    
    def __del__(self):
        self._cleanup()
    
    def close(self):
        """Explicitly close the engine"""
        self._cleanup()


class SubprocessChessEngine:
    """Chess engine interface using subprocess - compatible with existing bot_engine.py"""
    
    def __init__(self, exe_path: Optional[str] = None):
        self.engine_process = None
        self.exe_path = exe_path
        try:
            self.engine_process = ChessEngineProcess(exe_path)
            logger.info("🚀 C++ Engine Process Active")
        except Exception as e:
            logger.error(f"Failed to initialize subprocess engine: {e}")
            raise
    
    def find_best_move(self, chess_game, depth: int = 4, time_limit: int = 5) -> Optional[Tuple[Tuple[int, int], Tuple[int, int]]]:
        """Find best move - compatible with bot_engine.py interface"""
        if not self.engine_process or not self.engine_process.is_ready():
            logger.warning("Engine process not ready")
            return None
        
        try:
            # Validate input parameters
            if depth < 1:
                depth = 3
            if time_limit < 1:
                time_limit = 5
                
            # Convert chess game to board state
            # Convert Piece objects to numerical board format for C++ engine
            board = [[0 for _ in range(8)] for _ in range(8)]
            
            for i in range(8):
                for j in range(8):
                    piece = chess_game.board[i][j]
                    if piece is not None:
                        board[i][j] = self._piece_to_number(piece)
            
            # Handle different attribute names for current player
            if hasattr(chess_game, 'white_to_move'):
                white_to_move = chess_game.white_to_move
            elif hasattr(chess_game, 'current_turn'):
                # Convert Color enum to boolean
                from server.core.enums import Color
                white_to_move = (chess_game.current_turn == Color.WHITE)
            else:
                white_to_move = True  # Default to white's turn
            
            # Debug logging
            logger.debug(f"C++ Engine Input - Turn: {'White' if white_to_move else 'Black'}")
            piece_count = sum(1 for row in board for cell in row if cell != 0)
            logger.debug(f"C++ Engine Input - Pieces on board: {piece_count}")
            
            # Validate we have pieces on the board
            if piece_count == 0:
                logger.error("No pieces found on board - invalid game state")
                return None
            
            # Handle castling flags
            white_king_castled = False
            black_king_castled = False
            if hasattr(chess_game, 'white_king_castled'):
                white_king_castled = chess_game.white_king_castled
            elif hasattr(chess_game, 'king_castled'):
                from server.core.enums import Color
                white_king_castled = chess_game.king_castled.get(Color.WHITE, False)
                black_king_castled = chess_game.king_castled.get(Color.BLACK, False)
            
            if hasattr(chess_game, 'black_king_castled'):
                black_king_castled = chess_game.black_king_castled
            
            result = self.engine_process.find_best_move(
                board=board,
                depth=depth,
                time_limit_ms=time_limit * 1000,
                white_to_move=white_to_move,
                white_king_castled=white_king_castled,
                black_king_castled=black_king_castled,
                en_passant_col=getattr(chess_game, 'en_passant_col', -1),
                en_passant_row=getattr(chess_game, 'en_passant_row', -1)
            )
            
            if result:
                move = result['move']
                logger.info(f"C++ process found move: {move} (eval: {result['evaluation']}, time: {result['time_taken_ms']}ms)")
                
                # Validate the move before returning
                if move == ((0, 0), (0, 0)):
                    logger.warning("C++ engine returned invalid move (0,0) -> (0,0)")
                    return None
                    
                return move
            else:
                logger.warning("C++ engine returned no result")
                return None
            
        except Exception as e:
            logger.error(f"Error in subprocess engine find_best_move: {e}")
        
        return None
    
    def _piece_to_number(self, piece):
        """Convert a Piece object to the numerical format expected by C++ engine"""
        if piece is None:
            return 0
        
        from server.core.enums import PieceType, Color
        
        # Base piece type
        piece_value = 0
        if piece.type == PieceType.PAWN:
            piece_value = 1  # PIECE_PAWN
        elif piece.type == PieceType.KNIGHT:
            piece_value = 2  # PIECE_KNIGHT
        elif piece.type == PieceType.BISHOP:
            piece_value = 4  # PIECE_BISHOP
        elif piece.type == PieceType.ROOK:
            piece_value = 8  # PIECE_ROOK
        elif piece.type == PieceType.QUEEN:
            piece_value = 16  # PIECE_QUEEN
        elif piece.type == PieceType.KING:
            piece_value = 32  # PIECE_KING
        
        # Add abilities
        for ability in piece.abilities:
            if ability == PieceType.PAWN:
                piece_value |= 64  # ABILITY_PAWN
            elif ability == PieceType.KNIGHT:
                piece_value |= 128  # ABILITY_KNIGHT
            elif ability == PieceType.BISHOP:
                piece_value |= 256  # ABILITY_BISHOP
            elif ability == PieceType.ROOK:
                piece_value |= 512  # ABILITY_ROOK
            elif ability == PieceType.QUEEN:
                piece_value |= 1024  # ABILITY_QUEEN
            elif ability == PieceType.KING:
                piece_value |= 2048  # ABILITY_KING
        
        # Add color and movement flags
        if piece.color == Color.WHITE:
            piece_value |= 8192  # IS_WHITE
        
        if piece.has_moved:
            piece_value |= 4096  # HAS_MOVED
        
        return piece_value
    
    def get_evaluation(self) -> int:
        """Get current position evaluation"""
        # This would need to be cached from the last find_best_move call
        # For now, return 0
        return 0
    
    def is_available(self) -> bool:
        """Check if engine is available"""
        return self.engine_process and self.engine_process.is_ready()
    
    def close(self):
        """Close the engine"""
        if self.engine_process:
            self.engine_process.close()
            self.engine_process = None