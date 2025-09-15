import asyncio
import uuid
import websockets
import datetime
from typing import Dict
from server.core.game import ChessGame
from server.handlers.lobby_handler import LobbyHandler
from server.core.state import GlobalState
from server.handlers.game_handler import GameHandler
import json


class ConnectionManager:
    
    def __init__(self, game_handler : GameHandler, message_handler=None):
        """Initialize the connection manager
        
        Args:
            message_handler: Optional callback for handling game-specific messages
        """
        self.state = GlobalState.get_instance()
        self.message_handler = message_handler
        self.auto_resign_tasks = {}
        self.game_handler = game_handler
   
    
    async def send_message(self, websocket, message):
        """Send a message to a specific websocket"""
        try:
            await websocket.send(json.dumps(message))
        except websockets.exceptions.ConnectionClosed:
            pass
            
    
    async def register_client(self, websocket) -> str:
        """Register a new client and start connection checking"""
        # Generate unique client ID
        while True:
            client_id = str(uuid.uuid4())
            if not self.state.get_client_websocket(client_id):
                break
                
        # Register with global state
        self.state.register_client(client_id, websocket)
        
        try:
            # Handle messages until connection closes
            await self.handle_client(client_id, websocket)
        except websockets.exceptions.ConnectionClosed:
            pass
        finally:
            # Clean up when client disconnects
            # Close websocket immediately but don't clean up state yet
            if client_id in self.state.connected_clients:
                try:
                    websocket = self.state.connected_clients[client_id]
                    await websocket.close()
                except Exception:
                    pass
                # Remove from connected_clients but keep everything else
                del self.state.connected_clients[client_id]
            
            # Always call handle_disconnect - it will manage the cleanup timing
            await self.handle_disconnect(client_id)
            
        return client_id
    
    async def broadcast_to_lobby_clients(self, lobby_code: str, message: dict, exclude_client_id: str = None):
        """Send a message to all clients in the specified lobby except the excluded one"""
        lobby = self.state.get_lobby(lobby_code)
        if not lobby:
            return
        for player in lobby.players:
            if player.id != exclude_client_id:
                ws = self.state.get_client_websocket(player.id)
                if ws:
                    try:
                        await ws.send(json.dumps(message))
                    except Exception:
                        pass


    async def unregister_client(self, client_id: str, remove_from_lobby: bool = True):
        """Unregister a client and clean up"""
        if client_id in self.state.connected_clients:
            try:
                websocket = self.state.connected_clients[client_id]
                await websocket.close()
            except Exception:
                pass
        self.state.unregister_client(client_id, remove_from_lobby=remove_from_lobby)
    


    async def handle_disconnect(self, client_id: str):
        """Handle client disconnection and auto-resign after 40 seconds"""
        # If auto-resign is already running for this client, don't start another one
        if client_id in self.auto_resign_tasks:
            return
            
        timestamp = datetime.datetime.now()
        abort_time = timestamp + datetime.timedelta(seconds=40)
        
        # Capture lobby code before any cleanup - this is crucial
        lobby_code = self.state.client_lobby_map.get(client_id)
        
        # Only broadcast to clients in the same lobby as the disconnecting player
        if lobby_code:
            await self.broadcast_to_lobby_clients(lobby_code, {
                'type': 'player_disconnected',
                'playerId': client_id,
                'disconnect_time': int(timestamp.timestamp()),
                'abort_time': int(abort_time.timestamp())
            }, exclude_client_id=client_id)

        # Schedule auto-resign/game over after 40 seconds
        async def auto_resign():
            try:
                await asyncio.sleep(40)
                
                # Just use the lobby_code we captured earlier - much simpler!
                if lobby_code:
                    lobby = self.state.get_lobby(lobby_code)
                    # print(f"[Auto-Resign] lobby: {lobby}, player: {client_id}")
                    # print(f"[Auto-Resign] using lobby_code: {lobby_code}")
                    
                    # Only resign if game is still active
                    if lobby and lobby.game_state and not lobby.game_state.get('game_over'):
                        response = await self.game_handler.handle_resign(client_id, None, lobby_code)
                        # print(f"[Auto-Resign] handle_resign response: {response}")
                        if response:
                            response['reason'] = 'disconnect'
                            # print(f"[Auto-Resign] broadcasting to lobby_code: {lobby_code}")
                            await self.broadcast_to_lobby_clients(lobby_code, response)
                
                # NOW clean up everything - after auto-resign is complete
                # print(f"[Auto-Resign] Cleaning up client {client_id}")
                await self.unregister_client(client_id, remove_from_lobby=True)
                
            except asyncio.CancelledError:
                # If cancelled (reconnected), don't clean up - client is still active
                print(f"[Auto-Resign] Cancelled for client {client_id} - client reconnected")
            finally:
                # Remove from auto_resign_tasks
                if client_id in self.auto_resign_tasks:
                    del self.auto_resign_tasks[client_id]

        task = asyncio.create_task(auto_resign())
        self.auto_resign_tasks[client_id] = task
        
    async def handle_reconnection(self, client_id: str):
        """Handle client reconnection and cancel auto-resign"""
        # Cancel auto-resign if scheduled
        if client_id in self.auto_resign_tasks:
            self.auto_resign_tasks[client_id].cancel()
            del self.auto_resign_tasks[client_id]
            print(f"[Reconnection] Cancelled auto-resign for {client_id}")
        
        if client_id in self.state.connected_clients:
            timestamp = datetime.datetime.now()
            lobby_code = self.state.client_lobby_map.get(client_id)
            if lobby_code:
                await self.broadcast_to_lobby_clients(lobby_code, {
                    'type': 'player_reconnected',
                    'player_id': client_id,
                    'timestamp': timestamp.isoformat()
                }, exclude_client_id=client_id)

    async def handle_client(self, client_id: str, websocket):
        """Handle incoming messages from a client"""
        async for message in websocket:
            try:
                data = json.loads(message)
                
                # Forward game-specific messages to the handler
                if self.message_handler:
                    await self.message_handler(client_id, websocket, data)
                    
            except json.JSONDecodeError:
                # print(f"Invalid JSON from {client_id}")
                await self.send_message(websocket, {
                    'type': 'error',
                    'message': 'Invalid JSON'
                })
            except Exception as e:
                # print(f"Error handling message from {client_id}: {str(e)}")
                await self.send_message(websocket, {
                    'type': 'error',
                    'message': f'Internal error: {str(e)}'
                })

    def is_client_connected(self, client_id: str) -> bool:
        """Check if a client is still connected"""
        return self.state.is_client_connected(client_id)

    async def broadcast_to_clients(self, message: dict, exclude_client_id: str = None):
        """Send a message to all connected clients except the excluded one"""
        await self.state.broadcast_to_clients(message, exclude_client_id)

    async def send_message(self, websocket, message):
        """Send a message to a specific websocket"""
        try:
            await websocket.send(json.dumps(message))
        except websockets.exceptions.ConnectionClosed:
            pass

