#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdbool.h>
#include <stdlib.h>
#include <ncurses.h>


#define PORT 8888
#define BUFFER_SIZE 1024
#define NAME_LENGTH 20
#define MAX_OWNED_ROOMS 10
#define MAX_ROOM_MEMBERS 10
#define MAX_CLIENTS 30
#define MAX_USERS   30
#define MAX_ROOMS    10
#define TITLE "Chat Server"

typedef struct user User;
typedef struct client Client;
typedef struct room Room;

struct client {
    int sd;
};

struct user {
    int sd;
    int owned_rooms_count;
    char user_name[NAME_LENGTH];
    Room *active_room;
    Room *owned_rooms[MAX_OWNED_ROOMS];
};

struct room {
    int member_count;
    char room_name[NAME_LENGTH];
    User *members[MAX_ROOM_MEMBERS];
};


Client *g_clients[MAX_CLIENTS];
User *g_users[MAX_USERS];
Room *g_rooms[MAX_ROOMS];
int g_client_count = 0;
int g_user_count = 0;
int g_room_count = 0;
WINDOW *main_window, *clients_window, *users_window, *rooms_window;

// [(client login)=> he is in users]. [(user create room)=> room is in owned]. [(user joined a room) => he is in members , his active room is this room]
// [(user logout): if has active room: remove from members , if has owned rooms: remove them , remove from users]
// [(remove room): if has members: tell them to leave , remove it from owned , remove it from rooms]
// [(leave room): empty active room , remove user from members]

// debug functions
void d_print_clients();
void d_print_users();
void d_print_rooms();
void d_print_command(int n, int w, char command[n][w]);
// helper functions
User *get_user(int sd);     // find user with sd
Room *get_room(char *name);     // find room with name
void add_client(int sd);        // add new client to clients array (new connection)
void remove_client(int sd); // remove client with passed sd from clients (disconnection)
int start_server(struct sockaddr_in server_addr);   // create server and start listening
int setup_sds(fd_set *read_fds, int server_sd);     // set clients and server sds and return max sd
bool is_ready(int sd, fd_set *read_fds);        // check if this sd is ready to read
void get_ready_client(fd_set *read_fds, Client **client);        // find the client who sent data

void remove_user(int sd);    // remove user with this sd from users (logout)
void remove_room(char *name);       // remove room with this name from rooms (logout)
void remove_rooms(Room**, int);         // remove passed rooms from global rooms array (logout)

void user_login(int sd, char *user_name);     // add new user
void user_logout(int sd);             // if has active room: remove from members , if has owned rooms: remove them , remove from users
void create_room(int sd, char *room_name);
void join_room(int sd, char *room_name);
void leave_room(int sd);     // empty active room , remove user from members
void send_available_rooms_to_user(int sd);
void send_server_command_to_users(char *command, User **users, int n);
void forward_message_to_users(char *name, char *message, User **members, int n);

// Ncurses
void draw_window(WINDOW *win, char *title);
void easy_print(WINDOW *win, char *title, char *s);

int main() {
    int server_sd, max_sd, buf_len, addr_len;
    char buf[BUFFER_SIZE] = "";
    bool exiting = false;
    fd_set read_fds;
    struct sockaddr_in server_addr;

    initscr();
    cbreak();
    keypad(stdscr, TRUE);

    main_window = newwin(LINES*2/3, COLS*2/3, 0, 0);
    clients_window = newwin(LINES*2/3, COLS/3, 0, COLS*2/3);
    users_window = newwin(LINES/3, COLS/2, LINES*2/3, 0);
    rooms_window = newwin(LINES/3, COLS/2, LINES*2/3, COLS/2);

    wmove(main_window, 1, 0);
    wmove(clients_window, 1, 0);
    wmove(users_window, 1, 0);
    wmove(rooms_window, 1, 0);
    draw_window(main_window, "main events");
    draw_window(clients_window, "clients");
    draw_window(users_window, "users");
    draw_window(rooms_window, "rooms");
    scrollok(main_window, true);
    scrollok(clients_window, true);
    scrollok(users_window, true);
    scrollok(rooms_window, true);

    // server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = ntohs(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    addr_len = sizeof(server_addr);

    server_sd = start_server(server_addr);
    easy_print(main_window, TITLE, "[SERVER_STARTED]\n");


    while (!exiting) {

        max_sd = setup_sds(&read_fds, server_sd);
        select(max_sd + 1, &read_fds, NULL, NULL, NULL);

        // if server sd is ready that means there is new connection
        if (is_ready(server_sd, &read_fds)) {
            int new_sd = accept(server_sd, (struct sockaddr *) &server_addr, &addr_len);    // accept incoming connection
            add_client(new_sd);
            easy_print(main_window, TITLE, "[NEW_CLIENT]\n");
            d_print_clients();
            continue;
        }

        Client *ready_client;
        get_ready_client(&read_fds, &ready_client);

        buf_len = read(ready_client->sd, buf, 1024);    // read what client sent
        buf[buf_len] = '\0';

        if (buf_len == 0) {
            user_logout(ready_client->sd);
            remove_client(ready_client->sd);
            easy_print(main_window, TITLE, "[CLIENT_DISCONNECTED]\n");
            d_print_clients();
            continue;
        }

        // parse buffer
        char *token, query[2][100], query_count = 0;
        token = strtok(buf, ",");
        while (token != NULL) {
            strcpy(query[query_count++], token);
            token = strtok(NULL, ",");
        }

        d_print_command(query_count, 100, query);

        if (strcmp(query[0], "user_login") == 0) {
            user_login(ready_client->sd, query[1]);
            d_print_users();
            d_print_rooms();
        }
        else if (strcmp(query[0], "user_logout") == 0) {
            user_logout(ready_client->sd);
            d_print_users();
            d_print_rooms();
        }
        else if (strcmp(query[0], "get_rooms") == 0) {
            send_available_rooms_to_user(ready_client->sd);
        }
        else if (strcmp(query[0], "create_room") == 0) {
            create_room(ready_client->sd, query[1]);
            d_print_users();
            d_print_rooms();
        }
        else if (strcmp(query[0], "join_room") == 0) {
            join_room(ready_client->sd, query[1]);
            d_print_users();
            d_print_rooms();
        }
        else if (strcmp(query[0], "leave_room") == 0) {
            leave_room(ready_client->sd);
            d_print_users();
            d_print_rooms();
        }
        else if (strcmp(query[0], "text_message") == 0) {
            // handle received message , get active room of that user and send message to all members
            User *user = get_user(ready_client->sd);
            forward_message_to_users(user->user_name, query[1], user->active_room->members,
                                     user->active_room->member_count);
        }
        else if (strcmp(query[0], "shutdown") == 0) {
            exiting = true;
        }
        else {
            // unknown command
            easy_print(main_window, TITLE, "[COMMAND_NOT_FOUND]\n");
        }
    }

    easy_print(main_window, TITLE, "[SERVER_ENDED]\n");


    endwin();
    return 0;
}

void d_print_clients() {
    char buf[1024];
    sprintf(buf, "Clients(%i):\n", g_client_count);
    easy_print(clients_window, TITLE, buf);
    for (int i = 0; i < g_client_count; i++) {
        sprintf(buf, "\tsd[%i]\n", g_clients[i]->sd);
        easy_print(clients_window, TITLE, buf);
    }
}

void d_print_users() {
    char buf[1024];
    sprintf(buf, "Users(%i):\n", g_user_count);
    easy_print(users_window, TITLE, buf);
    for (int i = 0; i < g_user_count; i++) {
        char *room_name = g_users[i]->active_room == NULL ? "" : g_users[i]->active_room->room_name;
        sprintf(buf, "\tuser_name[%s] active_room[%s] owned_rooms[", g_users[i]->user_name, room_name);
        easy_print(users_window, TITLE, buf);

        for (int j = 0; j < g_users[i]->owned_rooms_count; j++) {
            if (j > 0) {
                easy_print(users_window, TITLE, ",");
            }
            sprintf(buf, "%s", g_users[i]->owned_rooms[j]->room_name);
            easy_print(users_window, TITLE, buf);
        }
        easy_print(users_window, TITLE, "]\n");
    }
}

void d_print_rooms() {
    char buf[1024];
    sprintf(buf, "Rooms(%i):\n", g_room_count);
    easy_print(rooms_window, TITLE, buf);
    for (int i = 0; i < g_room_count; i++) {
        sprintf(buf, "\troom_name[%s] members[", g_rooms[i]->room_name);
        easy_print(rooms_window, TITLE, buf);
        for (int j = 0; j < g_rooms[i]->member_count; j++) {
            if (j > 0)
                easy_print(rooms_window, TITLE, ",");

            easy_print(rooms_window, TITLE, g_rooms[i]->members[j]->user_name);
        }
        easy_print(rooms_window, TITLE, "]\n");
    }
}

void d_print_command(int n, int w, char command[n][w]) {
    char buf[1024];
    sprintf(buf, "%s", command[0]);
    easy_print(main_window, TITLE, buf);

    for (int i = 1; i < n; i++) {
        easy_print(main_window, TITLE, ",");

        sprintf(buf, "%s", command[i]);
        easy_print(main_window, TITLE, buf);
    }
    easy_print(main_window, TITLE, "\n");
}

User *get_user(int sd) {    // find user with connection id and return it
    for (int i = 0; i < g_user_count; i++) {
        if (g_users[i]->sd == sd)
            return g_users[i];
    }

    return NULL;
}

Room *get_room(char *name) {  // find room with room name and return it
    for (int i = 0; i < g_room_count; i++) {
        if (strcmp(g_rooms[i]->room_name, name) == 0)
            return g_rooms[i];
    }

    return NULL;
}

void add_client(int sd) {
    Client *new_client = malloc(sizeof(Client));
    new_client->sd = sd;
    g_clients[g_client_count] = new_client;
    g_client_count++;
}

void remove_client(int sd) {
    for (int i = 0; i < g_client_count; i++) {
        if (g_clients[i]->sd == sd) {
            close(g_clients[i]->sd);    // safely close the socket

            // If its not the last one , make it the last
            if (i < g_client_count - 1)
                g_clients[i] = g_clients[g_client_count - 1];

            g_client_count--;

            break;
        }
    }
}

int start_server(struct sockaddr_in server_addr) {
    bool opt = true;
    int sd;

    // create a socket and get socket descriptor , Bind that sd to server address , Start listening
    sd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    bind(sd, (const struct sockaddr *) &server_addr, sizeof(server_addr));
    listen(sd, 3);

    return sd;
}

int setup_sds(fd_set *read_fds, int server_sd) {

    int max_sd;
    FD_ZERO(read_fds);

    FD_SET(server_sd, read_fds);
    max_sd = server_sd;

    for (int i = 0; i < g_client_count; i++) {
        FD_SET(g_clients[i]->sd, read_fds);

        if (g_clients[i]->sd > max_sd)
            max_sd = g_clients[i]->sd;
    }
    return max_sd;
}

bool is_ready(int sd, fd_set *read_fds) {
    return FD_ISSET(sd, read_fds);
}

void get_ready_client(fd_set *read_fds, Client **client) {
    for (int i = 0; i < g_client_count; i++) {
        if (is_ready(g_clients[i]->sd, read_fds)) {
            *client = g_clients[i];
            return;
        }
    }
}

void remove_user(int sd) {
    for (int i = 0; i < g_user_count; i++) {
        if (g_users[i]->sd == sd) {
            if (i < g_user_count - 1)
                g_users[i] = g_users[g_user_count - 1];

            g_user_count--;
        }
    }
}

void remove_room(char *name) {
    for (int i = 0; i < g_room_count; i++) {
        if (strcmp(g_rooms[i]->room_name, name) == 0) {
            // order all members to remove_their active room and its messages
            send_server_command_to_users("remove_room", g_rooms[i]->members, g_rooms[i]->member_count);
            // set active room to null for the deleted room members
            for (int j = 0; j < g_rooms[i]->member_count; j++) {
                User *user = g_rooms[i]->members[j];
                user->active_room = NULL;
            }

            if (i < g_room_count - 1) {
                g_rooms[i] = g_rooms[g_room_count - 1];
            }
            g_room_count--;
        }
    }
}

void remove_rooms(Room **rooms, int n) {
    while (n > 0) {
        n--;
        remove_room(rooms[n]->room_name);
    }
}

void user_login(int sd, char *user_name) {
    User *new_user = malloc(sizeof(User));
    new_user->sd = sd;
    strcpy(new_user->user_name, user_name);
    new_user->owned_rooms_count = 0;

    g_users[g_user_count] = new_user;
    g_user_count++;
}

void user_logout(int sd) {    // if has active room: remove from members , if has owned rooms: remove them , remove from users
    User *user = get_user(sd);
    if (user == NULL)
        return;

    // leave from active room
    if (user->active_room != NULL)
        leave_room(sd);

    // remove owned rooms
    if (user->owned_rooms_count > 0)
        remove_rooms(user->owned_rooms, user->owned_rooms_count);

    // Remove from users
    remove_user(sd);
}

void create_room(int sd, char *room_name) {
    User *user = get_user(sd);
    Room *room = get_room(room_name);
    if (room != NULL) {
        easy_print(main_window, TITLE, "[room name taken]\n");

        return;
    }

    Room *new_room = malloc(sizeof(Room));
    strcpy(new_room->room_name, room_name);
    new_room->member_count = 0;
    g_rooms[g_room_count] = new_room;
    user->owned_rooms[user->owned_rooms_count] = g_rooms[g_room_count];
    g_room_count++;
    user->owned_rooms_count++;
}

void join_room(int sd, char *room_name) {
    User *user = get_user(sd);
    Room *room = get_room(room_name);

    char buf[1024] = "server_command,result_code,";
    if (room == NULL) {
        strcat(buf, "-1");
        send(sd, buf, strlen(buf), 0);
        easy_print(main_window, TITLE, "room_not_found code sent\n");
        return;
    }

    room->members[room->member_count] = user;
    room->member_count++;
    user->active_room = room;

    strcat(buf, "0");
    send(sd, buf, strlen(buf), 0);
}

void leave_room(int sd) {    // empty active room , remove user from members
    User *user = get_user(sd);
    if (user == NULL)
        return;

    Room *room = user->active_room;
    if (room == NULL)
        return;

    // find user in members and remove it
    for (int i = 0; i < room->member_count; i++) {
        if (room->members[i] == user) {
            if (i < room->member_count - 1)
                room->members[i] = room->members[room->member_count - 1];

            room->members[room->member_count] = NULL;
            room->member_count--;
        }
    }

    user->active_room = NULL;
}

void send_available_rooms_to_user(int sd) {
    char *buf = malloc(sizeof(char) * 1024);

    strcpy(buf, "rooms,");

    if (g_room_count == 0) {
        strcat(buf, "\t[empty]");
    }
    else {
        for (int i = 0; i < g_room_count; i++) {
            if (i > 0)
                strcat(buf, "\n");

            strcat(buf, "\t");
            strcat(buf, g_rooms[i]->room_name);
        }
    }

    send(sd, buf, strlen(buf), 0);
}

void send_server_command_to_users(char *command, User **users, int n) {
    char buf[1024];

    for (int i = 0; i < n; i++) {
        sprintf(buf, "server_command,%s", command);
        send(users[i]->sd, buf, strlen(buf), 0);
    }
}

void forward_message_to_users(char *name, char *message, User **members, int n) {
    char buf[1024];

    for (int i = 0; i < n; i++) {
        sprintf(buf, "text_message,%s,%s", name, message);
        send(members[i]->sd, buf, strlen(buf), 0);
    }
}

// Ncurses
void draw_window(WINDOW *win, char *title) {
    int y, x;
    getyx(win, y, x);
    wborder(win, 0,0,0,0,0,0,0,0);
    mvwprintw(win, 0,1, " %s: ", title);
    wmove(win, y, x);
    wrefresh(win);
}

void easy_print(WINDOW *win, char *title, char *s) {
    wprintw(win, " %s", s);
    draw_window(win, title);
}