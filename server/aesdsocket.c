#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <syslog.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <linux/fs.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#define BACKLOG 15
#define INIT_BUF_SIZE 1024

int fd;
int sfd;
int client_fd;
char *storage_buffer;
char *read_buffer;

void sig_handler(int signum)
{
    if(signum == SIGINT)
    {
        syslog(LOG_DEBUG, "SIGINT Caught signal, exiting");
    }
    else if(signum == SIGTERM)
    {
        syslog(LOG_DEBUG, "SIGTERM Caught signal, exiting");
    }

    // Free malloced memory.
    if(storage_buffer)
    {
        free(storage_buffer);
        storage_buffer = NULL;
    }

    // free read buffer
    if(read_buffer)
    {
        free(read_buffer);
        read_buffer = NULL;
    }

    //Close client_fd
    if(-1 == close(client_fd))
    {
        syslog(LOG_ERR, "Either already closed/ unable to close cliend fd: %d",errno);
    }
    //Close fd
    if(-1 == close(fd))
    {
        syslog(LOG_ERR, "Error: close fd: %d",errno);
    }

    //unlink the file
    if(-1 ==unlink("/var/tmp/aesdsocketdata"))
    {
        syslog(LOG_ERR, "Unlin aesdsocketdata failed: %d",errno);
    }

    //Close syslog
    closelog();
    exit(EXIT_SUCCESS);
}

static int daemon_create()
{
    /* Create new process */
    pid_t pid = fork();
    if(pid == -1)
    {
        return -1;
    }
    else if(pid != 0)
    {
        exit(EXIT_SUCCESS);
    }
    /* Session ID create */
    if(setsid() == -1)
    {
        return -1;
    }
    /* Set the working directory to root */
    if(chdir("/") == -1)
    {
        return -1;
    }
    
    /* Set speacial fds */
    open("/dev/null", O_RDWR);  //fd = 0
    open("/dev/null", O_RDWR);  //fd = 1
    open("/dev/null", O_RDWR);  //fd = 2
    
    return 0;
}

int main(int argc, char **argv)
{
    openlog(NULL, 0, LOG_USER);

    //Daemon flag if -d is passed as an argument.
    int daemon_fg = 0;

    //signal handling

    if(argc > 1)
    {
        if(strcmp(argv[1], "-d") == 0)
        {
            daemon_fg = 1;
        }
        else
        {
            printf("Invalid argument to process daemon\n");
        }
    }

    signal(SIGINT, sig_handler);        //Signal handler for SIGINT
    signal(SIGTERM, sig_handler);       //Signal handler for SIGTERM

    //Socket variables.
    int sock_status = 0;
    struct addrinfo hints;
    struct addrinfo *servinfo = NULL;

    memset(&hints, 0, sizeof(hints));   // clear the struct
    hints.ai_family = AF_INET;          // IPv4
    hints.ai_socktype = SOCK_STREAM;    // TCP socket
    hints.ai_flags = AI_PASSIVE;        // Query for IP

    // get server info
    sock_status = getaddrinfo(NULL, "9000", &hints, &servinfo);
    if(0 != sock_status)
    {
        perror("getaddrinfo failure: ");
        return -1;
    }
    if(!servinfo)
    {
        perror("servinfo struct was not populated: ");
        return -1;
    }

    // create socket and get file descriptor
    sfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if(-1 == sfd)
    {
        perror("Failed to get socket fd: ");
        freeaddrinfo(servinfo);
        return -1;
    }

    // reuse the socket port
    int sock_opt = 1;
    sock_status = setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &sock_opt, sizeof(sock_opt));
    if(-1 == sock_status)
    {
        perror("setsockopt failed: ");
        freeaddrinfo(servinfo);
        return -1;
    }

    // bind the socket to port
    sock_status = bind(sfd, servinfo->ai_addr, sizeof(struct addrinfo));
    if(-1 == sock_status)
    {
        perror("socket bind failure: ");
        freeaddrinfo(servinfo);
        return -1;
    }

    // free the servinfo
    freeaddrinfo(servinfo);
    servinfo = NULL;

    // Create Daemon
    if(daemon_fg)
    {
        if(daemon_create() == -1)
        {
            syslog(LOG_ERR, "Error creating Daemon");
        }
        else
        {
            syslog(LOG_DEBUG, "Daemon created successfully");
        }
    }

    sock_status = listen(sfd, BACKLOG);
    if(-1 == sock_status)
    {
        perror("socket listen failure: ");
        return -1;
    }

    /* Buffer for receiving data from client */
    storage_buffer = (char *)malloc(INIT_BUF_SIZE);
    if(!storage_buffer)
    {
        perror("malloc failure: ");
        return -1;
    }

    /* Open a file */
    fd = open("/var/tmp/aesdsocketdata", (O_CREAT | O_TRUNC | O_RDWR), (S_IRWXU | S_IRWXG | S_IROTH));
    if(fd == -1)
    {
        perror("Error opening socket file:");
        free(storage_buffer);
        storage_buffer = NULL;
        return -1;
    }

    /* Static buffer for receiving data from client */
    char buffer[INIT_BUF_SIZE];                 // Static buffer of 1024 bytes to receive data
    int storage_buffer_size = INIT_BUF_SIZE;    // Static buffer size
    int storage_buffer_size_cnt = 1;            // Count to indicate the number of string received from client

    int complete_flag = 0;                      // Idetentifier of string complete.

    struct sockaddr_storage test_addr;          // Test addr to populate from accept()
    socklen_t addr_size = sizeof(test_addr);    // Size of test addr
    char address_string[INET_ADDRSTRLEN];       // Holds the IP address from client
    int actual_bytes_rcvd = 0;
    int bytes_rcvd;                             // Stores the bytes received from recv function
    int bytes_to_write;                         // Number of bytes to be written to fd.
    int file_bytes = 0;                         // Number of bytes present in fd

    while(1)
    {
        // Connection with the client
        client_fd = accept(sfd, (struct sockaddr *)&test_addr, &addr_size);
        if(client_fd == -1)
        {
            perror("accept failure: ");
            continue;                           // Try again
        }

        // Print the IP address of the client.
        struct sockaddr_in *p = (struct sockaddr_in *)&test_addr;
        syslog(LOG_DEBUG, "Accepted connection from %s", inet_ntop(AF_INET, &p->sin_addr, address_string, sizeof(address_string)));

        // Receive bytes by polling the client fd
        while((bytes_rcvd = recv(client_fd, buffer, sizeof(buffer), 0)) > 0)
        {
            printf("bytes rcvd = %d\n", bytes_rcvd);

            for(int i=0; i < bytes_rcvd; i++)
            {
                if(buffer[i] == '\n')
                {
                    complete_flag = 1;
                    actual_bytes_rcvd = i+1;
                    break;
                }
            }
            memcpy(storage_buffer + (storage_buffer_size_cnt - 1)*INIT_BUF_SIZE, buffer, bytes_rcvd);

            if(complete_flag == 1)
            {
                bytes_to_write = (storage_buffer_size_cnt - 1)*INIT_BUF_SIZE + actual_bytes_rcvd;
                break;
            }
            else    // reallocated the buffer by multiples of 1024 until complete flag is set
            {
                char *temp_ptr = (char *)realloc(storage_buffer, (storage_buffer_size + INIT_BUF_SIZE));
                if(temp_ptr)
                {
                    storage_buffer = temp_ptr;
                    storage_buffer_size += INIT_BUF_SIZE;
                    storage_buffer_size_cnt += 1;
                }
                else
                {
                    printf("No memory available to alloc\n");
                }
            }
        }
        printf("Storage buffer size = %d\n", bytes_to_write);

        // If transmission is complete,
        // Copy to local fd and then rewrite/resend it back to client.
        if(1 == complete_flag)
        {
            complete_flag = 0;

            //Write to file
            int wr_bytes = write(fd, storage_buffer, bytes_to_write);
            file_bytes += wr_bytes;

            //Set the fd to start of the file.
            lseek(fd, 0, SEEK_SET);

            read_buffer = (char *)malloc(file_bytes);
            if(!read_buffer)
            {
                printf("Read buffer malloc failed\n");
            }
            else
            {
                //Reading buffer part
                ssize_t rd_bytes = read(fd, read_buffer, file_bytes);
                if(rd_bytes == -1)
                {
                    perror("read from fd failed: ");
                }

                //return the bytes to the client
                int bytes_sent = send(client_fd, read_buffer, file_bytes, 0);
                if(bytes_sent == -1)
                {
                    perror("send to client failed: ");
                }
                free(read_buffer);
                read_buffer = NULL;
            }
            //Cleanup
            storage_buffer = realloc(storage_buffer, INIT_BUF_SIZE);
            storage_buffer_size_cnt = 1;
            storage_buffer_size = INIT_BUF_SIZE;
        }
        close(client_fd);
        syslog(LOG_DEBUG, "Closed connection from %s", address_string);
    }
    return 0;
}