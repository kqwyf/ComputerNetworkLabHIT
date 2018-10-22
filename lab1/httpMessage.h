#ifndef HTTPMESSAGE_H
#define HTTPMESSAGE_H

#include <windows.h>
#include "constants.h"

#define REQUEST_METHOD(message) ((message)->infoField[0])
#define REQUEST_URL(message) ((message)->infoField[1])
#define REQUEST_VERSION(message) ((message)->infoField[2])
#define RESPONSE_VERSION(message) ((message)->infoField[0])
#define RESPONSE_STATUS(message) ((message)->infoField[1])
#define RESPONSE_DESCRIPTION(message) ((message)->infoField[2])

#define PARSE_OK 0
#define PARSE_HOST_NOT_FOUND 1
#define PARSE_FORMAT_ERROR 2
#define PARSE_UNKNOWN_ERROR -1

typedef struct headerField {
    char name[NAME_LEN];
    char *value;
    struct headerField *next;
} headerField;

typedef struct httpMessage {
    char firstline[ADDR_LEN];
    char *infoField[3];
    char host[ADDR_LEN];
    char hostPort[PORT_LEN];
    headerField *header;
    char *extra;
} httpMessage;

int parseHttpMessage(const char *message, int len, httpMessage* result);
headerField *insertField(httpMessage *message, const char *name, const char *value);
BOOL removeField(httpMessage *message, const char *name);
char *getValueHandle(httpMessage *message, const char *name);
void fillResponse(httpMessage *message);
int writeMessageTo(httpMessage *message, char *buf);
void clearHttpMessage(httpMessage *message);
void freeHttpMessage(httpMessage *message);

#endif
