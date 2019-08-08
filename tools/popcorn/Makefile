obj-m	:= kmsg_ipi_test.o

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)

all: default calibrate

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

calibrate:
	$(CC) -o calibrate calibrate_tsc.c

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions
