#include <stdio.h>
#include <string.h>
#include "httpMessage.h"

void freeField(headerField *field);
headerField *_insertField(httpMessage *message, const char *name, int m, const char *value, int n);
const char *strnchr(const char *s, int len, const char c);

/*
 * Parse an HTTP message and get the host and the host port.
 */
int parseHttpMessage(const char *message, int len, httpMessage* result) {
    int err = PARSE_HOST_NOT_FOUND;
    result->header = NULL;
    result->extra = NULL;
    const char *endOfLine = strnchr(message, len, '\r');
    if(!endOfLine) return PARSE_FORMAT_ERROR;

    // extract the first 3 fields
    int firstLen = endOfLine - message;
    strncpy(result->firstline, message, firstLen);
    result->firstline[firstLen] = '\0';
    result->infoField[0] = result->firstline;
    for(int i = 0, j = 1; i < firstLen && j < 3 && result->firstline[i] != '\r'; i++) {
        if(result->firstline[i] == ' ') {
            result->firstline[i] = '\0';
            result->infoField[j++] = result->firstline + i + 1;
        }
    }

    // extract the header fields
    for(const char *i = endOfLine + 2; i - message <= len; i = endOfLine + 2) {
        endOfLine = strnchr(i, len - (i - message), '\r');
        if(endOfLine == NULL || endOfLine == i) break; // empty line
        const char *sp = strnchr(i, len - (i - message), ':');
        if(!sp) {
            err = PARSE_FORMAT_ERROR;
            break;
        }
        int nameLen = sp - i;
        for(sp++; *sp == ' ' && sp-message <= len; sp++);
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
        result->extraLen = len - (endOfLine + 2 - message);
        if(result->extraLen < 0) result->extraLen = 0;
        if(result->extraLen > 0) {
            result->extra = (char*)malloc(sizeof(char)*(result->extraLen));
            memcpy(result->extra, endOfLine + 2, result->extraLen);
        } else
            result->extra = NULL;
    }
    return err;
}

/*
 * Get the pointer to the value of a header field.
 */
char *getValueHandle(httpMessage *message, const char *name) {
    for(headerField *field = message->header; field; field = field->next)
        if(strcmp(field->name, name)==0)
            return field->value;
    return NULL;
}

/*
 * Set the 3 fields of the first line of an http message.
 */
int setFirstLine(httpMessage *message, const char *str1, const char *str2, const char *str3) {
    const char *(s[3]) = {str1, str2, str3};
    int len[3], totalLen = 0;
    for(int i = 0; i < 3; i++) {
        if(s[i] == NULL) s[i] = message->infoField[i];
        len[i] = strlen(s[i]);
        totalLen += len[i] + 1;
    }
    if(totalLen > ADDR_LEN) return -1;
    char *tmp = (char*)malloc(sizeof(char)*ADDR_LEN);
    for(int i = 0, totalLen = 0; i < 3; i++) {
        strcpy(tmp + totalLen, s[i]);
        totalLen += len[i] + 1;
    }
    memcpy(message->firstline, tmp, ADDR_LEN);
    for(int i = 0, totalLen = 0; i < 3; i++) {
        message->infoField[i] = message->firstline + totalLen;
        totalLen += len[i] + 1;
    }
    free(tmp);
    return 0;
}

/*
 * Write an http message to a buffer.
 */
int writeMessageTo(const httpMessage *message, char *buf) {
    int i = 0;
    // write the first line
    for(int j = 0; j < 3 && i < BUFSIZE-3; j++) {
        for(int k = 0; message->infoField[j][k] && i < BUFSIZE-3; k++)
            buf[i++] = message->infoField[j][k];
        if(j < 2) buf[i++] = ' ';
    }
    buf[i++] = '\r'; buf[i++] = '\n';
    // write the headers
    for(headerField *field = message->header; field && i<BUFSIZE-3; field = field->next) {
        for(int j = 0; field->name[j] && i<BUFSIZE-3; j++)
            buf[i++] = field->name[j];
        buf[i++] = ':'; buf[i++] = ' ';
        for(int j = 0; field->value[j] && i<BUFSIZE-3; j++)
            buf[i++] = field->value[j];
        buf[i++] = '\r'; buf[i++] = '\n';
    }
    if(i < BUFSIZE-3)
        buf[i++] = '\r'; buf[i++] = '\n';
    // write the extra data
    if(message->extra != NULL) {
        if(i + message->extraLen < BUFSIZE)
        memcpy(buf+i, message->extra, message->extraLen);
        return i + message->extraLen;
    } else {
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

/*
 * Insert a field with a name and a value to an http message.
 */
headerField *insertField(httpMessage *message, const char *name, const char *value) {
    return _insertField(message, name, strlen(name), value, strlen(value));
}

/*
 * Find a char in a given string with length len.
 */
const char *strnchr(const char *s, int len, const char c) {
    if(s == NULL) return NULL;
    for(int i = 0; i < len; i++)
        if(s[i] == c) return s + i;
    return NULL;
}

headerField *_insertField(httpMessage *message, const char *name, int m, const char *value, int n) {
    headerField *field = (headerField*)malloc(sizeof(headerField));
    if(field == NULL || m >= NAME_LEN) return NULL;
    field->value = (char*)malloc(sizeof(char)*(n+1));
    strncpy(field->name, name, m);
    strncpy(field->value, value, n);
    field->name[m] = field->value[n] = '\0';
    field->next = message->header;
    message->header = field;
    return field;
}

/*
 * Remove a field by its name from an http message.
 */
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

