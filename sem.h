
int sem_create(key_t key, int initval);
int sem_rm(int id);
int sem_open(key_t key);
int sem_close(int id);
int sem_op(int id, int value);
int sem_wait(int id);
int sem_signal(int id);


