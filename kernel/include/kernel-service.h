#ifndef _KERNEL_SERVICE_H_
#define _KERNEL_SERVICE_H_

#include <ros.h>

struct KernerServiceTable {
	// basic functions
	void (*cprintf)(const char*, ...);
	void (*panic)(const char*);
	void (*printf)(const char*,...);
	Address (*pgalloc)();
	void (*pgfree)(Address);
	Address (*kmalloc)(unsigned int);
	void (*kfree)(Address);
	Address krealloc(Address, unsigned int);
};

#endif