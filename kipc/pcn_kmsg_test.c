
// moved as an external module (not compiled in kernel)
// the user space need integration of Phil's tests, not sure if there are tests from Ben tho

/*
 * Tests/benchmarks for Popcorn inter-kernel messaging layer
 * Must work for any transport (need more refactoring for that, here only one test at the time)
 *
 * Rewritten by Antonio Barbalace, Stevens 2019
 * 
 * (C) Ben Shelton <beshelto@vt.edu> 2013
 */



#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/slab.h>

#ifdef PCN_TEST_SYSCALL
#include <linux/syscalls.h>

#else
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/uaccess.h>

#include <linux/ioctl.h>
#endif

#include "multikernel.h"
#include "pcn_kmsg.h"
#include "pcn_kmsg_test.h"

///////////////////////////////////////////////////////////////////////////////
// macros
///////////////////////////////////////////////////////////////////////////////

#define KMSG_TEST_VERBOSE 0

#if KMSG_TEST_VERBOSE
#define TEST_PRINTK(fmt, args...) printk("%s: " fmt, __func__, ##args)
#else
#define TEST_PRINTK(...) ;
#endif

#define TEST_ERR(fmt, args...) printk("%s: ERROR: " fmt, __func__, ##args)

volatile unsigned long kmsg_tsc;
unsigned long ts1, ts2, ts3, ts4, ts5;

volatile int kmsg_done;

extern volatile unsigned long isr_ts, isr_ts_2, bh_ts, bh_ts_2;

///////////////////////////////////////////////////////////////////////////////
// test funcs
///////////////////////////////////////////////////////////////////////////////

static int pcn_kmsg_test_send_single(struct pcn_kmsg_test_args *args)
{
	int rc = 0;
	struct pcn_kmsg_test_message msg;
	unsigned long ts_start, ts_end;

	msg.hdr.type = PCN_KMSG_TYPE_TEST;
	msg.hdr.prio = PCN_KMSG_PRIO_HIGH;
	msg.op = PCN_KMSG_TEST_SEND_SINGLE;
	msg.src_cpu = raw_smp_processor_id();
	msg.dest_cpu = args->cpu;

	ts_start = rdtsc();
	pcn_kmsg_send(args->cpu, (struct pcn_kmsg_message *) &msg);
	ts_end = rdtsc();

	args->send_ts = ts_end - ts_start;

	return rc;
}

extern unsigned long int_ts;

static int pcn_kmsg_test_send_pingpong(struct pcn_kmsg_test_args __user *args)
{
	int rc = 0;
	struct pcn_kmsg_test_message msg;
	unsigned long ts_start, ts_end;

	msg.hdr.type = PCN_KMSG_TYPE_TEST;
	msg.hdr.prio = PCN_KMSG_PRIO_HIGH;
	msg.op = PCN_KMSG_TEST_SEND_PINGPONG;
	msg.src_cpu = raw_smp_processor_id();
	msg.dest_cpu = args->cpu;
	
	kmsg_done = 0;

	ts_start = rdtsc();
	pcn_kmsg_send(args->cpu, (struct pcn_kmsg_message *) &msg);
	ts_end = rdtsc();
	
	while (!kmsg_done) {} // TODO may require refactoring

// TODO some more refactoring is needed
	TEST_PRINTK("Elapsed time (ticks): %lu\n", kmsg_tsc - ts_start);

	args->send_ts = ts_start;
	args->ts0 = int_ts;
	args->ts1 = ts1;
	args->ts2 = ts2;
	args->ts3 = ts3;
	args->ts4 = ts4;
	args->ts5 = ts5;
	args->rtt = kmsg_tsc;

	return rc;
}

static int pcn_kmsg_test_send_batch(struct pcn_kmsg_test_args __user *args)
{
	int rc = 0;
	unsigned long i;
	struct pcn_kmsg_test_message msg;
	unsigned long batch_send_start_tsc, batch_send_end_tsc;

	TEST_PRINTK("Testing batch send, batch_size %lu\n", args->batch_size);

	msg.hdr.type = PCN_KMSG_TYPE_TEST;
	msg.hdr.prio = PCN_KMSG_PRIO_HIGH;
	msg.op = PCN_KMSG_TEST_SEND_BATCH;
	msg.batch_size = args->batch_size;

	batch_send_start_tsc = rdtsc();

	kmsg_done = 0;

	/* send messages in series */
	for (i = 0; i < args->batch_size; i++) {
		msg.batch_seqnum = i;

		TEST_PRINTK("Sending batch message, cpu %d, seqnum %lu\n", 
			    args->cpu, i);

		rc = pcn_kmsg_send(args->cpu, 
				   (struct pcn_kmsg_message *) &msg);

		if (rc) {
			TEST_ERR("Error sending message!\n");
			return -1;
		}
	}

	batch_send_end_tsc = rdtsc();

	/* wait for reply to last message */

	while (!kmsg_done) {}

	args->send_ts = batch_send_start_tsc;
	args->ts0 = batch_send_end_tsc;
	args->ts1 = ts1;
	args->ts2 = ts2;
	args->ts3 = ts3;
	args->rtt = kmsg_tsc;

	return rc;
}


static int pcn_kmsg_test_long_msg(struct pcn_kmsg_test_args __user *args)
{
	int rc;
	unsigned long start_ts, end_ts;
	struct pcn_kmsg_long_message lmsg;
	char *str = "This is a very long test message.  Don't be surprised if it gets corrupted; it probably will.  If it does, you're in for a lot more work, and may not get home to see your wife this weekend.  You should knock on wood before running this test.";

	lmsg.hdr.type = PCN_KMSG_TYPE_TEST_LONG;
	lmsg.hdr.prio = PCN_KMSG_PRIO_NORMAL;

	strcpy((char *) &lmsg.payload, str);

	TEST_PRINTK("Message to send: %s\n", (char *) &lmsg.payload);

	TEST_PRINTK("syscall to test kernel messaging, to CPU %d\n", 
		    args->cpu);

	start_ts = rdtsc();

	rc = pcn_kmsg_send_long(args->cpu, &lmsg, strlen(str) + 5);

	end_ts = rdtsc();

	args->send_ts = end_ts - start_ts;

	TEST_PRINTK("POPCORN: pcn_kmsg_send_long returned %d\n", rc);

	return rc;
}

#ifdef PCN_SUPPORT_MULTICAST
static int pcn_kmsg_test_mcast_open(struct pcn_kmsg_test_args __user *args)
{
	int rc;
	pcn_kmsg_mcast_id test_id = -1;

	/* open */
	TEST_PRINTK("open\n");
	rc = pcn_kmsg_mcast_open(&test_id, args->mask);

	TEST_PRINTK("pcn_kmsg_mcast_open returned %d, test_id %lu\n",
		    rc, test_id);

	args->mcast_id = test_id;

	return rc;
}

extern unsigned long mcast_ipi_ts;

static int pcn_kmsg_test_mcast_send(struct pcn_kmsg_test_args __user *args)
{
	int rc;
	struct pcn_kmsg_test_message msg;
	unsigned long ts_start, ts_end;

	/* send */
	TEST_PRINTK("send\n");
	msg.hdr.type = PCN_KMSG_TYPE_TEST;
	msg.hdr.prio = PCN_KMSG_PRIO_HIGH;
	msg.op = PCN_KMSG_TEST_SEND_SINGLE;

	ts_start = rdtsc();

	rc = pcn_kmsg_mcast_send(args->mcast_id,
				 (struct pcn_kmsg_message *) &msg);

	ts_end = rdtsc();

	if (rc) {
		TEST_ERR("failed to send mcast message to group %lu!\n",
			 args->mcast_id);
	}

	args->send_ts = ts_start;
	args->ts0 = mcast_ipi_ts;
	args->ts1 = ts_end;

	return rc;
}

static int pcn_kmsg_test_mcast_close(struct pcn_kmsg_test_args __user *args)
{
	int rc;

	/* close */
	TEST_PRINTK("close\n");

	rc = pcn_kmsg_mcast_close(args->mcast_id);

	TEST_PRINTK("mcast close returned %d\n", rc);

	return rc;
}
#endif /* PCN_SUPPORT_MULTICAST */


///////////////////////////////////////////////////////////////////////////////
// CALLBACKS 
///////////////////////////////////////////////////////////////////////////////

static int handle_single_msg(struct pcn_kmsg_test_message *msg)
{
	TEST_PRINTK("Received single test message from CPU %d! (current CPU %d) [from:%d, to:%d]\n",
		    msg->hdr.from_cpu, raw_smp_processor_id() msg->src_cpu, msg->dest_cpu);
	
	//nothing todo
	// TODO add another one for the case of shared memory multikernel where we do the trick of Barrelfish
	
	return 0;
}

static int handle_pingpong_msg(struct pcn_kmsg_test_message *msg)
{
	int rc = 0;
	unsigned long handler_ts;

	handler_ts = rdtsc();

	TEST_PRINTK("Received single test message from CPU %d! (current CPU %d) [from:%d, to:%d]\n",
		    msg->hdr.from_cpu, raw_smp_processor_id() msg->src_cpu, msg->dest_cpu);
	
	// this CPU is the designated receiver
	if (raw_smp_processor_id() == msg->dest_cpu) {

		struct pcn_kmsg_test_message reply_msg;

		reply_msg.hdr.type = PCN_KMSG_TYPE_TEST;
		reply_msg.hdr.prio = PCN_KMSG_PRIO_HIGH;
		reply_msg.op = PCN_KMSG_TEST_SEND_PINGPONG;
		reply_msg.ts1 = isr_ts;
		reply_msg.ts2 = isr_ts_2;
		reply_msg.ts3 = bh_ts;
		reply_msg.ts4 = bh_ts_2;
		reply_msg.ts5 = handler_ts;

		TEST_PRINTK("Sending message back to CPU 0...\n");
		rc = pcn_kmsg_send(msg->src_cpu, (struct pcn_kmsg_message *) &reply_msg);

		if (rc) {
			TEST_ERR("Message send failed!\n");
			return -1;
		}

		isr_ts = isr_ts_2 = bh_ts = bh_ts_2 = 0;
	} 
	// this CPU should be the sender
	else {
		if (raw_smp_processor_id() != msg->src_cpu)
			TEST_ERR("WARNING not the sender of this ping pong message (current CPU %d) [from:%d, to:%d]\n",
					 raw_smp_processor_id(), msg->src_cpu, msg->dest_cpu);
			
		TEST_PRINTK("Received ping-pong; reading end timestamp...\n");
		kmsg_tsc = rdtsc();
		ts1 = msg->ts1;
		ts2 = msg->ts2;
		ts3 = msg->ts3;
		ts4 = msg->ts4;
		ts5 = msg->ts5;
		kmsg_done = 1;
	}

	return 0;
}

unsigned long batch_start_tsc;

// TODO TODO TODO TODO TODO
static int handle_batch_msg(struct pcn_kmsg_test_message *msg)
{
	int rc = 0;

	TEST_PRINTK("seqnum %lu size %lu\n", msg->batch_seqnum, 
		    msg->batch_size);

	if (msg->batch_seqnum == 0) {
		TEST_PRINTK("Start of batch; taking initial timestamp!\n");
		batch_start_tsc = rdtsc();

	}
	
	if (msg->batch_seqnum == (msg->batch_size - 1)) {
		/* send back reply */
		struct pcn_kmsg_test_message reply_msg;
		unsigned long batch_end_tsc;

		TEST_PRINTK("End of batch; sending back reply!\n");
		batch_end_tsc = rdtsc();

		reply_msg.hdr.type = PCN_KMSG_TYPE_TEST;
		reply_msg.hdr.prio = PCN_KMSG_PRIO_HIGH;
		reply_msg.op = PCN_KMSG_TEST_SEND_BATCH_RESULT;
		reply_msg.ts1 = bh_ts;
		reply_msg.ts2 = bh_ts_2;
		reply_msg.ts3 = batch_end_tsc;

		isr_ts = isr_ts_2 = bh_ts = bh_ts_2 = 0;

		rc = pcn_kmsg_send(0, (struct pcn_kmsg_message *) &reply_msg);

		if (rc) {
			TEST_ERR("Message send failed!\n");
			return -1;
		}
	}
	return rc;
}

static int handle_batch_result_msg(struct pcn_kmsg_test_message *msg)
{
	int rc = 0;

	kmsg_tsc = rdtsc();

	ts1 = msg->ts1;
	ts2 = msg->ts2;
	ts3 = msg->ts3;

	kmsg_done = 1;

	return rc;
}

static int pcn_kmsg_test_callback(struct pcn_kmsg_message *message)
{
	int rc = 0;

	struct pcn_kmsg_test_message *msg = 
		(struct pcn_kmsg_test_message *) message;

	TEST_PRINTK("Reached %s, op %d!\n", __func__, msg->op);

	switch (msg->op) {
		case PCN_KMSG_TEST_SEND_SINGLE:
			rc = handle_single_msg(msg);
			break;

		case PCN_KMSG_TEST_SEND_PINGPONG:
			rc = handle_pingpong_msg(msg);
			break;

		case PCN_KMSG_TEST_SEND_BATCH:
			rc = handle_batch_msg(msg);
			break;

		case PCN_KMSG_TEST_SEND_BATCH_RESULT:
			rc = handle_batch_result_msg(msg);
			break;

		default:
			TEST_ERR("Operation %d not supported!\n", msg->op);
	}

	pcn_kmsg_free_msg(message);

	return rc;
}

// the following is TODO
static int pcn_kmsg_test_long_callback(struct pcn_kmsg_message *message)
{
	struct pcn_kmsg_long_message *lmsg =
		(struct pcn_kmsg_long_message *) message;

	TEST_PRINTK("Received test long message, payload: %s\n",
		    (char *) &lmsg->payload);

	pcn_kmsg_free_msg(message);

	return 0;
}

static int pcn_kmsg_register_handlers(void)
{
	int rc;

	TEST_PRINTK("Registering test callbacks!\n");

	rc = pcn_kmsg_register_callback(PCN_KMSG_TYPE_TEST,
					&pcn_kmsg_test_callback);
	if (rc) {
		TEST_ERR("Failed to register initial kmsg test callback! (%d) \n", rc);
	}

	rc = pcn_kmsg_register_callback(PCN_KMSG_TYPE_TEST_LONG,
					&pcn_kmsg_test_long_callback);
	if (rc) {
		TEST_ERR("Failed to register initial kmsg_test_long callback! (%d)\n", rc);
	}
	
	return rc;
}


#ifdef PCN_TEST_SYSCALL
///////////////////////////////////////////////////////////////////////////////
// syscall interface
///////////////////////////////////////////////////////////////////////////////

// REQUIRES KERNEL PATCHING, thus we moved to a dev driver, and ioctl interface
/* Syscall for testing all this stuff */
SYSCALL_DEFINE2(popcorn_test_kmsg, enum pcn_kmsg_test_op, op,
		struct pcn_kmsg_test_args __user *, args)
{
	int rc = 0;

// TODO this must be updated to use copy_[from|to]_user
	
	TEST_PRINTK("Reached test kmsg syscall, op %d, cpu %d\n",
		    op, args->cpu);

	switch (op) {
		case PCN_KMSG_TEST_SEND_SINGLE:
			rc = pcn_kmsg_test_send_single(args);
			break;

		case PCN_KMSG_TEST_SEND_PINGPONG:
			rc = pcn_kmsg_test_send_pingpong(args);
			break;

		case PCN_KMSG_TEST_SEND_BATCH:
			rc = pcn_kmsg_test_send_batch(args);
			break;

		case PCN_KMSG_TEST_SEND_LONG:
			rc = pcn_kmsg_test_long_msg(args);
			break;
#ifdef PCN_SUPPORT_MULTICAST
		case PCN_KMSG_TEST_OP_MCAST_OPEN:
			rc = pcn_kmsg_test_mcast_open(args);
			break;

		case PCN_KMSG_TEST_OP_MCAST_SEND:
			rc = pcn_kmsg_test_mcast_send(args);
			break;

		case PCN_KMSG_TEST_OP_MCAST_CLOSE:
			rc = pcn_kmsg_test_mcast_close(args);
			break;
#endif /* PCN_SUPPORT_MULTICAST */
			
		default:
			TEST_ERR("invalid option %d\n", op);
			return -1;
	}

	return rc;
}
#else /* ! SYSCALL */
///////////////////////////////////////////////////////////////////////////////
// dev driver interface
///////////////////////////////////////////////////////////////////////////////

#define FIRST_MINOR 0
#define MINOR_CNT 1
#define DEV_NAME "kmsg_test"
 
static int pcn_kmsg_test_open(struct inode *i, struct file *f)
{
    return 0;
}

static int pcn_kmsg_test_close(struct inode *i, struct file *f)
{
    return 0;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35))
static int pcn_kmsg_test_ioctl(struct inode *i, struct file *f, unsigned int cmd, unsigned long user_data)
#else
static long pcn_kmsg_test_ioctl(struct file *f, unsigned int cmd, unsigned long user_data)
#endif
{
	int rc;
    struct pcn_kmsg_test_args *args = kmalloc(sizeof(struct pcn_kmsg_test_args), GFP_KERNEL);
    
	if (args == 0)
		return -ENOMEM;
	
	rc = copy_from_user(args, (struct pcn_kmsg_test_args *) user_data, sizeof(struct pcn_kmsg_test_args));
	if (rc != 0) {
		TEST_ERR("some data cannot be copied from user %d\n", rc);
		return -ENODEV;
	}
	 
    switch (cmd)
    {
		case PCN_KMSG_TEST_SEND_SINGLE:
			rc = pcn_kmsg_test_send_single(args);
			break;

		case PCN_KMSG_TEST_SEND_PINGPONG:
			rc = pcn_kmsg_test_send_pingpong(args);
			break;

		case PCN_KMSG_TEST_SEND_BATCH:
			rc = pcn_kmsg_test_send_batch(args);
			break;

		case PCN_KMSG_TEST_SEND_LONG:
			rc = pcn_kmsg_test_long_msg(args);
			break;
#ifdef PCN_SUPPORT_MULTICAST
		case PCN_KMSG_TEST_OP_MCAST_OPEN:
			rc = pcn_kmsg_test_mcast_open(args);
			break;

		case PCN_KMSG_TEST_OP_MCAST_SEND:
			rc = pcn_kmsg_test_mcast_send(args);
			break;

		case PCN_KMSG_TEST_OP_MCAST_CLOSE:
			rc = pcn_kmsg_test_mcast_close(args);
			break;
#endif /* PCN_SUPPORT_MULTICAST */
			
		default:
			TEST_ERR("invalid option %d\n", cmd);
			rc = -1;
    }
    
    if (rc >= 0) {
		int __rc;
		__rc = copy_to_user((struct pcn_kmsg_test_args *) user_data, args, sizeof(struct pcn_kmsg_test_args));
		if (__rc != 0) {
			TEST_ERR("some data cannot be copied to user %d (but test succeded)\n", __rc);
			rc = -ENODEV;
		}
	}
    
    kfree(args); 
    return rc;
}
 
static struct file_operations pcn_kmsg_test_fops =
{
    .owner = THIS_MODULE,
    .open = pcn_kmsg_test_open,
    .release = pcn_kmsg_test_close,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35))
    .ioctl = pcn_kmsg_test_ioctl
#else
    .unlocked_ioctl = pcn_kmsg_test_ioctl
#endif
};

static dev_t dev;
static struct cdev c_dev;
static struct class *cl;

static int pcn_kmsg_test_devinit(void)
{
    int ret;
    struct device *dev_ret; 
 
	ret = alloc_chrdev_region(&dev, FIRST_MINOR, MINOR_CNT, DEV_NAME);
    if (ret < 0)
        return ret;
	TEST_PRINTK("%s registered major: %d\n", __func__, MAJOR(dev));
 
    cdev_init(&c_dev, &pcn_kmsg_test_fops);
	ret = cdev_add(&c_dev, dev, MINOR_CNT);
    if (ret < 0)
        return ret;
     
	cl = class_create(THIS_MODULE, "kipc");
    if (IS_ERR(cl)) {
		TEST_ERR("%s class_create error %ld\n", __func__, (long)cl);
        cdev_del(&c_dev);
        unregister_chrdev_region(dev, MINOR_CNT);
        return PTR_ERR(cl);
    }
    
    dev_ret = device_create(cl, NULL, dev, NULL, DEV_NAME);
    if (IS_ERR(dev_ret)) {
		TEST_ERR("%s device_create error %ld\n", __func__, (long)dev_ret);
		class_destroy(cl);
        cdev_del(&c_dev);
        unregister_chrdev_region(dev, MINOR_CNT);
        return PTR_ERR(dev_ret);
    }
 
    return 0;
}
 
static void pcn_kmsg_test_devexit(void)
{
    device_destroy(cl, dev);
    class_destroy(cl);
    cdev_del(&c_dev);
    unregister_chrdev_region(dev, MINOR_CNT);
}
#endif /* ! SYSCALL */

///////////////////////////////////////////////////////////////////////////////
// init fini
///////////////////////////////////////////////////////////////////////////////

static int __init pcn_kmsg_test_init(void)
{
	int rc;

#ifndef PCN_TEST_SYSCALL
	TEST_PRINTK("Registering control device /dev/%s\n", DEV_NAME);
	rc = pcn_kmsg_test_devinit();
	if (rc != 0) {
		TEST_ERR("Failed to register control device\n");
		return rc;
	}
#endif 

	// it registers to the messaging layer
	rc = pcn_kmsg_register_handlers();

	return rc;
}

static void __exit pcn_kmsg_test_exit(void)
{
#ifndef PCN_TEST_SYSCALL
	TEST_PRINTK("UnRegistering control device!\n");
	pcn_kmsg_test_devexit();
#endif
	
	TEST_PRINTK("UnRegistering test callbacks!\n");
	pcn_kmsg_unregister_callback(PCN_KMSG_TYPE_TEST);
	pcn_kmsg_unregister_callback(PCN_KMSG_TYPE_TEST_LONG);
}

#ifdef MODULE
module_init(pcn_kmsg_test_init);
module_exit(pcn_kmsg_test_exit);
#else
late_initcall(pcn_kmsg_test_init);
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Antonio Barbalace");
MODULE_DESCRIPTION("Testing module for Popcorn Linux messaging layer"); // TODO maybe want to rewrite
