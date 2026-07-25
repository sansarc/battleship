#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../include/game.h"

void create_match(Client *client) {
    // blocco l'intero manager perché l'operazione modifica l'array globale delle partite
    pthread_mutex_lock(&game_manager.global_lock);

    for (int i = 0; i < MAX_GAMES; i++) {
        if (game_manager.games[i].id != -1 &&
           (game_manager.games[i].creator == client || game_manager.games[i].opponent == client)) {

            pthread_mutex_unlock(&game_manager.global_lock);
            write(client->socket_fd, "ERR ALREADY_IN_MATCH\n", 21);
            return;
           }
    }

    int match_id = -1;

    // ricerca lineare del primo slot disponibile nel pool pre-allocato (object pooling)
    for (int i = 0; i < MAX_GAMES; i++) {
        if (game_manager.games[i].id == -1) {
            match_id = i;
            game_manager.games[i].id = match_id;
            game_manager.games[i].creator = client;
            game_manager.games[i].opponent = NULL;
            game_manager.games[i].state = GAME_WAITING;

            game_manager.games[i].creator_ships_placed
            = game_manager.games[i].opponent_ships_placed
            = game_manager.games[i].creator_ready
            = game_manager.games[i].opponent_ready
            = game_manager.games[i].turn = 0;

            // inizializzazione griglie
            memset(game_manager.games[i].creator_grid, 0, sizeof(game_manager.games[i].creator_grid));
            memset(game_manager.games[i].opponent_grid, 0, sizeof(game_manager.games[i].opponent_grid));

            game_manager.active_games_count++;
            break;
        }
    }

    pthread_mutex_unlock(&game_manager.global_lock);

    char response[258];
    if (match_id != -1)
        snprintf(response, sizeof(response), "OK MATCH_CREATED %d\n", match_id);
    else
        snprintf(response, sizeof(response), "ERR MAX_GAMES_REACHED\n");

    write(client->socket_fd, response, strlen(response));
}

void list_matches(const Client *client) {
    char response[1024] = "MATCH_LIST\n";
    char line[64];
    int found = 0;

    pthread_mutex_lock(&game_manager.global_lock);
    for (int i = 0; i < MAX_GAMES; i++) {
        // filtra partite attive (id != -1) e che siano in attesa di un avversario
        if (game_manager.games[i].id != -1 && game_manager.games[i].state == GAME_WAITING) {
            snprintf(line, sizeof(line), "ID: %d - Waiting for players\n", game_manager.games[i].id);
            strcat(response, line);
            found = 1;
        }
    }
    pthread_mutex_unlock(&game_manager.global_lock);

    if (!found)
        strcat(response, "No matches available.\n");

    write(client->socket_fd, response, strlen(response));
}

void request_join_match(Client *client, const int match_id) {
    if (match_id < 0 || match_id >= MAX_GAMES) {
        write(client->socket_fd, "ERR INVALID_MATCH_ID\n", 21);
        return;
    }

    pthread_mutex_lock(&game_manager.global_lock);

    for (int i = 0; i < MAX_GAMES; i++) {
        Match *match = &game_manager.games[i];

        if (match->id != -1 && match->state == GAME_WAITING && match->creator == client) {
            pthread_mutex_lock(&match->lock);
            match->id = -1;
            match->creator = match->opponent = NULL;
            game_manager.active_games_count--;
            pthread_mutex_unlock(&match->lock);
        }
    }

    // si prende il lock globale per recuperare il singolo match, che viene bloccato a sua volta, per poi rilasciare il lock globale
    Match *match = &game_manager.games[match_id];
    pthread_mutex_lock(&match->lock);
    pthread_mutex_unlock(&game_manager.global_lock);

    // security check
    if (match->id == match_id && match->creator == client) {
        write(client->socket_fd, "ERR CANNOT_JOIN_SELF\n", 21);
        pthread_mutex_unlock(&match->lock);
        return;
    }

    char response[128];
    // si verifica se la partita può effettivamente accettare un avversario
    if (match->id == match_id && match->state == GAME_WAITING && match->opponent == NULL) {
        match->opponent = client; // "prenotazione" provvisoria, in attesa di conferma

        char notify_creator[128];
        snprintf(notify_creator, sizeof(notify_creator), "JOIN MATCH REQUEST FROM %d (match_id=%d)\n", client->socket_fd, match->id);
        write(match->creator->socket_fd, notify_creator, strlen(notify_creator));

        snprintf(response, sizeof(response), "OK JOIN_PENDING (match_id=%d)\n", match->id);
    }
    else
        snprintf(response, sizeof(response), "ERR MATCH_UNAVAILABLE\n");

    write(client->socket_fd, response, strlen(response));
    pthread_mutex_unlock(&match->lock);
}

void handle_join_response(const Client *client, const int match_id, const int accepted) {
    if (match_id < 0 || match_id >= MAX_GAMES) return;

    pthread_mutex_lock(&game_manager.global_lock);
    Match *match = &game_manager.games[match_id];
    pthread_mutex_lock(&match->lock);
    pthread_mutex_unlock(&game_manager.global_lock);

    char response[128];
    // verifica che l'utente che sta rispondendo sia effettivamente il creatore della partita
    if (match->id == match_id && match->creator == client && match->state == GAME_WAITING && match->opponent != NULL) {
        if (accepted) {
            match->state = GAME_PLACEMENT;
            snprintf(response, sizeof(response), "OK GAME_START (match_id=%d)\n", match->id);
            write(match->creator->socket_fd, response, strlen(response));
            snprintf(response, sizeof(response), "OK GAME_START (match_id=%d)\n", match->id);
            write(match->opponent->socket_fd, response, strlen(response));
        } else {
            snprintf(response, sizeof(response), "ERR REJECTED (match_id=%d)\n", match->id);
            write(match->opponent->socket_fd, response, strlen(response));

            match->opponent = NULL; // reset avversario per permettere nuove richieste ad altri utenti

            snprintf(response, sizeof(response), "OK REJECTED ACK (match_id=%d)\n", match->id);
            write(match->creator->socket_fd, response, strlen(response));
        }
    }

    pthread_mutex_unlock(&match->lock);
}

void place_ship(const Client *client, const int match_id, const int x, const int y, const char orientation) {
    if (match_id < 0 || match_id >= MAX_GAMES) {
        char response[128];
        snprintf(response, sizeof(response), "ERR INVALID_MATCH_ID %d\n", match_id);
        write(client->socket_fd, response, strlen(response));;
        return;
    }

    pthread_mutex_lock(&game_manager.global_lock);
    Match *match = &game_manager.games[match_id];
    pthread_mutex_lock(&match->lock);
    pthread_mutex_unlock(&game_manager.global_lock);

    if (match->id != match_id || match->state != GAME_PLACEMENT) {
        write(client->socket_fd, "ERR INVALID STATE\n", 18);
        pthread_mutex_unlock(&match->lock);
        return;
    }

    // pointer per evitare di duplicare il codice di posizionamento per creatore e avversario
    // punteranno ai dati corretti in base al turno
    int (*grid)[GRID_SIZE];
    int *ships_placed;
    int *player_ready;

    // identifica il turno per operare sulle giuste variabili
    if (client == match->creator) {
        grid = match->creator_grid;
        ships_placed = &match->creator_ships_placed;
        player_ready = &match->creator_ready;
    }
    else if (client == match->opponent) {
        grid = match->opponent_grid;
        ships_placed = &match->opponent_ships_placed;
        player_ready = &match->opponent_ready;
    }
    else {
        write(client->socket_fd, "ERR NOT_IN_MATCH\n", 18);
        pthread_mutex_unlock(&match->lock);
        return;
    }

    if (*player_ready) {
        write(client->socket_fd, "ERR ALREADY_READY\n", 18);
        pthread_mutex_unlock(&match->lock);
        return;
    }

    const int length = SHIP_LENGTHS[* ships_placed];

    if (x < 0 || x >= GRID_SIZE || y < 0 || y >= GRID_SIZE) {
        write(client->socket_fd, "ERR OUT_OF_BOUNDS\n", 18);
        pthread_mutex_unlock(&match->lock);
        return;
    }

    if (orientation == 'H' || orientation == 'h') {
        if (y + length > GRID_SIZE) {
            write(client->socket_fd, "ERR OUT_OF_BOUNDS\n", 18);
            pthread_mutex_unlock(&match->lock);
            return;
        }

        for (int i = 0; i < length; i++) {
            if (grid[x][y + i] != 0) {
                write(client->socket_fd, "ERR OVERLAP\n", 12);
                pthread_mutex_unlock(&match->lock);
                return;
            }
        }

        for (int i = 0; i < length; i++)
            grid[x][y + i] = 1;
    }
    else if (orientation == 'V' || orientation == 'v') {
        if (x + length > GRID_SIZE) {
            write(client->socket_fd, "ERR OUT_OF_BOUNDS\n", 18);
            pthread_mutex_unlock(&match->lock);
            return;
        }

        for (int i = 0; i < length; i++) {
            if (grid[x + i][y] != 0) {
                write(client->socket_fd, "ERR OVERLAP\n", 12);
                pthread_mutex_unlock(&match->lock);
                return;
            }
        }

        for (int i = 0; i < length; i++)
            grid[x + i][y] = 1;
    }
    else {
        write(client->socket_fd, "ERR INVALID_ORIENTATION\n", 24);
        pthread_mutex_unlock(&match->lock);
        return;
    }

    (*ships_placed)++;
    char placement_ok[64];
    snprintf(placement_ok, sizeof(placement_ok), "OK SHIP PLACED %d/%d\n", *ships_placed, SHIPS_PER_PLAYER);
    write(client->socket_fd, placement_ok, strlen(placement_ok));

    if (*ships_placed >= SHIPS_PER_PLAYER) {
        *player_ready = 1;
        write(client->socket_fd, "OK WAITING_OPPONENT\n", 20);
    }

    if (match->creator_ready && match->opponent_ready) {
        match->state = GAME_COMBAT;
        // il creatore ha sempre il primo turno di default
        write(match->creator->socket_fd, "OK COMBAT_START YOUR_TURN\n", 26);
        write(match->opponent->socket_fd, "OK COMBAT_START OPPONENT_TURN\n", 30);
    }

    pthread_mutex_unlock(&match->lock);
}

static int check_win_condition(int grid[GRID_SIZE][GRID_SIZE]) {
    for (int i = 0; i < GRID_SIZE; i++) {
        for (int j = 0; j < GRID_SIZE; j++) {
            if (grid[i][j] == 1) return 0; // c'è ancora una cella di una nave integra
        }
    }

    return 1;
}

void handle_shoot(const Client *client, const int match_id, const int x, const int y) {
    if (match_id < 0 || match_id >= MAX_GAMES) return;

    pthread_mutex_lock(&game_manager.global_lock);
    Match *match = &game_manager.games[match_id];
    pthread_mutex_lock(&match->lock);
    pthread_mutex_unlock(&game_manager.global_lock);

    if (match->id != match_id || match->state != GAME_COMBAT) {
        write(client->socket_fd, "ERR INVALID_STATE\n", 18);
        pthread_mutex_unlock(&match->lock);
        return;
    }

    if (x < 0 || x >= GRID_SIZE || y < 0 || y >= GRID_SIZE) {
        write(client->socket_fd, "ERR OUT_OF_BOUNDS\n", 18);
        pthread_mutex_unlock(&match->lock);
        return;
    }

    const int is_creator = (client == match->creator);
    const int is_opponent = (client == match->opponent);

    // previene attacchi da parte di utenti esterni
    if (!is_creator && !is_opponent) {
        write(client->socket_fd, "ERR NOT_YOUR_MATCH\n", 19);
        pthread_mutex_unlock(&match->lock);
        return;
    }

    // 0 = turno del creatore, 1 = turno dell'avversario
    if ((is_creator && match->turn != 0) || (is_opponent && match->turn != 1)) {
        write(client->socket_fd, "ERR NOT_YOUR_TURN\n", 18);
        pthread_mutex_unlock(&match->lock);
        return;
    }

    // se sono il creatore, sparo sulla griglia dell'avversario, altrimenti viceversa
    int (*target_grid)[GRID_SIZE] = is_creator ? match->opponent_grid : match->creator_grid;
    const Client *defender = is_creator ? match->opponent : match->creator;

    // 0 = acqua, 1 = nave, 2 = nave colpita, 3 = mancato
    const int cell_value = target_grid[x][y];
    char attacker_msg[64];
    char defender_msg[64];

    if (cell_value == 2 || cell_value == 3) {
        write(client->socket_fd, "ERR ALREADY_SHOT\n", 17);
        pthread_mutex_unlock(&match->lock);
        return;
    }

    int hit = 0;
    if (cell_value == 1) {
        target_grid[x][y] = 2; // colpito
        hit = 1;
        snprintf(attacker_msg, sizeof(attacker_msg), "OK HIT %d %d\n", x, y);
        snprintf(defender_msg, sizeof(defender_msg), "OPPONENT_HIT %d %d\n", x, y);
    }
    else {
        target_grid[x][y] = 3; // mancato
        snprintf(attacker_msg, sizeof(attacker_msg), "OK MISS %d %d\n", x, y);
        snprintf(defender_msg, sizeof(defender_msg), "OPPONENT_MISS %d %d\n", x, y);
    }

    write(client->socket_fd, attacker_msg, strlen(attacker_msg));
    write(defender->socket_fd, defender_msg, strlen(defender_msg));

    // il check di vittoria si fa ad ogni colpo a segno (inutile farlo sui mancati)
    if (hit && check_win_condition(target_grid)) {
        match->state = GAME_FINISHED;

        // potenziale richiesta di rematch, per il momento settata a false
        match->creator_rematch = 0;
        match->opponent_rematch = 0;

        write(client->socket_fd, "GAME_OVER WIN\n", 14);
        write(defender->socket_fd, "GAME_OVER LOSE\n", 15);
        pthread_mutex_unlock(&match->lock);
        return;
    }

    // inverti il turno, nessuno ha vinto
    match->turn = is_creator ? 1 : 0;
    write(client->socket_fd, "TURN_END\n", 9);
    write(defender->socket_fd, "YOUR_TURN\n", 10);
    pthread_mutex_unlock(&match->lock);
}

void handle_rematch(const Client *client, const int match_id) {
    if (match_id < 0 || match_id >= MAX_GAMES) return;

    pthread_mutex_lock(&game_manager.global_lock);
    Match *match = &game_manager.games[match_id];
    pthread_mutex_lock(&match->lock);
    pthread_mutex_unlock(&game_manager.global_lock);

    if (match->id == match_id && match->state == GAME_FINISHED) {
        if (client == match->creator)
            match->creator_rematch = 1;
        if (client == match->opponent)
            match->opponent_rematch = 1;

        if (match->creator_rematch && match->opponent_rematch) {
            // hard reset di tutto lo stato della partita per rimettere i player in GAME_PLACEMENT
            match->creator_rematch
            = match->opponent_rematch
            = match->creator_ships_placed
            = match->opponent_ships_placed
            = match->creator_ready
            = match->opponent_ready
            = match->turn = 0;

            // pulizia griglie
            memset(match->creator_grid, 0, sizeof(match->creator_grid));
            memset(match->opponent_grid, 0, sizeof(match->opponent_grid));
            match->state = GAME_PLACEMENT;

            write(match->creator->socket_fd, "OK GAME_START_PLACEMENT\n", 24);
            write(match->opponent->socket_fd, "OK GAME_START_PLACEMENT\n", 24);
        }
        else {
            write(client->socket_fd, "OK REMATCH_PENDING\n", 19);

            const Client *other = (client == match->creator) ? match->opponent : match->creator;
            if (other) write(other->socket_fd, "REMATCH_REQUEST\n", 16);
        }
    }

    pthread_mutex_unlock(&match->lock);
}

void handle_leave(const Client *client, const int match_id) {
    if (match_id < 0 || match_id >= MAX_GAMES) return;

    pthread_mutex_lock(&game_manager.global_lock);
    Match *match = &game_manager.games[match_id];
    pthread_mutex_lock(&match->lock);
    pthread_mutex_unlock(&game_manager.global_lock);

    if (match->id == match_id) {
        const Client *other = (client == match->creator) ? match->opponent : match->creator;
        if (other != NULL)
            write(other->socket_fd, "ERR OPPONENT_LEFT\n", 18);

        // reset del match
        match->id = -1;
        match->creator = match->opponent = NULL;
        match->state = GAME_WAITING;
        game_manager.active_games_count--;

        write(client->socket_fd, "OK LEFT_ACK\n", 12);
    }

    pthread_mutex_unlock(&match->lock);
}

void cleanup_client_matches(const Client *client) {
    pthread_mutex_lock(&game_manager.global_lock);

    // caso di disconnessioni anomale, quindi il client non ha chiamato handle_leave
    for (int i = 0; i < MAX_GAMES; i++) {
        Match *match = &game_manager.games[i];

        if (match->id != -1) {
            pthread_mutex_lock(&match->lock);

            if (match->creator == client || match->opponent == client) {
                const Client *survivor = (match->creator == client) ? match->opponent : match->creator;
                if (survivor != NULL && survivor->is_connected)
                    write(survivor->socket_fd, "ERR OPPONENT_DISCONNECTED\n", 26);

                match->id = -1;
                match->creator = match->opponent = NULL;
                match->state = GAME_WAITING;
            }

            pthread_mutex_unlock(&match->lock);
        }
    }

    pthread_mutex_unlock(&game_manager.global_lock);
}

void handle_client_message(Client *client, char *message) {
    char command[32];
    int args[5] = {-1, -1, -1, -1, -1};
    char orientation = '\0';

    message[strcspn(message, "\r\n")] = 0;
    const int parsed = sscanf(message, "%s %d", command, &args[0]);

    if (parsed >= 1) {
        if (strcmp(command, "CREATE") == 0) {
            create_match(client);
            return;
        }
        if (strcmp(command, "LIST") == 0) {
            list_matches(client);
            return;
        }
        if (strcmp(command, "JOIN") == 0 && args[0] != -1) {
            request_join_match(client, args[0]);
            return;
        }
        if (strcmp(command, "ACCEPT") == 0 && args[0] != -1) {
            handle_join_response(client, args[0], 1);
            return;
        }
        if (strcmp(command, "REJECT") == 0 && args[0] != -1) {
            handle_join_response(client, args[0], 0);
            return;
        }
        if (strcmp(command, "REMATCH") == 0 && args[0] != -1) {
            handle_rematch(client, args[0]);
            return;
        }
        if (strcmp(command, "LEAVE") == 0 && args[0] != -1) {
            handle_leave(client, args[0]);
            return;
        }

    }

    if (strcmp(command, "PLACE") == 0) {
        const int parsed_place = sscanf(message, "PLACE %d %d %d %c", &args[0], &args[1], &args[2], &orientation);
        if (parsed_place == 4) {
            place_ship(client, args[0], args[1], args[2], orientation);
            return;
        }

        write(client->socket_fd, "ERR INVALID_PLACE_SYNTAX\n", 25);
        return;
    }

    if (strcmp(command, "SHOOT") == 0) {
        const int parsed_shoot = sscanf(message , "SHOOT %d %d %d", &args[0], &args[1], &args[2]);
        if (parsed_shoot == 3) {
            handle_shoot(client, args[0], args[1], args[2]);
            return;
        }

        write(client->socket_fd, "ERR INVALID_SHOOT_SYNTAX\n", 25);
        return;
    }

    write(client->socket_fd, "ERR UNKNOWN_COMMAND_OR_ARGS\n", 28);
}