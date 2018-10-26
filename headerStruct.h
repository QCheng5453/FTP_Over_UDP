#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <netdb.h>
#include <stdbool.h>
#include <sys/resource.h>

struct headerStruct {
    u_int32_t seqnum;
    u_int32_t totalSeqnum;
    u_int16_t payloadSize;
    u_int16_t flag;
};