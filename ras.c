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

/* #define DEBUG */

void doprocessing(int sockfd); 
void handler(int fd);

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
            doprocessing(newsockfd);
            printf("Done\n");
            exit(0);
        } else {
            close(newsockfd);
        }   
    }
    
    close(sockfd);

    return 0;
}

typedef enum { 
    E_proc = 0, 
    E_argv = 1, 
    E_stdout = 2, 
    E_stderr = 3,
    E_outfile = 4
} CommandType;

typedef struct Command {
    CommandType commandType;
    char *path;
    char *command;
    struct Command *next;
} Command;

const char *unknown = "Unknown command: [%s].\n";
char *defaultPath[] = {"bin", "."};

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

Command *parseCommands(char *commands) {

    int num = 0;
    Command *head, *pre = NULL;
    int len = strlen(commands);
    for (unsigned pos = 0; pos < len; pos++) {

        CommandType type;

        while (*commands == ' ') {
            pos++;
            commands++;
        }

        if (pre == NULL)
            type = E_proc;
        else
            type = E_argv;

        if (*commands == '|' && *(commands + 1) == ' ') {
            type = E_proc;    
            pos++;
            commands++;
        } else if (*commands == '>' && *(commands + 1) == ' ') {
            type = E_outfile;
            pos++;
            commands++;
        }

        while (*commands == ' ') {
            pos++;
            commands++;
        }

        if (*commands == '|') {
            type = E_stdout;
            pos++;
            commands++;
        }

        if (*commands == '!') {
            type = E_stderr;
            pos++;
            commands++;
        }

        // split commands
        int count = 0;
        while (commands[count] != ' ' && commands[count] != '\0') {
            count++;
            pos++;
        }

        char *command = (char*)malloc(sizeof(char) * (count + 1));
        memcpy(command, commands, count);
        command[count] = '\0';
        commands += count;

        if (command[0] == '|') {
            type = E_stdout;
        }

        if (command[0] == '!') {
            type = E_stderr;
        }

        Command *current = (Command*)malloc(sizeof(Command));

        current->path = NULL;
        // get executable file by env
        if (type == E_proc) {
            for (int i = 0; i < sizeof(defaultPath) / sizeof(char*); i++) {
                if ((current->path = getExec(defaultPath[i], command)) != NULL) {
                    break;
                }
            }
        }
        current->command = command;
        current->commandType = type;
        /* printf("================\n"); */
        /* printf("command = %s\n", command); */
        /* printf("commandType = %d\n", type); */
        /* printf("commandPath = %s\n", current->path); */
        /* printf("================\n"); */
        // Example:
        // current->path = bin/ls
        // current->command = ls
        // current->commandType = E_proc
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


int run(int sockfd, int readfd, Command *command, int *index, int *status) {
    Command *proc = command;
    Command *temp = command;

    int count = 0;
    // get arguments
    while (command->next != NULL && command->next->commandType == E_argv) {
        command = command->next;
        count++;
    }

    char *arguments[count + 1];
    bzero(arguments, count + 1);
    int n;
    char buf[277]; // 256 + 20 + 1 unknown
    switch (proc->commandType) {
        case E_proc:
            if (proc->path == NULL) {
                if (strcmp(proc->command, "exit") == 0) {
                    printf("exit");
                    close(sockfd);
                    exit(0);
                }
                // unknown command
                bzero(buf, 277);
                printf("debug\n");
                sprintf(buf, unknown, proc->command);
                n = write(sockfd, buf, strlen(buf));
                if (n < 0) {
                    perror("ERROR writing unknown command to socket");       
                }        
                *index = 1;
                *status = -1;
                return readfd;
            }
            // get arguments
            arguments[0] = proc->command;
            for (unsigned i = 1; i < count + 1; i++) {
                arguments[i] = temp->next->command;
                temp = temp->next;
            }
            arguments[count + 1] = NULL;
                    
            pid_t pid;
            int status_pid;

            int pfd[2];
            if (pipe(pfd) < 0) {
                printf("ERROR creating a pipe");
                exit(1);
            }

            pid = fork();

            if (pid < 0) {
                perror("ERROR on fork");
                exit(1);
            }

            if (pid == 0) {
                        
                printf("pfd[0] = %d pfd[1] = %d\n", pfd[0], pfd[1]);
                printf("readfd = %d\n", readfd);

                if (command->next == NULL) { // command end
                    dup2(sockfd, 1); // replace stdout with sockfd
                    dup2(sockfd, 2); // replace stderr with sockfd
                    dup2(readfd, 0); 
                    close(pfd[0]);
                    close(pfd[1]);
                } else if(command->next->commandType == E_outfile) { // redirect data to file (> xxx)
                    FILE *file;
                    file = fopen(command->next->command, "w");
                    dup2(fileno(file), 1);
                    dup2(readfd, 0);
                    close(pfd[0]);
                    close(pfd[1]);
                } else {
                    dup2(pfd[1], 1);
                    dup2(readfd, 0);
                    close(pfd[0]);
                }

                execv(proc->path, arguments);
                exit(0);
            } else {
                close(pfd[1]);
                waitpid((pid_t)pid, &status_pid, 0); 
                
                *index = 0;
                *status = 0;
                return pfd[0];
            }   
            break;
        case E_argv:

            break;

        case E_stdout:
            printf("DEBUG %s\n", proc->command);          
            *index = atoi(proc->command);
            *status = 0;
            return readfd;
            break;

        case E_stderr:

            break;   

        case E_outfile:
            
            break;
    }
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
    const char *prompt = "% ";

    n = write(sockfd, welcome, strlen(welcome) + 1);
    if (n < 0) {
        perror("ERROR writing welcome information to socket");
        exit(1);
    }
    int fdlist[1000] = {0};
    int counter = 0;

    while (1) {
        n = write(sockfd, prompt, strlen(prompt));
        if (n < 0) {
            perror("ERROR writing prompt to socket");       
        }

        n = read(sockfd, buffer, bufferSize - 1);
        if (n < 0) {
            perror("ERROR reading from socket");
            exit(1);
        }

        // replace enter key and next line character
        buffer[n] = 0;
        buffer[n - 1] = 0;
        buffer[n - 2] = '\0';

#ifdef DEBUG
        printf("Here is the message: %s\n", buffer);
        n = write(sockfd, "I got your message\n", 19);
#endif

        char *commands = buffer;
        Command *head = parseCommands(commands);
        Command *go = head;
        int index , status = 0;
        int retfd;
        int readfd = fdlist[counter % 1000];
        printf("counter = %d\n", counter);
        while (go != NULL && status == 0) {
            printf("commandPath = %s\n", go->path);
            printf("command = %s\n", go->command);
            printf("type = %d\n", go->commandType);
            fflush(stdout);
            
            retfd = run(sockfd, readfd, go, &index, &status);
 
            fdlist[(counter + index) % 1000] = retfd;
            printf("fdlist[(%d + %d) % 1000] = %d\n", counter, index, retfd);

            readfd = fdlist[counter % 1000];
            printf("retfd = %d\n", retfd);
            fflush(stdout);
            while (go->next != NULL && go->next->commandType == E_argv) {
                go = go->next;
            }
            go = go->next;
        }
        // free linked list
        Command *tmp;
        while (head != NULL) {
            tmp = head;
            head = head->next;
            free(tmp);
        }
        // reset
        fdlist[counter % 1000] = 0;
        counter++;
        bzero(buffer, bufferSize);
    }
    if (n < 0) {
        perror("ERROR writing to socket");
        exit(1);
    }
}

