#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>

#include "hdr.h"

typedef enum
{
    FIRST_STAGE,
    SECOND_STAGE
} stage;

static char gbuffer[4096];

volatile int stop = 0;

void signal_handler(int signo)
{
    if (signo == SIGINT)
        stop = 1;
}

/*
 * return a valid unix socket descriptor
 */
static int init_socket()
{
    int ret, serverfd;
    serverfd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (ret == -1) {
        perror("socket");
        return -1;
    }
    
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, socket_path);

    unlink(socket_path);
    ret = bind(serverfd, (const struct sockaddr*)&addr, sizeof(addr));
    if (ret == -1) {
        perror("bind");
        return -1;
    }

    ret = listen(serverfd, 3);
    if (ret == -1) {
        perror("bind");
        return -1;
    }

    return serverfd;
}

static int accept_client(int serverfd)
{
    int clientfd = -1;
    int ret = 0;
    struct pollfd fds[1];
    memset(fds, 0, sizeof(struct pollfd));
    fds[0].fd = serverfd;
    fds[0].events = POLLIN;

    struct sockaddr client;
    socklen_t client_len = sizeof(struct sockaddr);
    ret = poll(fds, 1, 0);
    if (ret == -1 && errno != EINTR) {
        perror("poll");
        return -1;
    }
    if (ret > 0 && fds[0].revents & POLLIN) {
        clientfd = accept(serverfd, &client, &client_len);
        if (clientfd == -1) {
            perror("accept");
            return -1;
        }

        struct hdr *h = (struct hdr*)gbuffer;
        memset(h, 0, hdr_sz);
        h->windowid = 0xABCDEF;
        int attempts = 10;
        ssize_t sent = 0;
        do {
            //force send windowid messages for attempts times in case previous calls failed
            sent = send(clientfd, gbuffer, hdr_sz, 0);
            if (sent == hdr_sz)
                printf("written %d\n", sent);
        } while ((sent != hdr_sz) || (sent == -1 && errno == EINTR));
    }

    return clientfd;
}

static void read_messages(int clientfd)
{
    int ret = 0;

    struct pollfd fds[1];
    memset(fds, 0, sizeof(struct pollfd));
    fds[0].fd = clientfd;
    fds[0].events = POLLIN;

    int available = 0;
    do {
        available = 0;
        //read everything that is on this socket
        ret = poll(fds, 1, 0);
        if (ret == -1 && errno != EINTR) {
            perror("poll");
            return -1;
        }
        if (ret > 0 && fds[0].revents & POLLIN) {
            ssize_t r = recv(clientfd, gbuffer, hdr_sz, MSG_DONTWAIT);
            if (r > 0) {
                available = 1;
                printf("read_message of size: %d\n", r);
                //cast gbuffer and do something
            }
        }
    } while(available);
}


int main()
{
    signal(SIGINT, signal_handler);

    stage current_stage = FIRST_STAGE;

    int serverfd = init_socket();
    int clientfd = -1;

    printf("header size %d\n", hdr_sz);

    while (!stop) { //this will be removed in the GTK swupdate GUI
        usleep(100000); //this will be removed in the GTK swupdate GUI
        switch (current_stage) {
            case FIRST_STAGE:
            {
                clientfd = accept_client(serverfd);
                if (clientfd == -1) {
                    continue;
                }
                current_stage = SECOND_STAGE;
                break;
            }
            case SECOND_STAGE:
            {
                read_messages(clientfd);
                close(clientfd);
                current_stage = FIRST_STAGE; //loop-back
                break;
            }
        }
    }

    printf("Done...\n");
    close(serverfd);
    unlink(socket_path);

    return 0;
}
