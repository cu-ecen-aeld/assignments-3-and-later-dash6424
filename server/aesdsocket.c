/*
 * Filename: aesdsocket.c
 * File Description:
 * This file contains the socket implementation for A5.
 * This code has been compiled with GCC using VS Code Editor.
 * Author: Daanish Shariff
 */

/*==========================================================================
  Include files
========================================================================== */
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
#include <pthread.h>
#include <stdbool.h>
#include "../aesd-char-driver/aesd_ioctl.h"

/*==========================================================================
  MACROS
========================================================================== */
/* Comment below line for normal AESD Socket test */
#define USE_AESD_CHAR_DEVICE 1

#define BACKLOG 15
#define INIT_BUF_SIZE 1024

#ifndef USE_AESD_CHAR_DEVICE
#define SOCKET_ADDR "/var/tmp/aesdsocketdata"
#define TIME_PERIOD 10
#else
#define SOCKET_ADDR "/dev/aesdchar"
#endif

/*==========================================================================
  Global Declarations
========================================================================== */
int complete_exec = 0;

typedef struct
{
    pthread_t thread_id;
    int complete_flag;
    int client_fd;
    int fd;
    struct sockaddr_in *client_addr;
#ifndef USE_AESD_CHAR_DEVICE
    pthread_mutex_t *mutex;
#endif
}thread_data_t;

typedef struct node
{
    thread_data_t thread_data;
    struct node *next;
}node_t;

#ifndef USE_AESD_CHAR_DEVICE
typedef struct
{
    pthread_t thread_id;
    int fd;
    pthread_mutex_t *mutex;
}timer_node_t;
#endif

/*==========================================================================
  Function Declarations
========================================================================== */
/* Description: ll_clean
 * Clears the linked list and joins the
 * threads associated with each ll node
 *
 * Parameters:
 * head	: Points to the head node of ll
 * is_clear : TRUE - Free all the nodes
 *            False - Free node only if thread complete flag is set.
 *
 * Return Type:
 * void     : No return type required.
 */
static void ll_clean(node_t **head, bool is_clear);

/*==========================================================================
  Function Definitions
========================================================================== */
/* Description: sig_handler
 * Signal Handler function to terminate process
 * based on signal received from SIGINT or SIGTERM.
 * Ensure only reentrant calls are made in sig_handler.
 *
 * Parameters:
 * signum	: Holds the signal number received.
 *
 * Return Type:
 * void     : No return type required.
 */
void sig_handler(int signum)
{
    if((signum == SIGINT) || signum == SIGTERM)
    {
        complete_exec = 1;
    }
}

/* Description: signal_init
 * Initialize signal handler for SIGINT & SIGTERM
 *
 * Parameters:
 * void     : No parameters required.
 *
 * Return Type:
 * int      : 0 on Success. Exit on failure.
 */
int signal_init()
{
    struct sigaction sig_action;
    sig_action.sa_handler = &sig_handler;

    sigfillset(&sig_action.sa_mask);

    sig_action.sa_flags = 0;

    if(-1 == sigaction(SIGINT, &sig_action, NULL))
    {
        perror("sigaction failed: ");
        exit(EXIT_FAILURE);
    }

    if(-1 == sigaction(SIGTERM, &sig_action, NULL))
    {
        perror("sigaction failed: ");
        exit(EXIT_FAILURE);
    }
    return 0;
}

/* Description: daemon_create
 * Creates a daemon background process for running
 * the server.
 *
 * Parameters:
 * void     : No return type required.
 *
 * Return Type:
 * void     : No return type required.
 */
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

#ifndef USE_AESD_CHAR_DEVICE
void *timer_thread(void *thread_params)
{
    if(NULL == thread_params)
    {
        return NULL;
    }
    timer_node_t *thread_data = (timer_node_t *)thread_params;
    struct timespec ts;
    time_t t;
    struct tm* temp;

    while(!complete_exec)
    {
        char buffer[INIT_BUF_SIZE] = {0};

        if(0 !=clock_gettime(CLOCK_MONOTONIC, &ts))
        {
            perror("Clock get time failed: ");
            break;
        }
        ts.tv_sec += TIME_PERIOD;

        /* If signal received exit thread */
        if(complete_exec)
        {
            pthread_exit(NULL);
        }

        if(0 != clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL))
        {
            perror("clock nanosleep failed: ");
            break;
        }
        /* If signal received exit thread */
        if(complete_exec)
        {
            pthread_exit(NULL);
        }
        
        t = time(NULL);
        if (t == ((time_t) -1))
        {
            perror("time since epoch failed: ");
            break;
        }

        temp = localtime(&t);
        if (temp == NULL)
        {
            perror("Local time failed: ");
            break;
        }

        int len = strftime(buffer, INIT_BUF_SIZE, "timestamp: %Y, %b, %d, %H:%M:%S\n", temp);
        if(len == 0)
        {
            syslog(LOG_ERR, "Failed to get time");
        }

        if (0 != pthread_mutex_lock(thread_data->mutex)) {
            perror("Mutex lock failed: ");
            break;
        }

        if (-1 == write(thread_data->fd, buffer, len))
        {
            perror("write timestamp failed: ");
        }

        if (pthread_mutex_unlock(thread_data->mutex) != 0) {
            perror("Mutex unlock failed: ");
            break;
        }
    }
    return NULL;
}
#endif

void *thread_socket(void *thread_params)
{
    if(NULL == thread_params)
    {
        return NULL;
    }
    thread_data_t *thread_data = (thread_data_t*)thread_params;

    char address_string[INET_ADDRSTRLEN];       // Holds the IP address from client
    syslog(LOG_DEBUG, "Accepted connection from %s", inet_ntop(AF_INET, &(thread_data->client_addr->sin_addr), address_string, sizeof(address_string)));
    
    /* Buffer for receiving data from client */
    char *storage_buffer = (char *)malloc(INIT_BUF_SIZE);
    if(!storage_buffer)
    {
        perror("malloc failure: ");
        goto err_handling;
    }

    /* Static buffer for receiving data from client */
    char buffer[INIT_BUF_SIZE];                 // Static buffer of 1024 bytes to receive data
    int storage_buffer_size = INIT_BUF_SIZE;    // Static buffer size

    int storage_buffer_size_cnt = 1;            // Count to indicate the number of string received from client

    int complete_flag = 0;                      // Idetentifier of string complete.


    
    int actual_bytes_rcvd = 0;
    int bytes_rcvd = 0;                             // Stores the bytes received from recv function
    int bytes_to_write = 0;                         // Number of bytes to be written to fd.
    ssize_t rd_bytes = 0;

    // Receive bytes by polling the client fd for every 1024 bytes
    while((bytes_rcvd = recv(thread_data->client_fd, buffer, INIT_BUF_SIZE, 0)) > 0)
    {
        for(int i=0; i < bytes_rcvd; i++)
        {
            if(buffer[i] == '\n')
            {
                complete_flag = 1;
                actual_bytes_rcvd = i+1;
                break;
            }
        }
        if(complete_flag == 1)
        {
            memcpy(storage_buffer + (storage_buffer_size_cnt - 1)*INIT_BUF_SIZE, buffer, actual_bytes_rcvd);
            bytes_to_write = (storage_buffer_size_cnt - 1)*INIT_BUF_SIZE + actual_bytes_rcvd;
            break;
        }
        else    // reallocated the buffer by multiples of 1024 until complete flag is set
        {
            memcpy(storage_buffer + (storage_buffer_size_cnt - 1)*INIT_BUF_SIZE, buffer, bytes_rcvd);
            char *temp_ptr = (char *)realloc(storage_buffer, (storage_buffer_size + INIT_BUF_SIZE));
            if(temp_ptr)
            {
                storage_buffer = temp_ptr;
                storage_buffer_size += INIT_BUF_SIZE;
                storage_buffer_size_cnt++;
            }
            else
            {
                syslog(LOG_ERR,"No memory available to alloc");
            }
        }
    }
    syslog(LOG_DEBUG, "Storage buffer size = %d", bytes_to_write);
    if(-1 == bytes_rcvd)
    {
        perror("receive failed: ");
        goto err_handling;
    }

    // If transmission is complete,
    // Copy to local fd and then rewrite/resend it back to client.
    if(1 == complete_flag)
    {
        complete_flag = 0;

        /* Malloc 1kB for reading the buffer */
        char read_buffer[INIT_BUF_SIZE] = {0};

        /* IOCTL operation */
        bool ioctl_flag = 0;
        char *ioctl_buff = storage_buffer;
        char *ioctl_str =  "AESDCHAR_IOCSEEKTO:";
        /* Check if the recvd string is related to ioctl operation */
        if(0 == strncmp(ioctl_buff, ioctl_str, strlen(ioctl_str)))
        {
            struct aesd_seekto seek;            // IOCTL seek
            ioctl_buff += strlen(ioctl_str);    // Point to the aesd_seek params.
            /* Parse the string and populate write_cmd & write_cmd_offset */
            sscanf(ioctl_buff, "%d,%d", &seek.write_cmd, &seek.write_cmd_offset);
            /* Perform IOCTL */
            if(ioctl(thread_data->fd, AESDCHAR_IOCSEEKTO, &seek))
            {
                perror("ioctl failed: ");
            }
            ioctl_flag = 1;
        }

#ifndef USE_AESD_CHAR_DEVICE
        /* Mutex lock */
        if(0 != pthread_mutex_lock(thread_data->mutex))
        {
            perror("Mutex Lock Failed: ");
            goto err_handling;
        }
#endif
        if(!ioctl_flag)
        {
            //Write to file
            write(thread_data->fd, storage_buffer, bytes_to_write);  
        }      

#ifndef USE_AESD_CHAR_DEVICE
        //Set the fd to start of the file.
        lseek(thread_data->fd, 0, SEEK_SET);
#endif

        while((rd_bytes = read(thread_data->fd, read_buffer, INIT_BUF_SIZE)) > 0)
        {
            if(-1 == send(thread_data->client_fd, read_buffer, rd_bytes, 0))
            {
                perror("send failed: ");
                break;
            }
        }

#ifndef USE_AESD_CHAR_DEVICE
        /* Mutex unlock */
        if(0 != pthread_mutex_unlock(thread_data->mutex))
        {
            perror("Mutex unlock failed: ");
        }
#endif
        if(-1 == rd_bytes)
        {
            perror("read failed: ");
        }
    }
    /* Clean up code */
err_handling:
    if(storage_buffer)
    {
        free(storage_buffer);
        storage_buffer = NULL;
    }
    /* Set the complete flag to 1 */
    thread_data->complete_flag = 1;
    /* Close client fd */
    close(thread_data->client_fd);

    syslog(LOG_DEBUG, "Closed connection from %s", address_string);
    return NULL;
}

/* Description: main
 * Main function for socket implementation.
 *
 * Parameters:
 * argc     : argument count.
 * argv     : argument string array.
 *
 * Return Type:
 * int     : 0 on success. -1 on error.
 */
int main(int argc, char **argv)
{
    openlog(NULL, 0, LOG_USER);

    //Daemon flag if -d is passed as an argument.
    int daemon_fg = 0;

    if(argc > 1)
    {
        if(strcmp(argv[1], "-d") == 0)
        {
            daemon_fg = 1;
        }
        else
        {
            syslog(LOG_ERR, "Invalid argument to process daemon");
        }
    }

    /* Signal init */
    if(0 != signal_init())
    {
        closelog();
        return -1;
    }

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
        closelog();
        perror("getaddrinfo failure: ");
        return -1;
    }
    if(!servinfo)
    {
        closelog();
        perror("servinfo struct was not populated: ");
        return -1;
    }

    // create socket and get file descriptor
    int sfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if(-1 == sfd)
    {
        closelog();
        perror("Failed to get socket fd: ");
        freeaddrinfo(servinfo);
        return -1;
    }

    /* Set socket to non blocking */
    int flags = fcntl(sfd, F_GETFL, 0);
    fcntl(sfd, F_SETFL, flags | O_NONBLOCK);

    // reuse the socket port
    int sock_opt = 1;
    sock_status = setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &sock_opt, sizeof(sock_opt));
    if(-1 == sock_status)
    {
        perror("setsockopt failed: ");
        closelog();
        freeaddrinfo(servinfo);
        return -1;
    }

    // bind the socket to port
    sock_status = bind(sfd, servinfo->ai_addr, sizeof(struct addrinfo));
    if(-1 == sock_status)
    {
        perror("socket bind failure: ");
        closelog();
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
        closelog();
        perror("socket listen failure: ");
        return -1;
    }

#ifndef USE_AESD_CHAR_DEVICE
    /* Mutex init */
    pthread_mutex_t mutex;
    pthread_mutex_init(&mutex, NULL);

    int timer_fd = open(SOCKET_ADDR, (O_CREAT | O_TRUNC | O_RDWR), (S_IRWXU | S_IRWXG | S_IROTH));
    if(timer_fd == -1)
    {
        closelog();
        perror("Error opening socket file:");
        return -1;
    }

    /* Timer thread create */
    timer_node_t timer_node;
    timer_node.mutex = &mutex;
    timer_node.fd = timer_fd;
    if(0 != pthread_create(&(timer_node.thread_id), NULL, timer_thread, &timer_node))
    {
        /* Still continue if failure is seen */
        perror("pthread create failed: ");
    }
    else
    {
        syslog(LOG_DEBUG, "Thread create successful. thread = %ld", timer_node.thread_id);
    }
#endif

    /* Linked List head */
    node_t *head = NULL;

    struct sockaddr_storage test_addr;          // Test addr to populate from accept()
    socklen_t addr_size = sizeof(test_addr);    // Size of test addr
    
    /* Bool to check if host fd is already created */
    bool is_fd_created = 0;
    int fd;

    while(!complete_exec)
    {
        // Connection with the client
        int client_fd = accept(sfd, (struct sockaddr *)&test_addr, &addr_size);
        if((client_fd == -1) && ((errno == EAGAIN) || (errno == EWOULDBLOCK)))
        {
            ll_clean(&head, 0);
            continue;                           // Try again
        }
        else if(client_fd == -1)
        {
            perror("accept failure: ");
            break;
        }

        /* Open a file */
	if(!is_fd_created)
	{
            fd = open(SOCKET_ADDR, (O_CREAT | O_TRUNC | O_RDWR), (S_IRWXU | S_IRWXG | S_IROTH));
            if(fd == -1)
            {
                closelog();
                perror("Error opening socket file:");
                return -1;
            }
	    is_fd_created = 1;
	}

        /* Create new node and add to the linked list */
        node_t *new_node = (node_t *)malloc(sizeof(node_t));
        if(!new_node)
        {
            syslog(LOG_ERR, "Error creating new node.");
            break;
        }
        new_node->thread_data.complete_flag = 0;
        new_node->thread_data.client_fd = client_fd;
        new_node->thread_data.client_addr = (struct sockaddr_in *)&test_addr;
        new_node->thread_data.fd = fd;
#ifndef USE_AESD_CHAR_DEVICE
        new_node->thread_data.mutex = &mutex;
#endif
        /* Create a new thread */
        int res = pthread_create(&(new_node->thread_data.thread_id), NULL, thread_socket, &(new_node->thread_data));
        if(res == 0)
        {
            syslog(LOG_DEBUG, "Thread create successful. thread = %ld", new_node->thread_data.thread_id);
        }
        else
        {
            perror("pthread create failed: ");
            free(new_node);
            break;
        }
        //Insert the node to the linked list
        new_node->next = head;
        head = new_node;

        //Check for thread completion.
        ll_clean(&head, 0);
    }

    /* Clean the linked list and join all the threads. */
    ll_clean(&head, 1);

    //cleanup.
    if(-1 == close(sfd))
    {
        perror("close sfd failed: ");
    }

    if (close(fd) == -1)
    {
        perror("close fd failed: ");
    }

    //unlink file
    if (unlink(SOCKET_ADDR) == -1)
    {
        perror("unlink failed: ");
    }

#ifndef USE_AESD_CHAR_DEVICE
    /* Thread join timer threads */
    pthread_join(timer_node.thread_id, NULL);

    if (close(timer_fd) == -1)
    {
        perror("close timer fd failed: ");
    }

    //Destroy pthread
    pthread_mutex_destroy(&mutex);
#endif

    //Close log
    closelog();

    return 0;
}

/* Description: ll_clean
 * Clears the linked list and joins the
 * threads associated with each ll node
 *
 * Parameters:
 * head	: Points to the head node of ll
 * is_clear : TRUE - Free all the nodes
 *            False - Free node only if thread complete flag is set.
 *
 * Return Type:
 * void     : No return type required.
 */
static void ll_clean(node_t **head, bool is_clear)
{
    if(!head || !(*head))
    {
        return;
    }
    node_t *current = NULL, *previous = NULL;

    current = *head;
    while(current)
    {
        if((1 == current->thread_data.complete_flag) || (1 == is_clear))
        {
            node_t *temp = current;
            // check if it's the head pointer.
            if(current == *head)
            {
                *head = current->next;
                current = *head;
            }
            else
            {
                current = current->next;
                previous->next = current;
            }
            if(temp)
            {
                syslog(LOG_DEBUG, "thread %ld exited successfully", temp->thread_data.thread_id);
                pthread_join(temp->thread_data.thread_id, NULL);
                free(temp);
            }
        }
        else
        {
            previous = current;
            current = current->next;
        }
    }
    return;
}
