#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "session.h"

const char *CONFIG_FILE = "config.ini";

int closeSocket(SOCKET sd);
int loadConfig();
void freeSiteRecords();
void freeUserRecords();
void freeRedirectRecords();

unsigned __stdcall threadMain(void *context) {
    int err;
    threadInfo *info = (threadInfo*)context;
    printf("Hello, thread!\n");
    err = closeSocket(info->client);
    if(err)
        fprintf(stderr, "Failed to shutdown the client socket of thread %d. Error code: %d\n",
                info->td, err);
    err = closeSocket(info->server);
    if(err)
        fprintf(stderr, "Failed to close the server socket of thread %d. Error code: %d\n",
                info->td, err);
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
