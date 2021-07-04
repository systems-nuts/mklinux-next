/*
 * Antonio Barbalace, Stevens 2019
 * Tong Xing, Edinburgh
 */

#include <linux/init.h>

/*
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/spinlock.h>*/
#include <linux/export.h>
#include <linux/kernel_stat.h>

/*
#include <linux/mc146818rtc.h>
#include <linux/cache.h>
*/
#include <linux/interrupt.h>
#include <linux/cpu.h>
/*#include <linux/gfp.h>
*/

/*
#include <asm/mtrr.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
*/
/*#include <asm/nmi.h>
#include <asm/mce.h>
#include <asm/trace/irq_vectors.h>
#include <asm/kexec.h>
#include <asm/virtext.h>
*/


void (*popcorn_kmsg_interrupt_handler)(struct pt_regs *regs, unsigned long long timestamp) = 0;
EXPORT_SYMBOL(popcorn_kmsg_interrupt_handler);

