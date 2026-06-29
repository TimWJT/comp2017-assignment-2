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
#include <errno.h>
#include <time.h>
#include "../libs/markdown.h"

// Define command log array
#define MAX_LOG_ENTRIES 100

typedef struct {
    char* messages[MAX_LOG_ENTRIES];
    uint64_t versions[MAX_LOG_ENTRIES]; // Store version for each EDIT line
    int count;
} CommandLog;

// Define thread arguments structure
typedef struct {
    int file_descriptor;
    document* document;
    CommandLog* log;
    volatile sig_atomic_t* shutdown;
} ThreadArguments;

// Mutex for document access
pthread_mutex_t document_mutex = PTHREAD_MUTEX_INITIALIZER;

// Initialize command log
void initialize_command_log(CommandLog* log) {
    // Clear log entries
    log->count = 0;
    for (int index = 0; index < MAX_LOG_ENTRIES; index++) {
        log->messages[index] = NULL;
        log->versions[index] = 0;
    }
}

// Free command log
void free_command_log(CommandLog* log) {
    // Release memory for log messages
    for (int index = 0; index < log->count; index++) {
        free(log->messages[index]);
        log->messages[index] = NULL;
    }
    log->count = 0;
}

// Add log entry
void add_log_entry(CommandLog* log, const char* message, uint64_t version) {
    printf("DEBUG: Adding log entry: %s\n", message);
    if (log->count < MAX_LOG_ENTRIES) {
        // Allocate and store new message
        log->messages[log->count] = malloc(strlen(message) + 1);
        strcpy(log->messages[log->count], message);
        log->versions[log->count] = version;
        log->count++;
    }
}

// Print command log
void print_command_log(CommandLog* log) {
    printf("LOG?\n");
    uint64_t current_version = 0;
    int index = 0;
    while (index < log->count) {
        // Check for version change
        if (log->versions[index] != current_version) {
            if (index > 0) {
                printf("END\n");
            }
            current_version = log->versions[index];
            printf("VERSION %llu\n", current_version);
        }
        printf("%s\n", log->messages[index]);
        index++;
    }
    if (log->count > 0) {
        printf("END\n");
    }
}

// Signal handler for SIGRTMIN+1
volatile sig_atomic_t signal_received = 0;
void handle_sigrtmin_plus1(int signal, siginfo_t* info, void* context) {
    // Set flag on signal receipt
    signal_received = 1;
}

// Helper to read a line from file descriptor
static ssize_t read_line(int file_descriptor, char *buffer, size_t max) {
    size_t position = 0;
    while (position + 1 < max) {
        // Read one byte at a time
        ssize_t num_read = read(file_descriptor, buffer + position, 1);
        if (num_read <= 0) return num_read;
        if (buffer[position] == '\n') {
            buffer[position] = '\0';
            return position;
        }
        position++;
    }
    buffer[position] = '\0';
    return position;
}

// Applies editing commands to the document
static void apply_edit_command(document* document, uint64_t new_version, const char* edit_line) {
    printf("DEBUG: Applying edit command: %s, new_version=%llu, document_version=%zu\n", edit_line, new_version, document->version);
    char* line = strdup(edit_line);
    if (!line) {
        printf("DEBUG: Failed to allocate edit line copy\n");
        return;
    }
    // Find status in edit line
    char* last_space = strrchr(line, ' ');
    if (!last_space) {
        printf("DEBUG: Malformed EDIT line (no status): %s\n", edit_line);
        free(line);
        return;
    }
    char* status = last_space + 1;
    *last_space = '\0';
    char* save_pointer = NULL;
    char* token = strtok_r(line, " ", &save_pointer);
    if (!token || strcmp(token, "EDIT") != 0) {
        printf("DEBUG: Expected EDIT token, got: %s\n", edit_line);
        free(line);
        return;
    }
    char* username = strtok_r(NULL, " ", &save_pointer);
    if (!username) {
        printf("DEBUG: No username in: %s\n", edit_line);
        free(line);
        return;
    }
    char* command = save_pointer;
    if (!command) {
        printf("DEBUG: No command in: %s\n", edit_line);
        free(line);
        return;
    }
    if (strcmp(status, "SUCCESS") != 0) {
        printf("DEBUG: Skipping non-successful command: %s\n", edit_line);
        free(line);
        return;
    }

    char command_type[16];
    int position, number_of_chars, start, end, level;
    char content[256], url[256];
    pthread_mutex_lock(&document_mutex);

    // Parse and apply command
    if (sscanf(command, "%15s %d %255[^\n]", command_type, &position, content) == 3) {
        if (strcmp(command_type, "INSERT") == 0) {
            printf("DEBUG: Applying INSERT at position=%d, content=%s\n", position, content);
            markdown_insert(document, new_version, position, content);
        }
    } else if (sscanf(command, "%15s %d %d", command_type, &position, &number_of_chars) == 3) {
        if (strcmp(command_type, "DEL") == 0) {
            printf("DEBUG: Applying DEL at position=%d, number_of_chars=%d\n", position, number_of_chars);
            markdown_delete(document, new_version, position, number_of_chars);
        }
    } else if (sscanf(command, "%15s %d", command_type, &position) == 2) {
        if (strcmp(command_type, "NL") == 0) {
            printf("DEBUG: Applying NL at position=%d\n", position);
            markdown_newline(document, new_version, position);
        } else if (strcmp(command_type, "BLOCKQUOTE") == 0) {
            printf("DEBUG: Applying BLOCKQUOTE at position=%d\n", position);
            markdown_blockquote(document, new_version, position);
        } else if (strcmp(command_type, "OL") == 0) {
            printf("DEBUG: Applying OL at position=%d\n", position);
            markdown_ordered_list(document, new_version, position);
        } else if (strcmp(command_type, "UL") == 0) {
            printf("DEBUG: Applying UL at position=%d\n", position);
            markdown_unordered_list(document, new_version, position);
        } else if (strcmp(command_type, "HR") == 0) {
            printf("DEBUG: Applying HR at position=%d\n", position);
            markdown_horizontal_rule(document, new_version, position);
        }
    } else if (sscanf(command, "%15s %d %d", command_type, &level, &position) == 3) {
        if (strcmp(command_type, "HEADING") == 0) {
            printf("DEBUG: Applying HEADING level=%d at position=%d\n", level, position);
            markdown_heading(document, new_version, level, position);
        }
    } else if (sscanf(command, "%15s %d %d", command_type, &start, &end) == 3) {
        if (strcmp(command_type, "BOLD") == 0) {
            printf("DEBUG: Applying BOLD from start=%d to end=%d\n", start, end);
            markdown_bold(document, new_version, start, end);
        } else if (strcmp(command_type, "ITALIC") == 0) {
            printf("DEBUG: Applying ITALIC from start=%d to end=%d\n", start, end);
            markdown_italic(document, new_version, start, end);
        } else if (strcmp(command_type, "CODE") == 0) {
            printf("DEBUG: Applying CODE from start=%d to end=%d\n", start, end);
            markdown_code(document, new_version, start, end);
        }
    } else if (sscanf(command, "%15s %d %d %255s", command_type, &start, &end, url) == 4) {
        if (strcmp(command_type, "LINK") == 0) {
            printf("DEBUG: Applying LINK from start=%d to end=%d, url=%s\n", start, end, url);
            markdown_link(document, new_version, start, end, url);
        }
    } else {
        printf("DEBUG: Unknown or malformed EDIT command: %s\n", command);
    }

    document->version = new_version;
    printf("DEBUG: Updated document_version to %zu\n", document->version);
    pthread_mutex_lock(&document_mutex);
    free(line);
}

// Updates thread with latest document versions
void* update_thread_function(void* argument) {
    ThreadArguments* args = (ThreadArguments*)argument;
    int file_descriptor = args->file_descriptor;
    document* document = args->document;
    CommandLog* log = args->log;
    volatile sig_atomic_t* shutdown = args->shutdown;
    char buffer[1024];
    while (!*shutdown) {
        // Read server broadcasts
        ssize_t length = read_line(file_descriptor, buffer, sizeof(buffer));
        if (length <= 0) {
            printf("DEBUG: FIFO_S2C read failed\n");
            return NULL;
        }
        printf("DEBUG: Received message: %s, document_version=%zu\n", buffer, document->version);
        if (strncmp(buffer, "VERSION", 7) == 0) {
            uint64_t new_version = atoi(buffer + 8);
            printf("DEBUG: Processing VERSION %llu\n", new_version);
            pthread_mutex_lock(&document_mutex);
            document->version = new_version;
            printf("DEBUG: Updated document_version to %zu\n", document->version);
            pthread_mutex_unlock(&document_mutex);
            while (1) {
                length = read_line(file_descriptor, buffer, sizeof(buffer));
                if (length <= 0) {
                    printf("DEBUG: FIFO_S2C read failed\n");
                    return NULL;
                }
                printf("DEBUG: Received broadcast message: %s\n", buffer);
                if (strcmp(buffer, "END") == 0) {
                    printf("DEBUG: End of broadcast\n");
                    break;
                }
                add_log_entry(log, buffer, new_version);
                apply_edit_command(document, new_version, buffer);
            }
        } else if (strncmp(buffer, "Reject", 6) == 0) {
            printf("%s\n", buffer);
        }
    }
    return NULL;
}

// Main function handling client operations
int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("not enough arguments\n");
        exit(1);
    }
    pid_t server_process_id = atoi(argv[1]);
    char* username = argv[2];
    printf("Client PID: %d\n", getpid());

    // Initialize document and log
    document* document = markdown_init();
    CommandLog log;
    initialize_command_log(&log);
    volatile sig_atomic_t shutdown = 0;

    sigset_t signal_set;
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGRTMIN + 1);
    if (sigprocmask(SIG_BLOCK, &signal_set, NULL) == -1) {
        perror("sigprocmask failed");
        markdown_free(document);
        free_command_log(&log);
        exit(1);
    }

    struct sigaction signal_action;
    signal_action.sa_sigaction = handle_sigrtmin_plus1;
    signal_action.sa_flags = SA_SIGINFO;
    sigemptyset(&signal_action.sa_mask);
    if (sigaction(SIGRTMIN + 1, &signal_action, NULL) == -1) {
        perror("sigaction failed");
        markdown_free(document);
        free_command_log(&log);
        exit(1);
    }

    // Send signal to server
    union sigval signal_value = {0};
    if (sigqueue(server_process_id, SIGRTMIN, signal_value) == -1) {
        perror("sigqueue failed");
        markdown_free(document);
        free_command_log(&log);
        exit(1);
    }

    int signal;
    if (sigwait(&signal_set, &signal) != 0) {
        perror("sigwait failed");
        markdown_free(document);
        free_command_log(&log);
        exit(1);
    }

    char client_to_server_fifo[256];
    snprintf(client_to_server_fifo, sizeof(client_to_server_fifo), "FIFO_C2S_%d", getpid());
    int client_to_server_fd = open(client_to_server_fifo, O_WRONLY);
    if (client_to_server_fd == -1) {
        perror("open FIFO_C2S failed");
        markdown_free(document);
        free_command_log(&log);
        exit(1);
    }

    char server_to_client_fifo[256];
    snprintf(server_to_client_fifo, sizeof(server_to_client_fifo), "FIFO_S2C_%d", getpid());
    int server_to_client_fd = open(server_to_client_fifo, O_RDONLY);
    if (server_to_client_fd == -1) {
        perror("open FIFO_S2C failed");
        close(client_to_server_fd);
        markdown_free(document);
        free_command_log(&log);
        exit(1);
    }

    // Send username to server
    char username_buffer[256];
    int num_chars = snprintf(username_buffer, sizeof(username_buffer), "%s\n", username);
    if (write(client_to_server_fd, username_buffer, num_chars) < 0) {
        perror("write username failed");
        close(client_to_server_fd);
        close(server_to_client_fd);
        markdown_free(document);
        free_command_log(&log);
        exit(1);
    }

    char role[16] = {0};
    int position = 0;
    while (position < sizeof(role) - 1) {
        read(server_to_client_fd, role + position, 1);
        if (role[position] == '\n') {
            role[position] = '\0';
            break;
        }
        position++;
    }

    if (strcmp(role, "Reject") == 0) {
        char reject_buffer[256] = {0};
        position = 0;
        while (position < sizeof(reject_buffer) - 1) {
            read(server_to_client_fd, reject_buffer + position, 1);
            if (reject_buffer[position] == '\n') {
                reject_buffer[position] = '\0';
                break;
            }
            position++;
        }
        printf("Exiting because rejected, %s\n", reject_buffer);
        close(client_to_server_fd);
        close(server_to_client_fd);
        markdown_free(document);
        free_command_log(&log);
        exit(1);
    }

    char version_buffer[32] = {0};
    position = 0;
    while (position < sizeof(version_buffer) - 1) {
        read(server_to_client_fd, version_buffer + position, 1);
        if (version_buffer[position] == '\n') {
            version_buffer[position] = '\0';
            break;
        }
        position++;
    }
    uint64_t version = atoi(version_buffer);
    printf("DEBUG: Initial document_version=%llu\n", version);

    char length_buffer[32] = {0};
    position = 0;
    while (position < sizeof(length_buffer) - 1) {
        read(server_to_client_fd, length_buffer + position, 1);
        if (length_buffer[position] == '\n') {
            length_buffer[position] = '\0';
            break;
        }
        position++;
    }
    size_t length = atoi(length_buffer);

    // Read initial document
    char* document_buffer = malloc(length + 1);
    position = 0;
    while (position < length) {
        read(server_to_client_fd, document_buffer + position, 1);
        position++;
    }
    document_buffer[length] = '\0';

    pthread_mutex_lock(&document_mutex);
    for (size_t index = 0; index < length; index++) {
        char character[2] = {document_buffer[index], '\0'};
        markdown_insert(document, version, index, character);
    }
    document->version = version;
    pthread_mutex_unlock(&document_mutex);
    free(document_buffer);

    ThreadArguments thread_arguments;
    thread_arguments.file_descriptor = server_to_client_fd;
    thread_arguments.document = document;
    thread_arguments.log = &log;
    thread_arguments.shutdown = &shutdown;
    pthread_t update_thread;
    if (pthread_create(&update_thread, NULL, update_thread_function, &thread_arguments) != 0) {
        perror("pthread_create failed");
        close(client_to_server_fd);
        close(server_to_client_fd);
        markdown_free(document);
        free_command_log(&log);
        exit(1);
    }

    // Process user commands
    char command[256];
    while (fgets(command, sizeof(command), stdin)) {
        command[strcspn(command, "\n")] = '\0';
        printf("DEBUG: Processing command: %s, document_version=%zu\n", command, document->version);
        char command_buffer[256];
        pthread_mutex_lock(&document_mutex);
        uint64_t current_version = document->version;
        pthread_mutex_unlock(&document_mutex);
        if (strcmp(command, "DISCONNECT") == 0 || strcmp(command, "QUIT") == 0) {
            snprintf(command_buffer, sizeof(command_buffer), "%s\n", command);
            printf("DEBUG: Sending command: %s\n", command_buffer);
            if (write(client_to_server_fd, command_buffer, strlen(command_buffer)) < 0) {
                perror("write command failed");
            }
            break;
        } else if (strcmp(command, "DOC?") == 0) {
            pthread_mutex_lock(&document_mutex);
            markdown_print(document, stdout);
            pthread_mutex_unlock(&document_mutex);
            printf("\n");
            continue;
        } else if (strcmp(command, "PERM?") == 0) {
            printf("PERM?\n%s\n", role);
            continue;
        } else if (strcmp(command, "LOG?") == 0) {
            print_command_log(&log);
            continue;
        }
        char command_type[16], content[256] = "";
        int position, number_of_chars;
        if (sscanf(command, "%15s %d %255[^\n]", command_type, &position, content) >= 3 ||
            sscanf(command, "%15s %d %d", command_type, &position, &number_of_chars) == 3) {
            snprintf(command_buffer, sizeof(command_buffer), "%s\n", command);
            printf("DEBUG: Sending command: %s\n", command_buffer);
            if (write(client_to_server_fd, command_buffer, strlen(command_buffer)) < 0) {
                perror("write command failed");
                break;
            }
        } else {
            printf("ERROR: Invalid command format: %s\n", command);
        }
    }

    // Cleanup
    shutdown = 1;
    pthread_join(update_thread, NULL);
    close(client_to_server_fd);
    close(server_to_client_fd);
    unlink(client_to_server_fifo);
    unlink(server_to_client_fifo);
    pthread_mutex_lock(&document_mutex);
    markdown_free(document);
    pthread_mutex_unlock(&document_mutex);
    free_command_log(&log);
    pthread_mutex_destroy(&document_mutex);
    return 0;
}