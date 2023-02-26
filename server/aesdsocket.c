/* Includes */

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
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


/* Macros */

#define BACKLOG_VAL 20 //Setting the maximum connection request limit while listening

/* Global variables */
int sock_fd = -1;
int acc_fd = -1;
int filetotest_fd = -1;
char *packet_buffer;

void sig_handler(int signum)
{
    if(signum == SIGINT)
    {
        syslog(LOG_DEBUG, "SIGINT received, exiting...");
    }

    else if(signum == SIGTERM)
    {
        syslog(LOG_DEBUG, "SIGTERM received, exiting...");
    }

    free(packet_buffer);

    int status = close(sock_fd);
    if(status < 0)
    {
        syslog(LOG_ERR, "Unable to close socket FD with an error: %d", errno);
    }

    status = close(acc_fd);
    if(status < 0)
    {
        syslog(LOG_ERR, "Unable to close socket FD with an error: %d", errno);
    }

    status = close(filetotest_fd);
    if(status < 0)
    {
        syslog(LOG_ERR, "Unable to close socket FD with an error: %d", errno);
    }

    unlink("/var/tmp/aesdsocketdata");
    if(status < 0)
    {
        syslog(LOG_ERR, "Unable to close socket FD with an error: %d", errno);
    }

    closelog();
    exit(EXIT_SUCCESS);

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

    signal(SIGINT, sig_handler); //Register the signal handler for SIGINT
    signal(SIGTERM, sig_handler); //Register signal handler for SIGTERM

    //Set socket parameters to stream (TCP/IP) and AF_INET for IPv4
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    
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
        int retval = daemon_creation();
        if(retval == -1)
        {
            syslog(LOG_ERR, "Error creating a daemon");
        }

        else if(retval == 0)
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
    filetotest_fd = open("/var/tmp/aesdsocketdata", O_RDWR | O_CREAT | O_TRUNC, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP | S_IROTH);

    if(filetotest_fd < 0)
    {
        syslog(LOG_ERR, "Error opening the file in this location with code : %d", errno);
    }

    //Creating receive buffer to recieve and store incoming packets
    char buffer[1024];

    //Allocate storage buffer
    packet_buffer = (char *) malloc(1024);

    int packet_buffer_len = 1024;

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

    struct sockaddr_storage accept_addr;

    socklen_t accept_addr_len = sizeof(accept_addr);
    

    while(1)
    {
        acc_fd = accept(sock_fd, (struct sockaddr *)&accept_addr, &accept_addr_len); //Accept incoming connections in a forever loop as specificed by instructions. 

        if(acc_fd < 0)
        {
            perror("accept");
            continue; //Continue trying to accept if failed
        }

        //Attempting to print the IP address of the accepted client connection on success of accept using the inet_ntop function
        struct sockaddr_in *ptr = (struct sockaddr_in *)&accept_addr;
        syslog(LOG_DEBUG, "Accepted connection from IP : %s", inet_ntop(AF_INET, &ptr->sin_addr, address_str, sizeof(address_str)));

        //Receive all bytes from client
        while((bytes_received = recv(acc_fd, buffer, sizeof(buffer), 0)) > 0)
        {
            printf("Verifying the number of bytes received: %d\n", bytes_received);

            //Check for \n for the end of a packet received. 
            for(int i = 0; i < bytes_received; i++)
            {
                if(buffer[i] == '\n')
                {
                    nullterm_flag = 1;
                    nullchar_it = i+1;
                    break;
                }
            }

            memcpy(packet_buffer + (packet_buffer_size_count -1)*1024, buffer, bytes_received);

            //Check the completeness of data packet and assign bytes to write based on the value of nullchar_it
            if(nullterm_flag == 1)
            {
                write_len = (packet_buffer_size_count -1)*1024 + nullchar_it;
                break;
            }

            else
            {
                //Increase the size of the packet buffer by 1024 if no Null terminator is found in the buffer to accomodate full packet. 
                char *tmp = (char *)realloc(packet_buffer, (packet_buffer_len + 1024));

                if(tmp != NULL)
                {
                    packet_buffer = tmp;
                    packet_buffer_len = packet_buffer_len + 1024;
                    packet_buffer_size_count +=1;
                }

                else
                {
                    perror("Memory allocation failure");
                }
            }

        }

        printf("Packet buffer size obtained : %d\n", write_len);

        if(nullterm_flag == 1)
        {
            nullterm_flag = 0;

            //Write obtained packet to the file
            int ret_wr = write(filetotest_fd, packet_buffer, write_len);
            file_size += ret_wr; //Adding to the current file

            lseek(filetotest_fd, 0, SEEK_SET); //Set the file descriptor to the top of the file

            char *read_buffer = (char*)malloc(file_size);

            if(read_buffer == NULL)
            {
                perror("Read buffer allocation failed.");
            }

            //Reading into the read buffer
            ssize_t bytes_read = read(filetotest_fd, read_buffer, file_size);
            if(bytes_read == -1)
            {
                perror("Read");
            }

            //Return value from the send functon
            int bytes_sent = send(acc_fd, read_buffer, file_size, 0);

            if(bytes_sent < 0)
            {
                printf("Error sending on the socket connection to client.");
            }

            free(read_buffer);

            //Take everything back to original state
            packet_buffer = realloc(packet_buffer, 1024);
            packet_buffer_size_count = 1;
            packet_buffer_len = 1024;
        }

        close(acc_fd);
        syslog(LOG_DEBUG, "Closed connection from %s", address_str);
    }

    return 0;

        


}
    


