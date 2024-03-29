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
BOOL redirect(httpMessage *request, BOOL removeCookie);
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
    BOOL httpsMode = FALSE;
    BOOL redirected = FALSE;

    // initialize
    threadInfo *info = (threadInfo*)context;
    printf("Created a new thread: %d.\n", info->td);

    BOOL connected = FALSE;
    char *buf = (char*)malloc(sizeof(char)*BUFSIZE);
    httpMessage *request = (httpMessage*)malloc(sizeof(httpMessage));
    httpMessage *localResponse = (httpMessage*)malloc(sizeof(httpMessage));
    httpMessage *serverResponse = (httpMessage*)malloc(sizeof(httpMessage));
    if(buf == NULL || request == NULL || serverResponse == NULL || localResponse == NULL) {
        fprintf(stderr, "Failed to allocate memory.\n");
        goto close;
    }
    request->header = localResponse->header = serverResponse->header = NULL;
    request->extra = localResponse->extra = serverResponse->extra = NULL;

    // the main loop of the session
    while(1) {
        BOOL localResponseFlag = FALSE;
        BOOL connectionCloseFlag = FALSE;
        BOOL conditionalGetFlag = FALSE;

        // check if there is new data from client
        if(!isReadable(info->client)) break;
        // receive message from client
        len = recv(info->client, buf, BUFSIZE, 0);
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
        if(httpsMode) printf("Received message from client of %d. Length: %d\n", info->td, len);
        else printf("Message from client of %d:\n%s\n", info->td, buf);

        if(!httpsMode) {
            // parse the HTTP message header
            clearHttpMessage(request);
            err = parseHttpMessage(buf, len, request);
            if(err) {
                fprintf(stderr, "Failed to parse the http message from client of thread %d. Error code: %d\n", info->td, err);
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
            localResponseFlag = FALSE;
            char host[ADDR_LEN];
            strcpy(host, request->host);
            if(redirect(request, !redirected)) {
                if(getValueHandle(request, "Host") == NULL) {
                    printf("Host %s has been blocked.\n", host);
                    clearHttpMessage(localResponse);
                    setFirstLine(localResponse, HTTP_VERSION_11, HTTP_404, HTTP_404_DESCRIPTION);
                    localResponseFlag = TRUE;
                } else {
                    writeMessageTo(request, buf);
                    printf("The real host to be visited: %s\n", request->host);
                    redirected = TRUE;
                }
            }

            /*
             * Control reaches there through any path guarantees that request is a proper request
             * and buf is corresponding to request, or a proper localResponse has been set.
             */

            // check if there is available cache
            if(!localResponseFlag && !httpsMode && cacheEnabled) {
                clearHttpMessage(localResponse);
                char lastModified[TIME_LEN];
                err = getCachedData(request, localResponse, lastModified);
                if(err == CACHE_OK) {
                    printf("Got cached data for %s\n", REQUEST_URL(request));
                    localResponseFlag = TRUE;
                } else if(err == CACHE_DEAD){
                    printf("Cached data for %s has been dead. A conditional GET will be sent to server.\n", REQUEST_URL(request));
                    insertField(request, "If-Modified-Since", lastModified);
                    writeMessageTo(request, buf);
                    conditionalGetFlag = TRUE;
                } else if(err == CACHE_NOT_FOUND) {
                    printf("No cache found for %s\n", REQUEST_URL(request));
                }
            }
        }

        // send request to server
        if(!localResponseFlag) {
            if(!httpsMode && !connected) {
                // connect to the server
                info->server = connectToServer(request->host, request->hostPort);
                if(info->server == INVALID_SOCKET) {
                    fprintf(stderr, "Failed to connect to server of thread %d. Error code: %d\n",
                            info->td, WSAGetLastError());
                    break;
                }
                connected = TRUE;
                if(strcmp(REQUEST_METHOD(request), "CONNECT") == 0) { // HTTPS
                    clearHttpMessage(localResponse);
                    setFirstLine(localResponse, HTTP_VERSION_11, HTTP_200, HTTP_200_DESCRIPTION);
                    localResponseFlag = TRUE;
                    httpsMode = TRUE;
                }
                printf("Connected to the server of thread %d successfully.\n", info->td);
            }

            if(!localResponseFlag) {
                // forward the message to server and wait for reply
                if(httpsMode) printf("Sending message to server of %d. Length: %d\n", info->td, len);
                else printf("Message to server of %d:\n%s\n", info->td, buf);
                len = send(info->server, buf, len, 0);
                if(len == SOCKET_ERROR) {
                    fprintf(stderr, "Failed to send the message to server of thread %d. Error code: %d\n",
                            info->td, WSAGetLastError());
                    clearHttpMessage(localResponse);
                    setFirstLine(localResponse, HTTP_VERSION_11, HTTP_500, HTTP_500_DESCRIPTION);
                    localResponseFlag = TRUE;
                }
            }
        }

        // the receiving-forwarding loop of the session, receiving data from server and forwarding it to client
        BOOL firstLoop = TRUE;
        do {
            int parserErr = PARSE_FORMAT_ERROR;
            // receive data from server
            if(!localResponseFlag) {
                if(!isReadable(info->server)) break;
                len = recv(info->server, buf, BUFSIZE, 0);
                if(len == SOCKET_ERROR) {
                    fprintf(stderr, "Failed to receive the message from server of thread %d. Error code: %d\n",
                            info->td, WSAGetLastError());
                    clearHttpMessage(localResponse);
                    setFirstLine(localResponse, HTTP_VERSION_11, HTTP_500, HTTP_500_DESCRIPTION);
                    localResponseFlag = TRUE;
                } else if(len == 0)
                    break;
            }
            if(!localResponseFlag) {
                if(httpsMode) printf("Receiving message from server of %d. Length: %d\n", info->td, len);
                else printf("Message from server of %d:\n%s\n", info->td, buf);
                // check if the data should be cached
                if(!httpsMode && cacheEnabled) {
                    clearHttpMessage(serverResponse);
                    parserErr = parseHttpMessage(buf, len, serverResponse);
                    if(conditionalGetFlag && (parserErr==PARSE_OK || parserErr==PARSE_HOST_NOT_FOUND) && strcmp(RESPONSE_STATUS(serverResponse), HTTP_304) == 0) {
                        printf("The dead cached data of %s can be used since data on the server is not modified.\n", REQUEST_URL(request));
                        localResponseFlag = TRUE; // the cached response can be used
                    }
                    // for dead data which isn't modified, update its lastUpdated and ttl. for new data, cache it.
                    cacheData(request, buf, len, firstLoop);
                }
            }
            // if the response has been set, write it to the send buffer
            if(localResponseFlag) {
                len = writeMessageTo(localResponse, buf);
            }

            // send the reply to client
            if(!httpsMode)
                printf("Message to client of %d:\n%s\n", info->td, buf);
            len = send(info->client, buf, len, 0);
            if(len == SOCKET_ERROR) {
                fprintf(stderr, "Failed to send the message to client of thread %d. Error code: %d\n",
                        info->td, WSAGetLastError());
                break;
            }

            // check if the connection should be closed
            if(!localResponseFlag && (parserErr == PARSE_HOST_NOT_FOUND || parserErr == PARSE_OK)) {
                const char *connectionField = getValueHandle(serverResponse, "Connection");
                connectionCloseFlag |= (connectionField != NULL && _stricmp(connectionField, "Close") == 0);
                const char *proxyConnectionField = getValueHandle(serverResponse, "Proxy-Connection");
                connectionCloseFlag |= (proxyConnectionField != NULL && _stricmp(proxyConnectionField, "Close") == 0);
            }
            firstLoop = FALSE;
        } while(!localResponseFlag);
        if(connectionCloseFlag) break;
    }

close:
    if(buf)
        free(buf);
    if(request != NULL)
        freeHttpMessage(request);
    if(localResponse != NULL)
        freeHttpMessage(localResponse);
    if(serverResponse != NULL)
        freeHttpMessage(serverResponse);
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
        if(strcmp(buf, "[Wall]") == 0)
            state = 1;
        else if(strcmp(buf, "[BlockedUser]") == 0)
            state = 2;
        else if(strcmp(buf, "[Redirect]") == 0)
            state = 3;
        else {
            // insert config items
            in_addr addr;
            char *sp, *tmp;
            switch(state) {
                case 0: // cache config
                    sp = strchr(buf, '=');
                    if(sp == NULL) break;
                    for(tmp = sp - 1; tmp != buf && *tmp == ' '; tmp--);
                    if(strncmp(buf, "CacheEnabled", tmp + 1 - buf) == 0) {
                        for(tmp = sp + 1; tmp - buf < len && *tmp == ' '; tmp++);
                        cacheEnabled = (strcmp(tmp, "0") != 0);
                    }
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
                    for(tmp = sp + 1; tmp - buf < len && *tmp == ' '; tmp++);
                    insertRedirectRecord(buf, sp - buf, tmp, buf + len - tmp);
                    break;
            }
        }
    }
    fclose(f);
    return 0;
}

/*
 * Check if a socket is readable (with timeout).
 */
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

/*
 * Change the request if the host to be visited is blocked or redirected.
 */
BOOL redirect(httpMessage *request, BOOL removeCookie) {
    BOOL result = FALSE;
    for(redirectRecord *record = redirectRecords; record; record = record->next)
        if(strcmp(record->source, request->host) == 0) {
            removeField(request, "Host");
            insertField(request, "Host", record->target);
            if(removeCookie)
                removeField(request, "Cookie");
            strcpy(request->host, record->target);
            strcpy(request->hostPort, "http");
            // change the url in the first line of the request
            char *url = REQUEST_URL(request);
            char *suffix = strchr(url, '/');
            if(suffix != NULL && *(suffix+1) == '/') { // found pattern "//"
                char *newurl = (char*)malloc(sizeof(char)*ADDR_LEN);
                int len = suffix + 2 - url;
                strncpy(newurl, url, len);
                newurl[len] = '\0';
                strcat(newurl, record->target);
                suffix = strchr(suffix+2, '/');
                int hostLen = suffix - url - len;
                if(strlen(url) - strlen(record->target) + hostLen >= ADDR_LEN)
                    break; // new url is too long, redirection failed
                strcat(newurl, suffix==NULL ? "/" : suffix);
                setFirstLine(request, NULL, newurl, NULL);
                free(newurl);
            }
            result = TRUE;
            break;
        }
    for(siteRecord *record = siteRecords; record; record = record->next)
        if(strcmp(record->host, request->host) == 0) {
            removeField(request, "Host");
            request->host[0] = '\0';
            result = TRUE;
            break;
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
