#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <time.h>
#include <cstdint>

// Node for linked list to store document
typedef struct Node {
    char data;
    struct Node* next;
} Node;

// Document structure
typedef struct {
    Node* head;
    uint64_t length;
    uint64_t version;
} Document;

// Client structure
typedef struct {
    pid_t pid;
    char* username;
    char* role;
    int c2s_fd; // Client-to-Server FIFO
    int s2c_fd; // Server-to-Client FIFO
    pthread_t thread;
} Client;

// Command structure for queue
typedef struct {
    char* command;
    char* username;
    time_t timestamp;
} Command;

// Global state
static Document doc = {NULL, 0, 0};
static Client* clients = NULL;
static int client_count = 0;
static Command* command_queue = NULL;
static int command_count = 0;
static pthread_mutex_t doc_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile sig_atomic_t running = 1;

// Signal handler for SIGRTMIN
void handle_sigrtmin(int sig, siginfo_t* info, void* context) {
    pid_t client_pid = info->si_pid;
    // Spawn thread to handle client
    Client* client = realloc(clients, (client_count + 1) * sizeof(Client));
    if (!client) {
        perror("realloc clients");
        return;
    }
    clients = client;
    client = &clients[client_count];
    client->pid = client_pid;
    client->username = NULL;
    client->role = NULL;
    if (pthread_create(&client->thread, NULL, handle_client, client) != 0) {
        perror("pthread_create");
        return;
    }
    client_count++;
}

// Client thread function
void* handle_client(void* arg) {
    Client* client = (Client*)arg;
    char c2s_fifo[256], s2c_fifo[256];
    snprintf(c2s_fifo, sizeof(c2s_fifo), "FIFO_C2S_%d", client->pid);
    snprintf(s2c_fifo, sizeof(s2c_fifo), "FIFO_S2C_%d", client->pid);

    // Clean up existing FIFOs
    unlink(c2s_fifo);
    unlink(s2c_fifo);

    // Create FIFOs
    if (mkfifo(c2s_fifo, 0666) == -1 || mkfifo(s2c_fifo, 0666) == -1) {
        perror("mkfifo");
        return NULL;
    }

    // Send SIGRTMIN+1 to client
    union sigval val = {0};
    sigqueue(client->pid, SIGRTMIN + 1, val);

    // Open FIFOs
    client->c2s_fd = open(c2s_fifo, O_RDONLY);
    client->s2c_fd = open(s2c_fifo, O_WRONLY);
    if (client->c2s_fd == -1 || client->s2c_fd == -1) {
        perror("open fifo");
        goto cleanup;
    }

    // Read username
    char username[256] = {0};
    char buf[256];
    int bytes = 0, pos = 0;
    while (pos < sizeof(username) - 1) {
        if (read(client->c2s_fd, buf + pos, 1) <= 0) break;
        if (buf[pos] == '\n') {
            buf[pos] = '\0';
            break;
        }
        pos++;
    }
    strncpy(username, buf, pos);

    // Check roles.txt
    FILE* roles = fopen("roles.txt", "r");
    char role[16] = {0};
    int authorised = 0;
    if (roles) {
        char line[256], u[256], r[16];
        while (fgets(line, sizeof(line), roles)) {
            sscanf(line, "%s %s", u, r);
            if (strcmp(u, username) == 0) {
                strcpy(role, r);
                authorised = 1;
                break;
            }
        }
        fclose(roles);
    }

    // Send role or rejection
    if (!authorised) {
        write(client->s2c_fd, "Reject UNAUTHORISED\n", 20);
        sleep(1);
        goto cleanup;
    }
    client->username = strdup(username);
    client->role = strdup(role);
    char role_msg[20];
    snprintf(role_msg, sizeof(role_msg), "%s\n", role);
    write(client->s2c_fd, role_msg, strlen(role_msg));

    // Send document
    pthread_mutex_lock(&doc_mutex);
    char len_buf[32];
    snprintf(len_buf, sizeof(len_buf), "%lu\n", doc.length);
    write(client->s2c_fd, "0\n", 2); // Version
    write(client->s2c_fd, len_buf, strlen(len_buf));
    Node* curr = doc.head;
    while (curr) {
        write(client->s2c_fd, &curr->data, 1);
        curr = curr->next;
    }
    pthread_mutex_unlock(&doc_mutex);

    // Command loop
    while (running) {
        char cmd[256] = {0};
        pos = 0;
        while (pos < sizeof(cmd) - 1) {
            if (read(client->c2s_fd, cmd + pos, 1) <= 0) break;
            if (cmd[pos] == '\n') {
                cmd[pos] = '\0';
                break;
            }
            pos++;
        }
        if (pos == 0) continue;

        // Queue command
        pthread_mutex_lock(&queue_mutex);
        command_queue = realloc(command_queue, (command_count + 1) * sizeof(Command));
        command_queue[command_count].command = strdup(cmd);
        command_queue[command_count].username = strdup(client->username);
        command_queue[command_count].timestamp = time(NULL);
        command_count++;
        pthread_mutex_unlock(&queue_mutex);
    }

cleanup:
    close(client->c2s_fd);
    close(client->s2c_fd);
    unlink(c2s_fifo);
    unlink(s2c_fifo);
    return NULL;
}

// Broadcast updates
void* broadcast_thread(void* arg) {
    int interval_ms = *(int*)arg;
    while (running) {
        usleep(interval_ms * 1000);
        pthread_mutex_lock(&queue_mutex);
        if (command_count == 0) {
            // Broadcast version only
            char msg[32];
            snprintf(msg, sizeof(msg), "VERSION %lu\nEND\n", doc.version);
            for (int i = 0; i < client_count; i++) {
                write(clients[i].s2c_fd, msg, strlen(msg));
            }
            pthread_mutex_unlock(&queue_mutex);
            continue;
        }

        // Process commands in timestamp order
        for (int i = 0; i < command_count - 1; i++) {
            for (int j = 0; j < command_count - i - 1; j++) {
                if (command_queue[j].timestamp > command_queue[j + 1].timestamp) {
                    Command tmp = command_queue[j];
                    command_queue[j] = command_queue[j + 1];
                    command_queue[j + 1] = tmp;
                }
            }
        }

        // Process commands
        char broadcast_msg[1024] = {0};
        snprintf(broadcast_msg, sizeof(broadcast_msg), "VERSION %lu\n", doc.version + 1);
        pthread_mutex_lock(&doc_mutex);
        for (int i = 0; i < command_count; i++) {
            char* cmd = command_queue[i].command;
            char* user = command_queue[i].username;
            char response[512];
            int valid = 1;

            // Parse and process command (simplified)
            if (strncmp(cmd, "INSERT ", 7) == 0 && strcmp(clients[i].role, "write") == 0) {
                uint64_t pos;
                char* content = strchr(cmd + 7, ' ') + 1;
                sscanf(cmd, "INSERT %lu", &pos);
                if (pos > doc.length) {
                    snprintf(response, sizeof(response), "EDIT %s %s Reject INVALID_POSITION\n", user, cmd);
                    valid = 0;
                } else {
                    Node* new_node = malloc(sizeof(Node));
                    new_node->data = content[0]; // Simplified: insert one char
                    new_node->next = NULL;
                    if (pos == 0) {
                        new_node->next = doc.head;
                        doc.head = new_node;
                    } else {
                        Node* curr = doc.head;
                        for (uint64_t j = 0; j < pos - 1 && curr; j++) curr = curr->next;
                        new_node->next = curr->next;
                        curr->next = new_node;
                    }
                    doc.length++;
                    snprintf(response, sizeof(response), "EDIT %s %s SUCCESS\n", user, cmd);
                }
            } else if (strncmp(cmd, "DISCONNECT", 10) == 0) {
                snprintf(response, sizeof(response), "EDIT %s %s SUCCESS\n", user, cmd);
                // Handle disconnect (simplified)
            } else {
                // snprintf(response, sizeof(response), "EDIT %s %s Reject UNAUTHORISED\n", user, cmd);
                snprintf(response, sizeof(response), "EDIT %s Reject UNAUTHORISED\n", user, cmd);

                valid = 0;
            }
            strcat(broadcast_msg, response);
            free(command_queue[i].command);
            free(command_queue[i].username);
        }
        strcat(broadcast_msg, "END\n");
        doc.version++;
        command_count = 0;
        free(command_queue);
        command_queue = NULL;

        // Broadcast to all clients
        for (int i = 0; i < client_count; i++) {
            write(clients[i].s2c_fd, broadcast_msg, strlen(broadcast_msg));
        }
        pthread_mutex_unlock(&doc_mutex);
        pthread_mutex_unlock(&queue_mutex);
    }
    return NULL;
}

// Save document to doc.md
void save_document() {
    FILE* fp = fopen("doc.md", "w");
    if (!fp) return;
    Node* curr = doc.head;
    while (curr) {
        fputc(curr->data, fp);
        curr = curr->next;
    }
    fclose(fp);
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <TIME_INTERVAL>\n", argv[0]);
        return 1;
    }
    int interval_ms = atoi(argv[1]);
    printf("Server PID: %d\n", getpid());

    // Set up signal handler
    struct sigaction sa;
    sa.sa_sigaction = handle_sigrtmin;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGRTMIN, &sa, NULL);

    // Block signals in main thread
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGRTMIN);
    sigaddset(&set, SIGRTMIN + 1);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    // Start broadcast thread
    pthread_t broadcast_tid;
    pthread_create(&broadcast_tid, NULL, broadcast_thread, &interval_ms);

    // Handle QUIT command
    char input[256];
    while (fgets(input, sizeof(input), stdin)) {
        input[strcspn(input, "\n")] = '\0';
        if (strcmp(input, "QUIT") == 0) {
            if (client_count > 0) {
                printf("QUIT rejected, %d clients still connected.\n", client_count);
            } else {
                running = 0;
                save_document();
                // Clean up
                for (int i = 0; i < client_count; i++) {
                    pthread_join(clients[i].thread, NULL);
                    free(clients[i].username);
                    free(clients[i].role);
                }
                free(clients);
                Node* curr = doc.head;
                while (curr) {
                    Node* tmp = curr;
                    curr = curr->next;
                    free(tmp);
                }
                pthread_join(broadcast_tid, NULL);
                break;
            }
        }
    }

    return 0;
}