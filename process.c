#include "process.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <dirent.h>

int process_errno = 0;

extern char** environ;

int run_process(const char *binary_name)
{
   int pid = fork();
   if (pid == 0) { //child
      int whoami = getuid();
      if (whoami == 0) {
         char procpath[64];
         sprintf(procpath, "/proc/%i/fd/", getpid());
         DIR* dir = opendir(procpath);
         struct dirent *de;
         int fds = 0;
         if (dir) {
            while ((de = readdir(dir)) != NULL)
               fds++;
            closedir(dir);
         }
         int ret = 0;
         for (; fds >= 0; fds--) {
            do {
               ret = close(fds);
            } while (ret == -1 && errno == EINTR);
         }
         int fd = open("/dev/null", O_RDWR);
         if (fd >= 0) {
            dup2(fd, STDIN_FILENO);
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > STDERR_FILENO)
               close(fd);
         }
      }
      char *const argv[] = { binary_name, NULL };
      //putenv("DISPLAY=:0");
      execve(binary_name, argv, environ);
      _exit(123);
   }

   if (pid == -1)
      process_errno = errno;

   return pid;
}
