#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <time.h>
#include "../libs/markdown.h"

// Constants for resource limits and buffer sizes
#define MAX_CLIENTS 100
#define MAX_LOG_ENTRIES 100
#define BUFFER_SIZE 256
#define ROLE_SIZE 16
#define FIFO_PREFIX "FIFO_"

// Client structure for managing client state
typedef struct {
    pid_t pid;              // Client process ID
    pthread_t thread;        // Client handling thread
    int c2s_fd;             // Client-to-server FIFO descriptor
    int s2c_fd;             // Server-to-client FIFO descriptor
    char *username;         // Client username
} client_t;

// Command structure for queued document operations
typedef struct command {
    char *text;             // Raw command (e.g., "INSERT 1 Hello")
    char *username;         // Username of command issuer
    uint64_t version;       // Document version for command
    time_t timestamp;       // Command submission time
    struct command *next;   // Next command in queue
} command_t;

// Server command log entry for tracking edits
typedef struct {
    uint64_t version;       // Document version
    char *edit_line;        // Edit command string
} log_entry_t;

// Server command log for storing edit history
typedef struct {
    log_entry_t entries[MAX_LOG_ENTRIES]; // Fixed-size log array
    int count;                            // Current entry count
} server_log_t;

// Global state: document, clients, command queue, and synchronization
static document *doc;                    // Shared markdown document
static pthread_mutex_t doc_mutex = PTHREAD_MUTEX_INITIALIZER;
static client_t *clients[MAX_CLIENTS];   // Client array
static int client_count = 0;
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
static server_log_t server_log = { .count = 0 };
static command_t *cmd_head = NULL;       // Command queue head
static command_t *cmd_tail = NULL;       // Command queue tail

// Initializes server command log
void init_server_log(server_log_t *log) {
    log->count = 0;
    for (int i = 0; i < MAX_LOG_ENTRIES; i++) {
        log->entries[i].edit_line = NULL;
    }
}

// Frees server command log resources
void free_server_log(server_log_t *log) {
    for (int i = 0; i < log->count; i++) {
        free(log->entries[i].edit_line);
        log->entries[i].edit_line = NULL;
    }
    log->count = 0;
}

// Adds entry to server command log
void add_server_log_entry(server_log_t *log, uint64_t version, const char *edit_line) {
    if (log->count >= MAX_LOG_ENTRIES) return;
    log->entries[log->count].version = version;
    log->entries[log->count].edit_line = strdup(edit_line);
    log->count++;
}

// Prints server command log with versioned edit lines
void print_server_log(const server_log_t *log) {
    printf("LOG?\n");
    uint64_t current_version = 0;
    for (int i = 0; i < log->count; i++) {
        if (log->entries[i].version != current_version) {
            if (i > 0) printf("END\n");
            current_version = log->entries[i].version;
            printf("VERSION %llu\n", current_version);
        }
        printf("%s\n", log->entries[i].edit_line);
    }
    if (log->count > 0) printf("END\n");
}

// Enqueues command to tail under doc_mutex
void enqueue_command(command_t *cmd) {
    cmd->next = NULL;
    if (cmd_tail) {
        cmd_tail->next = cmd;
    } else {
        cmd_head = cmd;
    }
    cmd_tail = cmd;
}

// Dequeues command from head under doc_mutex
command_t *dequeue_command(void) {
    if (!cmd_head) return NULL;
    command_t *cmd = cmd_head;
    cmd_head = cmd_head->next;
    if (!cmd_head) cmd_tail = NULL;
    return cmd;
}

// Checks user role from roles.txt
static char *check_user_role(const char *username) {
    FILE *roles = fopen("roles.txt", "r");
    if (!roles) return strdup("");
    char file_username[BUFFER_SIZE];
    char role[ROLE_SIZE] = "";
    while (fscanf(roles, "%255s %15s", file_username, role) == 2) {
        if (strcmp(username, file_username) == 0) break;
    }
    fclose(roles);
    return strdup(role[0] ? role : "");
}

// Applies markdown command to document with version and permission checks
static int apply_command(document *doc, uint64_t cmd_version, const char *command,
                        const char *username, char *status_buf, size_t status_buf_size) {
    // Reject outdated commands
    if (cmd_version < doc->version) {
        snprintf(status_buf, status_buf_size, "Reject OUTDATED_VERSION");
        return OUTDATED_VERSION;
    }

    // Verify write permission
    char *role = check_user_role(username);
    if (!role || strcmp(role, "write") != 0) {
        snprintf(status_buf, status_buf_size, "Reject UNAUTHORISED");
        free(role);
        return -1;
    }
    free(role);

    // Parse command
    char buf[BUFFER_SIZE];
    strncpy(buf, command, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *tok = strtok(buf, " ");
    if (!tok) {
        snprintf(status_buf, status_buf_size, "Reject INVALID_COMMAND");
        return -1;
    }

    int result = -1;
    if (strcmp(tok, "INSERT") == 0) {
        char *pos_s = strtok(NULL, " ");
        char *content = strtok(NULL, "");
        if (!pos_s || !content) {
            snprintf(status_buf, status_buf_size, "Reject INVALID_COMMAND");
            return -1;
        }
        size_t pos = strtoul(pos_s, NULL, 10);
        result = markdown_insert(doc, cmd_version, pos, content);
    } else if (strcmp(tok, "DEL") == 0) {
        char *pos_s = strtok(NULL, " ");
        char *len_s = strtok(NULL, " ");
        if (!pos_s || !len_s) {
            snprintf(status_buf, status_buf_size, "Reject INVALID_COMMAND");
            return -1;
        }
        size_t pos = strtoul(pos_s, NULL, 10);
        size_t len = strtoul(len_s, NULL, 10);
        result = markdown_delete(doc, cmd_version, pos, len);
    } else if (strcmp(tok, "NL") == 0) {
        char *pos_s = strtok(NULL, " ");
        if (!pos_s) {
            snprintf(status_buf, status_buf_size, "Reject INVALID_COMMAND");
            return -1;
        }
        size_t pos = strtoul(pos_s, NULL, 10);
        result = markdown_newline(doc, cmd_version, pos);
    } else if (strcmp(tok, "HEADING") == 0) {
        char *level_s = strtok(NULL, " ");
        char *pos_s = strtok(NULL, " ");
        if (!level_s || !pos_s) {
            snprintf(status_buf, status_buf_size, "Reject INVALID_COMMAND");
            return -1;
        }
        size_t level = strtoul(level_s, NULL, 10);
        size_t pos = strtoul(pos_s, NULL, 10);
        result = markdown_heading(doc, cmd_version, level, pos);
    } else if (strcmp(tok, "BOLD") == 0) {
        char *s_s = strtok(NULL, " ");
        char *e_s = strtok(NULL, " ");
        if (!s_s || !e_s) {
            snprintf(status_buf, status_buf_size, "Reject INVALID_COMMAND");
            return -1;
        }
        size_t start = strtoul(s_s, NULL, 10);
        size_t end = strtoul(e_s, NULL, 10);
        result = markdown_bold(doc, cmd_version, start, end);
    } else if (strcmp(tok, "ITALIC") == 0) {
        char *s_s = strtok(NULL, " ");
        char *e_s = strtok(NULL, " ");
        if (!s_s || !e_s) {
            snprintf(status_buf, status_buf_size, "Reject INVALID_COMMAND");
            return -1;
        }
        size_t start = strtoul(s_s, NULL, 10);
        size_t end = strtoul(e_s, NULL, 10);
        result = markdown_italic(doc, cmd_version, start, end);
    } else if (strcmp(tok, "BLOCKQUOTE") == 0) {
        char *pos_s = strtok(NULL, " ");
        if (!pos_s) {
            snprintf(status_buf, status_buf_size, "Reject INVALID_COMMAND");
            return -1;
        }
        size_t pos = strtoul(pos_s, NULL, 10);
        result = markdown_blockquote(doc, cmd_version, pos);
    } else if (strcmp(tok, "OL") == 0) {
        char *pos_s = strtok(NULL, " ");
        if (!pos_s) {
            snprintf(status_buf, status_buf_size, "Reject INVALID_COMMAND");
            return -1;
        }
        size_t pos = strtoul(pos_s, NULL, 10);
        result = markdown_ordered_list(doc, cmd_version, pos);
    } else if (strcmp(tok, "UL") == 0) {
        char *pos_s = strtok(NULL, " ");
        if (!pos_s) {
            snprintf(status_buf, status_buf_size, "Reject INVALID_COMMAND");
            return -1;
        }
        size_t pos = strtoul(pos_s, NULL, 10);
        result = markdown_unordered_list(doc, cmd_version, pos);
    } else if (strcmp(tok, "CODE") == 0) {
        char *s_s = strtok(NULL, " ");
        char *e_s = strtok(NULL, " ");
        if (!s_s || !e_s) {
            snprintf(status_buf, status_buf_size, "Reject INVALID_COMMAND");
            return -1;
        }
        size_t start = strtoul(s_s, NULL, 10);
        size_t end = strtoul(e_s, NULL, 10);
        result = markdown_code(doc, cmd_version, start, end);
    } else if (strcmp(tok, "HR") == 0) {
        char *pos_s = strtok(NULL, " ");
        if (!pos_s) {
            snprintf(status_buf, status_buf_size, "Reject INVALID_COMMAND");
            return -1;
        }
        size_t pos = strtoul(pos_s, NULL, 10);
        result = markdown_horizontal_rule(doc, cmd_version, pos);
    } else if (strcmp(tok, "LINK") == 0) {
        char *s_s = strtok(NULL, " ");
        char *e_s = strtok(NULL, " ");
        char *url = strtok(NULL, " ");
        if (!s_s || !e_s || !url) {
            snprintf(status_buf, status_buf_size, "Reject INVALID_COMMAND");
            return -1;
        }
        size_t start = strtoul(s_s, NULL, 10);
        size_t end = strtoul(e_s, NULL, 10);
        result = markdown_link(doc, cmd_version, start, end, url);
    } else {
        snprintf(status_buf, status_buf_size, "Reject INVALID_COMMAND");
        return -1;
    }

    // Map result to status message
    switch (result) {
        case SUCCESS: snprintf(status_buf, status_buf_size, "SUCCESS"); break;
        case INVALID_CURSOR_POS: snprintf(status_buf, status_buf_size, "Reject INVALID_CURSOR_POS"); break;
        case OUTDATED_VERSION: snprintf(status_buf, status_buf_size, "Reject OUTDATED_VERSION"); break;
        default: snprintf(status_buf, status_buf_size, "Reject UNKNOWN_ERROR"); break;
    }
    return result;
}

// Broadcasts document updates to all clients
void broadcast_document(void) {
    pthread_mutex_lock(&doc_mutex);
    char *edit_lines_buf = NULL;
    size_t edit_lines_len = 0;
    int commands_processed = 0;
    uint64_t broadcast_version = doc->version;

    // Process queued commands
    command_t *cmd = cmd_head;
    while (cmd) {
        char status[64];
        int result = apply_command(doc, cmd->version, cmd->text, cmd->username, status, sizeof(status));
        if (result == SUCCESS) commands_processed++;
        char edit_line[BUFFER_SIZE];
        snprintf(edit_line, sizeof(edit_line), "EDIT %s %s %s\n", cmd->username, cmd->text, status);
        add_server_log_entry(&server_log, broadcast_version, edit_line);
        size_t edit_line_len = strlen(edit_line);
        char *new_buf = realloc(edit_lines_buf, edit_lines_len + edit_line_len + 1);
        if (!new_buf) {
            cmd = cmd->next;
            continue;
        }
        edit_lines_buf = new_buf;
        strcpy(edit_lines_buf + edit_lines_len, edit_line);
        edit_lines_len += edit_line_len;
        cmd = cmd->next;
    }

    // Update document version if commands were applied
    if (commands_processed > 0) markdown_increment_version(doc);
    char version_buf[32];
    snprintf(version_buf, sizeof(version_buf), "VERSION %zu\n", doc->version);
    pthread_mutex_unlock(&doc_mutex);

    // Broadcast to clients
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++) {
        int fd = clients[i]->s2c_fd;
        write(fd, version_buf, strlen(version_buf));
        if (edit_lines_buf) write(fd, edit_lines_buf, edit_lines_len);
        write(fd, "END\n", 4);
    }
    pthread_mutex_unlock(&clients_mutex);

    // Clean up processed commands
    free(edit_lines_buf);
    pthread_mutex_lock(&doc_mutex);
    command_t *cmd_free;
    while ((cmd_free = dequeue_command()) != NULL) {
        free(cmd_free->text);
        free(cmd_free->username);
        free(cmd_free);
    }
    pthread_mutex_unlock(&doc_mutex);
}

// Timer thread for periodic broadcasts
void *timer_thread(void *arg) {
    long interval = *(long *)arg;
    while (1) {
        sleep(interval / 1000);
        broadcast_document();
    }
    return NULL;
}

// Reads a line from file descriptor
static ssize_t read_line(int fd, char *buf, size_t max) {
    size_t pos = 0;
    while (pos + 1 < max) {
        ssize_t n = read(fd, buf + pos, 1);
        if (n <= 0) return n;
        if (buf[pos] == '\n') {
            buf[pos] = '\0';
            return pos;
        }
        pos++;
    }
    buf[pos] = '\0';
    return pos;
}

// Handles client communication and commands
void *client_thread(void *arg) {
    client_t *client = (client_t *)arg;
    pid_t client_pid = client->pid;

    // Create client-specific FIFOs
    char c2s_fifo[BUFFER_SIZE], s2c_fifo[BUFFER_SIZE];
    snprintf(c2s_fifo, sizeof(c2s_fifo), "%sC2S_%d", FIFO_PREFIX, client_pid);
    snprintf(s2c_fifo, sizeof(s2c_fifo), "%sS2C_%d", FIFO_PREFIX, client_pid);
    unlink(c2s_fifo);
    unlink(s2c_fifo);
    if (mkfifo(c2s_fifo, 0666) == -1 || mkfifo(s2c_fifo, 0666) == -1) {
        free(client);
        return NULL;
    }

    // Signal client with SIGRTMIN+1
    union sigval sv = {0};
    if (sigqueue(client_pid, SIGRTMIN + 1, sv) == -1) {
        unlink(c2s_fifo);
        unlink(s2c_fifo);
        free(client);
        return NULL;
    }

    // Open FIFOs
    client->c2s_fd = open(c2s_fifo, O_RDONLY);
    if (client->c2s_fd == -1) {
        unlink(c2s_fifo);
        unlink(s2c_fifo);
        free(client);
        return NULL;
    }
    client->s2c_fd = open(s2c_fifo, O_WRONLY);
    if (client->s2c_fd == -1) {
        close(client->c2s_fd);
        unlink(c2s_fifo);
        unlink(s2c_fifo);
        free(client);
        return NULL;
    }

    // Read and validate username
    char username[BUFFER_SIZE] = {0};
    char buf[BUFFER_SIZE];
    ssize_t len = read_line(client->c2s_fd, buf, sizeof(buf));
    if (len <= 0) {
        goto cleanup;
    }
    strncpy(username, buf, sizeof(username) - 1);
    client->username = strdup(username);

    // Check role
    char *role = check_user_role(username);
    if (strcmp(role, "write") != 0 && strcmp(role, "read") != 0) {
        write(client->s2c_fd, "Reject UNAUTHORISED\n", 21);
        free(role);
        sleep(1);
        goto cleanup;
    }

    // Send role and initial document
    write(client->s2c_fd, role, strlen(role));
    write(client->s2c_fd, "\n", 1);
    free(role);
    pthread_mutex_lock(&doc_mutex);
    char version_buf[32];
    char length_buf[32];
    snprintf(version_buf, sizeof(version_buf), "%zu\n", doc->version);
    snprintf(length_buf, sizeof(length_buf), "%zu\n", doc->length);
    char *content = markdown_flatten(doc);
    write(client->s2c_fd, version_buf, strlen(version_buf));
    write(client->s2c_fd, length_buf, strlen(length_buf));
    write(client->s2c_fd, content, doc->length);
    free(content);
    pthread_mutex_unlock(&doc_mutex);

    // Process client commands
    char cmd_buf[BUFFER_SIZE];
    while (1) {
        len = read_line(client->c2s_fd, cmd_buf, sizeof(cmd_buf));
        if (len <= 0) break;
        if (strcmp(cmd_buf, "DISCONNECT") == 0 || strcmp(cmd_buf, "QUIT") == 0) break;
        command_t *cmd = malloc(sizeof(command_t));
        cmd->text = strdup(cmd_buf);
        cmd->username = strdup(client->username);
        pthread_mutex_lock(&doc_mutex);
        cmd->version = doc->version;
        cmd->timestamp = time(NULL);
        enqueue_command(cmd);
        pthread_mutex_unlock(&doc_mutex);
    }

cleanup:
    close(client->c2s_fd);
    close(client->s2c_fd);
    unlink(c2s_fifo);
    unlink(s2c_fifo);
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++) {
        if (clients[i] == client) {
            free(client->username);
            free(client);
            for (int j = i; j < client_count - 1; j++) {
                clients[j] = clients[j + 1];
            }
            client_count--;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    return NULL;
}

// Handles SIGRTMIN for client registration
void handle_sigrtmin(int sig, siginfo_t *info, void *context) {
    pid_t client_pid = info->si_pid;
    client_t *client = malloc(sizeof(client_t));
    client->pid = client_pid;
    if (pthread_create(&client->thread, NULL, client_thread, client) != 0) {
        free(client);
        return;
    }
    pthread_mutex_lock(&clients_mutex);
    clients[client_count++] = client;
    pthread_mutex_unlock(&clients_mutex);
}

// Handles server shutdown
void handle_quit(void) {
    pthread_mutex_lock(&clients_mutex);
    if (client_count > 0) {
        printf("QUIT rejected, %d clients still connected.\n", client_count);
        pthread_mutex_unlock(&clients_mutex);
        return;
    }
    pthread_mutex_unlock(&clients_mutex);

    // Process remaining commands
    if (cmd_head) {
        markdown_increment_version(doc);
        broadcast_document();
        command_t *cmd;
        while ((cmd = dequeue_command()) != NULL) {
            free(cmd->text);
            free(cmd->username);
            free(cmd);
        }
    }

    // Save document
    char *content = markdown_flatten(doc);
    FILE *file = fopen("doc.md", "w");
    if (file) {
        fwrite(content, 1, doc->length, file);
        fclose(file);
    }
    free(content);

    // Clean up resources
    markdown_free(doc);
    free_server_log(&server_log);
    pthread_mutex_destroy(&doc_mutex);
    pthread_mutex_destroy(&clients_mutex);
    exit(0);
}

int main(int argc, char *argv[]) {
    if (argc != 2) exit(1);
    long interval = atol(argv[1]);
    printf("Server PID: %d\n", getpid());

    // Initialize document and log
    doc = markdown_init();
    init_server_log(&server_log);

    // Set up signal handling
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGRTMIN);
    sigaddset(&set, SIGRTMIN + 1);
    sigprocmask(SIG_BLOCK, &set, NULL);
    struct sigaction sa = {
        .sa_sigaction = handle_sigrtmin,
        .sa_flags = SA_SIGINFO
    };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGRTMIN, &sa, NULL);
    sigdelset(&set, SIGRTMIN);
    sigprocmask(SIG_SETMASK, &set, NULL);

    // Start timer thread
    pthread_t timer;
    pthread_create(&timer, NULL, timer_thread, &interval);

    // Handle terminal commands
    char input[BUFFER_SIZE];
    while (1) {
        fgets(input, sizeof(input), stdin);
        input[strcspn(input, "\n")] = '\0';
        if (strcmp(input, "QUIT") == 0) {
            handle_quit();
        } else if (strcmp(input, "DOC?") == 0) {
            pthread_mutex_lock(&doc_mutex);
            markdown_print(doc, stdout);
            pthread_mutex_unlock(&doc_mutex);
            printf("\n");
        } else if (strcmp(input, "LOG?") == 0) {
            print_server_log(&server_log);
        }
    }
    return 0;
}