#!/bin/sh

rmmod pcn_kmsg_test
rmmod kmsg

lsmod | grep kmsg
