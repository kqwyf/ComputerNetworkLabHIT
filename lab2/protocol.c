#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "protocol.h"

gbnSndWindow *createGbnSndWindow() {
    gbnSndWindow *win = (gbnSndWindow*) malloc(sizeof(gbnSndWindow));
    memset(win->sndpkt, 0, sizeof(message*)*SND_WIN_SIZE);
    win->base = win->nextseqnum = 1;
    win->timer = 0;
    return win;
}

gbnRcvWindow *createGbnRcvWindow() {
    gbnRcvWindow *win = (gbnRcvWindow*) malloc(sizeof(gbnRcvWindow));
    win->expectedseqnum = 0;
    return win;
}

srSndWindow *createSrSndWindow() {
    srSndWindow *win = (srSndWindow*) malloc(sizeof(srSndWindow));
    memset(win->isrcved, 0, sizeof(int)*SND_WIN_SIZE);
    memset(win->timer, 0, sizeof(int)*SND_WIN_SIZE);
    win->base = win->nextseqnum = 0;
    return win;
}

srRcvWindow *createSrRcvWindow() {
    srRcvWindow *win = (srRcvWindow*) malloc(sizeof(srRcvWindow));
    memset(win->isrcved, 0, sizeof(int)*RCV_WIN_SIZE);
    win->base = 0;
    return win;
}

int gbnSend(gbnSndWindow *win, int peerfd, sockaddr_in *peerAddr, const char *buf, int len) {
    
}

int gbnRecv(gbnRcvWindow *win, int peerfd, sockaddr_in *peerAddr, char *buf, int size) {
}

int srSend(srSndWindow *win, int peerfd, sockaddr_in *peerAddr, const char *buf, int len) {
}

int srRecv(srRcvWindow *win, int peerfd, sockaddr_in *peerAddr, char *buf, int size) {
}

void freeGbnSndWindow(gbnSndWindow *win) {
    for(int i = 0; i < SND_WIN_SIZE; i++)
        if(win->sndpkt[i])
            free(win->sndpkt[i]);
    free(win);
}

void freeGbnRcvWindow(gbnRcvWindow *win) {
    free(win);
}

void freeSrSndWindow(srSndWindow *win) {
    for(int i = 0; i < SND_WIN_SIZE; i++)
        if(win->sndpkt[i])
            free(win->sndpkt[i]);
    free(win);
}

void freeSrRcvWindow(srRcvWindow *win) {
    for(int i = 0; i < RCV_WIN_SIZE; i++)
        if(win->rcvpkt[i])
            free(win->rcvpkt[i]);
    free(win);
}
