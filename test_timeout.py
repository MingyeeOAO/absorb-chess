#!/usr/bin/env python3
"""
Test script for automatic timeout detection
"""
import asyncio
import websockets
import json
import time

async def test_timeout():
    """Test automatic timeout detection"""
    
    # Connect two players
    async with websockets.connect("ws://localhost:8765") as player1, \
               websockets.connect("ws://localhost:8765") as player2:
        
        print("Connected two players")
        
        # Player 1 searches for game
        await player1.send(json.dumps({
            'type': 'search_game',
            'player_name': 'Player1'
        }))
        
        # Player 2 searches for game  
        await player2.send(json.dumps({
            'type': 'search_game',
            'player_name': 'Player2'
        }))
        
        # Wait for game to start
        print("Waiting for game to start...")
        
        # Read messages until game starts
        game_started = False
        while not game_started:
            try:
                msg1 = await asyncio.wait_for(player1.recv(), timeout=1.0)
                data1 = json.loads(msg1)
                if data1.get('type') == 'game_started':
                    print(f"Player 1 received game_started")
                    game_started = True
                    
                msg2 = await asyncio.wait_for(player2.recv(), timeout=1.0)
                data2 = json.loads(msg2)
                if data2.get('type') == 'game_started':
                    print(f"Player 2 received game_started")
                    
            except asyncio.TimeoutError:
                continue
        
        print("Game started! Now waiting for automatic timeout...")
        print("(The timer should trigger when white's time runs out)")
        
        # Listen for timeout message
        timeout_detected = False
        start_time = time.time()
        
        while not timeout_detected and (time.time() - start_time) < 30:  # Max 30 second test
            try:
                # Check both players for timeout message
                msg1 = await asyncio.wait_for(player1.recv(), timeout=0.5)
                data1 = json.loads(msg1)
                if data1.get('type') == 'game_over' and data1.get('reason') == 'timeout':
                    print(f"✅ Player 1 received timeout: {data1}")
                    timeout_detected = True
                    
            except asyncio.TimeoutError:
                pass
                
            try:
                msg2 = await asyncio.wait_for(player2.recv(), timeout=0.5)
                data2 = json.loads(msg2)
                if data2.get('type') == 'game_over' and data2.get('reason') == 'timeout':
                    print(f"✅ Player 2 received timeout: {data2}")
                    timeout_detected = True
                    
            except asyncio.TimeoutError:
                pass
        
        if timeout_detected:
            print("✅ Automatic timeout detection working correctly!")
        else:
            print("❌ No timeout detected within test period")

if __name__ == "__main__":
    print("Starting timeout test...")
    print("Make sure the server is running first!")
    asyncio.run(test_timeout())