
/// Antonio Barbalace, Stevens 2019

#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>

//TODO fix the following
#define BITS_PER_LONG 64

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

typedef struct thread_data {
	int cpu;
	void* data;
	void* fun; //one entry must point to what to do ... for shared memory there is no problem but for messaging, one have to be the server
	int* stop;
	
	unsigned long long* shared_variable; /// should I keep the value per thread?
	
} thread_data_t;



void test()  {
	//here I need to do set_affinity
	//put myself on a barrier (how to implement this?)
	//big while cycle to enter the critical section
	//do variable++
	//exit critical section
	// ??? do check exit condition?
}

int main(int argc, char **argv)
{
	thread_data_t *data;
	pthread_t *threads;
	pthread_attr_t attr;
	//barrier_t barrier;
    struct timespec timeout;
	int ret, i, l, cpus=0, current;
	
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
	
    if ((data = (thread_data_t *)malloc(cpus * sizeof(thread_data_t))) == NULL) {
        perror("malloc");
        exit(1);
    }
    if ((threads = (pthread_t *)malloc(cpus * sizeof(pthread_t))) == NULL) {
        perror("malloc");
        exit(1);
    }
    cur=0;

	for (i = 0; i<sizeof(set)/sizeof(unsigned long); i++)
		for (l = 0; l<(sizeof(set)*8); l++) {
			if ( ((unsigned long*)&set)[i] & 0x1 ) {
				// need to load a thread on this CPU id
				int cpu = (i*sizeof(set))+l;
				printf("%d ", cpu);
				
				// check if we are doing something wrong, if not, start thread
				if (cur > cpus) {
					printf("error: cur %d cpus %d\n", cur, (int)cpus);
					return 0;
				}
				data[cur].cpu = cpu;
				data[cur].data = 0;
				data[cur].func = 0;
				
				ret = pthread_create(&threads[i], &attr, test, (void *)(&data[i])
				if (ret)
					//check for pthread_create error
					
				
				
				cur++;
			}
			((unsigned long*)&set)[i] >>= 1;
		}
	printf("\n");
	
	timer
	release the barrier
	
	return 0;
	


#ifdef PRINT_OUTPUT
    printf("Initializing locks\n");
#endif
    the_locks = init_lock_array_global(num_locks, num_threads);

    /* Access set from all threads */
    barrier_init(&barrier, num_threads + 1);
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    for (i = 0; i < num_threads; i++) {
#ifdef PRINT_OUTPUT
        printf("Creating thread %d\n", i);
#endif
        data[i].id = i;
        data[i].num_acquires = 0;
#if defined(DETAILED_LATENCIES)
	data[i].acq_time = 0;
	data[i].rls_time = 0;
#endif
        data[i].total_time = 0;

        data[i].barrier = &barrier;
        if (pthread_create(&threads[i], &attr, test, (void *)(&data[i])) != 0) {
            fprintf(stderr, "Error creating thread\n");
            exit(1);
        }
    }
    pthread_attr_destroy(&attr);
	
	    /* Catch some signals */
    if (signal(SIGHUP, catcher) == SIG_ERR ||
            signal(SIGINT, catcher) == SIG_ERR ||
            signal(SIGTERM, catcher) == SIG_ERR) {
        perror("signal");
        exit(1);
    }
	#ifdef PRINT_OUTPUT
    printf("STARTING...\n");
#endif
	
    /* Start threads */
    barrier_cross(&barrier);
	
	

    gettimeofday(&start, NULL);
    if (duration > 0) {
        nanosleep(&timeout, NULL);
    } else {
        sigemptyset(&block_set);
        sigsuspend(&block_set);
    }
    stop = 1;
    gettimeofday(&end, NULL);
	
	    stop = 1;
    gettimeofday(&end, NULL);

#ifdef PRINT_OUTPUT
    printf("STOPPING...\n");
#endif

    /* Wait for thread completion */
    for (i = 0; i < num_threads; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            fprintf(stderr, "Error waiting for thread completion\n");
            exit(1);
        }
    }
#endif    
    
    
    free(threads);
   // free(data);

    return 0;
}
