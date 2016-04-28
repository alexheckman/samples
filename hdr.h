#include <string.h>
#include <errno.h>

#define die(cond, ...) \
   do { \
      if ((cond)) { \
         printf(__VA_ARGS__); \
         exit(1); \
      } \
   } while(0)

#define str(s) #s
#define die_syscall(cond, msg) \
   die(cond, str(msg) " %s\n", strerror(errno))

struct hdr
{
    unsigned long windowid;
    unsigned long progress_rate;
    char msg[128];
};

const unsigned int hdr_sz = sizeof(struct hdr);

const char socket_path[] = "/tmp/seqpacket_example";
