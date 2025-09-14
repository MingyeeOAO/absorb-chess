import asyncio
from server.server import GameServer

if __name__ == "__main__":
    server = GameServer()
    asyncio.run(server.start())
