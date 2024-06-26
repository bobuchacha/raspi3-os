#include "printf.h"
#include "syscall.h"
#include "user/lib.h"

#define ENTRYPOINT __attribute__((section(".entrypoint"))) void
long main();

/**
 * this has to be first of this file
*/
ENTRYPOINT _entrypoint(){
   call_sys_exit(main());
   while(1);
}

static int counter;

long main(){       
   
		int *ptr = (int*)0x6100;
      *ptr = 0xDEADACBE;
      call_sys_write("Here is my number: ");
      call_sys_malloc(*ptr);
      call_sys_write(" OK? \n");

      syscall(SYS_WRITE_NUMBER, "Hello World\n\n\n\n");

      char buff[1024];
      fprint((char*)&buff, "Hello Thang 0x%016lx.\n", 0x1234);
      syscall(SYS_WRITE_NUMBER, buff);

   return 0xDEAD;
	
}


