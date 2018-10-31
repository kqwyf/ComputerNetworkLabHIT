#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include "message.h"
#include "protocol.h"

#define SND_BUF_SIZE 1024
#define RCV_BUF_SIZE 1024

const char *PEER_IP = "127.0.0.1";

const char *help =
"Commands:\n"
"/connect <port>    Set the remote port. The port number should be in\n"
"                   [1024, 65535].\n"
"                   You can send messages to remote at the given port\n"
"                   on 127.0.0.1 after the port has been set.\n"
"/lost <number>     Set the lost rate of sending. The number should be\n"
"                   in [0,100] and invalid number will be regarded as\n"
"                   0. The default lost rate is 0.\n"
"/quit              Quit this program.\n";

void *serverThread(void *context);
void *clientThread(void *context);

// global configuration
int gbnMode = 1;
int lostRate = 0;

int main(int argc, char **argv) {
    int err = 0;

    if(argc > 1 && argv[1][0] == 's')
        gbnMode = 0;
    printf("The program is running in %s mode.\n", gbnMode?"GBN":"SR");

    // initialize sockets
    int serverfd = socket(AF_INET, SOCK_DGRAM|SOCK_NONBLOCK, IPPROTO_UDP);
    int clientfd = socket(AF_INET, SOCK_DGRAM|SOCK_NONBLOCK, IPPROTO_UDP);
    if(serverfd < 0 || clientfd < 0) {
        fprintf(stderr, "Failed to create sockets.\n");
        goto finalize;
    }

    // configure sockets
    struct sockaddr_in myAddr;
    myAddr.sin_family = AF_INET;
    myAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    for(int port = 10000; port < 65536; port++) {
        myAddr.sin_port = htons(port);
        err = bind(serverfd, (struct sockaddr*)&myAddr, sizeof(myAddr));
        if(!err) break;
    }
    if(err) {
        fprintf(stderr, "Failed to find a free port.\n");
        goto finalize;
    }
    printf("Server port: %d\n", ntohs(myAddr.sin_port));

    puts(help);
    puts("Start listening...");
    pthread_t serverTid, clientTid;
    pthread_create(&serverTid, NULL, serverThread, &serverfd);
    pthread_create(&clientTid, NULL, clientThread, &clientfd);
    pthread_join(clientTid, NULL);

finalize:
    if(serverfd >= 0) {
        shutdown(serverfd, SHUT_RDWR);
        close(serverfd);
    }
    if(clientfd >= 0) {
        shutdown(clientfd, SHUT_RDWR);
        close(clientfd);
    }
    return 0;
}

void *serverThread(void *context) {
    int serverfd = *(int*)context;
    char *sendBuf = (char*) malloc(sizeof(char)*SND_BUF_SIZE);
    char *recvBuf = (char*) malloc(sizeof(char)*(RCV_BUF_SIZE));
    void *sndWindow;
    void *rcvWindow;
    if(gbnMode) {
        sndWindow = createGbnSndWindow();
        rcvWindow = createGbnRcvWindow();
    } else {
        sndWindow = createSrSndWindow();
        rcvWindow = createSrRcvWindow();
    }
    struct sockaddr_in clientAddr;
    while(1) {
        // receive message
        int len = 0;
        if(gbnMode) {
            len = gbnRecv((gbnRcvWindow*)sndWindow, serverfd, &clientAddr, recvBuf, RCV_BUF_SIZE);
            if(len <= 0) continue;
        } else {
            len = srRecv((srRcvWindow*)rcvWindow, serverfd, &clientAddr, recvBuf, RCV_BUF_SIZE);
            if(len <= 0) continue;
        }
        // read and/or print message
        if(recvBuf[0] == '?') {
            if(strncmp(recvBuf+1, "hello", 4)) {
                strcpy(sendBuf, "hello");
            } else {
                printf("Received query \"%s\", but cannot understand.\n", recvBuf);
                continue;
            }
        } else {
            puts(recvBuf);
            continue;
        }
        // send reply (if any)
        if(gbnMode) {
            gbnSend((gbnSndWindow*)sndWindow, serverfd, &clientAddr, sendBuf, len);
        } else {
            srSend((srSndWindow*)sndWindow, serverfd, &clientAddr, sendBuf, len);
        }
    }
    free(sendBuf);
    free(recvBuf);
    return NULL;
}

void *clientThread(void *context) {
    int clientfd = *(int*)context;
    char *sendBuf = (char*) malloc(sizeof(char)*SND_BUF_SIZE);
    char *recvBuf = (char*) malloc(sizeof(char)*RCV_BUF_SIZE);
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(PEER_IP);
    serverAddr.sin_port = 0;
    void *sndWindow;
    void *rcvWindow;
    if(gbnMode) {
        sndWindow = createGbnSndWindow();
        rcvWindow = createGbnRcvWindow();
    } else {
        sndWindow = createSrSndWindow();
        rcvWindow = createSrRcvWindow();
    }
    while(1) {
        fgets(sendBuf, SND_BUF_SIZE, stdin);
        int len = strlen(sendBuf);
        sendBuf[--len] = '\0';
        if(len == 0) continue;
        if(sendBuf[0] == '/') {
            if(strncmp(sendBuf+1, "connect ", 8) == 0) {
                int serverPort = atoi(sendBuf+9);
                if(serverPort < 1024 || serverPort > 65535) {
                    puts("[CTRL] The port number should be in [1024, 65535].");
                } else {
                    serverAddr.sin_port = htons(serverPort);
                    printf("[CTRL] The remote port has been set to %d.\n", serverPort);
                }
            } else if(strncmp(sendBuf+1, "lost ", 5) == 0) {
                int tmpRate = atoi(sendBuf+6);
                if(tmpRate < 0 || tmpRate > 100)
                    tmpRate = 0;
                lostRate = tmpRate;
                printf("[CTRL] The lost rate has been set to %d.\n", lostRate);
            } else if(strcmp(sendBuf+1, "quit") == 0) {
                puts("[CTRL] Quit program.");
                break;
            } else {
                puts("[CTRL] Unknown command.");
            }
        } else {
            if(serverAddr.sin_port == 0) {
                puts("[SEND] You should set the remote port before sending message.");
            } else {
                if(gbnMode) {
                    gbnSend((gbnSndWindow*)sndWindow, clientfd, &serverAddr, sendBuf, len+1);
                    len = gbnRecv((gbnRcvWindow*)sndWindow, clientfd, &serverAddr, recvBuf, RCV_BUF_SIZE);
                }
                else {
                    srSend((srSndWindow*)sndWindow, clientfd, &serverAddr, sendBuf, len+1);
                    len = srRecv((srRcvWindow*)rcvWindow, clientfd, &serverAddr, recvBuf, RCV_BUF_SIZE);
                }
            }
        }
    }
    free(sendBuf);
    free(recvBuf);
    return NULL;
}
