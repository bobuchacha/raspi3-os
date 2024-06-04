#include "utils.h"
#include "printf.h"
#include "timer.h"
#include "device/raspi3b.h"

void invalid_entry() {
  // In case the UART isn't initialized
//   uart_init();
//   printf("[Error] Invalid exception occurred\n");

  // Load and decode the exception
  unsigned long esr, elr, spsr, far;
  asm volatile(
      "mrs %[esr], esr_el1\n\t"
      "mrs %[elr], elr_el1\n\t"
      "mrs %[spsr], spsr_el1\n\t"
      "mrs %[far], far_el1\n\t"
      : [esr] "=r"(esr), [elr] "=r"(elr), [spsr] "=r"(spsr), [far] "=r"(far)::);

  // Decode exception type (some, not all. See ARM DDI0487G_a chapter D13.2.37)
  switch (esr >> 26) {
    case 0b000000:
      printf("Unknown");
      break;
    case 0b000001:
      printf("Trapped WFI/WFE");
      break;
    case 0b001110:
      printf("Illegal execution");
      break;
    case 0b010101:
      printf("System call");
      break;
    case 0b100000:
      printf("Instruction abort, lower EL");
      break;
    case 0b100001:
      printf("Instruction abort, same EL");
      break;
    case 0b100010:
      printf("Instruction alignment fault");
      break;
    case 0b100100:
      printf("Data abort, lower EL");
      break;
    case 0b100101:
      printf("Data abort, same EL");
      break;
    case 0b100110:
      printf("Stack alignment fault");
      break;
    case 0b101100:
      printf("Floating point");
      break;
    default:
      printf("Unknown");
      break;
  }
  // Decode data abort cause
  if (esr >> 26 == 0b100100 || esr >> 26 == 0b100101) {
    printf(", ");
    switch ((esr >> 2) & 0x3) {
      case 0:
        printf("Address size fault");
        break;
      case 1:
        printf("Translation fault");
        break;
      case 2:
        printf("Access flag fault");
        break;
      case 3:
        printf("Permission fault");
        break;
    }
    switch (esr & 0x3) {
      case 0:
        printf(" at level 0");
        break;
      case 1:
        printf(" at level 1");
        break;
      case 2:
        printf(" at level 2");
        break;
      case 3:
        printf(" at level 3");
        break;
    }
  }

  // Dump registers
  printf(":\n  ESR_EL1 %lX", esr);
  printf(" ELR_EL1 %lX", elr);
  printf("\n SPSR_EL1 %lX", spsr);
  printf(" FAR_EL1 %lX", far);
  printf("\n");

  while (1) {
    // do nothing
  }
}

#include "utils.h"
#include "printf.h"
#include "timer.h"
#include "device/raspi3b.h"

const char *entry_error_messages[] = {
	"SYNC_INVALID_EL1t",
	"IRQ_INVALID_EL1t",		
	"FIQ_INVALID_EL1t",		
	"ERROR_INVALID_EL1T",		

	"SYNC_INVALID_EL1h",		
	"IRQ_INVALID_EL1h",		
	"FIQ_INVALID_EL1h",		
	"ERROR_INVALID_EL1h",		

	"SYNC_INVALID_EL0_64",		
	"IRQ_INVALID_EL0_64",		
	"FIQ_INVALID_EL0_64",		
	"ERROR_INVALID_EL0_64",	

	"SYNC_INVALID_EL0_32",		
	"IRQ_INVALID_EL0_32",		
	"FIQ_INVALID_EL0_32",		
	"ERROR_INVALID_EL0_32",

	"SYNC_ERROR",			// show this message
	"SYSCALL_ERROR"
};

void enable_interrupt_controller()
{
	put32(ENABLE_IRQS_1, SYSTEM_TIMER_IRQ_1);
}

void show_invalid_entry_message(int type, unsigned long esr, unsigned long address)
{
	kdebug("Type %d", type);
	printf("Exception: %s, ESR: %lx, address: %lx\r\n", entry_error_messages[type], esr, address);
}

void handle_irq(void)
{

	unsigned int irq = get32(IRQ_PENDING_1);
	switch (irq) {
		case (SYSTEM_TIMER_IRQ_1):
			handle_timer_irq();
			break;
		default:
			printf("Unknown pending irq: %x\r\n", irq);
	}
}
