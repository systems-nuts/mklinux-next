
/// Antonio Barbalace, Stevens 2019

#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>
#include <sys/time.h>
#include <malloc.h>

//TODO fix the following
#define BITS_PER_LONG 64


#define TEST_LATENCY


// the following is in seconds
#define DEFAULT_TEST_TIME 10

static pthread_barrier_t barrier;

volatile int stop=0;

void catcher (int sig) {
	printf("signal %d\n", sig);
	stop = 1;
	// OR exit?
}

// copied from the Linux kernel tools/perf/util/hweight.c
unsigned long hweight64(unsigned long w)
{
#if BITS_PER_LONG == 32
	return hweight32((unsigned int)(w >> 32)) + hweight32((unsigned int)w);
#elif BITS_PER_LONG == 64
	unsigned long res = w - ((w >> 1) & 0x5555555555555555ul);
	res = (res & 0x3333333333333333ul) + ((res >> 2) & 0x3333333333333333ul);
	res = (res + (res >> 4)) & 0x0F0F0F0F0F0F0F0Ful;
	res = res + (res >> 8);
	res = res + (res >> 16);
	return (res + (res >> 32)) & 0x00000000000000FFul;
#endif
}

unsigned long rdtsc(){
    unsigned int lo,hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((unsigned long)hi << 32) | lo;
}

typedef struct thread_data {
	int cpu;
	int id;
	unsigned long count;
	//void* fun; //one entry must point to what to do ... for shared memory there is no problem but for messaging, one have to be the server
	pthread_t *thread;
	unsigned long * array;
	
	unsigned long * spinlock;
	unsigned long long* variable; 
	
} thread_data_t;

// TODO
// this code assumes we trust the library and we trust the operating system,
// but we know, OS is not real-time

#ifdef TEST_LATENCY
#define TIMESTAMP(a) (a= rdtsc())
#define MAX_ARRAY 1024
#else
#define TIMESTAMP(a)
#endif

#define MUTEX_LOCK(name) \
	while (!__sync_bool_compare_and_swap(name, 0, 1)); \
	__sync_synchronize();
#define MUTEX_UNLOCK(name) \
	__sync_synchronize(); \
	*name = 0;

void * test(void *args)
{
	int ret;
	unsigned long me=0;
	struct thread_data * tdata = (struct thread_data *) args;
	volatile unsigned long *spinlock = tdata->spinlock;
	unsigned long long *variable = tdata->variable;
#ifdef TEST_LATENCY
	unsigned long time1, time2;
	unsigned long *array = tdata->array;
#endif
	cpu_set_t set, get_set;
	CPU_ZERO(&set); CPU_ZERO(&get_set);
	
	printf("Thread %d going to cpu %d\n", tdata->id, tdata->cpu );
	
	//here I need to do set_affinity
	CPU_SET(tdata->cpu, &set);
	ret = pthread_setaffinity_np(*(tdata->thread), sizeof(set), &set);
	if (ret != 0)
		printf("ERROR pthread set affinity failed");
	
	// wait for the cpu affinity to stabilize
	do {
		ret = pthread_getaffinity_np(*(tdata->thread), sizeof(get_set), &get_set);
		if (ret != 0) {
			printf("ERROR pthread get affinity failed");
			break;
		}
	} while (CPU_EQUAL(&set, &get_set) == 0);
	
	//put thread on a barrier (how to implement this?)
	printf("going to barrier (cpu %d)\n", tdata->cpu);
	ret = pthread_barrier_wait(&barrier);
	if (!((ret == 0) || (ret == PTHREAD_BARRIER_SERIAL_THREAD))) {
		perror("ERROR pthread barrier wait");
		goto exit_thread;
	}
	
	//big while cycle to enter the critical section
//	printf("INTO THE LOOP cpu %d\n", tdata->cpu);
	
	while (!stop) {
		TIMESTAMP(time1);
		MUTEX_LOCK(spinlock);
		TIMESTAMP(time2);
		//actual work
		(*variable)++; // this will tell me the max number of acquisitions
		MUTEX_UNLOCK(spinlock);
		me++;
#ifdef TEST_LATENCY
		array[(me % MAX_ARRAY)]=time2-time1;
#endif
	}
	tdata->count = me;
	
//	printf("out THE LOOP cpu %d me %d\n", tdata->cpu, me);
	
exit_thread:
	return 0;
}











/*
 * To reduce code, this program spans one thread per CPU in the affinity mask.
 * The CPU with the smallest id will host main and thread zero (0).
 */
int main(int argc, char **argv)
{
	thread_data_t *threads_data;
	pthread_t *threads;
	pthread_attr_t attr;
    struct timespec timeout;
	struct timeval start, end;
	unsigned long duration;
	int ret, i, l, cpus=0, cur;
	sigset_t block_set;
	char* shared, *shared_var;
#ifdef TEST_LATENCY
	unsigned long * the_array;
#endif
	
	cpu_set_t set;
	CPU_ZERO(&set);

	ret = sched_getaffinity(getpid(), sizeof(set), &set);
	if (ret == -1) {
		perror("sched_getaffinity");
		return -1;
	}
	
	for (i = 0; i<sizeof(set)/sizeof(unsigned long); i++) {
		unsigned long tmp =((unsigned long*)&set)[i];
		printf("%00lx:", tmp);
		cpus += hweight64(tmp);
	}
	printf(" (total %ld)\n", cpus);
	
	if (cpus == 0) {
		perror("zero CPUs");
		exit(1);
	}
    if ((threads_data = (thread_data_t *)malloc(cpus * sizeof(thread_data_t))) == NULL) {
        perror("malloc");
        exit(1);
    }
    if ((threads = (pthread_t *)malloc(cpus * sizeof(pthread_t))) == NULL) {
        perror("malloc");
        exit(1);
    }
	
	//initialize barrier 
	ret = pthread_barrier_init(&barrier, NULL, cpus +1); // barrier_init(&barrier, num_threads + 1); 
    if (ret != 0) {
        perror("pthread_barrier_init error");
		exit(1);
	}

	// initialize test length (for now we set a default duration of the test
	duration = DEFAULT_TEST_TIME;
	timeout.tv_sec = duration;
    timeout.tv_nsec = 0; // because duration is in seconds at this time 
	
	
	// create shared lock on what to work on, includes the variable to increment
	shared = memalign(4096, 4096);
	if (shared == 0) {
		printf("memalign error\n");
		exit(1);
	}
	shared_var = shared +16;// NOTE now is same cacheline
#ifdef TEST_LATENCY
	//allocates memory for the array
	the_array = malloc(cpus*(MAX_ARRAY*sizeof(unsigned long)));
	if (the_array == 0) {
		printf("malloc error\n");
		exit(1);
	}		
#endif
	
	stop=0;
	
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	
	cur=0;
	for (i = 0; i<sizeof(set)/sizeof(unsigned long); i++) {
		for (l = 0; l<(sizeof(unsigned long)*8); l++) {
			if ( ((unsigned long*)&set)[i] & 0x1 ) {
				// need to load a thread on this CPU id
				int cpu = (i*sizeof(unsigned long)*8)+l;
				printf("%d ", cpu);
				
				// check if we are doing something wrong, if not, start thread
				if (cur > cpus) {
					printf("error: cur %d cpus %d\n", cur, (int)cpus);
					return 0;
				}
				threads_data[cur].id = cur;
				threads_data[cur].cpu = cpu;
				threads_data[cur].count = 0;
				//threads_data[cur].fun = 0;
				threads_data[cur].thread = &threads[cur];
#ifdef TEST_LATENCY
				threads_data[cur].array = &(the_array[cur*MAX_ARRAY]);
#endif
				
				//critical section
				threads_data[cur].spinlock = (unsigned long*) shared;
				threads_data[cur].variable = (unsigned long long*) shared_var; 
								
				ret = pthread_create(&threads[cur], &attr, test, (void *)(&threads_data[cur]));
				if (ret != 0) {
					fprintf(stderr, "Error creating thread for cpu %d (cur %d)\n", cpu, cur);
					exit(1);
				}
					
				cur++;
			}
			((unsigned long*)&set)[i] >>= 1;
		}
	}
    pthread_attr_destroy(&attr);

	printf("\n");
printf("cpu %d cur %d\n", cpus, cur);
	
	//timer ? 

	/* Catch some signals */
	if (signal(SIGHUP, catcher) == SIG_ERR ||
			signal(SIGINT, catcher) == SIG_ERR ||
			signal(SIGTERM, catcher) == SIG_ERR) {
        perror("error during signal registration");
        exit(1);
    }
#define PRINT_OUTPUT
#ifdef PRINT_OUTPUT
    printf("STARTING...\n");
#endif

    /* Start threads */
    pthread_barrier_wait(&barrier);
	
    gettimeofday(&start, NULL);
    if (duration > 0) {
        nanosleep(&timeout, NULL);
    }
    else { //run until ctrl+c, ctrl+...
        sigemptyset(&block_set);
        sigsuspend(&block_set);
    }
    stop = 1;
    gettimeofday(&end, NULL);

#ifdef PRINT_OUTPUT
    printf("STOPPING...\n");
#endif

    /* Wait for thread completion */
    for (i = 0; i < cpus; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            fprintf(stderr, "Error waiting for thread completion\n");
            exit(1);
        }
    }

	duration = (end.tv_sec * 1000 + end.tv_usec / 1000) - (start.tv_sec * 1000 + start.tv_usec / 1000);
	printf("Duration      : %d (ms)\n", duration);
	printf("Acquisitions  : %lld\n", *((unsigned long long*)shared_var));
#ifdef TEST_LATENCY
	for (i =0; i< (cpus*MAX_ARRAY); i++)
		printf("%ld\n", the_array[i]);
#endif

    free(threads_data);
    free(threads);

    return 0;
}
