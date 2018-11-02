#include <stdlib.h>
#include "channel.h"

extern int lostRate;
extern int bitRate;

int insertBitError(void *buf, size_t len);

ssize_t Sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen) {
    if(rand()%100 < lostRate)
        return len; // lost packet
    return sendto(sockfd, buf, len, flags, dest_addr, addrlen);
}

ssize_t Recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen) {
    ssize_t result = recvfrom(sockfd, buf, len, flags, src_addr, addrlen);
    if(result > 0 && rand()%100 < bitRate)
        insertBitError(buf, result);
    return result;
}

int insertBitError(void *buf, size_t len) {
    if(len <= 0) return 0;
    ((char*)buf)[rand()%len] ^= 1<<(rand()%8);
    return 1;
}
