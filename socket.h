#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>

#include <string.h>
#include <errno.h>

extern int xsocket_errno;

/* 
 * all timeouts are in milliseconds
*/

/*
 * Returns: -2 in case of socket related errors
 *          -1 in case of normal errors
 *           0 in case of timeout, signal interruption
 *          >0 server fd
 */
int xconnect(const char* socket_path, int timeout);

/*
 * Returns: -2 connection closed
 *          -1 in case of errors
 *           0 in case of timeout, signal interruption or nothing read
 *          >0 bytes read, which should be equal to buffer_len in size
 */
int xrecv(int sockfd, void* buffer, int buffer_len, int timeout = 0);

/*
 * Returns: -1 in case of errors
 *           0 in case of timeout, signal interruption or nothing written
 *          >0 bytes written, which should be equal to buffer_len in size
 */
int xsend(int sockfd, void* buffer, int buffer_len, int timeout = 0);
