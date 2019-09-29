#ifndef __LINUX_MULTIKERNEL_H
#define __LINUX_MULTIKERNEL_H
/*
 * Boot parameters and other support stuff for MKLinux
 *
 * (C) Ben Shelton <beshelto@vt.edu> 2012
 */

// TODO put it as a configuration parameter somewhere in the Linux kernel
//#define POPCORN_MAX_CPUS 64

#define POPCORN_MAX_CPUS 256

extern int mklinux_boot;

#endif /* __LINUX_MULTIKERNEL_H */

