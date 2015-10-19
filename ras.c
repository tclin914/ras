#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include <unistd.h>
#include <string.h>

#define DEBUG

void doprocessing(int sockfd); 
void handler(int fd);

int sockfd;

int main(int argc, const char *argv[])
{
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

void doprocessing(int sockfd) {
    int n;
    int bufferSize = 15001;
    char buffer[bufferSize];
    bzero(buffer, bufferSize);
    char *defaultPath[] = {"bin", "."};

    const char *welcome = 
        "****************************************\n"
        "** Welcome to the information server. **\n"
        "****************************************\n";
    const char *prompt = "% ";
    const char *commandset[] = { "exit", "|", "printenv", "setenv", "ls"};
    const char *unknown = "Unknown command: [%s].\n";

    n = write(sockfd, welcome, strlen(welcome) + 1);
    if (n < 0) {
        perror("ERROR writing welcome information to socket");
        exit(1);
    }

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

        // Replace enter key and next line character
        buffer[n] = 0;
        buffer[n - 1] = 0;
        buffer[n - 2] = '\0';

#ifdef DEBUG
        printf("Here is the message: %s\n", buffer);
        n = write(sockfd, "I got your message\n", 19);
#endif

        char first[257];
        char argv[257];
        bzero(first, 257);
        bzero(argv, 257);

        char *commands = buffer;
        for (unsigned pos = 0; pos < strlen(buffer); pos++) {

            while (*commands == ' ') {
                pos++;
                commands++;
            }

            char command[257];
            bzero(command, 257);

            int pfd[2];
            if (pipe(pfd) < 0) {
                printf("ERROR creating a pipe");
                exit(1);
            }
            
            printf("pfd[0]=%d pfd[1]=%d\n", pfd[0], pfd[1]);

            // split commands
            int count = 0;
            while (commands[count] != ' ' && commands[count] != '\0') {
                count++;
                pos++;
            }
            memcpy(command, commands, count);
            command[count] = '\0';
            commands += count;

            if(!first) {
                memcpy(first, command, count + 1);
            } else if (!argv) {
                memcpy(argv, command, count + 1);
            }

            // exit
            if (strcmp(command, commandset[0]) == 0) {
                close(sockfd);
                return;

            // |
            } else if (strcmp(command, commandset[1]) == 0) {
                              

            // printenv
            } else if (strcmp(command, commandset[2]) == 0) {
                char path[258]; // 256 + 1 + 1
                char paths[15001] = {'P', 'A', 'T', 'H', '=', '\0'};
                bzero(path, 258);

                if (sizeof(defaultPath) / sizeof(char*) > 0) {
                    strcat(paths, defaultPath[0]);
                }
                for (unsigned i = 1; i < sizeof(defaultPath) / sizeof(char*); i++) {
                    sprintf(path, ":%s", defaultPath[i]);   
                    strcat(paths, path);
                }
                strcat(paths, "\n");
                printf(paths);
                fflush(stdout);
                n  = write(sockfd, paths, strlen(paths));
                if (n < 0) {
                    perror("ERROR writing printenv");
                    exit(1);
                }
            // setenv
            } else if (strcmp(command, commandset[3]) == 0) {
            
            // ls
            } else if (strcmp(command, commandset[4]) == 0) {
                pid_t pid;
                int status;
                pid = fork();

                if (pid < 0) {
                    perror("ERROR on fork");
                    exit(1);
                }

                if (pid == 0) {
                    dup2(sockfd, 1);
                    close(pfd[0]);

                    execlp("./dir/bin/ls", "ls", "dir", NULL);              
                    exit(0);
                } else {
                    waitpid((pid_t)pid, &status, 0); 
                }   
                
            } else {
                // unknown command
                char cl[300];
                bzero(cl, 300);
                sprintf(cl, unknown, command);
                n = write(sockfd, cl, strlen(cl));
                if (n < 0) {
                    perror("ERROR writing unknown command");
                    exit(1);
                }
                break;
            }
        }
        dup2(sockfd, 1)
        bzero(buffer, bufferSize);
    }
    

    if (n < 0) {
        perror("ERROR writing to socket");
        exit(1);
    }
}

