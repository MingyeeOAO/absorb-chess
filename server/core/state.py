from typing import Dict, Optional, TYPE_CHECKING
from dataclasses import dataclass
import websockets
import datetime
import json
import threading

if TYPE_CHECKING:
    from .models import Lobby

@dataclass
class GlobalState:
    """Singleton class to store global state"""
    searching_players: Dict[str, tuple]  # {client_id: (websocket, name)}
    draw_offer_history: Dict[str, list]
    game_timeouts: Dict[str, datetime.datetime]  # {client_id: timeout_time}
    client_states: Dict[str, dict]  # {client_id: {'status': 'in_game'|'searching'|'idle', ...}}
    
    # Lobby storage
    lobbies: Dict[str, 'Lobby']  # {lobby_code: Lobby}
    client_lobby_map: Dict[str, str]  # {client_id: lobby_code}
    
    # Connection tracking
    connected_clients: Dict[str, websockets.WebSocketServerProtocol] 
    last_connection_check: Dict[str, datetime.datetime]
    missed_checks: Dict[str, int]
    _instance = None
    _lock = threading.Lock()  # Lock for thread-safe singleton


    @classmethod
    def get_instance(cls):
        if cls._instance is None:   # first check (fast path)
            with cls._lock:         # Lock to prevent simultaneous creation
                if cls._instance is None:  # Double-checked locking
                    cls._instance = cls(
                        searching_players={},
                        draw_offer_history={},
                        game_timeouts={},
                        client_states={},
                        lobbies={},
                        client_lobby_map={},
                        connected_clients={},
                        last_connection_check={},
                        missed_checks={}
                    )
        return cls._instance

       

    def update_client_state(self, client_id: str, status: str, **kwargs):
        """Update a client's state with additional data"""
        if client_id not in self.client_states:
            self.client_states[client_id] = {}
        self.client_states[client_id]['status'] = status
        self.client_states[client_id].update(kwargs)

    def get_client_state(self, client_id: str) -> Optional[dict]:
        """Get the current state of a client"""
        return self.client_states.get(client_id)

    def register_client(self, client_id: str, websocket: websockets.WebSocketServerProtocol):
        """Register a new client connection"""
        self.connected_clients[client_id] = websocket
        self.last_connection_check[client_id] = datetime.datetime.now()
        self.missed_checks[client_id] = 0
        self.update_client_state(client_id, 'idle')

    def unregister_client(self, client_id: str):
        """Remove a client when they disconnect"""
        if client_id in self.connected_clients:
            del self.connected_clients[client_id]
        if client_id in self.last_connection_check:
            del self.last_connection_check[client_id]
        if client_id in self.missed_checks:
            del self.missed_checks[client_id]
        self.remove_searching_player(client_id)
        self.remove_player_from_lobby(client_id)
        if client_id in self.client_states:
            del self.client_states[client_id]

    def add_searching_player(self, client_id: str, websocket: websockets.WebSocketServerProtocol, name: str):
        self.searching_players[client_id] = (websocket, name)

    def remove_searching_player(self, client_id: str):
        if client_id in self.searching_players:
            del self.searching_players[client_id]

    def get_searching_players(self) -> Dict[str, tuple]:
        return self.searching_players.copy()

    def clear_searching_player(self):
        self.searching_players.clear()

    def add_draw_offer(self, offerer_id: str, target_id: str):
        if offerer_id not in self.draw_offer_history:
            self.draw_offer_history[offerer_id] = []
        self.draw_offer_history[offerer_id].append(target_id)

    def has_draw_offer(self, offerer_id: str, target_id: str) -> bool:
        return (offerer_id in self.draw_offer_history and 
                target_id in self.draw_offer_history[offerer_id])
        
    def update_connection_check(self, client_id: str):
        """Update the last connection check time for a client"""
        self.last_connection_check[client_id] = datetime.datetime.now()
        self.missed_checks[client_id] = 0

    def increment_missed_checks(self, client_id: str) -> int:
        """Increment the number of missed connection checks for a client"""
        if client_id in self.connected_clients:
            self.missed_checks[client_id] = self.missed_checks.get(client_id, 0) + 1
            return self.missed_checks[client_id]
        return 0

    def get_client_websocket(self, client_id: str) -> Optional[websockets.WebSocketServerProtocol]:
        """Get the websocket for a connected client"""
        return self.connected_clients.get(client_id)

    def get_last_check_time(self, client_id: str) -> Optional[datetime.datetime]:
        """Get the last connection check time for a client"""
        return self.last_connection_check.get(client_id)

    def is_client_connected(self, client_id: str) -> bool:
        """Check if a client is connected and within missed check threshold"""
        return (client_id in self.connected_clients and 
                self.missed_checks.get(client_id, 0) < 8)  # 40 seconds (8 * 5s checks)

    def get_client_info(self, client_id: str) -> Optional[Dict]:
        """Get detailed information about a connected client"""
        if client_id in self.connected_clients:
            return {
                'id': client_id,
                'connected_since': self.last_connection_check[client_id].isoformat(),
                'missed_checks': self.missed_checks.get(client_id, 0),
                'status': self.client_states.get(client_id, {}).get('status', 'unknown')
            }
        return None

    async def broadcast_to_clients(self, message: dict, exclude_client_id: str = None):
        """Send a message to all connected clients except the excluded one"""
        for client_id, websocket in list(self.connected_clients.items()):
            if client_id != exclude_client_id:
                try:
                    await websocket.send(json.dumps(message))
                except websockets.exceptions.ConnectionClosed:
                    pass

    # Lobby management methods
    def add_lobby(self, lobby_code: str, lobby: 'Lobby'):
        """Add a lobby to the global state"""
        self.lobbies[lobby_code] = lobby
        # Map the owner to this lobby
        if lobby.owner_id:
            self.client_lobby_map[lobby.owner_id] = lobby_code

    def remove_lobby(self, lobby_code: str):
        """Remove a lobby and update client mappings"""
        if lobby_code in self.lobbies:
            lobby = self.lobbies[lobby_code]
            # Remove all players from client_lobby_map
            for player in lobby.players:
                if player.id in self.client_lobby_map:
                    del self.client_lobby_map[player.id]
            # Remove the lobby
            del self.lobbies[lobby_code]

    def get_lobby(self, lobby_code: str) -> Optional['Lobby']:
        """Get a lobby by its code"""
        return self.lobbies.get(lobby_code)

    def get_lobby_by_client(self, client_id: str) -> Optional['Lobby']:
        """Get the lobby that a client is currently in"""
        lobby_code = self.client_lobby_map.get(client_id)
        if lobby_code:
            return self.lobbies.get(lobby_code)
        return None

    def add_player_to_lobby(self, client_id: str, lobby_code: str):
        """Map a client to a lobby"""
        self.client_lobby_map[client_id] = lobby_code

    def remove_player_from_lobby(self, client_id: str):
        """Remove client's lobby mapping"""
        if client_id in self.client_lobby_map:
            del self.client_lobby_map[client_id]

    def get_all_lobbies(self) -> Dict[str, 'Lobby']:
        """Get all lobbies"""
        return self.lobbies.copy()

    def lobby_exists(self, lobby_code: str) -> bool:
        """Check if a lobby exists"""
        return lobby_code in self.lobbies