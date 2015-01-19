#include "FuncHeader.h"


// Client's main function
int main(int argc, char *argv[]) 
{
	int sock;
	struct sockaddr_in theServer;

	char userinp[256];

	// Validate user cmd line input!
	if ( argc != 4 || (strcmp("-s", argv[1]) != 0) )
	{
		fprintf(stderr, "USAGE: client -s <server_IP> <server_port>\n");
		exit(-1);
	}
	do {
		// Create TCP socket
		if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
			Die("Failed to create socket");
		}

		// Construct server sockaddr_in structure
		memset(&theServer, 0, sizeof(theServer));       // Clear socket struct
		theServer.sin_family = AF_INET;                  // Internet 
		theServer.sin_addr.s_addr = inet_addr(argv[2]);  // the IP address
		theServer.sin_port = htons(atoi(argv[3]));       // server's port
		// Establish the TCP connection
		if (connect(sock, (struct sockaddr *) &theServer, sizeof(theServer)) < 0) {
			Die("Failed to connect with server");
		}

		fprintf(stdout, "Waiting for input.. (Type EXIT/exit to exit..)\n");
		fgets(userinp, 256, stdin);
		// Handle the case that client wants to stop requesting webpages
		if (strstr(userinp, "EXIT") != NULL || strstr(userinp, "exit") != NULL) {
			// send a cancelation message to server!
			strcpy(userinp, "CANCEL");
			if (send(sock, (char *) &userinp, sizeof(userinp), 0) < 0) {
				Die("Failed to send bytes to client");   
			}
			// if yes just break and client program will exit!
			break;
		}
		
		// do some client side checks...
		// first check for GETNEW then for GET because substring GET 
		// is part of the string GETNEW !
		if (( strstr(userinp, "GETNEW ") != NULL) || (strstr(userinp, "GET ") != NULL)) {
			if (send(sock, (char *) &userinp, sizeof(userinp), 0) < 0) {
				Die("Failed to send bytes to client");   
			}
		}
		else {
			// this means that user has given invalid input!
			fprintf(stdout, "Invalid input. Please try again..\n");
			// send a cancelation message to server!
			strcpy(userinp, "CANCEL");
			if (send(sock, (char *) &userinp, sizeof(userinp), 0) < 0) {
				Die("Failed to send bytes to client");   
			}
			continue;
		}

		// wait for the reply containing the HTML content
		char htmlcontent[BUFSIZ+1];
		memset(htmlcontent, 0, BUFSIZ + 1);
		int bytes;
		while((bytes = recv(sock, htmlcontent, BUFSIZ, 0)) > 0) {

			fprintf(stdout, "%s", htmlcontent);
			
			if (strstr(htmlcontent, "EOF\n") != NULL) {
				break;
			}

			memset(htmlcontent, 0, BUFSIZ + 1);
		}

		// Shut down the connection with the TCP server
		close(sock);

	} while(1);

	exit(0);
}
