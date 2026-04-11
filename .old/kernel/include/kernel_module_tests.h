/*
 * kernel_module_tests.h
 *
 * Boot-time kernel smoke harness for embeddable modules.
 *
 * The individual module test files live next to their subsystems, but the
 * boot logic only needs one entry point that starts a dedicated kernel thread.
 */
#ifndef KERNEL_MODULE_TESTS_H
#define KERNEL_MODULE_TESTS_H

 /* Spawn the one-shot kernel thread that runs module smoke tests. */
void kernel_module_tests_start(void);

#endif /* KERNEL_MODULE_TESTS_H */