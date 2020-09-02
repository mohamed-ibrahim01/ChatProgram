#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

#define PORT 8888
#define NAME_LENGTH 20
#define MAX_ROOM_MESSAGES 100
#define MESSAGE_LENGTH 100


struct recv_thread_args {
    int sd;
};

enum guest_menu_choice {
    LOGIN=1, EXIT
};

enum user_menu_choice {
    LIST_ROOMS=1, CREATE_ROOM, JOIN_ROOM, LOGOUT
};

enum room_menu_choice {
    DISPLAY_MESSAGES=1, WRITE_MESSAGE, LEAVE_ROOM
};

enum menu {
    GUEST_MENU=100, USER_MENU, ROOM_MENU
};


char main_buf[1024];    // used to store some received data to handel it outside the receiving thread
int g_sd;
char g_user_name[NAME_LENGTH];
char g_active_room_name[NAME_LENGTH];
char g_messages[MESSAGE_LENGTH][MAX_ROOM_MESSAGES];
int g_message_count = 0;
enum menu current_menu = GUEST_MENU;

int guest_menu();  // Print guest menu
int user_menu();   // Print User menu
int room_menu();   // Print Room menu
void serve_user(int, enum menu*);    // handle user navigation through menus
int connect_to_server(char*);       // connect to the server and return the sd
void disconnect_from_server(int);     // close sd
void request_login(int);     // prompt the user for user name and send login command to server
void request_logout(int);     // send logout command to server
void request_rooms_list(int);   // send get rooms command to server , wait to receive it and print it
void request_create_room(int);   // send create room command to server
int request_join_room(int);      // send join command to server , wait to receive result code handle it
void request_leave_room(int, char*);    // send leave room to server and empty local data (messages , active room)
void request_send_message(int);    // send text message command to server
void print_messages(int);       // print messages
void* receive_new_data(void*);     // run on separate thread , constantly waiting for new data , handle some and fill the buffer with other
void h_empty_room_local_data();    // empty local data (messages , active room)
void h_wait_main_buffer();


int main() {
    g_sd = connect_to_server("127.0.0.1");

    // Start receiving thread
    struct recv_thread_args args = {g_sd};
    pthread_t recv_thread;
    pthread_create(&recv_thread, NULL, receive_new_data, &args);

    serve_user(g_sd, &current_menu);

    disconnect_from_server(g_sd);
    printf("Program finished\n");

    return 0;
}



int guest_menu() {
    int choice;
    do {
        printf("1 - Login\n");
        printf("2 - Exit\n");
        printf("--------------------\n");
        printf("Choice: ");
        scanf("%i", &choice);
        if (choice == 126)      // server shutdown secret code : 126
            break;

    } while (choice < LOGIN || choice > EXIT);

    return choice;
}

int user_menu() {
    int choice;
    do {
        printf("1 - List available chat rooms\n");
        printf("2 - Create chat room\n");
        printf("3 - Join chat room\n");
        printf("4 - Logout\n");
        printf("--------------------\n");
        printf("Choice: ");
        scanf("%i", &choice);
    } while (choice < LIST_ROOMS || choice > LOGOUT);

    return choice;
}

int room_menu() {
    int choice;
    do {
        printf("1 - Display messages\n");
        printf("2 - Write a message\n");
        printf("3 - Leave room\n");
        printf("--------------------\n");
        printf("Choice: ");
        scanf("%i", &choice);
    } while (choice < DISPLAY_MESSAGES || choice > LEAVE_ROOM);

    return choice;
}

void serve_user(int sd, enum menu *current_menu) {
    int choice;
    char input[1024];
    while (1) {
        if (*current_menu == GUEST_MENU) {
            choice = guest_menu();

            switch (choice) {
                case EXIT:
                    return;
                case LOGIN:
                    // Login process
                    request_login(sd);
                    *current_menu = USER_MENU;      // if login is successful
                    break;
                case 126:   // secret code to shutdown server
                    send(g_sd, "shutdown", 8, 0);
                    break;
                default:
                    break;
            }
        } else if (*current_menu == USER_MENU) {
            choice = user_menu();

            switch (choice) {
                case LIST_ROOMS:
                    // list available chat rooms on the server
                    request_rooms_list(g_sd);
                    break;
                case CREATE_ROOM:
                    // create room process
                    request_create_room(sd);
                    break;
                case JOIN_ROOM:
                    // join room process
                    if (request_join_room(sd) == 0)
                        *current_menu = ROOM_MENU;
                    break;
                case LOGOUT:
                    // logout
                    request_logout(sd);
                    *current_menu = GUEST_MENU;     // if logout is successful
                    break;
                default:
                    break;
            }
        } else if (*current_menu == ROOM_MENU) {
            choice = room_menu();

            switch (choice) {
                case DISPLAY_MESSAGES:
                    // print messages from active room local data
                    print_messages(sd);
                    break;
                case WRITE_MESSAGE:
                    // get message from user and send it to server
                    request_send_message(sd);
                    break;
                case LEAVE_ROOM:
                    // leave room process
                    request_leave_room(sd, g_active_room_name);
                    *current_menu = USER_MENU;  // if leave room is successful
                    break;
                default:
                    break;
            }
        }
    }
}

int connect_to_server(char *ip) {
    int sd;
    struct sockaddr_in server_addr;

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = ntohs(PORT);
    inet_pton(AF_INET, ip, &server_addr.sin_addr);

    sd = socket(AF_INET, SOCK_STREAM, 0);
    connect(sd, (const struct sockaddr *) &server_addr, sizeof(server_addr));

    return sd;
}

void disconnect_from_server(int sd) {
    close(sd);
}

void request_login(int sd) {
    char buf[1024], name[1024];

    printf("User name: ");
    scanf("%s", name);

    sprintf(buf, "user_login,%s", name);
    send(sd, buf, strlen(buf), 0);

    strcpy(g_user_name, name);
}

void request_logout(int connection) {
    char buf[1024];
    sprintf(buf, "user_logout");
    send(connection, buf, strlen(buf), 0);
    h_empty_room_local_data();
}

void request_rooms_list(int connection) {
    char buf[1024] = "get_rooms";
    send(connection, buf, strlen(buf), 0);

    h_wait_main_buffer();
    printf("%s\n", main_buf);

    main_buf[0] = '\0';
}

void request_create_room(int connection) {
    char buf[1024], name[1024];

    printf("Room name: ");
    scanf("%s", name);

    sprintf(buf, "create_room,%s", name);
    send(connection, buf, strlen(buf), 0);
}

int request_join_room(int connection) {
    char buf[1024], name[1024];

    printf("Room name: ");
    scanf("%s", name);

    sprintf(buf, "join_room,%s", name);
    send(connection, buf, strlen(buf), 0);

    h_wait_main_buffer();
    if (strcmp(main_buf, "-1") == 0) {
        printf("room not found\n");
        return -1;
    }
    main_buf[0] = '\0';

    strcpy(g_active_room_name, name);
    return 0;
}

void request_leave_room(int connection, char *room_name) {
    char buf[1024];
    sprintf(buf, "leave_room,%s", room_name);
    send(connection, buf, strlen(buf), 0);

    h_empty_room_local_data();
}

void request_send_message(int connection) {
    char buf[1024], message[1024];

    if (strlen(g_active_room_name) == 0) {
        printf("room was deleted.\n");
        return;
    }

    printf("Your message: ");
    scanf("%s", message);

    sprintf(buf, "text_message,%s", message);
    send(connection, buf, strlen(buf), 0);
}

void print_messages(int connection) { // print messages in user's room_messages[]
    if (strlen(g_active_room_name) == 0) {
        printf("room was deleted.\n");
        return;
    }

    printf("%s messages:\n", g_active_room_name);

    if (g_message_count == 0)
        printf("empty\n");

    for (int i = 0; i < g_message_count; i++) {
        printf("%s\n", g_messages[i]);
    }
}

void* receive_new_data(void *args) {
    struct recv_thread_args *a = args;
    int connection = a->sd;
    while (1) {
        char buf[1024], result[3][100], *token, result_count = 0, buf_len;
        buf_len = read(connection, buf, 1024);
        buf[buf_len] = '\0';
        if (buf_len == 0)
            break;

        // parse buffer
        token = strtok(buf, ",");
        while (token != NULL) {
            strcpy(result[result_count++], token);
            token = strtok(NULL, ",");
        }

        if (strcmp(result[0], "rooms") == 0) {
            sprintf(main_buf, "Rooms:\n%s\n", result[1]);
        }
        else if (strcmp(result[0], "text_message") == 0) {
            sprintf(g_messages[g_message_count], "%s: %s", result[1], result[2]);
            g_message_count++;
        }
        else if (strcmp(result[0], "server_command") == 0) {
            if (strcmp(result[1], "remove_room") == 0) {
                printf("\n%s room was deleted from server. You should leave the room.\n", g_active_room_name);
                h_empty_room_local_data();
                current_menu = USER_MENU;
            }
            else if (strcmp(result[1], "result_code") == 0) {
                strcpy(main_buf, result[2]);
            }
        }
    }
    printf("\n[RECEIVING_THREAD_ENDED]\n");
}

void h_empty_room_local_data() {
    g_message_count = 0;
    strcpy(g_active_room_name, "");
}

void h_wait_main_buffer() {
    while (strlen(main_buf) == 0) {
        usleep(100);
    }
}
