#define _XOPEN_SOURCE 700 // request all POSIX features, even with -std=c11
#include <fcntl.h>        // for O_CREAT, O_RDWR
#include <limits.h>       // for NAME_MAX
#include <stdlib.h>       // for EXIT_SUCCESS, NULL, abort
#include <stdio.h>        // for getline, perror
#include <sys/mman.h>     // for mmap, shm_open
#include <signal.h>       // for signals
#include <string.h>       // for memset
#include <time.h>         // for nanosleep 
#include <unistd.h>       // for getpid, kill

#define BOX_SIZE 4096

pid_t other_pid = 0;
char *my_inbox;     // inbox of current process, set by setup_inboxes()
char *other_inbox;  // inbox of PID other_pid, set by setup_inboxes()
char my_inbox_shm_open_name[NAME_MAX];
char other_inbox_shm_open_name[NAME_MAX];

// Function to set up a shared memory inbox for a process
char *setup_inbox_for(pid_t pid, char *filename) {
    snprintf(filename, NAME_MAX, "/%d-chat", pid);
    int fd = shm_open(filename, O_CREAT | O_RDWR, 0666);
    if (fd < 0) {
        perror("shm_open");
        abort();
    }
    if (ftruncate(fd, BOX_SIZE) != 0) {
        perror("ftruncate");
        abort();
    }
    char *ptr = mmap(NULL, BOX_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == (char *) MAP_FAILED) {
        perror("mmap");
        abort();
    }
    return ptr;
}

// Function to set up inboxes for communication
void setup_inboxes() {
    my_inbox = setup_inbox_for(getpid(), my_inbox_shm_open_name);
    other_inbox = setup_inbox_for(other_pid, other_inbox_shm_open_name);
    memset(my_inbox, 0, BOX_SIZE); // Clear inbox initially
    memset(other_inbox, 0, BOX_SIZE); // Clear outbox initially
}

// Cleanup function to release resources and unlink shared memory
void cleanup_inboxes() {
    munmap(my_inbox, BOX_SIZE);
    munmap(other_inbox, BOX_SIZE);
    shm_unlink(my_inbox_shm_open_name);
}

// Signal handler for SIGINT, SIGTERM, and SIGUSR1
void signal_handler(int signum) {
    if (signum == SIGTERM) {
        printf("Received SIGTERM, cleaning up...\n");
        cleanup_inboxes();
        exit(EXIT_SUCCESS);
    } else if (signum == SIGINT) {
        printf("Received SIGINT, cleaning up and sending SIGTERM to other process...\n");
        cleanup_inboxes();
        kill(other_pid, SIGTERM);
        exit(EXIT_SUCCESS);
    } else if (signum == SIGUSR1) {
        // Handle incoming message from other process
        printf("Received message: %s", my_inbox);
        fflush(stdout);
        my_inbox[0] = '\0'; // Mark inbox as empty
    }
}

// Function to set up signal handlers
void setup_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART; // Restart interrupted system calls

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
}

int main(void) {
    printf("This process's ID: %ld\n", (long) getpid());
    
    // Prompt user for other process ID
    char *line = NULL; 
    size_t line_length = 0;
    do {
        printf("Enter other process ID: ");
        if (-1 == getline(&line, &line_length, stdin)) {
            perror("getline");
            abort();
        }
    } while ((other_pid = strtol(line, NULL, 10)) == 0);
    free(line);

    // Set up shared memory and signals
    setup_inboxes();
    setup_signal_handlers();

    // Main loop to read user input and send messages
    char message[BOX_SIZE];
    while (1) {
        printf("Enter a message (Ctrl+D to quit): ");
        
        if (fgets(message, BOX_SIZE, stdin) == NULL) {
            // If EOF is reached, send SIGTERM to the other process and exit
            printf("EOF detected, cleaning up...\n");
            kill(other_pid, SIGTERM);
            break;
        }

        // Copy the message to the other process's inbox
        strncpy(other_inbox, message, BOX_SIZE);

        // Notify the other process with SIGUSR1
        kill(other_pid, SIGUSR1);

        // Wait until the other process has read the message
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 10000000 }; // 10ms
        while (other_inbox[0] != '\0') {
            nanosleep(&ts, NULL);
        }
    }

    // Cleanup inboxes on normal exit
    cleanup_inboxes();
    return EXIT_SUCCESS;
}
