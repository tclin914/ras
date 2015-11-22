void init_FIFO();
int create_FIFO(int index);
int unlink_FIFO(int index);
int unlink_ALL_FIFO();
int open_FIFO(int index, int *readfd, int *writefd);
int read_FIFO(int index, int *readfd);
int write_FIFO(int index, int *writefd);
