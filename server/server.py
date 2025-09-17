import asyncio
import websockets
import json
from typing import Dict, Optional

from server.networking.connection import ConnectionManager
from server.handlers.lobby_handler import LobbyHandler
from server.handlers.game_handler import GameHandler
from server.handlers.search_handler import SearchHandler
from server.core.state import GlobalState
from server.core.game import ChessGame
from server.checkers.timer_manager import TimerManager

class GameServer:
    def __init__(self, host: str = "0.0.0.0", port: int = 8765):
        self.host = host
        self.port = port
        # Create game_handler first
        self.game_handler = GameHandler()
        # Create connection_manager without game_handler dependency
        self.connection_manager = ConnectionManager(game_handler=self.game_handler, message_handler=self.handle_message)
        self.lobby_handler = LobbyHandler(self.connection_manager)
        self.search_handler = SearchHandler(self.connection_manager, self.lobby_handler)
        self.state = GlobalState.get_instance()
        self.timer_manager = TimerManager(self.handle_timeout)
        
    async def handle_timeout(self, lobby_code: str, timed_out_player: str, winner: str):
        """Handle automatic timeout from timer manager"""
        print(f"[TIMEOUT] Handling timeout for lobby {lobby_code}, timed_out_player: {timed_out_player}, winner: {winner}")
        lobby = self.state.get_lobby(lobby_code)
        if not lobby or not lobby.game_state:
            print(f"[TIMEOUT] No lobby or game state found for {lobby_code}")
            return
            
        print(f"[TIMEOUT] Players in lobby: {[p.id for p in lobby.players]}")
        print(f"[TIMEOUT] Game over status: {lobby.game_state.get('game_over')}")
        
        # Ensure the timeout information is in the game state
        lobby.game_state['game_over'] = True
        lobby.game_state['winner'] = winner
        lobby.game_state['timeout_player'] = timed_out_player
        
        # Update the database immediately to prevent infinite timeout loop
        print(f"[TIMEOUT] Updating database with game_over=True for lobby {lobby_code}")
        self.state.update_lobby_game_state(lobby_code, lobby.game_state)
        print(f"[TIMEOUT] Database update completed for lobby {lobby_code}")
        
        # Create timeout message with the current game state
        timeout_message = {
            'type': 'game_over',
            'reason': 'timeout',
            'game_state': lobby.game_state,
            'timed_out_player': timed_out_player,
            'winner': winner
        }
        
        print(f"[TIMEOUT] Broadcasting timeout message to lobby {lobby_code}")
        await self.connection_manager.broadcast_to_lobby_clients(lobby_code, timeout_message)
        
    async def handle_message(self, client_id: str, websocket, data: dict):
        """Route messages to appropriate handlers"""
        message_type = data.get('type')
        
        if message_type == 'validate_server':
            await self.connection_manager.send_message(websocket, {
                'type': 'validate_server_response',
                'isChessServer': True
            })
            
        elif message_type == 'search_game':
            await self.search_handler.handle_search_game(client_id, websocket, data)
            
        elif message_type == 'cancel_search':
            await self.search_handler.handle_cancel_search(client_id, websocket)
            
        elif message_type == 'create_lobby':
            response = await self.lobby_handler.create_lobby(client_id, websocket, data)
            await self.connection_manager.send_message(websocket, response)
            
        elif message_type == 'join_lobby':
            response = await self.lobby_handler.join_lobby(client_id, websocket, data)
            if response:
                # Get the lobby to access other players
                lobby = self.lobby_handler.get_lobby_by_client(client_id)
                if lobby:
                    # Send join message to the new player
                    await self.connection_manager.send_message(websocket, response['join'])
                    
                    # Send broadcast message to existing players (excluding the new player)
                    for player in lobby.players:
                        if player.id != client_id:  # Don't send broadcast to joining player
                            broadcast_msg = response['broadcast'].copy()
                            broadcast_msg['is_owner'] = player.id == lobby.owner_id  # Set correct owner status
                            await self.connection_manager.send_message(player.websocket, broadcast_msg)
            else:
                await self.connection_manager.send_message(websocket, {
                    'type': 'error',
                    'message': 'Could not join the lobby, might due to invalid code or the lobby is full'
                })
                
        elif message_type == 'start_game':
            await self.lobby_handler.start_game(client_id, websocket, data)
            # start_game method handles sending messages internally
                        
        elif message_type == 'move_piece':
            lobby = self.lobby_handler.get_lobby_by_client(client_id)
            if lobby and lobby.game_state:
                responses = await self.game_handler.handle_move(client_id, websocket, lobby.game_state, data)
                # If the response is not a list, make it a list for uniform processing
                if not isinstance(responses, list):
                    responses = [responses]
                for response in responses:
                    # Update lobby game state if move was successful
                    if response.get('type') == 'move_made' and response.get('game_state'):
                        self.state.update_lobby_game_state(lobby.code, response['game_state'])
                    # Special handling for promotion_pending - only send to the player whose turn it is
                    if response.get('type') == 'promotion_pending':
                        # Update lobby state for promotion_pending too
                        if response.get('game_state'):
                            self.state.update_lobby_game_state(lobby.code, response['game_state'])
                        current_turn = response.get('game_state', {}).get('current_turn')
                        for player in lobby.players:
                            # Only send to the player whose turn it is to choose promotion
                            # Compare player.color.value (string) with current_turn (string)
                            if player.color.value == current_turn:
                                await self.connection_manager.send_message(player.websocket, response)
                                break
                    else:
                        # For all other move responses, send to all players
                        for player in lobby.players:
                            if hasattr(player, 'websocket') and player.websocket:  # Check for bot players
                                await self.connection_manager.send_message(player.websocket, response)
                
                # After processing all responses, check if bot should move
                if lobby.has_bot and not lobby.game_state.get('game_over'):
                    print(f"[DEBUG] Scheduling bot move for lobby {lobby.code}")
                    # Schedule bot move after a short delay to allow frontend to update
                    asyncio.create_task(self._schedule_bot_move(lobby.code))
                else:
                    print(f"[DEBUG] Not scheduling bot move: has_bot={lobby.has_bot}, game_over={lobby.game_state.get('game_over')}")
                    
        elif message_type == 'get_valid_moves':
            lobby = self.lobby_handler.get_lobby_by_client(client_id)
            if lobby and lobby.game_state:
                game_obj = ChessGame()
                game_obj.load_from_state(lobby.game_state)
                response = await self.game_handler.handle_valid_moves_request(client_id, websocket, game_obj)
                await self.connection_manager.send_message(websocket, response)
                
        elif message_type == 'resign':
            response = await self.game_handler.handle_resign(client_id, websocket)
            if response:
                lobby = self.lobby_handler.get_lobby_by_client(client_id)
                if lobby:
                    self.state.update_lobby_game_state(lobby.code, response['game_state'])
                    for player in lobby.players:
                        await self.connection_manager.send_message(player.websocket, response)
                        
        elif message_type == 'offer_draw':
            response = await self.game_handler.handle_offer_draw(client_id, websocket)
            if response:
                lobby = self.lobby_handler.get_lobby_by_client(client_id)
                if lobby:
                    # Send response to appropriate players
                    if response.get('type') == 'draw_offered':
                        # Send to opponent
                        for player in lobby.players:
                            if player.id != client_id:
                                await self.connection_manager.send_message(player.websocket, response)
                        # Send acknowledgment to offerer
                        await self.connection_manager.send_message(websocket, {'type': 'draw_offer_ack'})
                    else:
                        # Rate limited or other response
                        await self.connection_manager.send_message(websocket, response)
                        
        elif message_type == 'accept_draw':
            response = await self.game_handler.handle_accept_draw(client_id, websocket)
            if response:
                lobby = self.lobby_handler.get_lobby_by_client(client_id)
                if lobby:
                    self.state.update_lobby_game_state(lobby.code, response['game_state'])
                    for player in lobby.players:
                        await self.connection_manager.send_message(player.websocket, response)
                        
        elif message_type == 'decline_draw':
            response = await self.game_handler.handle_decline_draw(client_id, websocket, data)
            if response:
                lobby = self.lobby_handler.get_lobby_by_client(client_id)
                if lobby:
                    # Send to offerer
                    for player in lobby.players:
                        if player.id != client_id:
                            await self.connection_manager.send_message(player.websocket, response)
                            
        elif message_type == 'promotion_choice':
            response = await self.game_handler.handle_promotion_choice(client_id, websocket, data)
            if response:
                lobby = self.lobby_handler.get_lobby_by_client(client_id)
                if lobby:
                    self.state.update_lobby_game_state(lobby.code, response['game_state'])
                    for player in lobby.players:
                        await self.connection_manager.send_message(player.websocket, response)
                        
        elif message_type == 'swap_colors':
            response = await self.lobby_handler.swap_colors(client_id, websocket, data)
            if response:
                lobby = self.lobby_handler.get_lobby_by_client(client_id)
                if lobby:
                    for player in lobby.players:
                        if hasattr(player, 'websocket') and player.websocket:  # Check for bot players
                            # Personalize is_owner for each player
                            player_response = response.copy()
                            player_response['is_owner'] = player.id == lobby.owner_id
                            await self.connection_manager.send_message(player.websocket, player_response)
                            
        elif message_type == 'randomize_colors':
            response = await self.lobby_handler.randomize_colors(client_id, websocket, data)
            if response:
                lobby = self.lobby_handler.get_lobby_by_client(client_id)
                if lobby:
                    for player in lobby.players:
                        if hasattr(player, 'websocket') and player.websocket:  # Check for bot players
                            # Personalize is_owner for each player
                            player_response = response.copy()
                            player_response['is_owner'] = player.id == lobby.owner_id
                            await self.connection_manager.send_message(player.websocket, player_response)
    
    async def client_handler(self, websocket):
        """Handle new client connections"""
        client_id = await self.connection_manager.register_client(websocket)
        try:
            async for message in websocket:
                try:
                    data = json.loads(message)
                    await self.handle_message(client_id, websocket, data)
                except json.JSONDecodeError:
                    await self.connection_manager.send_message(websocket, {
                        'type': 'error',
                        'message': 'Invalid JSON'
                    })
        except websockets.exceptions.ConnectionClosed:
            pass
        finally:
            # Don't remove from lobby here - let connection manager handle it
            await self.connection_manager.unregister_client(client_id, remove_from_lobby=False)
    
    async def _schedule_bot_move(self, lobby_code: str):
        """Schedule a bot move after a short delay"""
        print(f"[DEBUG] _schedule_bot_move called for lobby {lobby_code}")
        await asyncio.sleep(1)  # 1 second delay for better UX
        print(f"[DEBUG] About to call handle_bot_move for lobby {lobby_code}")
        move_result = await self.game_handler.handle_bot_move(lobby_code)
        #print(f"[DEBUG] handle_bot_move returned: {move_result}")
        
        # If bot made a move, broadcast it to all players
        if move_result:
            lobby = self.state.get_lobby(lobby_code)
            if lobby:
                for player in lobby.players:
                    if hasattr(player, 'websocket') and player.websocket:  # Check for bot players
                        await self.connection_manager.send_message(player.websocket, move_result)
    
    async def start(self):
        """Start the game server"""
        # Start the timer manager
        timer_task = asyncio.create_task(self.timer_manager.start())
        
        server = await websockets.serve(self.client_handler, self.host, self.port)
        print(f"Game server started on ws://{self.host}:{self.port}")
        print("Timer manager started for automatic timeout detection")
        
        try:
            await server.wait_closed()
        finally:
            self.timer_manager.stop()
            timer_task.cancel()
            try:
                await timer_task
            except asyncio.CancelledError:
                pass

if __name__ == "__main__":
    server = GameServer()
    asyncio.run(server.start())