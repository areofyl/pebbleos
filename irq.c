#include <stdint.h>

// called when an irq fires. figure out what it was and handle it

uint32_t gic_ack(void);
void gic_eoi(uint32_t iar);
void timer_reset(void);
extern volatile uint64_t tick_count;
void print(const char *s);
void print_hex(unsigned long val);

void irq_handler(unsigned long esr, unsigned long elr, unsigned long far) {
    // dont need these for irqs, theyre for sync exceptions
    (void)esr;
    (void)elr;
    (void)far;

    uint32_t iar = gic_ack();
    uint32_t intid = iar & 0x3FF;

    // spurious interrupt, just ignore
    if (intid == 1023)
        return;

    if (intid == 30) {
        // timer
        tick_count++;
        timer_reset();

        print("[tick] #");
        print_hex(tick_count);
        print("\n");
    } else {
        print("[irq] unexpected INTID: ");
        print_hex(intid);
        print("\n");
    }

    gic_eoi(iar);
}
