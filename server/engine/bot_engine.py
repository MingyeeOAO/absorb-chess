"""
Optimized Bot Engine Interface
Uses the best available engine with performance optimization
"""

import logging
from typing import Tuple, Optional, Dict


logger = logging.getLogger(__name__)

class BotEngineManager:
    """Manages bot engine with fallback and optimization"""
    
    def __init__(self):
        self.cpp_engine = None
        self.python_engine = None
        self.use_cpp = False
        
        # Try subprocess C++ engine first
        try:
            from server.engine.subprocess_interface import SubprocessChessEngine
            self.cpp_engine = SubprocessChessEngine()
            if self.cpp_engine.is_available():
                self.use_cpp = True
                #logger.info("[ENGINE] C++ subprocess engine loaded successfully")
            else:
                logger.warning("[ENGINE] C++ subprocess engine not available")
        except Exception as e:
            logger.warning(f"[ENGINE] C++ subprocess engine unavailable: {e}")
    
        # Always have Python engine as backup
        try:
            from server.engine.engine import Engine
            # We'll create the engine when we need it with the actual game state
            self.python_engine_class = Engine
            if not self.use_cpp:
                logger.info("[ENGINE] Using optimized Python engine")
        except Exception as e:
            logger.error(f"[ENGINE] Failed to load Python engine: {e}")
            raise RuntimeError("No chess engine available!")
    
    def find_best_move(self, game_state, depth: int = 4, time_limit_ms: int = 15000) -> Tuple[Optional[Dict], int, str]:
        """Find best move using the best available engine"""
        
        # Try C++ engine first if available
        if self.use_cpp and self.cpp_engine:
            try:
                move = self.cpp_engine.find_best_move(game_state, depth, time_limit_ms)
                if move:
                    evaluation = self.cpp_engine.get_evaluation()  # No game_state parameter needed
                    
                    # Convert tuple format to dictionary format
                    if isinstance(move, tuple) and len(move) == 2:
                        # Handle tuple format: ((from_row, from_col), (to_row, to_col))
                        from_pos, to_pos = move
                        move_dict = {
                            'from': list(from_pos),
                            'to': list(to_pos)
                        }
                        logger.debug(f"[ENGINE] Converted C++ move {move} to {move_dict}")
                        return move_dict, evaluation, "[ENGINE] C++ Engine"
                    elif isinstance(move, dict) and 'from' in move and 'to' in move:
                        # Already in correct format
                        logger.debug(f"[ENGINE] C++ move already in dict format: {move}")
                        return move, evaluation, "[ENGINE] C++ Engine"
                    else:
                        logger.warning(f"[ENGINE] Unexpected move format from C++: {move} (type: {type(move)})")
                        return move, evaluation, "[ENGINE] C++ Engine"
                else:
                    logger.warning("[ENGINE] C++ engine returned no move, falling back")
                    self.use_cpp = False
            except Exception as e:
                logger.error(f"[ENGINE] C++ engine error: {e}, falling back to Python")
                self.use_cpp = False
        
        # Use Python engine (fallback or primary)
        if self.python_engine_class:
            try:
                # Create engine instance with the current game state
                engine = self.python_engine_class(game_state)
                
                # Use optimized settings for bot performance
                time_limit_sec = min(time_limit_ms / 1000.0, 20.0)  # Cap at 20 seconds
                
                # Adaptive depth based on game complexity
                move_count = self._count_pieces(game_state)
                if move_count > 20:  # Opening/middlegame
                    bot_depth = max(3, min(depth, 4))
                elif move_count > 10:  # Late middlegame
                    bot_depth = max(4, min(depth + 1, 5))
                else:  # Endgame
                    bot_depth = max(5, min(depth + 2, 6))
                
                best_move, evaluation, analysis = engine.find_best_move_with_time_limit(
                    bot_depth, time_limit_sec
                )
                
                # Convert tuple format to dictionary format if needed
                if best_move and isinstance(best_move, tuple) and len(best_move) == 2:
                    from_pos, to_pos = best_move
                    best_move = {
                        'from': list(from_pos),
                        'to': list(to_pos)
                    }

                engine_info = f"[ENGINE] Python Engine (depth={bot_depth}, pieces={move_count})"
                return best_move, evaluation, engine_info
                
            except Exception as e:
                logger.error(f"[ENGINE] Python engine error: {e}")
                return None, 0, f"[ENGINE] Error: {e}]"

        return None, 0, "[ENGINE] No engine available"

    def find_best_move_dict(self, game_state, depth: int = 4, time_limit_ms: int = 15000) -> Tuple[Optional[Dict], int, str]:
        """Alias for find_best_move for compatibility"""
        return self.find_best_move(game_state, depth, time_limit_ms)
    
    def get_engine_status(self) -> str:
        """Get current engine status for logging"""
        if self.use_cpp and self.cpp_engine:
            return "🚀 C++ Engine Active"
        elif self.python_engine_class:
            return "⚡ Python Engine Active"
        else:
            return "❌ No Engine Available"
    
    def _count_pieces(self, game_state) -> int:
        """Count total pieces on board for adaptive depth"""
        try:
            count = 0
            for row in game_state.board:
                for piece in row:
                    if piece:  # Not empty
                        count += 1
            return count
        except:
            return 16  # Default assumption
    
    def get_engine_status(self) -> str:
        """Get current engine status"""
        if self.use_cpp and self.cpp_engine:
            return "[ENGINE] C++ Engine Active"
        elif self.python_engine_class:
            return "[ENGINE] Python Engine Active"
        else:
            return "[ENGINE] No Engine Available"


# Global instance
_bot_engine_manager = None

def get_bot_engine_manager():
    """Get the global bot engine manager"""
    global _bot_engine_manager
    if _bot_engine_manager is None:
        _bot_engine_manager = BotEngineManager()
    return _bot_engine_manager


# Keep compatibility with existing interface
def get_engine_manager():
    """Compatibility wrapper for existing code"""
    return get_bot_engine_manager()