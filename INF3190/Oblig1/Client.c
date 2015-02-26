#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>
#include <sys/un.h>


int main(int argc, char* argv[])
{

	if(argc != 4){
		printf("\"Usage: <Daemon-name> <reciever-adress> <message> \n");
		return -1;
	}

	//Lager socket
	int sock=socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if(sock == -1){
		perror("Error while creating socket!");
		return -2;
	}
	
	//Allokerer name
	char* daemonName = malloc(strlen(argv[1])+1);
	strcpy(daemonName, argv[1]);
	
	//Sørger for at sockene kan gjenbrukes
	int activate=1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &activate, sizeof(int));

	struct sockaddr_un bindaddr;
	bindaddr.sun_family = AF_UNIX;
	strncpy(bindaddr.sun_path, daemonName, sizeof(bindaddr.sun_path));

	//Connecter socketen
	if(connect(sock, (struct sockaddr*)&bindaddr, sizeof(bindaddr)) == -1){
		perror("Error during connection to socket");
		free(daemonName);
		close(sock);
		return -3;
	}
	
	//Lager komplett message til Daemon
	char add[] = "__";
	
	char* mipRecv = malloc(strlen(argv[2]) + strlen(add)+1);
	strcpy(mipRecv, add);

	char* mipAdr = malloc(strlen(argv[2])+1);
	strcpy(mipAdr, argv[2]);
	
	strncat(mipRecv, mipAdr, strlen(mipAdr));
	
	char* message = malloc(strlen(argv[3])+1);
	strcpy(message, argv[3]);

	char* complMsg = malloc(strlen(mipRecv) + strlen(message) +1);
	strcpy(complMsg, message);
	strncat(complMsg, mipRecv, strlen(mipRecv)+1);
	
	//Skriver til Daemon, via IPC socket
	size_t s = write(sock, complMsg, strlen(complMsg));
	
	//Error under sending!
	if(s==-1){
		perror("Error during sending");
		close(sock);
		free(daemonName);
		return -7;
	//Socket closed hos daemon
	} else if(s==0){
		printf("Socket closed!\n");
		close(sock);
		free(daemonName);
		return 0;
	}

	//Frigjør minne
	free(complMsg);
	free(mipRecv);
	free(mipAdr);
	free(message);
	
	//Klargjør timeout-struct & select-variabler
	struct timeval xit;
	fd_set acc;

	FD_ZERO(&acc);
	FD_SET(sock, &acc);

	xit.tv_sec=0;
	xit.tv_usec=1000000;

	//Select
	int err=select(sock+1, &acc, NULL, NULL, &xit);

	//Error!
	if(err==-1){
		perror("select");
		close(sock);
		free(daemonName);
		return -4;
		//Timeout!
	}else if(err==0){
		printf("Timeout!\n");
		//Connected
	}else{
		ssize_t recieved=0;

		char buf[5];

		//Leser info fra daemon
		recieved = read(sock, buf, 5);
		
		//Error under lesing fra socket
		if(recieved < 0){
			perror("Error during read from socket");
			close(sock);
			free(daemonName);
			return -8;
			//Daemon lukket socketen
		} else if(recieved == 0){
			printf("Daemon closed!\n");
			free(daemonName);
			close(sock);
			return 0;
			//Printer info
		}else{
			buf[recieved]=0;
			printf("Message recieved: %s\n", buf);
			printf("Time used: %f ms\n", ((1000000-xit.tv_usec)/1000.0));
		}
	}
	//Lukker & frigir minne.
	free(daemonName);
	close(sock);
	return 0;
}