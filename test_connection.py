#!/usr/bin/env python3
"""
Simple test script to verify the WebSocket server is working
"""
import asyncio
import websockets
import json

async def test_connection():
    try:
        async with websockets.connect("ws://localhost:8765") as websocket:
            print("✅ Successfully connected to server!")
            
            # Test creating a lobby
            create_message = {
                "type": "create_lobby",
                "player_name": "Test Player",
                "settings": {
                    "time_control": "10+0",
                    "ability_system": "enabled"
                }
            }
            
            await websocket.send(json.dumps(create_message))
            print("✅ Sent create lobby message")
            
            # Wait for response
            response = await asyncio.wait_for(websocket.recv(), timeout=5.0)
            data = json.loads(response)
            print(f"✅ Received response: {data}")
            
            if data.get("type") == "lobby_created":
                print(f"✅ Lobby created successfully! Code: {data.get('lobby_code')}")
            else:
                print(f"❌ Unexpected response: {data}")
                
    except asyncio.TimeoutError:
        print("❌ Connection timed out")
    except ConnectionRefusedError:
        print("❌ Could not connect to server. Make sure server.py is running.")
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    print("Testing WebSocket connection...")
    asyncio.run(test_connection())
