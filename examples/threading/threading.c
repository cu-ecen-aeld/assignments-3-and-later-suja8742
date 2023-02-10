#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>

/* Attributions: Stack overflow, Man Pages and Youtube.
   Author: Sudarshan J
*/
// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    int err = usleep(thread_func_args->wait_to_obtain_ms * 1000); //Wait to acquire mutex
    if(err == -1)
    {
        ERROR_LOG("usleep Error! Error code: %d", errno);
        return thread_param; //Something went wrong. Return early
    }
    int lck = pthread_mutex_lock(thread_func_args->mutex); //Acquire

    if(lck != 0)
    {
        ERROR_LOG("Mutex Lock error! Error code: %d", errno);
        return thread_param; //Return early, something went wrong. 
    }

    int error = usleep(thread_func_args->wait_to_release_ms * 1000); //Wait
    if(error == -1)
    {
        ERROR_LOG("usleep Error! Error code: %d", errno);
        return thread_param; //Something went wrong. Return early
    }

    int unlk = pthread_mutex_unlock(thread_func_args->mutex); //Release
    if(unlk != 0)
    {
        ERROR_LOG("Mutex Unlock error! Error code: %d", errno);
        return thread_param; //Something went wrong, return early.
    }

    thread_func_args->thread_complete_success = true; //Set to true IF it does not return earlier in the code, to fulfill the test suite requirement.
    
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{

    struct thread_data *thread_p = (struct thread_data *)malloc(sizeof(struct thread_data)); //Allocate memory for the structure

  //  if(thread_p == NULL)
   // {
    //    ERROR_LOG("Malloc Failed!");
     //   return false;
   // }

    thread_p->wait_to_obtain_ms = wait_to_obtain_ms;
    thread_p->wait_to_release_ms = wait_to_release_ms;
    thread_p->mutex = mutex;
    thread_p->thread = *thread;

    int ret_cr = pthread_create(thread, NULL, threadfunc, thread_p); //Create a thread and pass threadfunc() as the function pointer. ret_cr holds thread ID.
    
    if(ret_cr != 0)
    {
        ERROR_LOG("Creating thread failed with code: %d", errno);
        return false;
    }
    else if(ret_cr == 0)
    {
        return true;    //Success case for pthread_create
    }
    
    return false;
}

