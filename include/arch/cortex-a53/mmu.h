#include "mmu/mair.h"
#include "mmu/pte.h"
#include "mmu/tcr.h"

// these properties will be passed to common code so we need to map according to our arch

// Memory Type: User Code
#define MT_USER_CODE    PE_USER_CODE        // user code    
#define MT_USER_DATA    PE_USER_DATA        // user data
#define MT_USER_RO      PE_USER_RO          // user read-only
#define MT_USER_RW      PE_USER_RW          // user read-write