#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <arpa/inet.h>
#include "message.h"

// WARNING: window size must be power of 2
#define SND_WIN_SIZE (1<<10)
#define RCV_WIN_SIZE (1<<10)

#define TIMEOUT 5
#define TIMEOUT_MAX_COUNT 10
#define WAIT_TIMEOUT 15
#define DELAY 100000

typedef struct gbnSndWindow {
    message *sndpkt[SND_WIN_SIZE];
    int base;
    int nextseqnum;
    int timer;
} gbnSndWindow;

typedef struct gbnRcvWindow {
    int expectedseqnum;
} gbnRcvWindow;

typedef struct srSndWindow {
    message *sndpkt[SND_WIN_SIZE];
    int isrcved[SND_WIN_SIZE];
    int timer[SND_WIN_SIZE];
    int base;
    int nextseqnum;
} srSndWindow;

typedef struct srRcvWindow {
    message *rcvpkt[RCV_WIN_SIZE];
    int isrcved[RCV_WIN_SIZE];
    int base;
} srRcvWindow;

gbnSndWindow *createGbnSndWindow();
gbnRcvWindow *createGbnRcvWindow();
srSndWindow *createSrSndWindow();
srRcvWindow *createSrRcvWindow();

int gbnSend(gbnSndWindow *win, int peerfd, struct sockaddr_in *peerAddr, const char *buf, int len);
int gbnRecv(gbnRcvWindow *win, int peerfd, struct sockaddr_in *peerAddr, char *buf, int size);
int srSend(srSndWindow *win, int peerfd, struct sockaddr_in *peerAddr, const char *buf, int len);
int srRecv(srRcvWindow *win, int peerfd, struct sockaddr_in *peerAddr, char *buf, int size);

void freeGbnSndWindow(gbnSndWindow *win);
void freeGbnRcvWindow(gbnRcvWindow *win);
void freeSrSndWindow(srSndWindow *win);
void freeSrRcvWindow(srRcvWindow *win);

#endif
