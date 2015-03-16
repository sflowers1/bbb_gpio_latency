#include<sys/time.h>
#include<time.h>
#include<sys/types.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <sched.h>
#include <sys/mman.h>
#include "poll.h"
#include "fcntl.h"
#include "string.h"
#include "errno.h"
#include <sys/timerfd.h>
#include <sys/epoll.h>
#include <aio.h>

#define TIMER_OUT_PATH 	"/sys/class/gpio/gpio47"
//#define LED_OUT_PATH 	"/sys/class/gpio/gpio26"
#define IRQ_IN_PATH 	"/sys/class/gpio/gpio46"

void stack_prefault(void)
{
	unsigned char dummy[8192];
	memset(dummy, 0, 8192);
}


int main(int argc, char* argv[])
{   
	struct itimerspec itv;
	unsigned long long timer_increment = 0;
     
	struct sched_param sp;
   
	int epfd;
	int fd_in;
	int action;
	int fd_timer_out;      
	int fd_timer_in;
   
	int len;
	int i;
	struct aiocb cbtimer;
 
	// setup gpio
	if(system("echo 46 > /sys/class/gpio/export") == -1)
		perror("unable to export gpio 46");
  
	if(system("echo 47 > /sys/class/gpio/export") == -1)
		perror("unable to export gpio 47");
		
	if(system("echo 26 > /sys/class/gpio/export") == -1)
		perror("unable to export gpio 26");
	
	// timer out
	if(system("echo out > /sys/class/gpio/gpio47/direction") == -1)
		perror("unable to set 47 to output");

	// irq in
	if(system("echo in > /sys/class/gpio/gpio46/direction") == -1)
		perror("unable to set 46 to input");
		
	if(system("echo falling > /sys/class/gpio/gpio46/edge") == -1)
		perror("unable to set 46 edge");
	
	// set scheduling parameters
	sp.sched_priority = 49;
	if(sched_setscheduler(0, SCHED_FIFO, &sp) == -1)
	{
		perror("setscheduler");
		exit(-1);
	}
		
	// lock memory
	if(mlockall(MCL_CURRENT|MCL_FUTURE) == -1)
		perror("mlockall");
	
	stack_prefault();
	   
	// Set up timer
   	fd_timer_in = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
	printf("fd_timer:%d\n",fd_timer_in);

	if(fd_timer_in < 0)
	{
		perror("timerfd_create()");
	}
	
	itv.it_value.tv_sec = 0;
	itv.it_value.tv_nsec = 10000000;
	itv.it_interval.tv_sec = 0;
	itv.it_interval.tv_nsec = 10000000;
	if(-1 == timerfd_settime(fd_timer_in, 0, &itv, NULL))
	{
		perror("settime()");
	}
      
	// setup file descriptor for poll()		
	fd_in = open(IRQ_IN_PATH "/value", O_RDONLY | O_SYNC | O_NONBLOCK);
	printf("fd irq input:%d\n",fd_in);
   
	if(fd_in < 0)
	{
		perror("file open problem");
		exit(0);
	}
   
	// setup epoll instance
	epfd = epoll_create1(EPOLL_CLOEXEC);
   
	if(epfd < 0)
	{
		perror("epoll_create");
	}
   
	struct epoll_event ev; 
	struct epoll_event events[2];
   
	// add timerfd to epoll
	ev.events = EPOLLIN | EPOLLERR | EPOLLET;
	ev.data.ptr = &fd_timer_in;
	epoll_ctl(epfd, EPOLL_CTL_ADD, fd_timer_in, &ev);
   
	// add fd_in to epoll
	ev.events = EPOLLPRI | EPOLLERR | EPOLLET;
	ev.data.ptr = &fd_in;
	epoll_ctl(epfd, EPOLL_CTL_ADD, fd_in, &ev);
   
	// setup output fds
	fd_timer_out = open( TIMER_OUT_PATH "/value", O_WRONLY | O_DSYNC | O_NONBLOCK);
   
	memset(&cbtimer, 0, sizeof(cbtimer));
	cbtimer.aio_fildes = fd_timer_out;
	cbtimer.aio_nbytes = 2;					   
   
	while(1)
	{
		action = epoll_wait(epfd, events, 2, -1);
		
		if(action < 0)
		{
			if(errno == EINTR)
			{
				// when signal interrupts poll, we poll again
				continue;
			}
			else
			{
				perror("poll failed");
				exit(0);
			}
		}
		
		if(action > 0)
		{
			for(i = 0; i < action; i++)
			{
				if(events[i].data.ptr == &fd_timer_in)
				{
					len = read(fd_timer_in, &timer_increment, sizeof(timer_increment));
					
					cbtimer.aio_buf = "0";		   
					aio_write(&cbtimer);
				}
				
				if(events[i].data.ptr == &fd_in)
				{	
					cbtimer.aio_buf = "1";
					aio_write(&cbtimer);					
				}
			}
		}
	}
   
   close(fd_in);
   close(fd_timer_in);
   printf("finished\n");
   return 0;
}
