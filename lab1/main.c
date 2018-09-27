#include <stdio.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>

#pragma comment(lib, "ws2_32")

#define WINSOCK_VERSION_REQUIRED MAKEWORD(2, 2)

const int PROXY_PORT = 10240; // port number of the proxy server
const int THREAD_POOL_SIZE = 20; // size of the thread pool

typedef struct sockaddr_in sockaddr_in;
typedef struct threadInfo {
    SOCKET sd;
} threadInfo;

SOCKET mainSocket = INVALID_SOCKET;
int err;

int initializeMainSocket();
int mainLoop();
int finalizeMainSocket();

int createThread(SOCKET sd);
VOID CALLBACK threadMain(PTP_CALLBACK_INSTANCE instance, PVOID Context, PTP_WORK Work);
int parseHttpRequest();
int process(); // process the HTTP message

/*
 * Inithalize Winsock and the main socket.
 */
int initializeMainSocket() {
    // initialize winsock
    WSADATA wsaData;
    err = WSAStartup(WINSOCK_VERSION_REQUIRED, &wsaData);
    if(err) {
        fprintf(stderr, "winsock.dll not found.\n");
        return -1;
    } else if(LOBYTE(wsaData.wVersion) != LOBYTE(WINSOCK_VERSION_REQUIRED)
                || HIBYTE(wsaData.wVersion) != HIBYTE(WINSOCK_VERSION_REQUIRED)) {
        fprintf(stderr, "Invalid Winsock version: %d.%d . Expected version: %d.%d .",
                    LOBYTE(wsaData.wVersion), HIBYTE(wsaData.wVersion),
                    LOBYTE(WINSOCK_VERSION_REQUIRED), HIBYTE(WINSOCK_VERSION_REQUIRED));
        return -1;
    }
    printf("Initialized Winsock successfully.\n");

    // create and configure the main socket
    mainSocket = socket(PF_INET, SOCK_STREAM, 0);
    if(mainSocket == INVALID_SOCKET) {
        fprintf(stderr, "Failed to create the main socket.\n");
        return -1;
    }
    printf("Created the main socket successfully.\n");
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PROXY_PORT);
    serverAddr.sin_addr.S_un.S_addr = INADDR_ANY;
    err = bind(mainSocket, (SOCKADDR*)&serverAddr, sizeof(SOCKADDR));
    if(err) {
        fprintf(stderr, "Failed to bind the socket to port %d.\n", PROXY_PORT);
        return -1;
    }
    printf("Bind the main socket to port %d successfully.\n", PROXY_PORT);
    err = listen(mainSocket, SOMAXCONN);
    if(err) {
        fprintf(stderr, "Failed to start listening on main socket.\n");
        return -1;
    }
    printf("Start listening...\n");
    fflush(stdout);
    return 0;
}

/*
 * Wait for connection requests and create threads.
 */
int mainLoop() {
    while(1) {
        SOCKET sd = accept(mainSocket, NULL, NULL);
    }
    return 0;
}

int finalizeMainSocket() {
    // close the main socket
    if(mainSocket != INVALID_SOCKET) {
        err = closesocket(mainSocket);
        if(err) {
            fprintf(stderr, "Failed to close the main socket.\n");
            WSACleanup();
            return -1;
        }
    }

    // clean up winsock
    WSACleanup();
    return 0;
}

/*
 * Main thread of server.
 */
int main() {
    int err;

    err = initializeMainSocket();
    if(err == -1) {
        finalizeMainSocket();
        return -1;
    }

    err = mainLoop();
    if(err == -1) {
        finalizeMainSocket();
        return -1;
    }

    err = finalizeMainSocket();
    if(err == -1)
        return -1;

    return 0;
}
