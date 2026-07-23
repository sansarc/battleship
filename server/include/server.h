#ifndef SERVER_H
#define SERVER_H

#include <pthread.h>
#include <netinet/in.h>

#define MAX_CLIENTS 100
#define MAX_GAMES 50
#define GRID_SIZE 10
#define SHIPS_PER_PLAYER 3

typedef enum {
    GAME_WAITING,
    GAME_PLACEMENT,
    GAME_COMBAT,
    GAME_FINISHED
} GameState;

typedef struct {
    int socket_fd;
    int is_connected;
    int user_id;
} Client;

typedef struct {
    int id;
    Client* creator;
    Client* opponent;
    GameState state;
    int creator_grid[GRID_SIZE][GRID_SIZE];
    int opponent_grid[GRID_SIZE][GRID_SIZE];
    int creator_ships_placed;
    int opponent_ships_placed;
    int creator_ready;
    int opponent_ready;
    int creator_rematch;
    int opponent_rematch;
    int turn;
    pthread_mutex_t lock;
} Match;

typedef struct {
    Match games[MAX_GAMES];
    int active_games_count;
    pthread_mutex_t global_lock;
} GameManager;

extern GameManager game_manager;

#endif