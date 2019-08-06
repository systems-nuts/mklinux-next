/*
 * IPI latency measurement for Popcorn Kernel Messaging
 */

#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/smp.h>
#include <linux/syscalls.h>
#include <linux/kernel.h>
#include <linux/multikernel.h>
#include <linux/list.h>
#include <linux/slab.h>

#include <asm/system.h>
#include <asm/apic.h>
#include <asm/hardirq.h>
#include <asm/setup.h>
#include <asm/bootparam.h>
#include <asm/errno.h>
#include <asm/msr.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Antonio Barbalace");

volatile int done; // this must be allocate on one or the other NUMA node

static int target_cpu;

/*
 * when you start the module you should indicate on which node you want to allocate the done variable
 */
static int numa_node=0;
module_param(numa_node,int,0660);

static void __smp_popcorn_kmsg_interrupt(struct pt_regs *regs, unsigned long long ts)
{
	done = 1;
}

#if 0
static unsigned long calculate_tsc_overhead(void)
{
	unsigned long t0, t1, overhead = ~0UL;
	int i;

	for (i = 0; i < 1000; i++) {
		rdtscll(t0);
		asm volatile("");
		rdtscll(t1);
		if (t1 - t0 < overhead)
			overhead = t1 - t0;
	}

	printk("tsc overhead is %ld\n", overhead);

	return overhead;
}
#endif

inline static int __kmsg_ipi_test(unsigned long long *ts, int cpu)
{
	register unsigned long long tinit, tsent, tfinish;

	done = 0;
	tinit = rdtsc();
	
	apic->send_IPI_mask(cpumask_of(cpu), POPCORN_KMSG_INTERRUPT_VECTOR);
	tsent = rdtsc();
	
	while (!done) {} //busy waiting
	tfinish = rdtsc();

	if (ts) {
		timers[0] = tinit;
		timers[1] = tsent;
		timers[2] = tfinish;
		return 3;
	}
		
	return 0;
}

int kmsg_ipi_test(unsigned long long *timestamps, int cpu)
{
	unsigned long flags;
	int ret = 0;

	local_irq_save(flags);
	ret = kmsg_ipi_test(timestamps, cpu);
	local_irq_restore(flags);

	return ret;
}


#define BUFSIZE 4096
static struct proc_dir_entry *ent;

/*
 * write to setup the target _cpu
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
	int len=0;
	unsigned long long timestamps[3];
	
	kmsg_ipi_test(&timestamps, target_cpu);
	
	if(*ppos > 0 || count < BUFSIZE)
		return 0;
	len += sprintf(buf,"current = %d target = %d\n", smp_processor_id(), target_cpu);
	len += sprintf(buf + len,"init = %lld sent = %lld finish = %lld\n", timestamps[0], timestamps[1], timestamps[2]);
	
	if(copy_to_user(ubuf,buf,len))
		return -EFAULT;
	*ppos = len;
	return len;
}

static struct file_operations kmsg_ipi_ops = 
{
	.owner = THIS_MODULE,
	.read = kmsg_ipi_read,
	.write = kmsg_ipi_mywrite,
};

static int kmsg_ipi_test_init(void)
{
	ent = proc_create("ipi_test", 0660, NULL, &kmsg_ipi_ops);
	printk(KERN_ALERT "kmsg_ipi_test registered /proc/kmsg_ipi_test\n");
	
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
