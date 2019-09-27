
// TODO to be moved as an external module (not compiled in kernel)

/*
 * Tests/benchmarks for Popcorn inter-kernel messaging layer
 *
 * Rewritten by Antonio Barbalace, Stevens 2019
 * 
 * (C) Ben Shelton <beshelto@vt.edu> 2013
 */

#include <linux/syscalls.h>

#include <linux/multikernel.h>
#include <linux/pcn_kmsg.h>
#include <linux/pcn_kmsg_test.h>

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

extern int my_cpu;

extern volatile unsigned long isr_ts, isr_ts_2, bh_ts, bh_ts_2;

///////////////////////////////////////////////////////////////////////////////
// test funcs
///////////////////////////////////////////////////////////////////////////////

static int pcn_kmsg_test_send_single(struct pcn_kmsg_test_args __user *args)
{
	int rc = 0;
	struct pcn_kmsg_test_message msg;
	unsigned long ts_start, ts_end;

	msg.hdr.type = PCN_KMSG_TYPE_TEST;
	msg.hdr.prio = PCN_KMSG_PRIO_HIGH;
	msg.op = PCN_KMSG_TEST_SEND_SINGLE;

	rdtsc(ts_start);

	pcn_kmsg_send(args->cpu, (struct pcn_kmsg_message *) &msg);

	rdtsc(ts_end);

	args->send_ts = ts_end - ts_start;

	return rc;
}

extern unsigned long int_ts;

static int pcn_kmsg_test_send_pingpong(struct pcn_kmsg_test_args __user *args)
{
	int rc = 0;
	struct pcn_kmsg_test_message msg;
	unsigned long tsc_init;

	msg.hdr.type = PCN_KMSG_TYPE_TEST;
	msg.hdr.prio = PCN_KMSG_PRIO_HIGH;
	msg.op = PCN_KMSG_TEST_SEND_PINGPONG;

	kmsg_done = 0;

	rdtsc(tsc_init);
	pcn_kmsg_send(args->cpu, (struct pcn_kmsg_message *) &msg);
	while (!kmsg_done) {}

	TEST_PRINTK("Elapsed time (ticks): %lu\n", kmsg_tsc - tsc_init);

	args->send_ts = tsc_init;
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

	rdtsc(batch_send_start_tsc);

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

	rdtsc(batch_send_end_tsc);

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

	rdtsc(start_ts);

	rc = pcn_kmsg_send_long(args->cpu, &lmsg, strlen(str) + 5);

	rdtsc(end_ts);

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

	rdtsc(ts_start);

	rc = pcn_kmsg_mcast_send(args->mcast_id,
				 (struct pcn_kmsg_message *) &msg);

	rdtsc(ts_end);

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
	TEST_PRINTK("Received single test message from CPU %d!\n",
		    msg->hdr.from_cpu);
	return 0;
}

static int handle_pingpong_msg(struct pcn_kmsg_test_message *msg)
{
	int rc = 0;
	unsigned long handler_ts;

	rdtsc(handler_ts);

	if (my_cpu) {

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
		rc = pcn_kmsg_send(0, (struct pcn_kmsg_message *) &reply_msg);

		if (rc) {
			TEST_ERR("Message send failed!\n");
			return -1;
		}

		isr_ts = isr_ts_2 = bh_ts = bh_ts_2 = 0;
	} else {
		TEST_PRINTK("Received ping-pong; reading end timestamp...\n");
		rdtsc(kmsg_tsc);
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

static int handle_batch_msg(struct pcn_kmsg_test_message *msg)
{
	int rc = 0;

	TEST_PRINTK("seqnum %lu size %lu\n", msg->batch_seqnum, 
		    msg->batch_size);

	if (msg->batch_seqnum == 0) {
		TEST_PRINTK("Start of batch; taking initial timestamp!\n");
		rdtsc(batch_start_tsc);

	}
	
	if (msg->batch_seqnum == (msg->batch_size - 1)) {
		/* send back reply */
		struct pcn_kmsg_test_message reply_msg;
		unsigned long batch_end_tsc;

		TEST_PRINTK("End of batch; sending back reply!\n");
		rdtsc(batch_end_tsc);

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

	rdtsc(kmsg_tsc);

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

static int pcn_kmsg_register_handlers()
{
	int rc;

	TEST_PRINTK("Registering test callbacks!\n");

	rc = pcn_kmsg_register_callback(PCN_KMSG_TYPE_TEST,
					&pcn_kmsg_test_callback);
	if (rc) {
		TEST_ERR("Failed to register initial kmsg test callback!\n");
	}

	rc = pcn_kmsg_register_callback(PCN_KMSG_TYPE_TEST_LONG,
					&pcn_kmsg_test_long_callback);
	if (rc) {
		TEST_ERR("Failed to register initial kmsg_test_long callback!\n");
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
#endif /* SYSCALL */

///////////////////////////////////////////////////////////////////////////////
// dev driver interface
///////////////////////////////////////////////////////////////////////////////

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <asm/uaccess.h>

#include <linux/ioctl.h>
 
typedef struct
{
    int status, dignity, ego;
} query_arg_t;
 
#define QUERY_GET_VARIABLES _IOR('q', 1, query_arg_t *)
#define QUERY_CLR_VARIABLES _IO('q', 2)
#define QUERY_SET_VARIABLES _IOW('q', 3, query_arg_t *)

#define FIRST_MINOR 0
#define MINOR_CNT 1
 
static dev_t dev;
static struct cdev c_dev;
static struct class *cl;
static int status = 1, dignity = 3, ego = 5;
 
static int my_open(struct inode *i, struct file *f)
{
    return 0;
}
static int my_close(struct inode *i, struct file *f)
{
    return 0;
}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35))
static int my_ioctl(struct inode *i, struct file *f, unsigned int cmd, unsigned long arg)
#else
static long my_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
#endif
{
    query_arg_t q;
 
    switch (cmd)
    {
        case QUERY_GET_VARIABLES:
            q.status = status;
            q.dignity = dignity;
            q.ego = ego;
            if (copy_to_user((query_arg_t *)arg, &q, sizeof(query_arg_t)))
            {
                return -EACCES;
            }
            break;
        case QUERY_CLR_VARIABLES:
            status = 0;
            dignity = 0;
            ego = 0;
            break;
        case QUERY_SET_VARIABLES:
            if (copy_from_user(&q, (query_arg_t *)arg, sizeof(query_arg_t)))
            {
                return -EACCES;
            }
            status = q.status;
            dignity = q.dignity;
            ego = q.ego;
            break;
        default:
            return -EINVAL;
    }
 
    return 0;
}
 
static struct file_operations query_fops =
{
    .owner = THIS_MODULE,
    .open = my_open,
    .release = my_close,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35))
    .ioctl = my_ioctl
#else
    .unlocked_ioctl = my_ioctl
#endif
};

int ret;
    struct device *dev_ret;
 
 
    if ((ret = alloc_chrdev_region(&dev, FIRST_MINOR, MINOR_CNT, "query_ioctl")) < 0)
    {
        return ret;
    }
 
    cdev_init(&c_dev, &query_fops);
 
    if ((ret = cdev_add(&c_dev, dev, MINOR_CNT)) < 0)
    {
        return ret;
    }
     
    if (IS_ERR(cl = class_create(THIS_MODULE, "char")))
    {
        cdev_del(&c_dev);
        unregister_chrdev_region(dev, MINOR_CNT);
        return PTR_ERR(cl);
    }
    if (IS_ERR(dev_ret = device_create(cl, NULL, dev, NULL, "query")))
    {
        class_destroy(cl);
        cdev_del(&c_dev);
        unregister_chrdev_region(dev, MINOR_CNT);
        return PTR_ERR(dev_ret);
    }
 
    return 0;
}
 
static void __exit query_ioctl_exit(void)
{
    device_destroy(cl, dev);
    class_destroy(cl);
    cdev_del(&c_dev);
    unregister_chrdev_region(dev, MINOR_CNT);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Antonio Barbalace");
MODULE_DESCRIPTION("Testing module for Popcorn Linux messaging layer"); // TODO


///////////////////////////////////////////////////////////////////////////////
// init fini
///////////////////////////////////////////////////////////////////////////////

static int __init pcn_kmsg_test_init(void)
{
	int rc;

	TEST_PRINTK("Registering test callbacks!\n");

	rc = pcn_kmsg_register_callback(PCN_KMSG_TYPE_TEST,
					&pcn_kmsg_test_callback);
	if (rc) {
		TEST_ERR("Failed to register initial kmsg test callback!\n");
	}

	rc = pcn_kmsg_register_callback(PCN_KMSG_TYPE_TEST_LONG,
					&pcn_kmsg_test_long_callback);
	if (rc) {
		TEST_ERR("Failed to register initial kmsg_test_long callback!\n");
	}

	return rc;
}

static void __init pcn_kmsg_test_exit(void)
{
	TEST_PRINTK("UnRegistering test callbacks!\n");

	pcn_kmsg_unregister_callback(PCN_KMSG_TYPE_TEST);
	pcn_kmsg_unregister_callback(PCN_KMSG_TYPE_TEST_LONG);
}

#define MODULE
#ifdef MODULE
module_init(pcn_kmsg_test_init);
module_exit(pcn_kmsg_test_exit);
#else
late_initcall(pcn_kmsg_test_init);
#endif


