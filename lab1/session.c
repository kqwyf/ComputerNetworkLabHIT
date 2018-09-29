#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "session.h"

const char *CONFIG_FILE = "config.ini";
const int BUFSIZE = 65536;
const short HTTP_PORT = 80;

typedef struct httpMessage {
    unsigned hostIp;
    short hostPort;
    char host[128];
    int bodyOffset;
    char *message;
} httpMessage;

int parseHttpMessage(char *message, int len, httpMessage *result);
SOCKET connectToServer(httpMessage *message);
int process(char *buf, int len);
int closeSocket(SOCKET sd);
int loadConfig();
void freeSiteRecords();
void freeUserRecords();
void freeRedirectRecords();

unsigned __stdcall threadMain(void *context) {
    int err;

    // initialize
    threadInfo *info = (threadInfo*)context;
    printf("Created a new thread: %d.\n", info->td);

    // receive message from client
    char *buf = (char*)calloc(BUFSIZE, sizeof(char));
    int len = recv(info->client, buf, BUFSIZE, 0);
    if(len == SOCKET_ERROR) {
        fprintf(stderr, "Failed to receive the message from client of thread %d. Error code: %d\n",
                info->td, WSAGetLastError());
        goto close;
    }
    printf("Message from client of %d:\n%s\n", info->td, buf);

    // parse the HTTP message header
    httpMessage result;
    err = parseHttpMessage(buf, len, &result);
    if(err) {
        fprintf(stderr, "Failed to parse the http message from server of thread %d. No host found.\n", info->td);
        goto close;
    }
    
    // connect to the server
    info->server = connectToServer(&result);
    if(info->server == INVALID_SOCKET) {
        fprintf(stderr, "Failed to connect to server of thread %d. Error code: %d\n",
                info->td, WSAGetLastError());
        goto close;
    }
    printf("Connected to the server of thread %d successfully.\n", info->td);

    // forward the message to server and wait for reply
    len = send(info->server, buf, len, 0);
    if(len == SOCKET_ERROR) {
        fprintf(stderr, "Failed to send the message to server of thread %d. Error code: %d\n",
                info->td, WSAGetLastError());
        goto close;
    }
    len = recv(info->server, buf, BUFSIZE, 0);
    if(len == SOCKET_ERROR) {
        fprintf(stderr, "Failed to receive the message from server of thread %d. Error code: %d\n",
                info->td, WSAGetLastError());
        goto close;
    }
    printf("Message from server of %d:\n%s\n", info->td, buf);

    // process the reply
    len = process(buf, len);

    // send the reply to client
    len = send(info->client, buf, len, 0);
    if(len == SOCKET_ERROR) {
        fprintf(stderr, "Failed to send the message to client of thread %d. Error code: %d\n",
                info->td, WSAGetLastError());
        goto close;
    }

close:
    // close sockets
    err = closeSocket(info->client);
    if(err)
        fprintf(stderr, "Failed to shutdown the client socket of thread %d. Error code: %d\n",
                info->td, err);
    if(info->server != INVALID_SOCKET) {
        err = closeSocket(info->server);
        if(err)
            fprintf(stderr, "Failed to close the server socket of thread %d. Error code: %d\n",
                    info->td, err);
    }
    printf("Ended a thread: %d.\n", info->td);
    free(buf);
    _endthreadex(0);
    return 0;
}

int closeSocket(SOCKET sd) {
    int err;
    err = shutdown(sd, SD_BOTH);
    if(err) {
        err = WSAGetLastError();
        closesocket(sd);
    }
    err = closesocket(sd);
    if(err)
        return WSAGetLastError();
    return 0;
}

int loadConfig() {
    return 0;
}

void initializeServer() {
    cacheEnabled = FALSE;
    siteRecords = NULL;
    userRecords = NULL;
    redirectRecords = NULL;
    if(_access(CONFIG_FILE, 0)) {
        puts("Config file not found.");
    } else {
        puts("Config file found.");
        int err = loadConfig();
        if(err) {
            puts("Failed to load config file.");
            freeSiteRecords();
            freeUserRecords();
            freeRedirectRecords();
            cacheEnabled = FALSE;
            siteRecords = NULL;
            userRecords = NULL;
            redirectRecords = NULL;
        } else {
            puts("Loaded config successfully.");
        }
    }
}

int parseHttpMessage(char *message, int len, httpMessage *result) {
    BOOL foundHost = FALSE;
    for(int i = 0; i < len; i++) {
        if(!strncmp(message + i, "Host:", 5)) {
            i += 5;
            while(message[i] == ' ') i++;
            for(int j = 0; i + j < len && message[i + j] != '\r' && message[i + j] != '\0' && message[i + j] != ':'; j++)
                result->host[j] = message[i + j];
            foundHost = TRUE;
        }
        while(message[i] != '\n' && message[i] != '\0') i++;
    }
    return !(foundHost);
}

SOCKET connectToServer(httpMessage *message) {
    addrinfo *info;
    addrinfo hints;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    int err = getaddrinfo(message->host, "http", &hints, &info);
    SOCKET server = INVALID_SOCKET;
    if(!err) {
        server = socket(PF_INET, SOCK_STREAM, 0);
        for(addrinfo *i = info; i != NULL; i = i->ai_next) {
            err = connect(server, i->ai_addr, i->ai_addrlen);
            if(!err) break;
        }
    }
    freeaddrinfo(info);
    return err ? INVALID_SOCKET : server;
}

int process(char *buf, int len) {
    return len;
}

int insertSiteRecord(char *host, int len) {
    siteRecord *record = (siteRecord*)malloc(sizeof(siteRecord));
    if(!record) return -1;
    strncpy_s(record->host, 128, host, len);
    record->next = siteRecords;
    siteRecords = record;
    return 0;
}

int insertUserRecord(in_addr addr) {
    userRecord *record = (userRecord*)malloc(sizeof(userRecord));
    if(!record) return -1;
    record->addr = addr;
    record->next = userRecords;
    userRecords = record;
    return 0;
}

int insertRedirectRecord(char *source, int slen, char *target, int tlen) {
    redirectRecord *record = (redirectRecord*)malloc(sizeof(redirectRecord));
    if(!record) return -1;
    strncpy_s(record->source, 128, source, slen);
    strncpy_s(record->target, 128, target, tlen);
    record->next = redirectRecords;
    redirectRecords = record;
    return 0;
}

void freeSiteRecords() {
    for(siteRecord *record = siteRecords; record;) {
        siteRecord *next = record->next;
        free(record);
        record = next;
    }
}

void freeUserRecords() {
    for(userRecord *record = userRecords; record;) {
        userRecord *next = record->next;
        free(record);
        record = next;
    }
}

void freeRedirectRecords() {
    for(redirectRecord *record = redirectRecords; record;) {
        redirectRecord *next = record->next;
        free(record);
        record = next;
    }
}

void finalizeServer() {
    freeSiteRecords();
    freeUserRecords();
    freeRedirectRecords();
}
