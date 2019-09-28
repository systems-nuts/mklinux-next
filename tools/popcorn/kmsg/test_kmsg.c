
/*
 * Current version Antonio Barbalace, Stevens 2019
 * Originally from Phil Wilshire and Ben Shelton, VT 2013
 */

/* test_kmsg.c is used to test inter-kernel messaging */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <stdint.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#ifdef POPCORN_SYSCALL
#define __NR_popcorn_test_kmsg 314
#endif

// TODO not sure about the following (also mcast was never implemented)
typedef unsigned long pcn_kmsg_mcast_id;

// TODO refactoring is needed from here down -- the enum should be given by the kernel not here
enum pcn_kmsg_test_op {
	PCN_KMSG_TEST_SEND_SINGLE,
	PCN_KMSG_TEST_SEND_PINGPONG,
	PCN_KMSG_TEST_SEND_BATCH,
	PCN_KMSG_TEST_SEND_BATCH_RESULT,
	PCN_KMSG_TEST_SEND_LONG,
	PCN_KMSG_TEST_OP_MCAST_OPEN,
	PCN_KMSG_TEST_OP_MCAST_SEND,
	PCN_KMSG_TEST_OP_MCAST_CLOSE,
	
// the following were added by Phil, cannot find the equivalent kernel code at the moment	
/*	PCN_KMSG_TEST_CPU_WAIT,
	PCN_KMSG_TRIGGER_CPU_WAIT_IDLE,
	PCN_KMSG_TRIGGER_CPU_WAIT_SCHED,
	PCN_KMSG_TRIGGER_CPU_WAIT_SCHED_IDLE,
	PCN_KMSG_TRIGGER_CPU_WAIT */
};

struct pcn_kmsg_teb nmst_args {
	int cpu;
	/*int use_thread;
	int wg_val; 
	int g_val;*/ // TODO these were probably added by Phil
	unsigned long mask;
	unsigned long batch_size;
	pcn_kmsg_mcast_id mcast_id;
	unsigned long send_ts;
	unsigned long ts0;
	unsigned long ts1;
	unsigned long ts2;
	unsigned long ts3;
	unsigned long ts4;
	unsigned long ts5;
	unsigned long rtt;
};

// TODO refactoring is needed
///////////////////////////////////////////////////////////////////////////////

void print_usage(void)
{
	fprintf(stderr, 
		"Usage: test_kmsg [-c cpu] [-t test_op] [-b batch_size]\n"
		"[-n num_tests] [-i mcast_id] [-g group value] [-w wait for ack]\n");
		fprintf(stderr," Test Ops :- \n");
		
	fprintf(stderr,"PCN_KMSG_TEST_SEND_SINGLE %d \n",
		PCN_KMSG_TEST_SEND_SINGLE);
	fprintf(stderr,"PCN_KMSG_TEST_SEND_PINGPONG %d \n",
		PCN_KMSG_TEST_SEND_PINGPONG);
	fprintf(stderr,"PCN_KMSG_TEST_SEND_BATCH %d \n",
		PCN_KMSG_TEST_SEND_BATCH);
	fprintf(stderr,"PCN_KMSG_TEST_SEND_BATCH_RESULT %d \n",	
		PCN_KMSG_TEST_SEND_BATCH_RESULT);
	fprintf(stderr,"PCN_KMSG_TEST_SEND_LONG %d \n",	
		PCN_KMSG_TEST_SEND_LONG);
	fprintf(stderr,"PCN_KMSG_TEST_OP_MCAST_OPEN %d \n",
		PCN_KMSG_TEST_OP_MCAST_OPEN);
	fprintf(stderr,"PCN_KMSG_TEST_OP_MCAST_SEND %d \n",
		PCN_KMSG_TEST_OP_MCAST_SEND);
	fprintf(stderr,"PCN_KMSG_TEST_OP_MCAST_CLOSE %d \n",
		PCN_KMSG_TEST_OP_MCAST_CLOSE);
/*	
	fprintf(stderr,"PCN_KMSG_TEST_CPU_WAIT %d \n",
		PCN_KMSG_TEST_CPU_WAIT);
	fprintf(stderr,"PCN_KMSG_TRIGGER_CPU_WAIT_IDLE %d \n",
		PCN_KMSG_TRIGGER_CPU_WAIT_IDLE);
	fprintf(stderr,"PCN_KMSG_TRIGGER_CPU_WAIT_SCHED %d \n",
		PCN_KMSG_TRIGGER_CPU_WAIT_SCHED);
	fprintf(stderr,"PCN_KMSG_TRIGGER_CPU_WAIT_SCHED_IDLE %d \n",
		PCN_KMSG_TRIGGER_CPU_WAIT_SCHED_IDLE);
	fprintf(stderr,"PCN_KMSG_TRIGGER_CPU_WAIT %d \n",
		PCN_KMSG_TRIGGER_CPU_WAIT);
		*/
}

struct pcn_kmsg_test_args mtest_args[16];

int print_res3(struct pcn_kmsg_test_args *targsp) 
{
        int i;
	int g_val = targsp->g_val;
	for ( i = 0 ; i < g_val; i++) {
	        printf("%lu %u %u %u %u %lu\n",
		       targsp->send_ts,
		       (int)targsp->ts0,
		       (int)targsp->ts1,
		       (int)targsp->ts2,
		       (int)targsp->ts3,  // mid time
		       targsp->rtt
		       );
		targsp++;
	}
	return 0;
}

int print_res2(struct pcn_kmsg_test_args *targsp, int g_val) 
{
        int i;
	for ( i = 0 ; i < g_val; i++) {
    
      	        printf("%lu %lu %lu %lu %lu %lu\n", 
		       targsp->send_ts,
		       (int)targsp->ts0-targsp->send_ts,
		       (int)targsp->ts1-targsp->send_ts,
		       (int)targsp->ts3-targsp->send_ts,
		       (int)targsp->ts2-targsp->send_ts,  // mid time
		       targsp->rtt-targsp->send_ts
		       );
		targsp++;
	}
	return 0;
}
    

int print_res(struct pcn_kmsg_test_args *targsp) 
{
        int i;
	int g_val = targsp->g_val;
	for ( i = 0 ; i < g_val; i++) {
	  
	        printf("%010lu %02d %02d %08x %u %lu\n",
		       targsp->send_ts,
		       (int)targsp->ts0,
		       (int)targsp->ts1,
		       (int)targsp->ts3,
		       (int)targsp->ts2,  // mid time
		       targsp->rtt
		       
		       );
		targsp++;
	}
	return 0;
}

///////////////////////////////////////////////////////////////////////////////
// main function
///////////////////////////////////////////////////////////////////////////////

int main(int argc,  char *argv[]) 
{
	int opt, i, j;
	int test_op, rc;
	int g_val;
	int wg_val;
	int num_res;
	int fd;

	struct pcn_kmsg_test_args *targsp=&mtest_args[0];
	unsigned long num_tests = 1;

	targsp->cpu = -1;
	targsp->use_thread = 0;
	targsp->mask = 0;
	targsp->mcast_id = -1;
	targsp->batch_size = 1;
	//targsp->g_val = 1;

	test_op = -1;
	g_val = 1;
	wg_val = 0;

	while ((opt = getopt(argc, argv, "hc:t:b:n:m:i:uwg:")) != -1) {
		switch (opt) {
			case 'h':
				print_usage();
				break;
			case 'c':
				targsp->cpu = atoi(optarg);
				break;
			case 't':
				test_op = atoi(optarg);
				break;
			case 'w':
				printf("not supported\n"); exit(1);
				
				wg_val = 1;
				break;
			case 'g':
				printf("not supported\n"); exit(1);
				
				g_val = atoi(optarg);  // limited to 15
				if (g_val > 15) g_val=15;
				break;
			case 'b':
				targsp->batch_size = atoi(optarg);
				break;
			case 'n':
				num_tests = atoi(optarg);
				break;
			case 'm':
				targsp->mask = strtoul(optarg, NULL, 0);
				break;
			case 'i':
				targsp->mcast_id = atoi(optarg);
				break;
			case 'u':
				printf("not supported\n"); exit(1);
				
				//targsp->use_thread = 1; // TODO put it back later eventually
				break;
			default:
				print_usage();
				exit(EXIT_FAILURE);
		}
	}

	if (targsp->cpu == -1) {
		fprintf(stderr, "Failed to specify CPU!\n");
		print_usage();
		exit(EXIT_FAILURE);
	}

	if (test_op == -1) {
		fprintf(stderr, "Failed to specify test operation!\n");
		print_usage();
		exit(EXIT_FAILURE);
	}

	if ((test_op == PCN_KMSG_TEST_OP_MCAST_OPEN) && !targsp->mask) {
		fprintf(stderr, "Failed to specify mask for mcast open!\n");
		print_usage();
		exit(EXIT_FAILURE);
	}

	if ((test_op == PCN_KMSG_TEST_OP_MCAST_SEND) && (targsp->mask == -1)) {
		fprintf(stderr, "Failed to specify group id for mcast send!\n");
		print_usage();
		exit(EXIT_FAILURE);
	}

	//printf("pcn_kmsg test syscall, cpu %d, test_op %d...\n", 
	//       test_args.cpu, test_op);

	if (test_op == PCN_KMSG_TEST_SEND_PINGPONG) {
	  //printf("send,IPI,isr1,isr2,bh,bh2,handler,roundtrip\n");
	}

	if ((test_op == PCN_KMSG_TEST_SEND_BATCH) || 
	    (test_op == PCN_KMSG_TEST_SEND_LONG)) {
		printf("send,sendreturn,bh,bh2,lasthandler,roundtrip\n");
	}

/* TODO Phil integration
	if(test_op == PCN_KMSG_TRIGGER_CPU_WAIT) {
	  //printf("Spin START,targ_cpu,rep_cpu,rep_val,send_val,rtt\n");
	}
	if(test_op == PCN_KMSG_TRIGGER_CPU_WAIT_IDLE) {
	  printf("Idle START,targ_cpu,rep_cpu,send_val,rtt-mid, rtt\n");
	}
	if(test_op == PCN_KMSG_TRIGGER_CPU_WAIT_SCHED) {
	  printf("Sched START,targ_cpu,rep_cpu,rep_val,send_val,rtt\n");
	}
	if(test_op == PCN_KMSG_TRIGGER_CPU_WAIT_SCHED_IDLE) {
	  printf("Sched_Idle,targ_cpu,rep_cpu,rep_val,send_val,rtt\n");
	}
*/

#ifndef POPCORN_SYSCALL
    fd = open("/dev/kmsg_test", O_RDWR);
    if (fd == -1) {
        perror("cannot open /dev/kmsg_test");
        return 2;
    }
#endif

///////////////////////////////////////////////////////////////////////////////
// main test loop (can do multiple tests one after the other)
///////////////////////////////////////////////////////////////////////////////
	// kernel will send all messages at once when wg_val == 1
	for (i = 0; i < num_tests; i++) {
		// Update group number
		// data from 0 used for the whole group 
		targsp=&mtest_args[0];  // use one of the test args buffers

		// copy default data
		//targsp->cpu = test_args.cpu;
		//targsp->use_thread = test_args.use_thread;
		//targsp->mask = test_args.mask;
		//targsp->mcast_id=test_args.mcast_id;
		//targsp->batch_size =test_args.batch_size;
		//targsp->g_val = g_val;
		//targsp->wg_val = wg_val;

		//if(wg_val == 0) {
		//	wg_val = g_val;
		//	if (g_val > 0 ) {
		//	if (( num_tests - i) < g_val) {
		//		g_val = num_tests-i;
		//		targsp->g_val = g_val;
		//		wg_val = num_tests - i -1;
		//		}
		//	}
		//}

#ifdef POPCORN_SYSCALL
		if ((num_tests > 1 ) &&
			(targsp->use_thread == 0) &&
			(test_op == PCN_KMSG_TRIGGER_CPU_WAIT)
		   ){
			rc = syscall(__NR_popcorn_test_kmsg, PCN_KMSG_TEST_CPU_WAIT, targsp);
		}

		rc = syscall(__NR_popcorn_test_kmsg, test_op, targsp);
		if (rc) {
			printf("ERROR: Syscall returned %d\n", rc);
		}
#else
    if (ioctl(fd, test_op, targsp) == -1) {
        perror("ioctl error");
    }
#endif

		switch (test_op) {
/*			
			case PCN_KMSG_TRIGGER_CPU_WAIT:
				print_res(targsp);
			break;
			case PCN_KMSG_TRIGGER_CPU_WAIT_IDLE:
			case PCN_KMSG_TRIGGER_CPU_WAIT_SCHED:		
			case PCN_KMSG_TRIGGER_CPU_WAIT_SCHED_IDLE:
				print_res(targsp);
				break;
*/
			case PCN_KMSG_TEST_SEND_SINGLE:
				printf("Single ticks: sender %lu\n",
					targsp->send_ts);
				break;
			case PCN_KMSG_TEST_SEND_PINGPONG:
				num_res=g_val;
				if (num_res==0) num_res = 1;

				for (j = 0; j < num_res; j++ ) {
					targsp=&mtest_args[j]; 
					printf("%lu %lu %lu %lu %lu %lu\n", 
						targsp->send_ts,
						(int)targsp->ts0-targsp->send_ts,
						(int)targsp->ts1-targsp->send_ts,
						(int)targsp->ts3-targsp->send_ts,
						(int)targsp->ts2-targsp->send_ts,
						targsp->rtt-targsp->send_ts
					);
				}
			  break;
			case PCN_KMSG_TEST_SEND_LONG:
			case PCN_KMSG_TEST_SEND_BATCH:
				print_res3(targsp);
				break;

			case PCN_KMSG_TEST_OP_MCAST_OPEN:
				printf("Opened mcast group, ID %lu\n",
				       targsp->mcast_id);
				break;
			case PCN_KMSG_TEST_OP_MCAST_SEND:
				printf("Sent message to mcast group %lu, sender %lu, receiver %lu\n",
				       targsp->mcast_id, targsp->send_ts, targsp->ts0);
				break;
		}
		usleep(25000);
	}
#ifndef POPCORN_SYSCALL
    close (fd);
#endif

	exit(EXIT_SUCCESS);
}
