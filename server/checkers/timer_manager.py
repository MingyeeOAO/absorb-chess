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
                    last_dt = datetime.datetime.fromisoformat(last_turn_start)
                    elapsed_ms = (datetime.datetime.now() - last_dt).total_seconds() * 1000
                except:
                    continue
            else:
                # Handle timestamp format
                elapsed_ms = current_time - float(last_turn_start)
            
            # Get current player's remaining time
            time_key = f'{current_turn}_ms'
            remaining_time = clock.get(time_key, 0)
            
            # Check if time has run out
            if remaining_time <= elapsed_ms:
                # Player has timed out
                winner = 'black' if current_turn == 'white' else 'white'
                
                # Update game state
                lobby.game_state['game_over'] = True
                lobby.game_state['winner'] = winner
                lobby.game_state['timeout_player'] = current_turn
                
                # Notify via callback
                await self.timeout_callback(lobby_code, current_turn, winner)