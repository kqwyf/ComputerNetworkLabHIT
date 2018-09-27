#include <stdio.h>
#include <string.h>
#include <Windows.h>
#include <process.h>
#include <winsock2.h>

#pragma comment(lib, "ws2_32")

#define WINSOCK_VERSION MAKEWORD(2, 2)

typedef struct sockaddr_in sockaddr_in;

const int PROXY_PORT = 10240; // port number of the proxy server
const int THREAD_POOL_SIZE = 20; // size of the thread pool

int err;

int createThread(SOCKET sd);
VOID CALLBACK threadMain(PTP_CALLBACK_INSTANCE instance, PVOID Context, PTP_WORK Work);
int parseHttpRequest();
int process(); // process the HTTP message

/*
 * Main thread of server.
 */
int main() {
    // initialize winsock
    WSADATA wsaData;
    err = WSAStartup(WINSOCK_VERSION, &wsaData);
    if(err) {
        fprintf(stderr, "winsock.dll not found.\n");
        return -1;
    } else if(LOBYTE(wsaData.wVersion) != LOBYTE(WINSOCK_VERSION)
                || HIBYTE(wsaData.wVersion) != HIBYTE(WINSOCK_VERSION)) {
        fprintf(stderr, "Invalid Winsock version: %d.%d . Expected version: %d.%d .",
                    LOBYTE(wsaData.wVersion), HIBYTE(wsaData.wVersion),
                    LOBYTE(WINSOCK_VERSION), HIBYTE(WINSOCK_VERSION));
        WSACleanup();
        return -1;
    }
    printf("Initialized Winsock successfully.\n");

    // create and configure the main socket
    SOCKET mainSocket = socket(PF_INET, SOCK_STREAM, 0);
    if(mainSocket == INVALID_SOCKET) {
        fprintf(stderr, "Failed to create the main socket.\n");
        WSACleanup();
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
        closesocket(mainSocket);
        WSACleanup();
        return -1;
    }
    printf("Bind the main socket to port %d successfully.\n", PROXY_PORT);
    err = listen(mainSocket, SOMAXCONN);
    if(err) {
        fprintf(stderr, "Failed to start listening on main socket.\n");
        closesocket(mainSocket);
        WSACleanup();
        return -1;
    }

    // wait for connection requests and create threads
    printf("Start listening...\n");
    fflush(stdout);
    while(1) {
        SOCKET sd = accept(mainSocket, NULL, NULL);
    }

    // close the main socket
    err = closesocket(mainSocket);
    if(err) {
        fprintf(stderr, "Failed to close the main socket.\n");
        WSACleanup();
        return -1;
    }

    // clean up winsock
    WSACleanup();
    return 0;
}
