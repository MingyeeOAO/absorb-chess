import asyncio
import uuid
import websockets
import datetime
from typing import Dict
from ..core.game import ChessGame
from ..handlers.lobby_handler import LobbyHandler
from ..core.state import GlobalState
import json


class ConnectionManager:
    
    def __init__(self, message_handler=None):
        """Initialize the connection manager
        
        Args:
            message_handler: Optional callback for handling game-specific messages
        """
        self.state = GlobalState.get_instance()
        self.connection_check_tasks = {}
        self.message_handler = message_handler
   
    
    async def start_connection_checker(self, client_id: str, websocket):
        """Start periodic connection checking for a client"""
        if client_id in self.connection_check_tasks:
            self.connection_check_tasks[client_id].cancel()
            del self.connection_check_tasks[client_id]
        
        async def checker():
            while True:
                try:
                    await asyncio.sleep(10)
                    await self.check_client_connection(client_id, websocket)
                except Exception as e:
                    print(f"Connection checker error for {client_id}: {str(e)}")
                    break

        self.state.update_connection_check(client_id)
        task = asyncio.create_task(checker())
        self.connection_check_tasks[client_id] = task
    
    async def check_client_connection(self, client_id: str, websocket):
        """Check if a client is still connected and handle timeouts"""
        try:
            timestamp = datetime.datetime.now()
            await self.send_message(websocket, {
                'type': 'connection_check',
                'timestamp': timestamp.isoformat()
            })
            await asyncio.sleep(2)
            
            last_response = self.state.get_last_check_time(client_id)
            if not last_response or (timestamp - last_response).total_seconds() > 10:
                missed = self.state.increment_missed_checks(client_id)
                if missed >= 8:  # 40 seconds (8 * 5s checks)
                    await self.handle_disconnect(client_id)
                    return False
            else:
                if self.state.missed_checks.get(client_id, 0) > 0:
                    self.state.update_connection_check(client_id)
                    await self.handle_reconnection(client_id)
                return True
        except websockets.exceptions.ConnectionClosed:
            await self.handle_disconnect(client_id)
            return False
                
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
        
        # Start connection checker task
        await self.start_connection_checker(client_id, websocket)
        
        try:
            # Handle messages until connection closes
            await self.handle_client(client_id, websocket)
        except websockets.exceptions.ConnectionClosed:
            pass
        finally:
            # Clean up when client disconnects
            if client_id in self.connection_check_tasks:
                self.connection_check_tasks[client_id].cancel()
                del self.connection_check_tasks[client_id]
            await self.handle_disconnect(client_id)
            
        return client_id
    
    async def unregister_client(self, client_id: str):
        """Unregister a client and clean up"""
        if client_id in self.state.connected_clients:
            try:
                websocket = self.state.connected_clients[client_id]
                await websocket.close()
            except Exception:
                pass
            
        self.state.unregister_client(client_id)
    
    async def handle_disconnect(self, client_id: str):
        """Handle client disconnection"""
        # Notify other clients about the disconnection
        timestamp = datetime.datetime.now()
        await self.broadcast_to_clients({
            'type': 'player_disconnected',
            'player_id': client_id,
            'timestamp': timestamp.isoformat()
        }, exclude_client_id=client_id)
        
        await self.unregister_client(client_id)
        
    async def handle_reconnection(self, client_id: str):
        """Handle client reconnection"""
        if client_id in self.state.connected_clients:
            # Notify other clients about the reconnection
            timestamp = datetime.datetime.now()
            await self.broadcast_to_clients({
                'type': 'player_reconnected',
                'player_id': client_id,
                'timestamp': timestamp.isoformat()
            }, exclude_client_id=client_id)

    async def handle_client(self, client_id: str, websocket):
        """Register a new client and start monitoring their connection"""
        while True:
            client_id = str(uuid.uuid4())
            if not self.state.get_client_websocket(client_id):
                break
                
        # Register with global state
        self.state.register_client(client_id, websocket)
        
        # Start connection checker task
        await self.start_connection_checker(client_id, websocket)
        
        try:
            await self.handle_client(client_id, websocket)
        except websockets.exceptions.ConnectionClosed:
            pass
        finally:
            # Cancel connection checker task
            if client_id in self.connection_check_tasks:
                self.connection_check_tasks[client_id].cancel()
                del self.connection_check_tasks[client_id]
            # Clean up when client disconnects
            await self.handle_disconnect(client_id)
    

    
    async def handle_client(self, client_id: str, websocket):
        """Handle incoming messages from a client"""
        async for message in websocket:
            try:
                data = json.loads(message)
                
                # Handle connection-specific messages
                if data.get('type') == 'connection_response':
                    self.state.update_connection_check(client_id)
                    continue
                    
                # Forward game-specific messages to the handler
                if self.message_handler:
                    await self.message_handler(client_id, websocket, data)
                    
            except json.JSONDecodeError:
                print(f"Invalid JSON from {client_id}")
                await self.send_message(websocket, {
                    'type': 'error',
                    'message': 'Invalid JSON'
                })
            except Exception as e:
                print(f"Error handling message from {client_id}: {str(e)}")
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
        
