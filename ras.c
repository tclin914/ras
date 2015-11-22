#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/wait.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h> /* open() */
#include <fcntl.h> /* file control options */
#include <arpa/inet.h> /* inet_ntoa() */
#include <signal.h>
#include <errno.h>

#include "fifo.h"

/* semaphore and shared memory */
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include "sem.h"
#define CSHMKEY ((key_t)5674)
#define MSHMKEY ((key_t)6666)
#define SEMKEY1 ((key_t)7777)
#define SEMKEY2 ((key_t)8888)

#define STDIN 0
#define STDOUT 1
#define STDERR 2

typedef enum { 
    e_proc = 0, 
    e_argv = 1, 
    e_stdout = 2, 
    e_stderr = 3,
    e_outfile = 4,
    e_public_out = 5,
    e_public_in = 6
} CommandType;

typedef struct Command {
    CommandType commandType;
    char *command;
    struct Command *next;
} Command;

typedef struct {
    unsigned int sockfd;
    char nickname[21];
    char ip[16];
    unsigned short port;
} Client;

typedef enum {
    e_command = 0,
    e_exit_command = 1,
    e_message = 2,
    e_none
} MsgType;

typedef struct {
    int len;
    MsgType type;
    char message[1024];
} Msg;

void doprocessing(int id, int sockfd); 
void handler(int sig);
void broadcast(char *message, MsgType type, int *fds);
Command *parseCommands(char *commands);
int readline(int fd,char *ptr,int maxlen); 
int run(int id, int sockfd, int readfd, Command *command,int counter, int readfdlist[], int writefdlist[], char* commands, Msg *msgptrs[], int sersems[], int clisems[]);
int sockfd;
Client *clientptr;
int clishmid;

Msg *msgptrs[30];
int msgshmids[30];
int clisems[30], sersems[30];

void handle_sigchld(int sig) {
    while (waitpid((pid_t)(-1), 0, WNOHANG) > 0) {
        printf("%s", "child exit\n"); 
    }   
}

int fds[30] = {0};

fd_set afds;

int main(int argc, const char *argv[]) {
    int ret = chdir("/u/gcs/103/0356100/ras/dir");

    signal(SIGINT, handler);
    
    struct sigaction sa;
    sa.sa_handler = &handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, 0) == -1) {
        perror(0);
        exit(1);
    }

    /* signal(SIGUSR1, handler); */
    int newsockfd, portno, clilen;
    struct sockaddr_in serv_addr, cli_addr;
    int n, pid;
    int pids[30];

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0) {
        perror("ERROR opening socket");
        exit(1);
    }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    bzero((char *) &serv_addr, sizeof(serv_addr));
    portno = 5566;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR on binding");
        exit(1);
    }

    listen(sockfd, 5);
    clilen = sizeof(cli_addr);

    fd_set rfds;
    fd_set wfds;
    /* fd_set afds; */
    int nfds, fd;

    nfds = getdtablesize();
    FD_ZERO(&afds);
    FD_SET(sockfd, &afds);

	char buffer[15001];
    bzero(buffer, 15001);
    char msg_buf[15001];
    bzero(msg_buf, 15001);

    if ((clishmid = shmget(CSHMKEY, sizeof(Client) * 30, IPC_CREAT | 0666)) < 0) {
        perror("ERROR on creating shared memory for clients data");
        exit(1);
    }
    if ((clientptr = (Client*)shmat(clishmid, (char*)0, 0)) == NULL) {
        perror("ERROR on attaching shared memory for clients data");
        exit(1);
    }

    /* initialize clients data's sockfd to 0 */
    int i;
    for (i = 0; i < 30; i++) {
        clientptr[i].sockfd = 0;
    }

    const char *noname = "(no name)";
 
    /* create shared memory for message */
    int k;
    for (k = 0; k < 30; k++) {
        if ((msgshmids[k] = shmget(MSHMKEY + k, sizeof(Msg), IPC_CREAT | 0666)) < 0) {
            perror("ERROR on creating shared memory for clients data");
            exit(1);
        }
        if ((msgptrs[k] = (Msg*)shmat(msgshmids[k], (char*)0, 0)) == NULL) {
            perror("ERROR on attaching shared memory for clients data");
            exit(1);
        }
    }
    /* create semaphore */
    int j;
    for (j = 0; j < 30; j++) {
        if ((clisems[j] = sem_create(SEMKEY1 + j, 0)) < 0) {
            perror("ERROR on creating semaphore");
            exit(1);
        }
        if ((sersems[j] = sem_create(SEMKEY2 + j, 1)) < 0) {
            perror("ERROR on creating semaphore");
            exit(1);
        }
    }
    
    while (1) {
        memcpy(&rfds, &afds, sizeof(rfds));
        /* memcpy(&wfds, &afds, sizeof(wfds)); */
        if (select(nfds, &rfds, (fd_set*)0, (fd_set*)0, (struct timeval*)0) < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("ERROR on select");
            exit(1);
        }

        if (FD_ISSET(sockfd, &rfds)) {	
            newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
			/* set client data to shared memory  */
			int i;
			for (i = 0; i < 30; i++) {
				if (clientptr[i].sockfd == 0) { /*   */
					clientptr[i].sockfd = newsockfd;
					memcpy(clientptr[i].nickname, noname, strlen(noname) + 1);
					memcpy(clientptr[i].ip, inet_ntoa(cli_addr.sin_addr), 16);
					clientptr[i].port = cli_addr.sin_port;
                    fds[i] = newsockfd;
					break;
				}
			}

			if (newsockfd < 0) {
				perror("ERROR on accept");
				exit(1);
			}
            
			pid = fork();

			if (pid < 0) {
				perror("ERROR on fork");
				exit(1);
			}

			if (pid == 0) {
				close(sockfd);
				dup2(newsockfd, 0);
				dup2(newsockfd, 1);
				dup2(newsockfd, 2);
				doprocessing(newsockfd, i);
                close(newsockfd);
                exit(0);
			} else {
                pids[i] = pid;
                sprintf(msg_buf, "*** User '(no name)' entered from (%s/%d). ***\n", clientptr[i].ip, clientptr[i].port);
                broadcast(msg_buf, e_message, fds);
				bzero(msg_buf, 15001);
                FD_SET(newsockfd, &afds);
			}
		}

        for (fd = 0; fd < nfds; fd++) {
            if (fd != sockfd && FD_ISSET(fd, &rfds)) {
                printf("rfd = %d\n", fd);
                n = readline(fd, buffer, sizeof(buffer) - 1);
         
                /* get client's id by client'fd */
                int l;
                for (l = 0; l < 30; l++) {
                    if (fds[l] == fd) {
                        break;
                    }
                }
                printf("xx = %s\n", buffer);
                /* exit */
                if (strncmp(buffer, "exit", 4) == 0) {
                    sem_wait(sersems[l]);
                    msgptrs[l]->type = e_exit_command;
                    sem_signal(clisems[l]);
                    sprintf(msg_buf, "*** User '%s' left. ***\n", clientptr[l].nickname);
                    fds[l] = 0;
                    /* broadcast all clients that someone left */
                    broadcast(msg_buf, e_message, fds);
                    bzero(msg_buf, 15001);
                    close(fd);
                    printf("close(fd) = %d\n", fd);
                    FD_CLR(fd, &afds);
                    printf("exit\n");
                /* yell */
                } else if (strncmp(buffer, "yell", 4) == 0) {
                    buffer[n - 1] = '\0';
                    sprintf(msg_buf, "*** %s yelled ***: %s\n", clientptr[l].nickname, buffer + 5);
                    broadcast(msg_buf, e_message, fds);
                /* tell */
                } else if (strncmp(buffer, "tell", 4) == 0) {
                    int receiver_id;
                    int msg[1025];
                    bzero(msg, 1025);
                    buffer[n - 1] = '\0';
                    sscanf(buffer, "%*s %d %[^\t\n]", &receiver_id, msg);
                    if (clientptr[receiver_id - 1].sockfd != 0) {
                        sprintf(msg_buf, "*** %s told you ***: %s\n", clientptr[l].nickname, msg);
                        sem_wait(sersems[receiver_id - 1]);
                        memcpy(msgptrs[receiver_id - 1]->message, msg_buf, strlen(msg_buf) + 1);
                        msgptrs[receiver_id - 1]->type = e_message;
                        msgptrs[receiver_id - 1]->len = strlen(msg_buf) + 1;
                        sem_signal(clisems[receiver_id - 1]);

                        /* signal client to send prompt */
                        sem_wait(sersems[l]);
                        msgptrs[l]->type = e_none;
                        sem_signal(clisems[l]);

                    } else {
                        sprintf(msg_buf, "*** Error: user #%d does not exist yet. ***\n", receiver_id);   
                        sem_wait(sersems[l]);
                        memcpy(msgptrs[l]->message, msg_buf, strlen(msg_buf) + 1);
                        msgptrs[l]->type = e_message;
                        msgptrs[l]->len = strlen(msg_buf) + 1;
                        sem_signal(clisems[l]);
                    }
                    bzero(msg_buf, 15001);
                } else if (strncmp(buffer, "name", 4) == 0) {
                    /* TODO: */
                    buffer[n - 2] = '\0';
                    int isSame = 1;
                    int i;
                    for (i = 0; i < 30; i++) {
                        if (strcmp(clientptr[i].nickname, buffer + 5) == 0) {
                            sem_wait(sersems[l]);
                            sprintf(msg_buf, "*** User '%s' already exists. ***\n", buffer + 4);
                            memcpy(msgptrs[l]->message, msg_buf, strlen(msg_buf) + 1);
                            msgptrs[l]->type = e_message;
                            msgptrs[l]->len = strlen(msg_buf) + 1;
                            sem_signal(clisems[l]);
                            isSame = 0;
                            break;
                        }
                    }

                    if (isSame == 1) {
                        memcpy(clientptr[l].nickname, buffer + 5, strlen(buffer) - 4);

                        /* signal client to send prompt */
                        sem_wait(sersems[l]);
                        msgptrs[l]->type = e_none;
                        sem_signal(clisems[l]);
                    }

                } else {
                    printf("recv = %s\n", buffer);
                    sem_wait(sersems[l]);
                    memcpy(msgptrs[l]->message, buffer, n);
                    msgptrs[l]->len = n;
                    msgptrs[l]->type = e_command;
                    sem_signal(clisems[l]);
                }

            }

            /* if (fd != sockfd && FD_ISSET(fd, &wfds)) { */
                /* printf("wfds %d\n", fd); */
           /* } */
        }    
        bzero(buffer, 15001);
    }
	close(sockfd);
    return 0;
}

void handler(int sig) {
    if (sig == SIGUSR1) {
        printf("SIGUSR1\n");
        int i;
        for (i = 0; i < 30; i++) {
            if (clientptr[i].sockfd == 0 && fds[i] != 0) {
                printf("i = %d\n", i);
                printf("fds[%d] = %d\n", i, fds[i]);
                FD_CLR(fds[i], &afds);
                break;
            }
        }
        return;
    }
    /* remove shared memory */
    if ((clishmid = shmget(CSHMKEY, sizeof(Client) * 30, 0)) < 0) {
        perror("ERROR on getting shared memory for clients data");
        exit(1);
    }
    if (shmctl(clishmid, IPC_RMID, (struct shmid_ds*)0) < 0) {
        perror("ERROR on removing shared memory for clients data");
        exit(1);
    }

    int i; 
    for (i = 0; i < 30; i++) {
        if ((msgshmids[i] = shmget(MSHMKEY + i, sizeof(Msg), 0)) < 0) {
            perror("ERROR on getting shared memory for message");
            exit(1);
        }
        if (shmctl(msgshmids[i], IPC_RMID, (struct shmid_ds*)0) < 0) {
            perror("ERROR on removing shared memory for message");
            exit(1);
        }
    }
    for (i = 0; i < 30; i++) {
        sem_close(clisems[i]);
        sem_close(sersems[i]);
    }
    unlink_ALL_FIFO();
    exit(0);
}

void broadcast(char *message, MsgType type, int *fds) {
    int i;
    for (i = 0; i < 30; i++) {
        if (fds[i] != 0) {
            printf("i = %d\n", i);
            sem_wait(sersems[i]);
            memcpy(msgptrs[i]->message, message, strlen(message) + 1);
            msgptrs[i]->type = type;
            sem_signal(clisems[i]);
        }
    }
}

/* parse input command line */
Command *parseCommands(char *commands) {

    int num = 0;
    Command *head, *pre = NULL;
    int len = strlen(commands);

    int check = 0;
    CommandType type;
    char *pch = NULL;
    pch = strtok(commands, " \n\r\t");
    for (unsigned i = 0; pch != NULL; i++) {
        if (pre == NULL) {
            Command *current = (Command*)malloc(sizeof(Command));
            current->command = pch;
            current->commandType = e_proc;
            current->next = NULL;

            pre = current;
            head = current;
        } else if (strcmp(pch, "|") == 0) {
            pch = strtok(NULL, " \n\r\t");
            Command *current = (Command*)malloc(sizeof(Command));
            current->command = pch;
            current->commandType = e_proc;
            current->next = NULL;

            pre->next = current;
            pre = current;
        } else if (strcmp(pch, ">") == 0) {
            /* file redirection */
            pch = strtok(NULL, " \n\r\t");
            Command *current = (Command*)malloc(sizeof(Command));
            current->command = pch;
            current->commandType = e_outfile;
            current->next = NULL;

            pre->next = current;
            pre = current;
        } else if (*pch - '|' == 0) {
            /* numbered-pipe stdout */
            Command *current = (Command*)malloc(sizeof(Command));
            current->command = pch + 1;
            current->commandType = e_stdout;
            current->next = NULL;

            pre->next = current;
            pre = current;
        } else if (*pch - '!' == 0) {
            /* numbered-pipe stderr */
            Command *current = (Command*)malloc(sizeof(Command));
            current->command = pch + 1;
            current->commandType = e_stderr;
            current->next = NULL;
        
            pre->next = current;
            pre = current;
        } else if (*pch - '>' == 0) {
            /* public pipe out */
            Command *current = (Command*)malloc(sizeof(Command));
            current->command = pch + 1;
            current->commandType = e_public_out;
            current->next = NULL;

            pre->next = current;
            pre = current;
        } else if (*pch - '<' == 0) {
            /* public pipe in */
            Command *current = (Command*)malloc(sizeof(Command));
            current->command = pch + 1;
            current->commandType = e_public_in;
            current->next = NULL;

            pre->next = current;
            pre = current;
        } else {
            /* arguments */
            Command *current = (Command*)malloc(sizeof(Command));
            current->command = pch;
            current->commandType = e_argv;
            current->next = NULL;

            pre->next = current;
            pre = current;
        }

        pch = strtok(NULL, " \n\r\t");
    }
    return head;
}


int run(int id, int sockfd, int readfd, Command *command,int counter, int readfdlist[], int writefdlist[], char *commands, Msg *msgptrs[], int sersems[], int clisems[]) {
    Command *args = command;
    Command *temp = command;

    int count = 0;
    // get arguments
    while (args->next != NULL && args->next->commandType == e_argv) {
        args = args->next;
        count++;
    }
    char *arguments[count + 1];
    bzero(arguments, count + 1);
    int n;
    char *envar = NULL;
    switch (command->commandType) {
        case e_proc:
            /* exit command */
            if (strcmp(command->command, "exit") == 0) {
                return -1;
            /* printenv */
            } else if (strcmp(command->command, "printenv") == 0) {
                if ((envar = getenv(command->next->command)) != NULL) { 
                    printf("%s=%s\n", command->next->command, envar);
                    fflush(stdout);
                    return 0;
                }
            /* setenv */
            } else if (strcmp(command->command, "setenv") == 0) {
                // TODO:less arguments event
                setenv(command->next->command, command->next->next->command, 1);
                return 0;
            } else if(strcmp(command->command, "remove") == 0) {
                setenv(command->next->command, "", 1);
                return 0;
            } else if (strcmp(command->command, "who") == 0) {
                int clishmid;
                Client *clientptr;
                if ((clishmid = shmget(CSHMKEY, sizeof(Client) * 30, 0)) < 0) {
                    perror("ERROR on getting shared memory for clients data");
                    exit(1);
                }

                if ((clientptr = (Client*)shmat(clishmid, (char*)0, 0)) == NULL) {
                    perror("ERROR on attaching shared memory for clients data");
                    exit(1);
                }
                const char *me = "<-me";
                fprintf(stdout, "<ID>\t<nickname>\t<IP/port>\t<indicate me>\n");
                int i;
                for (i = 0; i < 30; i++) {
                    if (clientptr[i].sockfd != 0) {
                        if (i == id) {
                            fprintf(stdout, "%d\t%s\t%s%c%hu\t%s\n", i + 1, clientptr[i].nickname, 
                                    clientptr[i].ip, '/', clientptr[i].port, me);
                        } else {
                            fprintf(stdout, "%d\t%s\t%s%c%hu\n", i + 1, clientptr[i].nickname, 
                                    clientptr[i].ip, '/', clientptr[i].port);
                        }
                    }
                }
                if (shmdt(clientptr) < 0) {
                    perror("ERROR on deattaching shared memory for clients data");
                    exit(1);
                }
                return 0;
            }

            /* get arguments */
            arguments[0] = command->command;
            for (unsigned i = 1; i < count + 1; i++) {
                arguments[i] = temp->next->command;
                temp = temp->next;
            }
            arguments[count + 1] = NULL;
           
            int pfd[2];
            pid_t pid;
            int status_pid;

            /* command tail */
            if (args->next == NULL) {
                /* close writefd */
                if (writefdlist[counter] != 0) {
                    close(writefdlist[counter]);
                }

                /* char buffer[256]; */
                /* int n; */
                /* n = read(readfd, buffer, 256); */
                /* printf("%s\n", buffer); */
                
                pid = fork();
                
                if (pid < 0) {
                    perror("ERROR on fork");
                    exit(1);
                }

                if (pid == 0) {
                    dup2(readfd, STDIN); /* replace stdin with readfd */ 
                    dup2(sockfd, STDOUT); /* replace stdout with sockfd */
                    dup2(sockfd, STDERR); /* replace stderr with sockfd */
                    
                    execvp(command->command, arguments);
                    fprintf(stderr, "Unknown command: [%s].\n", command->command);
                    exit(1);
                } else {
                    waitpid((pid_t)pid, &status_pid, 0);
                    
                    if (WIFEXITED(status_pid)) {
                        if (WEXITSTATUS(status_pid) == 0) {
                            return 0;
                        }
                        return 1;
                    }
                    return 0;
                }
            /* redirect data to file */
            } else if (args->next->commandType == e_outfile) { 
                /* close writefd */
                if (writefdlist[counter] != 0) {
                    close(writefdlist[counter]);
                }

                pid = fork();

                if (pid < 0) {
                    perror("ERROR on fork");
                    exit(1);
                }

                if (pid == 0) {
                    int filefd = open(args->next->command, O_CREAT | O_RDWR, S_IREAD | S_IWRITE);
                    dup2(readfd, STDIN); /* replace stdin with readfd */
                    dup2(filefd, STDOUT); /* replace stdout with fileno(file) */
                    dup2(sockfd, STDERR); /* replace stderr with sockfd */
                    close(filefd);

                    execvp(command->command, arguments);
                    fprintf(stderr, "Unknown command: [%s].\n", command->command);
                    exit(1);
                } else {
                    waitpid((pid_t)pid, &status_pid, 0);
                    
                    if (WIFEXITED(status_pid)) {
                        return WEXITSTATUS(status_pid);
                    }
                    return 0;
                }
            } else if (args->next->commandType == e_public_out) {
                /* close writefd */
                if (writefdlist[counter] != 0) {
                    close(writefdlist[counter]);
                }

                int index = atoi(args->next->command);
                index = index - 1;
                if (create_FIFO(index) < 0) {
                    fprintf(stderr, "*** Error: public pipe #%d already exists. ***\n", index + 1);
                    return 0;
                }
                int write, read;
                open_FIFO(index, &read, &write); 
        
                pid = fork();

                if (pid < 0) {
                    perror("ERROR on fork");
                    exit(1);
                }

                if (pid == 0) {
                    dup2(write, STDOUT);
                    dup2(write, STDERR);
                    dup2(readfd, STDIN);

                    execvp(command->command, arguments);
                    fprintf(stderr, "Unknown command: [%s].\n", command->command);
                    exit(1);
                } else { 
                    waitpid((pid_t)pid, &status_pid, 0);
                        
                    if (WIFEXITED(status_pid)) {
                        if (WEXITSTATUS(status_pid) == 0) {
                            if ((clishmid = shmget(CSHMKEY, sizeof(Client) * 30, 0)) < 0) {
                                perror("ERROR on getting shared memory for clients data");
                                exit(1);
                            }

                            if ((clientptr = (Client*)shmat(clishmid, (char*)0, 0)) == NULL) {
                                perror("ERROR on attaching shared memory for clients data");
                                exit(1);
                            }
                            /* broadcast public pipe created event */
                            char msg_buf[1024];
                            sprintf(msg_buf, "*** %s #%d just piped '%s' ***\n", clientptr[id].nickname, id, commands);
                            int i;
                            for (i = 0; i < 30; i++) {
                                if (clientptr[i].sockfd != 0 && i != id) {
                                    sem_wait(sersems[i]);
                                    memcpy(msgptrs[i]->message, msg_buf, strlen(msg_buf) + 1);
                                    msgptrs[i]->type = e_message;
                                    msgptrs[i]->len = strlen(msg_buf) + 1;
                                    sem_signal(clisems[i]);
                                }
                            }
                            /* close(write); */
                            /* close(read); */
                            return 0;
                        } else { 
                            unlink_FIFO(index);
                            return 1;
                        }
                    }
                    return 0;    
                }
            } else if (args->next->commandType == e_public_in && args->next->next == NULL) {
                /* close writefd */
                if (writefdlist[counter] != 0) {
                    close(writefdlist[counter]);
                }

                int index = atoi(args->next->command);
                index = index - 1;
                int writeFIFO, readFIFO;
                if (open_FIFO(index, &readFIFO, &writeFIFO) < 0) {
                    fprintf(stderr, "*** Error: public pipe #%d does not already exists. ***\n", index + 1);
                    return 0;
                }
                /* printf("write = %d\n", writeFIFO); */
                /* printf("read = %d\n", readFIFO);  */
                pid = fork();

                if (pid < 0) {
                    perror("ERROR on fork");
                    exit(1);
                }

                if (pid == 0) {
                    dup2(sockfd, STDOUT);
                    dup2(sockfd, STDERR);
                    dup2(readFIFO, STDIN);

                    execvp(command->command, arguments);
                    fprintf(stderr, "Unknown command: [%s].\n", command->command);
                    exit(1);
                } else { 
                    waitpid((pid_t)pid, &status_pid, 0);
                        
                    if (WIFEXITED(status_pid)) {
                        if (WEXITSTATUS(status_pid) == 0) {
                            if ((clishmid = shmget(CSHMKEY, sizeof(Client) * 30, 0)) < 0) {
                                perror("ERROR on getting shared memory for clients data");
                                exit(1);
                            }

                            if ((clientptr = (Client*)shmat(clishmid, (char*)0, 0)) == NULL) {
                                perror("ERROR on attaching shared memory for clients data");
                                exit(1);
                            }
                            /* broadcast public pipe received event */
                            char msg_buf[1024];
                            sprintf(msg_buf, "*** %s #%d just received via '%s' ***\n", clientptr[id].nickname, id, commands);
                            int i;
                            for (i = 0; i < 30; i++) {
                                if (clientptr[i].sockfd != 0 && i != id) {
                                    sem_wait(sersems[i]);
                                    memcpy(msgptrs[i]->message, msg_buf, strlen(msg_buf) + 1);
                                    msgptrs[i]->type = e_message;
                                    msgptrs[i]->len = strlen(msg_buf) + 1;
                                    sem_signal(clisems[i]);
                                }
                            }
                            unlink_FIFO(index);
                            return 0;
                        } else { 
                            return 1;
                        }
                    }
                    return 0;    
                }

            /* 1 numbered-pipe */
            } else if ((args->next->commandType == e_stdout ||
                    args->next->commandType == e_stderr) && args->next->next == NULL) {
                int std = args->next->commandType == e_stdout ? STDOUT : STDERR; /* stdout or stderr */
                
                int number = atoi(args->next->command);
                int writefd;

                /* close writefd */
                if (writefdlist[counter] != 0) {
                    close(writefdlist[counter]);
                }
                
                /* if pipe is not already exist */
                if ((writefd = writefdlist[(counter + number) % 2000]) == 0) {
                    if (pipe(pfd) < 0) {
                        perror("ERROR creating a pipe");
                        exit(1);
                    }  

                    pid = fork();

                    if (pid < 0) {
                        perror("ERROR on fork");
                        exit(1);
                    }

                    if (pid == 0) {
                        dup2(pfd[1], std); /* replace stdout or stderr with pfd[1] */
                        dup2(readfd, STDIN); /* replace stdin with readfd */
                        close(pfd[0]);

                        execvp(command->command, arguments);
                        fprintf(stderr, "Unknown command: [%s].\n", command->command);
                        exit(1);
                    } else {

                        /* if (readfd != 0) { */
                            /* close(readfd); */
                            /* printf("close = %d\n", readfd); */
                        /* } */
                        waitpid((pid_t)pid, &status_pid, 0);
                     
                        if (readfd != 0) {
                            close(readfd);
                        }

                        if (WIFEXITED(status_pid)) {
                            if (WEXITSTATUS(status_pid) == 0) {
                                writefdlist[(counter + number) % 2000] = pfd[1];
                                readfdlist[(counter + number) % 2000] = pfd[0];
                                return 0;
                            } 
                            return 1;
                        }
                        return 0;    
                    }
                /* if pipe is exist */
                } else {
                    pid = fork();
                    if (pid == 0) {
                        dup2(writefd, std); /* replace stdout or stderr with writefd */
                        dup2(readfd, STDIN); /* replace stdin with readfd */
                        
                        execvp(command->command, arguments);
                        fprintf(stderr, "Unknown command: [%s].\n", command->command);
                        exit(1);
                    } else {
                        waitpid((pid_t)pid, &status_pid, 0);
                    
                        /* if (readfd != 0) { */
                            /* close(readfd); */
                        /* } */

                        if (WIFEXITED(status_pid)) {
                            return WEXITSTATUS(status_pid);
                        }
                        return 0;
                    }
                }
            /* 2 numbered-pipe */
            } else if ((args->next->commandType == e_stdout || args->next->commandType == e_stderr) &&
                    (args->next->next->commandType == e_stdout || args->next->next->commandType == e_stderr)) {
                /* close writefd */
                if (writefdlist[counter] != 0) {
                    close(writefdlist[counter]);
                }

                int num_stdout, num_stderr;
                if (args->next->commandType == e_stdout) {
                    num_stdout = atoi(args->next->command);
                    num_stderr = atoi(args->next->next->command);
                } else {
                    num_stdout = atoi(args->next->next->command);
                    num_stderr = atoi(args->next->command);
                }
                int writefd_stdout = writefdlist[(counter + num_stdout) % 2000];
                int writefd_stderr = writefdlist[(counter + num_stderr) % 2000];

                /* numbered-pipe stdout and stderr is not yet created */
                if (writefd_stdout == 0 && writefd_stderr == 0) {
                    /* the number of stdout pipe and stderr pipe is not equal */
                    if (num_stdout != num_stderr) {
                        int pfd_stdout[2], pfd_stderr[2];
                        if (pipe(pfd_stdout) < 0) {
                            perror("ERROR creating a pipe");
                            exit(1);
                        }           
                        if (pipe(pfd_stderr) < 0) {
                            perror("ERROR creating a pipe");
                            exit(1);
                        }
                        
                        pid = fork();

                        if (pid == 0) {
                            /* stdin */
                            dup2(readfd, STDIN);
                            /* stdout */
                            dup2(pfd_stdout[1], STDOUT);
                            close(pfd_stdout[0]);
                            /* stderr */
                            dup2(pfd_stderr[1], STDERR);
                            close(pfd_stderr[0]);

                            execvp(command->command, arguments);
                            fprintf(stderr, "Unknown command: [%s].\n", command->command);
                            exit(1);
                        } else {
                            waitpid((pid_t)pid, &status_pid, 0);
                            
                            if (WIFEXITED(status_pid)) {
                                if(WEXITSTATUS(status_pid) == 0) {
                                    writefdlist[(counter + num_stdout) % 2000] = pfd_stdout[1];
                                    writefdlist[(counter + num_stderr) % 2000] = pfd_stderr[1];
                                    readfdlist[(counter + num_stdout) % 2000] = pfd_stdout[0];
                                    readfdlist[(counter + num_stderr) % 2000] = pfd_stderr[0];
                                    return 0;
                                } 
                                return 1;
                            }
                            return 0;
                        }
                    /* the number of stdout pipe and stderr pipe is equal */
                    } else {
                        if (pipe(pfd) < 0) {
                            perror("ERROR creating a pipe");
                            exit(1);
                        }           
                        
                        pid = fork();

                        if (pid == 0) {
                            /* stdin */
                            dup2(readfd, STDIN);
                            /* stdout */
                            dup2(pfd[1], STDOUT);
                            /* stdin */
                            dup2(pfd[1], STDERR);
                            close(pfd[0]);

                            execvp(command->command, arguments);
                            fprintf(stderr, "Unknown command: [%s].\n", command->command);
                            exit(1);
                        } else {
                            waitpid((pid_t)pid, &status_pid, 0);
                                
                            if (WIFEXITED(status_pid)) {
                                if(WEXITSTATUS(status_pid) == 0) {
                                    writefdlist[(counter + num_stdout) % 2000] = pfd[1];
                                    readfdlist[(counter + num_stdout) % 2000] = pfd[0];
                                    return 0;
                                }
                                return 0;
                            }
                        }
                    }
                /* numbered-pipe stdout is created, but stderr not yet */
                } else if (writefd_stdout != 0 && writefd_stderr == 0) {
                    if (pipe(pfd) < 0) {
                        perror("ERROR creating a pipe");
                        exit(1);
                    }

                    pid = fork();

                    if (pid == 0) {
                        /* stdin */
                        dup2(readfd, STDIN); 
                        /* stdout */
                        dup2(writefd_stdout, STDOUT); 
                        /* stderr */
                        dup2(pfd[1], STDERR); 
                        close(pfd[0]);

                        execvp(command->command, arguments);
                        fprintf(stderr, "Unknown command: [%s].\n", command->command);
                        exit(1);
                    } else {
                        waitpid((pid_t)pid, &status_pid, 0);
                        
                        if (WIFEXITED(status_pid)) {
                            if(WEXITSTATUS(status_pid) == 0) {
                                writefdlist[(counter + num_stderr) % 2000] = pfd[1];
                                readfdlist[(counter + num_stderr) % 2000] = pfd[0];
                                return 0;
                            }
                            return 1;
                        }
                        return 0;
                    }
                /* numbered-pipe stderr is created, but stdout not yet */
                } else if (writefd_stdout == 0 && writefd_stderr != 0) {
                    if (pipe(pfd) < 0) {
                        perror("ERROR creating a pipe");
                        exit(1);
                    }          

                    pid = fork();

                    if (pid == 0) {
                        /* stdin */
                        dup2(readfd, STDIN);
                        /* stdout */
                        dup2(pfd[1], STDOUT);
                        /* stderr */
                        dup2(writefd_stderr, STDERR);
                        close(pfd[0]);

                        execvp(command->command, arguments);
                        fprintf(stderr, "Unknown command: [%s].\n", command->command);
                        exit(1);
                    } else {
                        waitpid((pid_t)pid, &status_pid, 0);

                        if (WIFEXITED(status_pid)) {
                            if(WEXITSTATUS(status_pid) == 0) {
                                writefdlist[(counter + num_stdout) % 2000] = pfd[1];
                                readfdlist[(counter + num_stdout) % 2000] = pfd[0];
                                return 0;
                            }
                            return 1;
                        }
                        return 0;
                    }
                /* numbered-pipe stdout and stderr are created */
                } else {
                    
                    pid = fork();

                    if (pid == 0) {
                        /* stdin */
                        dup2(readfd, STDIN);
                        /* stdout */
                        dup2(writefd_stdout, STDOUT);
                        /* stderr */
                        dup2(writefd_stderr, STDERR);

                        execvp(command->command, arguments);
                        fprintf(stderr, "Unknown command: [%s].\n", command->command);
                        exit(1);
                    } else {
                        waitpid((pid_t)pid, &status_pid, 0);
                        
                        if (WIFEXITED(status_pid)) {
                            return WEXITSTATUS(status_pid);
                        }
                        return 0;
                    }
                }               
            } else {
                /* close writefd */
                if (writefdlist[counter] != 0) {
                    close(writefdlist[counter]);
                    writefdlist[counter] = 0;
                }

                /* NOTE:Create needed pipe first, then close writefd of current counter */
                if (pipe(pfd) < 0) {
                    perror("ERROR creating a pipe");
                    exit(1);
                }
                int index = -1;
                int writeFIFO, readFIFO;
                if (args->next->commandType == e_public_in) {
                    index = atoi(args->next->command);
                    index = index - 1;
                    int writeFIFO, readFIFO;
                    if (open_FIFO(index, &readFIFO, &writeFIFO) < 0) {
                        fprintf(stderr, "*** Error: public pipe #%d does not already exists. ***\n", index + 1);
                        return 0;
                    }
                    readfd = readFIFO;
                }
                pid = fork();
                    
                if (pid == 0) {
                    dup2(readfd, STDIN); /* replace stdin with readfd  */
                    dup2(pfd[1], STDOUT); /* replace stdout with pfd[1] */
                    dup2(sockfd, STDERR); /* replace stderr with sockfd */
                    close(pfd[0]);
                    
                    execvp(command->command, arguments);
                    fprintf(stderr, "Unknown command: [%s].\n", command->command);
                    exit(1);
                } else {
                    close(pfd[1]);
                 
                    waitpid((pid_t)pid, &status_pid, 0);
                    if (readfd != 0) {
                        close(readfd);   
                    }

                    if (WIFEXITED(status_pid)) {
                        if(WEXITSTATUS(status_pid) == 0) {
                            readfdlist[counter % 2000] = pfd[0];                    
                            if (index != -1) {
                                if ((clishmid = shmget(CSHMKEY, sizeof(Client) * 30, 0)) < 0) {
                                    perror("ERROR on getting shared memory for clients data");
                                    exit(1);
                                }

                                if ((clientptr = (Client*)shmat(clishmid, (char*)0, 0)) == NULL) {
                                    perror("ERROR on attaching shared memory for clients data");
                                    exit(1);
                                }
                                /* broadcast public pipe received event */
                                char msg_buf[1024];
                                sprintf(msg_buf, "*** %s #%d just received via '%s' ***\n", clientptr[id].nickname, id, commands);
                                int i;
                                for (i = 0; i < 30; i++) {
                                    if (clientptr[i].sockfd != 0 && i != id) {
                                        sem_wait(sersems[i]);
                                        memcpy(msgptrs[i]->message, msg_buf, strlen(msg_buf) + 1);
                                        msgptrs[i]->type = e_message;
                                        msgptrs[i]->len = strlen(msg_buf) + 1;
                                        sem_signal(clisems[i]);
                                    }
                                }
                                unlink_FIFO(index);
                            }
                            return 0;
                        }
                        return 1;
                    }
                    return 0;
                }
            }
            break;
        case e_argv:
            break;

        case e_stdout:
            break;

        case e_stderr:
            break;   

        case e_outfile:
            break;
    }
}

int readline(int fd,char *ptr,int maxlen) {
	int n,rc;
	char c;
	*ptr = 0;
	for(n=1;n<maxlen;n++)
	{
		if((rc=read(fd,&c,1)) == 1)
		{
			*ptr++ = c;	
			if(c=='\n')  break;
		}
		else if(rc==0)
		{
			if(n==1)     return(0);
			else         break;
		}
		else
			return(-1);
	}
	return(n);
}      


void doprocessing(int sockfd, int id) {
    int n;
    int bufferSize = 15001;
    char buffer[bufferSize];
    bzero(buffer, bufferSize);
    init_FIFO();

    const char *welcome = 
        "****************************************\n"
        "** Welcome to the information server. **\n"
        "****************************************\n";

    const char prompt[] = {'%', ' ', 13, '\0'};
    char *defaultPath[] = {"bin", "."};

    setenv("PATH", "bin:.", 1);

    printf(welcome);
    fflush(stdout);
    int readfdlist[2000] = {0};
    int writefdlist[2000] = {0};
    int counter = 0;

    int msgshmids[30];
    Msg *msgptrs[30];
    /* create shared memory for message */
    int k;
    for (k = 0; k < 30; k++) {
        if ((msgshmids[k] = shmget(MSHMKEY + k, sizeof(Msg), 0)) < 0) {
            perror("ERROR on opening shared memory for clients data");
            exit(1);
        }
        if ((msgptrs[k] = (Msg*)shmat(msgshmids[k], (char*)0, 0)) == NULL) {
            perror("ERROR on attaching shared memory for clients data");
            exit(1);
        }
    }
    int clisems[30], sersems[30];
    /* open semaphore */
    int j;
    for (j = 0; j < 30; j++) {
        if ((clisems[j] = sem_open(SEMKEY1 + j)) < 0) {
            perror("ERROR on opening semaphore");
            exit(1);
        }
        if ((sersems[j] = sem_open(SEMKEY2 + j)) < 0) {
            perror("ERROR on opening semaphore");
            exit(1);
        }
    }

    sem_wait(clisems[id]);

    if (msgptrs[id]->type == e_message) {
        fprintf(stdout, msgptrs[id]->message);
        fflush(stdout);
    }

    sem_signal(sersems[id]);

    while (1) {
        printf(prompt);
        fflush(stdout);
	
        sem_wait(clisems[id]);
        if (msgptrs[id]->type == e_message) {
            fprintf(stdout, msgptrs[id]->message);
            fflush(stdout);
            sem_signal(sersems[id]);
            continue;
        /* receive exit command */
        } else if (msgptrs[id]->type == e_exit_command) {
            int i;
            for (i = 0; i < 30; i++) {
                if (shmdt(msgptrs[i]) < 0) {
                    perror("ERROR on deattaching shared memory for message");
                    exit(1);
                }
            }
            int clishmid;
            Client* clientptr;
            if ((clishmid = shmget(CSHMKEY, sizeof(Client) * 30, 0)) < 0) {
                perror("ERROR on getting shared memory for clients data");
                exit(1);
            }

            if ((clientptr = (Client*)shmat(clishmid, (char*)0, 0)) == NULL) {
                perror("ERROR on attaching shared memory for clients data");
                exit(1);
            }

            char msg_buf[1024];
            sprintf(msg_buf, "*** User '%s' left. ***\n", clientptr[id].nickname);
            fprintf(stdout, msg_buf);
            fflush(stdout);

            /* remove client data from shared memory  */
            clientptr[id].sockfd = 0;
            memset(clientptr[id].nickname, 0, 21);
            memset(clientptr[id].ip, 0, 16);
            clientptr[id].port = 0;
            if (shmdt(clientptr) < 0) {
                perror("ERROR on deattaching shared memory for clients data");
                exit(1);
            }
            sem_signal(sersems[id]);

            for (j = 0; j < 30; j++) {
                if (sem_close(sersems[j]) < 0) {
                    perror("ERROR on closeing semaphore");
                    exit(1);
                }
                if (sem_close(clisems[j]) < 0) {
                    perror("ERROR on opening semaphore");
                    exit(1);
                }
            }
            return;
        } else if (msgptrs[id]->type == e_none) {
            sem_signal(sersems[id]);
            continue; 
        } else {
            if (msgptrs[id]->len == 0) {
                break;
            } else if (msgptrs[id]->len == 2) {
                sem_signal(sersems[id]);
                continue;
            }
        }
        
        /* remove 13 enter key and 10 next key */
        memcpy(buffer, msgptrs[id]->message, msgptrs[id]->len);
        if (buffer[msgptrs[id]->len - 1] == 10) {
            buffer[msgptrs[id]->len - 1] = '\0';
        } else {
            buffer[msgptrs[id]->len - 2] = '\0';
        }
        sem_signal(sersems[id]);
       
        char *commands = (char*)malloc(sizeof(char) * (strlen(buffer) + 1));
        memcpy(commands, buffer, strlen(buffer) + 1);
        /* char *commands = buffer; */

        Command *head = parseCommands(buffer);
        Command *go = head;

        int retfd, status;
        int move = 0;
        int readfd = readfdlist[counter % 2000];
        while (go != NULL) {
            
            status = run(id, sockfd, readfd, go, counter, readfdlist, writefdlist, commands, msgptrs, sersems, clisems);
            /* printf("status = %d\n", status); */
            if (status == 0) {
                move = 1;
            } else if (status == 1) {
                break;
            }
            readfd = readfdlist[counter % 2000];
            while (go->next != NULL && (go->next->commandType == e_argv || 
                        go->next->commandType == e_stdout ||
                        go->next->commandType == e_stderr ||
                        go->next->commandType == e_outfile)) {
                go = go->next;
            }
            if (go->next != NULL && go->next->commandType == e_public_in && go->next->next != NULL) {
                go = go->next; 
            }
            go = go->next;
        }
        
        /* free linked list */
        Command *tmp;
        while (head != NULL) {
            tmp = head;
            head = head->next;
            free(tmp);
        }
        free(commands);

        if (status == -1) {

            int i;
            for (i = 0; i < 30; i++) {
                if (shmdt(msgptrs[i]) < 0) {
                    perror("ERROR on deattaching shared memory for clients data");
                    exit(1);
                }
            }
            int clishmid;
            Client* clientptr;
            if ((clishmid = shmget(CSHMKEY, sizeof(Client) * 30, 0)) < 0) {
                perror("ERROR on getting shared memory for clients data");
                exit(1);
            }

            if ((clientptr = (Client*)shmat(clishmid, (char*)0, 0)) == NULL) {
                perror("ERROR on attaching shared memory for clients data");
                exit(1);
            }

            /* remove client data from shared memory  */
            clientptr[id].sockfd = 0;
            memset(clientptr[id].nickname, 0, 1024);
            memset(clientptr[id].ip, 0, 16);
            clientptr[id].port = 0;
            return;
        }
    
        /* normal situation */
        if (move == 1) {
            /* reset writefd and readfd; */
            writefdlist[counter % 2000] = 0;
            readfdlist[counter % 2000] = 0;
            counter++;
            move = 0;
        }
        bzero(buffer, bufferSize);
    }
    return;
}

