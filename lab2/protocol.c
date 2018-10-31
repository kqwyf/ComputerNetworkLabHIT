#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "protocol.h"

gbnSndWindow *createGbnSndWindow() {
    gbnSndWindow *win = (gbnSndWindow*) malloc(sizeof(gbnSndWindow));
    memset(win->sndpkt, 0, sizeof(message*)*SND_WIN_SIZE);
    win->base = win->nextseqnum = 0;
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

int gbnSend(gbnSndWindow *win, int peerfd, struct sockaddr_in *peerAddr, const char *buf, int len) {
    int index = 0;
    char sendbuf[MESSAGE_MAX_SIZE];
    char ackbuf[128];
    do {
        if(win->base == win->nextseqnum)
            win->timer = TIMEOUT;
        while(win->base != win->nextseqnum + 1 && index < len) { // window is not full
            int i = win->nextseqnum++;
            if(win->nextseqnum >= SND_WIN_SIZE)
                win->nextseqnum -= SND_WIN_SIZE;
            if(win->sndpkt[i]) freeMessage(win->sndpkt[i]);
            win->sndpkt[i] = createMessage(i, 0, buf+index, 1);
            int len = writeMessageTo(win->sndpkt[i], sendbuf);
            len = sendto(peerfd, sendbuf, len, 0, (struct sockaddr*)peerAddr, sizeof(struct sockaddr_in));
            if(len < 0) {
                fprintf(stderr, "[SEND] Failed to send message. Error code: %d\n", errno);
                //continue;
            } else {
                printf("[SEND] Sent message of seq %d: %c\n", i, win->sndpkt[i]->data[0]);
            }
            index++;
        }
        usleep(DELAY);
        if(--win->timer == 0) { // timeout
            win->timer = TIMEOUT;
            puts("[SEND] TIMEOUT. Trying to send again.");
            for(int i = win->base; i != win->nextseqnum;) {
                if(i >= SND_WIN_SIZE) i -= SND_WIN_SIZE;
                int len = writeMessageTo(win->sndpkt[i], sendbuf);
                int err = sendto(peerfd, sendbuf, len, 0, (struct sockaddr*)peerAddr, sizeof(struct sockaddr_in));
                if(err < 0) {
                    fprintf(stderr, "[SEND] Failed to send message. Error code: %d\n", err);
                    //continue;
                } else {
                    printf("[SEND] Sent message of seq %d: %c\n", i, win->sndpkt[i]->data[0]);
                }
                i++;
            }
        } else {
            int len = recvfrom(peerfd, ackbuf, sizeof(ackbuf), 0, NULL, NULL);
            if(len <= 0) continue;
            printf("[SEND] Received something like ACK?");
            message *msg = readMessageFrom(ackbuf, len);
            if(msg != NULL && IS_ACK(msg)) {
                win->base = msg->seq + 1;
                if(win->base != win->nextseqnum)
                    win->timer = TIMEOUT;
                printf("[SEND] Received ACK for seq: %d\n", msg->seq);
            }
        }
    } while(win->base != win->nextseqnum);
    puts("[SEND] All data has been sent.");
    return 0;
}

int gbnRecv(gbnRcvWindow *win, int peerfd, struct sockaddr_in *peerAddr, char *buf, int size) {
    int timeouted = 0;
    int len = 0;
    char recvbuf[MESSAGE_MAX_SIZE];
    char ackbuf[128];
    while(1) {
        int recvlen = recvfrom(peerfd, recvbuf, sizeof(recvbuf), 0, NULL, NULL);
        if(recvlen < 0 && errno == EAGAIN) {
            if(timeouted) break; // receiving finished
            timeouted = 1;
            usleep(DELAY);
            continue;
        }
        message *msg = readMessageFrom(recvbuf, recvlen);
        if(msg == NULL) {
            fprintf(stderr, "[RECV] Invalid checksum.\n");
            usleep(DELAY);
            continue;
        }
        printf("[RECV] Received message of seq %d: %c\n", msg->seq, msg->data[0]);
        if(msg->seq != win->expectedseqnum) {
            fprintf(stderr, "[RECV] Not the expected seq. Dropped.\n");
            usleep(DELAY);
            continue;
        }
        buf[len++] = msg->data[0];
        win->expectedseqnum = (win->expectedseqnum + 1) % RCV_WIN_SIZE;
        message *ackmsg = createMessage(msg->seq, 1, NULL, 0);
        int acklen = writeMessageTo(msg, ackbuf);
        acklen = sendto(peerfd, ackbuf, acklen, 0, (struct sockaddr*)peerAddr, sizeof(struct sockaddr_in));
        if(acklen <= 0) {
            fprintf(stderr, "[RECV] Failed to send ACK of seq %d. Error code: %d\n", msg->seq, errno);
        }
        freeMessage(msg);
        freeMessage(ackmsg);
        if(len >= size) break;
        usleep(DELAY);
    }
    return len;
}

int srSend(srSndWindow *win, int peerfd, struct sockaddr_in *peerAddr, const char *buf, int len) {
    return 0;
}

int srRecv(srRcvWindow *win, int peerfd, struct sockaddr_in *peerAddr, char *buf, int size) {
    int len = 0;
    return len;
}

void freeGbnSndWindow(gbnSndWindow *win) {
    for(int i = 0; i < SND_WIN_SIZE; i++)
        if(win->sndpkt[i])
            freeMessage(win->sndpkt[i]);
    free(win);
}

void freeGbnRcvWindow(gbnRcvWindow *win) {
    free(win);
}

void freeSrSndWindow(srSndWindow *win) {
    for(int i = 0; i < SND_WIN_SIZE; i++)
        if(win->sndpkt[i])
            freeMessage(win->sndpkt[i]);
    free(win);
}

void freeSrRcvWindow(srRcvWindow *win) {
    for(int i = 0; i < RCV_WIN_SIZE; i++)
        if(win->rcvpkt[i])
            freeMessage(win->rcvpkt[i]);
    free(win);
}
