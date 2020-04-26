#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <assert.h>

#include"packet.h"
#include"common.h"

#define STDIN_FD    0
#define RETRY  120 //milli second

int next_seqno=0;
int send_base=0;
int window_size = 10;
int last_ack = 0;
int last_sent = -1;
FILE *fp;
int sockfd, serverlen, total_packets;//rcvr socket, serveraddrsize, numofpackets
struct sockaddr_in serveraddr; //server
struct itimerval timer; //timer
tcp_packet *sndpkt;//sent packet
tcp_packet *recvpkt;//packet reveived
sigset_t sigmask;  //signal for timeout


void send_packets(int start, int end);
tcp_packet * make_send_packet(int index);
void start_timer();
void resend_packets(int sig);


void resend_packets(int sig)
{
    if (sig == SIGALRM)
    {
        //Resend all packets range between
        //sendBase and nextSeqNum
        VLOG(INFO, "Timout happend");
        send_packets(last_ack, last_ack+window_size-1);
        start_timer();
    }
}


void start_timer()
{
    sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
    setitimer(ITIMER_REAL, &timer, NULL);
}


void stop_timer()
{
    sigprocmask(SIG_BLOCK, &sigmask, NULL);
}


/*
 * init_timer: Initialize timeer
 * delay: delay in milli seconds
 * sig_handler: signal handler function for resending unacknoledge packets
 */
void init_timer(int delay, void (*sig_handler)(int))
{
    signal(SIGALRM, resend_packets);
    timer.it_interval.tv_sec = delay / 1000;    // sets an interval of the timer
    timer.it_interval.tv_usec = (delay % 1000) * 1000;
    timer.it_value.tv_sec = delay / 1000;       // sets an initial value
    timer.it_value.tv_usec = (delay % 1000) * 1000;

    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGALRM);
}

tcp_packet * make_send_packet(int index){
    char buffer[DATA_SIZE]; //Buffer after reading packet number packet.
    tcp_packet *sndpkt; //Create the packet.
    fseek(fp, index * DATA_SIZE, SEEK_SET); //Seek to the correct position
    size_t sz = fread(buffer, 1, DATA_SIZE, fp); //Read the data
   sndpkt = make_packet(sz); //Create our packet
   memcpy(sndpkt->data, buffer, sz); //Populate the data section with buffer
   sndpkt->hdr.seqno = index * DATA_SIZE; //Use byte-level sequence number

  //last_sent=max_int(last_sent,index);

  return(sndpkt);
}


void send_packets(int start, int end){
   // make sure end < max_size
   if (start == -1){
       tcp_packet * sndpkt = make_packet(0);
       if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0,
                   ( const struct sockaddr *)&serveraddr, serverlen) < 0)
       {
           error("sendto");
       }
       return;
   }
   int serverlen = sizeof(serveraddr);
   if (end >= total_packets) end = total_packets - 1;
   for (int i = start; i <= end; i ++){
       /* Create our snpkt */
       tcp_packet * sndpkt = make_send_packet(i);
       /*
        * If the sendto is called for the first time, the system will
        * will assign a random port number so that server can send its
        * response to the src port.
        */
       if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0,
                   ( const struct sockaddr *)&serveraddr, serverlen) < 0)
       {
           error("sendto");
       }
   }
}

int main (int argc, char **argv)
{
    int portno, len;
    int next_seqno;
    char *hostname;
    char buffer[DATA_SIZE];
    //FILE *fp;

    /* check command line arguments */
    if (argc != 4) {
        fprintf(stderr,"usage: %s <hostname> <port> <FILE>\n", argv[0]);
        exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);
    fp = fopen(argv[3], "r");
    fseek(fp, 0L, SEEK_END);//
     long sz = ftell(fp);//
     total_packets = sz / DATA_SIZE;// full packets (counting amount of packets)
     if (sz % DATA_SIZE > 0) total_packets ++;//partial packet IMCODE
    if (fp == NULL) {
        error(argv[3]);
    }

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");


    /* initialize server server details */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serverlen = sizeof(serveraddr);

    /* covert host into network byte order */
    if (inet_aton(hostname, &serveraddr.sin_addr) == 0) {
        fprintf(stderr,"ERROR, invalid host %s\n", hostname);
        exit(0);
    }

    /* build the server's Internet address */
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(portno);

    assert(MSS_SIZE - TCP_HDR_SIZE > 0);

    //Stop and wait protocol

    init_timer(RETRY, resend_packets);
    next_seqno = 0;
    send_packets(0, window_size-1);//
    start_timer();//
    while (1)
    {
        if(recvfrom(sockfd, buffer, MSS_SIZE, 0,//
            (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0)
         {
             error("recvfrom");
         }
         recvpkt = (tcp_packet *)buffer;
         int ackno = recvpkt->hdr.ackno;
         //if (recvpkt->hdr.ackno % DATA_SIZE != 0) ackno ++;
         printf("total=%d ackno=%d lastack=%d\n",total_packets, ackno, last_ack);
         if (ackno > last_ack){ //
             if (ackno >= total_packets - 1){ //
                 send_packets(-1,-1);
                 printf("Completed transfer\n");
                 break;
             }
             send_packets(last_ack+window_size,ackno+window_size-1);
             stop_timer();
             start_timer();
             last_ack = ackno;
             
         }
    }

    return 0;

}



