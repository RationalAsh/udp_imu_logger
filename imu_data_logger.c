#include <stdio.h>
#include <stdlib.h>
#include <string.h> 
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>

#define UDP_THREAD_RUN 1
#define UDP_THREAD_STOP 0
#define UDP_THREAD_PAUSE 2

//Mutexes for the thread run/pause/stop flag
pthread_mutex_t udp_thread_status_mutex;
int udp_thread_status = 0;

struct udp_thread_args
{    
    char *ip;
    int port;
    int msg_len;
};

void print_hex(const char *s)
{
  while(*s)
    printf("%02x ", *s++);
  printf("\n");
}

float ntohf(uint32_t p)
{
    float f = ((p>>16)&0x7fff); // whole part
    f += (p&0xffff) / 65536.0f; // fraction

    if (((p>>31)&0x1) == 0x1) { f = -f; } // sign bit set

    return f;
}

void *udp_recv_thread(void *arguments)
{
    /*
      This is the thread that handles the UDP data logging.
      Spawn as many of these threads as there are sensors.
      For a real time process, you could write to shared memory
    */
    //Get the ip, port and message length
    struct udp_thread_args *args = arguments;
    
    //Structs to hold the socket data
    struct sockaddr_in si_me, si_dest;
    int sock, i, slen = sizeof(si_dest);
    int recv_len;
    
    //Buffer to hold the received data
    char *buf = (char *) malloc(args->msg_len*sizeof(char));
    memset(buf, 0, sizeof(buf));
    //Get in 4 byte segments
    unsigned long tbuf[9];
    //Floats
    float imu_data_line[9];
    
    //Create a UDP socket 
    if((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
    {
	printf("Could not create the socket\n");
	printf("IP: %s, PORT: %d, MSG_LEN: %d\n", args->ip, args->port, args->msg_len);
	pthread_exit(NULL);
    }
    
    //Zero out the struct before populating it
    memset((char *) &si_me, 0, sizeof(si_me));
    
    //Fill the struct with the details
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(args->port);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    
    //Bind socket to port
    printf("Binding socket to port %d\n",args->port);
    if(bind(sock, (struct sockaddr *)&si_me, sizeof(si_me)) == -1)
    {
	printf("Binding socket failed.\n");
	pthread_exit(NULL);
    }

    printf("Waiting for signal to start experiment..\n");
    while(1)
    {
	pthread_mutex_lock(&udp_thread_status_mutex);
	int ts = udp_thread_status;
	pthread_mutex_unlock(&udp_thread_status_mutex);

	if(ts == UDP_THREAD_RUN) break;
	usleep(10000);
    }
    
    //Keep listening for data
    while(1)
    {
	fflush(stdout);
	//Blocking call to receive data on UDP port
	if((recv_len = recvfrom(sock, buf, (args->msg_len)-1, 0, (struct sockaddr *)&si_dest, &slen)) == -1)
	{
	    printf("Recv failed.\n");
	    //pthread_exit(NULL);
	}

	memcpy(tbuf, buf, (args->msg_len)-1);

	for(int i=0; i<9; i++)
	{
	    imu_data_line[i] = ntohf(tbuf[i]);
	}

	pthread_mutex_lock(&udp_thread_status_mutex);
	int ts = udp_thread_status;
	pthread_mutex_unlock(&udp_thread_status_mutex);

	printf("Raw packet: ");
	print_hex(buf);
	printf("Received packet %f, %f, %f, %f from IP: %s, PORT: %d\n",
	       imu_data_line[0], imu_data_line[1], imu_data_line[2], imu_data_line[3],
	       inet_ntoa(si_dest.sin_addr), ntohs(si_dest.sin_port));
	
	if(ts == UDP_THREAD_STOP)
	{
	    break;
	}
    }

    close(sock);
    
    //printf("IP: %s, PORT: %d, MSG_LEN: %d\n", args->ip, args->port, args->msg_len);
}

int main(int argc, char**argv)
{
    pthread_t test_thread;
    struct udp_thread_args *args;
    args->ip = "192.168.0.1";
    args->port = 9751;
    args->msg_len = 37;
    
    int experiment_duration;
    int sample_rate;
    char c;
    
    printf("Enter experiment duration in seconds: ");
    scanf("%d",&experiment_duration);
    printf("Enter IMU sample rate (Hz): ");
    scanf("%d",&sample_rate);
    
    printf("Press enter to start the experiment\n");
    //scanf("%c",&c);
    c = getchar();
    c = getchar();
    
    printf("Starting experiment...\n");
    
    //Creating a single network test thread
    if(pthread_create(&test_thread, NULL, udp_recv_thread, (void *)args) != 0)
    {
	printf("Thread creation failed\n");
	return -1;
    }

    //Signal threads to start recording data
    pthread_mutex_lock(&udp_thread_status_mutex);
    udp_thread_status = UDP_THREAD_RUN;
    pthread_mutex_unlock(&udp_thread_status_mutex);

    //Get the current time in seconds
    int t0 = (int) time(NULL);
    while(1)
    {
	int t1 = (int) time(NULL);
	if((t1 - t0) >= experiment_duration)
	{
	    printf("Experiment duration is over stopping program and saving data...\n");
	    //Signal threads to start recording data
	    pthread_mutex_lock(&udp_thread_status_mutex);
	    udp_thread_status = UDP_THREAD_STOP;
	    pthread_mutex_unlock(&udp_thread_status_mutex);
	    break;
	}
	printf("%d Seconds since start of experiment.\n", t1 - t0);
	sleep(1);
    }
    return pthread_join(test_thread, NULL);
}
