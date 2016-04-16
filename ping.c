#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

void hexdump(void *ptr, int buflen) {
    unsigned char *buf = (unsigned char*)ptr;
    int i, j;
    for (i=0; i<buflen; i+=16) {
        printf("%06x: ", i);
        for (j=0; j<16; j++) 
            if (i+j < buflen)
                printf("%02x ", buf[i+j]);
            else
                printf("   ");
        printf(" ");
        for (j=0; j<16; j++) 
            if (i+j < buflen)
                printf("%c", isprint(buf[i+j]) ? buf[i+j] : '.');
        printf("\n");
    }
}

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

#define MAX_OPTIONS_SIZE 60 //options size = 15x32 bits = 60 bytes
#define _BUF_SIZE  sizeof(struct iphdr) + MAX_OPTIONS_SIZE + sizeof(struct icmp)
#define BUF_SIZE _BUF_SIZE + 2048 //give more room junst in case, either way we should be under a page size
static char gbuffer[BUF_SIZE];

const uint16_t identifier = 0x2BAD;

const uint8_t icmp_echo_hdr_size = 8; //bytes

/*
 * As described in https://en.wikipedia.org/wiki/Ping_%28networking_utility%29
 * It is the 16-bit one's complement of the one's complement sum of the ICMP message starting with the Type field
 */
uint16_t checksum(uint16_t *data, uint16_t len)
{
    uint16_t i = 0;
    uint32_t sum = 0;
    for (; i < len; i += 2)
        sum += *(data + i);

    if ((i - len)== 1) //odd len
        sum += *((uint8_t*)(data + i - 2));

    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);

    return (uint16_t)~sum;
}

/*
 * Minimalistic ICMP echo request/reply to test if destionation is alive
 */
int ping4(const char* destination)
{
    int errors = 0;
    uint16_t seq_number = 0;
    int ret = 0;

    struct iphdr* ip_pkt;;
    struct icmp* icmp_pkt;

    struct sockaddr sa[2];
    memset(sa, 0, 2 * sizeof(struct sockaddr));
    struct sockaddr_in *to = (struct sockaddr_in*)&sa[0];
    struct sockaddr_in *from = (struct sockaddr_in*)&sa[1];

    int pingsock = socket(AF_INET, SOCK_RAW, 0x01 /*ICMP proto as per /etc/protocols*/);
    die_syscall(pingsock == -1, socket);

    from->sin_family = AF_INET;
    from->sin_addr.s_addr = INADDR_ANY;
    ret = bind(pingsock, (struct sockaddr*)from, sizeof(struct sockaddr));
    die_syscall(ret == -1, bind);

    //recv timeout on socket in case socket is the other end is dead
    struct timeval tv = {1,0};
    socklen_t tlen = sizeof(struct timeval);
    ret = setsockopt(pingsock, SOL_SOCKET, SO_RCVTIMEO, &tv, tlen);
    die_syscall(ret == -1, setsockopt);

    icmp_pkt = (struct icmp*)gbuffer;
    memset(icmp_pkt, 0, sizeof(struct icmp));
    icmp_pkt->icmp_type = ICMP_ECHO;
    icmp_pkt->icmp_cksum = 0; //checksum is computed with initial checksum 0
    icmp_pkt->icmp_seq = htons(seq_number);
    icmp_pkt->icmp_id = identifier;
    icmp_pkt->icmp_cksum = checksum((uint16_t*)icmp_pkt, icmp_echo_hdr_size);

    to->sin_family = AF_INET;
    inet_aton(destination, &to->sin_addr);
    do {
        ret = sendto(pingsock, icmp_pkt, icmp_echo_hdr_size, 0, (const struct sockaddr*)to, sizeof(struct sockaddr));
    } while (ret == -1 && errno == EINVAL);

    if (ret == icmp_echo_hdr_size) {
        printf("sent %d bytes\n", ret);
        socklen_t addrlen = sizeof(struct sockaddr);
        ret = recvfrom(pingsock, gbuffer, BUF_SIZE, 0, (struct sockaddr*)from, &addrlen);
        if (ret == -1) {
            if (errno == EAGAIN) {
                printf("#\tNo reply from the other side!\n");
                errors = 1; //timeout
            } else {
                errors = ret; //generic errors 
            }
            goto cleanup;
        } else {
            printf("received %d bytes, errno %d\n", ret, errno);
            int hlen = 0;
            ip_pkt = (struct iphdr*)gbuffer;
            hlen = ip_pkt->ihl << 2; //IHL = internet header length = number of 32-bit words in the header <==> ihl*4 bytes <=> left-shift 2
            icmp_pkt = (struct icmp*)(gbuffer + hlen);
            // for an echo request to be successful, the reply type and code must be 0.
            if (icmp_pkt->icmp_type == ICMP_ECHOREPLY && icmp_pkt->icmp_code == 0 && icmp_pkt->icmp_id == identifier)
                printf("it's alive!\n");
        }
    } else {
        int saved_errno = errno;
        printf("#\tFailed to send echo request!\n");
        if (ret == -1) {
            printf(strerror(saved_errno));
            errors = ret;
            goto cleanup;
        }
    }

cleanup:
    close(pingsock);
    return errors;
}

int main(int argc, char* argv[])
{
    int ret = ping4(argv[1]);
    //getc(stdin);;
    exit(ret);
}
