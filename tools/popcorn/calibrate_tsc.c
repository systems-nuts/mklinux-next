/* 
 * x86 rdtsc calibration and overhead tool
 * Antonio Barbalace, Stevens 2019
 */

#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <sys/mman.h>

#define handle_error(msg) \
  do { perror(msg); exit(EXIT_FAILURE); } while (0)

static char *buffer;
static int counter = 0;

static inline unsigned long rdtsc(void)
{
	unsigned long low, high;

	asm volatile("rdtsc" : "=a" (low), "=d" (high));

	return ((low) | (high) << 32);
}

static inline unsigned long rdtsc_ordered(void)
{
#if 1
	asm volatile("mfence" : : : "memory");
#else
	asm volatile("lfence" : : : "memory");
#endif	
	return rdtsc();
}
	
#define REPETITIONS 1024
#define LOOP 1000000

// TODO perf turbostat.c in linux for x86 don't know for arm/crystal
// TODO cat /proc/cpuinfo bogomips entry

double tsc_calibrate(int verbose, int ordered) {
	unsigned long int start, stop;
	struct timespec st_start, st_stop;
	double d_stop, skew, total = 0.0;
	int i, rep;
   
	for (rep=0; rep < REPETITIONS; rep++) {
		
		start = ordered ? rdtsc_ordered() : rdtsc();
		clock_gettime(CLOCK_REALTIME, &st_start);
		for (i=0; i<LOOP; i++) {
			asm volatile ("" ::: "memory");
		}
		stop = ordered ? rdtsc_ordered() : rdtsc();
		clock_gettime(CLOCK_REALTIME, &st_stop);

		stop -= start; // time
		d_stop = (st_stop.tv_sec - st_start.tv_sec) +
				(st_stop.tv_nsec - st_start.tv_nsec)*0.000000001;
		skew = (double)stop / d_stop;
		if (verbose)
			printf ("%lf\n", skew);
		total += skew;
	}
    
	return (total/REPETITIONS);
}

/*
 * This is max overhead TODO average
 */
static unsigned long tsc_overhead(int ordered)
{
	unsigned long t0, t1, overhead = ~0UL;
	int i;

	for (i = 0; i < REPETITIONS; i++) {
		t0 = ordered ? rdtsc_ordered() : rdtsc();
		asm volatile("");
		t1 = ordered ? rdtsc_ordered() : rdtsc();
		if (t1 - t0 < overhead)
			overhead = t1 - t0;
	}

	return overhead;
}

#ifndef ORDERED
#ifdef TSC_ORDERED
#define ORDERED	1
#else
#define ORDERED	0
#endif
#endif

int main(int argc, char *argv[])
{
	double val = 0.0;
	unsigned long max_overhead = 0;
	
	val = tsc_calibrate(1, ORDERED);
	printf ("calibrate: %lf tick/second\n", val);

	max_overhead = tsc_overhead(ORDERED);
	printf ("overhead: %ld tick\n", max_overhead);
	
	printf("mode: tsc %s\n", ORDERED ? "ordered" : "not ordered");
	return 0;
}
