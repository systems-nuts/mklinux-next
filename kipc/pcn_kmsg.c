/*
 * Inter-kernel messaging support for Popcorn
 *
 * Current ver: Antonio Barbalace, Phil Wilshire 2013
 * First ver: Ben Shelton <beshelto@vt.edu> 2013
 */

#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/smp.h>
#include <linux/syscalls.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>


//#include <linux/multikernel.h>
#include "multikernel.h"
//#include <linux/pcn_kmsg.h>
#include "pcn_kmsg.h"

//for module compilationnn
#include <linux/module.h>


#include <asm/msr.h> //#include <asm/system.h>
#include <asm/ipi.h>
#include <asm/apic.h>
#include <asm/hardirq.h>
#include <asm/setup.h>
#include <asm/bootparam.h>
#include <asm/errno.h>
#include <asm/atomic.h>

#include <linux/delay.h>
#include <linux/timer.h>

#include "atomic_x86.h"
#include "kmsg_core.h"
#include "ringBuffer.h"

MODULE_LICENSE("GPL");

/*****************************************************************************/
/* Logging Macros and variables */
/*****************************************************************************/

//#define LOGLEN 4
//#define LOGCALL 32


#define ROUND_PAGES(size) ((size/PAGE_SIZE) + ((size%PAGE_SIZE)? 1:0))
#define ROUND_PAGE_SIZE(size) (ROUND_PAGES(size)*PAGE_SIZE)

extern void (*popcorn_kmsg_interrupt_handler)(struct pt_regs *regs, unsigned long long timestamp);

struct pcn_kmsg_hdr log_receive[LOGLEN]; // this is to be moved to ringBuffer, but not sure it is the right place
struct pcn_kmsg_hdr log_send[LOGLEN]; // this is to be moved to ringBuffer
int log_r_index=0;
int log_s_index=0;

void * log_function_called[LOGCALL];
int log_f_index=0;
int log_f_sendindex=0;
void * log_function_send[LOGCALL];

/*****************************************************************************/
/* Statistics */
/*****************************************************************************/

unsigned long long total_sleep_win_put = 0;
unsigned int sleep_win_put_count = 0;
unsigned long long total_sleep_win_get = 0;
unsigned int sleep_win_get_count = 0;

long unsigned int msg_put=0;
long unsigned msg_get=0;

/*****************************************************************************/
/* COMMON STATE */
/*****************************************************************************/

/* table of callback functions for handling each message type */
pcn_kmsg_cbftn callback_table[PCN_KMSG_TYPE_MAX];

/* number of current kernel */
int my_cpu = 0; // NOT CORRECT FOR CLUSTERING!!! STILL WE HAVE TO DECIDE HOW TO IMPLEMENT CLUSTERING

/* set if the kernel is halting*/
int shut_down_kmsg = 0;

/* pointer to table with phys addresses for remote kernels' windows,
 * owned by kernel 0 */
struct pcn_kmsg_rkinfo *rkinfo;

/* table with virtual (mapped) addresses for remote kernels' windows,
   one per kernel */
struct pcn_kmsg_window * rkvirt[POPCORN_MAX_CPUS];
unsigned long rkvirt_timeout[POPCORN_MAX_CPUS];
unsigned long rkvirt_seq[POPCORN_MAX_CPUS];

/* lists of messages to be processed for each prio */
struct list_head msglist_hiprio, msglist_normprio;

/* array to hold pointers to large messages received */
//struct pcn_kmsg_container * lg_buf[POPCORN_MAX_CPUS];
struct list_head lg_buf[POPCORN_MAX_CPUS];
volatile unsigned long long_id;
//int who_is_writing=-1; //now in ringBuffer.c

/* action for bottom half */
static void pcn_kmsg_action(/*struct softirq_action *h*/struct work_struct* work);

/* workqueue for operations that can sleep */
struct workqueue_struct *kmsg_wq;
struct workqueue_struct *messaging_wq;

#ifdef POPCORN_KEEP_ALIVE
struct timer_list keepalive_tl;
#endif /* POPCORN_KEEP_ALIVE */

/*****************************************************************************/
/* WINDOWS/BUFFERING */
/*****************************************************************************/



/* win_enable_int
 * win_disable_int
 * win_int_enabled
 *
 * These functions will inhibit senders to send a message while
 * the receiver is processing IPI from any sender.
 */
static inline void win_enable_int(struct pcn_kmsg_window *win) { // this is arch indep
	        win->int_enabled = 1;
	        wmb(); // enforce ordering
}
static inline void win_disable_int(struct pcn_kmsg_window *win) { // this is arch indep
	        win->int_enabled = 0;
	        wmb(); // enforce ordering
}
static inline unsigned char win_int_enabled(struct pcn_kmsg_window *win) { // this is arch indep
    		rmb(); //not sure this is required (Antonio)
	        return win->int_enabled;
}

/* INITIALIZATION */

/*****************************************************************************/
/* checking procedures */
/*****************************************************************************/

#ifdef PCN_SUPPORT_MULTICAST
static int pcn_kmsg_mcast_callback(struct pcn_kmsg_message *message);
#endif /* PCN_SUPPORT_MULTICAST */

static void map_msg_win(pcn_kmsg_work_t *w)
{
	int cpu = w->cpu_to_add;

	if (cpu < 0 || cpu >= POPCORN_MAX_CPUS) {
		KMSG_ERR("invalid CPU %d specified!\n", cpu);
		return;
	}

	rkvirt[cpu] = ioremap_cache(rkinfo->phys_addr[cpu],
				  ROUND_PAGE_SIZE(sizeof(struct pcn_kmsg_window)));

	if (rkvirt[cpu]) {
		KMSG_INIT("ioremapped window, virt addr 0x%p\n", 
			  rkvirt[cpu]);
	} else {
		KMSG_ERR("failed to map CPU %d's window at phys addr 0x%lx\n",
			 cpu, rkinfo->phys_addr[cpu]);
	}
}

/* bottom half for workqueue */
static void process_kmsg_wq_item(struct work_struct * work)
{
	pcn_kmsg_work_t *w = (pcn_kmsg_work_t *) work;

	KMSG_PRINTK("called with op %d\n", w->op);

	switch (w->op) {
		case PCN_KMSG_WQ_OP_MAP_MSG_WIN:
			map_msg_win(w);
			break;

		case PCN_KMSG_WQ_OP_UNMAP_MSG_WIN:
			KMSG_ERR("%s: UNMAP_MSG_WIN not yet implemented!\n",
				 __func__);
			break;

#ifdef PCN_SUPPORT_MULTICAST
		case PCN_KMSG_WQ_OP_MAP_MCAST_WIN:
			map_mcast_win(w);
			break;

		case PCN_KMSG_WQ_OP_UNMAP_MCAST_WIN:
			KMSG_ERR("UNMAP_MCAST_WIN not yet implemented!\n");
			break;
#endif /* PCN_SUPPORT_MULTICAST */

		default:
			KMSG_ERR("Invalid work queue operation %d\n", w->op);

	}

	kfree(work);
}

//TODO maybe move in an include? do we want to show the internals?
void pcn_kmsg_free_msg(void * msg)
{
	kfree(msg - sizeof(struct list_head));
}
EXPORT_SYMBOL(pcn_kmsg_free_msg);

static int pcn_kmsg_checkin_callback(struct pcn_kmsg_message *message) 
{
	struct pcn_kmsg_checkin_message *msg = 
		(struct pcn_kmsg_checkin_message *) message;
	int from_cpu = msg->hdr.from_cpu;
	pcn_kmsg_work_t *kmsg_work = NULL;


	KMSG_INIT("From CPU %d, type %d, window phys addr 0x%lx\n", 
		  msg->hdr.from_cpu, msg->hdr.type, 
		  msg->window_phys_addr);

	if (from_cpu >= POPCORN_MAX_CPUS) {
		KMSG_ERR("Invalid source CPU %d\n", msg->hdr.from_cpu);
		return -1;
	}

	if (!msg->window_phys_addr) {

		KMSG_ERR("Window physical address from CPU %d is NULL!\n", 
			 from_cpu);
		return -1;
	}

	/* Note that we're not allowed to ioremap anything from a bottom half,
	   so we'll add it to a workqueue and do it in a kernel thread. */
	kmsg_work = kmalloc(sizeof(pcn_kmsg_work_t), GFP_ATOMIC);
	if (likely(kmsg_work)) {
		INIT_WORK((struct work_struct *) kmsg_work, 
			  process_kmsg_wq_item);
		kmsg_work->op = PCN_KMSG_WQ_OP_MAP_MSG_WIN;
		kmsg_work->from_cpu = msg->hdr.from_cpu;
		kmsg_work->cpu_to_add = msg->cpu_to_add;
		queue_work(kmsg_wq, (struct work_struct *) kmsg_work);
	} else {
		KMSG_ERR("Failed to malloc work structure!\n");
	}

	pcn_kmsg_free_msg(message);

	return 0;
}



//extern unsigned long orig_boot_params;

static int send_checkin_msg(unsigned int cpu_to_add, unsigned int to_cpu)
{
	int rc;
	struct pcn_kmsg_checkin_message msg;

	msg.hdr.type = PCN_KMSG_TYPE_CHECKIN;
	msg.hdr.prio = PCN_KMSG_PRIO_HIGH;
	msg.window_phys_addr = rkinfo->phys_addr[my_cpu];
	msg.cpu_to_add = cpu_to_add;

	memcpy(&(msg._cpumask), cpu_present_mask, sizeof(struct cpumask));

	rc = pcn_kmsg_send(to_cpu, (struct pcn_kmsg_message *) &msg);

	if (rc) {
		KMSG_ERR("Failed to send checkin message, rc = %d\n", rc);
		return rc;
	}

	return 0;
}

static int do_checkin(void)
{
	int rc = 0;
	int i;

	for (i = 0; i < POPCORN_MAX_CPUS; i++) {
		if (i == my_cpu) {
			continue;
		}

		if (rkinfo->phys_addr[i]) {
			rkvirt[i] = ioremap_cache(rkinfo->phys_addr[i],
						  ROUND_PAGE_SIZE(sizeof(struct pcn_kmsg_window)));

			if (rkvirt[i]) {
				KMSG_INIT("ioremapped CPU %d's window, virt addr 0x%p\n", 
					  i, rkvirt[i]);
			} else {
				KMSG_ERR("Failed to ioremap CPU %d's window at phys addr 0x%lx\n",
					 i, rkinfo->phys_addr[i]);
				return -1;
			}

			KMSG_INIT("Sending checkin message to kernel %d\n", i);			
			rc = send_checkin_msg(my_cpu, i);
			if (rc) {
				KMSG_ERR("POPCORN: Checkin failed for CPU %d!\n", i);
				return rc;
			}
		}
	}

	return rc;
}

#ifdef POPCORN_KEEP_ALIVE
///////////////////////////////////////////////////////////////////////////////
// keepalive
///////////////////////////////////////////////////////////////////////////////

static const unsigned long tdelay = (HZ * 500)/1000; // every 500ms

static int pcn_kmsg_keepalive_callback(struct pcn_kmsg_message *message)
{
	struct pcn_kmsg_keepalive_message *msg =
		(struct pcn_kmsg_keepalive_message *) message;
	int from_cpu = msg->hdr.from_cpu;
	int from_cpu1 = msg->sender;
	unsigned long seq = msg->sequence_num;

//	KMSG_INIT("From CPU %d, type %d, sender %d seq %ld\n",//TODO change debug level and uncomment
//		  msg->hdr.from_cpu, msg->hdr.type, from_cpu1, seq);

	if (from_cpu >= POPCORN_MAX_CPUS || from_cpu1 >= POPCORN_MAX_CPUS) {
		KMSG_ERR("Invalid source CPU %d %d\n", from_cpu, from_cpu1);
		return -1; // TODO is this correct, no release of the message?!
	}

	rkvirt_timeout[from_cpu1] = jiffies;
	rkvirt_seq[from_cpu1] = seq; // TODO maybe we can do some checks on this value

	pcn_kmsg_free_msg(message);

	return 0;
}

static unsigned long keepalive = 0;
static int send_keepalive_msg(unsigned int myself, unsigned int remote)
{
	int rc;
	struct pcn_kmsg_keepalive_message msg;

	msg.hdr.type = PCN_KMSG_TYPE_KEEPALIVE;
	msg.hdr.prio = PCN_KMSG_PRIO_HIGH;
	msg.sender = myself;
	msg.sequence_num = keepalive++;

	rc = pcn_kmsg_send(remote, (struct pcn_kmsg_message *) &msg);

	if (rc) {
		KMSG_ERR("Failed to send keepalive message, rc = %d\n", rc);
		return rc;
	}

	return 0;
}

// TODO at some time we should set a rate - initial idea is that there is a rate if any other kernel realizes
// that a kernel don't send any beacon for a time superior as twice the rate, we consider the kernel has failed
// for some reason
static int do_keepalive(void)
{
	int rc = 0;
	int i;

	for (i = 0; i < POPCORN_MAX_CPUS; i++) {
		if (i == my_cpu) {
			continue;
		}

		if (rkinfo->phys_addr[i]) {
			if (rkvirt[i]) {

//				KMSG_INIT("Sending checkin message to kernel %d\n", i); // TODO change debug level and uncomment
				rc = send_keepalive_msg(my_cpu, i);
				if (rc) {
					KMSG_ERR("POPCORN: Keepalive msg failed for CPU %d!\n", i);
					return rc; // OR continue?
				}
			}
		}
	}

	return rc;
}

extern struct workqueue_struct *crash_wq;
extern void process_crash_kernel_notification(struct work_struct *work);

#define VALIDITY_WIN 4
void keepalive_timer (unsigned long arg)
{
	int i;
	unsigned long now = jiffies;
	static int dead= 0;
	struct work_struct* work;
	
	do_keepalive();

	for (i=0; i<POPCORN_MAX_CPUS; i++) {
		if (i == my_cpu) {
			continue;
		}
		if(rkinfo->phys_addr[i])
			if (rkvirt_timeout[i] < (now - tdelay*VALIDITY_WIN)) {
				KMSG_ERR("POPCORN: Keepalive msg failed for CPU %d! Kernel is dead \n", i);
				KMSG_ERR("POPCORN: last beacon at %ld, now %ld, validity win %ld\n",
					rkvirt_timeout[i], now, (now -tdelay*2));
				if(system_state	== SYSTEM_RUNNING)
					dead++;
			}
		// TODO here we should create a notification mechanism for the upper layers ..
		// the messaging layer should be able to propagate itself when a connection drops
	}

	if(dead==1){
		printk("adding work\n");
	        work= kmalloc(sizeof(*work), GFP_ATOMIC);
        	if(!work)
                	return;

        	INIT_WORK(work, process_crash_kernel_notification);
        	queue_work(crash_wq, work);
		
	}
	else{
		printk("new timer\n");
		keepalive_tl.expires = jiffies + tdelay;
		add_timer(&keepalive_tl);
	}
}

void keepalive_init(void ) {    
	//crash_wq= create_singlethread_workqueue("crash_wq");
	/* start timer for keepalive functionality */
        init_timer(&keepalive_tl);
        keepalive_tl.data = (unsigned long ) 0;
        keepalive_tl.function = keepalive_timer;
        keepalive_tl.expires = jiffies + (tdelay*100); // TODO tdelay should be in jiffies
        //add_timer(&(keepalive_tl)); // TODO here the timer is not enabled!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
}

int keepalive_exit(void) {
	return del_timer_sync(&keepalive_tl);
}

#endif /* POPCORN_KEEP_ALIVE */

/*****************************************************************************/
/* General interface and Internal dispatching */
/*****************************************************************************/

void log_init(void ) {
	memset(log_receive,0,sizeof(struct pcn_kmsg_hdr)*LOGLEN); // LOG????
	memset(log_send,0,sizeof(struct pcn_kmsg_hdr)*LOGLEN);
	memset(log_function_called,0,sizeof(void*)*LOGCALL);
	memset(log_function_send,0,sizeof(void*)*LOGCALL);
}

volatile static int force_flush =0;
volatile static int force_waiters =0;
int max_msg_put = 0;
//static int pcn_read_proc(char *page, char **start, off_t off, int count, int *eof, void *data)
static ssize_t pcn_read_proc(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
    char *p= kmalloc(count, GFP_KERNEL), *end = 0;
    int len, i, idx;

    // to simplify life TODO TODO TODO
    if (*ppos > 0)
	return 0;

    end = p + count;
    p += snprintf(p, (size_t)(end -p), "force_flush: %d %d\n", force_flush, force_waiters);

// TODO put snprintf everywhere

    p += sprintf(p, "Sleep win_put[total,count,avg]=[%llx,%x,%llx]\n",
                    total_sleep_win_put,
                    sleep_win_put_count,
                    sleep_win_put_count ? (total_sleep_win_put/sleep_win_put_count) : 0ll);
    p += sprintf(p, "Sleep win_get[total,count,avg]=[%llx,%x,%llx]\n",
                    total_sleep_win_get,
                    sleep_win_get_count,
                    sleep_win_get_count ? (total_sleep_win_get/sleep_win_get_count) : 0ll);

    p += sprintf(p, "msg get:%ld\n", msg_get);
    p += sprintf(p, "msg put:%ld len:%d\n", msg_put, max_msg_put);

    idx = log_r_index;
    for (i =0; i>-LOGLEN; i--)
    	p +=sprintf (p,"r%d: from%d type%d %1d:%1d:%1d seq%d\n",
    			(idx+i),(int) log_receive[(idx+i)%LOGLEN].from_cpu, (int)log_receive[(idx+i)%LOGLEN].type,
    			(int) log_receive[(idx+i)%LOGLEN].is_lg_msg, (int)log_receive[(idx+i)%LOGLEN].lg_start,
    			(int) log_receive[(idx+i)%LOGLEN].lg_end, (int) log_receive[(idx+i)%LOGLEN].lg_seqnum );

    idx = log_s_index;
    for (i =0; i>-LOGLEN; i--)
    	p +=sprintf (p,"s%d: from%d type%d %1d:%1d:%1d seq%d\n",
    			(idx+i),(int) log_send[(idx+i)%LOGLEN].from_cpu, (int)log_send[(idx+i)%LOGLEN].type,
    			(int) log_send[(idx+i)%LOGLEN].is_lg_msg, (int)log_send[(idx+i)%LOGLEN].lg_start,
    			(int) log_send[(idx+i)%LOGLEN].lg_end, (int) log_send[(idx+i)%LOGLEN].lg_seqnum );

    idx = log_f_index;
    for (i =0; i>-LOGCALL; i--)
        p +=sprintf (p,"f%d: %pB\n",
        			(idx+i),(void*) log_function_called[(idx+i)%LOGCALL] );

    idx = log_f_sendindex;
    for (i =0; i>-LOGCALL; i--)
        p +=sprintf (p,"[s%d]->: %pB\n",
           			(idx+i),(void*) log_function_send[(idx+i)%LOGCALL] );

//    for(i=0; i<PCN_KMSG_RBUF_SIZE; i++)
    for(i=0; i<((PCN_KMSG_RBUF_SIZE >32) ? 32 : PCN_KMSG_RBUF_SIZE); i++) {
    	p +=sprintf (p,"sb[%i]=%i\n",i,rkvirt[my_cpu]->second_buffer[i]);
    }

//proc/file specific handling
    len = (p - (end -count));
    if ( copy_to_user(ubuf, (end -count), len) )
	return -EFAULT;
    kfree ((end -count));
/*	len = (p -page) - off;
	if (len < 0)
		len = 0;
	*eof = (len <= count) ? 1 : 0;
	*start = page + off; */
	*ppos = len;
	return len;
}

//static int peers_read_proc(char *page, char **start, off_t off, int count, int *eof, void *data)
static ssize_t peers_read_proc(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
	char *p= kmalloc(count, GFP_KERNEL), *end = 0;
    int len, i;
    char sbuffer[32];

    // to simplify life TODO TODO TODO
    if (*ppos > 0)
        return 0;

    end = p + count; // TODO TODO TODO check that we are not going out of the buffer (snprintf)

    // NOTE here we want to list which are the other kernels, then if the upper layers want to know
    // more about the other kernels they should send further messages?! like kinit from Akshay/Antonio?!
    // - I think functionalities should be split across layers, therefore here should be minimal, however
    // further redesign is necessary to support different plug-ins, here we are still too much confined
    // to the MAX_CPUS idea of multikernel
    for (i=0; i< POPCORN_MAX_CPUS; i++) {
    	memset(sbuffer, 0, 32);
//TODO depends on kernel version
#if 0        
    	cpumask_scnprintf(sbuffer, 32, &(rkinfo->_cpumask[i]));
#else
        scnprintf(sbuffer, 32, "%*pbl", 32, &(rkinfo->_cpumask[i]));
#endif

    	if (rkinfo->active[i] || rkinfo->phys_addr[i]) //I am not sure what active is
    		p += sprintf(p, "krn %d active %lx phys addr %lx (seq: %lx) cpus %s\n",
    		                    i, (unsigned long)rkinfo->active[i], (unsigned long)rkinfo->phys_addr[i], rkvirt_seq[i], sbuffer );
    	if (rkvirt[i])
    	    p += sprintf(p, "kernel %d mapped at %p (virtual) h:%ld t:%ld %ld ticks ago. %s\n",
                    i, rkvirt[i], rkvirt[i]->head, rkvirt[i]->tail,
					rkvirt_timeout[i], // TODO now
					(i == my_cpu) ? "THIS_CPU" : "" );
    }

//proc/file specific handling
	len = (p - (end -count));
	if ( copy_to_user(ubuf, (end -count), len) )
        	return -EFAULT;
	kfree ((end -count));

/*	len = (p -page) - off;
	if (len < 0)
		len = 0;
	*eof = (len <= count) ? 1 : 0;
	*start = page + off; */
	*ppos = len;
	return len;
}

static const struct file_operations pcn_read_proc_fops = {
	.owner = THIS_MODULE,
	.read		= pcn_read_proc,
};

static const struct file_operations peers_read_proc_fops = {
        .owner = THIS_MODULE,
	.read		= peers_read_proc,
};

// TODO should keep pointers to struct proc_dir_entry (s)
//
int pcn_init_proc (void) {
	/* if everything is ok create a proc interface */
	struct proc_dir_entry *res;
	//res = create_proc_entry("pcnmsg", S_IRUGO, NULL);
    res = proc_create_data("pcnmsg", S_IRUGO, NULL, &pcn_read_proc_fops, 0);
	if (!res) {
		printk(KERN_ALERT"%s: create_proc_entry pcnmsg failed (%p)\n", __func__, res);
		return -ENOMEM;
	}
	//res->read_proc = pcn_read_proc;

	res = proc_create_data("pcnpeers", S_IRUGO, NULL, &peers_read_proc_fops, 0);
	if (!res) {
		printk(KERN_ALERT"%s: create_proc_entry pcnpeers failed (%p)\n", __func__, res);
		return -ENOMEM;
	}
	//res->read_proc = peers_read_proc;

	return 0;
}

int pcn_exit_proc(void) {
	remove_proc_entry("pcnmsg", NULL);
	remove_proc_entry("pcnpeers", NULL);
	return 0;
}

void smp_popcorn_kmsg_interrupt(struct pt_regs *regs, unsigned long long ts);

int mklinux_boot = 0;

/*****************************************************************************/
/* init functions */
/*****************************************************************************/

static int __init pcn_kmsg_init(void)
{
	int rc, i; //rk
	unsigned long win_phys_addr, rkinfo_phys_addr = 0;
	struct pcn_kmsg_window *win_virt_addr;
	struct boot_params *boot_params_va;

	win_init(); // checks moved to ringBuffer code

	my_cpu = raw_smp_processor_id();
	if (!(my_cpu < POPCORN_MAX_CPUS))		
		my_cpu = POPCORN_MAX_CPUS -1;
	
	printk("%s: Entered pcn_kmsg_init raw: %d id: %d\n",
		__func__, my_cpu, raw_smp_processor_id());

	/* Initialize list heads */
	INIT_LIST_HEAD(&msglist_hiprio);
	INIT_LIST_HEAD(&msglist_normprio);

	/* Clear out large-message receive buffers */ //message reconstruction is done at high level code TODO
	// this is for very initial mklinux popcorn, where Ben created a receiver message per FROM cpu -- it used also by current version
	//memset(&lg_buf, 0, POPCORN_MAX_CPUS * sizeof(unsigned char *));
	for(i=0; i<POPCORN_MAX_CPUS; i++) {
		INIT_LIST_HEAD(&(lg_buf[i]));
	}
	long_id=0;

	/* initialize logging functions */
	log_init();
	
	/* Clear callback table and register default callback functions */ //also callback should be registeresd high level TODO
	KMSG_INIT("Registering initial callbacks...\n");
	memset(&callback_table, 0, PCN_KMSG_TYPE_MAX * sizeof(pcn_kmsg_cbftn));
	rc = pcn_kmsg_register_callback(PCN_KMSG_TYPE_CHECKIN, 
					&pcn_kmsg_checkin_callback);
	if (rc) {
		printk(KERN_ALERT"Failed to register initial kmsg checkin callback!\n");
	}
#ifdef POPCORN_KEEP_ALIVE
	rk = pcn_kmsg_register_callback(PCN_KMSG_TYPE_KEEPALIVE,
						&pcn_kmsg_keepalive_callback);
	if (rk) {
		printk(KERN_ALERT"Failed to register initial kmsg keepalive callback!\n");
	}
#endif /* POPCORN_KEEP_ALIVE */
#ifdef PCN_SUPPORT_MULTICAST
	rc = pcn_kmsg_register_callback(PCN_KMSG_TYPE_MCAST, 
					&pcn_kmsg_mcast_callback);
	if (rc) {
		printk(KERN_ALERT"Failed to register initial kmsg mcast callback!\n");
	}
#endif /* PCN_SUPPORT_MULTICAST */ 	

	/* Register softirq handler now kworker */
	KMSG_INIT("Registering softirq handler...\n");
	//open_softirq(PCN_KMSG_SOFTIRQ, pcn_kmsg_action);
	messaging_wq= create_workqueue("messaging_wq");
	if (!messaging_wq) 
		printk("%s: create_workqueue(messaging_wq) ret 0x%lx ERROR\n",
			__func__, (unsigned long)messaging_wq);

	/* Initialize work queue */
	KMSG_INIT("Initializing workqueue...\n");
	kmsg_wq = create_workqueue("kmsg_wq");
	if (!kmsg_wq)
		printk("%s: create_workqueue(kmsg_wq) ret 0x%lx ERROR\n",
			__func__, (unsigned long)kmsg_wq);

/*****************************************************************************/
/* transport specific initialization  TODO move somewhere else */
/*****************************************************************************/
		
	/* If we're the master kernel, malloc and map the rkinfo structure and 
	   put its physical address in boot_params; otherwise, get it from the 
	   boot_params and map it */
	if (!mklinux_boot) {
		/* rkinfo must be multiple of a page, because the granularity of
		 * foreings mapping is per page. The following didn't worked,
		 * the returned address is on the form 0xffff88000000, ioremap
		 * on the remote fails. 
		int order = get_order(sizeof(struct pcn_kmsg_rkinfo));
		rkinfo = __get_free_pages(GFP_KERNEL, order);
		*/
		KMSG_INIT("Primary kernel, mallocing rkinfo size:%ld rounded:%ld\n",
		       sizeof(struct pcn_kmsg_rkinfo), ROUND_PAGE_SIZE(sizeof(struct pcn_kmsg_rkinfo)));
		rkinfo = kmalloc(ROUND_PAGE_SIZE(sizeof(struct pcn_kmsg_rkinfo)), GFP_KERNEL);
		if (!rkinfo) {
			KMSG_ERR("Failed to malloc rkinfo structure!\n");
			return -1;
		}
		memset(rkinfo, 0x0, sizeof(struct pcn_kmsg_rkinfo));
		rkinfo_phys_addr = virt_to_phys(rkinfo);
		KMSG_INIT("rkinfo virt %p, phys 0x%lx MAX_CPUS %d\n", 
			  rkinfo, rkinfo_phys_addr, POPCORN_MAX_CPUS);

		/* Otherwise, we need to set the boot_params to show the rest
		   of the kernels where the master kernel's messaging window 
		   is. */
		KMSG_INIT("Setting boot_params...\n");
		boot_params_va = (struct boot_params *) 
			(__START_KERNEL_map ); //+ orig_boot_params);
// TODO requires patching
#if 0            
		boot_params_va->pcn_kmsg_master_window = rkinfo_phys_addr;
#endif
		KMSG_INIT("boot_params virt %p phys 0x%lx\n",
			boot_params_va, -1l); //orig_boot_params);
	}
	else {
// TODO requires patching        
#if 0        
		KMSG_INIT("Primary kernel rkinfo phys addr: 0x%lx\n", 
			  (unsigned long) boot_params.pcn_kmsg_master_window);
		rkinfo_phys_addr = boot_params.pcn_kmsg_master_window;
#endif		
		rkinfo = ioremap_cache(rkinfo_phys_addr, ROUND_PAGE_SIZE(sizeof(struct pcn_kmsg_rkinfo)));
		if (!rkinfo) {
			KMSG_ERR("Failed to map rkinfo from master kernel!\n");
		}
		KMSG_INIT("rkinfo virt addr: 0x%p\n", rkinfo);
	}

///////////////////////////////////////////////////////////////////////////////
// malloc its own receive buffer
	
	/* Malloc our own receive buffer and set it up */
	if (ROUND_PAGE_SIZE(sizeof(struct pcn_kmsg_window)) > KMALLOC_MAX_SIZE)
		printk(KERN_ALERT"%s: The next attempt to allocate memory will fail. Requested=0x%lx, max=0x%lx.\n",
        __func__, (unsigned long)ROUND_PAGE_SIZE(sizeof(struct pcn_kmsg_window)), (unsigned long)KMALLOC_MAX_SIZE);

	/* register interrupt handler, this requires kernel modifications */
	if (popcorn_kmsg_interrupt_handler == 0)
		popcorn_kmsg_interrupt_handler = smp_popcorn_kmsg_interrupt;
	else {
		printk(KERN_ALERT "another module already registered TODO\n");
		//TODO deregister the rest
		return -1;
	}
	
	win_virt_addr = kmalloc(ROUND_PAGE_SIZE(sizeof(struct pcn_kmsg_window)), GFP_KERNEL);
	if (win_virt_addr) {
		KMSG_INIT("Allocated %ld(%ld) bytes for my win, virt addr 0x%p\n", 
			  ROUND_PAGE_SIZE(sizeof(struct pcn_kmsg_window)),
			  sizeof(struct pcn_kmsg_window), win_virt_addr);
	} else {
		KMSG_ERR("%s: Failed to kmalloc kmsg recv window!\n", __func__);
		return -1;
	}

	rkvirt[my_cpu] = win_virt_addr;
	win_phys_addr = virt_to_phys((void *) win_virt_addr);
	KMSG_INIT("cpu %d physical address: 0x%lx\n", my_cpu, win_phys_addr);
	
	/* one of the following, rkinfo, per system */
	rkinfo->phys_addr[my_cpu] = win_phys_addr;
	memcpy(&(rkinfo->_cpumask[my_cpu]), cpu_present_mask, sizeof(struct cpumask));

	rc = pcn_kmsg_window_init(rkvirt[my_cpu]);
        if (rc) {
                KMSG_ERR("Failed to initialize kmsg recv window!\n");
                return -1;
        }

	rkinfo->active[my_cpu]= 1;
#if 1
	/* antonio clustering also for test TODO this is not completely correct, needs more work*/
{
	int cur_cpu;
	for_each_present_cpu(cur_cpu) { 
		if (cur_cpu == my_cpu)
			continue;
		if (!(cur_cpu < POPCORN_MAX_CPUS))
			break;
		rkvirt[cur_cpu] = win_virt_addr;
		rkinfo->active[cur_cpu] =1;
	}
}
#endif	

	/* If we're not the master kernel, we need to check in */
	if (mklinux_boot) {
		rc = do_checkin();

		if (rc) { 
			KMSG_ERR("Failed to check in!\n");
			return -1;
		}
	} 
	
// end window init
///////////////////////////////////////////////////////////////////////////////

	pcn_init_proc(); // check return value
#ifdef POPCORN_KEEP_ALIVE
	keepalive_init();
#endif

	return 0;
}


module_init(pcn_kmsg_init);
//the following when it is not a module
#if 0
subsys_initcall(pcn_kmsg_init);
#endif



static void wait_for_senders(void);

// TODO divide between arch specific / transport specific etc.
void pcn_kmsg_exit(void)
{
#ifdef POPCORN_KEEP_ALIVE
	keepalive_exit();
#endif
	wait_for_senders();
// TODO inhibit the possibility to send messages
	rkinfo->active[my_cpu]= 0; // we are not active anymore
	
	pcn_kmsg_unregister_callback(PCN_KMSG_TYPE_CHECKIN);
#ifdef POPCORN_KEEP_ALIVE	
	pcn_kmsg_unregister_callback(PCN_KMSG_TYPE_KEEPALIVE);
#endif /* POPCORN_KEEP_ALIVE */
#ifdef PCN_SUPPORT_MULTICAST
	pcn_kmsg_unregister_callback(PCN_KMSG_TYPE_MCAST);
#endif /* PCN_SUPPORT_MULTICAST */ 	
	
	popcorn_kmsg_interrupt_handler = 0; // unregister interrupt handler
	
	destroy_workqueue(messaging_wq);
	destroy_workqueue(kmsg_wq);
	
	pcn_exit_proc();
	
	kfree(rkvirt[my_cpu]);
	rkvirt[my_cpu] = 0;
	if (mklinux_boot)
		kfree(rkinfo); 
	else
		iounmap(rkinfo); 
	my_cpu = -1;
	
	printk("pcn_kmsg_exit: all resources released\n");
}
module_exit(pcn_kmsg_exit);

/* Register a callback function when a kernel module is loaded */
int pcn_kmsg_register_callback(enum pcn_kmsg_type type, pcn_kmsg_cbftn callback)
{
	PCN_WARN("%s: registering callback for type %d, ptr 0x%p\n", __func__, type, callback);

	if (type >= PCN_KMSG_TYPE_MAX) {
		printk(KERN_ALERT"Attempted to register callback with bad type %d\n", 
			 type);
		return -1;
	}

	callback_table[type] = callback;

	return 0;
}
EXPORT_SYMBOL(pcn_kmsg_register_callback);

/* Unregister a callback function when a kernel module is unloaded */
int pcn_kmsg_unregister_callback(enum pcn_kmsg_type type)
{
	if (type >= PCN_KMSG_TYPE_MAX) {
		KMSG_ERR("Attempted to register callback with bad type %d\n", 
			 type);
		return -1;
	}

	callback_table[type] = NULL;

	return 0;
}
EXPORT_SYMBOL(pcn_kmsg_unregister_callback);

/*****************************************************************************/
/* blocking functions */
/*****************************************************************************/

// this is used in exit function - accounts for (current) concurrent writers
static void wait_for_senders(void)
{
	long* active= &rkinfo->active[my_cpu];
	long copy_active;
        long res;
again:
        copy_active= *active;
        
        res= cmpxchg(active, copy_active, (-copy_active));
        if(res==(copy_active)){
        	copy_active= *active;
		while(copy_active!=-1){
			msleep(10);
			copy_active= *active;
		}
        }
        else{
        	goto again;
        }
        
}

// used in __pcn_kmsg_send only
// increments one the number of active writers to a single buffer
static int start_send_if_possible(int dest_cpu){
	long* active= &rkinfo->active[dest_cpu];
	long copy_active;
	long res;
again: 
	copy_active= *active;
	if(copy_active>0){
		res= cmpxchg(active, copy_active, copy_active+1);	
		if(res==(copy_active)){
			return 0;
		}
		else{
			goto again;
		}
	}
	else{
		return -1;
	}
}

// used in __pcn_kmsg_send only
// decrements one the number of active writers to a single buffer
static void finish_send(int dest_cpu){
	long* active= &rkinfo->active[dest_cpu];
        long copy_active;
        long res;
again:
        copy_active= *active;
        if(copy_active<-1){
                res= cmpxchg(active, copy_active, copy_active+1);
                if(res==(copy_active)){
                        return;
                }
                else{
                        goto again;
                }
        }
        else{
        	if(copy_active>1){
                	res= cmpxchg(active, copy_active, copy_active-1);
                	if(res==(copy_active)){
                        	return;
                	}
                	else{
                        	goto again;
            		}
        	}
		else{
		   printk("ERROR: win active count of cpu %d is %ld\n",dest_cpu,copy_active);
		}
        }
}

/* SENDING / MARSHALING */

//TODO refactoring
unsigned long int_ts;
EXPORT_SYMBOL(int_ts);

// TODO move to ringBuffer.c
static int __pcn_kmsg_send_timed(unsigned int dest_cpu, struct pcn_kmsg_message *msg,
			   int no_block, unsigned long * time)
{
	int rc= 0;
	struct pcn_kmsg_window *dest_window;

	if (unlikely(dest_cpu >= POPCORN_MAX_CPUS)) {
		KMSG_ERR("Invalid destination CPU %d\n", dest_cpu);
		return -1;
	}
	if (unlikely(!msg)) {
        KMSG_ERR("Passed in a null pointer to msg!\n");
        return -1;
    }

	dest_window = rkvirt[dest_cpu];
	if (unlikely(!dest_window)) {
		KMSG_ERR("Dest win for CPU %d not mapped!\n", dest_cpu);
		return -1;
	}

	rc = start_send_if_possible(dest_cpu);
	if (unlikely(rc==-1)) {
		KMSG_ERR("start_send_if possible failed\n");
                return -1;
	}

	msg->hdr.from_cpu = raw_smp_processor_id();
	rc = win_put_timed(dest_window, msg, no_block, time);
	if (rc) {
		if (no_block && (rc == -EAGAIN)) {
			KMSG_ERR("Failed to insert message\n");
			goto exit;
		}

		KMSG_ERR("Failed to place message in dest win!\n");
		goto exit;
	}

// NOTIFICATION ---------------------------------------------------------------
	/* send IPI */
	if (win_int_enabled(dest_window)) {
		KMSG_PRINTK("Interrupts enabled; sending IPI to %d ...\n", dest_cpu);

preempt_disable();
	int_ts = rdtsc(); // TODO refactor, somewhere else we are using native_read_tsc ...
// TODO the following dpends on the kernel version        
#if 1
        apic->send_IPI(dest_cpu, POPCORN_KMSG_VECTOR); //apic->send_IPI_single(dest_cpu, POPCORN_KMSG_VECTOR);
#else
        //default_send_IPI_single(dest_cpu, POPCORN_KMSG_VECTOR); // TODO note that there is also send_IPI_single_phys
#endif
preempt_enable();
	} else {
		KMSG_PRINTK("Interrupts not enabled; not sending IPI...\n");
	}

exit:
	finish_send(dest_cpu);
	return rc;
}

int pcn_kmsg_send(unsigned int dest_cpu, struct pcn_kmsg_message *msg)
{
// TODO this is only for logging should be included in a #ifdef (maybe these days is not needed anymore)
	unsigned long bp;
	//get_bp(bp);
	bp = (unsigned long)__builtin_return_address(0);
	log_function_send[log_f_sendindex%LOGCALL]= callback_table[msg->hdr.type];
	log_f_sendindex++;
// TODO end logging
	
	msg->hdr.is_lg_msg = 0;
	msg->hdr.lg_start = 0;
	msg->hdr.lg_end = 0;
	msg->hdr.lg_seqnum = 0;
	msg->hdr.long_number= 0;

	return __pcn_kmsg_send_timed(dest_cpu, msg, 0, 0);
}
EXPORT_SYMBOL(pcn_kmsg_send);

// TODO no block should be a parameter
int pcn_kmsg_send_noblock(unsigned int dest_cpu, struct pcn_kmsg_message *msg)
{
// TODO why there is no logging here?
	
	msg->hdr.is_lg_msg = 0;
	msg->hdr.lg_start = 0;
	msg->hdr.lg_end = 0;
	msg->hdr.lg_seqnum = 0;
	msg->hdr.long_number= 0;

	return __pcn_kmsg_send_timed(dest_cpu, msg, 1, 0);
}

/*
 * __pcn_ksg_send_long
 *
 * RETURNS 0 on success, it can propagate errors from lower layers
 */
static inline
int __pcn_kmsg_send_long(unsigned int dest_cpu,
		       struct pcn_kmsg_long_message *lmsg, 
		       unsigned int payload_size,
			   long * timeout)
{
	int i, ret =0;
	int num_chunks = payload_size / PCN_KMSG_PAYLOAD_SIZE;
	struct pcn_kmsg_message this_chunk;
	unsigned long _time;

	if (payload_size % PCN_KMSG_PAYLOAD_SIZE) {
		num_chunks++;
	}

	 if ( num_chunks >= MAX_CHUNKS ){
		printk(KERN_ALERT"Message too long (size:%d, chunks:%d, max:%d) can not be transferred\n",
	                payload_size, num_chunks, MAX_CHUNKS);
	        return -EINVAL;
	 }

	KMSG_PRINTK("Sending large message to CPU %d, type %d, payload size %d bytes, %d chunks\n", 
		    dest_cpu, lmsg->hdr.type, payload_size, num_chunks);

	if (payload_size > max_msg_put)
		max_msg_put = payload_size;

	this_chunk.hdr.type = lmsg->hdr.type;
	this_chunk.hdr.prio = lmsg->hdr.prio;
	this_chunk.hdr.is_lg_msg = 1;
	this_chunk.hdr.long_number= fetch_and_add(&long_id,1);

	for (i = 0; i < num_chunks; i++) {
		KMSG_PRINTK("Sending chunk %d\n", i);

		this_chunk.hdr.lg_start = (i == 0) ? 1 : 0;
		this_chunk.hdr.lg_end = (i == num_chunks - 1) ? 1 : 0;
		this_chunk.hdr.lg_seqnum = (i == 0) ? num_chunks : i;

		memcpy(&this_chunk.payload, 
		       ((unsigned char *) &lmsg->payload) + 
		       i * PCN_KMSG_PAYLOAD_SIZE, 
		       PCN_KMSG_PAYLOAD_SIZE);

		ret= __pcn_kmsg_send_timed(dest_cpu, &this_chunk, 0, &_time);
		if (timeout) {
			*timeout -= _time;
			if (*timeout < 0 && (i < (num_chunks-1))) { // TODO inform the other end that the message is truncated
				//notify the other kernel to erase this partial message
                                this_chunk.hdr.type= PCN_KMSG_DELETE_LONG_MSG;
                                __pcn_kmsg_send_timed(dest_cpu, &this_chunk, 0, 0);
				return -ETIMEDOUT;
			}
		}

		if(ret!=0)
			return ret;
	}

	return 0;
}

int pcn_kmsg_send_long(unsigned int dest_cpu,
		       struct pcn_kmsg_long_message *lmsg,
		       unsigned int payload_size)
{
	return __pcn_kmsg_send_long(dest_cpu, lmsg, payload_size, 0);
}
EXPORT_SYMBOL(pcn_kmsg_send_long);

/*
 * RETURNs -ETIMEDOUT if timeout expires and in timeout the still available time (negative if is past the deadline)
 */
int pcn_kmsg_send_long_timeout(unsigned int dest_cpu,
		       struct pcn_kmsg_long_message *lmsg,
		       unsigned int payload_size, long * timeout)
{
	return __pcn_kmsg_send_long(dest_cpu, lmsg, payload_size, timeout);
}

///////////////////////////////////////////////////////////////////////////////
// RECEIVING / UNMARSHALING
///////////////////////////////////////////////////////////////////////////////

static int process_message_list(struct list_head *head) 
{
	int rc, rc_overall = 0;
	struct pcn_kmsg_container *pos = NULL, *n = NULL;
	struct pcn_kmsg_message *msg;

	list_for_each_entry_safe(pos, n, head, list) {
		msg = &pos->msg;

		KMSG_PRINTK("Item in list, type %d,  processing it...\n", 
			    msg->hdr.type);

		list_del(&pos->list);

		if (unlikely(msg->hdr.type >= PCN_KMSG_TYPE_MAX || 
			     !callback_table[msg->hdr.type])) {
			KMSG_ERR("Invalid type %d; continuing!\n", 
				 msg->hdr.type);
			continue;
		}

		rc = callback_table[msg->hdr.type](msg);
		if (!rc_overall) {
			rc_overall = rc;
		}
		//log_function_called[log_f_index%LOGLEN]= callback_table[msg->hdr.type];
		//memcpy(&(log_function_called[log_f_index%LOGCALL]),&(callback_table[msg->hdr.type]),sizeof(void*));
		log_function_called[log_f_index%LOGCALL]= callback_table[msg->hdr.type];
		log_f_index++;
		/* NOTE: callback function is responsible for freeing memory
		   that was kmalloced! */
	}

	return rc_overall;
}

//void pcn_kmsg_do_tasklet(unsigned long);
//DECLARE_TASKLET(pcn_kmsg_tasklet, pcn_kmsg_do_tasklet, 0);

// TODO this is pretty bad need to refactor, also it doesn't support concurrency ...
unsigned volatile long isr_ts = 0, isr_ts_2 = 0;
EXPORT_SYMBOL(isr_ts);
EXPORT_SYMBOL(isr_ts_2);

/* top half */
#if 0
void smp_popcorn_kmsg_interrupt(struct pt_regs *regs)
{
	//if (!isr_ts) {
		rdtscll(isr_ts);
	//}

	ack_APIC_irq();

	KMSG_PRINTK("Reached Popcorn KMSG interrupt handler!\n");

//	inc_irq_stat(irq_popcorn_kmsg_count); // TODO require patching
	irq_enter();
#endif

//irq_enter and irq_exit are already called by the __irq_entry ISR
void smp_popcorn_kmsg_interrupt(struct pt_regs *regs, unsigned long long ts)
{
	struct work_struct* kmsg_work = 0;

	/* We do as little work as possible in here (decoupling notification 
	   from messaging) */

	/* disable further interrupts for now */
	win_disable_int(rkvirt[my_cpu]);

	isr_ts_2 = rdtsc();
	isr_ts = (unsigned long)ts;

	KMSG_PRINTK("interrupt handler at CPU %d\n", raw_smp_processor_id());

	/* schedule bottom half */
	//__raise_softirq_irqoff(PCN_KMSG_SOFTIRQ);
	kmsg_work = kmalloc(sizeof(struct work_struct), GFP_ATOMIC); // TODO allocate a pool of this size to speed up allocation
	if (kmsg_work) {
		INIT_WORK(kmsg_work,pcn_kmsg_action);
		queue_work(messaging_wq, kmsg_work); //it was queue_work_on schedules on the same CPU
	} else {
		KMSG_ERR("Failed to kmalloc work structure!\n");
	}
	//tasklet_schedule(&pcn_kmsg_tasklet);
	return;
}
	
#if 0
	irq_exit();
	return;
}
#endif

static int msg_add_list(struct pcn_kmsg_container *ctr)
{
	int rc = 0;

	switch (ctr->msg.hdr.prio) {
		case PCN_KMSG_PRIO_HIGH:
			KMSG_PRINTK("%s: Adding to high-priority list...\n", __func__);
			list_add_tail(&(ctr->list),
				      &msglist_hiprio);
			break;

		case PCN_KMSG_PRIO_NORMAL:
			KMSG_PRINTK("%s: Adding to normal-priority list...\n", __func__);
			list_add_tail(&(ctr->list),
				      &msglist_normprio);
			break;

		default:
			KMSG_ERR("%s: Priority value %d unknown -- THIS IS BAD!\n", __func__,
				  ctr->msg.hdr.prio);
			rc = -1;
	}

	return rc;
}

static int process_large_message(struct pcn_kmsg_reverse_message *msg)
{
	int rc = 0;
	int recv_buf_size;
	struct pcn_kmsg_long_message *lmsg;
	int work_done = 0;
	struct pcn_kmsg_container* container_long=NULL, *n=NULL;

	KMSG_PRINTK("Got a large message fragment, type %u, from_cpu %u, start %u, end %u, seqnum %u!\n",
		    msg->hdr.type, msg->hdr.from_cpu,
		    msg->hdr.lg_start, msg->hdr.lg_end,
		    msg->hdr.lg_seqnum);

	 //msg timeout, erase whatever was saved of it (NOTE at least on msg must exist)
        if(msg->hdr.type == PCN_KMSG_DELETE_LONG_MSG){
                //It should not be needed safe
                list_for_each_entry_safe(container_long, n, &lg_buf[msg->hdr.from_cpu], list) {
                        if ( (container_long != NULL) &&
                          (container_long->msg.hdr.long_number == msg->hdr.long_number) ){
                                list_del(&container_long->list);
                                work_done = 1;
                                goto out;

                        }

                }

                KMSG_ERR("Failed to find long message %lu in the list of cpu %i!\n",
                         msg->hdr.long_number, msg->hdr.from_cpu);
                goto out;

        }

	if (msg->hdr.lg_start) {
		KMSG_PRINTK("Processing initial message fragment...\n");

		if (!msg->hdr.lg_seqnum)
		  printk(KERN_ALERT"%s: ERROR lg_seqnum is zero:%d long_number:%ld\n",
		      __func__, (int)msg->hdr.lg_seqnum, (long)msg->hdr.long_number);
		  
		// calculate the size of the holding buffer
		recv_buf_size = sizeof(struct list_head) + 
			sizeof(struct pcn_kmsg_hdr) + 
			msg->hdr.lg_seqnum * PCN_KMSG_PAYLOAD_SIZE;
#undef BEN_VERSION
#ifdef BEN_VERSION		
		lg_buf[msg->hdr.from_cpu] = kmalloc(recv_buf_size, GFP_ATOMIC);
		if (!lg_buf[msg->hdr.from_cpu]) {
					KMSG_ERR("Unable to kmalloc buffer for incoming message!\n");
					goto out;
				}
		lmsg = (struct pcn_kmsg_long_message *) &lg_buf[msg->hdr.from_cpu]->msg;
#else /* BEN_VERSION */
		container_long= kmalloc(recv_buf_size, GFP_ATOMIC);
		if (!container_long) {
			KMSG_ERR("Unable to kmalloc buffer for incoming message!\n");
			goto out;
		}
		lmsg = (struct pcn_kmsg_long_message *) &container_long->msg; //TODO wrong cast!
#endif /* !BEN_VERSION */

		/* copy header first */
		memcpy((unsigned char *) &lmsg->hdr, 
		       &msg->hdr, sizeof(struct pcn_kmsg_hdr));
		/* copy first chunk of message */
		memcpy((unsigned char *) &lmsg->payload,
		       &msg->payload, PCN_KMSG_PAYLOAD_SIZE);

		if (msg->hdr.lg_end) {
			KMSG_PRINTK("NOTE: Long message of length 1 received; this isn't efficient!\n");

			/* add to appropriate list */
#ifdef BEN_VERSION			
			rc = msg_add_list(lg_buf[msg->hdr.from_cpu]);
#else /* BEN_VERSION */
			rc = msg_add_list(container_long);
#endif /* !BEN_VERSION */
			if (rc)
				KMSG_ERR("Failed to add large message to list!\n");
			work_done = 1;
		}
#ifndef BEN_VERSION		
		else
		  // add the message in the lg_buf
		  list_add_tail(&container_long->list, &lg_buf[msg->hdr.from_cpu]);
#endif /* !BEN_VERSION */
	}
	else {
		KMSG_PRINTK("Processing subsequent message fragment...\n");

		//It should not be needed safe
		list_for_each_entry_safe(container_long, n, &lg_buf[msg->hdr.from_cpu], list) {
			if ( (container_long != NULL) &&
			  (container_long->msg.hdr.long_number == msg->hdr.long_number) )
				// found!
				goto next;
		}

		KMSG_ERR("Failed to find long message %lu in the list of cpu %i!\n",
			 msg->hdr.long_number, msg->hdr.from_cpu);
		goto out;

next:		
		lmsg = (struct pcn_kmsg_long_message *) &container_long->msg;
		memcpy((unsigned char *) ((void*)&lmsg->payload) + (PCN_KMSG_PAYLOAD_SIZE * msg->hdr.lg_seqnum),
		       &msg->payload, PCN_KMSG_PAYLOAD_SIZE);

		if (msg->hdr.lg_end) {
			KMSG_PRINTK("Last fragment in series...\n");
			KMSG_PRINTK("from_cpu %d, type %d, prio %d\n",
				    lmsg->hdr.from_cpu, lmsg->hdr.type, lmsg->hdr.prio);
			/* add to appropriate list */
#ifdef BEN_VERSION
			rc = msg_add_list(lg_buf[msg->hdr.from_cpu]);
#else /* BEN_VERSION */
			list_del(&container_long->list);
			rc = msg_add_list(container_long);
#endif /* !BEN_VERSION */			
			if (rc)
				KMSG_ERR("Failed to add large message to list!\n");
			work_done = 1;
		}
	}

out:
	return work_done;
}

static int process_small_message(struct pcn_kmsg_reverse_message *msg)
{
	int rc = 0, work_done = 1;
	struct pcn_kmsg_container *incoming;

	/* malloc some memory (don't sleep!) */
	incoming = kmalloc(sizeof(struct pcn_kmsg_container), GFP_ATOMIC);
	if (unlikely(!incoming)) {
		KMSG_ERR("Unable to kmalloc buffer for incoming message!\n");
		return 0;
	}

	/* memcpy message from rbuf */
	memcpy(&incoming->msg.hdr, &msg->hdr,
	       sizeof(struct pcn_kmsg_hdr));

	memcpy(&incoming->msg.payload, &msg->payload,
	       PCN_KMSG_PAYLOAD_SIZE);

	KMSG_PRINTK("Received message, type %d, prio %d\n",
		    incoming->msg.hdr.type, incoming->msg.hdr.prio);

	/* add container to appropriate list */
	rc = msg_add_list(incoming);

	return work_done;
}

//DECLARE_PER_CPU(int, force_flush); //TODO initialize to 0
//volatile static int force_flush=0; //NOTE declared before for statistics
//DECLARE_PER_CPU(int, force_waiters); //TODO initialize to 0
//volatile static int force_waiters=0; //NOTE declared before for statistics
DECLARE_WAIT_QUEUE_HEAD(force_flush_queue);

//TODO this should be converted PER_CPU we left like this because it also possible that the calling thread can migrate
//NOTE if you don't want a timeout just pass 0 as an argument --- timeout in jiffies
int pcn_kmsg_force_flush(unsigned long *timeout)
{
	struct work_struct* kmsg_work = kmalloc(sizeof(struct work_struct), GFP_ATOMIC);
	int forced = sync_cmpxchg(&force_flush, 0, 1); //another thread on the same cpu can be already waiting
	int ret =0;

	force_waiters++; //we are keeping statistics on how many waiters there are
	if (!kmsg_work) //check consistency before continuing
		BUG(); //cannot allocate memory to queue work

	if (!forced) { //if we are the first asking for a force action we should trigger pcn_kmsg_action->pcn_kmsg_poll_handler
		INIT_WORK(kmsg_work,pcn_kmsg_action);
                queue_work(messaging_wq, kmsg_work);
	}

	if (timeout) { //now waiting (all threads in synch) - with or without timeout
		ret = wait_event_timeout(force_flush_queue, (force_flush==0),(long)*timeout);
		if (ret) 
			*timeout = (unsigned long) ret;
		else
			ret =-3; //timeout error
	}
	else {
		wait_event(force_flush_queue, (force_flush==0));
	}
	
	force_waiters--; //note that we are doing this even in the case of timeout error
	if (ret ==-3) //I am not sure if this is a good idea or no
		sync_cmpxchg(&force_flush, 1, 0);

	return ret;
}

//NOTE  the following is right now in ticks
#define FORCED_TIMEOUT 1000000l
#define UNFORCED_TIMEOUT 4000000l
#define CHANGE_MODE_T 1000
//#define UNFORCED_TIMEOUT (~0l)
int change_mode= 0;
static int pcn_kmsg_poll_handler(void)
{
	struct pcn_kmsg_reverse_message *msg;
	struct pcn_kmsg_window *win = rkvirt[my_cpu]; // TODO this will not work for clustering
	int work_done = 0;
	int forced =0; // TODO fetch this value from a configuration it should be protected or atomically changed
	int ret =0;
	unsigned long timeout =UNFORCED_TIMEOUT;

	KMSG_PRINTK("called\n");

	if ((forced = force_flush))
		timeout = FORCED_TIMEOUT;

pull_msg:
	/* Get messages out of the buffer first */
//#define PCN_KMSG_BUDGET 128
	//while ((work_done < PCN_KMSG_BUDGET) && (!win_get(rkvirt[my_cpu], &msg))) {
	//while ( win_get(win, &msg) ) {
	while ( (ret = win_get_common(win, &msg, forced, &timeout)) != -1 ) {
		int _forced = forced;
		KMSG_PRINTK("got a message!\n");

		if ((forced = force_flush)) //update operation mode (it is async with this execution)
			timeout = FORCED_TIMEOUT;
		else
                        timeout = UNFORCED_TIMEOUT;

		if (ret == 0) { // success
			/* Special processing for large messages */
			if (msg->hdr.is_lg_msg) {
				KMSG_PRINTK("message is a large message!\n");
				work_done += process_large_message(msg);
			} else {
				KMSG_PRINTK("message is a small message!\n");
				work_done += process_small_message(msg);
			}
			pcn_barrier();
			msg->ready = 0;
			if(change_mode>0)
				change_mode--;
		}
		else if (ret == -2) { // timout expired
			KMSG_PRINTK("retry (timout expired %lu ticks) %s %d\n", 
				(_forced ? UNFORCED_TIMEOUT : FORCED_TIMEOUT),
				(_forced != forced ? "mode switch" : "same mode"), forced);
			change_mode++;
			if(change_mode>CHANGE_MODE_T)
				force_flush= 1;
			continue; //don't increment
		}
		else if (ret == -3) {// message not ready yet
			KMSG_PRINTK("continue to the next message (message not ready in timeout %lu %lu) %s\n",
				 FORCED_TIMEOUT, timeout,
				(_forced != forced ? "mode switch" : "same mode"));
		}
		else {// unexpected state
			KMSG_PRINTK(KERN_ALERT"%s: win_get_common returned %ld unknown value ABORTING OPERATION FORCED (%d,%d).\n",
				__func__, (long)ret, _forced, forced);
			win_enable_int(win);
			return work_done;
		}

		//win_advance_tail(win);
		fetch_and_add(&win->tail, 1);
	}

	win_enable_int(win);
	if ( win_inuse(win) ) {
		win_disable_int(win);
		goto pull_msg;
	}
/* NOTE  we moved this into pcn_kmsg_action because you want to be sure that messages were also dispatched
	if ( wait_queue_active(&force_flush_queue) ) {
		wake_up_all(force_flush_queue);
		int ret = sync_cmpxchg(&force_flush, 1, 0);
		if ( !ret )
			printk(KERN_ALERT"%s: waken up all elements in force_flush wait queue but force_flush was %d\n", __func__, ret );
	}*/
	return work_done;
}

// TODO need refactoring
volatile unsigned long bh_ts = 0, bh_ts_2 = 0;
EXPORT_SYMBOL(bh_ts);
EXPORT_SYMBOL(bh_ts_2);

// NOTE the following was declared as a bottom half
//static void pcn_kmsg_action(struct softirq_action *h)
static void pcn_kmsg_action(struct work_struct* work)
{
	int rc;
	int work_done = 0;

	//if (!bh_ts) {
		bh_ts = rdtsc();
	//}
	KMSG_PRINTK("called\n");

	work_done = pcn_kmsg_poll_handler();
	KMSG_PRINTK("Handler did %d units of work!\n", work_done);

#ifdef PCN_SUPPORT_MULTICAST	
	for (i = 0; i < POPCORN_MAX_MCAST_CHANNELS; i++) {
		if (MCASTWIN(i)) {
			KMSG_PRINTK("mcast win %d mapped, processing it\n", i);
			process_mcast_queue(i);
		}
	}
	KMSG_PRINTK("Done checking mcast queues; processing messages\n");
#endif /* PCN_SUPPORT_MULTICAST */

	//if (!bh_ts_2) {
		bh_ts_2 = rdtsc();
	//}

	/* Process high-priority queue first */
	rc = process_message_list(&msglist_hiprio);

	if (list_empty(&msglist_hiprio)) {
		KMSG_PRINTK("High-priority queue is empty!\n");
	}

	/* Then process normal-priority queue */
	rc = process_message_list(&msglist_normprio);

	if ( waitqueue_active(&force_flush_queue) ) {
		int ret;                
		wake_up_all(&force_flush_queue);
                ret = sync_cmpxchg(&force_flush, 1, 0);
                if ( !ret )
                        printk(KERN_ALERT"%s: waken up all elements in force_flush wait queue but force_flush was %d\n", __func__, ret );
	}

	kfree(work);
	return;
}

// TODO remove, really old, from Ben
#ifdef PCN_SUPPORT_MULTICAST
# include "pcn_kmsg_mcast.h"
#endif /* PCN_SUPPORT_MULTICAST */
