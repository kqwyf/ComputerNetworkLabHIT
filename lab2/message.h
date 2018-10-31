#ifndef MESSAGE_H
#define MESSAGE_H

typedef unsigned short ushort;

#define L 15
#define SEQ_MAX_NUM (1<<L)
#define DATA_MAX_SIZE (1472-sizeof(ushort)*3-sizeof(char))
#define MESSAGE_MAX_SIZE 1472

typedef struct message {
    ushort seq;
    char flags;
    ushort len;
    char *data;
} message;

#define SET_ACK(message, value) (message)->flags = ((message)->flags & ~1) | ((value) != 0)

#define IS_ACK(message) ((message)->flags & 1)

message *createMessage(ushort seq, int isAck, const char *data, int len);
int writeMessageTo(const message *msg, char *buf);
message *readMessageFrom(const char *buf, int len);
void freeMessage(message *msg);

int insertBitError(char *buf, int len);

#endif
