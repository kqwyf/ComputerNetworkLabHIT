#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "message.h"

typedef unsigned long ulong;

ushort checksum(const char *buf, int len);

message *createMessage(ushort seq, int isAck, int hasMore, int isFirst, const char *data, int len) {
    if(len < 0 || (unsigned long)len >= DATA_MAX_SIZE)
        return NULL;
    message *msg = (message*) malloc(sizeof(message));
    if(msg == NULL) return NULL;
    msg->seq = seq;
    SET_ACK(msg, isAck);
    SET_MORE(msg, hasMore);
    SET_FIRST(msg, isFirst);
    msg->len = (ushort)len;
    if(len == 0 || data == NULL)
        msg->data = NULL;
    else {
        msg->data = (char*) malloc(sizeof(char)*len);
        strncpy(msg->data, data, len);
    }
    return msg;
}

int writeMessageTo(const message *msg, char *buf) {
    int len = 0;

    for(ulong i = 0; i < sizeof(msg->seq); i++)
        buf[len++] = ((char*)&msg->seq)[i];

    for(ulong i = 0; i < sizeof(msg->flags); i++)
        buf[len++] = ((char*)&msg->flags)[i];

    for(ulong i = 0; i < sizeof(msg->len); i++)
        buf[len++] = ((char*)&msg->len)[i];

    if(msg->data)
        strncpy(buf+len, msg->data, msg->len);
    len += msg->len;

    ushort chksum = checksum(buf, len);
    for(ulong i = 0; i < sizeof(chksum); i++)
        buf[len++] = ((char*)&chksum)[i];
    return len;
}

message *readMessageFrom(const char *buf, int len) {
    message *msg = (message*) malloc(sizeof(message));
    if(msg == NULL) return NULL;
    int idx = 0;

    if(idx + (int)sizeof(msg->seq) > len) goto err;
    for(ulong i = 0; i < sizeof(msg->seq) && idx <= len; i++)
        ((char*)&msg->seq)[i] = buf[idx++];

    if(idx + (int)sizeof(msg->flags) > len) goto err;
    for(ulong i = 0; i < sizeof(msg->flags) && idx <= len; i++)
        ((char*)&msg->flags)[i] = buf[idx++];

    if(idx + (int)sizeof(msg->len) > len) goto err;
    for(ulong i = 0; i < sizeof(msg->len) && idx <= len; i++)
        ((char*)&msg->len)[i] = buf[idx++];

    if(idx + msg->len > len) goto err;
    if(msg->len > 0) {
        msg->data = (char*) malloc(sizeof(char)*msg->len);
        strncpy(msg->data, buf+idx, msg->len);
    } else
        msg->data = NULL;
    idx += msg->len;

    ushort chksum;
    if(idx + (int)sizeof(chksum) > len) goto err;
    for(ulong i = 0; i < sizeof(chksum); i++)
        ((char*)&chksum)[i] = buf[idx++];
    if(checksum(buf, len)) goto err;
    return msg;
err:
    freeMessage(msg);
    return NULL;
}

ushort checksum(const char *buf, int len) {
    typedef unsigned char uchar;
    const unsigned int inc = 1<<16;
    unsigned int sum = 0;
    if(len % 2 != 0)
        sum = (uchar)buf[0];
    for(int i = len%2; i < len; i += 2) {
        sum += (((ushort)(uchar)buf[i+1])<<8) + (uchar)buf[i];
        if(sum & inc)
            sum = (sum & (inc-1)) + 1;
    }
    return ~(ushort)sum;
}

void freeMessage(message *msg) {
    if(msg->data)
        free(msg->data);
    free(msg);
}
