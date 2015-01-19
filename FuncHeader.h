#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#include <semaphore.h> // define sem_t type, used in performing semaphore operations
#include <sys/ipc.h>   // define ipc_perm structure, perform an IPC operation
#include <sys/types.h> // define key_t type and other basic system data types
#include <sys/shm.h>   // shared memory facility 
#include <sys/wait.h>
#include <sys/time.h>  // define timeval and itimerval structures
#include <sys/socket.h>// basic socket definitions
#include <sys/un.h>    // for Unix domain sockets

#include <fcntl.h>
#include <stdio.h>     // standard input/output library
#include <unistd.h>    // provide access to the POSIX
#include <stdlib.h>    // standard general utilities library
#include <errno.h>     // for the EINTR constant
#include <signal.h>     // define handling on various signals
#include <string.h>    // several functions to manipulate strings and arrays

#define MAXPENDING 100 // Max connections that the server can accept
#define PORT 80
#define USERAGENT "HTMLGET 1.0"
#define MAXWEBSITES 100
/*
 * Just some defines for displaying
 * messages in different colors
 */
#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"

struct webpage {
	char fname[32];
	char url[256];
};

struct webpage * shm, * shm_bkup;
int * shm_pageid, * shm_pageid_bkup;

// struct webpage webpages[MAXWEBSITES];

int serversock, clientsock;
int wpage_id = -1;

// shared mems keys
key_t key1, key2;
int shmid, shmid_pageid;

/*
 * Handles any errors by printing
 * them and then simply exiting the program
 */
void Die(char * mess) 
{ 
	perror(mess); 
	exit(-1); 
}


/*
 * When server is shut down with CTRL+C
 * we have to release any reserved resources
 */
void sig_int( int signo ) 
{
	int i;
	shm_bkup = shm;
	for (i = 0; i < MAXWEBSITES; i++) {
		shm_bkup++;
	}
	// Free shared mem segments
	shmctl(shmid, IPC_RMID, NULL);
	shmctl(shmid_pageid, IPC_RMID, NULL);
	// close any open sockets
	close(serversock);
	// shut down the server process
	exit(0); 
}

/*
 * The use of this functions avoids having
 * "zombie" processes in the system!
 */
void sig_chld( int signo )
{
	pid_t pid;
	int stat;  

	while ( ( pid = waitpid( -1, &stat, WNOHANG ) ) > 0 ) { }
}

/*
 * Calculates the number of whitespaces that 
 * have to be concatenated with a message, so that it can 
 * be displayed in a nice way
 */
char * addwhitespaces(short int cur_len)
{
	int msglen = 48;
	char buf[msglen - cur_len];
	strcpy(buf, "");

	short int i;
	for (i = 0; i < msglen - cur_len; i++)
		strcat(buf, " ");

	return buf;
}

/*
 * Displays all the available IPs that
 * server listens on. However, it is going
 * to display only the ones that are mapped
 * to the OSes hostname at the /etc/hosts file.
 */
int display_info(char port [])
{
	struct addrinfo AvailAddrs;
	struct addrinfo *result, *rp;
	struct in_addr addr;
	int s;
	char buf[128], ac[128];

	if ( gethostname(ac, sizeof(ac)) == -1 ) {
		Die("Sorry something went wrong when getting local hostname.\n");
	}

	fprintf(stdout, " ------------------------------------------------\n");       
	fprintf(stdout, "|\t     Proxy Server is now running..   \t|\n");
	fprintf(stdout, " ------------------------------------------------\n");

	memset(&AvailAddrs, 0, sizeof(struct addrinfo));
	AvailAddrs.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
	AvailAddrs.ai_socktype = SOCK_DGRAM; /* Datagram socket */
	AvailAddrs.ai_flags = AI_PASSIVE;    /* For wildcard IP address */
	AvailAddrs.ai_protocol = 0;          /* Any protocol */
	AvailAddrs.ai_canonname = NULL;
	AvailAddrs.ai_addr = NULL;
	AvailAddrs.ai_next = NULL;

	/* 
	 * getaddrinfo() returns a list of address structures.
	 */
	s = getaddrinfo(ac, NULL, &AvailAddrs, &result);
	if (s != 0) {
		Die("Getaddrinfo error !\n");
	}

	fprintf(stdout, "| Server is listening on:\t\t\t|\n");
	/*
	 * Traverse the list returned by getaddrinfo
	 * and print all the available LISTEN addresses
	 */ 
	for (rp = result; rp != NULL; rp = rp->ai_next) 
	{      
		addr.s_addr = ((struct sockaddr_in *)(rp->ai_addr))->sin_addr.s_addr;

		strcpy(buf, inet_ntoa(addr));
		strcat(buf, ":");
		strcat(buf, port);

		strcat(buf, addwhitespaces(strlen(buf) + 4 + 4));
		fprintf(stdout, "|%s\t%s%s|\n", KCYN, buf, KNRM);

	}

	fprintf(stdout, " ------------------------------------------------\n");

	freeaddrinfo(result);  // No longer needed
	return 1;
}

