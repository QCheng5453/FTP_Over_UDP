// client code for UDP socket programming
#include "headerStruct.h"

#define NET_BUF_SIZE 1420
#define SEQ_NUM 8
#define notfound "File Not Found"

// funtion to clear buffer
void clearBuf(char *b, int size) {
    int i;
    for (i = 0; i < size; i++)
        b[i] = '\0';
}

char file_name[20];

// funtion sending file
int fillBufFromMem(char *fileMemBuf, char *buf, u_int32_t seq, u_int32_t totalSeqNum, u_int16_t payloadSize, u_int16_t flag) {
    struct headerStruct packetHeader;
    packetHeader.seqnum = seq;
    packetHeader.totalSeqnum = totalSeqNum;
    packetHeader.payloadSize = payloadSize;
    packetHeader.flag = flag;
    int i, len;

    char *loc = fileMemBuf + seq * NET_BUF_SIZE;

    //Prepend sequence number
    memcpy(buf, &packetHeader, sizeof(struct headerStruct));
    memcpy(buf + sizeof(struct headerStruct), loc, payloadSize);

    // printf("in fillbuffer fun payload size = %ld\n", ((struct headerStruct*)buf)->payloadSize);
    // printf("in fillbuffer fun packet size = %ld\n", ((struct headerStruct*)buf)->payloadSize + sizeof(struct headerStruct));
    // printf("in fillbuffer fun payload = %.*s\n", ((struct headerStruct*)buf)->payloadSize, buf+sizeof(struct headerStruct));

    return 0;
}

struct sock_th {
    int sock;
    struct sockaddr_in server;
    char *fileLoc;
    u_int32_t lastSeq;
};

static struct sock_th st;

static long fifo_buf_size = 100000;
long *fifo_buf;
long fifo_recv_ptr = 0;
long fifo_send_ptr = 0;

pthread_mutex_t th_lock = PTHREAD_MUTEX_INITIALIZER;

void *myThreadFun(void *v) {
    char buf[SEQ_NUM];
    struct sockaddr_in from;
    int length;
    socklen_t fromlen;
    char net_buf[NET_BUF_SIZE + sizeof(struct headerStruct)];

    while (1) {
        int recvLen = recvfrom(st.sock, buf, SEQ_NUM, 0, (struct sockaddr *)&from, &fromlen);
        //Convert seqnum
        long seqnum;
        sscanf(buf, "%ld", &seqnum);

        pthread_mutex_lock(&th_lock);
        fifo_buf[fifo_recv_ptr++] = seqnum;
        if (fifo_recv_ptr >= fifo_buf_size)
            fifo_recv_ptr = 0;
        if(fifo_recv_ptr == fifo_send_ptr)
            usleep(150);
        pthread_mutex_unlock(&th_lock);

        if (seqnum == -1) {
            // end of program
            printf("The original data file md5: \n");
            char command[30];
            strcpy(command, "md5sum ");
            strcat(command, file_name);
            system(command);
            exit(0);
        }
        // fillBufFromMem(st.fileLoc, net_buf, seqnum, st.lastSeq, NET_BUF_SIZE, 0);
        // // for (int i = 0; i < 1; i++) {
        // sendto(st.sock, net_buf, NET_BUF_SIZE + sizeof(struct headerStruct), 0, (struct sockaddr *)&st.server, sizeof(struct sockaddr_in));
        // // }
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    setpriority(PRIO_PROCESS, 0, -20);
    int sock, length, nBytes;
    struct sockaddr_in server;
    struct hostent *hp;
    char net_buf[NET_BUF_SIZE + sizeof(struct headerStruct)];
    FILE *fp;

    fifo_buf = (long *)malloc(sizeof(long) * fifo_buf_size);
    /* SOCKET INIT */
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        printf("Error in socket()\n");

    server.sin_family = AF_INET;
    hp = gethostbyname(argv[1]);
    if (hp == 0)
        printf("Error: unknown host\n");

    bcopy((char *)hp->h_addr,
          (char *)&server.sin_addr,
          hp->h_length);
    server.sin_port = htons(atoi(argv[2]));
    length = sizeof(struct sockaddr_in);
    /* SOCKET INIT END */

    printf("\nPlease enter file name to send:\n");
    scanf("%s", net_buf);

    fp = fopen(net_buf, "rb");
    strcpy(file_name, net_buf);
    printf("\nFile Name Received: %s\n", net_buf);
    if (fp == NULL) {
        printf("\nFile open failed!\n");
        perror("fopen() error\n");
    }

    else
        printf("\nFile Successfully opened!\n");

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    printf("file size = %ld Bytes\n", file_size);

    long lastSeq = (long)file_size / (long)NET_BUF_SIZE;
    int lastPacketSize = file_size % NET_BUF_SIZE;
    if (file_size % NET_BUF_SIZE == 0) {
        lastSeq -= 1;
        lastPacketSize = NET_BUF_SIZE;
    }

    //STORE FILE to MEMORY
    char *fileMemBuf = malloc(file_size);
    long fileMemSize = fread(fileMemBuf, 1, file_size, fp);
    printf("Stored %ld Bytes to memory\n", fileMemSize);

    if (fclose(fp)) {
        printf("fclose() error");
        exit(-1);
    }

    //CREATE THREAD FOR RECEIVING LOSS PACKETS
    st.sock = sock;
    st.fileLoc = fileMemBuf;
    st.server = server;

    pthread_t thread_id;
    printf("--start thread--\n");
    pthread_create(&thread_id, NULL, myThreadFun, NULL);

    long seq = 0;

    time_t t = time(NULL);
    struct tm tm = *localtime(&t);

    printf("start time is: %d:%d:%d \n", tm.tm_hour, tm.tm_min, tm.tm_sec);

    bool first_flag = true;
    while (1) {
        if (first_flag == false) {
            pthread_mutex_lock(&th_lock);
            long seq = fifo_buf[fifo_send_ptr++];
            if (fifo_send_ptr >= fifo_buf_size)
                fifo_send_ptr = 0;
            pthread_mutex_unlock(&th_lock);

            fillBufFromMem(fileMemBuf, net_buf, seq, lastSeq, NET_BUF_SIZE, 0);
            for (int i = 0; i < 1; i++) {
                sendto(sock, net_buf, NET_BUF_SIZE + sizeof(struct headerStruct), 0, (struct sockaddr *)&server, length);
            }
        } else if (seq == lastSeq) {
            fillBufFromMem(fileMemBuf, net_buf, seq, lastSeq, lastPacketSize, 1);
            for (int i = 0; i < 10; i++) {
                sendto(sock, net_buf, lastPacketSize + sizeof(struct headerStruct), 0, (struct sockaddr *)&server, length);
            }
            first_flag = false;
            usleep(50);
        } else {
            // send
            fillBufFromMem(fileMemBuf, net_buf, seq, lastSeq, NET_BUF_SIZE, 0);
            for (int i = 0; i < 1; i++) {
                sendto(sock, net_buf, NET_BUF_SIZE + sizeof(struct headerStruct), 0, (struct sockaddr *)&server, length);
            }
            seq += (long)1;
        }
    }
    pthread_join(thread_id, NULL);
    return 0;
}
