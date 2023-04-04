/* Includes */


#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <linux/fs.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/time.h>
#include "../aesd-char-driver/aesd_ioctl.h"

/* Macros */


#define BACKLOG_VAL 20 //Setting the maximum connection request limit while listening
#define WRITE_BUFFER_SIZE 512
#define INITIAL_BUFF_SIZE 1024
/* Global variables */

//Macro switch used as suggested in the videos, set to 1 by default
#define USE_AESD_CHAR_DEVICE 1
int sock_fd = -1;
//int acc_fd = -1;
int thread_com = 0;     
//int filetotest_fd = -1; //Test file 
#ifndef USE_AESD_CHAR_DEVICE
pthread_mutex_t mutex; //Mutex init
timer_t timer; //Timer used to get timestamp.
#endif

//Prototypes to avoid the implicit declaration warning
#ifndef USE_AESD_CHAR_DEVICE
static void append_timestamp();
static int timer_10();
#endif

void sig_handler(int signum)
{
    if(signum == SIGINT || signum == SIGTERM)
    {
        thread_com = 1;
        syslog(LOG_DEBUG, "SIGINT received, exiting...");
    }

    #ifndef USE_AESD_CHAR_DEVICE
    if(signum == SIGALRM)
    {
        append_timestamp();
        syslog(LOG_DEBUG, "SIGALARM");
    }
    #endif

}

/* Seperating exit handler for signal safety */
void exit_handler()
{
    //Closing sock_fd
   int status = close(sock_fd);
    if(status < 0)
    {
        syslog(LOG_ERR, "Unable to close socket FD with an error: %d", errno);
    }

    /* status = close(filetotest_fd);
    if(status < 0)
    {
        syslog(LOG_ERR, "Unable to close socket FD with an error: %d", errno);
    } */
    
    #ifndef USE_AESD_CHAR_DEVICE
    unlink("/var/tmp/aesdsocketdata");
    if(status < 0)
    {
        syslog(LOG_ERR, "Unable to close socket FD with an error: %d", errno);
    }
    #endif
    //Destroy Mutex
    #ifndef USE_AESD_CHAR_DEVICE
    pthread_mutex_destroy(&mutex);
    timer_delete(timer);
    #endif

    closelog();
    exit(EXIT_SUCCESS);
}

/* Register and initialize signals */

int signal_init()
{
    struct sigaction act;
    act.sa_handler = &sig_handler;

    sigfillset(&act.sa_mask);

    act.sa_flags = 0;

    if(sigaction(SIGINT, &act, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    if(sigaction(SIGTERM, &act, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    #ifndef USE_AESD_CHAR_DEVICE
    if(sigaction(SIGALRM, &act, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    #endif
    return EXIT_SUCCESS;

}

static int daemon_creation()
{
    pid_t pid;

    /*Create a new child process*/
    pid = fork();

    if(pid == -1)
    {
        return -1;
    }

    else if(pid != 0)
    {
        exit(EXIT_SUCCESS);
    }

    /* Create a new session and process group*/
    if(setsid() == -1)
    {
        return -1;
    }

    /* set the working directory to the root directory*/
    if(chdir("/") == -1)
    {
        return -1;
    }

    /* Redirect fd's 0,1, 2 to /dev/null as specified */
    open("/dev/null", O_RDWR); //stdin - Standard input
    open("/dev/null", O_RDWR); //stdout
    open("/dev/null", O_RDWR); //stderror

    return 0;
}

struct thread_data_t
{
    pthread_t threadid; //Thread ID for bookkeeping
    int thread_term_flag; //Flag to check if the thread terminated. 
    int client_connected_fd; //File descriptor for connection with a client. 
    struct sockaddr_in *client_addr; //Client Address
};

struct Node
{
    struct thread_data_t ll_data;
    struct Node* next;
};

//Credit: GeeksforGeeks
//Allocate a node into a linked list at the HEAD. 
void allocate_node(struct Node** head_ref, struct Node *node)
{
    node->next = *head_ref;
    *head_ref = node;
}

/* THREADS SPAWNING */

void* threadfunc(void* thread_param)
{

    struct thread_data_t *thread_local_stg = (struct thread_data_t *)thread_param;

    //Creating receive buffer to recieve and store incoming packets
    char buffer[INITIAL_BUFF_SIZE];

    //Allocate storage buffer
    char *packet_buffer = (char *) malloc(INITIAL_BUFF_SIZE);

    int packet_buffer_len = INITIAL_BUFF_SIZE;

    //String to hold IP address of client from the inet_nto fn
    char address_str[INET_ADDRSTRLEN];

    //Variable to store returned string from receive
    int bytes_received;

    //Set this flag when \n is received to terminate
    int nullterm_flag;

    //Temporary variable inside the for loop for \n ind.
    int nullchar_it = 1; 

    //Variable to track the size of the packet buffer. Value of this multiplied by the standard size 1024 used when computing bytes to be written and memcpy operation on the packet buffer. 
    int packet_buffer_size_count = 1;

    //Variable to indicate number of bytes of data to be appended to the file in /var/tmp/
    int write_len = 0;

    //Variable to check the file size after writes are performed
    int file_size = 0;

    //Bytes from packets from recv() without \n and less than 1024 bytes
    int bytes_cnt = 0;

    //Return write value
    int ret_wr = 0;

    //The formatted string for the ioctl command as mentioned in the lecture slides. 
    const char* ioctl_format_str = "AESDCHAR_IOCSEEKTO:";

    //Attempting to print the IP address of the accepted client connection on success of accept using the inet_ntop function
   // struct sockaddr_in *ptr = (struct sockaddr_in *)&accept_addr;
   // syslog(LOG_DEBUG, "Accepted connection from IP : %s", inet_ntop(AF_INET, &ptr->sin_addr, address_str, sizeof(address_str)));

        //Receive all bytes from client. Handling the case where \n s not sent or length < 1024
    while((bytes_received = recv(thread_local_stg->client_connected_fd, buffer, sizeof(buffer), 0)) > 0) 
    {
        if(bytes_cnt + bytes_received > packet_buffer_len)
        {
            char *tmp = (char *)realloc(packet_buffer, packet_buffer_len + INITIAL_BUFF_SIZE);
                if(tmp)
                {
                    packet_buffer = tmp; //Assign the new pointer to the buffer
                    packet_buffer_len+=INITIAL_BUFF_SIZE; //Lengt increases by 1024. 
                    packet_buffer_size_count+=1; //Number of times realloced
                }

                else
                {
                    printf("Malloc failure");
                }
        }

        int i;
        //Check for \n for the end of a packet received. 
        for(i = 0; i < bytes_received; i++)
        {
            if(buffer[i] == '\n')
            {
                nullterm_flag = 1;
                nullchar_it = i+1;
                break;
            }
        }

        memcpy(packet_buffer + (packet_buffer_size_count -1)*INITIAL_BUFF_SIZE + bytes_cnt, buffer, bytes_received);

        //Check the completeness of data packet and assign bytes to write based on the value of nullchar_it
        if(nullterm_flag == 1)
        {
            write_len = (packet_buffer_size_count -1)*INITIAL_BUFF_SIZE + nullchar_it + bytes_cnt;
            bytes_cnt = 0;
            break;
        }

        else
        {
            if((i == bytes_received) && (bytes_received < INITIAL_BUFF_SIZE))
            {
                bytes_cnt += bytes_received;
            }

            else if(bytes_received == INITIAL_BUFF_SIZE)
            {
                char* tmp = (char *)realloc(packet_buffer, packet_buffer_len + INITIAL_BUFF_SIZE);
            

                if(tmp)
                {
                    packet_buffer = tmp;
                    packet_buffer_len = packet_buffer_len + INITIAL_BUFF_SIZE;
                    packet_buffer_size_count +=1;
                }

                else
                {
                    perror("Memory allocation failure");
                }
            }
        }

    }

    if(bytes_received == -1)
    {
        syslog(LOG_ERR, "recv() error, errno: %d\n", errno);
        goto cleanup_label; //Approach for thread safe cleanup
    }

    
    if(nullterm_flag == 1)
    {
        nullterm_flag = 0;

        #ifndef USE_AESD_CHAR_DEVICE

        //Mutex lock while access write operation to the file, for thread safety. 
        int ret = pthread_mutex_lock(&mutex);
        if(ret != 0)
        {
            perror("Mutex Lock");
        }
        #endif

        #ifdef USE_AESD_CHAR_DEVICE
        char *filename = "/dev/aesdchar";
        #else
        char *filename = "/var/tmp/aesdsocketdata";
        #endif

        //Write obtained packet to the file IF it is not the "AESDCHAR_IOCSEEKTO:" string. If it is, use the case for the ioctl command below.
        //If ioctl case, then do not write the string. 
        //Extract the values of write_cmd and write_cmd_offset.
        int filetotest_fd = open(filename, O_RDWR | O_CREAT | O_APPEND, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP | S_IROTH);

                                /* IOCTL SUPPORT*/


        int ioctl_str_len = 0; //Obtain length of the ioctl string

        int is_ioctl = 0;   //Flag for checks while reading 
        
        ioctl_str_len = strlen(ioctl_format_str);   //Obtain the length of "AESD_IOCHARSEEKTO:"

        if(strncmp(packet_buffer, ioctl_format_str, ioctl_str_len) == 0 )     //Check if the string received is the ioctl format str. strncmp returns 0 on success. 
        {
            struct aesd_seekto seekto;  //Init
            seekto.write_cmd = 0;
            seekto.write_cmd_offset = 0;
            char *ioctl_buffer = packet_buffer; //Create a buffer locally to modify and extract write_cmd and write_cmd_offset

            ioctl_buffer+=ioctl_str_len;    //Sets the ptr to the end of the string so we can extract only X&Y values.
        
            printf("The string is: %s and the length is %d", ioctl_buffer, ioctl_str_len);
            //Credit:
            //Extracting values from a formatted string - educative.io (sscanf) and ChatGPT
            //Using sscanf to extract X&Y values as an alternative to atoi or similar methods. 
            if(sscanf(ioctl_buffer, "%d,%d", &seekto.write_cmd, &seekto.write_cmd_offset) < 0) //Checking for EOF or other types of error
            {
                syslog(LOG_ERR, "Error with sscanf, errno: %d", errno);
            }

            if(ioctl(filetotest_fd, AESDCHAR_IOCSEEKTO, &seekto))       //ioctl command with existing opened file descriptor. Interfaces with driver. 
            {
                syslog(LOG_ERR, "Error with ioctl, errno: %d\n", errno);
            }
            
            is_ioctl = 1;
        }

        else //Write as we did previously if ioctl string not received
        {   
            is_ioctl = 0;
            ret_wr = write(filetotest_fd, packet_buffer, write_len);   
        }

        file_size += ret_wr; //Adding len to the current file size
        #ifndef USE_AESD_CHAR_DEVICE
        lseek(filetotest_fd, 0, SEEK_SET); //Set the file descriptor to the top of the file
        #endif

        char *read_buffer = NULL;
        int read_buffer_size;

    
        read_buffer = (char*)malloc(WRITE_BUFFER_SIZE);
        read_buffer_size = WRITE_BUFFER_SIZE;

        if(read_buffer == NULL)
        {
            perror("Read buffer allocation failed.");
        }

        //Reading into the read buffer
        ssize_t bytes_read;
        int bytes_sent;
        while((bytes_read = read(filetotest_fd, read_buffer, read_buffer_size)) > 0)
        {
            bytes_sent = send(thread_local_stg->client_connected_fd, read_buffer, bytes_read, 0);

            if(bytes_sent == -1)
            {
                syslog(LOG_ERR, "Error sending, errno: %d", errno);
                break;
            }
        }

        if((is_ioctl == 0 )&& (bytes_read == -1))
        {
            perror("Read");
        }

        close(filetotest_fd);
        free(read_buffer);

        #ifndef USE_AESD_CHAR_DEVICE
        ret = pthread_mutex_unlock(&mutex);

        if(ret!=0)
        {
            perror("Mutex Unlock");
        }
        #endif
    }

    cleanup_label: free(packet_buffer);
    
    thread_local_stg->thread_term_flag = 1;

      
    close(thread_local_stg->client_connected_fd);
    syslog(LOG_DEBUG, "Closed connection from %s", address_str);
    return NULL;
}

/* TIMER SECTION.*/

#ifndef USE_AESD_CHAR_DEVICE
static void append_timestamp()
{
    time_t timestamp;
    char time_buffer[40];
    char write_buffer[100];

    struct tm* tm_info;

    time(&timestamp);
    tm_info = localtime(&timestamp);

    strftime(time_buffer, 40, "%a, %d %b %Y %T %z", tm_info);
    sprintf(write_buffer, "timestamp:%s\n", time_buffer);

    lseek(filetotest_fd, 0, SEEK_END);

    pthread_mutex_lock(&mutex);
    int nr = write(filetotest_fd, write_buffer, strlen(write_buffer));
    if(nr < 0)
    {
        perror("write");
    }

    pthread_mutex_unlock(&mutex);

}

static int timer_10()
{
    int status = timer_create(CLOCK_REALTIME, NULL, &timer);
    if(status == -1)
    {
        perror("timer create");
        return EXIT_FAILURE;
    }

    
    struct itimerspec delay;

    int ret;

delay.it_value.tv_sec = 10;
delay.it_value.tv_nsec = 0;
delay.it_interval.tv_sec = 10;
delay.it_interval.tv_nsec = 0;

ret = timer_settime(timer, 0, &delay, NULL);
if(ret)
{
    perror("timer settime");
    return EXIT_FAILURE;
}


return EXIT_SUCCESS;
}
#endif

int main(int argc, char **argv)
{
    
    
    
    //Log open for syslog
    openlog(NULL, 0, LOG_USER);

    //set a daemon flag when the program is run with the -d argument
    int daemon_flag = 0;

    if(argc == 2)
    {
        char *daemon_argument = argv[1];
        if(strcmp(daemon_argument, "-d") != 0)
        {
            printf("Invalid argument. Expected argument for daemon is \"-d\\n");
        }

        else
        {
            daemon_flag = 1;
        }
    }

    //Call to init signals
    int sig_ret_status = signal_init();

    if(sig_ret_status < 0)
    {
        printf("Signal init error\n");
    }

    //Set socket parameters to stream (TCP/IP) and AF_INET for IPv4
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    //fcntl(sock_fd, F_SETFL, O_NONBLOCK);
    
    if(sock_fd < 0)
    {
        printf("Error creating the socket, file descriptor not obtained. Error code: %d\n", errno);
        return -1;
    }

    int status;
    struct addrinfo hints;
    struct addrinfo *servinfo; //Pointer to be passed to getaddrinfo()
    int yes = 1;

    //Initialize hints values
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    //Using getaddrinfo to get the addresses
    status = getaddrinfo(NULL, "9000", &hints, &servinfo);

    if(status != 0)
    {
        printf("Error in getting address information. Error number is %d\n", errno);
        return -1;
    }

    //set socket options below
    if(setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
    {
        printf("Error setting options for FD. Error code: %d\n", errno);
        return -1;
    }
    
    //Complete bind with the socket
    status = bind(sock_fd, servinfo->ai_addr, servinfo->ai_addrlen);

    if(status < 0)
    {
        printf("Error binding socket. Error code : %d\n", errno);
        return -1;
    }

   
    //servinfo is malloc'ed by the getaddrinfo function. This needs to be freed. 
    freeaddrinfo(servinfo);

     if(daemon_flag == 1)
    {
        int ret_d = daemon_creation();
        if(ret_d == -1)
        {
            syslog(LOG_ERR, "Error creating a daemon");
        }

        else if(ret_d == 0)
        {
            syslog(LOG_DEBUG, "Created Daemon");
        }
    }


    //listen for incoming connections. 
    status = listen(sock_fd, BACKLOG_VAL);
    if(status < 0)
    {
        printf("Error with listen(). Error code: %d\n", errno);
        return -1;
    }

    //permissions for this file is usr=rw, g=rw, oth = r
  //  filetotest_fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP | S_IROTH);

   // if(filetotest_fd < 0)
  //  {
   //     syslog(LOG_ERR, "Error opening the file in this location with code : %d", errno);
  //  }

    #ifndef USE_AESD_CHAR_DEVICE
    timer_10(); //Start the 10s timer to facilitate the append timestamp every 10s in the opened file. 
    #endif

    #ifndef USE_AESD_CHAR_DEVICE
    pthread_mutex_init(&mutex, NULL);
    #endif

    struct sockaddr_storage accept_addr;

    socklen_t accept_addr_len = sizeof(accept_addr);

    struct Node *head = NULL;
    struct Node *current, *previous;

    while(!thread_com)
    {
        int acc_fd = accept(sock_fd, (struct sockaddr *)&accept_addr, &accept_addr_len); //Accept incoming connections in a forever loop as specificed by instructions.

        if(acc_fd < 0)
        {   
            if(errno == EINTR)
            {

                continue; //Continue trying to accept if failed
            }

            else
            {
                perror("accept");
                continue;
            }
        }

        //Create new node on the LL allocating the structure parameters
        struct Node *new_node = (struct Node *)malloc(sizeof(struct Node));
        new_node->ll_data.thread_term_flag = 0;
        new_node->ll_data.client_connected_fd = acc_fd;
        new_node->ll_data.client_addr = (struct sockaddr_in *)&accept_addr;

        //Create a thread for each unit
        int ret = pthread_create(&(new_node->ll_data.threadid), NULL, threadfunc, &(new_node->ll_data));
        if(ret != 0)
        {
            printf("Error in pthread_create with err number: %d", errno);

        }

        else if(ret == 0)
        {
            printf("Thread created successfully. Thread ID: %lu\n", new_node->ll_data.threadid);
        }

        allocate_node(&head, new_node);


        //Traverse all nodes to remove a node from the list
        current = head;
        previous = head;

        while(current != NULL)
        {
            if((current->ll_data.thread_term_flag == 1) && (current == head))
            {
                printf("Exited from the Thread with id %lu\n", current->ll_data.threadid);
                head = current->next;
                current->next = NULL;
                pthread_join(current->ll_data.threadid, NULL);
                free(current);
                current = head;
            }

            else if((current->ll_data.thread_term_flag == 1) && (current != head))
            {
                //Deleting other nodes
                printf("Exited successfully from Thread %lu\n", (current->ll_data.threadid));
                previous->next = current->next;
                current->next = NULL;
                pthread_join(current->ll_data.threadid, NULL);
                free(current);
                current = previous->next;
            }

            else
            {
                //Travers with the previous behind the current
                previous = current;
                current = current->next;
            }
        }

        
    }

    //Valgrind extra thread err: Freeing head node if it exists and corresponding thread has been completed

    if(head)
    {
        if(head->ll_data.thread_term_flag == 1)
        {
            pthread_join(head->ll_data.threadid, NULL);
            free(head);
            head = NULL;
        }
    }

    //Cleanup step with signal safety in mind. 
    exit_handler();

    return 0;

        


}
    


