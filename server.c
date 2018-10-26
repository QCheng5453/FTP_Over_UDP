// server code for UDP socket programming
#include "headerStruct.h"

#define NET_BUF_SIZE 1420
#define SEQ_NUM 8
#define notfound "File Not Found"

long memSize;
float old_speed = 0;
static float display_progress(float percentage, time_t inter, u_int8_t style) {
    char *bar = (char *)malloc(sizeof(char) * (100));
    char time_buf[10];
    u_int8_t rounded_p = (u_int8_t)(percentage);
    for (u_int8_t i = 0; i < rounded_p / 2; i++) {
        bar[i] = '+';
        printf("%s+", "\x1B[32m");
    }
    for (u_int8_t i = 0; i < 50 - rounded_p / 2; i++) {
        bar[i] = '-';
        printf("%s-", "\x1B[0m");
    }
    printf("%s", "\x1B[0m");
    /*printf("%.*s %5.1f %%  ", 50, bar, percentage);*/
    printf("  %5.1f %%  ", percentage);
    strftime(time_buf, 7, "%M:%S", localtime(&inter));
    printf("%.*s", 5, time_buf);

    struct tm inter_time_tm = *localtime(&inter);
    int inter_time = inter_time_tm.tm_min * 60 + inter_time_tm.tm_sec;
    float cur_speed = memSize * percentage / 100 / 1000.0 / 1000 / inter_time * 8;
    if (cur_speed - old_speed > 1 || cur_speed - old_speed < -1) {
        old_speed = cur_speed;
        printf("  %4.1f Mbps", cur_speed);
    } else {
        printf("  %4.1f Mbps", old_speed);
    }
    printf("  \r");
    fflush(stdout);
    return percentage;
}

long missArrLen = 1000000;
u_int8_t missArr[1000000];
long missArrPtr = 0;
long missArrCnt = 0;
long missArrLowPtr = 0;

long file_size = 0;

// funtion to clear buffer
void clearBuf(char *b, int size) {
    int i;
    for (i = 0; i < size; i++)
        b[i] = '\0';
}

int lastPacket(char *buf) {
    return ((struct headerStruct *)buf)->flag;
}

struct sock_th {
    int sock;
    struct sockaddr_in client;
    socklen_t length;
};

static struct sock_th st;

// void printMissArr() {
//     for (int i = 0; i < missArrPtr + 1; i++) {
//         if (missArr[i] >= 0) {
//             printf("Loc %d: %ld\n", i, missArr[i]);
//         }
//     }
//     // printf("------Missing Array-------//\n");
// }

int checkMissArr(long seq) {
    return missArr[seq];
}

int addMissArr(long seq) {
    missArr[seq] = 1;
    if (seq > missArrPtr) missArrPtr = seq;
    missArrCnt += 1;
    return 0;
}

int rmMissArr(long seq) {
    missArr[seq] = 0;
    missArrCnt -= 1;
    if (seq == missArrLowPtr) missArrLowPtr ++;
    if (seq == missArrPtr) missArrPtr --;
    // printMissArr();
    return 0;
}

int missArrEmpty() {
    if (missArrCnt == 0) {
        return 1;
    }
    return 0;
}

int writeToMem(long seq, char *memLoc, char *payload) {
    int size = ((struct headerStruct *)payload)->payloadSize;
    memcpy(memLoc + seq * (long)NET_BUF_SIZE, payload + sizeof(struct headerStruct), size);
    return 0;
}

static long fifo_buf_size = 100000;
long *fifo_buf;
long fifo_recv_ptr = 0;
long fifo_send_ptr = 0;

pthread_mutex_t th_lock = PTHREAD_MUTEX_INITIALIZER;

void *myThreadFun(void *v) {
    char *buf = (char *)malloc(8);
    long seq;
   bool order_flag = false;
    while (1) {
        //pthread_mutex_lock(&th_lock);
        seq = fifo_buf[fifo_send_ptr++];
        if (fifo_send_ptr >= fifo_buf_size)
            fifo_send_ptr = 0;
        // pthread_mutex_lock(&th_lock);
        if (fifo_send_ptr == fifo_recv_ptr) {
           if (order_flag) {
                order_flag = false;
                for (long i = missArrLowPtr; i <= missArrPtr; i++) {
                    if (missArr[i] == 1) {
                        fifo_buf[fifo_recv_ptr++] = i;
                        if (fifo_recv_ptr >= fifo_buf_size)
                            fifo_recv_ptr = 0;
                    }
                }
           } 
            else {
                order_flag = true;
                for (long i = missArrPtr; i >= missArrLowPtr; i--) {
                    if (missArr[i] == 1) {
                        fifo_buf[fifo_recv_ptr++] = i;
                        if (fifo_recv_ptr >= fifo_buf_size)
                            fifo_recv_ptr = 0;
                    }
                }
            }
            usleep(150);
        }
        // pthread_mutex_unlock(&th_lock);
        sprintf(buf, "%ld", seq);

        for (int i = 0; i < 1; i++) {
            sendto(st.sock, buf, SEQ_NUM, 0, (struct sockaddr *)&st.client, st.length);
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    setpriority(PRIO_PROCESS, 0, -20);
    int sock, length, nBytes, totalSeq, lastPayloadSize;
    struct sockaddr_in server;
    struct sockaddr_in from;
    socklen_t fromlen;
    int addrlen = sizeof(server);
    char net_buf[NET_BUF_SIZE + sizeof(struct headerStruct)];

    fifo_buf = (long *)malloc(sizeof(long) * fifo_buf_size);

    for (long i = 0; i < missArrLen; i++) {
        missArr[i] = 0;
    }

    if (argc < 2) {
        fprintf(stderr, "ERROR, no port provided\n");
        exit(0);
    }

    /* SOCKET INIT */
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        printf("Error in socket()\n");
    }
    length = sizeof(server);
    bzero(&server, length);
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(atoi(argv[1]));
    if (bind(sock, (struct sockaddr *)&server, length) < 0) {
        printf("Error in bind()\n");
    }
    /* SOCKET INIT END */

    fromlen = sizeof(struct sockaddr_in);

    long prevseq = -1;
    int lastFlag = 0;

    //Receive Packet
    // first packet might miss, but anyway
    int payLen = recvfrom(sock, net_buf, NET_BUF_SIZE + sizeof(struct headerStruct), 0, (struct sockaddr *)&from, &fromlen);

    st.sock = sock;
    st.client = from;
    st.length = fromlen;

    struct headerStruct packetHeader;
    memcpy(&packetHeader, net_buf, sizeof(struct headerStruct));

    totalSeq = packetHeader.totalSeqnum;

    //Allocate MEMORY
    memSize = (long)(packetHeader.totalSeqnum + 1) * (long)packetHeader.payloadSize;
    long seqnum = packetHeader.seqnum;
    file_size += packetHeader.payloadSize * packetHeader.totalSeqnum;
    char *fileMemBuf = (char *)malloc(memSize);

    pthread_t thread_id;

    // printMissArr();
    time_t start_time = time(NULL);
    printf("-----------------start receiving------------------\n");

    // int first = 1;
    bool first_flag = true;
    bool cre_first_flag = true;
    while (1) {
        //Receive Packet
        int payLen = recvfrom(sock, net_buf, NET_BUF_SIZE + sizeof(struct headerStruct), 0, (struct sockaddr *)&from, &fromlen);

        seqnum = ((struct headerStruct *)net_buf)->seqnum;

        if ((totalSeq - seqnum + 0.0) / totalSeq < 0.1 && cre_first_flag) {
            cre_first_flag = false;
            pthread_create(&thread_id, NULL, myThreadFun, NULL);
        }
        // if it's a missing packet
        if (seqnum < prevseq) {
            int found = checkMissArr(seqnum);
            if (found) {
                writeToMem(seqnum, fileMemBuf, net_buf);
                rmMissArr(seqnum);
            }
            // int found = checkMissArr(seqnum);
            // if (found) {

            //     rmMissArr(seqnum);
            // }
        } else if (seqnum > prevseq) {
            if (seqnum - prevseq > 1) {
                int difference = seqnum - prevseq;
                for (int i = 1; i < difference; i++) {
                    addMissArr(prevseq + 1);
                    // pthread_mutex_lock(&th_lock);
                    fifo_buf[fifo_recv_ptr++] = prevseq + 1;
                    if (fifo_recv_ptr >= fifo_buf_size)
                        fifo_recv_ptr = 0;
                    // pthread_mutex_unlock(&th_lock);
                    prevseq += 1;
                }
            }
            //No packets were lost, expected seq arrived
            prevseq += 1;
            writeToMem(seqnum, fileMemBuf, net_buf);
        }

        if (lastPacket(net_buf) == 1 && first_flag) {
            first_flag = false;
            // printf("GGGGot last Flag\n");
            lastFlag = 1;  //Got last packet (Still may have misses)
            lastPayloadSize = ((struct headerStruct *)net_buf)->payloadSize;
            file_size += lastPayloadSize;
            // break;
        }

        if (lastFlag == 1 && missArrEmpty()) {
            char *buf = (char *)malloc(8);
            long a = -1;
            sprintf(buf, "%ld", a);
            for (int i = 0; i < 6; i++) {
                sendto(st.sock, buf, SEQ_NUM, 0, (struct sockaddr *)&st.client, st.length);
            }
            time_t cur_time = time(NULL);
            float per = (prevseq - missArrCnt + 0.0) / totalSeq * 100;
            display_progress(per, cur_time - start_time, 1);
            break;  //Got last data
        }

        //display progress
        time_t cur_time = time(NULL);
        float per = (prevseq - missArrCnt + 0.0) / totalSeq * 100;
        display_progress(per, cur_time - start_time, 1);
    }

    time_t end_time = time(NULL);
    struct tm end_tm = *localtime(&end_time);
    printf("\nend time is: %d:%d:%d \n", end_tm.tm_hour, end_tm.tm_min, end_tm.tm_sec);

    time_t inter_time = end_time - start_time;
    struct tm total_time_tm = *localtime(&inter_time);
    int total_time = total_time_tm.tm_min * 60 + total_time_tm.tm_sec;
    float speed = file_size / 1000.0 / 1000 / total_time * 8;
    printf("%ld Bytes data have received, Total time is %d sec, average speed is %5.2f Mbps\n", file_size, total_time, speed);

    //Write to file
    FILE *fp;
    fp = fopen("/tmp/new.bin", "wb");
    if (fp == NULL)
        printf("\nFile open failed!\n");
    else
        printf("\nnew.bin successfully created!\n");
    fwrite(fileMemBuf, 1, NET_BUF_SIZE * totalSeq + lastPayloadSize, fp);

    if (fclose(fp)) {
        printf("fclose() error");
        exit(-1);
    }

    printf("The received data file md5: \n");
    system("md5sum /tmp/new.bin");
    return 0;
}
