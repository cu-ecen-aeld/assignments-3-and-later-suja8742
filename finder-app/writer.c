/*
 * Author: Sudarshan J
 * Filename: writer.c
 * File Description: Replacement for the writer.sh script to create a file and a string to it.
 * Reference: Linux SP by Robert Love Chapter 2, Man pages.
*/

//Includes

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <syslog.h>

//Prototypes
static void display_msg(void);


int main(int argc, char **argv)
{
	display_msg();

	//Check corner case for number of arguments as in writer.sh
	
	if(argc != 3)
	{
	printf("Error! Number of arguments need to be executable + 2\n");	       printf("Correct format: ./writer <filepath> <text to write>\n");
	return EXIT_FAILURE;  
	}

	//File Descriptor var
	int file_descriptor;

	//Variable to store bytes that are written
	ssize_t bytes_writ;

	//Filepath and string to be written
	char *filepath = argv[1];
	char *string = argv[2];

	//Opening to use syslog
	openlog(NULL, 0, LOG_USER);

	//Opening the specified filepath as given in the man pages.
	//Permissions used are user: rw, group: rw, other:r
	//using open syscall over fopen
	file_descriptor = open(filepath, O_CREAT | O_TRUNC | O_WRONLY, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP | S_IROTH);

	//Error check for open
	if(file_descriptor == -1)
	{
		syslog(LOG_ERR, "Error while opening file: %d", errno);
	}

	bytes_writ = write(file_descriptor, string, strlen(string));
	
	//Debug statement
	syslog(LOG_DEBUG, "Writing %s to %s", string, filepath);

	//Error check and debug for write
	
if(bytes_writ == -1)	{
syslog(LOG_ERR, "Error writing string \"%s\" to file with error%d", string, errno);
	}

else if(bytes_writ != strlen(string)){
	syslog(LOG_ERR, "Error writing complete string %s to file", string);
	}

closelog();
close(file_descriptor);

return 0;

}

static void display_msg()
{
	printf("Writer App to write a <string> onto a <file>.\n");
	printf("Format is ./writer (exe) <filepath> <text to be written>\n");	
}
