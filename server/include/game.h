#ifndef GAME_H
#define GAME_H

#include "server.h"

void handle_client_message(Client *client, char *message);
void create_match(Client* client);
void list_matches(const Client* client);
void request_join_match(Client *client, int match_id);
void handle_join_response(const Client *client, int match_id, int accepted);
void place_ship(const Client *client, int match_id, int x, int y, char orientation);
void handle_shoot(const Client *client, int match_id, int x, int  y);

void handle_rematch(const Client *client, int match_id);
void handle_leave(const Client *client, int match_id);
void cleanup_client_matches(const Client *client);

#endif