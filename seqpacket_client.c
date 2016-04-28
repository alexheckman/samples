#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>

#include "hdr.h"

static char gbuffer[4096];

int xsocket_errno = 0;

#define SAVE_ERRNO() \
    do { \
        xsocket_errno = errno; \
    } while(0)
/*
 *  * Returns: -2 in case of socket related errors
 *   *          -1 in case of normal errors
 *    *           0 in case of timeout, signal interruption
 *     *          >0 server fd
 *      */
int xconnect(const char* socket_path, int timeout)
{
    int ret = 0;
    xsocket_errno = 0;
    int serverfd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (serverfd == -1) {
        SAVE_ERRNO();
        return -1;
    }

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, socket_path);

    long opt = fcntl(serverfd, F_GETFL, NULL);
    if (opt < 0)
        goto error_with_sock;

    if (opt ^ O_NONBLOCK) {
        opt |= O_NONBLOCK;
        if (fcntl(serverfd, F_SETFL, opt) < 0)
            goto error_with_sock;
    }

    ret = connect(serverfd, (const struct sockaddr*)&addr, sizeof(addr));
    ret = -1;
    errno = EINPROGRESS;
    if (ret == -1) {
        switch (errno) {
            case EINPROGRESS:
                {
                    struct pollfd fds[1];
                    fds[0].fd = serverfd;
                    fds[0].events = POLLOUT;

                    int rr = poll(fds, 1, timeout);
                    printf("poll %d\n", rr);

                    switch (rr) {
                        case 0:
                            {
                                close(serverfd);
                                return 0;
                            }
                        case -1:
                            {
                                if (errno != EINTR)
                                    goto error_with_sock; 

                                close(serverfd);
                                return 0;
                            }
                        default:
                            {
                                if (fds[0].revents & POLLOUT) {
                                    int valopt;
                                    int szint = sizeof(int);
                                    if (getsockopt(serverfd, SOL_SOCKET, SO_ERROR, (void*)(&valopt), &szint) < 0)
                                        goto error_with_sock;
                                    if (valopt) {
                                        xsocket_errno = valopt;
                                        close(serverfd);
                                        return -2;
                                    }
                                } else {
                                    printf("no pollout\n");
                                }
                                break;
                            }
                    }

                    break;
                }
            case EINTR:
                {
                    close(serverfd);
                    return 0;
                }
            default:
                goto error_with_sock;
        }
    }

    opt &= (~O_NONBLOCK);
    if (fcntl(serverfd, F_SETFL, opt) < 0)
        goto error_with_sock;

    return serverfd;

error_with_sock:
    SAVE_ERRNO();
    close(serverfd);
    return -1;
}

/*
 *  * Returns: -2 connection closed
 *   *          -1 in case of errors
 *    *           0 in case of timeout, signal interruption
 *     *          >0 bytes read, which should be equal to buffer_len in size
 *      */
int xrecv(int sockfd, void* buffer, int buffer_len, int timeout)
{
    xsocket_errno = 0;
    ssize_t r = 0;
    struct pollfd fds[1];
    fds[0].fd = sockfd;
    fds[0].events = POLLIN;

    int ret = poll(fds, 1, timeout);
    if (ret > 0 && fds[0].revents & POLLIN) {
        do {
            r = recv(sockfd, buffer, buffer_len, 0);
        } while(r != 0 && ((r != buffer_len) || (r == -1 && errno == EINTR)));
        if (r == 0)
            return -2;
        if (r == -1)
            goto errors;
    }
    if (ret == -1 && errno != EINTR)
        goto errors;

    return r;

errors:
    SAVE_ERRNO();
    return -1;
}

/*
 *  * Returns: -1 in case of errors
 *   *           0 in case of timeout, signal interruption
 *    *          >0 bytes written, which should be equal to buffer_len in size
 *     */
int xsend(int sockfd, void* buffer, int buffer_len, int timeout)
{
    xsocket_errno = 0;
    ssize_t w = 0;
    struct pollfd fds[1];
    fds[0].fd = sockfd;
    fds[0].events = POLLOUT;

    int ret = poll(fds, 1, timeout);
    if (ret > 0 && fds[0].revents & POLLOUT) {
        do {
            w = send(sockfd, buffer, buffer_len, 0);
        } while ((w != buffer_len) || (w == -1 && errno == EINTR));
        if (w == -1)
            goto errors;
    }
    if (ret == -1 && errno != EINTR)
        goto errors;

    return w;

errors:
    SAVE_ERRNO();
    return -1;
}


int main()
{
    struct hdr h;
    memset(&h, 0, hdr_sz);

    int serverfd = xconnect(socket_path, 5000);
    if (serverfd == -1) {
        perror("connect");
        close(serverfd);
        return -1;
    }

    int r = xrecv(serverfd, &h, hdr_sz, 2000);

    printf("windowid is %x\n", h.windowid);

    //for testing
    ssize_t sent = xsend(serverfd, &h, hdr_sz, 0);
    printf("sent %d\n", sent);
    sent = xsend(serverfd, &h, hdr_sz, 0);
    printf("sent %d\n", sent);
    sent = xsend(serverfd, &h, hdr_sz, 0);
    printf("sent %d\n", sent);

    close(serverfd);

    return 0;
}
