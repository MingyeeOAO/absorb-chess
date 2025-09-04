# Chess with Piece Abilities

A web-based multiplayer chess game where pieces gain abilities by capturing other pieces. When a piece captures another piece, it inherits the movement abilities of the captured piece while retaining its own abilities.

## Features

- **Multiplayer Support**: Real-time 2-player gameplay via WebSocket
- **Lobby System**: Create and join games with unique lobby codes
- **Piece Ability System**: Pieces gain abilities when capturing others
- **Pawn Promotion**: Promoted pawns retain their existing abilities
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

### WebSocket Messages
- `create_lobby`: Create new game lobby
- `join_lobby`: Join existing lobby
- `start_game`: Start the game (owner only)
- `move_piece`: Make a chess move
- `lobby_update`: Broadcast lobby state changes
- `game_state`: Broadcast game state updates

### Piece Ability System
- Each piece has an `abilities` array containing movement types
- On capture, abilities are merged (no duplicates)
- Movement validation considers all abilities
- Visual indicators show piece abilities

## Development

### Adding New Features
1. Update server logic in `server.py`
2. Add frontend handling in `app.js`
3. Update UI in `index.html` and `styles.css`
4. Test with multiple browser windows

### Testing
- Open two browser windows to test multiplayer
- Test lobby creation and joining
- Verify ability inheritance works correctly
- Test pawn promotion with abilities

## Future Enhancements

- AI opponent option
- Spectator mode
- Game replay system
- Advanced time controls
- Custom piece sets
- Tournament mode

## License

This project is open source and available under the MIT License.
