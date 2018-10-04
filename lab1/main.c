#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>
#include <winsock2.h>
#include <process.h>
#include "session.h"

#pragma comment(lib, "ws2_32")

#define WINSOCK_VERSION_REQUIRED MAKEWORD(2, 2)
#define PROXY_PORT 8080

typedef struct sockaddr_in sockaddr_in;
SOCKET mainSocket = INVALID_SOCKET;
threadInfo *threads;
int err;

int initializeMainSocket();
unsigned __stdcall mainLoop(void *context);
int finalizeMainSocket();

/*
 * Main thread.
 */
int main() {
    int err;

    // initialize the server
    initializeServer();

    //initialize the thread list
    threads = NULL;

    // initialize the main socket
    err = initializeMainSocket();
    if(err == -1) {
        finalizeMainSocket();
        return -1;
    }

    // start up the main loop
    unsigned mainThread;
    _beginthreadex(NULL, 0, mainLoop, NULL, 0, &mainThread);

    // accept commands to control the proxy server
    while(1) {
        char cmd[32];
        fgets(cmd, sizeof(cmd), stdin);
        int len = strlen(cmd);
        for(int i = len - 1; i > 0; i--)
            if(cmd[i] == ' ' || cmd[i] == '\n' || cmd[i] == '\r') cmd[i] = '\0';
            else break;
        if(!strcmp(cmd, "c") || !strcmp(cmd, "cache status")) {
            if(cacheEnabled)
                puts("Cache is enabled.");
            else
                puts("Cache is disabled.");
        } else if(!strcmp(cmd, "w") || !strcmp(cmd, "wall status")) {
            puts("No wall status.");
        } else if(!strcmp(cmd, "u") || !strcmp(cmd, "user status")) {
            puts("No user status.");
        } else if(!strcmp(cmd, "r") || !strcmp(cmd, "redirect status")) {
            puts("No redirect status.");
        } else if(!strcmp(cmd, "s") || !strcmp(cmd, "status")) {
            puts("No status.");
        } else if(!strcmp(cmd, "e") || !strcmp(cmd, "exit")) {
            break;
        } else if(!strcmp(cmd, "q") || !strcmp(cmd, "quit")) {
            break;
        } else {
            puts("Invalid command.");
        }
    }

    // finalize the main socket
    puts("Closing the main socket...");
    err = finalizeMainSocket();
    if(err == -1)
        return -1;
    else
        puts("Closed the main socket successfully.");

    // finalize the thread list
    for(threadInfo *info = threads; info;) {
        threadInfo *next = info->next;
        WaitForSingleObject(&info->td, INFINITE);
        free(info);
        info = next;
    }

    // finalize the server
    finalizeServer();

    return 0;
}

/*
 * Initialize Winsock and the main socket.
 */
int initializeMainSocket() {
    // initialize winsock
    WSADATA wsaData;
    err = WSAStartup(WINSOCK_VERSION_REQUIRED, &wsaData);
    if(err) {
        fprintf(stderr, "winsock.dll not found.\n");
        return -1;
    } else if(wsaData.wVersion != WINSOCK_VERSION_REQUIRED) {
        fprintf(stderr, "Invalid Winsock version: %d.%d. Expected version: %d.%d.",
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
    return 0;
}

/*
 * Wait for connection requests and create threads.
 */
unsigned __stdcall mainLoop(void *context) {
    while(1) {
        SOCKET sd = accept(mainSocket, NULL, NULL);
        if(sd == INVALID_SOCKET && WSAGetLastError() == WSAEINTR)
            break;
        threadInfo *info = (threadInfo*)malloc(sizeof(threadInfo));
        info->client = sd;
        info->server = INVALID_SOCKET;
        info->next = threads;
        threads = info;
        _beginthreadex(NULL, 0, threadMain, info, 0, &info->td);
    }
    _endthreadex(0);
    return 0;
}

int finalizeMainSocket() {
    // close the main socket
    if(mainSocket != INVALID_SOCKET) {
        err = shutdown(mainSocket, SD_BOTH);
        if(err) {
            err = WSAGetLastError();
            if(err != WSAENOTCONN)
                fprintf(stderr, "Failed to shutdown the main socket. Error code: %d\n", err);
        }
        err = closesocket(mainSocket);
        if(err) {
            fprintf(stderr, "Failed to close the main socket. Error code: %d\n", WSAGetLastError());
            WSACleanup();
            return -1;
        }
    }

    // clean up winsock
    WSACleanup();
    return 0;
}
