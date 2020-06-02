#include <sys/types.h> 
#include <sys/socket.h> 
#include <arpa/inet.h> 
#include <netinet/in.h> 
#include <sys/stat.h>
#include <fcntl.h>

#include "util.h"


struct sockaddr_in servaddr;
socklen_t len = sizeof(servaddr);
int sockfd;

void sendWindow(window w, int isResend); /* send all packets in window to server */
void sendLastPacket(window w); /* send the last packet when window moves forward */
void finish(int seq, int ack); /* 4-way finishing */

int main(int argc, char** argv){

	/*------   validate arguments  --------*/
	if(argc != 4){
		perror("Missing arguments.");
		exit(1);
	}

	int portno = atoi(argv[2]);
	if (portno == 0){
		perror("Invalid port number.");
		exit(1);
	}

	/*------   establish UDP  --------*/
	if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { 
        perror("socket creation failed"); 
        exit(1); 
    } 
	  
    memset(&servaddr, 0, sizeof(servaddr)); 
      
    // Filling server information 
    servaddr.sin_family = AF_INET; 
    servaddr.sin_port = htons(portno); 
    servaddr.sin_addr.s_addr = inet_addr(argv[1]); 

	/*------   3-way handshaking  --------*/    
    srand(time(0));
    uint16_t initSeq = rand() % SEQNUM;
    header sp;
    createHeader(&sp, initSeq,0,0,1,0);
    int n = sendto(sockfd, &sp, HEADSIZE, 0, (struct sockaddr*) &servaddr, len);
    if(n <= 0){
    	errorHandler("Failed to send to server");
    }
    else{
    	printLine(SEND, &sp, 0);
    }


    n = fcntl(sockfd, F_SETFL, O_NONBLOCK);
	if ( n < 0 ){
		errorHandler("Can't set file descriptor to non-blocking.");
	}

	time_t synTime = time(0);
	header sa;
	while(1){
		if ((time(0) - synTime) > 0.5){
			printf("TIMEOUT %u\n", (unsigned int) initSeq);
			n = sendto(sockfd, &sp, HEADSIZE, 0, (struct sockaddr *) &servaddr, len);
			if (n < 0){
				errorHandler("Failed to send to server.");
			}
			else{
				printLine(RESEND,&sp, 0);
				synTime = time(0);
			}
		}
		else{
			n = recvfrom(sockfd, &sa, HEADSIZE, 0, (struct sockaddr *) &servaddr, &len);
			if (n != -1){
				if(sa.ACK_FLAG != 1 || sa.SYN_FLAG != 1){
					errorHandler("Wrong packet.");
				}
				else{
					printLine(RECV, &sa, 0);
					break;
				}
			}
		}
	}


	/*
    header sa;
    n = recvfrom(sockfd, &sa, HEADSIZE, 0, (struct sockaddr *) &servaddr, &len);
    if (n < 0){
    	errorHandler("Failed to receive from server");
    }
    else if(sa.ACK_FLAG != 1 || sa.SYN_FLAG != 1){
    	errorHandler("Wrong packet.");
    }
    else{
		printLine(RECV, &sa, 0);
    }
    */

	/*------   creating packets  --------*/
	uint16_t seq = sa.ack;
	uint16_t ack = sa.seq + 1;
	packet msg;

	FILE* filefd = fopen(argv[3],"rb");
	struct stat st;
	if (filefd == NULL){
		errorHandler("Unable to open file.");
	}
	stat(argv[3],&st);
	unsigned long fileSize = st.st_size;
	int fileNumber = fileSize / DATASIZE;
	int remainBytes = fileSize % DATASIZE;
	char* buffer = malloc(fileSize * sizeof(char));
	fread(buffer, sizeof(char),fileSize,filefd);
	fclose(filefd);

	packet* p = (packet *)calloc(fileNumber+1,PACKETSIZE);
	int i = 0;
	for(i = 0; i < fileNumber; i++){
		char temp[DATASIZE];
		memset(temp, 0, DATASIZE);
		memcpy(temp, buffer+i*DATASIZE, DATASIZE);
		createPacket(&p[i],seq,0,0,0,0,temp,DATASIZE);
		seq += DATASIZE;
		seq = (seq > SEQNUM) ? (seq % (SEQNUM + 1)) : seq;

	}

	int packetNumber = fileNumber;
	if (remainBytes > 0){
		packetNumber = fileNumber + 1;
		char* temp = (char *) malloc(DATASIZE * sizeof(char));
		memset(temp,0,DATASIZE);
		memcpy(temp, buffer+fileNumber*DATASIZE, remainBytes);
		// strncpy(temp,buffer+fileNumber*511,remainBytes); 
		createPacket(&p[fileNumber],seq,0,0,0,0,temp,remainBytes);

		seq += remainBytes;
		seq = (seq > SEQNUM) ? (seq % (SEQNUM + 1)) : seq;		

	}

	p[0].head.ack = ack;
	p[0].head.ACK_FLAG = 1;

	/*------   start transmitting file  --------*/	
	window w;
	createWindow(&w, p, packetNumber, 0);

	/*
	FILE* debug = fopen("result","wb");
	for (i = 0; i < packetNumber; i++){
		fwrite(w.p[i].payload, sizeof(char), w.p[i].head.len, debug);
	}
	fclose(debug); */

	sendWindow(w,0);

	n = fcntl(sockfd, F_SETFL, O_NONBLOCK);
	if ( n < 0 ){
		errorHandler("Can't set file descriptor to non-blocking.");
	}
	time_t startTime = time(0);


	while(1){
		if ( time(0) - startTime >= 0.5 ){
			startTime = time(0);
			sendWindow(w,1);
		} 
		else{ 
			header tempAck;
			n = recvfrom(sockfd, &tempAck, HEADSIZE, 0, (struct sockaddr *) &servaddr, &len);
			if (n != -1){
				printLine(RECV, &tempAck, 0);
				int expectedAck = w.initSeq + w.p[0].head.len;
				expectedAck = (expectedAck > SEQNUM) ? (expectedAck % (SEQNUM+1)) : expectedAck;
				if(tempAck.ACK_FLAG != 1){
					errorHandler("Wrong ACK packet.");
				}
				else if ( tempAck.ack < expectedAck){
					continue;
				}
				else if ( tempAck.ack == expectedAck ){
					moveWindow(&w, p, packetNumber);
					if ( w.size == 10){
						sendLastPacket(w);
					}
					if ( w.size == 0 ){
						break;
					}
					startTime = time(0);
				}
				else{
					while ( expectedAck < tempAck.ack ){
						moveWindow(&w, p, packetNumber);
						if ( w.size == 10 ){
							sendLastPacket(w);
						}
						if ( w.size == 0 ){
							break;
						}
					}
					startTime = time(0);
				}
			}
		}
	}  

	finish(seq,ack);

    return 0;
}



void finish(int seq, int ack){
	int n;
	header fin1,fin4, fin2;
	createHeader(&fin1, seq, 0, 0, 0, 1);
	createHeader(&fin4, seq+1, ack+1, 1, 0, 0);

	n = sendto(sockfd, &fin1, HEADSIZE, 0, (struct sockaddr*) &servaddr, len);
	if (n < 0){
		errorHandler("Failed to send to server.");
	}
	else{
		printLine(SEND, &fin1, 0);
	}

	time_t finTime = time(0);
	while(1){
		if ((time(0) - finTime) > 0.5){
			printf("TIMEOUT %i\n", seq);
			n = sendto(sockfd, &fin1, HEADSIZE, 0, (struct sockaddr*) &servaddr, len);		
			if (n < 0){
				errorHandler("Failed to send to server.");
			}
			else{
				printLine(RESEND, &fin1, 0);
				finTime = time(0);
			}	
		}
		else{
			n = recvfrom(sockfd, &fin2, HEADSIZE, 0, (struct sockaddr *) &servaddr, &len);
			if ( n != -1){
				printLine(RECV, &fin2, 0);
				if(fin2.FIN_FLAG == 1){
					n = sendto(sockfd, &fin4, HEADSIZE, 0, (struct sockaddr* ) &servaddr, len);
					if (n <= 0){
						errorHandler("Failed to send to server.");
					}
					else{
						printLine(SEND, &fin4, 0);
					}
				}
				if (fin2.ACK_FLAG == 1){
					break;
				}
			}
		}
	}

	header fin3;
	while(1){
		n = recvfrom(sockfd, &fin3, HEADSIZE, 0, (struct sockaddr *) &servaddr, &len);
		if ( n != -1 ){
			printLine(RECV, &fin3, 0);
			break;
		}
	}

	n = sendto(sockfd, &fin4, HEADSIZE, 0, (struct sockaddr* ) &servaddr, len);
	if (n <= 0){
		errorHandler("Failed to send to server.");
	}
	else{
		printLine(SEND, &fin4, 0);
	}

	time_t finalCheck = time(0);
	while(1){
		if( time(0) - finalCheck > 0.5){
			break;
		}
		else{
			n = recvfrom(sockfd, &fin3, HEADSIZE, 0, (struct sockaddr *) &servaddr, &len);
			if ( n != -1 ){
				printLine(RECV, &fin3, 0);
				n = sendto(sockfd, &fin4, HEADSIZE, 0, (struct sockaddr* ) &servaddr, len);
				if (n <= 0){
					errorHandler("Failed to send to server.");
				}
				else{
					printLine(SEND, &fin4, 1);
				}				
			}
		}
	}


}




/*
void finish(int seq, int ack){
	header fin1; 
	createHeader(&fin1, seq, 0, 0, 0, 1);
	int n = sendto(sockfd, &fin1, HEADSIZE, 0, (struct sockaddr*) &servaddr, len);
	if (n < 0){
		errorHandler("Failed to send to server.");
	}
	else{
		printLine(SEND, &fin1, 0);
	}


	header fin2, fin3;
	time_t finTime = time(0);
	while(1){
		if ((time(0) - finTime) > 0.5){
			printf("TIMEOUT %i\n", seq);
			n = sendto(sockfd, &fin1, HEADSIZE, 0, (struct sockaddr*) &servaddr, len);
			if (n < 0){
				errorHandler("Failed to send to server.");
			}
			else{
				printLine(RESEND, &fin1, 0);
				finTime = time(0);
			}			
		}
		else{
			n = recvfrom(sockfd, &fin2, HEADSIZE, 0, (struct sockaddr *) &servaddr, &len);
			if (n != -1){
				printLine(RECV, &fin2, 0);
				break;
			}
		}
	}

	int opts;
    opts = fcntl(sockfd,F_GETFL);
    if (opts < 0) {
    	errorHandler("fcntl error.");
    }
    opts = opts & (~O_NONBLOCK);
    if (fcntl(sockfd,F_SETFL,opts) < 0) {
        errorHandler("fcntl error.");
    }  

 	n = recvfrom(sockfd, &fin3, HEADSIZE, 0, (struct sockaddr *) &servaddr, &len);
	if (n < 0){
		errorHandler("Failed to receive from server.");
	}
	else{
		printLine(RECV, &fin3, 0);
	}   

///////////
	header fin2;
	n = recvfrom(sockfd, &fin2, HEADSIZE, 0, (struct sockaddr *) &servaddr, &len);
	if (n < 0){
		errorHandler("Failed to receive from server.");
	}
	else{
		printLine(RECV, &fin2, 0);
	}

	header fin3;
	n = recvfrom(sockfd, &fin3, HEADSIZE, 0, (struct sockaddr *) &servaddr, &len);
	if (n < 0){
		errorHandler("Failed to receive from server.");
	}
	else{
		printLine(RECV, &fin3, 0);
	}
//////////

	header fin4;
	seq += 1;
	ack += 1;
	createHeader(&fin4, seq, ack, 1, 0, 0);
	n = sendto(sockfd, &fin4, HEADSIZE, 0, (struct sockaddr*) &servaddr, len);
	if (n < 0){
		errorHandler("Failed to send to server.");
	}
	else{
		printLine(SEND, &fin4, 0);
	}
}
*/




void sendWindow(window w, int isResend){
	int i = 0;
	for (i = 0; i < w.size; i++){
		int n = sendto(sockfd, &(w.p[i]), PACKETSIZE, 0, (struct sockaddr*) &servaddr, len);
		if (n < 0){
			errorHandler("Failed to send data to server.");
		}
		else{
			if (isResend)
				printPacket(RESEND,&(w.p[i]),0);
			else
				printPacket(SEND,&(w.p[i]),0);
		}
	}
}

void sendLastPacket(window w){
	int n = sendto(sockfd, &(w.p[9]), PACKETSIZE, 0, (struct sockaddr*) &servaddr, len);
	if ( n < 0 ){
		errorHandler("Failed to send data to server.");
	}
	else{
		printPacket(SEND, &(w.p[9]), 0);
	}
}
