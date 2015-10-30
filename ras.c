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
    char *path;
    char *command;
    struct Command *next;
} Command;

void doprocessing(int sockfd); 
void handler(int fd);
char *getExec(const char *path, const char *exec);
int run(int sockfd, int readfd, Command *command,int counter, int readfdlist[], int writefdlist[]);
int sockfd;

int main(int argc, const char *argv[])
{
    int ret = chdir("/u/gcs/103/0356100/ras/dir");

    int newsockfd, portno, clilen;
    char buffer[256];
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

    while (1) {
        newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);

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
            doprocessing(newsockfd);
            close(newsockfd);
            exit(0);
        } else {
            close(newsockfd);
        }   
    }
    
    close(sockfd);
    return 0;
}

char *defaultPath[] = {"bin", "."};

/* get executable file from env */
char *getExec(const char *path, const char *exec) {
    DIR *dirp;
    struct dirent *direntp;
    if ((dirp = opendir(path)) == NULL) {
        printf("ERROR opening directory fail");
        return NULL;
    }
    while ((direntp = readdir(dirp)) != NULL ) {
        if (strcmp(exec, direntp->d_name) == 0) {
            char *result = (char*)malloc(sizeof(char) * (strlen(path) + strlen(direntp->d_name) + 2));
            sprintf(result, "%s/%s", path, direntp->d_name);
            closedir(dirp);
            return result;
        }   
    }
    closedir(dirp);
    return NULL;
}

/* parse input command line */
Command *parseCommands(char *commands) {

    int num = 0;
    Command *head, *pre = NULL;
    int len = strlen(commands);
    for (unsigned pos = 0; pos < len;) {

        CommandType type;

        while (*commands == ' ') {
            pos++;
            commands++;
        }

        if (pre == NULL)
            type = e_proc;
        else
            type = e_argv;

        if (*commands == '|' && *(commands + 1) == ' ') {
            type = e_proc;    
            pos++;
            commands++;
        } else if (*commands == '>' && *(commands + 1) == ' ') {
            type = e_outfile;
            pos++;
            commands++;
        }

        while (*commands == ' ') {
            pos++;
            commands++;
        }

        if (*commands == '|') {
            type = e_stdout;
            pos++;
            commands++;
        }

        if (*commands == '!') {
            type = e_stderr;
            pos++;
            commands++;
        }

        /* split commands */
        int count = 0;
        while (commands[count] != ' ' && commands[count] != '\0') {
            count++;
            pos++;
        }

        char *command = (char*)malloc(sizeof(char) * (count + 1));
        memcpy(command, commands, count);
        command[count] = '\0';
        commands += count;

        Command *current = (Command*)malloc(sizeof(Command));

        current->path = NULL;
        /* get executable file by env */
        if (type == e_proc) {
            for (int i = 0; i < sizeof(defaultPath) / sizeof(char*); i++) {
                if ((current->path = getExec(defaultPath[i], command)) != NULL) {
                    break;
                }
            }
        }
        current->command = command;
        current->commandType = type;
        current->next = NULL;
        /* Example: */
        /* current->path = bin/ls */
        /* current->command = ls */
        /* current->commandType = e_proc */
        if (pre == NULL) { 
            head = current;
            pre = current;
        } else {
            pre->next = current;
            pre = current;
        }
    }
    return head;
}


int run(int sockfd, int readfd, Command *command,int counter, int readfdlist[], int writefdlist[]) {
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
    switch (command->commandType) {
        case e_proc:
            if (command->path == NULL) {
                /* exit command */
                if (strcmp(command->command, "exit") == 0) {
                    return 1;
                } else if (strcmp(command->command, "printenv")) {
                                       
                } else if (strcmp(command->command, "setenv")) {
                    
                }
                /* unknown command */
                fprintf(stderr, "Unknown Command: [%s].\n", command->command);
                fflush(stderr);
                return -1;
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

                    execv(command->path, arguments);
                    exit(0);
                } else {
                    waitpid((pid_t)pid, &status_pid, 0);
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
                    FILE *file;
                    file = fopen(args->next->command, "w");                    
                    dup2(readfd, STDIN); /* replace stdin with readfd */
                    dup2(fileno(file), STDOUT); /* replace stdout with fileno(file) */
                    dup2(sockfd, STDERR); /* replace stderr with sockfd */

                    execv(command->path, arguments);
                    exit(0);
                } else {
                    waitpid((pid_t)pid, &status_pid, 0);
                    return 0;
                }
            /* 1 numbered-pipe */
            } else if ((args->next->commandType == e_stdout ||
                    args->next->commandType == e_stderr) && args->next->next == NULL) {
                int std = args->next->commandType == e_stdout ? STDOUT : STDERR; /* stdout or stderr */
                
                int number = atoi(args->next->command);
                /* printf("writefdlist = %d\n", writefdlist[(counter + number) % 1000]);  */
                /* printf("(counter + number) % 1000 = %d\n", (counter + number) % 1000); */
                /* printf("counter = %d\n", counter); */
                /* printf("number = %d\n", number); */
                int writefd;

                /* close writefd */
                if (writefdlist[counter] != 0) {
                    close(writefdlist[counter]);
                }

                /* if pipe is not already exist */
                if ((writefd = writefdlist[(counter + number) % 1000]) == 0) {
                    if (pipe(pfd) < 0) {
                        perror("ERROR creating a pipe");
                        exit(1);
                    }   
                    /* printf("pfd[0] = %d pfd[1] = %d\n", pfd[0], pfd[1]); */
                    
                    pid = fork();

                    if (pid < 0) {
                        perror("ERROR on fork");
                        exit(1);
                    }

                    if (pid == 0) {
                        dup2(pfd[1], std); /* replace stdout or stderr with pfd[1] */
                        dup2(readfd, STDIN); /* replace stdin with readfd */
                        close(pfd[0]);

                        execv(command->path, arguments);
                        exit(0);
                    } else {
                        waitpid((pid_t)pid, &status_pid, 0);
                        writefdlist[(counter + number) % 1000] = pfd[1];
                        readfdlist[(counter + number) % 1000] = pfd[0];
                      
                        return 0;    
                    }
                /* if pipe is exist */
                } else {
                    pid = fork();
                    if (pid == 0) {
                        dup2(writefd, std); /* replace stdout or stderr with writefd */
                        dup2(readfd, STDIN); /* replace stdin with readfd */
                        
                        execv(command->path, arguments);
                        exit(0);
                    } else {
                        waitpid((pid_t)pid, &status_pid, 0);
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
                int writefd_stdout = writefdlist[(counter + num_stdout) % 1000];
                int writefd_stderr = writefdlist[(counter + num_stderr) % 1000];

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

                            execv(command->path, arguments);
                            exit(0);
                        } else {
                            waitpid((pid_t)pid, &status_pid, 0);
                            
                            writefdlist[(counter + num_stdout) % 1000] = pfd_stdout[1];
                            writefdlist[(counter + num_stderr) % 1000] = pfd_stderr[1];
                            readfdlist[(counter + num_stdout) % 1000] = pfd_stdout[0];
                            readfdlist[(counter + num_stderr) % 1000] = pfd_stderr[0];
                            
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

                            execv(command->path, arguments);
                            exit(0);
                        } else {
                            waitpid((pid_t)pid, &status_pid, 0);
                                
                            writefdlist[(counter + num_stdout) % 1000] = pfd[1];
                            readfdlist[(counter + num_stdout) % 1000] = pfd[0];
                            return 0;
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

                        execv(command->path, arguments);
                        exit(0);
                    } else {
                        waitpid((pid_t)pid, &status_pid, 0);
                        
                        writefdlist[(counter + num_stderr) % 1000] = pfd[1];
                        readfdlist[(counter + num_stderr) % 1000] = pfd[0];
                        return 0;
                    }
                /* numbered-pipe stderr is created, but stdout not yet */
                } else if (writefd_stdout == 0 && writefd_stderr != 0) {
                    
                }               
            } else {
                /* NOTE:Create needed pipe first, then close writefd of current counter */
                if (pipe(pfd) < 0) {
                    perror("ERROR creating a pipe");
                    exit(1);
                }
                
                /* close writefd */
                if (writefdlist[counter] != 0) {
                    close(writefdlist[counter]);
                }

                pid = fork();
                
                   
                if (pid == 0) {
                    dup2(readfd, STDIN); /* replace stdin with readfd  */
                    dup2(pfd[1], STDOUT); /* replace stdout with pfd[1] */
                    dup2(sockfd, STDERR); /* replace stderr with sockfd */
                    close(pfd[0]);

                    execv(command->path, arguments);
                    exit(0);
                } else {
                    close(pfd[1]);
                    waitpid((pid_t)pid, &status_pid, 0);
                   
                    readfdlist[counter] = pfd[0];
                    
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

void doprocessing(int sockfd) {
    int n;
    int bufferSize = 15001;
    char buffer[bufferSize];
    bzero(buffer, bufferSize);

    const char *welcome = 
        "****************************************\n"
        "** Welcome to the information server. **\n"
        "****************************************\n";

    const char prompt[] = {'%', ' ', 13, '\0'};
    
    printf(welcome);
    fflush(stdout);
    int readfdlist[1000] = {0};
    int writefdlist[1000] = {0};
    int counter = 0;

    while (1) {

        printf(prompt);
        fflush(stdout);

        n = readline(sockfd, buffer, sizeof(buffer) - 1);
        /* remove 13 enter key and 10 next key */
        buffer[n - 2] = '\0';

        char *commands = buffer;
        Command *head = parseCommands(commands);

        Command *go = head;
        int retfd, status;
        int readfd = readfdlist[counter % 1000];
        while (go != NULL) {
            
            status = run(sockfd, readfd, go, counter, readfdlist, writefdlist);
            
            /* Unknown command */
            if (status == -1) {
                break;
            }

            readfd = readfdlist[counter % 1000];
            while (go->next != NULL && (go->next->commandType == e_argv || 
                        go->next->commandType == e_stdout ||
                        go->next->commandType == e_stderr ||
                        go->next->commandType == e_outfile)) {
                go = go->next;
            }
            go = go->next;
        }

        /* normal situation */
        if (status == 0) {
            /* reset writefd and readfd; */
            writefdlist[counter % 1000] = 0;
            readfdlist[counter % 1000] = 0;
            counter++;   
        /* unknown command */
        } else if (status == -1) {
            /* reset writefd and readfd; */
            writefdlist[counter % 1000] = 0;
            readfdlist[counter % 1000] = 0;
        /* exit */
        } else if (status == 1) {
            return;
        }

        /* free linked list */
        Command *tmp;
        while (head != NULL) {
            tmp = head;
            head = head->next;
            free(tmp);
        }
        bzero(buffer, bufferSize);
    }
    if (n < 0) {
        perror("ERROR writing to socket");
        exit(1);
    }
}

