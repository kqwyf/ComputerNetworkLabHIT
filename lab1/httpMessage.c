#include <stdio.h>
#include <string.h>
#include "httpMessage.h"

void freeField(headerField *field);
headerField *_insertField(httpMessage *message, const char *name, int m, const char *value, int n);

/*
 * Parse an HTTP message and get the host and the host port.
 */
int parseHttpMessage(const char *message, int len, httpMessage* result) {
    int err = PARSE_HOST_NOT_FOUND;
    result->header = NULL;
    result->extra = NULL;
    const char *endOfLine = strchr(message, '\r');
    if(!endOfLine) return PARSE_FORMAT_ERROR;

    // extract the first 3 fields
    int firstLen = endOfLine - message;
    strncpy(result->firstline, message, firstLen);
    result->firstline[firstLen] = '\0';
    result->infoField[0] = result->firstline;
    for(int i = 0, j = 1; j < 3 && result->firstline[i] != '\r'; i++) {
        if(result->firstline[i] == ' ') {
            result->firstline[i] = '\0';
            result->infoField[j++] = result->firstline + i + 1;
        }
    }

    // extract the header fields
    for(const char *i = endOfLine + 2; i && *i; i = endOfLine + 2) {
        endOfLine = strchr(i, '\r');
        if(endOfLine == NULL || endOfLine == i) break; // empty line
        const char *sp = strchr(i, ':');
        if(!sp) {
            err = PARSE_FORMAT_ERROR;
            break;
        }
        int nameLen = sp - i;
        for(sp++; *sp == ' '; sp++);
        int valueLen = endOfLine - sp;
        headerField *field = _insertField(result, i, nameLen, sp, valueLen);
        if(field == NULL) {
            err = PARSE_UNKNOWN_ERROR;
            break;
        }
        // extract host and port
        if(strcmp(field->name, "Host") == 0) {
            // get the port number if exists
            result->hostPort[0] = '\0';
            char *sp = strchr(field->value, ':');
            if(sp) {
                int len = strlen(sp + 1);
                if(len > 0 && len < PORT_LEN) {
                    strcpy(result->hostPort, sp + 1);
                }
                *sp = '\0';
            } else {
                sp = field->value + valueLen;
            }
            if(result->hostPort[0] == '\0') {
                strcpy(result->hostPort, "http");
            }
            // get the host
            result->host[0] = '\0';
            int len = sp - field->value;
            if(len > 0 && len < ADDR_LEN) {
                strncpy(result->host, field->value, len);
                result->host[len] = '\0';
                err = PARSE_OK;
            }
        }
    }

    // extract extra data
    if((err==PARSE_OK || err==PARSE_HOST_NOT_FOUND) && endOfLine && len > endOfLine + 2 - message) {
        int extraLen = len - (endOfLine + 2 - message);
        result->extra = (char*)malloc(sizeof(char)*(extraLen+1));
        strcpy(result->extra, endOfLine + 2);
    }
    return err;
}

char *getValueHandle(httpMessage *message, const char *name) {
    for(headerField *field = message->header; field; field = field->next)
        if(strcmp(field->name, name)==0)
            return field->value;
    return NULL;
}

void fillResponse(httpMessage *message) {
    strcpy(message->firstline, "HTTP/1.1 200 OK");
    insertField(message, "Connection", "close");
}

int writeMessageTo(httpMessage *message, char *buf) {
    int i = 0;
    for(int j = 0; message->firstline[j] && i<BUFSIZE-3; j++)
        buf[i++] = message->firstline[j];
    buf[i++] = '\r'; buf[i++] = '\n';
    for(headerField *field = message->header; field && i<BUFSIZE-3; field = field->next) {
        for(int j = 0; field->name[j] && i<BUFSIZE-3; j++)
            buf[i++] = field->name[j];
        buf[i++] = ':'; buf[i++] = ' ';
        for(int j = 0; field->value[j] && i<BUFSIZE-3; j++)
            buf[i++] = field->value[j];
        buf[i++] = '\r'; buf[i++] = '\n';
    }
    if(i<BUFSIZE-3)
        buf[i++] = '\r'; buf[i++] = '\n';
    if(message->extra != NULL) {
        int extraLen = strlen(message->extra);
        if(i + extraLen < BUFSIZE - 1)
        strcpy(buf+i, message->extra);
        return i + extraLen;
    } else {
        buf[i] = '\0';
        return i;
    }
}

void clearHttpMessage(httpMessage *message) {
    if(message->extra)
        free(message->extra);
    message->extra = NULL;
    for(headerField *field = message->header; field;) {
        headerField *next = field->next;
        freeField(field);
        field = next;
    }
    message->header = NULL;
}

void freeHttpMessage(httpMessage *message) {
    clearHttpMessage(message);
    free(message);
}

headerField *insertField(httpMessage *message, const char *name, const char *value) {
    return _insertField(message, name, strlen(name), value, strlen(value));
}

headerField *_insertField(httpMessage *message, const char *name, int m, const char *value, int n) {
    headerField *field = (headerField*)malloc(sizeof(headerField));
    if(field == NULL) return NULL;
    field->value = (char*)malloc(sizeof(char)*(n+1));
    strncpy(field->name, name, m);
    strncpy(field->value, value, n);
    field->name[m] = field->value[n] = '\0';
    field->next = message->header;
    message->header = field;
    return field;
}

BOOL removeField(httpMessage *message, const char *name) {
    for(headerField **fp = &message->header; *fp; fp = &(*fp)->next) {
        if(strcmp((*fp)->name, name) == 0) {
            headerField *field = *fp;
            *fp = field->next;
            freeField(field);
            return TRUE;
        }
    }
    return FALSE;
}

void freeField(headerField *field) {
    free(field->value);
    free(field);
}
