# Build sentinel.c as a Linux kernel module
obj-m += sentinel.o

# Use the currently running kernel's build system
KDIR := /lib/modules/$(shell uname -r)/build

# Current driver directory
PWD := $(shell pwd)

# Default target: build the kernel module
all:
	make -C $(KDIR) M=$(PWD) modules

# Clean generated build files
clean:
	make -C $(KDIR) M=$(PWD) clean
