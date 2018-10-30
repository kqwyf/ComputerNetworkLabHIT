#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "message.h"
#include "protocol.h"

#define SND_WIN_SIZE (1<<10)
#define RCV_WIN_SIZE (1<<10)

const char *PEER_IP = "127.0.0.1";
const char *PEER_PORT = "23156";
const uint16_t MY_PORT = 23155;

int main(int argc, char **argv) {
    int gbnMode = 1;
    if(argc > 1 && argv[1][0] == 's')
        gbnMode = 0;

    // initialize socket
    int peerfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(peerfd < 0) {
        printf("Failed to create a socket.\n");
        goto finalize;
    }
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(MY_PORT);
    serverAddr.sin_addr = INADDR_ANY;
    
    if(gbnMode)
        gbnStart(peerfd);
    else
        srStart(peerfd);

finalize:
    shutdown(peerfd, SHUT_RDWR);
    close(peerfd);
}

