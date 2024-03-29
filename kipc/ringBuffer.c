
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/smp.h>
#include <linux/syscalls.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>

//for module compilationnn
#include <linux/module.h>

#include <asm/msr.h>
#include <asm/apic.h>
#include <asm/hardirq.h>
#include <asm/setup.h>
#include <asm/bootparam.h>
#include <asm/errno.h>
#include <asm/atomic.h>

#include <linux/delay.h>


//#include <linux/pcn_kmsg.h
#include "pcn_kmsg.h"

#include "kmsg_core.h"
#include "atomic_x86.h"
#include "ringBuffer.h"

#ifndef native_read_tsc
#define native_read_tsc rdtsc_ordered
#endif

int who_is_writing=-1;

/*****************************************************************************/
/* WINDOWS/BUFFERING */
/*****************************************************************************/

/*static inline unsigned long win_inuse(struct pcn_kmsg_window *win)
{
	return win->head - win->tail;
}
*/ //in the .h file
static inline void win_advance_tail(struct pcn_kmsg_window *win)
{
	win->tail++;
}

/*
 * this implementation was done like this in order to make fair the decision of who will insert the next message
 * this is why win_put insert only a single message at the time
 */
static inline
int __win_put(struct pcn_kmsg_window *win,
			  struct pcn_kmsg_message *msg,
			  int no_block,
			  unsigned long * time)
{
	unsigned long ticket;
  	unsigned long long sleep_start;

	/* if we can't block and the queue is already really long,
	   return EAGAIN */
	if (no_block && (win_inuse(win) >= RB_SIZE)) {
		KMSG_PRINTK("window full, caller should try again...\n");
		return -EAGAIN;
	}

	/* grab ticket */ // TODO grab a bunch of tickets instead of just one
	ticket = fetch_and_add(&win->head, 1);
	if(ticket >= ULONG_MAX)
		printk(KERN_ALERT"ERROR threashold ticket reached\n");

	/*PCN_DEBUG(KERN_ERR "%s: ticket = %lu, head = %lu, tail = %lu\n",
		 __func__, ticket, win->head, win->tail);*/
	KMSG_PRINTK("%s: ticket = %lu, head = %lu, tail = %lu\n",
			 __func__, ticket, win->head, win->tail);

	who_is_writing= ticket;
	/* spin until there's a spot free for me */
	//while (win_inuse(win) >= RB_SIZE) {}
	//if(ticket>=PCN_KMSG_RBUF_SIZE){
    sleep_start = native_read_tsc();
		while((win->buffer[ticket%PCN_KMSG_RBUF_SIZE].last_ticket != ticket-PCN_KMSG_RBUF_SIZE)) {
			//pcn_cpu_relax();
			//msleep(1);
			// TODO add the check if the remote kernel is dead
		}
		while(	win->buffer[ticket%PCN_KMSG_RBUF_SIZE].ready!=0){
			//pcn_cpu_relax();
			//msleep(1);
			// TODO add the check if the remote kernel is dead
		}
	sleep_start = native_read_tsc() - sleep_start;
	total_sleep_win_put += sleep_start;
    sleep_win_put_count++;
	//}
	/* insert item */
	memcpy((void*)&win->buffer[ticket%PCN_KMSG_RBUF_SIZE].payload,
	       &msg->payload, PCN_KMSG_PAYLOAD_SIZE);

	memcpy((void*)&(win->buffer[ticket%PCN_KMSG_RBUF_SIZE].hdr),
	       (void*)&(msg->hdr), sizeof(struct pcn_kmsg_hdr));

	//log_send[log_s_index%LOGLEN]= win->buffer[ticket & RB_MASK].hdr;
	memcpy(&(log_send[log_s_index%LOGLEN]),
		(void*)&(win->buffer[ticket%PCN_KMSG_RBUF_SIZE].hdr),
		sizeof(struct pcn_kmsg_hdr));
	log_s_index++;

	win->second_buffer[ticket%PCN_KMSG_RBUF_SIZE]++;

	/* set completed flag */
	win->buffer[ticket%PCN_KMSG_RBUF_SIZE].ready = 1;
	wmb();
	win->buffer[ticket%PCN_KMSG_RBUF_SIZE].last_ticket = ticket;

	who_is_writing=-1;

	if (time)
		*time = sleep_start;
msg_put++;
	return 0;
}

int win_put(struct pcn_kmsg_window *win,
		  struct pcn_kmsg_message *msg,
		  int no_block)
{
	return __win_put(win, msg, no_block, 0);
}

int win_put_timed(struct pcn_kmsg_window *win,
		  struct pcn_kmsg_message *msg,
		  int no_block,
		  unsigned long *time)
{
	return __win_put(win, msg, no_block, time);
}

// TODO
// 1. give me the error asap - or fault if the other end goes down during this
// 3. interrupt, receiving by multiple cores - workqueues
// 3.a follows the scheduler (at least try)
// 3.b round-robin
// 4. multiple channels - high prio, low prio

static inline
int __win_get(struct pcn_kmsg_window *win,
			struct pcn_kmsg_reverse_message **msg,
			int force,
			unsigned long* timeout)
{
	struct pcn_kmsg_reverse_message *rcvd;
	unsigned long long sleep_start;
	unsigned long sleep_timeout = (timeout ? *timeout : 0);
	int ready =1;

	if (!win_inuse(win)) {
		KMSG_PRINTK("nothing in buffer, returning...\n");
		return -1;
	}

	KMSG_PRINTK("reached win_get, head %lu, tail %lu\n",
		    win->head, win->tail);

	/* spin until entry.ready at end of cache line is set */
	rcvd =(struct pcn_kmsg_reverse_message*) &(win->buffer[win->tail % PCN_KMSG_RBUF_SIZE]);
	//KMSG_PRINTK("%s: Ready bit: %u\n", __func__, rcvd->hdr.ready);
    sleep_start = native_read_tsc();
	while (!rcvd->ready) {
		//pcn_cpu_relax();
		//msleep(1);

		if ((unsigned long)(native_read_tsc() - sleep_start) > sleep_timeout) {
			ready =0;
			if (force)
				break;     // continue handling the message even if it is an error
			else
				return -2; //TODO timeout error, set the right error code
		}
	}
    total_sleep_win_get += native_read_tsc() - sleep_start;
    sleep_win_get_count++;
if (timeout)
	*timeout -= (unsigned long) total_sleep_win_get;

	// barrier here?
	pcn_barrier();

if (force && !ready) {
	if (rcvd->ready)
		ready=1;
}
	
	//log_receive[log_r_index%LOGLEN]=rcvd->hdr;
	memcpy((void*)&(log_receive[log_r_index%LOGLEN]),&(rcvd->hdr),sizeof(struct pcn_kmsg_hdr));
	log_r_index++;
	//rcvd->hdr.ready = 0;

if (ready) {
	*msg = rcvd;
msg_get++;
	return 0;
}
else
	return -3;
}

//TODO migrate this in the header file as static inline and convert the main function as exported function
//static inline 
int win_get(struct pcn_kmsg_window *win,
                          struct pcn_kmsg_reverse_message **msg)
{
  return __win_get(win, msg, 0, 0);
}
int win_get_forced(struct pcn_kmsg_window *win,
                          struct pcn_kmsg_reverse_message **msg)
{
  return __win_get(win, msg, 1, 0);
}
int win_get_common(struct pcn_kmsg_window *win,
                        struct pcn_kmsg_reverse_message **msg,
			int force,
			unsigned long * timeout)
{
  return __win_get(win, msg, force, timeout);
}


int win_init (void)
{
	int bug=0;

// TODO move the following in the initialization specific code
//antoniob these are controls that should be done a compile time and are dependent to the low level messaging used ...
	if ( __PCN_KMSG_TYPE_MAX > ((1<<8) -1) ) {
		printk(KERN_ALERT"%s: __PCN_KMSG_TYPE_MAX=%ld too big.\n", // this check goes here because is related to the transport: this transport doesn't support more than this number of messages types
			__func__, (unsigned long) __PCN_KMSG_TYPE_MAX);
		bug++;
	}
// TODO I have no idea of what the following does mean to be, seems like a magic formula
	if ( (((sizeof(struct pcn_kmsg_hdr)*8) - 24 - sizeof(unsigned long) - __READY_SIZE) != LG_SEQNUM_SIZE) ) {
		printk(KERN_ALERT"%s: LG_SEQNUM_SIZE=%ld is not correctly sized, should be %ld.\n",
			__func__, (unsigned long) LG_SEQNUM_SIZE,
			(unsigned long)((sizeof(struct pcn_kmsg_hdr)*8) - 24 - sizeof(unsigned long) - __READY_SIZE));
		bug++;
	}
	
	if ( (sizeof(struct pcn_kmsg_message) % CACHE_LINE_SIZE != 0) ) {
		printk(KERN_ALERT"%s: sizeof(struct pcn_kmsg_message)=%ld is not a multiple of cacheline size.\n",
			__func__, (unsigned long)sizeof(struct pcn_kmsg_message));
		bug++;
	}
	if ( (sizeof(struct pcn_kmsg_reverse_message) % CACHE_LINE_SIZE != 0) ) {
		printk(KERN_ALERT"%s: sizeof(struct pcn_kmsg_reverse_message)=%ld is not a multiple of cacheline size.\n",
			__func__, (unsigned long)sizeof(struct pcn_kmsg_reverse_message));
		bug++;
	}
	BUG_ON((bug>1));
	return bug;
}

// move in ringbuffer TODO TODO TODO
int pcn_kmsg_window_init(struct pcn_kmsg_window *window)
{
	int i;

	window->head = 0;
	window->tail = 0;
	//memset(&window->buffer, 0,
	     //  PCN_KMSG_RBUF_SIZE * sizeof(struct pcn_kmsg_reverse_message));
	for(i=0;i<PCN_KMSG_RBUF_SIZE;i++){
		window->buffer[i].last_ticket=i-PCN_KMSG_RBUF_SIZE;
		window->buffer[i].ready=0;
	}
	memset((void*)&window->second_buffer, 0,
		       PCN_KMSG_RBUF_SIZE * sizeof(int));

	window->int_enabled = 1;
	return 0;
}
