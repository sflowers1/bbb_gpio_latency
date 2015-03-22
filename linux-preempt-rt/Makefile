# Makefile for compiling Kernel
# modules on the fly.

obj-m += test-irq-latency.o

KDIR := /home/ste/yl/poky/build/tmp/work/beaglebone-poky-linux-gnueabi/linux-yocto-rt/3.14.4+gitAUTOINC+183622e809_0822940252-r0/linux-beaglebone-preempt-rt-build
INCLUDE += -I$(KDIR)/arch/arm/mach-omap2/include
INCLUDE += -I$(KDIR)/arch/arm/plat-omap/include

all:
	make -C $(KDIR) $(INCLUDE) M=$(PWD) modules
clean:
	make -C $(KDIR) $(INCLUDE) M=$(PWD) clean

