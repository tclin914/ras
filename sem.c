#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

#include <stdio.h>

#define MAX_PROCESS 2048


/*      
 *      A Robust Semaphore
 * 3 semaphore values
 * 1. The real semaphore value
 * 2. The counter of the number of process using this semaphore
 * 3. A lock variable for the semaphore 
*/


static struct sembuf op_lock[2] = {
    2, 0, 0, /* wait for [2] (lock) to equal 0 */
    2, 1, SEM_UNDO /* the increment [2] to 1, this locks it */
};

static struct sembuf op_endcreate[2] = {
    1, -1, SEM_UNDO, /* decrement [1] (process counter) */
    2, -1, SEM_UNDO, /* decrement[2] (lock) back to 0 -> unlock */
};

int sem_create(key_t key, int initval) {
    register int id, semval;
    if (key == IPC_PRIVATE) {
        return -1;
    } else if (key == (key_t)-1) {
        return -1;
    }

    if ((id = semget(key, 3, 0666 | IPC_CREAT)) < 0) 
        return -1;

    if (semop(id, &op_lock[0], 2) < 0) {
        fprintf(stderr, "Can't lock the semaphore\n");
        return -1;
    }

    if ((semval = semctl(id, 1, GETVAL, 0)) < 0) {
        fprintf(stderr, "Can't get GETVAL\n");
        return -1;
    }
    printf("semval from sem = %d\n", semval);
    union semun {
        int val;
        struct semid_ds *buff;
        ushort *array;
    } arg;

    union semun semctl_arg;
    if (semval == 0) {
        semctl_arg.val = initval;
        if (semctl(id, 0, SETVAL, semctl_arg) < 0) {
            fprintf(stderr, "Can't SETVAL[0]\n");
            return -1;
        }
        semctl_arg.val = MAX_PROCESS;
        if (semctl(id, 1, SETVAL, semctl_arg) < 0) {
            fprintf(stderr, "Can't SETVAL[1]\n");
        }
    }

    if (semop(id, &op_endcreate[0], 2) < 0) {
        fprintf(stderr, "Can't end create\n");
        return -1;
    }

    return id;
}

int sem_rm(int id) {
    if (semctl(id, 0, IPC_RMID, 0) < 0) {
        fprintf(stderr, "Can't IPC_RMID\n");
        return -1;
    } 
    return 0;
}

static struct sembuf op_open[1] = {
    1, -1, SEM_UNDO /* decrement [1] (process counter) */
};

static struct sembuf op_close[3] = {
    2, 0, 0, /* wait for [2] (lock) to equal 0 */
    2, 1, SEM_UNDO, /* then increment [2] to 1, this locks it */
    1, 1, SEM_UNDO /* then increment [1] (process counter) */
};

static struct sembuf op_unlock[1] = {
    2, -1, SEM_UNDO /* decrement [2] (lock) back to 0 */
};

int sem_open(key_t key) {
    register int id;
    if (key == IPC_PRIVATE)  {
        return -1;
    } else if (key == (key_t)-1) {
        return -1;
    }

    if ((id = semget(key, 3, 0)) < 0) {
        return -1;
    }

    /* Decrement the process counter. We don't need a lock to do this */
    if (semop(id, &op_open[0], 1) < 0) {
        fprintf(stderr, "Can't open\n");
        return -1;
    }
    return id;
}

int sem_close(int id) {
    register int semval;
    if (semop(id, &op_close[0], 3) < 0) {
        fprintf(stderr, "Can't lock the semaphore and decrement the process counter\n");
        return -1;
    }

    /* if this is the last reference to the semaphore, remove this. */
    if ((semval = semctl(id, 1, GETVAL, 0)) < 0) {
        fprintf(stderr, "Can't get GETVAL\n");
        return -1;
    }

    if (semval > MAX_PROCESS) {
        fprintf(stderr, "the process counter of semaphore > MAX_PROCESS\n");
        return -1;
    } else if (semval == MAX_PROCESS) {
        sem_rm(id);
    } else {
        if (semop(id, &op_unlock[0], 1) < 0) {
            fprintf(stderr, "Can't unlock\n");
            return -1;
        }
    }
}

static struct sembuf op_op[1] = {
    0, 99, SEM_UNDO /* decrement or increment [0] */
};

int sem_op(int id, int value) {
    if ((op_op[0].sem_op = value) == 0) {
        fprintf(stderr, "Can't have value == 0\n");
        return -1;
    }

    if (semop(id, &op_op[0], 1) < 0) {
        fprintf(stderr, "Can't set the semaphore\n");
        return -1;
    }
}

int sem_wait(int id) {
    return sem_op(id, -1);
}

int sem_signal(int id) {
    return sem_op(id, 1);
}





