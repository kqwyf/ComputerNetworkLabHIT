#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <process.h>

typedef struct in_addr in_addr;
typedef struct sockaddr_in sockaddr_in;
typedef struct addrinfo addrinfo;

typedef struct siteRecord {
    char host[128];
    struct siteRecord *next;
} siteRecord;
typedef struct userRecord {
    in_addr addr;
    struct userRecord *next;
} userRecord;
typedef struct redirectRecord {
    char source[128];
    char target[128];
    struct redirectRecord *next;
} redirectRecord;

typedef struct threadInfo {
    unsigned td;
    SOCKET client;
    SOCKET server;
    struct threadInfo *next;
} threadInfo;

// data shared by all threads
BOOL cacheEnabled;
siteRecord *siteRecords;
userRecord *userRecords;
redirectRecord *redirectRecords;

unsigned __stdcall threadMain(void *context);
void initializeServer();
void finalizeServer();
