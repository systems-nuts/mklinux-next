/*
 * IPI latency measurement for Popcorn Kernel Messaging
 * Antonio Barbalace, Stevens 2019
 */

#include <linux/irq.h>
#include <linux/cpu.h>
#include <linux/interrupt.h>
#include <linux/smp.h>
#include <linux/syscalls.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/gfp.h>

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/proc_fs.h>

#include <asm/apic.h>
#include <asm/hardirq.h>
#include <asm/setup.h>
#include <asm/bootparam.h>
#include <asm/errno.h>
#include <asm/msr.h>
#include <asm/uaccess.h>
#include <asm/tsc.h>

//#include <asm/irq.h> //for x86_platform_ipi_callback

#ifdef ORDERED
 #define RDTSC rdtsc_ordered
#else
 #define RDTSC rdtsc
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Antonio Barbalace");

volatile int done; // this must be allocate on one or the other NUMA node
volatile unsigned long inc;

static int target_cpu;

/*
 * when you start the module you should indicate on which node you want to allocate the done variable
 */
static int test_numa_node=0;
module_param(test_numa_node,int,0660);

static unsigned long long tinterrupt[5];

extern void (*popcorn_kmsg_interrupt_handler)(struct pt_regs *regs, unsigned long long timestamp);

void __smp_popcorn_kmsg_interrupt(struct pt_regs *regs, unsigned long long ts)
{
	register unsigned long long tdone, tenter = RDTSC();
	//unsigned long flags;

	//local_irq_save(flags);
	//irq_enter();
	*(&done) = 1;
	mb();
	//irq_exit();
	//local_irq_restore(flags);
	tdone = RDTSC();
	
	// save the timestamps
	tinterrupt[3] = *(&inc);
	tinterrupt[0] = ts;
	tinterrupt[1] = tenter;
	tinterrupt[2] = tdone;
	tinterrupt[4] = *(&inc);
}

#define MAX_LOOP 1999999999

/*
 * returns the number of iterations if successful (< MAX_LOOP) or zero if error
 * i.e., the IPI didn't succeded; a negative value means that the cpu is offline
 */
inline static int __kmsg_ipi_test(unsigned long long *ts, int cpu)
{
	register unsigned long long tinit, tpdis, tsent, tpen, tfinish;
	//register unsigned long
	*(&inc)=0;

	if (cpu_is_offline(cpu)) {
		printk("%s: cpu %d is offline!\n", __func__, cpu);
		return -1;
	}
	
	*(&done) = 0;
	tinit = RDTSC();

	preempt_disable();
	tpdis = RDTSC();
	apic->send_IPI(cpu, POPCORN_KMSG_VECTOR);
	tsent = RDTSC();
	preempt_enable();
	tpen = RDTSC();
	
	while ((*(&done) == 0) && ((*(&inc))++ < MAX_LOOP) ) {};//busy waiting
	tfinish = RDTSC();

	if (ts) {
		ts[0] = tinit;
		ts[1] = tpdis;
		ts[2] = tsent;
		ts[3] = tpen;
		ts[4] = tfinish;
	}

	if (done)
		return inc;
	else
		return 0;
}

int kmsg_ipi_test(unsigned long long *timestamps, int cpu)
{
//	unsigned long flags;
	int ret = 0, prev_cpu, next_cpu;

	if (cpu > nr_cpu_ids) {
		printk("%s: cpu %d > nr_cpu_ids %d\n", __func__, cpu, nr_cpu_ids);
		return -1;
	}
	
	prev_cpu = smp_processor_id();
//	local_irq_save(flags);
	ret = __kmsg_ipi_test(timestamps, cpu);
//	local_irq_restore(flags);
	next_cpu = smp_processor_id();
	
	if (next_cpu != prev_cpu) {
		printk("%s: prev_cpu %d != next_cpu %d\n", __func__, prev_cpu, next_cpu);
		return -1;
	}
	
	return ret;
}


#define BUFSIZE 1024
static struct proc_dir_entry *ent;

/*
 * write to setup the target_cpu
 */ 
static ssize_t kmsg_ipi_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos) 
{
	int num,c,i;
	char buf[BUFSIZE];
	if(*ppos > 0 || count > BUFSIZE)
		return -EFAULT;
	if(copy_from_user(buf, ubuf, count))
		return -EFAULT;
	num = sscanf(buf,"%d", &i);
	if(num != 1)
		return -EFAULT;
	target_cpu = i; 
	c = strlen(buf);
	*ppos = c;
	return c;
}

/*
 * read to run the benchmark
 */
static ssize_t kmsg_ipi_read(struct file *file, char __user *ubuf,size_t count, loff_t *ppos) 
{
	char buf[BUFSIZE];
	int len=0, ret =-1;
	unsigned long long timestamps[5];

        if(*ppos > 0)
                return 0;

	// run benchmark
	ret = kmsg_ipi_test(timestamps, target_cpu);

	len += sprintf(buf,"current %d target %d\n", smp_processor_id(), target_cpu);
	if (ret < 0)
		len += sprintf(buf + len, "error cpu\n");
	else if (ret == 0)
		len += sprintf(buf + len, "error ipi\n");
	else {
#ifdef TIMESTAMPS
		len += sprintf(buf + len,"sender %lld %lld %lld %lld %lld (%d)\n",
						timestamps[0], timestamps[1], timestamps[2], timestamps[3], timestamps[4], ret);
		len += sprintf(buf + len,"inthnd %lld %lld %lld (%lld %lld)\n",
						tinterrupt[0], tinterrupt[1], tinterrupt[2], tinterrupt[3], tinterrupt[4]);
#else
                len += sprintf(buf + len,"sender %lld %lld %lld %lld (%d)\n",
                                                timestamps[1] - timestamps[0], timestamps[2] - timestamps[1],
						timestamps[3] - timestamps[2], timestamps[4] - timestamps[3], ret);
                len += sprintf(buf + len,"inthnd %lld %lld (%lld %lld)\n",
                                                tinterrupt[1] - tinterrupt[0], tinterrupt[2] - tinterrupt[1], tinterrupt[3], tinterrupt[4]);
#endif
	}

	if(copy_to_user(ubuf,buf,len))
		return -EFAULT;
	*ppos = len;
	return len;
}

static struct file_operations kmsg_ipi_ops = 
{
	.owner = THIS_MODULE,
	.read = kmsg_ipi_read,
	.write = kmsg_ipi_write,
};



static int kmsg_ipi_test_init(void)
{
	ent = proc_create("ipi_test", 0660, NULL, &kmsg_ipi_ops);
	printk(KERN_ALERT "kmsg_ipi_test registered /proc/kmsg_ipi_test\n");

	printk("kmsg_ipi_test cpu_khz %d tsc_khz %d\n", cpu_khz, tsc_khz);
	
	/* 
	 * an alternative is to use the x86_platform_ipi_callback defined in
	 * arch/x86/kernel/irq.c but it is not exported, thus need patching
	 */
	//if (x86_platform_ipi_callback == 0)
	//	printk(KERN_WARNING "can use x86_platform_ipi_callback\n");
	
	if (!popcorn_kmsg_interrupt_handler)
		popcorn_kmsg_interrupt_handler = __smp_popcorn_kmsg_interrupt;
	else {
		printk(KERN_ALERT "another module already registered\n");
		return -1;
	}
	
	return 0;
}
 
static void kmsg_ipi_test_cleanup(void)
{
	proc_remove(ent);
	printk(KERN_WARNING "kmsg_ipi_test deregistered /proc/kmsg_ipi_test\n");
	
	// cleaning up registration
	popcorn_kmsg_interrupt_handler = 0;
}
 
module_init(kmsg_ipi_test_init);
module_exit(kmsg_ipi_test_cleanup);
