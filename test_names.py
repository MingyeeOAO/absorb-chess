#!/usr/bin/env python3
"""
Test script for name display and cancel button fixes
"""
import asyncio
import websockets
import json

async def test_search_and_names():
    """Test search functionality and player name display"""
    
    print("Testing search functionality and player name display...")
    
    # Connect two players
    async with websockets.connect("ws://localhost:8765") as player1, \
               websockets.connect("ws://localhost:8765") as player2:
        
        print("✅ Connected two players")
        
        # Player 1 searches for game
        await player1.send(json.dumps({
            'type': 'search_game',
            'player_name': 'Alice'
        }))
        print("✅ Player 1 (Alice) sent search request")
        
        # Player 2 searches for game  
        await player2.send(json.dumps({
            'type': 'search_game',
            'player_name': 'Bob'
        }))
        print("✅ Player 2 (Bob) sent search request")
        
        # Wait for messages
        print("Waiting for search results and game start...")
        
        messages_received = 0
        game_started_count = 0
        lobby_data_received = 0
        
        while messages_received < 10 and game_started_count < 2:  # Expect game_started for both players
            try:
                # Check player 1 messages
                try:
                    msg1 = await asyncio.wait_for(player1.recv(), timeout=0.5)
                    data1 = json.loads(msg1)
                    print(f"Player 1 received: {data1['type']}")
                    
                    if data1.get('type') == 'game_started':
                        game_started_count += 1
                        if 'lobby_data' in data1 and 'players' in data1['lobby_data']:
                            lobby_data_received += 1
                            players = data1['lobby_data']['players']
                            print(f"✅ Player 1 lobby_data contains players: {[p['name'] for p in players]}")
                        else:
                            print("❌ Player 1 game_started missing lobby_data or players")
                            
                    messages_received += 1
                except asyncio.TimeoutError:
                    pass
                
                # Check player 2 messages
                try:
                    msg2 = await asyncio.wait_for(player2.recv(), timeout=0.5)
                    data2 = json.loads(msg2)
                    print(f"Player 2 received: {data2['type']}")
                    
                    if data2.get('type') == 'game_started':
                        game_started_count += 1
                        if 'lobby_data' in data2 and 'players' in data2['lobby_data']:
                            lobby_data_received += 1
                            players = data2['lobby_data']['players']
                            print(f"✅ Player 2 lobby_data contains players: {[p['name'] for p in players]}")
                        else:
                            print("❌ Player 2 game_started missing lobby_data or players")
                            
                    messages_received += 1
                except asyncio.TimeoutError:
                    pass
                    
            except Exception as e:
                print(f"Error: {e}")
                break
        
        print(f"\n--- Test Results ---")
        print(f"Game started messages received: {game_started_count}/2")
        print(f"Lobby data with players received: {lobby_data_received}/2")
        
        if game_started_count == 2 and lobby_data_received == 2:
            print("✅ SUCCESS: Both players received game_started with proper lobby_data!")
        else:
            print("❌ FAILED: Missing game_started messages or lobby_data")

if __name__ == "__main__":
    print("Starting name display test...")
    print("Make sure the server is running first!")
    asyncio.run(test_search_and_names())