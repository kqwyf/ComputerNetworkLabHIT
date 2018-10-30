#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "message.h"
#include "protocol.h"

#define SND_WIN_SIZE (1<<10)
#define RCV_WIN_SIZE (1<<10)

#define ACK_PERIOD 10

const char *PEER_IP = "127.0.0.1";
const char *PEER_PORT = "23155";
const char *MY_PORT = "23156";

int main(int argc, char **argv) {
    int gbnMode = 1;
    if(argc > 1 && argv[1][0] == 's')
        gbnMode = 0;
    if(gbnMode)
        gbnStart();
    else
        srStart();
}
