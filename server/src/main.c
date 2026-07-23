#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include "../include/server.h"
#include "../include/game.h"

#define PORT 8080   

GameManager game_manager;

static void init_game_manager() {
    game_manager.active_games_count = 0;
    pthread_mutex_init(&game_manager.global_lock, NULL);

    for (int i = 0; i < MAX_GAMES; i++) {
        game_manager.games[i].id = -1; // -1 indica uno slot vuoto
        pthread_mutex_init(&game_manager.games[i].lock, NULL);
    }
}

static void *client_handler(void *arg) {
    Client *client = arg;
    char buffer[1024];

    printf("New client connected: %d\n", client->socket_fd);

    char welcome_msg[64];
    snprintf(welcome_msg, sizeof(welcome_msg), "HELLO %d\n", client->socket_fd);
    write(client->socket_fd, welcome_msg, strlen(welcome_msg));

    while(1) {
        memset(buffer, 0, sizeof(buffer));
        const int bytes_read = (int) read(client->socket_fd, buffer, sizeof(buffer) - 1);

        // client disconnesso o errore di rete
        if(bytes_read <= 0) {
            printf("Client disconnected: %d\n", client->socket_fd);
            break; //
        }

        printf("Received by socket %d: %s\n", client->socket_fd, buffer);
        handle_client_message(client, buffer);
    }

    // onde evitare memory leak e giocatori "fantasma"
    cleanup_client_matches(client);
    close(client->socket_fd);
    client->is_connected = 0;
    free(client);
    pthread_exit(NULL);
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    const int opt = 1;

    // ignora il segnale SIGPIPE
    // se il server cerca di fare una write su un socket chiuso dal client, l'OS invia SIGPIPE che di default killa il processo
    // ignorandolo, la write restituirà semplicemente un errore (-1).
    signal(SIGPIPE, SIG_IGN);

    init_game_manager();

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // SO_REUSEADDR permette di riavviare il server e riutilizzare istantaneamente la porta
    // altrimenti se il server crasha per qualche minuto la porta verrà considerata ancora utilizzata
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt error");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("binding failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", PORT);

    while (1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed");
            continue;
        }

        // ogni client viene allocato dinamicamente (heap)
        // se si usasse una variabile locale, verrebbe sovrascritta alla successiva iterazione del while
        Client *new_client = malloc(sizeof(Client));
        new_client->socket_fd = new_socket;
        new_client->is_connected = 1;

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, client_handler, new_client) > 0) {
            free(new_client);
            close(new_socket);
        }

        // si liberano in automatico le risorse del thread, non appena client_handler termina
        pthread_detach(thread_id);
    }

    return 0;
}