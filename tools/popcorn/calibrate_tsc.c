/* 
 * Antonio Barbalace 2018
 * 
 * Microbenchmark for memory management in user space
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

unsigned long int rdtsc(void)
{
	unsigned int low, high;

	asm volatile("rdtsc" : "=a" (low), "=d" (high));

	return low | ((unsigned long int)high) << 32;
}

#define REPETITIONS 1024
#define LOOP 1000000

// TODO perf turbostat.c in linux for x86 don't know for arm/crystal
// TODO cat /proc/cpuinfo bogomips entry

double calibrate(void) {
    unsigned long int start, stop;
    struct timespec st_start, st_stop;
    double d_stop, skew, total = 0.0;
    int i, rep;
    
    for (rep=0; rep < REPETITIONS; rep++) {
        
        start = rdtsc();
        clock_gettime(CLOCK_REALTIME, &st_start);
        for (i=0; i<LOOP; i++) {
            asm volatile ("" ::: "memory");
        }
	stop =  rdtsc();
        clock_gettime(CLOCK_REALTIME, &st_stop);
//        stop = rdtsc();
        
        stop -= start; // time
        d_stop = (st_stop.tv_sec - st_start.tv_sec) +
                (st_stop.tv_nsec - st_start.tv_nsec)*0.000000001;
        skew = (double)stop / d_stop;
printf ("%lf\n", skew);
        total += skew;
    }
    
    return (total/REPETITIONS);
}

double val = 0.0;

#define TOTAL_ACCESSES 32
static unsigned long int times[TOTAL_ACCESSES*3];
static unsigned int flags[TOTAL_ACCESSES*3];


// TODO shouldn't migrate -- do a script taskset bash command (it is faster)


// current idea: allocate a big area with mmap; divide it in 4 parts:
// present but pre-faulted
// present not pre-faulted
// present but protected
// not present (unmapped)

// do it with signal, do it with userfaultfd (there is a demo in man pages)

static void
handler(int sig, siginfo_t *si, void *unused)
{
           /* Note: calling printf() from a signal handler is not safe
              (and should not be done in production programs), since
              printf() is not async-signal-safe; see signal-safety(7).
              Nevertheless, we use printf() here as a simple way of
              showing that the handler was called. */

           if (counter > TOTAL_ACCESSES) {
               int i;
               for (i =0 ; i< TOTAL_ACCESSES *3; i++)
                   printf("time[%d]: %ld %d sec:%.9lf\n", i, times[i], flags[i], ((double)times[i]/val));
               
                exit(EXIT_FAILURE); 
           }

            /*char *code_errors[] = {"ERROR", "MAPERR", "ACCERR", "BNDERR", "PKUERR"};           
            printf("Got SIGSEGV at address: 0x%lx lower: 0x%lx upper: 0x%lx signo: %d errno: %d code: %d(%s) buffer: 0x%lx %lx\n",
                   (long) si->si_addr, (long) si->si_lower, (long) si->si_upper, si->si_signo, si->si_errno, si->si_code, code_errors[si->si_code], (long)buffer,  
                   ((unsigned long) si->si_addr - (unsigned long) buffer) >> 12); */
           //exit(EXIT_FAILURE); // original
flags[ ((unsigned long) si->si_addr - (unsigned long) buffer) >> 12 ] = si->si_code;
            if (si->si_code == 2) {        
                if (mprotect(si->si_addr, 4096, PROT_READ | PROT_WRITE)== -1) {
                    handle_error("mprotect handler");
                }
            } else if (si->si_code ==1) {
                if (mmap(si->si_addr, 4096, (PROT_READ | PROT_WRITE), (MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED), 0, 0) == -1) {
                    handle_error("mmap handler");
                }
            }
    
           counter++;
       }

       int
       main(int argc, char *argv[])
       {
           char *p;
           int pagesize;
           struct sigaction sa;
           int i; unsigned long int start;           


           //sleep(1);
          val = calibrate();
           printf ("calibrate : %lf\n", val);

return 0;



           for (i=0; i< TOTAL_ACCESSES*3; i++) {
               times[i]=0; flags[i]=0;
           }
           
           sa.sa_flags = SA_SIGINFO;
           sigemptyset(&sa.sa_mask);
           sa.sa_sigaction = handler;
           if (sigaction(SIGSEGV, &sa, NULL) == -1)
               handle_error("sigaction");

           pagesize = sysconf(_SC_PAGE_SIZE);
           if (pagesize == -1)
               handle_error("sysconf");

           /* Allocate a buffer aligned on a page boundary;
              initial protection is PROT_READ | PROT_WRITE */

           buffer = mmap(0, pagesize * 16, (PROT_READ | PROT_WRITE), (MAP_PRIVATE | MAP_ANONYMOUS), 0, 0);
           //buffer = memalign(pagesize, 4 * pagesize);
           if (buffer == -1)
               //handle_error("memalign");
               handle_error("mmap");

           printf("Start of region:        0x%lx-0x%lx\n", (long) buffer, (long) buffer + (16 * pagesize));

           //sleep(1);
          val = calibrate();
           printf ("calibrate: %lf tick/second\n", val);
           
           if (mprotect(buffer + pagesize * 8, 8 * pagesize,
                       PROT_READ) == -1)
               handle_error("mprotect");

           printf("0= map-in; 1= mmap; 2= mprotect; 3= pre-fault\n");
           
           p = buffer;
           // prefault
           for (i=0; i<4; i++) {
               *p = 'b';
               p += pagesize;
               flags[i] = 3;
           }

           i=0;
           for (p = buffer ; ; ) {
               start = rdtsc();
               *p = 'a';
               times[i++] = rdtsc() - start;
               p += pagesize;
           }

           
    printf("Loop completed\n");     /* Should never happen */
    exit(EXIT_SUCCESS);
}
