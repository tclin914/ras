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
    e_outfile = 4
} CommandType;

typedef struct Command {
    CommandType commandType;
    char *command;
    struct Command *next;
} Command;

typedef struct {
    unsigned int sockfd;
    char nickname[1024];
    char ip[16];
    unsigned short port;
} Client;

typedef enum {
    e_command = 0,
    e_message = 1
} MsgType;

typedef struct {
    int len;
    MsgType type;
    char message[1024];
} Msg;

void doprocessing(int id, int sockfd); 
void handler(int sig);
int run(int id, int sockfd, int readfd, Command *command,int counter, int readfdlist[], int writefdlist[]);
int readline(int fd,char *ptr,int maxlen); 
int sockfd;
Client *clientptr;
int clishmid;

Msg *msgptrs[30];
int msgshmids[30];
int clisems[30], sersems[30];

int main(int argc, const char *argv[])
{
    int ret = chdir("/u/gcs/103/0356100/ras/dir");

    signal(SIGINT, handler);

    int newsockfd, portno, clilen;
    struct sockaddr_in serv_addr, cli_addr;
    int n, pid;

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
    fd_set afds;
    int nfds, fd;

    nfds = getdtablesize();
    FD_ZERO(&afds);
    FD_SET(sockfd, &afds);

	char buffer[15001];
	bzero(buffer, 15001);

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
    int fds[30] = {0};
    while (1) {
        memcpy(&rfds, &afds, sizeof(rfds));
        memcpy(&wfds, &afds, sizeof(wfds));
        if (select(nfds, &rfds, (fd_set*)0, (fd_set*)0, (struct timeval*)0) < 0) {
            perror("ERROR on select");
            exit(1);
        }

        if (FD_ISSET(sockfd, &rfds)) {
            printf("debug\n");        
			newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
		
			/* set client data to shared memory  */
			int i;
			for (i = 0; i < 30; i++) {
				if (clientptr[i].sockfd == 0) {
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
                printf("fork\n");
				close(sockfd);
				dup2(newsockfd, 0);
				dup2(newsockfd, 1);
				dup2(newsockfd, 2);
				doprocessing(newsockfd, i);
				close(newsockfd);
                fds[i] = 0;
                FD_CLR(newsockfd, &afds);
				exit(0);
			} else {
				FD_SET(newsockfd, &afds);
			}
		}

        for (fd = 0; fd < nfds; fd++) {
            if (fd != sockfd && FD_ISSET(fd, &rfds)) {
        		n = readline(fd, buffer, sizeof(buffer) - 1);

                int l;
                for (l = 0; l < 30; l++) {
                    if (fds[l] == fd) {
                        break;
                    }
                }

                sem_wait(sersems[l]);
                memcpy(msgptrs[l]->message, buffer, strlen(buffer) + 1);
                msgptrs[l]->len = n;
                sem_signal(clisems[l]);
            }

            if (fd != sockfd && FD_ISSET(fd, &wfds)) {
                printf("wfds %d\n", fd);
            }
        }
 		bzero(buffer, 15001);   
    }

	close(sockfd);
    return 0;
}

void handler(int sig) {
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
    exit(0);
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


int run(int id, int sockfd, int readfd, Command *command,int counter, int readfdlist[], int writefdlist[]) {
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

                if ((clishmid = shmget(CSHMKEY, sizeof(Client) * 30, 0)) < 0) {
                    perror("ERROR on getting shared memory for clients data");
                    exit(1);
                }

                if ((clientptr = (Client*)shmat(clishmid, (char*)0, 0)) == NULL) {
                    perror("ERROR on attaching shared memory for clients data");
                    exit(1);
                }

                /* remove client data from shared memory */
                clientptr[id].sockfd = 0;
                memset(clientptr[id].nickname, 0, 1024);
                memset(clientptr[id].ip, 0, 16);
                clientptr[id].port = 0;
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
            /* 1 numbered-pipe */
            } else if ((args->next->commandType == e_stdout ||
                    args->next->commandType == e_stderr) && args->next->next == NULL) {
                int std = args->next->commandType == e_stdout ? STDOUT : STDERR; /* stdout or stderr */
                
                int number = atoi(args->next->command);
                int writefd;

                /* close writefd */
                if (writefdlist[counter] != 0) {
                    close(writefdlist[counter]);
                    /* printf("close write = %d\n", writefdlist[counter]); */
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
                            readfdlist[counter] = pfd[0];                    
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

    int msgshmid;
    Msg *msgptr;
    /* get shared memory */
    if ((msgshmid = shmget(MSHMKEY + id, sizeof(Msg), 0)) < 0) {
		perror("ERROR on creating shared memory for clients data");
		exit(1);
	}
	if ((msgptr = (Msg*)shmat(msgshmid, (char*)0, 0)) == NULL) {
		perror("ERROR on attaching shared memory for clients data");
		exit(1);
    }
    int clisem, sersem;
    /* open semaphore */
    if ((clisem = sem_open(SEMKEY1 + id)) < 0) {
        perror("ERROR on opening semaphore");
        exit(1);
    }
    if ((sersem = sem_open(SEMKEY2 + id)) < 0) {
        perror("ERROR on opening semaphore");
        exit(1);
    }

    while (1) {
        printf(prompt);
        fflush(stdout);
	
        sem_wait(clisem);
        
        if (msgptr->len == 0) {
            break;
        } else if (msgptr->len == 2) {
            continue;
        }
        /* remove 13 enter key and 10 next key */
        memcpy(buffer, msgptr->message, strlen(msgptr->message) + 1);
        buffer[msgptr->len - 2] = '\0';
        
        sem_signal(sersem);
        
        char *commands = buffer;
        Command *head = parseCommands(commands);
        Command *go = head;

        /* while (go != NULL) { */
            /* printf("command path = %s\n", go->path); */
            /* printf("command = %s\n", go->command); */
            /* printf("command type = %d\n", go->commandType); */
            /* go = go->next; */
        /* } */

        int retfd, status;
        int move = 0;
        int readfd = readfdlist[counter % 2000];
        /* printf("readfd %d\n", readfd); */
        /* fflush(stdout); */
        while (go != NULL) {
            
            status = run(id, sockfd, readfd, go, counter, readfdlist, writefdlist);
            /* printf("status = %d\n", status); */
            /* fflush(stdout); */
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
            go = go->next;
        }
        
        /* free linked list */
        Command *tmp;
        while (head != NULL) {
            tmp = head;
            head = head->next;
            free(tmp);
        }

        if (status == -1) {
            return;
        }
    
        /* normal situation */
        if (move == 1) {
            /* reset writefd and readfd; */
            writefdlist[counter % 2000] = 0;
            readfdlist[counter % 2000] = 0;
            /* printf("counter++\n"); */
            /* fflush(stdout); */
            /* printf("counter = %d\n", counter); */
            counter++;
            move = 0;
        }
        bzero(buffer, bufferSize);
    }
    return;
}

