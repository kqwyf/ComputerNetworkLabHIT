#ifndef MESSAGE_H
#define MESSAGE_H

typedef unsigned short ushort;

#define L 15
#define SEQ_MAX_NUM (1<<L)
#define INVALID_SEQ ((SEQ_MAX_NUM) + 1)
#define DATA_MAX_SIZE (1472-sizeof(ushort)*3-sizeof(char))
#define MESSAGE_MAX_SIZE 1472

typedef struct message {
    ushort seq;
    char flags;
    ushort len;
    char *data;
} message;

#define SET_ACK(message, value) (message)->flags = ((message)->flags & ~1) | ((value) != 0)
#define SET_MORE(message, value) (message)->flags = ((message)->flags & ~2) | (((value) != 0) << 1)
#define SET_FIRST(message, value) (message)->flags = ((message)->flags & ~4) | (((value) != 0) << 2)

#define IS_ACK(message) ((message)->flags & 1)
#define HAS_MORE(message) (((message)->flags & 2) >> 1)
#define IS_FIRST(message) (((message)->flags & 4) >> 2)

message *createMessage(ushort seq, int isAck, int hasMore, int isFirst, const char *data, int len);
int writeMessageTo(const message *msg, char *buf);
message *readMessageFrom(const char *buf, int len);
void freeMessage(message *msg);

#endif
