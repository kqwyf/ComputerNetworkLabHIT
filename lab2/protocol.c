#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "protocol.h"
#include "channel.h"

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
    int result = 0;
    char sendbuf[MESSAGE_MAX_SIZE];
    char ackbuf[128];
    int timeoutCount = 0;
    win->timer = TIMEOUT;
    do {
        // send data
        while((win->nextseqnum - win->base + SEQ_MAX_NUM) % SEQ_MAX_NUM <= SND_WIN_SIZE
                && index < len) { // window is not full
            int seq = win->nextseqnum++;
            int pktIndex = seq % SND_WIN_SIZE;
            if(win->nextseqnum >= SEQ_MAX_NUM)
                win->nextseqnum -= SEQ_MAX_NUM;
            if(win->sndpkt[pktIndex]) freeMessage(win->sndpkt[pktIndex]);
            win->sndpkt[pktIndex] = createMessage(seq, 0, index!=len-1, index==0, buf+index, 1);
            int len = writeMessageTo(win->sndpkt[pktIndex], sendbuf);
            len = Sendto(peerfd, sendbuf, len, 0, (struct sockaddr*)peerAddr, sizeof(struct sockaddr_in));
            if(len < 0) {
                fprintf(stderr, "[SEND] Failed to send message. Error code: %d\n", errno);
                //continue;
            } else {
                printf("[SEND] Sent message of seq %d: %c\n", seq, win->sndpkt[pktIndex]->data[0]);
            }
            index++;
        }
        // simulate timer
        usleep(DELAY);
        // receive ACKs
        while(1) {
            int len = Recvfrom(peerfd, ackbuf, sizeof(ackbuf), 0, NULL, NULL);
            if(len <= 0) break;
            message *msg = readMessageFrom(ackbuf, len);
            if(msg != NULL && IS_ACK(msg)) {
                if(win->base != (msg->seq + 1) % SEQ_MAX_NUM) {
                    timeoutCount = 0;
                    result += (msg->seq - win->base + 1 + SEQ_MAX_NUM) % SEQ_MAX_NUM;
                }
                win->base = (msg->seq + 1) % SEQ_MAX_NUM;
                win->timer = TIMEOUT + 1; // timer will be TIMEOUT after decreasing it by 1
                printf("[SEND] Received ACK for seq: %d\n", msg->seq);
            }
        }
        if(win->base == win->nextseqnum && index == len) // all data has been sent
            break;
        // check the timer
        if(--win->timer == 0) { // timeout
            win->timer = TIMEOUT;
            if(timeoutCount++ >= TIMEOUT_MAX_COUNT) {
                puts("[SEND] Too many tries. Stop sending.");
                break;
            } else {
                puts("[SEND] TIMEOUT. Trying to send again.");
            }
            for(int seq = win->base; seq != win->nextseqnum; seq = (seq + 1) % SEQ_MAX_NUM) {
                int pktIndex = seq % SND_WIN_SIZE;
                int len = writeMessageTo(win->sndpkt[pktIndex], sendbuf);
                int err = Sendto(peerfd, sendbuf, len, 0, (struct sockaddr*)peerAddr, sizeof(struct sockaddr_in));
                if(err < 0) {
                    fprintf(stderr, "[SEND] Failed to send message. Error code: %d\n", err);
                    //continue;
                } else {
                    printf("[SEND] Sent message of seq %d: %c\n", seq, win->sndpkt[pktIndex]->data[0]);
                }
            }
        }
    } while(win->base != win->nextseqnum);
    win->base = win->nextseqnum; // clear window with no respect whether all data been sent
    if(result == len) puts("[SEND] All data has been sent.");
    else printf("[SEND] %d bytes of data has been sent.\n", result);
    return result;
}

int gbnRecv(gbnRcvWindow *win, int peerfd, struct sockaddr_in *peerAddr, char *buf, int size) {
    int len = 0;
    char recvbuf[MESSAGE_MAX_SIZE];
    char ackbuf[128];
    int waitFlag = 0;
    int timer = TIMEOUT * TIMEOUT_MAX_COUNT;
    struct sockaddr_in tmpAddr;
    win->expectedseqnum = INVALID_SEQ; // receive message with any seq number is allowed
    tmpAddr.sin_family = AF_INET;
    tmpAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    tmpAddr.sin_port = 0;
    socklen_t addrlen = sizeof(struct sockaddr_in);
    while(1) {
        message *msg = NULL;
        message *ackmsg = NULL;
        if(peerAddr == NULL) peerAddr = &tmpAddr;
        int acklen = 0;
        int recvlen = Recvfrom(peerfd, recvbuf, sizeof(recvbuf), 0, (struct sockaddr*)peerAddr, &addrlen);
        if(recvlen < 0) {
            if(errno == EAGAIN) {
                if(timer-- <= 0)
                    waitFlag = 1; // free resources and break
            } else {
                fprintf(stderr, "[RECV] Failed to receive message. Error code: %d\n", errno);
            }
            goto conti;
        }
        timer = TIMEOUT * TIMEOUT_MAX_COUNT;
        msg = readMessageFrom(recvbuf, recvlen);
        if(msg == NULL) {
            fprintf(stderr, "[RECV] Invalid checksum.\n");
            goto conti;
        }
        printf("[RECV] Received message of seq %d: %c\n", msg->seq, msg->data[0]);
        if(win->expectedseqnum == INVALID_SEQ) {
            if(IS_FIRST(msg)) win->expectedseqnum = msg->seq;
            else {
                fprintf(stderr, "[RECV] Not the first packet. Dropped.\n");
                goto conti;
            }
        }
        if(msg->seq != win->expectedseqnum) {
            fprintf(stderr, "[RECV] Not the expected seq. Dropped.\n");
        } else {
            waitFlag = !HAS_MORE(msg);
            buf[len++] = msg->data[0];
            win->expectedseqnum = (msg->seq + 1) % SEQ_MAX_NUM;
        }
        ackmsg = createMessage((win->expectedseqnum-1+SEQ_MAX_NUM)%SEQ_MAX_NUM, 1, 0, 0, NULL, 0);
        acklen = writeMessageTo(ackmsg, ackbuf);
        acklen = Sendto(peerfd, ackbuf, acklen, 0, (struct sockaddr*)peerAddr, sizeof(struct sockaddr_in));
        if(acklen <= 0) {
            fprintf(stderr, "[RECV] Failed to send ACK of seq %d. Error code: %d\n", msg->seq, errno);
        }
conti:
        if(msg) freeMessage(msg);
        if(ackmsg) freeMessage(ackmsg);
        if(waitFlag && timer-- <= 0) break; // receiving finished
        if(len >= size) break; // too much data
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
