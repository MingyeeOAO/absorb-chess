# Absorb Chess

A web-based multiplayer chess game where pieces gain abilities by capturing other pieces. When a piece captures another piece, it inherits the movement abilities of the captured piece while retaining its own abilities.

## Features

- **Multiplayer Support**: Real-time 2-player gameplay via WebSocket
- **Lobby System**: Create and join games with unique lobby codes
- **Piece Ability System**: Pieces gain abilities when capturing others
- **Search function**: Find real player opponent online
- **Modern UI**: Responsive design with real-time updates

## How to Play

1. **Create or Join a Lobby**: 
   - Create a new lobby with custom settings
   - Join an existing lobby using a 6-character code

2. **Game Rules**:
   - Standard chess rules apply
   - When a piece captures another, it gains the captured piece's movement abilities
   - Pawns can still promote, and promoted pieces retain all their abilities
   - No duplicate abilities are gained (capturing same type or already having the ability)

3. **Special Mechanics**:
   - A pawn that captures a queen can move like both a pawn and a queen
   - A promoted knight with queen abilities can move like both a knight and a queen

## Installation & Setup

### Prerequisites
- Python 3.7+
- Modern web browser


## Project Structure

```
absort chess/
├── app.js
├── engine.py
├── index.html
├── main.py
├── README.md
├── requirements.txt
├── resources/
│   └── pieces/
│       └── classic/
├── server/
│   ├── checkers/
│   │   └── timer_manager.py
│   ├── core/
│   │   ├── enums.py
│   │   ├── game.py
│   │   ├── models.py
│   │   ├── piece.py
│   │   └── state.py
│   ├── handlers/
│   │   ├── game_handler.py
│   │   ├── lobby_handler.py
│   │   └── search_handler.py
│   ├── networking/
│   │   └── connection.py
│   └── server.py
├── split_sprites.py
├── styles.css
├── test_names.py
├── test_timeout.py
└── oldversion/
    ├── app.js
    ├── server.py
    └── styles.css
```

### Backend Setup
1. Install Python dependencies:
   ```bash
   pip install -r requirements.txt
   ```

2. Start the WebSocket server:
   ```bash
   python server.py
   ```
   The server will run on `ws://localhost:8765`


### Frontend Setup
1. Open `index.html` in a web browser
2. The game will automatically connect to the server
3. The frontend will automatically try servers listed in servers (line 103 of app.js). If localhost is unavailable, it will fall back to chess.harc.qzz.io (production server). Update the list if your backend runs on a different port.

## Project Structure

```
├── server.py          # Python WebSocket server
├── index.html         # Main HTML file
├── styles.css         # CSS styling
├── app.js            # Frontend JavaScript
├── requirements.txt   # Python dependencies
└── README.md         # This file
```

## Game Flow

1. **Main Menu**: Choose to create or join a lobby
2. **Lobby Creation**: Set game settings (time control, ability system)
3. **Lobby Waiting**: Wait for second player to join
4. **Game Start**: Owner starts the game when ready
5. **Gameplay**: Real-time chess with ability inheritance
6. **Game End**: Winner determined by checkmate

## Technical Details

### Piece Ability System
- Each piece has an `abilities` array containing movement types
- On capture, abilities are merged (no duplicates)
- Movement validation considers all abilities
- Visual indicators show piece abilities


## License

This project is open source and available under the MIT License.
