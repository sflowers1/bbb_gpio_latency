#include <stdlib.h>
#include <stdio.h>
#include <sys/neutrino.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/netmgr.h>
#include <spawn.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <unistd.h>
#include <math.h>
#include <sys/timeb.h>
#include <sys/stat.h>
#include <hw/inout.h>
#include <sys/mman.h>

#define AM33XX_CONTROL_BASE		0x44e10000
#define AM33XX_GPIO1_BASE		0x4804C000
#define GPIO_DATAOUT            0x13C
#define GPIO_DATAIN				0x138
#define GPIO_OE                 0x134
#define GPIO_IRQSTATUS_SET_0 	0x34
#define GPIO_IRQ_CLRSTATUS0		0x3C
#define GPIO_LEVELDETECT0		0x140
#define GPIO_FALLINGDETECT		0x14C
#define GPIO_RISINGDETECT		0x148
#define IRQ_NUMBER				829
#define BIT28					0x10000000

void* addr;		// global pointer to output pin register

typedef struct
{
	struct _pulse   pulse;
} my_message_t;	// structure used in IPC

struct sigevent		irq_event;		// interrupt event

/* Thread dedicated to processing the gpio interrupt */
void * int_thread (void *arg)
{
	int id_int;

    // enable I/O privilege
    ThreadCtl (_NTO_TCTL_IO, NULL);

    memset(&irq_event, 0, sizeof(irq_event));
    irq_event.sigev_notify = SIGEV_INTR;
	if (-1 == (id_int = InterruptAttachEvent(IRQ_NUMBER, &irq_event,_NTO_INTR_FLAGS_TRK_MSK)))
	{
		perror("failed to attach interrupt\n");
	}
	else
	{
		printf("interrupt attached\n");
	}

	// now just wait for interrupt to trigger,
	// then set the pin immediately back high again,
	// ready then IRQ for the next trigger
    while (1)
    {
        InterruptWait (NULL, NULL);
        out32(addr, BIT28);
        InterruptUnmask(IRQ_NUMBER, id_int);
    }

	return 0;
}



int main(int argc, char *argv[]) {

	struct sigevent		event;
	struct itimerspec	itime;
	timer_t				timer_id;
	int					chid;
	int					rcvid;
	my_message_t 		msg;
	unsigned long		val;
	struct sched_param	sched_attr;

	chid = ChannelCreate(0); // receive signals on this channel

	ThreadCtl (_NTO_TCTL_IO, 0);

	// raise this process priority to max and scheduler to FIFO
	sched_attr.sched_priority = sched_get_priority_max(SCHED_FIFO);
	sched_setscheduler(0, SCHED_FIFO, &sched_attr);
	setprio(0, sched_get_priority_max(SCHED_FIFO));

	// -------------------------------
	// setup device registers
	// -------------------------------

	// set output pin gpio60: gpio1_28 (beaglebone p9/12) gpmc_ben1
	if(NULL == (addr = mmap_device_io(4, AM33XX_CONTROL_BASE + 0x878)))
	{
		perror("cant map addr");
		return 0;
	}
	out32(addr,0x7 | (2 << 3));	// mode 7 (gpio), PULLUP, OUTPUT
	munmap_device_io(addr, 4);

	// set input pin gpio61: gpio1_29: (beaglebone p8/26) gpmc_csn0
	if(NULL == (addr = mmap_device_io(4, (AM33XX_CONTROL_BASE+0x87C))))
	{
		perror("cant map addr");
		return 0;
	}
	out32(addr,0x7 | (2 << 3) | (1 << 5));
	munmap_device_io(addr, 4);

	// ensure pin is set for input
	if(NULL == (addr = mmap_device_io(4, (AM33XX_GPIO1_BASE + GPIO_OE))))
	{
		perror("cant map addr");
		return 0;
	}
	out32(addr,(1 << 29));
	munmap_device_io(addr, 4);

	// enable interrupts for input pin
	if(NULL == (addr = mmap_device_io(4, AM33XX_GPIO1_BASE + GPIO_IRQSTATUS_SET_0)))
	{
		perror("cant map addr");
		return 0;
	}
	out32(addr,(1 << 29));
	munmap_device_io(addr, 4);

	// set interrupt for falling edge
	if(NULL == (addr = mmap_device_io(4, AM33XX_GPIO1_BASE + GPIO_FALLINGDETECT)))
	{
		perror("cant map addr");
		return 0;
	}
	out32(addr,(1 <<29));
	munmap_device_io(addr, 4);

	// set OE to output for gpio1_28
	if(NULL == (addr = mmap_device_io(4, AM33XX_GPIO1_BASE + GPIO_OE)))
	{
		perror("cant map addr");
		return 0;
	}
	val = in32(addr);
	out32(addr, val & ~(BIT28));
	munmap_device_io(addr, 4);

	// create and hold pointer for output pin drive
	if(NULL == (addr = mmap_device_io(4, AM33XX_GPIO1_BASE + GPIO_DATAOUT)))
	{
		perror("cant map addr");
		return 0;
	}

	out32(addr, BIT28);	// initialise pin state high

	// start up a thread that is dedicated to interrupt processing
	// thread will inherit our scheduling properties
	pthread_create (NULL, NULL, int_thread, NULL);

	//Timer setup.
	event.sigev_notify = SIGEV_PULSE;
	event.sigev_coid = ConnectAttach(ND_LOCAL_NODE, 0, chid, _NTO_SIDE_CHANNEL, 0);
	event.sigev_priority = getprio(0);
	event.sigev_code = _PULSE_CODE_MINAVAIL;
	timer_create(CLOCK_REALTIME, &event, &timer_id);

	//Initialise the timer-structure members.
	itime.it_value.tv_sec = 1;
	itime.it_value.tv_nsec = 0;
	itime.it_interval.tv_sec = 0;
	itime.it_interval.tv_nsec = 10 * 1000 * 1000;	// 10ms
	timer_settime(timer_id, 0, &itime, NULL); 		// Set the timer going

	InterruptEnable();

	for (;;)
	{
		rcvid = MsgReceive(chid, &msg, sizeof(msg), NULL);	// block on message reception

		if(rcvid != -1)
		{
			if(rcvid == 0) 	// Means we received a pulse
			{
				if (msg.pulse.code == _PULSE_CODE_MINAVAIL)	// is it our timer?
				{
					out32(addr, 0);	// set the pin low
				}
			}
		}
	}

	munmap_device_io(addr, 4);
	return EXIT_SUCCESS;
}
