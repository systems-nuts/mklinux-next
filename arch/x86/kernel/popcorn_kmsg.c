

extern popcorn_kmsg_interupt_handler(struct pt_regs *regs, unsigned long long timestamp);


__visible void __irq_entry smp_popcorn_kmsg_interrupt(struct pt_regs *regs)
{
	unsigned long long timestamp = rdtsc(); //rdtsc_ordered()
	ack_APIC_irq();
	inc_irq_stat(irq_popcorn_kmsg_count);

	if (popcorn_kmsg_interrupt_handler)
		popcorn_kmsg_interrupt_handler(regs, timestamp);
	
	return;
}

