#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

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

static Document doc = {NULL, 0, 0};
static int c2s_fd, s2c_fd;
static char* role = NULL;

void handle_sigrtmin_plus1(int sig, siginfo_t* info, void* context) {
    // Signal received, proceed to open FIFOs
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_pid> <username>\n", argv[0]);
        return 1;
    }
    pid_t server_pid = atoi(argv[1]);
    char* username = argv[2];
    char c2s_fifo[256], s2c_fifo[256];
    snprintf(c2s_fifo, sizeof(c2s_fifo), "FIFO_C2S_%d", getpid());
    snprintf(s2c_fifo, sizeof(s2c_fifo), "FIFO_S2C_%d", getpid());

    // Send SIGRTMIN to server
    union sigval val = {0};
    sigqueue(server_pid, SIGRTMIN, val);

    // Wait for SIGRTMIN+1
    struct sigaction sa;
    sa.sa_sigaction = handle_sigrtmin_plus1;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGRTMIN + 1, &sa, NULL);

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGRTMIN + 1);
    int sig;
    sigwait(&set, &sig);

    // Open FIFOs
    c2s_fd = open(c2s_fifo, O_WRONLY);
    s2c_fd = open(s2c_fifo, O_RDONLY);
    if (c2s_fd == -1 || s2c_fd == -1) {
        perror("open fifo");
        return 1;
    }

    // Send username
    char username_buf[256];
    snprintf(username_buf, sizeof(username_buf), "%s\n", username);
    write(c2s_fd, username_buf, strlen(username_buf));

    // Read role
    char buf[256] = {0};
    int pos = 0;
    while (pos < sizeof(buf) - 1) {
        if (read(s2c_fd, buf + pos, 1) <= 0) break;
        if (buf[pos] == '\n') {
            buf[pos] = '\0';
            break;
        }
        pos++;
    }
    if (strncmp(buf, "Reject", 6) == 0) {
        printf("%s\n", buf);
        close(c2s_fd);
        close(s2c_fd);
        return 1;
    }
    role = strdup(buf);

    // Read document
    pos = 0;
    while (pos < sizeof(buf) - 1) {
        if (read(s2c_fd, buf + pos, 1) <= 0) break;
        if (buf[pos] == '\n') {
            buf[pos] = '\0';
            doc.version = atoll(buf);
            break;
        }
        pos++;
    }
    pos = 0;
    while (pos < sizeof(buf) - 1) {
        if (read(s2c_fd, buf + pos, 1) <= 0) break;
        if (buf[pos] == '\n') {
            buf[pos] = '\0';
            doc.length = atoll(buf);
            break;
        }
        pos++;
    }
    for (uint64_t i = 0; i < doc.length; i++) {
        char c;
        if (read(s2c_fd, &c, 1) <= 0) break;
        Node* new_node = malloc(sizeof(Node));
        new_node->data = c;
        new_node->next = doc.head;
        doc.head = new_node;
    }

    // Command loop
    char input[256];
    while (fgets(input, sizeof(input), stdin)) {
        input[strcspn(input, "\n")] = '\0';
        if (strcmp(input, "DISCONNECT") == 0) {
            write(c2s_fd, "DISCONNECT\n", 11);
            break;
        } else if (strcmp(input, "DOC?") == 0) {
            Node* curr = doc.head;
            while (curr) {
                putchar(curr->data);
                curr = curr->next;
            }
        } else if (strcmp(input, "PERM?") == 0) {
            printf("%s\n", role);
        } else {
            char cmd[256];
            snprintf(cmd, sizeof(cmd), "%s\n", input);
            write(c2s_fd, cmd, strlen(cmd));
        }
    }

    // Clean up
    close(c2s_fd);
    close(s2c_fd);
    Node* curr = doc.head;
    while (curr) {
        Node* tmp = curr;
        curr = curr->next;
        free(tmp);
    }
    free(role);
    return 0;
}