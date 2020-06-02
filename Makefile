default: client.c server.c
	gcc client.c util.c -o client
	gcc server.c util.c -o server

clean:
	rm -f client server *~
