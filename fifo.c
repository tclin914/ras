#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* for mknod */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "fifo.h"

char *FIFO[200];
const char *filename = "/tmp/fifo.%d";
int table[100] = {0};

void init_FIFO() {
    char buf[256];
    int i;
    for (i = 0; i < 100; i++) {
        sprintf(buf, filename, i);
        FIFO[i] = (char*)malloc(sizeof(char) * strlen(buf) + 1);
        memcpy(FIFO[i], buf, strlen(buf) + 1);
    }
}

int create_FIFO(int index) {
    /* unlink_FIFO(index); */
    /* printf("FIFO = %s\n", FIFO[index]); */
    if (mknod(FIFO[index], S_IFIFO | 0666, 0) < 0) {
        /* fprintf(stderr, "create %s error\n", FIFO[index]); */
        return -1;
    }
    table[index] = 1;
    return 0;
}

int unlink_FIFO(int index) {
    if (unlink(FIFO[index]) < 0) {
        /* fprintf(stderr, "unlink %s error\n", FIFO[index]); */
        return -1;
    }
    return 0;
}

int unlink_ALL_FIFO() {
    int index = 0;
    while (index < 100) {
        if (table[index] == 1) {
            unlink_FIFO(index);
        }
        index++;
    }
}

int open_FIFO(int index, int *readfd, int *writefd) {
    if ((*readfd = open(FIFO[index], 0 | O_NONBLOCK)) < 0) {
        /* fprintf(stderr, "open %s error\n", FIFO[index]); */
        return -1;
    }
    /* printf("readfd = %d\n", *readfd); */

    if ((*writefd = open(FIFO[index], 1 | O_NONBLOCK)) < 0) {
        /* fprintf(stderr, "open %s error\n", FIFO[index]); */
        return -1;
    }
    /* printf("writefd = %d\n", *writefd); */
    return 0;
}

int read_FIFO(int index, int *readfd) {
    if ((*readfd = open(FIFO[index], 0 | O_NONBLOCK)) < 0) {
        /* fprintf(stderr, "open %s error\n", FIFO[index]); */
        return -1;
    }
    /* printf("readfd = %d\n", *readfd); */
    return 0;
}

int write_FIFO(int index, int *writefd) {
    if ((*writefd = open(FIFO[index], 1 | O_NONBLOCK)) < 0) {
        /* fprintf(stderr, "open %s error\n", FIFO[index]); */
        return -1;
    }
    /* printf("writefd = %d\n", *writefd); */
    return 0;
}

/* int main(int argc, const char *argv[]) { */
    /* init_FIFO(); */
    /* int i; */
    /* for (i = 0; i < 100; i++) { */
        /* printf("%s\n", FIFO[i]); */
    /* } */
    /* [> unlink_FIFO(2); <] */
    /* printf("create_FIFO\n"); */
    /* create_FIFO(2); */
    /* int readfd, writefd; */
    /* printf("open_FIFO\n"); */
    /* open_FIFO(2, &readfd, &writefd); */
    /* printf("readfd = %d, writefd = %d\n", readfd, writefd); */
    /* printf("unlink_FIFO\n"); */
    /* unlink_FIFO(2);  */
    /* open_FIFO(2, &readfd, &writefd); */
    /* return 0; */
/* } */
