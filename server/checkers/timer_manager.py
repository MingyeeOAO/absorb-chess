import asyncio
import time
from typing import Dict, Callable, Optional
from server.core.state import GlobalState

class TimerManager:
    def __init__(self, timeout_callback: Callable):
        self.state = GlobalState.get_instance()
        self.timeout_callback = timeout_callback
        self.running = False
        self.check_interval = 0.1  # Check every 100ms for responsiveness
        self.processing_timeouts = set()  # Track lobbies currently processing timeout
        
    async def start(self):
        """Start the timer monitoring"""
        self.running = True
        while self.running:
            await self.check_timeouts()
            await asyncio.sleep(self.check_interval)
    
    def stop(self):
        """Stop the timer monitoring"""
        self.running = False
    
    async def check_timeouts(self):
        """Check all active games for timeouts"""
        current_time = time.time() * 1000  # Convert to milliseconds
        
        all_lobbies = self.state.get_all_lobbies()
        for lobby_code, lobby in all_lobbies.items():
            if not lobby.game_state or lobby.game_state.get('game_over'):
                continue
                
            # Check if all players are disconnected - if so, end the game
            connected_players = [p for p in lobby.players if p.websocket is not None]
            if not connected_players:
                print(f"[TIMER] All players disconnected in lobby {lobby_code}, ending game")
                lobby.game_state['game_over'] = True
                lobby.game_state['winner'] = None  # Draw due to abandonment
                lobby.game_state['timeout_player'] = None
                self.state.update_lobby_game_state(lobby_code, lobby.game_state)
                continue
                
            clock = lobby.game_state.get('clock')
            if not clock:
                continue
                
            # Calculate remaining time for current player
            current_turn = lobby.game_state.get('current_turn')
            if not current_turn:
                continue
                
            last_turn_start = clock.get('last_turn_start')
            if not last_turn_start:
                continue
                
            # Calculate elapsed time since turn started
            if isinstance(last_turn_start, str):
                # Handle ISO string format
                try:
                    import datetime
                    # Handle both with and without 'Z' suffix for UTC
                    last_turn_clean = last_turn_start.rstrip('Z')
                    last_dt = datetime.datetime.fromisoformat(last_turn_clean)
                    elapsed_ms = (datetime.datetime.utcnow() - last_dt).total_seconds() * 1000
                except:
                    continue
            else:
                # Handle timestamp format
                elapsed_ms = current_time - float(last_turn_start)
            
            # Get current player's remaining time
            time_key = f'{current_turn}_ms'
            remaining_time = clock.get(time_key, 0)
            
            # Check if time has run out (remaining time minus elapsed time)
            time_left = remaining_time - elapsed_ms
            if time_left <= 0:
                print(f"[TIMER] Timeout detected in lobby {lobby_code}, player {current_turn}, remaining: {remaining_time}, elapsed: {elapsed_ms}, time_left: {time_left}")
                # Player has timed out - only process if not already processing and game isn't over
                if lobby_code not in self.processing_timeouts and not lobby.game_state.get('game_over'):
                    # Mark this lobby as processing timeout to prevent multiple processing
                    self.processing_timeouts.add(lobby_code)
                    
                    winner = 'black' if current_turn == 'white' else 'white'
                    print(f"[TIMER] Setting winner to {winner} for lobby {lobby_code}")
                    
                    try:
                        # Call timeout callback - it will handle game state updates
                        print(f"[TIMER] Calling timeout callback for lobby {lobby_code}")
                        await self.timeout_callback(lobby_code, current_turn, winner)
                        
                    finally:
                        # Always remove from processing set
                        self.processing_timeouts.discard(lobby_code)
                    
                elif lobby.game_state.get('game_over'):
                    print(f"[TIMER] Game already over in lobby {lobby_code}, skipping timeout")
                else:
                    print(f"[TIMER] Timeout already being processed for lobby {lobby_code}, skipping")