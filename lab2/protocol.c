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
    int timeoutCount = 0; // counter for timeout. too many timeout lead to terminating sending
    win->timer = TIMEOUT;
    do {
        // send data
        while((win->nextseqnum - win->base + SEQ_MAX_NUM) % SEQ_MAX_NUM < SND_WIN_SIZE
                && index < len) { // window is not full
            // create packet
            int seq = win->nextseqnum;
            int pktIndex = seq % SND_WIN_SIZE;
            if(win->sndpkt[pktIndex]) freeMessage(win->sndpkt[pktIndex]);
            win->sndpkt[pktIndex] = createMessage(seq, 0, index!=len-1, index==0, buf+index, 1);
            if(!win->sndpkt[pktIndex]) continue;
            if(++win->nextseqnum >= SEQ_MAX_NUM)
                win->nextseqnum -= SEQ_MAX_NUM;
            // send packet to peer
            int sndlen = writeMessageTo(win->sndpkt[pktIndex], sendbuf);
            sndlen = Sendto(peerfd, sendbuf, sndlen, 0, (struct sockaddr*)peerAddr, sizeof(struct sockaddr_in));
            if(sndlen < 0) {
                fprintf(stderr, "[SEND] Failed to send message of seq %d. Error code: %d\n", seq, errno);
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
            int acklen = Recvfrom(peerfd, ackbuf, sizeof(ackbuf), 0, NULL, NULL);
            if(acklen <= 0) break;
            // read packet
            message *msg = readMessageFrom(ackbuf, acklen);
            if(msg != NULL && IS_ACK(msg)) {
                if(win->base != (msg->seq + 1) % SEQ_MAX_NUM) {
                    timeoutCount = 0;
                    result += (msg->seq - win->base + 1 + SEQ_MAX_NUM) % SEQ_MAX_NUM;
                }
                // slide window and restart timer
                for(; win->base != (msg->seq + 1) % SEQ_MAX_NUM; win->base = (win->base + 1) % SEQ_MAX_NUM) {
                    int pktIndex = win->base % SND_WIN_SIZE;
                    if(win->sndpkt[pktIndex]) freeMessage(win->sndpkt[pktIndex]);
                    win->sndpkt[pktIndex] = NULL;
                }
                win->timer = TIMEOUT + 1; // timer will be TIMEOUT after decreasing it by 1
                printf("[SEND] Received ACK of seq %d\n", msg->seq);
            }
            if(msg) freeMessage(msg);
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
            // resend the unACKed packets
            for(int seq = win->base; seq != win->nextseqnum; seq = (seq + 1) % SEQ_MAX_NUM) {
                int pktIndex = seq % SND_WIN_SIZE;
                int sndlen = writeMessageTo(win->sndpkt[pktIndex], sendbuf);
                int err = Sendto(peerfd, sendbuf, sndlen, 0, (struct sockaddr*)peerAddr, sizeof(struct sockaddr_in));
                if(err < 0) {
                    fprintf(stderr, "[SEND] Failed to send message of seq %d. Error code: %d\n", seq, errno);
                    //continue;
                } else {
                    printf("[SEND] Sent message of seq %d: %c\n", seq, win->sndpkt[pktIndex]->data[0]);
                }
            }
        }
    } while(win->base != win->nextseqnum || index < len);
    win->base = win->nextseqnum; // clear window with no respect whether all data been sent
    if(result == len) puts("[SEND] All data has been sent.");
    else printf("[SEND] %d bytes of data has been sent.\n", result);
    return result;
}

int gbnRecv(gbnRcvWindow *win, int peerfd, struct sockaddr_in *peerAddr, char *buf, int size) {
    int len = 0;
    char recvbuf[MESSAGE_MAX_SIZE];
    char ackbuf[128];
    int lastFlag = 0;
    int timer = WAIT_TIMEOUT; // timer for waiting (see above)
    struct sockaddr_in tmpAddr;
    win->expectedseqnum = INVALID_SEQ; // receive message with any seq number is allowed
    tmpAddr.sin_family = AF_INET;
    tmpAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    tmpAddr.sin_port = 0;
    if(peerAddr == NULL) peerAddr = &tmpAddr;
    socklen_t addrlen = sizeof(struct sockaddr_in);
    while(1) {
        while(1) {
            int breakFlag = 0;
            message *msg = NULL;
            message *ackmsg = NULL;
            int acklen = 0;
            // receive data
            int rcvlen = Recvfrom(peerfd, recvbuf, sizeof(recvbuf), 0, (struct sockaddr*)peerAddr, &addrlen);
            if(rcvlen < 0) {
                if(errno != EAGAIN) {
                    fprintf(stderr, "[RECV] Failed to receive message. Error code: %d\n", errno);
                }
                breakFlag = 1;
                goto conti;
            }
            timer = WAIT_TIMEOUT;
            // read packet
            msg = readMessageFrom(recvbuf, rcvlen);
            if(msg == NULL) {
                fprintf(stderr, "[RECV] Invalid checksum.\n");
                goto conti;
            }
            printf("[RECV] Received message of seq %d: %c\n", msg->seq, msg->data[0]);
            if(win->expectedseqnum == INVALID_SEQ) { // waiting for the first package
                if(IS_FIRST(msg)) win->expectedseqnum = msg->seq;
                else {
                    fprintf(stderr, "[RECV] Not the first packet. Dropped.\n");
                    goto conti;
                }
            }
            if(msg->seq != win->expectedseqnum) {
                fprintf(stderr, "[RECV] Not the expected seq. Dropped.\n");
            } else {
                if(!HAS_MORE(msg)) lastFlag = 1;
                buf[len++] = msg->data[0];
                win->expectedseqnum = (msg->seq + 1) % SEQ_MAX_NUM;
            }
            // send ACK
            ackmsg = createMessage((win->expectedseqnum-1+SEQ_MAX_NUM)%SEQ_MAX_NUM, 1, 0, 0, NULL, 0);
            if(ackmsg) {
                acklen = writeMessageTo(ackmsg, ackbuf);
                acklen = Sendto(peerfd, ackbuf, acklen, 0, (struct sockaddr*)peerAddr, sizeof(struct sockaddr_in));
            }
            if(!ackmsg || acklen <= 0) {
                fprintf(stderr, "[RECV] Failed to send ACK of seq %d. Error code: %d\n", msg->seq, errno);
            }
    conti:
            if(msg) freeMessage(msg);
            if(ackmsg) freeMessage(ackmsg);
            if(breakFlag) break;
        }
        if(len >= size) break; // too much data
        // simulate timer
        if(timer-- <= 0) break;
        usleep(DELAY);
    }
    if(lastFlag) {
        puts("[RECV] All data has been received.");
        return len;
    } else if(win->expectedseqnum != INVALID_SEQ) {
        printf("[RECV] %d continuous bytes of data has been received.\n", len);
        return -len;
    } else
        return 0;
}

int srSend(srSndWindow *win, int peerfd, struct sockaddr_in *peerAddr, const char *buf, int len) {
    int index = 0;
    int result = 0;
    char sendbuf[MESSAGE_MAX_SIZE];
    char ackbuf[128];
    int timeoutCount[SND_WIN_SIZE]; // counter for timeout. too many timeout lead to terminating sending
    do {
        // send data
        while((win->nextseqnum - win->base + SEQ_MAX_NUM) % SEQ_MAX_NUM < SND_WIN_SIZE
                && index < len) { // window is not full
            // create packet
            int seq = win->nextseqnum++;
            int pktIndex = seq % SND_WIN_SIZE;
            if(win->nextseqnum >= SEQ_MAX_NUM)
                win->nextseqnum -= SEQ_MAX_NUM;
            if(win->sndpkt[pktIndex]) freeMessage(win->sndpkt[pktIndex]);
            win->sndpkt[pktIndex] = createMessage(seq, 0, index!=len-1, index==0, buf+index, 1);
            win->isrcved[pktIndex] = 0;
            win->timer[pktIndex] = TIMEOUT;
            timeoutCount[pktIndex] = 0;
            // send packet to peer
            int sndlen = writeMessageTo(win->sndpkt[pktIndex], sendbuf);
            sndlen = Sendto(peerfd, sendbuf, sndlen, 0, (struct sockaddr*)peerAddr, sizeof(struct sockaddr_in));
            if(sndlen < 0) {
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
            int acklen = Recvfrom(peerfd, ackbuf, sizeof(ackbuf), 0, NULL, NULL);
            if(acklen <= 0) break;
            // read packet
            message *msg = readMessageFrom(ackbuf, acklen);
            if(msg != NULL && IS_ACK(msg)) {
                if(!win->isrcved[msg->seq%SND_WIN_SIZE]) result++;
                win->isrcved[msg->seq%SND_WIN_SIZE] = 1;
                // slide window
                if(msg->seq == win->base) {
                    while(win->base != win->nextseqnum && win->isrcved[win->base%SND_WIN_SIZE])
                        win->base = (win->base + 1) % SEQ_MAX_NUM;
                }
                printf("[SEND] Received ACK for seq %d\n", msg->seq);
            }
            if(msg) freeMessage(msg);
        }
        if(win->base == win->nextseqnum && index == len) break; // all data has been sent
        // check the timers
        int allTimeoutFlag = win->base != win->nextseqnum; // whether all packets been tried too many times
        for(int seq = win->base; seq != win->nextseqnum; seq = (seq + 1) % SEQ_MAX_NUM) {
            int pktIndex = seq % SND_WIN_SIZE;
            if(win->isrcved[pktIndex]) continue;
            if(timeoutCount[pktIndex] < TIMEOUT_MAX_COUNT) allTimeoutFlag = 0;
            if(timeoutCount[pktIndex] <= TIMEOUT_MAX_COUNT && --win->timer[pktIndex] == 0) {
                win->timer[pktIndex] = TIMEOUT;
                if(timeoutCount[pktIndex]++ >= TIMEOUT_MAX_COUNT) {
                    printf("[SEND] Too many tries for seq %d. Stop sending.\n", seq);
                    continue;
                } else {
                    printf("[SEND] TIMEOUT for seq %d. Trying to send again.\n", seq);
                }
                // resend the packet
                int sndlen = writeMessageTo(win->sndpkt[pktIndex], sendbuf);
                int err = Sendto(peerfd, sendbuf, sndlen, 0, (struct sockaddr*)peerAddr, sizeof(struct sockaddr_in));
                if(err < 0) {
                    fprintf(stderr, "[SEND] Failed to send message. Error code: %d\n", err);
                    //continue;
                } else {
                    printf("[SEND] Sent message of seq %d: %c\n", seq, win->sndpkt[pktIndex]->data[0]);
                }
            }
        }
        if(allTimeoutFlag) {
            puts("[SEND] Too many tries. Stop sending.");
            break;
        }
    } while(win->base != win->nextseqnum || index < len);
    win->base = win->nextseqnum; // clear window with no respect whether all data been sent
    if(result == len) puts("[SEND] All data has been sent.");
    else printf("[SEND] %d bytes of data has been sent.\n", result);
    return result;
}

int srRecv(srRcvWindow *win, int peerfd, struct sockaddr_in *peerAddr, char *buf, int size) {
    int len = 0;
    int firstFlag = 0; // whether received the first packet
    int lastSeq = -1; // seq of the last packet
    char recvbuf[MESSAGE_MAX_SIZE];
    char ackbuf[128];
    int timer = WAIT_TIMEOUT; // used to avoid endless waiting
    struct sockaddr_in tmpAddr;
    for(int i = 0; i < RCV_WIN_SIZE; i++) {
        if(win->rcvpkt[i]) {
            freeMessage(win->rcvpkt[i]);
            win->rcvpkt[i] = NULL;
        }
    }
    tmpAddr.sin_family = AF_INET;
    tmpAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    tmpAddr.sin_port = 0;
    if(peerAddr == NULL) peerAddr = &tmpAddr;
    socklen_t addrlen = sizeof(struct sockaddr_in);
    while(1) {
        while(1) {
            int breakFlag = 0;
            message *msg = NULL;
            message *ackmsg = NULL;
            int acklen = 0;
            // receive data
            int rcvlen = Recvfrom(peerfd, recvbuf, sizeof(recvbuf), 0, (struct sockaddr*)peerAddr, &addrlen);
            if(rcvlen < 0) {
                if(errno != EAGAIN) {
                    fprintf(stderr, "[RECV] Failed to receive message. Error code: %d\n", errno);
                }
                breakFlag = 1;
                goto conti;
            }
            timer = WAIT_TIMEOUT;
            // read packet
            msg = readMessageFrom(recvbuf, rcvlen);
            if(msg == NULL) {
                fprintf(stderr, "[RECV] Invalid checksum.\n");
                goto conti;
            }
            printf("[RECV] Received message of seq %d: %c\n", msg->seq, msg->data[0]);
            if(!firstFlag) { // waiting for the first package
                if(IS_FIRST(msg)) {
                    firstFlag = 1;
                    win->base = msg->seq;
                }
                else {
                    fprintf(stderr, "[RECV] Not the first packet. Dropped.\n");
                    goto conti;
                }
            }
            if(((int)win->base - (int)msg->seq + SEQ_MAX_NUM) % SEQ_MAX_NUM <= RCV_WIN_SIZE
                    || ((int)msg->seq - (int)win->base + SEQ_MAX_NUM) % SEQ_MAX_NUM < RCV_WIN_SIZE) {
                printf("[RECV] Sent ACK of seq %d\n", msg->seq);
                // save packet
                if(((int)msg->seq - (int)win->base + SEQ_MAX_NUM) % SEQ_MAX_NUM < RCV_WIN_SIZE) {
                    int pktIndex = msg->seq % RCV_WIN_SIZE;
                    if(win->rcvpkt[pktIndex]) freeMessage(win->rcvpkt[pktIndex]);
                    win->rcvpkt[pktIndex] = msg;
                }
                // check if it's the last packet
                if(!HAS_MORE(msg)) {
                    lastSeq = msg->seq;
                }
                // send ACK
                ackmsg = createMessage(msg->seq, 1, 0, 0, NULL, 0);
                acklen = writeMessageTo(ackmsg, ackbuf);
                acklen = Sendto(peerfd, ackbuf, acklen, 0, (struct sockaddr*)peerAddr, sizeof(struct sockaddr_in));
                if(acklen <= 0) {
                    fprintf(stderr, "[RECV] Failed to send ACK of seq %d. Error code: %d\n", msg->seq, errno);
                }
                // slide window
                if(msg->seq == win->base) { // in order
                    while(win->rcvpkt[win->base % RCV_WIN_SIZE]) {
                        int pktIndex = win->base % RCV_WIN_SIZE;
                        buf[len++] = win->rcvpkt[pktIndex]->data[0];
                        win->base = (win->base + 1) % SEQ_MAX_NUM;
                        freeMessage(win->rcvpkt[pktIndex]);
                        win->rcvpkt[pktIndex] = NULL;
                    }
                }
            }
conti:
            if(ackmsg) freeMessage(ackmsg);
            if(len >= size) break; // too much data
            if(breakFlag) break;
        }
        if(len >= size) break;
        // simulate timer
        if(timer-- <= 0) break;
        usleep(DELAY);
    }
    if(lastSeq >= 0 && (int)win->base == (lastSeq + 1) % SEQ_MAX_NUM) {
        puts("[RECV] All data has been received.");
        return len;
    } else if(firstFlag) {
        printf("[RECV] %d continuous bytes of data has been received.\n", len);
        return -len;
    } else
        return 0;
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
