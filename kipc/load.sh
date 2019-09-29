#!/bin/sh

insmod ./kmsg.ko
insmod ./pcn_kmsg_test.ko

lsmod | grep kmsg
