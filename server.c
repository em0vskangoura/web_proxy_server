#include "FuncHeader.h"

// Creates and returns an INET socket
int create_tcp_socket()
{
	int sock;
	if((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		perror("Can't create TCP socket");
		exit(1);
	}
	return sock;
}


// Returns the position of that cache webpage in array if found
// otherwise returns -1
int check_if_cached(char * host) {
	
	int i;
	shm_bkup = shm;
	for (i = 0; i < MAXWEBSITES; i++) {
		// if exists in table
		if (strcmp(host, (*shm_bkup).url) == 0) {
			if (access((*shm_bkup).fname, F_OK) != -1 ) {
				return i;
			}
		}
		shm_bkup++;
	}

	// increment global counter if this is a newly requested webpage
	(*shm_pageid)++;
	return -1;	
}
 
// Returns the IP of the URL that the client requested
char * get_ip(char * host)
{
	struct hostent * hent;
	int iplen = 15; //XXX.XXX.XXX.XXX
	char * ip = (char *) malloc(iplen + 1);
	memset(ip, 0, iplen + 1);
	if((hent = gethostbyname(host)) == NULL) {
		herror("Can't get IP");
		exit(1);
	}

	if(inet_ntop(AF_INET, (void *) hent->h_addr_list[0], ip, iplen + 1) == NULL) {
		perror("Can't resolve host");
		exit(1);
	}
	return ip;
}

// Builds the HTTP query to be sent to the webserver of that host
char * build_get_query(char * host, char * page)
{	
	char * query;
	char * getpage = page;
	char * tpl = "GET /%s HTTP/1.0\r\nHost: %s\r\nUser-Agent: %s\r\n\r\n";
	if(getpage[0] == '/') {
		getpage = getpage + 1;
	}
	// -5 is to consider the %s %s %s in tpl and the ending \0
	query = (char *) malloc(strlen(host) + strlen(getpage) + strlen(USERAGENT) + strlen(tpl) - 5);
	sprintf(query, tpl, getpage, host, USERAGENT);
	return query;
}



// Server's main function
int main(int argc, char * argv[]) 
{
	int pid;
	struct sockaddr_in serverinfo, inc_client;

	key1 = 1234;
	key2 = 5678;

	// Memory segment containing the structs of cached webpages
	if ( ( shmid = shmget(key1, MAXWEBSITES * sizeof(struct webpage), IPC_CREAT | 0666 ) ) < 0 ) {
		Die("shmget error");
	}
	if ( (int) ( shm = shmat(shmid, NULL, 0) ) == -1 ) {
		Die("shmat error");
	}

	// Memory segment for the cached webpages counter!
	if ( ( shmid_pageid = shmget(key2, sizeof(int), IPC_CREAT | 0666 ) ) < 0 ) {
		Die("shmget error");
	}
	if ( (int) ( shm_pageid = shmat(shmid_pageid, NULL, 0) ) == -1 ) {
		Die("shmat error");
	}

	// initialize counter to -1 so that first webpage will inc it to 0 !!!
	(*shm_pageid) = -1;
	
	// Checking on the command line args provided by the client
	if (argc != 3) {
		fprintf(stderr, "Wrong command line arguments!\n");
		fprintf(stderr, "USAGE: server -p <server_port>\n");
		exit(-1);
	}
	if (( strcmp("-p", argv[1]) != 0 ) || ( atoi(argv[2]) < 1024 || atoi(argv[2]) > 65355))
	{
		if ( atoi(argv[2]) < 1024 ) {
			fprintf(stderr, "Ports below 1024 should not be used in this app.\nPlease choose another port.\n");
		}
		fprintf(stderr, "USAGE: server -p <server_port>\n");
		exit(-1);
	}
	else if (atoi(argv[2]) > 9729 || atoi(argv[2]) < 9720) {
		fprintf(stderr, "This server should be using ports between 9720 and 9729!\n");
		exit(-1);
	}
	// Create the TCP socket
	if ((serversock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		Die("Failed to create socket");
	}

	// Construct the server sockaddr_in structure
	memset(&serverinfo, 0, sizeof(serverinfo)); // Clear struct
	serverinfo.sin_family = AF_INET; // Internet/IP

	// install the signal handlers !!
	signal( SIGINT, sig_int );  // signal handler for the SIGINT signal 
	signal( SIGCHLD, sig_chld ); // for waiting for children to end

	// do byte ordering on server's IP and LISTEN port
	serverinfo.sin_addr.s_addr = htonl(INADDR_ANY); // Incoming addr
	serverinfo.sin_port = htons(atoi(argv[2])); // server port

	// Display available IPs that server is listening on..
	// most probably this will be the loopback ip
	if ( display_info(argv[2]) == -1 ) {
		fprintf(stdout, "Error when trying to display IPs that server listens on.. will skip...");
	}
	fprintf(stdout, "\n");

	// Bind the server socket
	if (bind(serversock, (struct sockaddr *) &serverinfo, sizeof(serverinfo)) < 0) {
		Die("Failed to bind the server socket");
	}

	// Listen on the server socket
	if (listen(serversock, MAXPENDING) < 0) {
		Die("Failed to listen on server socket");
	}


	// Run until cancelled
	while ( 1 ) 
	{
		unsigned int clientlen = sizeof(inc_client);
		// Wait for client connection
		if ((clientsock = accept(serversock, (struct sockaddr *) &inc_client, &clientlen)) < 0) {
			Die("Failed to accept client connection");
		}

		if ((*shm_pageid) >= MAXWEBSITES) {
			fprintf(stderr, "No more space for cached webpages..\n");
			exit(1);
		}

		pid = fork();
		if (pid == 0)
		{
			// declare some auxiliary variables
			// that will be used later on for sending
			// and receiving messages and also editing files!		
			struct sockaddr_in * remote;
			int sock, tmpres, mode;
			char * ip, * get;
			char page[BUFSIZ + 1], buf[256];
			int bytes = 0;
			FILE * fp;

			if ((bytes = recv(clientsock, (char *) &buf, sizeof(buf), 0)) < 1) {
				Die("Failed to receive bytes from server");
			}
			
			// kill this child process if user's input was invalid!
			if (strstr(buf, "CANCEL") != NULL) {
				close(clientsock);
				exit(0);
			}

			// first check for GETNEW then for GET because substring GET 
			// is part of the string GETNEW !
			// if mode == 0 -> NEW
			// else if mode == 1 -> GETNEW
			if (strstr(buf, "GETNEW") != NULL) {
				mode = 1;
			}
			else if (strstr(buf, "GET") != NULL) {
				mode = 0;
			}
			// remove GET tag and keep URL
			char * tmp, * pagedir;			
			tmp = strstr(buf, " ");
			tmp++;

			if( (pagedir = strstr(buf, "/")) == NULL) { 
				pagedir = "/";
			}
			
			// strip newline char from pagedir string!
			char * pch = strstr(pagedir, "\n");
			if(pch != NULL) {
  				strncpy(pch, "\0", 1);
			}

			// remove subpage string from host string
			char host[256];
			strncpy(host, tmp, strlen(tmp) - strlen(pagedir));

			/*
			There are 3 cases we need to handle:
			1) If file is not cached -> fetch and cache it
			2) If file is cached and user makes a GET -> read from cache
			3) If file is cached and user makes a GETNEW -> fetch and replace cache
			*/
			int exists = check_if_cached(host);
			
			// if page requested by client is not cached
			if (exists == -1 || mode == 1) {

				// file is cached but user requested fresh copy
				if (exists != -1 && mode == 1) {
					fprintf(stdout, "%sSENDING FRESH WEB CONTENT (%s)%s\n", KGRN, host, KNRM);

					// simply delete that cache file
					// so that it gets updated later on !
					shm_bkup = shm;
					shm_bkup += exists;
					strcpy((*shm_bkup).fname, "DELETED");
					strcpy((*shm_bkup).url, "DELETED");
					// increment so that new webpage cache is 
					// stored on next struct
					(*shm_pageid)++;				
				}
				else {
					fprintf(stdout, "%sSENDING WEB CONTENT & CACHING IT (%s)%s\n", KBLU, host, KNRM);
				}
				
			  	sock = create_tcp_socket();
				ip = get_ip(host);

				remote = (struct sockaddr_in *) malloc(sizeof(struct sockaddr_in *));
				remote->sin_family = AF_INET;
				tmpres = inet_pton(AF_INET, ip, (void *)(&(remote->sin_addr.s_addr)));
				if( tmpres < 0)  {
					perror("Can't set remote->sin_addr.s_addr");
					exit(1);
				}
				else if(tmpres == 0) {
					fprintf(stderr, "%s is not a valid IP address\n", ip);
					exit(1);
				}
				
				remote->sin_port = htons(PORT);
				if(connect(sock, (struct sockaddr *) remote, sizeof(struct sockaddr)) < 0) {
					perror("Could not connect");
					exit(1);
				}
				// get string holds the whole HTTP query!
				get = build_get_query(host, pagedir);
				
				//fprintf(stdout, "Query is:\n<<START>>\n%s<<END>>\n", get);

				//Send the query to the server
				int sent = 0;
				while(sent < strlen(get)) {
					tmpres = send(sock, get+sent, strlen(get)-sent, 0);
					if(tmpres == -1) {
				  		perror("Can't send query");
				  		exit(1);
					}
					sent += tmpres;
				}

				int htmlstart = 0;
				char * htmlcontent;
				char htmlcontent_tosend[BUFSIZ + 1];

				// check_if_cached(host);
				shm_bkup = shm;
				shm_bkup += (*shm_pageid);
				strcpy((*shm_bkup).url, host);

				char tempfname[12] = "cached_page_", * thisfname, intbuf[12];
				snprintf(intbuf, 12, "%d", (*shm_pageid));
				if ((thisfname = strcat(tempfname, intbuf)) == NULL) {
					fprintf(stderr, "Something went wrong while trying to create a cache file!\n");
					exit(1);
				}

				strcpy((*shm_bkup).fname, thisfname);
				fp = fopen(thisfname, "w");
				//now it is time to receive the page
				memset(page, 0, sizeof(page));
				while((tmpres = recv(sock, page, BUFSIZ, 0)) > 0) {
					if(htmlstart == 0) {
						// Under certain conditions this will not work.
						// If the \r\n\r\n part is splitted into two messages
						// it will fail to detect the beginning of HTML content
						htmlcontent = strstr(page, "\r\n\r\n");
						if(htmlcontent != NULL) {
							htmlstart = 1;
							htmlcontent += 4;
						}
					}
					else {
						htmlcontent = page;
					}

					if(htmlstart) {
						strcpy(htmlcontent_tosend, htmlcontent);
						fprintf(fp, "%s", htmlcontent_tosend);
						if (send(clientsock, (char *) &htmlcontent_tosend, strlen(htmlcontent_tosend), 0) < 0) {
							Die("Failed to send bytes to client");
						}
					}
					memset(page, 0, tmpres);
				}
				if(tmpres < 0) {
					perror("Error receiving data");
				}
				// inform client that all HTML content has been sent,
				// by sending an EOF message!
				const char * eof = "\nEOF\n";
				if (send(clientsock, eof, strlen(eof) + 1, 0) < 0) {
					Die("Failed to send bytes to client");
				}

			}
			else if (exists != -1 && mode == 0) {
				char htmlcontent_tosend[BUFSIZ + 1];
				// FIND CACHED WEBPAGE and SEND IT !!!

				//now it is time to receive the page
				memset(page, 0, sizeof(page));
				// move pointer to cached webpage, retrieve its filename
				// and open that file in READ mode
				shm_bkup = shm;
				shm_bkup += exists;
				fp = fopen((*shm_bkup).fname, "r");

				while (fgets(htmlcontent_tosend, BUFSIZ, fp)) {
					if (send(clientsock, (char *) &htmlcontent_tosend, strlen(htmlcontent_tosend), 0) < 0) {
						Die("Failed to send bytes to client");
					}	
				}
				// inform client that all HTML content has been sent,
				// by sending an EOF message!
				const char * eof = "\nEOF\n";
				if (send(clientsock, eof, strlen(eof) + 1, 0) < 0) {
					Die("Failed to send bytes to client");
				}			

				fprintf(stdout, "%sSENT WEB CACHED CONTENT (%s)%s\n", KCYN, host, KNRM);
			}

			// free everything!
			fclose(fp);
			free(get);
			free(remote);
			free(ip);
			close(sock);
			// Close the TCP socket
			close(clientsock);

			// exit the forked child
			exit(0);
		}
	}
}      
