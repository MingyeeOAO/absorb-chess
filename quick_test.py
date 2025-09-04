import asyncio
import websockets
import json

async def quick_test():
    try:
        async with websockets.connect("ws://localhost:8765") as websocket:
            print("✅ Connected successfully!")
            
            # Send a simple message
            message = {
                "type": "create_lobby",
                "player_name": "Test",
                "settings": {}
            }
            
            await websocket.send(json.dumps(message))
            print("✅ Message sent")
            
            # Wait for response
            response = await asyncio.wait_for(websocket.recv(), timeout=3.0)
            data = json.loads(response)
            print(f"✅ Response: {data}")
            
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    asyncio.run(quick_test())
