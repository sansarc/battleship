# Battleship 
```text
                        ______       _   _   _           _     _        
                        | ___ \     | | | | | |         | |   (_)       
                        | |_/ / __ _| |_| |_| | ___  ___| |__  _ _ __   
                        | ___ \/ _` | __| __| |/ _ \/ __| '_ \| | '_ \  
                        | |_/ / (_| | |_| |_| |  __/\__ \ | | | | |_) | 
                        \____/ \__,_|\__|\__|_|\___||___/_| |_|_| .__/  
                                                                | |     
                                                                |_|     
                                                               
                                                       |
                                                 # #  ( )
                                              ___#_#___|__
                                          _  |____________|  _
                                   _=====| | |            | | |==== _
                             =====| |.---------------------------. | |====
            <--------------------'   .  .  .  .  .  .  .  .   '--------------/
              \                                                             /
               \___________________________________________________________/
              wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww
            wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww
              wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww
```

 A networked Battleship implementation featuring a Python-based TUI and a C server to handle game logic and concurrency.

## Architecture
This project is strictly divided into a multithread backend and an asynchronous frontend.
* **Server**: Written in C, utilizing `pthreads` and sockets to manage up to 10 concurrent clients
* **Client**: Written in Python, relaying on the `textual` library to render the terminal interface.
* **Protocol**: A custom text-based TCP protocol handling match creation, ship placement, turn-based combat and rematches

## Prerequisites
* **Docker and Docker Compose**: Necessary for spinning up the containerized server environment.
* **Python 3.x**: Required to execute the client.
* **Textual**: The client UI dependency, specified in requirements.txt

## Project Structure
| Directory / File     | Description                                                                                  |
|:---------------------|:---------------------------------------------------------------------------------------------|
| `server/`            | Contains the C source code, Makefile, and Dockerfile for the game server. **ZIP**            |
| `client/`            | Contains the Python client scripts (`client.py`, `game_screen.py`) and dependencies. **ZIP** |
| `docs/`              | Contains the Italian technical documentation (`documentazione_battleship_LSO.pdf`). **ZIP**  |
| `docker-compose.yml` | Configuration to build and run the server service, mapping port 8080. **ZIP**                |

## Usage Instructions
### 1. Start the server
The server listens on port 8080 by default. Boot it using Docker Compose from the root directory. 
```shell
docker-compose up  
```
Alternatively, to build it natively, navigate to the `server/` directory and use the Makefile:
```shell
cd server
make run
```

### 2. Start the client
Navigate to the `client/` directory and set up a Python virtual environment to isolate the project dependencies. 

**macOS and Linux**:
```shell
cd client
python3 -m venv venv
source venv/bin/activate
```
**Windows**:
```shell
cd client
python -m venv venv
venv\Scripts\activate
```
Once the virtual environment is activated, install the dependencies and run the client.
```shell
pip install -r requirements.txt
python client.py
```

### 3. Gameplay mechanics
* Players connect via a lobby interface to create, list and join matches using integer Match IDs.
* During the placement phase, each player positions 3 ships of varying lengths on 10x10 grid, toggling between horizontal ('H') and vertical ('V') orientations.
* Combat operates on a turn-based system, resolving hits and misses until one player's reaches the win condition, meaning all their ships are sunk.

