#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <syslog.h>
#include <fcntl.h>
#include "queue.h"   /* BSD SLIST macros */

// Add build switch to read from char driver instead of file. This is for testing the char driver.
#ifndef USE_AESD_CHAR_DEVICE
#define USE_AESD_CHAR_DEVICE 1
#endif

#if USE_AESD_CHAR_DEVICE
#define DATA_FILE "/dev/aesdchar"
#else
#define DATA_FILE "/var/tmp/aesdsocketdata"
#endif

/* ── Configuration ─────────────────────────────────────────────────────── */
#define PORT            "9000"
#define BACKLOG         10
#define BUFFER_SIZE     1024
#define TIMESTAMP_INTERVAL_S  10

/* ── Globals ────────────────────────────────────────────────────────────── */
static volatile sig_atomic_t exit_requested = 0;

/* Single mutex guards ALL writes to DATA_FILE (socket data + timestamps). */
static pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── Per-connection thread data ─────────────────────────────────────────── */
typedef struct thread_data_s {
    pthread_t       thread_id;
    int             client_fd;
    int             thread_complete;   /* set to 1 by the thread before exit */
    char            client_ip[INET6_ADDRSTRLEN];
} thread_data_t;

/* ── SLIST node ─────────────────────────────────────────────────────────── */
typedef struct slist_node_s slist_node_t;
struct slist_node_s {
    thread_data_t              *data;
    SLIST_ENTRY(slist_node_s)   entries;
};

SLIST_HEAD(thread_list_head_s, slist_node_s) thread_list_head; 

/*
 * Returns 1 if port 9000 has any socket in TIME_WAIT state.
 * Reads /proc/net/tcp — local_address field is hex "IP:PORT" little-endian.
 * Port 9000 decimal = 0x2328, printed as "2328" in /proc/net/tcp.
 * State 06 = TCP_TIME_WAIT.
 */
int port_in_time_wait(int port)
{
    FILE *f = fopen("/proc/net/tcp", "r");
    if (!f) return 0;

    char line[256];
    fgets(line, sizeof(line), f);   /* skip header */

    while (fgets(line, sizeof(line), f)) {
        unsigned local_addr, local_port, state;
        /* Format: "  sl  local_address rem_address st ..." */
        if (sscanf(line, " %*d: %x:%x %*x:%*x %x", 
                   &local_addr, &local_port, &state) == 3) {
            if ((int)local_port == port && state == 0x06) {
                fclose(f);
                return 1;   /* TIME_WAIT found */
            }
        }
    }

    fclose(f);
    return 0;
}

/* ── Helpers ────────────────────────────────────────────────────────────── */
static void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET)
        return &(((struct sockaddr_in  *)sa)->sin_addr);
    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

/* ── Signal handler ─────────────────────────────────────────────────────── */
static void signal_handler(int signo)
{
    (void)signo;
    exit_requested = 1;
}

/* ── Connection thread ──────────────────────────────────────────────────── */
/*
 * Flow (right-hand side of the flowchart):
 *   Receive on socket  →  (mutex) write to file  →  send file back  →
 *   set complete flag and exit
 */
static void *connection_thread(void *arg)
{
    thread_data_t *td = (thread_data_t *)arg;
    int   client_fd   = td->client_fd;

    char   buf[BUFFER_SIZE];
    char  *packet      = NULL;
    size_t packet_size = 0;
    ssize_t bytes;

    /* ── 1. Receive until newline ── */
    while ((bytes = recv(client_fd, buf, BUFFER_SIZE, 0)) > 0) {
        char *tmp = realloc(packet, packet_size + (size_t)bytes);
        if (!tmp) {
            syslog(LOG_ERR, "realloc failed");
            free(packet);
            goto done;
        }
        packet = tmp;
        memcpy(packet + packet_size, buf, (size_t)bytes);
        packet_size += (size_t)bytes;

        if (memchr(buf, '\n', (size_t)bytes))
            break;
    }

    if (bytes == -1) {
        syslog(LOG_ERR, "recv error: %s", strerror(errno));
        free(packet);
        goto done;
    }

    /* ── 2. Write packet to file (mutex protected) ── */
    pthread_mutex_lock(&file_mutex);

    int fd = open(DATA_FILE, O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (fd == -1) {
        syslog(LOG_ERR, "open for write failed: %s", strerror(errno));
        pthread_mutex_unlock(&file_mutex);
        free(packet);
        goto done;
    }

    if (write(fd, packet, packet_size) == -1)
        syslog(LOG_ERR, "write failed: %s", strerror(errno));

    close(fd);
    pthread_mutex_unlock(&file_mutex);
    free(packet);
    packet = NULL;

    /* ── 3. Send entire file back to client ── */
    /*
     * We lock again while reading so that a concurrent write (another thread
     * or the timer) cannot interleave mid-read and send a torn snapshot.
     */
    pthread_mutex_lock(&file_mutex);

    fd = open(DATA_FILE, O_RDONLY);
    if (fd == -1) {
        syslog(LOG_ERR, "open for read failed: %s", strerror(errno));
        pthread_mutex_unlock(&file_mutex);
        goto done;
    }

    while ((bytes = read(fd, buf, BUFFER_SIZE)) > 0) {
        if (send(client_fd, buf, (size_t)bytes, 0) == -1) {
            syslog(LOG_ERR, "send failed: %s", strerror(errno));
            break;
        }
    }

    close(fd);
    pthread_mutex_unlock(&file_mutex);

done:
    close(client_fd);
    syslog(LOG_INFO, "Closed connection from %s", td->client_ip);

    /* ── 4. Set complete flag and exit (flowchart: "set complete flag and exit") ── */
    td->thread_complete = 1;
    return NULL;
}

/* ── Timer thread ───────────────────────────────────────────────────────── */
/*
 * Appends "timestamp:<RFC 2822 time>\n" to DATA_FILE every 10 seconds.
 * Initialised in the parent process (before or after bind — before the
 * accept loop so the very first tick fires at t=10 s from server start).
 */
static void *timer_thread(void *arg)
{
    (void)arg;

    while (!exit_requested) {
        sleep(TIMESTAMP_INTERVAL_S);

        if (exit_requested)
            break;

        /* Build RFC 2822 timestamp string */
        time_t     now = time(NULL);
        struct tm  tm_info;
        char       ts_buf[128];

        localtime_r(&now, &tm_info);
        /* RFC 2822 example: "timestamp:Mon, 15 May 2023 14:30:00 +0000\n" */
        size_t ts_len = strftime(ts_buf, sizeof(ts_buf),
                                 "timestamp:%a, %d %b %Y %T %z\n", &tm_info);

        if (ts_len == 0) {
            syslog(LOG_ERR, "strftime failed");
            continue;
        }

        /* Write atomically with respect to socket data writes */
        pthread_mutex_lock(&file_mutex);

        int fd = open(DATA_FILE, O_CREAT | O_WRONLY | O_APPEND, 0644);
        if (fd == -1) {
            syslog(LOG_ERR, "timer: open failed: %s", strerror(errno));
            pthread_mutex_unlock(&file_mutex);
            continue;
        }

        if (write(fd, ts_buf, ts_len) == -1)
            syslog(LOG_ERR, "timer: write failed: %s", strerror(errno));

        close(fd);
        pthread_mutex_unlock(&file_mutex);

        syslog(LOG_DEBUG, "Timer wrote: %.*s", (int)(ts_len - 1), ts_buf);
    }

    return NULL;
}

/* ── join_completed_threads ─────────────────────────────────────────────── */
/*
 * Called from the main thread each iteration of the accept loop.
 * Walks the SLIST, joins any node whose thread_complete flag is set,
 * frees its resources, and removes it from the list.
 *
 * Only the main thread touches the SLIST (lecture note: "linked list
 * implementation is not thread safe — best to only access from main process
 * thread").
 */
static void join_completed_threads(void)
{
    slist_node_t *node, *tmp;

    SLIST_FOREACH_SAFE(node, &thread_list_head, entries, tmp) {
        if (node->data->thread_complete) {
            pthread_join(node->data->thread_id, NULL);
            syslog(LOG_DEBUG, "Joined thread for %s",
                   node->data->client_ip);
            SLIST_REMOVE(&thread_list_head, node, slist_node_s, entries);
            free(node->data);
            free(node);
        }
    }
}

/* ── main ───────────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    int daemon_mode = (argc == 2 && strcmp(argv[1], "-d") == 0);
    SLIST_INIT(&thread_list_head);
    openlog("aesdsocket", 0, LOG_USER);

    /* ── Signal setup ── */
    struct sigaction sa_term = {0};
    sa_term.sa_handler = signal_handler;
    sigemptyset(&sa_term.sa_mask);
    sa_term.sa_flags = 0;          /* do NOT set SA_RESTART so accept() wakes */
    sigaction(SIGTERM, &sa_term, NULL);
    sigaction(SIGINT,  &sa_term, NULL);

    /* ── Socket setup ── */
    struct addrinfo hints = {0};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;

    struct addrinfo *servinfo = NULL;
    if (getaddrinfo(NULL, PORT, &hints, &servinfo) != 0) {
        syslog(LOG_ERR, "getaddrinfo failed");
        return 1;
    }

    while (port_in_time_wait(9000)) {
        syslog(LOG_WARNING, "Port 9000 in TIME_WAIT, waiting...");
        sleep(1);
    }

    int sockfd = -1;
    int yes = 1;
    struct addrinfo *p;
    
    for (p = servinfo; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) continue;

        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof yes);

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            continue;
        }
        break;
    }
    freeaddrinfo(servinfo);

    if (p == NULL) {
        syslog(LOG_ERR, "bind failed");
        return 1;
    }

    if (listen(sockfd, BACKLOG) == -1) {
        syslog(LOG_ERR, "listen failed: %s", strerror(errno));
        return 1;
    }

    /* ── Daemonise (after bind so port errors are still visible) ── */
    if (daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) { syslog(LOG_ERR, "fork failed"); return 1; }
        if (pid > 0) return 0;          /* parent exits */

        setsid();
        chdir("/");
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }

#if !USE_AESD_CHAR_DEVICE
    /* ── Start timer thread (in parent / daemon process) ── */
    pthread_t timer_tid;
    if (pthread_create(&timer_tid, NULL, timer_thread, NULL) != 0) {
        syslog(LOG_ERR, "pthread_create timer failed");
        close(sockfd);
        return 1;
    }
#endif

    syslog(LOG_INFO, "Server listening on port %s", PORT);

    /* ════════════════════════════════════════════════════════════════════
     * Main accept loop
     *  1. accept()
     *  2. create_thread  (pthread_create)
     *  3. plug node into SLIST
     *  4. walk SLIST → join completed threads
     * ═══════════════════════════════════════════════════════════════════ */
    while (!exit_requested) {

        /* ── (flowchart) For threads in list … check complete flag ── */
        join_completed_threads();

        /* ── (flowchart) Accept connection ── */
        struct sockaddr_storage their_addr;
        socklen_t sin_size = sizeof their_addr;

        int new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) {
            if (errno == EINTR && exit_requested)
                break;
            if (errno != EINTR)
                syslog(LOG_ERR, "accept error: %s", strerror(errno));
            continue;
        }

        /* Allocate per-thread data */
        thread_data_t *td = calloc(1, sizeof(thread_data_t));
        if (!td) {
            syslog(LOG_ERR, "calloc thread_data failed");
            close(new_fd);
            continue;
        }

        td->client_fd      = new_fd;
        td->thread_complete = 0;
        inet_ntop(their_addr.ss_family,
                  get_in_addr((struct sockaddr *)&their_addr),
                  td->client_ip, sizeof td->client_ip);

        syslog(LOG_INFO, "Accepted connection from %s", td->client_ip);

        /* ── (flowchart) create_thread ── */
        if (pthread_create(&td->thread_id, NULL, connection_thread, td) != 0) {
            syslog(LOG_ERR, "pthread_create failed: %s", strerror(errno));
            close(new_fd);
            free(td);
            continue;
        }

        /* ── (flowchart) Plug in lamp — insert node into SLIST ── */
        slist_node_t *node = malloc(sizeof(slist_node_t));
        if (!node) {
            syslog(LOG_ERR, "malloc slist_node failed");
            /* thread is already running; mark for cleanup next iteration */
        } else {
            node->data = td;
            SLIST_INSERT_HEAD(&thread_list_head, node, entries);
        }
    }

    /* ── Graceful shutdown ── */
    syslog(LOG_INFO, "Shutdown signal received — waiting for threads");

#if !USE_AESD_CHAR_DEVICE
    /* Signal & join the timer thread */
    pthread_join(timer_tid, NULL);
#endif

    /* Join all remaining connection threads */
    slist_node_t *node, *tmp;
    SLIST_FOREACH_SAFE(node, &thread_list_head, entries, tmp) {
        pthread_join(node->data->thread_id, NULL);
        close(node->data->client_fd);   /* in case thread didn't close it */
        free(node->data);
        free(node);
    }

    close(sockfd);
#if !USE_AESD_CHAR_DEVICE
    remove(DATA_FILE);
    //unlink(DATA_FILE);
#endif
    pthread_mutex_destroy(&file_mutex);

    syslog(LOG_INFO, "Server shut down cleanly");
    closelog();
    return 0;
}
