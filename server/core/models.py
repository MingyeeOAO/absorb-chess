from dataclasses import dataclass
from typing import List, Optional, Dict
import datetime
import websockets
from .enums import Color

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