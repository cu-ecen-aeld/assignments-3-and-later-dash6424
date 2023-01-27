/*==========================================================================
  File Name: writer.c
  This file contains the writer code for writing string to a file.

  Author: Daanish Shariff
========================================================================== */

/*==========================================================================
  Include files
========================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <syslog.h>
#include <unistd.h>
#include <errno.h>

/*==========================================================================
  Function Declaration
========================================================================== */
/* Description: err_display
 * Displays the error message upon failure. 
 *
 * Parameters:
 * void : No parameters required.
 * Return Type:
 * void : No parameters required
 */
static void err_display();

/* Description: err_display
 * Displays the error message upon failure. 
 *
 * Parameters:
 * void : No parameters required.
 * Return Type:
 * void : No parameters required
 */
static void err_display()
{
	printf("Error. Expected 2 arguments with executable as shown below \n");
	printf("./writer <filepath> <string>\n");
}

/* Description: main
 * Function to perform file write.
 *
 * Parameters:
 * argc : Number of user arguments.
 * argv : Array of argument strings.
 * Return Type:
 * int  : Returns success or failure.
 */
int main(int argc, char *argv[])
{
	//Open system log in user mode
	openlog("a2_log", LOG_PID, LOG_USER);

	/* Check for argument count */
	if(3 != argc)
	{
		syslog(LOG_ERR, "Not enough arguments\n");
		err_display();
		return 1;
	}

	int fd;				// File Descriptor.
	ssize_t wr_bytes;		// Stores the number of bytes written.
	char *filepath	= argv[1];	// Writer file path.
	char *str	= argv[2];	// String to write.

	/* File open with write, creat and truncate options.
	 * Permissions:
	 * User:	Read Write.
	 * Group:	Read, Write.
	 * Others:	Read.
	 * */
	fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP | S_IROTH);
	if(fd == -1)
	{
		syslog(LOG_ERR, "Error opening file: %d", errno);
		return 1;
	}

        /* Write string to the file */
        wr_bytes = write(fd, str, strlen(str));
	
	if((wr_bytes == -1) || (wr_bytes != strlen(str)))
	{
		syslog(LOG_ERR, "Error writing string: %s to file: %s with error: %d", str, filepath, errno);
		return 1;
	}

        // Debug print.
        syslog(LOG_DEBUG, "Writing %s to %s success", str, filepath);

	closelog();
	close(fd);

	return 0;
}
