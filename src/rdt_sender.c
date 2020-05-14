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
double window_size = 1;
int ssthresh = 64;
int last_ack = 0;
int last_sent = -1;
int last_packet = 0;
int duplicate = 0;
short slow_start = 1;//1 == slow start; 0 == congestion avoidance
FILE *fp;
int sockfd, serverlen, total_packets;//rcvr socket, serveraddrsize, numofpackets
struct sockaddr_in serveraddr; //server
struct itimerval timer; //timer
tcp_packet *sndpkt;//sent packet
tcp_packet *recvpkt;//packet reveived
sigset_t sigmask;  //signal for timeout


void resend_packets(int sig);
tcp_packet * make_send_packet(int index);
void start_timer();
void send_packets(int start, int end);
int max(int a, int b){
    return (a > b)? a : b;
}
int min(int a, int b){
    return (a < b)? a : b;
}



void resend_packets(int sig)
{
    if (sig == SIGALRM)
    {
        //Resend all packets range between
        //lastack and lastack + windowsize
        VLOG(INFO, "Timout happened");
        ssthresh = max(2, (int)window_size / 2);
        slow_start = 1;
        window_size = 1;
        send_packets(last_ack, last_ack + (int)(window_size - 1));
        //start_timer();
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
    fseek(fp, index * DATA_SIZE, SEEK_SET); //Seek to the correct position
    char buffer[DATA_SIZE]; //Buffer after reading packet number packet.
    size_t sz = fread(buffer, 1, DATA_SIZE, fp); //Read the data
    tcp_packet *sndpkt = make_packet(sz); //Create our packet with size sz
    sndpkt->hdr.seqno = index * DATA_SIZE; // as an index of a byte in the file
    memcpy(sndpkt->data, buffer, sz); //Put data into our packet
    return(sndpkt);
}


void send_packets(int start, int end){
   
   int serverlen = sizeof(serveraddr);
    if (end > total_packets - 1){// make sure end < total
        end = total_packets - 1;
    }
    printf("start = %d end = %d\n", start , end);
    if(start > end){
        return;
    }
    while(start <=end && start <= total_packets - 1){
        
    /* Create our snpkt */
        tcp_packet * sndpkt = make_send_packet(start);
        if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0,
                    ( const struct sockaddr *)&serveraddr, serverlen) < 0)
        {
            error("sendto");
        }
        last_sent = start;
        start++;
    }

    
}

int main (int argc, char **argv)
{
    int portno;
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
    if (fp == NULL) {
        error(argv[3]);
    }
    total_packets = (sz + DATA_SIZE - 1) / DATA_SIZE;// counting number of packets
    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        error("ERROR opening socket");
    }

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
    //FILE *csv;
//    csv  = fopen("cwnd.csv", "w");
//    if (csv == NULL) {
//       error("cwnd.csv");
//    }
    //---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    send_packets(0, min(total_packets, (int)window_size) - 1);
    int ackno = 0;
    double increase_cwnd = 0;
    start_timer();//
    while (1)
    {
       // fputs(itoa((int)(window_size - 0.5)), csv);
//        fprintf(csv, "%d\n", (int)(window_size));
        if(recvfrom(sockfd, buffer, MSS_SIZE, 0,//receive packet
            (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0)
         {
             error("recvfrom");
         }
         recvpkt = (tcp_packet *)buffer;
         ackno = recvpkt->hdr.ackno; //gets the ACK number
         increase_cwnd = ackno - last_ack;
         

         printf("total=%d ackno=%d lastack=%d last_sent=%d ",total_packets, ackno, last_ack, last_sent);
        printf("window size = %d\n", (int)window_size);
         if (ackno > last_ack){ //if it is an ACK for a new packet
             last_ack = ackno;//update last ACK number
             stop_timer();// ACK has been received
             if (ackno >= total_packets){ //if it is the last ACK then transmission has been successful
                 tcp_packet * sndpkt = make_packet(0); //make an empty packet
                 if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0, //send an empty packet to let receiver know that it is time to say good bye
                             ( const struct sockaddr *)&serveraddr, serverlen) < 0)
                 {
                     error("sendto");
                 }
                 printf("Completed transfer\n");
//                 fclose(csv);
                 break;
             }
//--------------------------------------------------------------------------------------------------------------------------------------
             if(slow_start == 0){//congestion avodance mode (or fast recovery)
                 window_size += increase_cwnd / window_size;
             }
             else{//slow start
                 window_size += increase_cwnd;
                 if(ssthresh <= window_size){
                     slow_start = 0; //switch to congestion avoidance
                 }
             }
              // continue sending packets
            last_sent  = max(last_ack, last_sent + 1);
             int end = (last_ack + (int)(window_size - 1));
            send_packets(last_sent ,end);
            start_timer();// starts a new timer because new packets are sent
             duplicate = 0;
         }
         else if(ackno == last_ack){
             duplicate++;
             if(duplicate > 2){// fast recovery
                 slow_start = 0;
                 last_sent = ackno ;
                 ssthresh = max(2, (int)window_size / 2);
                 window_size = ssthresh;
                 int end = (last_ack + (int)(window_size - 1));
                 send_packets(ackno, end);
             }
         }
    }

    return 0;

}



