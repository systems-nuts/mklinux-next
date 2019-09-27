#
# Makefile for Popcorn messaging.
#

CONFIG_POPCORN_KMSG=m

obj-$(CONFIG_POPCORN_KMSG) += kmsg.o
kmsg-objs = ringBuffer.o pcn_kmsg.o

obj-$(CONFIG_POPCORN_KMSG) += pcn_kmsg_test.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean


