#include "socket.h"

#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>

#include <string.h>
#include <errno.h>
#include <poll.h>
#include <fcntl.h>

int xsocket_errno = 0;

#define SAVE_ERRNO() \
   do { \
      xsocket_errno = errno; \
   } while(0)


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
   memset(&addr, 0, sizeof(addr));
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
   if (ret == -1) {
      switch (errno) {
         case EINPROGRESS:
         {
            struct pollfd fds[1];
            fds[0].fd = serverfd;
            fds[0].events = POLLOUT;
            
            int rr = poll(fds, 1, timeout);
            switch (rr) {
               case 0:
               {
                  //timeout
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
                     socklen_t szint = sizeof(int);
                     if (getsockopt(serverfd, SOL_SOCKET, SO_ERROR, (void*)(&valopt), &szint) < 0)
                        goto error_with_sock;
                     if (valopt) {
                        xsocket_errno = valopt;
                        close(serverfd);
                        return -2;
                     }
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
