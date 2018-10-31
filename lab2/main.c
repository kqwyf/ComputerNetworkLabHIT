#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include "message.h"
#include "protocol.h"

#define SND_BUF_SIZE 1024
#define RCV_BUF_SIZE 1024

const char *PEER_IP = "127.0.0.1";

const char *help =
"Commands:\n"
"/connect <port>    Set the remote port. The port number should be\n"
"                   in [1024, 65535].\n"
"                   You can send messages to remote at the given\n"
"                   port on 127.0.0.1 after the port has been set.\n"
"/lost <number>     Set the lost rate of sending. The number should\n"
"                   be in [0,100] and invalid number will be regarded\n"
"                   as 0. The default lost rate is 0.\n"
"/quit              Quit this program.\n";

void serverThread(int clientfd);
void clientThread(int serverfd);

// global configuration
int gbnMode = 1;
int lostRate = 0;

int main(int argc, char **argv) {
    int err = 0;

    if(argc > 1 && argv[1][0] == 's')
        gbnMode = 0;
    printf("The program is running in %s mode.\n", gbnMode?"GBN":"SR");

    // initialize sockets
    int serverfd = socket(AF_INET, SOCK_DGRAM, 0);
    int clientfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(serverfd < 0 || clientfd < 0) {
        fprintf(stderr, "Failed to create sockets.\n");
        goto finalize;
    }

    // configure sockets
    ushort myPort;
    printf("Input the port to bind: ");
    scanf("%hu", &myPort);
    struct sockaddr_in myAddr;
    myAddr.sin_family = AF_INET;
    myAddr.sin_port = htons(myPort);
    myAddr.sin_addr.s_addr = inet_addr(PEER_IP);
    err = bind(serverfd, (struct sockaddr*)&myAddr, sizeof(myAddr));
    if(err) {
        fprintf(stderr, "Failed to bind the port %hu.\n", myPort);
        goto finalize;
    }

    puts(help);
    puts("Start listening...");
    if(fork() != 0) { // server thread
        serverThread(clientfd);
    } else { // client thread
        clientThread(serverfd);
    }

finalize:
    if(serverfd >= 0) {
        shutdown(serverfd, SHUT_RDWR);
        close(serverfd);
    }
    if(clientfd >= 0) {
        shutdown(serverfd, SHUT_RDWR);
        close(serverfd);
    }
    return 0;
}

void serverThread(int clientfd) {
    char *sendBuf = (char*) malloc(sizeof(char)*SND_BUF_SIZE);
    char *recvBuf = (char*) malloc(sizeof(char)*RCV_BUF_SIZE);
    struct sockaddr_in clientAddr;
    while(1) {
        if(gbnMode) {
            int len = gbnRecv(clientfd, &clientAddr, recvBuf, RCV_BUF_SIZE);
            gbnSend(clientfd, &clientAddr, sendBuf, len);
        }
        else {
            int len = srRecv(clientfd, &clientAddr, recvBuf, RCV_BUF_SIZE);
            srSend(clientfd, &clientAddr, sendBuf, len);
        }
    }
    free(sendBuf);
    free(recvBuf);
}

void clientThread(int serverfd) {
    char *sendBuf = (char*) malloc(sizeof(char)*SND_BUF_SIZE);
    char *recvBuf = (char*) malloc(sizeof(char)*RCV_BUF_SIZE);
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(PEER_IP);
    serverAddr.sin_port = 0;
    while(1) {
        fflush(stdin);
        fgets(sendBuf, SND_BUF_SIZE, stdin);
        if(sendBuf[0] == '/') {
            if(strncmp(sendBuf+1, "connect ", 8) == 0) {
                int serverPort = atoi(sendBuf+9);
                if(serverPort < 1024 || serverPort > 65535) {
                    puts("The port number should be in [1024, 65535].");
                } else {
                    serverAddr.sin_port = htons(serverPort);
                }
            } else if(strncmp(sendBuf+1, "lost ", 5) == 0) {
                int tmpRate = atoi(sendBuf+6);
                if(tmpRate < 0 || tmpRate > 100)
                    tmpRate = 0;
                lostRate = tmpRate;
            } else if(strcmp(sendBuf+1, "quit") == 0) {
                puts("Quit program.");
                break;
            } else {
                puts("Unknown command.");
            }
        } else {
            if(serverAddr.sin_port == 0) {
                puts("You should set the remote port before sending message.");
            } else {
                if(gbnMode) {
                    gbnSend(serverfd, &serverAddr, sendBuf);
                    int len = gbnRecv(serverfd, NULL, recvBuf, RCV_BUF_SIZE);
                }
                else {
                    srSend(serverfd, &serverAddr, sendBuf);
                    int len = srRecv(serverfd, NULL, recvBuf, RCV_BUF_SIZE);
                }
            }
        }
    }
    free(sendBuf);
    free(recvBuf);
}
