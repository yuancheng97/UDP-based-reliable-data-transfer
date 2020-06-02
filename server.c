#include <sys/types.h> 
#include <arpa/inet.h> 
#include <sys/socket.h> 
#include <netinet/in.h>

#include "util.h"


struct sockaddr_in my_addr; /* my address */
struct sockaddr_in their_addr; /* connector addr */
socklen_t len = sizeof(their_addr);
int sin_size;
int sockfd;

void finish(int seq, int ack); /* 4-way closing of socket */


int main(int argc, char** argv){

	/*------   validate arguments  --------*/
	if (argc != 2){
		perror("Missing arguments");
		exit(1);
	}

	int portno = atoi(argv[1]);
	if (portno == 0){
		perror("Invalid port number.");
		exit(1);
	}

	/*------   establish UDP  --------*/

	/* create a socket */
	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) { 
		perror("ERROR: opening socket");
		exit(1);
	}

	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(portno); /* short, network byte order */
	my_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	/* INADDR_ANY allows clients to connect to any one of the host’s IP address. Optionally, use this line if you
	know the IP to use:
	my_addr.sin_addr.s_addr = inet_addr(“127.0.0.1”);
	*/
	memset(my_addr.sin_zero, '\0', sizeof(my_addr.sin_zero));
	/* bind the socket */
	if (bind(sockfd, (struct sockaddr *) &my_addr, sizeof(struct sockaddr)) == -1) {
		perror("ERROR: binding.");
		exit(1); 
	}


	/*------   enter loop to hear from client  --------*/
	srand(time(0));	
	printf("Waiting for request from client.\n");

	int fileNum = 0;
	while(1){

	/*------   3-way handshaking  --------*/
		header sp;
		uint16_t seq, ack;
		int n = recvfrom(sockfd, &sp, HEADSIZE, 0, (struct sockaddr *) &their_addr, &len);
		if (n < 0){
			errorHandler("Could not read from client.");
		}
		else if( sp.SYN_FLAG != 1 ){
			continue;
		}
		else{
			// set up output file
			fileNum += 1;
			char fileName[64];
			sprintf(fileName, "%i.file",fileNum);
			FILE* log = fopen(fileName, "w");


			printLine(RECV, &sp, 0);
			seq = rand() % SEQNUM;
			ack = sp.seq+1;
			header sa;
			createHeader(&sa, seq, ack, 1, 1, 0);
			n = sendto(sockfd, &sa, HEADSIZE, 0, (struct sockaddr*) &their_addr, len);
			if (n <= 0){
				errorHandler("Could not send to client.");
			}
			else{
				printLine(SEND, &sa, 0);
			}

			seq += 1;
			while(1){
				packet temp;
				int n = recvfrom(sockfd, &temp, PACKETSIZE, 0, (struct sockaddr *) &their_addr, &len);
				if (n <= 0){
					errorHandler("Could not receive from client.");
				}
				else if (temp.head.FIN_FLAG == 1){
					printPacket(RECV, &temp, 0);
					fclose(log);
					break;
				}
				else if (temp.head.SYN_FLAG == 1){
					printPacket(RECV, &temp, 0);
					n = sendto(sockfd, &sa, HEADSIZE, 0, (struct sockaddr*) &their_addr, len);
					if ( n <= 0){
						errorHandler("Could not send to client.");
					}
					else{
						printLine(RESEND, &sa, 0);
					}
				}
				else if(temp.head.seq != ack){
					printPacket(RECV, &temp, 0);
					header dupAck;
					createHeader(&dupAck, seq, ack, 1, 0, 0);

					n = sendto(sockfd, &dupAck, HEADSIZE, 0, (struct sockaddr* ) &their_addr, len);
					if (n < 0){
						errorHandler("Failed to send to client");
					}
					else{
						printLine(SEND, &dupAck, 1);
					}
				}
				else{
					printPacket(RECV, &temp, 0);
					ack += temp.head.len;
					ack = (ack > SEQNUM) ? (ack % (SEQNUM+1)) : ack;
					fwrite(temp.payload, 1, temp.head.len, log);

					header tempAck;
					createHeader(&tempAck, seq, ack, 1, 0, 0);
					n = sendto(sockfd, &tempAck, HEADSIZE, 0, (struct sockaddr* ) &their_addr, len);
					if (n < 0){
						errorHandler("Failed to send to the client.");
					}
					else{
						printLine(SEND, &tempAck, 0);
					}
				}
			}

		}

		finish(seq, ack);
	}

	
	close(sockfd);
	return 0;
}


void finish(int seq, int ack){
	header fin1;
	ack += 1;
	createHeader(&fin1, seq, ack, 1, 0, 0);
	int n = sendto(sockfd, &fin1, HEADSIZE, 0, (struct sockaddr *) &their_addr, len);
	if(n < 0){
		errorHandler("Can't send to client.");
	}
	else{
		printLine(SEND, &fin1, 0);
	}

	header fin2;
	createHeader(&fin2, seq, 0, 0, 0, 1);
	n = sendto(sockfd, &fin2, HEADSIZE, 0, (struct sockaddr *) &their_addr, len);
	if( n < 0){
		errorHandler("Can't send to client.");
	}
	else{
		printLine(SEND, &fin2, 0);
	}

	header fin3;
	time_t finTime = time(0);
	while(1){
		if ( (time(0) - finTime) > 0.5){
			printf("TIMEOUT %i\n", seq);
			n = sendto(sockfd, &fin2, HEADSIZE, 0, (struct sockaddr *) &their_addr, len);
			if( n < 0){
				errorHandler("Can't send to client.");
			}
			else{
				printLine(RESEND, &fin2, 0);
				finTime = time(0);
			}			
		}
		else{
			n = recvfrom(sockfd, &fin3, HEADSIZE, 0, (struct sockaddr* ) &their_addr, &len);
			if (n != -1){
				printLine(RECV, &fin3, 0);
				break;
			}
		}
	}


}


/*
void finish(int seq, int ack){
	header fin1;
	ack += 1;
	createHeader(&fin1, seq, ack, 1, 0, 0);
	int n = sendto(sockfd, &fin1, HEADSIZE, 0, (struct sockaddr *) &their_addr, len);
	if(n < 0){
		errorHandler("Can't send to client.");
	}
	else{
		printLine(SEND, &fin1, 0);
	}

	header fin2;
	createHeader(&fin2, seq, 0, 0, 0, 1);
	n = sendto(sockfd, &fin2, HEADSIZE, 0, (struct sockaddr *) &their_addr, len);
	if( n < 0){
		errorHandler("Can't send to client.");
	}
	else{
		printLine(SEND, &fin2, 0);
	}

	header fin3;
	n = recvfrom(sockfd, &fin3, HEADSIZE, 0, (struct sockaddr* ) &their_addr, &len);
	if (n < 0){
		errorHandler("Can't read from client.");
	}
	else{
		printLine(RECV, &fin3, 0);
	}
} */