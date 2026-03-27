#ifndef STACKTRACE_H
#define STACKTRACE_H

#ifndef __ASSEMBLER__

void dump_stack(void);
void dump_stack_from_frame(unsigned long frame_pointer, unsigned long stack_pointer);

#endif

#endif