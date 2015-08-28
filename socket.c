#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <netdb.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <search.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>

#include "misc.h"

static struct sockaddr_in their_addr;

//Server
int udpsocket( int myport )
{
    struct sockaddr_in my_addr;
    int sockfd;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0))== -1)
    {
        perror("Socket");
        exit(1);
    }

    my_addr.sin_family = AF_INET; // host byte order
    my_addr.sin_addr.s_addr = INADDR_ANY; // automatically fill with my IP
    my_addr.sin_port = htons(myport); // short, network byte order

    memset(&(my_addr.sin_zero),0, 8); // zero the rest of the struct
    int optval = 1;
    setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,(void*)&optval,sizeof(optval));

    // binding to the socket
    if (bind(sockfd, (struct sockaddr *)&my_addr,sizeof(struct sockaddr)) == -1)  
    {
        perror("Bind ");
        exit(1);
    }
    return sockfd;
}

//Client
int udpclient( char* host, int theirport )
{
    int sockfd;

    their_addr.sin_family = AF_INET; // host byte order
    their_addr.sin_port = htons(theirport); // short, network byte order
    inet_pton(AF_INET, host, &their_addr.sin_addr);
    memset(&(their_addr.sin_zero),0, 8); // zero the rest of the struct

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0))== -1)
    {
        perror("Socket ");
        exit(0);
    }
    return sockfd;
}

void send_msg ( int sockfd, unsigned char * outMsgP, int len)
{   
    int numbytes;
    if ((numbytes = sendto(sockfd,outMsgP , len, 0,(struct sockaddr *)&their_addr, sizeof(struct sockaddr))) == -1)
    {
        perror("Send to ");
        exit(0);
    }
}


int recv_msg(int sockfd, unsigned char * inBufP )
{
    socklen_t addr_len;
    int numbytes = 0,flag =0;

    addr_len = sizeof(struct sockaddr);

    if ((numbytes=recvfrom(sockfd,inBufP, MAX_LEN , flag,(struct sockaddr *)&their_addr, &addr_len)) == -1)
    {
        perror("Recvfromsockfd :");
        //exit(0);
    }
    return numbytes;
}
