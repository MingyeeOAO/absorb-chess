from typing import Dict, Optional, TYPE_CHECKING
import websockets
import datetime
import json
import threading
import sqlite3

if TYPE_CHECKING:
    from .models import Lobby

class GlobalState:
    """Database-backed global state singleton"""
    
    def __init__(self):
        # Only keep things that can't be stored in DB (websockets, runtime state)
        self.connected_clients: Dict[str, websockets.WebSocketServerProtocol] = {}
        
        # Initialize SQLite DB
        self._db_conn = sqlite3.connect('global_state.db', check_same_thread=False)
        self._db_conn.row_factory = sqlite3.Row
        self._init_db()
    
    _instance = None
    _lock = threading.Lock()

    @classmethod
    def get_instance(cls):
        if cls._instance is None:
            with cls._lock:
                if cls._instance is None:
                    cls._instance = cls()
        return cls._instance
        
    def _init_db(self):
        cur = self._db_conn.cursor()
        # Lobbies table
        cur.execute('''CREATE TABLE IF NOT EXISTS lobbies (
            lobby_code TEXT PRIMARY KEY,
            owner_id TEXT,
            game_state TEXT,
            settings TEXT,
            created_at TEXT
        )''')
        # Client-lobby mapping
        cur.execute('''CREATE TABLE IF NOT EXISTS client_lobby_map (
            client_id TEXT PRIMARY KEY,
            lobby_code TEXT
        )''')
        # Searching players
        cur.execute('''CREATE TABLE IF NOT EXISTS searching_players (
            client_id TEXT PRIMARY KEY,
            name TEXT
        )''')
        # Draw offer history
        cur.execute('''CREATE TABLE IF NOT EXISTS draw_offer_history (
            offerer_id TEXT,
            target_id TEXT,
            PRIMARY KEY (offerer_id, target_id)
        )''')
        # Game timeouts
        cur.execute('''CREATE TABLE IF NOT EXISTS game_timeouts (
            client_id TEXT PRIMARY KEY,
            timeout_time TEXT
        )''')
        # Client states
        cur.execute('''CREATE TABLE IF NOT EXISTS client_states (
            client_id TEXT PRIMARY KEY,
            state_json TEXT
        )''')
        self._db_conn.commit()

    # ===== CLIENT CONNECTION METHODS (Memory-based for websockets) =====
    def register_client(self, client_id: str, websocket: websockets.WebSocketServerProtocol):
        """Register a new client connection"""
        self.connected_clients[client_id] = websocket
        self.update_client_state(client_id, 'idle')

    def unregister_client(self, client_id: str, remove_from_lobby: bool = True):
        """Remove a client when they disconnect"""
        if client_id in self.connected_clients:
            del self.connected_clients[client_id]
        self.remove_searching_player(client_id)
        if remove_from_lobby:
            self.remove_player_from_lobby(client_id)
        self.remove_client_state(client_id)

    def get_client_websocket(self, client_id: str) -> Optional[websockets.WebSocketServerProtocol]:
        """Get the websocket for a connected client"""
        return self.connected_clients.get(client_id)

    def is_client_connected(self, client_id: str) -> bool:
        """Check if a client is connected"""
        return client_id in self.connected_clients

    async def broadcast_to_clients(self, message: dict, exclude_client_id: str = None):
        """Send a message to all connected clients except the excluded one"""
        for client_id, websocket in list(self.connected_clients.items()):
            if client_id != exclude_client_id:
                try:
                    await websocket.send(json.dumps(message))
                except websockets.exceptions.ConnectionClosed:
                    pass

    # ===== SEARCHING PLAYERS METHODS (DB-based) =====
    def add_searching_player(self, client_id: str, websocket: websockets.WebSocketServerProtocol, name: str):
        """Add a player to the searching list"""
        # Store websocket in memory
        self.connected_clients[client_id] = websocket
        # Store search data in DB
        cur = self._db_conn.cursor()
        cur.execute('REPLACE INTO searching_players (client_id, name) VALUES (?, ?)', (client_id, name))
        self._db_conn.commit()

    def remove_searching_player(self, client_id: str):
        """Remove a player from the searching list"""
        cur = self._db_conn.cursor()
        cur.execute('DELETE FROM searching_players WHERE client_id = ?', (client_id,))
        self._db_conn.commit()

    def get_searching_players(self) -> Dict[str, tuple]:
        """Get all searching players"""
        cur = self._db_conn.cursor()
        cur.execute('SELECT client_id, name FROM searching_players')
        rows = cur.fetchall()
        result = {}
        for row in rows:
            # Return (websocket, name) tuple - websocket from memory, name from DB
            websocket = self.connected_clients.get(row['client_id'])
            result[row['client_id']] = (websocket, row['name'])
        return result

    def clear_searching_players(self):
        """Clear all searching players"""
        cur = self._db_conn.cursor()
        cur.execute('DELETE FROM searching_players')
        self._db_conn.commit()

    # ===== GAME TIMEOUT METHODS (DB-based) =====
    def set_game_timeout(self, client_id: str, timeout_time: datetime.datetime):
        """Set game timeout for a client"""
        cur = self._db_conn.cursor()
        cur.execute('REPLACE INTO game_timeouts (client_id, timeout_time) VALUES (?, ?)', (client_id, timeout_time.isoformat()))
        self._db_conn.commit()

    def get_game_timeout(self, client_id: str) -> Optional[datetime.datetime]:
        """Get game timeout for a client"""
        cur = self._db_conn.cursor()
        cur.execute('SELECT timeout_time FROM game_timeouts WHERE client_id = ?', (client_id,))
        row = cur.fetchone()
        if row:
            return datetime.datetime.fromisoformat(row['timeout_time'])
        return None

    def remove_game_timeout(self, client_id: str):
        """Remove game timeout for a client"""
        cur = self._db_conn.cursor()
        cur.execute('DELETE FROM game_timeouts WHERE client_id = ?', (client_id,))
        self._db_conn.commit()

    # ===== CLIENT STATE METHODS (DB-based) =====
    def update_client_state(self, client_id: str, status: str, **kwargs):
        """Update a client's state"""
        state = {'status': status}
        state.update(kwargs)
        cur = self._db_conn.cursor()
        cur.execute('REPLACE INTO client_states (client_id, state_json) VALUES (?, ?)', (client_id, json.dumps(state)))
        self._db_conn.commit()

    def get_client_state(self, client_id: str) -> Optional[dict]:
        """Get the current state of a client"""
        cur = self._db_conn.cursor()
        cur.execute('SELECT state_json FROM client_states WHERE client_id = ?', (client_id,))
        row = cur.fetchone()
        if row:
            return json.loads(row['state_json'])
        return None

    def remove_client_state(self, client_id: str):
        """Remove client state"""
        cur = self._db_conn.cursor()
        cur.execute('DELETE FROM client_states WHERE client_id = ?', (client_id,))
        self._db_conn.commit()

    # ===== DRAW OFFER METHODS (DB-based) =====
    def add_draw_offer(self, offerer_id: str, target_id: str):
        """Add a draw offer"""
        cur = self._db_conn.cursor()
        cur.execute('REPLACE INTO draw_offer_history (offerer_id, target_id) VALUES (?, ?)', (offerer_id, target_id))
        self._db_conn.commit()

    def has_draw_offer(self, offerer_id: str, target_id: str) -> bool:
        """Check if a draw offer exists"""
        cur = self._db_conn.cursor()
        cur.execute('SELECT 1 FROM draw_offer_history WHERE offerer_id = ? AND target_id = ?', (offerer_id, target_id))
        return cur.fetchone() is not None

    def remove_draw_offer(self, offerer_id: str, target_id: str):
        """Remove a draw offer"""
        cur = self._db_conn.cursor()
        cur.execute('DELETE FROM draw_offer_history WHERE offerer_id = ? AND target_id = ?', (offerer_id, target_id))
        self._db_conn.commit()

    # ===== LOBBY MANAGEMENT METHODS (DB-based) =====
    def add_lobby(self, lobby_code: str, lobby: 'Lobby'):
        """Add a lobby to the global state and DB"""
        # Serialize game_state and settings
        game_state_json = json.dumps(lobby.game_state) if lobby.game_state else None
        settings_json = json.dumps(lobby.settings) if hasattr(lobby, 'settings') else None
        created_at_iso = lobby.created_at.isoformat() if hasattr(lobby, 'created_at') and lobby.created_at else datetime.datetime.now().isoformat()
        cur = self._db_conn.cursor()
        cur.execute('REPLACE INTO lobbies (lobby_code, owner_id, game_state, settings, created_at) VALUES (?, ?, ?, ?, ?)',
                    (lobby_code, lobby.owner_id, game_state_json, settings_json, created_at_iso))
        self._db_conn.commit()
        # Map the owner to this lobby in DB
        if lobby.owner_id:
            self.add_player_to_lobby(lobby.owner_id, lobby_code)

    def remove_lobby(self, lobby_code: str):
        """Remove a lobby and update client mappings in DB"""
        # Get lobby details from DB first
        cur = self._db_conn.cursor()
        cur.execute('SELECT * FROM lobbies WHERE lobby_code = ?', (lobby_code,))
        lobby_row = cur.fetchone()
        if lobby_row:
            # Remove all players from client_lobby_map (DB)
            cur.execute('DELETE FROM client_lobby_map WHERE lobby_code = ?', (lobby_code,))
            cur.execute('DELETE FROM lobbies WHERE lobby_code = ?', (lobby_code,))
            self._db_conn.commit()

    def get_lobby(self, lobby_code: str) -> Optional['Lobby']:
        """Get a lobby by its code from DB and reconstruct players from client mappings"""
        cur = self._db_conn.cursor()
        cur.execute('SELECT * FROM lobbies WHERE lobby_code = ?', (lobby_code,))
        row = cur.fetchone()
        if row:
            from server.core.models import Lobby, Player
            from server.core.enums import Color
            
            game_state = json.loads(row['game_state']) if row['game_state'] else None
            settings = json.loads(row['settings']) if row['settings'] else {}
            created_at = row['created_at'] if 'created_at' in row.keys() and row['created_at'] else None
            if created_at:
                created_at = datetime.datetime.fromisoformat(created_at)
            else:
                created_at = datetime.datetime.now()
            
            # Reconstruct players list from client_lobby_map and connected_clients
            players = []
            cur.execute('SELECT client_id FROM client_lobby_map WHERE lobby_code = ?', (lobby_code,))
            client_rows = cur.fetchall()
            
            for client_row in client_rows:
                client_id = client_row['client_id']
                websocket = self.connected_clients.get(client_id)
                if websocket:  # Only include connected clients
                    # Get client state to find player name
                    client_state = self.get_client_state(client_id)
                    player_name = client_state.get('player_name', 'Player') if client_state else 'Player'
                    
                    # Determine color based on position (first player is WHITE, second is BLACK)
                    player_color = Color.WHITE if len(players) == 0 else Color.BLACK
                    
                    player = Player(client_id, player_name, player_color, websocket)
                    players.append(player)
            
            lobby = Lobby(code=row['lobby_code'], owner_id=row['owner_id'], players=players, game_state=game_state, settings=settings, created_at=created_at)
            return lobby
        return None

    def get_lobby_by_client(self, client_id: str) -> Optional['Lobby']:
        """Get the lobby that a client is currently in from DB"""
        cur = self._db_conn.cursor()
        cur.execute('SELECT lobby_code FROM client_lobby_map WHERE client_id = ?', (client_id,))
        row = cur.fetchone()
        if row:
            lobby_code = row['lobby_code']
            return self.get_lobby(lobby_code)
        return None

    def add_player_to_lobby(self, client_id: str, lobby_code: str):
        """Map a client to a lobby in DB"""
        cur = self._db_conn.cursor()
        cur.execute('REPLACE INTO client_lobby_map (client_id, lobby_code) VALUES (?, ?)', (client_id, lobby_code))
        self._db_conn.commit()

    def remove_player_from_lobby(self, client_id: str):
        """Remove client's lobby mapping from DB"""
        cur = self._db_conn.cursor()
        cur.execute('DELETE FROM client_lobby_map WHERE client_id = ?', (client_id,))
        self._db_conn.commit()

    def get_all_lobbies(self) -> Dict[str, 'Lobby']:
        """Get all lobbies from DB and reconstruct players"""
        cur = self._db_conn.cursor()
        cur.execute('SELECT * FROM lobbies')
        rows = cur.fetchall()
        lobbies = {}
        from server.core.models import Lobby, Player
        from server.core.enums import Color
        
        for row in rows:
            game_state = json.loads(row['game_state']) if row['game_state'] else None
            settings = json.loads(row['settings']) if row['settings'] else {}
            created_at = row['created_at'] if 'created_at' in row.keys() and row['created_at'] else None
            if created_at:
                created_at = datetime.datetime.fromisoformat(created_at)
            else:
                created_at = datetime.datetime.now()
            
            lobby_code = row['lobby_code']
            
            # Reconstruct players list from client_lobby_map and connected_clients
            players = []
            cur2 = self._db_conn.cursor()
            cur2.execute('SELECT client_id FROM client_lobby_map WHERE lobby_code = ?', (lobby_code,))
            client_rows = cur2.fetchall()
            
            for client_row in client_rows:
                client_id = client_row['client_id']
                websocket = self.connected_clients.get(client_id)
                if websocket:  # Only include connected clients
                    # Get client state to find player name
                    client_state = self.get_client_state(client_id)
                    player_name = client_state.get('player_name', 'Player') if client_state else 'Player'
                    
                    # Determine color based on position (first player is WHITE, second is BLACK)
                    player_color = Color.WHITE if len(players) == 0 else Color.BLACK
                    
                    player = Player(client_id, player_name, player_color, websocket)
                    players.append(player)
            
            lobby = Lobby(code=lobby_code, owner_id=row['owner_id'], players=players, game_state=game_state, settings=settings, created_at=created_at)
            lobbies[lobby_code] = lobby
        return lobbies

    def lobby_exists(self, lobby_code: str) -> bool:
        """Check if a lobby exists in DB"""
        cur = self._db_conn.cursor()
        cur.execute('SELECT 1 FROM lobbies WHERE lobby_code = ?', (lobby_code,))
        return cur.fetchone() is not None

    def update_lobby_game_state(self, lobby_code: str, game_state: dict):
        """Update lobby game state in DB"""
        cur = self._db_conn.cursor()
        game_state_json = json.dumps(game_state) if game_state else None
        cur.execute('UPDATE lobbies SET game_state = ? WHERE lobby_code = ?', (game_state_json, lobby_code))
        self._db_conn.commit()

    def update_lobby_owner(self, lobby_code: str, new_owner_id: str):
        """Update lobby owner in DB"""
        cur = self._db_conn.cursor()
        cur.execute('UPDATE lobbies SET owner_id = ? WHERE lobby_code = ?', (new_owner_id, lobby_code))
        self._db_conn.commit()