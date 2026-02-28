#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <syslog.h>
#include <signal.h>

#define PORT 9000
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BACKLOG 5
#define RECV_BUFFER 1024

static volatile sig_atomic_t exit_requested = 0;

void signal_handler(int signo)
{
    syslog(LOG_INFO, "Caught signal, exiting");
    exit_requested = 1;
}

int main()
{
    int server_fd = -1;
    int client_fd = -1;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len;
    char recv_buffer[RECV_BUFFER];

    openlog("aesdsocket", LOG_PID, LOG_USER);

    // Register signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        syslog(LOG_ERR, "Socket creation failed: %s", strerror(errno));
        return -1;
    }

    int optval = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        syslog(LOG_ERR, "Bind failed: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    // Listen
    if (listen(server_fd, BACKLOG) == -1) {
        syslog(LOG_ERR, "Listen failed: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    while (!exit_requested) {

        client_addr_len = sizeof(client_addr);
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);

        if (client_fd == -1) {
            if (exit_requested)
                break;
            syslog(LOG_ERR, "Accept failed: %s", strerror(errno));
            close(server_fd);
            return -1;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        char *packet_buffer = NULL;
        size_t packet_size = 0;
        ssize_t bytes_received;

        // Receive until newline
        while ((bytes_received = recv(client_fd, recv_buffer, RECV_BUFFER, 0)) > 0) {

            char *temp = realloc(packet_buffer, packet_size + bytes_received);
            if (!temp) {
                syslog(LOG_ERR, "Memory allocation failed");
                free(packet_buffer);
                close(client_fd);
                close(server_fd);
                return -1;
            }

            packet_buffer = temp;
            memcpy(packet_buffer + packet_size, recv_buffer, bytes_received);
            packet_size += bytes_received;

            if (memchr(packet_buffer, '\n', packet_size)) {
                break;
            }
        }

        if (bytes_received == -1) {
            syslog(LOG_ERR, "Receive failed: %s", strerror(errno));
            free(packet_buffer);
            close(client_fd);
            close(server_fd);
            return -1;
        }

        // Append packet to file
        if (packet_buffer) {
            int file_fd = open(DATA_FILE, O_CREAT | O_WRONLY | O_APPEND, 0644);
            if (file_fd == -1) {
                syslog(LOG_ERR, "File open failed: %s", strerror(errno));
                free(packet_buffer);
                close(client_fd);
                close(server_fd);
                return -1;
            }

            if (write(file_fd, packet_buffer, packet_size) == -1) {
                syslog(LOG_ERR, "File write failed: %s", strerror(errno));
                free(packet_buffer);
                close(file_fd);
                close(client_fd);
                close(server_fd);
                return -1;
            }

            close(file_fd);
            free(packet_buffer);
        }

        // Send entire file back to client
        int file_fd = open(DATA_FILE, O_RDONLY);
        if (file_fd == -1) {
            syslog(LOG_ERR, "File reopen failed: %s", strerror(errno));
            close(client_fd);
            close(server_fd);
            return -1;
        }

        while ((bytes_received = read(file_fd, recv_buffer, RECV_BUFFER)) > 0) {
            if (send(client_fd, recv_buffer, bytes_received, 0) == -1) {
                syslog(LOG_ERR, "Send failed: %s", strerror(errno));
                close(file_fd);
                close(client_fd);
                close(server_fd);
                return -1;
            }
        }

        close(file_fd);

        syslog(LOG_INFO, "Closed connection from %s", client_ip);

        close(client_fd);
    }

    // Graceful shutdown
    close(server_fd);
    unlink(DATA_FILE);
    closelog();

    return 0;
}