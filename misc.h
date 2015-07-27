#ifndef MISC_H
#define MISC_H

#define MAX_LEN 1500

#define START_TRAFFIC              1
#define STOP_TRAFFIC               2
#define CONFIG_TRAFFIC             3
#define SEND_TX_PPS                11

#define ARPHRD_ETHER               1
#define ARP_OP_REQUEST             1
#define ARP_OP_REPLY               2
#define RARP_OP_REQUEST            3
#define RARP_OP_REPLY              4



int master_loop(void );

//Socket Methods
int udpsocket( int myport );
int udpclient( int theirport );
void send_msg ( int sockfd, unsigned char * outMsgP, int len);
int recv_msg(int sockfd, unsigned char * inBufP );

//Database Methods
int database_connect( char *database);

int insert_table(int port, char *port_name, int status);

int update_table(int port, int status);

#endif
