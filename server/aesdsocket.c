

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/wait.h>
#include <syslog.h>
#include <fcntl.h>

#define PORT "9000"
#define BACKLOG 10
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BUFFER 1024

volatile sig_atomic_t exit_requested = 0;

void signal_handler(int signo)
{
    exit_requested = 1;
}

void sigchld_handler(int s)
{
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET)
        return &(((struct sockaddr_in*)sa)->sin_addr);

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void handle_client(int client_fd)
{
    char buffer[BUFFER];
    char *packet = NULL;
    size_t packet_size = 0;
    ssize_t bytes;

    while((bytes = recv(client_fd, buffer, BUFFER, 0)) > 0)
    {
        char *tmp = realloc(packet, packet_size + bytes);
        if(!tmp)
        {
            syslog(LOG_ERR,"realloc failed");
            free(packet);
            return;
        }

        packet = tmp;
        memcpy(packet + packet_size, buffer, bytes);
        packet_size += bytes;

        if(memchr(buffer,'\n',bytes))
            break;
    }

    if(bytes == -1)
    {
        syslog(LOG_ERR,"recv error");
        free(packet);
        return;
    }

    int fd = open(DATA_FILE,O_CREAT|O_WRONLY|O_APPEND,0644);
    if(fd == -1)
    {
        syslog(LOG_ERR,"file open failed");
        free(packet);
        return;
    }

    write(fd,packet,packet_size);
    close(fd);
    free(packet);

    fd = open(DATA_FILE,O_RDONLY);
    if(fd == -1)
        return;

    while((bytes = read(fd,buffer,BUFFER)) > 0)
        send(client_fd,buffer,bytes,0);

    close(fd);
}

int main(int argc,char *argv[])
{
    int sockfd,new_fd;
    struct addrinfo hints,*servinfo,*p;
    struct sockaddr_storage their_addr;
    socklen_t sin_size;
    int yes=1;
    char s[INET6_ADDRSTRLEN];

    int daemon_mode = 0;
    if(argc == 2 && strcmp(argv[1],"-d")==0)
        daemon_mode = 1;

    openlog("aesdsocket",0,LOG_USER);

    struct sigaction sa_term;
    sa_term.sa_handler = signal_handler;
    sigemptyset(&sa_term.sa_mask);
    sa_term.sa_flags = 0;
    sigaction(SIGTERM,&sa_term,NULL);
    sigaction(SIGINT,&sa_term,NULL);

    struct sigaction sa_child;
    sa_child.sa_handler = sigchld_handler;
    sigemptyset(&sa_child.sa_mask);
    sa_child.sa_flags = SA_RESTART;
    sigaction(SIGCHLD,&sa_child,NULL);

    memset(&hints,0,sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    getaddrinfo(NULL,PORT,&hints,&servinfo);

    for(p=servinfo;p!=NULL;p=p->ai_next)
    {
        sockfd = socket(p->ai_family,p->ai_socktype,p->ai_protocol);
        if(sockfd == -1)
            continue;

        setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof yes);

        if(bind(sockfd,p->ai_addr,p->ai_addrlen)==-1)
        {
            close(sockfd);
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo);

    if(p==NULL)
    {
        syslog(LOG_ERR,"bind failed");
        exit(1);
    }

    if(listen(sockfd,BACKLOG)==-1)
    {
        syslog(LOG_ERR,"listen failed");
        exit(1);
    }

    if(daemon_mode)
    {
        pid_t pid = fork();
        if(pid>0)
            exit(EXIT_SUCCESS);

        setsid();
        chdir("/");
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }

    while(!exit_requested)
    {
        sin_size = sizeof their_addr;

        new_fd = accept(sockfd,(struct sockaddr*)&their_addr,&sin_size);

        if(new_fd == -1)
        {
            if(errno == EINTR && exit_requested)
                break;

            continue;
        }

        inet_ntop(their_addr.ss_family,
                  get_in_addr((struct sockaddr*)&their_addr),
                  s,sizeof s);

        syslog(LOG_INFO,"Accepted connection from %s",s);

        pid_t pid = fork();

        if(pid==0)
        {
            close(sockfd);
            handle_client(new_fd);
            close(new_fd);
            exit(0);
        }

        close(new_fd);
    }

    close(sockfd);
    remove(DATA_FILE);

    syslog(LOG_INFO,"Server shutting down");
    closelog();

    return 0;
}
