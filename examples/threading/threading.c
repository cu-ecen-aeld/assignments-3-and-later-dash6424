#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <string.h>

// Optional: use these functions to add debug or error prints to your application
//#define DEBUG_LOG(msg,...)
#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)
#define IN_MICRO_SEC 1000


void* threadfunc(void* thread_param)
{
    struct thread_data *thread_params = (struct thread_data *)thread_param;

    //Sleep before mutex lock
    int result = usleep(thread_params->wait_to_obtain_ms * IN_MICRO_SEC);
    if(result == -1)
    {
      ERROR_LOG("Error in obtain usleep: %d", errno);
      goto err_handle;
    }

    //Mutex lock
    result = pthread_mutex_lock(thread_params->mutex);
    if(result != 0)
    {
      ERROR_LOG("Error in mutex lock: %d", errno);
      goto err_handle;
    }

    //Sleep before release
    result = usleep(thread_params->wait_to_release_ms * IN_MICRO_SEC);
    if(result == -1)
    {
      ERROR_LOG("Error in release usleep: %d", errno);
      goto err_handle;
    }

    //Mutex unlock
    result = pthread_mutex_unlock(thread_params->mutex);
    if(result != 0)
    {
      ERROR_LOG("Error in mutex unlock: %d", errno);
      goto err_handle;
    }
    //Success scenario
    thread_params->error_status = 0;
    thread_params->thread_complete_success = true;  
    return thread_param;

//Handle error scenario
err_handle:
    thread_params->error_status = 1;
    thread_params->thread_complete_success = false;
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
     struct thread_data *thread_params = (struct thread_data *)malloc(sizeof(struct thread_data));
     if(NULL == thread_params)
     {
        ERROR_LOG("Malloc failure for thread params: %d", errno);
        return false;
     }
     //Initialize all variables to 0.
     memset(thread_params, 0, sizeof(struct thread_data));
     //populate the variables
     thread_params->wait_to_obtain_ms = wait_to_obtain_ms;      // Assign mutex wait to obtain in ms
     thread_params->wait_to_release_ms = wait_to_release_ms;    // Assign mutex wait to release in ms
     thread_params->mutex = mutex;                              // Assign mutex info
     thread_params->thread = *thread;                           // Assign thread ID
     thread_params->error_status = 0;                           // Default error code.

     // Create a new thread with the thread params.
     // Keeping the thread attributes to default by providing NULL
     int result = pthread_create(thread, NULL, threadfunc, thread_params);
     if(result != 0)
     {
        ERROR_LOG("Error in creating pthread: %d", errno);
        // free the allocated memory for thread params
        free(thread_params);
        return false;
     }
     else if(result == 0)
     {
        DEBUG_LOG("Successfully created thread");
        return true;
     }
     return false;
}

