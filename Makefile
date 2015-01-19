all:
	gcc -g -Wall client.c -o client -lpthread -lrt
	gcc -g -Wall server.c -o server -lpthread -lrt
clean:
	rm client
	rm server
