# Makefile for compiling Kernel
# modules on the fly.

obj-m += test-irq-latency.o

KDIR := /home/ste/mkit/linux-dev/KERNEL
#INCLUDE += -I$(KDIR)/arch/arm/mach-omap2/include
#INCLUDE += -I$(KDIR)/arch/arm/plat-omap/include

all:
	make -C $(KDIR) $(INCLUDE) M=$(PWD) modules
clean:
	make -C $(KDIR) $(INCLUDE) M=$(PWD) clean

