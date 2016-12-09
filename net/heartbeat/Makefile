all: server client
	echo "make all"
server:
	gcc -g heartbeat_server.c -o heartbeat_server
client:
	gcc -g heartbeat_client.c -o heartbeat_client
clean:
	rm -rf heartbeat_server heartbeat_client

