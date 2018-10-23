#define _CRT_SECURE_NO_WARNINGS

#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "session.h"
#include "httpMessage.h"
#include "cache.h"

// local data shared by all threads
const char *CONFIG_FILE = "config.ini";

BOOL isReadable(SOCKET sd);
SOCKET connectToServer(const char *host, const char *hostPort);
BOOL redirect(httpMessage *request);
int closeSocket(SOCKET sd);
int loadConfig();
int insertSiteRecord(char *host, int len);
int insertUserRecord(in_addr addr);
int insertRedirectRecord(char *source, int slen, char *target, int tlen);
void freeSiteRecords();
void freeUserRecords();
void freeRedirectRecords();

/*
 * Session thread
 */
unsigned __stdcall threadMain(void *context) {
    int err, len;
    BOOL connectionCloseFlag = FALSE;

    // initialize
    threadInfo *info = (threadInfo*)context;
    printf("Created a new thread: %d.\n", info->td);

    BOOL connected = FALSE;
    char *buf = (char*)malloc(sizeof(char)*BUFSIZE);
    httpMessage *request = (httpMessage*)malloc(sizeof(httpMessage));
    httpMessage *serverResponse = (httpMessage*)malloc(sizeof(httpMessage));
    httpMessage *tmpResponse = (httpMessage*)malloc(sizeof(httpMessage));
    if(buf == NULL || request == NULL || tmpResponse == NULL || serverResponse == NULL) {
        fprintf(stderr, "Failed to allocate memory.\n");
        goto close;
    }
    request->header = serverResponse->header = tmpResponse->header = NULL;
    request->extra = serverResponse->extra = tmpResponse->extra = NULL;

    while(1) {
        connectionCloseFlag = FALSE;
        // receive message from client
        if(!isReadable(info->client)) continue;
        len = recv(info->client, buf, BUFSIZE-1, 0);
        if(len == SOCKET_ERROR) {
            err = WSAGetLastError();
            if(connectionCloseFlag)
                break;
            else {
                fprintf(stderr, "Failed to receive the message from client of thread %d. Error code: %d\n",
                        info->td, err);
                break;
            }
        } else if(len == 0)
            break;
        buf[len] = '\0';
        printf("Message from client of %d:\n%s\n", info->td, buf);

        // parse the HTTP message header
        clearHttpMessage(request);
        err = parseHttpMessage(buf, len, request);
        if(err) {
            fprintf(stderr, "Failed to parse the http message from client of thread %d. Error code: %d\n", info->td, err);
            fprintf(stderr, "message length: %d\n", len);
            continue;
        }
        puts("Parsed HTTP request successfully.");
        printf("Get host: %s\n", request->host);

        // check if the connection should be closed
        const char *connectionField = getValueHandle(request, "Connection");
        connectionCloseFlag |= (connectionField != NULL && _stricmp(connectionField, "Close") == 0);
        const char *proxyConnectionField = getValueHandle(request, "Proxy-Connection");
        connectionCloseFlag |= (proxyConnectionField != NULL && _stricmp(proxyConnectionField, "Close") == 0);

        // check if the server is blocked or redirected
        BOOL localResponseFlag = FALSE;
        char host[ADDR_LEN];
        strcpy(host, request->host);
        if(redirect(request)) {
            if(getValueHandle(request, "Host") == NULL) {
                printf("Host %s has been blocked.\n", host);
                clearHttpMessage(serverResponse);
                strcpy(serverResponse->firstline, "HTTP/1.1 404 Not Found\r\n\r\n");
                localResponseFlag = TRUE;
            } else {
                writeMessageTo(request, buf);
            }
            printf("The real host to be visited: %s\n", request->host);
        }

        /*
         * Control reaches there through any path guarantees that request is a proper request
         * and buf is corresponding to request, or a proper serverResponse has been set.
         */

        if(!localResponseFlag && cacheEnabled) {
            clearHttpMessage(serverResponse);
            char lastModified[TIME_LEN];
            err = getCachedData(request->firstline, serverResponse, lastModified);
            if(err == CACHE_OK) {
                localResponseFlag = TRUE;
            } else if(err == CACHE_DEAD){
                insertField(request, "If-Modified-Since", lastModified);
                writeMessageTo(request, buf);
            } else if(err == CACHE_NOT_FOUND) {
            }
        }

        if(!localResponseFlag) {
            if(!connected) {
                // connect to the server
                info->server = connectToServer(request->host, request->hostPort);
                if(info->server == INVALID_SOCKET) {
                    fprintf(stderr, "Failed to connect to server of thread %d. Error code: %d\n",
                            info->td, WSAGetLastError());
                    break;
                }
                connected = TRUE;
                printf("Connected to the server of thread %d successfully.\n", info->td);
            }

            // forward the message to server and wait for reply
            printf("Message to server of %d:\n%s\n", info->td, buf);
            len = send(info->server, buf, len, 0);
            if(len == SOCKET_ERROR) {
                fprintf(stderr, "Failed to send the message to server of thread %d. Error code: %d\n",
                        info->td, WSAGetLastError());
                continue; // TODO: return an error message to the client
            }
        }

        do {
            if(!localResponseFlag) {
                if(!isReadable(info->server)) break;
                len = recv(info->server, buf, BUFSIZE-1, 0);
                if(len == SOCKET_ERROR) {
                    fprintf(stderr, "Failed to receive the message from server of thread %d. Error code: %d\n",
                            info->td, WSAGetLastError());
                    // TODO: ditto
                    break;
                } else if(len == 0)
                    break;
                buf[len] = '\0';
                printf("Message from server of %d:\n%s\n", info->td, buf);
                if(cacheEnabled) {
                    clearHttpMessage(tmpResponse);
                    err = parseHttpMessage(buf, len, tmpResponse);
                    if(strcmp(RESPONSE_STATUS(tmpResponse), HTTP_STATUS_NOT_MODIFIED) == 0) {
                        localResponseFlag = TRUE; // the cached response can be used
                    } else {
                        cacheData(tmpResponse); // TODO: use the first line of REQUEST to cache
                    }
                }
            }
            if(localResponseFlag) {
                len = writeMessageTo(serverResponse, buf);
            }

            // send the reply to client
            len = send(info->client, buf, len, 0);
            if(len == SOCKET_ERROR) {
                fprintf(stderr, "Failed to send the message to client of thread %d. Error code: %d\n",
                        info->td, WSAGetLastError());
                break;
            }

            // check if the connection should be closed
            if(!localResponseFlag) {
                const char *connectionField = getValueHandle(tmpResponse, "Connection");
                connectionCloseFlag |= (connectionField != NULL && _stricmp(connectionField, "Close") == 0);
                const char *proxyConnectionField = getValueHandle(tmpResponse, "Proxy-Connection");
                connectionCloseFlag |= (proxyConnectionField != NULL && _stricmp(proxyConnectionField, "Close") == 0);
            }
            if(connectionCloseFlag) goto close;
        } while(!localResponseFlag);
    }

close:
    if(buf)
        free(buf);
    if(request != NULL)
        freeHttpMessage(request);
    if(serverResponse != NULL)
        freeHttpMessage(serverResponse);
    if(tmpResponse != NULL)
        freeHttpMessage(tmpResponse);
    // close sockets
    err = closeSocket(info->client);
    if(err)
        fprintf(stderr, "Failed to shutdown the client socket of thread %d. Error code: %d\n", info->td, err);
    if(info->server != INVALID_SOCKET) {
        err = closeSocket(info->server);
        if(err)
            fprintf(stderr, "Failed to close the server socket of thread %d. Error code: %d\n", info->td, err);
    }
    printf("Ended a thread: %d.\n", info->td);
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

int loadConfig(const char *file) {
    FILE *f = fopen(file, "r");
    char buf[256];
    int state = 0;
    while(fgets(buf, 256, f) != NULL) {
        if(buf[0] == '#') continue; // '#' for a comment line
        // remove the line break
        int len = strlen(buf);
        if(buf[len - 1] == '\n') buf[--len] = '\0';
        if(buf[len - 1] == '\r') buf[--len] = '\0';
        // switch the state
        if(strncmp(buf, "[Wall]", 6) == 0)
            state = 1;
        else if(strncmp(buf, "[BlockedUser]", 13) == 0)
            state = 2;
        else if(strncmp(buf, "[Redirect]", 10) == 0)
            state = 3;
        else {
            // insert config items
            in_addr addr;
            char *sp;
            switch(state) {
                case 0: // cache config
                    if(strncmp(buf, "CacheEnabled=1", 14) == 0)
                        cacheEnabled = TRUE;
                    break;
                case 1: // wall config
                    insertSiteRecord(buf, len);
                    break;
                case 2: // user config
                    addr.S_un.S_addr = inet_addr(buf);
                    if(addr.S_un.S_addr == INADDR_NONE)
                        break;
                    insertUserRecord(addr);
                    break;
                case 3: // redirect config
                    sp = strchr(buf, ' ');
                    if(sp == NULL) break;
                    insertRedirectRecord(buf, sp - buf, sp + 1, buf + len - sp - 1);
                    break;
            }
        }
    }
    fclose(f);
    return 0;
}

BOOL isReadable(SOCKET sd) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sd, &readfds);
    TIMEVAL timeout;
    timeout.tv_sec = TIMEOUT/1000;
    timeout.tv_usec = TIMEOUT%1000;
    return select(0, &readfds, NULL, NULL, &timeout) > 0;
}

void initializeServer() {
    cacheEnabled = FALSE;
    siteRecords = NULL;
    userRecords = NULL;
    redirectRecords = NULL;
    if(_access(CONFIG_FILE, 0) != 0) {
        puts("Config file not found.");
    } else {
        puts("Config file found.");
        if(loadConfig(CONFIG_FILE) != 0) {
            puts("Failed to load config file.");
        } else {
            puts("Loaded config successfully.");
        }
    }
}

SOCKET connectToServer(const char *host, const char *hostPort) {
    addrinfo *info;
    addrinfo hints;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    int err = getaddrinfo(host, hostPort, &hints, &info);
    SOCKET server = INVALID_SOCKET;
    if(!err) {
        server = socket(PF_INET, SOCK_STREAM, 0);
        for(addrinfo *i = info; i != NULL; i = i->ai_next) {
            err = connect(server, i->ai_addr, i->ai_addrlen);
            if(!err) break;
        }
    } else {
        printf("Failed to get address info. Error code:%d\n", WSAGetLastError());
    }
    freeaddrinfo(info);
    return err ? INVALID_SOCKET : server;
}

BOOL redirect(httpMessage *request) {
    //TODO: change the firstline
    BOOL result = FALSE;
    for(redirectRecord *record = redirectRecords; record; record = record->next)
        if(strcmp(record->source, request->host) == 0) {
            removeField(request, "Host");
            insertField(request, "Host", record->target);
            strcpy(request->host, record->target);
            strcpy(request->hostPort, "http");
            result = TRUE;
        }
    for(siteRecord *record = siteRecords; record; record = record->next)
        if(strcmp(record->host, request->host) == 0) {
            removeField(request, "Host");
            request->host[0] = '\0';
            result = TRUE;
        }
    return result;
}

BOOL isBlockedUser(const sockaddr_in addr) {
    for(userRecord *record = userRecords; record; record = record->next)
        if(record->addr.S_un.S_addr == addr.sin_addr.S_un.S_addr)
            return TRUE;
    return FALSE;
}

int insertSiteRecord(char *host, int len) {
    siteRecord *record = (siteRecord*)malloc(sizeof(siteRecord));
    if(!record) return -1;
    strncpy(record->host, host, len);
    record->host[len] = '\0';
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
    strncpy(record->source, source, slen);
    record->source[slen] = '\0';
    strncpy(record->target, target, tlen);
    record->target[tlen] = '\0';
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
