#include "ros.h"
#include "paging.h"
#include "memory.h"
#include "memory_helper.h"
#include "../hardware/uart/uart.h"
#include "../lib/string.h"

unsigned long PGD[512];        // 512 64-bit long table of PGD - each map to a PUD or a block of 1GB each
unsigned long PUD[512];        // the first PUD. each 64-bit maps to a block of 2MB. First entry of PGD Points to this one
unsigned long PMD[512];         // First PMD. each 64-bit points to a page which control 4K memory. First entry of PUD points here
unsigned long Pages[512];        // first page table. Each entry points to a physical memory. First entry of PMD points here.

// granularity
#define PT_PAGE     0b11        // 4k granule
#define PT_BLOCK    0b01        // 2M granule
// accessibility
#define PT_KERNEL   (0<<6)      // privileged, supervisor EL1 access only
#define PT_USER     (1<<6)      // unprivileged, EL0 access allowed
#define PT_RW       (0<<7)      // read-write
#define PT_RO       (1<<7)      // read-only
#define PT_AF       (1<<10)     // accessed flag
#define PT_NX       (1UL<<54)   // no execute
// shareability
#define PT_OSH      (2<<8)      // outter shareable
#define PT_ISH      (3<<8)      // inner shareable
// defined in MAIR register
#define PT_MEM      (0<<2)      // normal memory
#define PT_DEV      (1<<2)      // device MMIO
#define PT_NC       (2<<2)      // non-cachable

#define TTBR_CNP    1
#define PAGESIZE    4096

#define MT_NORMAL             0u
#define MT_NORMAL_NO_CACHING  2u
#define MT_DEVICE_NGNRNE      3u
#define MT_DEVICE_NGNRE       4u

#define PF_TYPE_TABLE      ((unsigned long)0b11 << 0)        // type is table descriptor
#define PF_TYPE_BLOCK      ((unsigned long)1 << 0)
#define PF_MEM_TYPE_NORMAL ((unsigned long)MT_NORMAL << 2)
#define PF_READ_WRITE      ((unsigned long)1 << 6)
#define PF_INNER_SHAREABLE ((unsigned long)3 << 8)
#define PF_ACCESS_FLAG     ((unsigned long)1 << 10)

static const unsigned NORMAL_MEMORY = 0xff;
static const unsigned NORMAL_MEMORY_NO_CACHING = 0x44;
static const unsigned DEVICE_NGNRNE = 0x00;
static const unsigned DEVICE_NGNRE = 0x04;

extern volatile unsigned char _data;
extern volatile unsigned char _end;

static unsigned long mair_attr(unsigned long attr, unsigned idx)
{
    return attr << (8 * idx);
}
void ttbr0_el1_store(unsigned long ttbr){
    asm volatile ("msr ttbr0_el1, %0" : : "r" (ttbr));
}
void mair_el1_store(unsigned long val){
    asm volatile ("msr mair_el1, %0" : : "r" (val));
}
unsigned long mair_el1_load(){
      unsigned long ret;
      asm volatile ("mrs %0, mair_el1" : : "r" (ret));
      return ret;
}

#define GET_PGD_ID(addr) (addr >> 39 & 0x1FF)    // get bit 39-47 of a number
#define GET_PUD_ID(addr) (addr >> 30 & 0x1FF) // get bit 30-38 of an address
#define GET_PMD_ID(addr) (addr >> 21 & 0x1FF) // get bit 21-29 of the address
#define GET_PAGE_ID(addr) (addr >> 12 & 0x1FF) // get bit 12-20 of the address
#define GET_FRAME_OFFSET(addr) (addr & 0xFFF)    // get first 12 bit 0-11
#define TRIM_ADDRESS_TO_LOWER_4K(addr) (addr & 0xFFFFFFFFFFFFF000ull)
#define GET_NEXT_TABLE_ADDRESS(ull) (ull & 0x1FFFFFF000ull)

#define TABLE_DESCRIPTOR_ATTR PF_TYPE_TABLE | \
PF_MEM_TYPE_NORMAL | \
PF_READ_WRITE | \
PF_INNER_SHAREABLE | \
PF_ACCESS_FLAG

#define BLOCK_DESCRIPTOR_ATTR PF_TYPE_BLOCK | \
PF_MEM_TYPE_NORMAL | \
PF_READ_WRITE | \
PF_INNER_SHAREABLE | \
PF_ACCESS_FLAG

/**
* map a virtual address into physical memory
*/
void memory_map_4k(unsigned long long va, unsigned long pa){
    // check if address are aligned
    // trim to lowest 4k frame so we can map correctly
    va = TRIM_ADDRESS_TO_LOWER_4K(va);
    pa = TRIM_ADDRESS_TO_LOWER_4K(pa);

    kdebug("memory_map_4k: mapping 0x%x to physical memory at 0x%x", va, pa);

    unsigned int pgdi = GET_PGD_ID(va);
    unsigned int pudi = GET_PUD_ID(va);
    unsigned int pmdi = GET_PMD_ID(va);
    unsigned int page_id = GET_PAGE_ID(va);

    printf("Mapping 0x%x\n", va);
    printf("PGD Index %d\n", pgdi);
    printf("PUD Index %d\n", pudi);
    printf("PMD Index %d\n", pmdi);
    printf("Page Index %d\n", page_id);

    // pgd extry must exist. read it
    unsigned long long pgd = PGD[pgdi];
    unsigned long *pud_base;
    // pdg should point to the base of pud. if it is empty, we haven't allocate the 1G address space PUD yet.
    // so we should create a PUD table and point pdg to the base of that table
    if (!pgd) {
        pud_base = mem_alloc_page();
        PGD[pgdi] = (unsigned long long)pud_base | TABLE_DESCRIPTOR_ATTR;
    }else{
        pud_base = (unsigned long*)GET_NEXT_TABLE_ADDRESS(pgd);
    }

    // next read PUD. We have PUD table at PGD[pgdi] or pgd
    unsigned long long pud = *(pud_base + pudi);        // read value of PUD entry at [base + index]
    unsigned long *pmd_base;
    //like wise, we check if pmd is allocated, if not, allocate it
    if (!pud){
        pmd_base = mem_alloc_page();
        *(pud_base + pudi) =  (unsigned long long)pmd_base | TABLE_DESCRIPTOR_ATTR;
    }else{
        pmd_base = (unsigned long*)GET_NEXT_TABLE_ADDRESS(pud);
    }

    // same as above, proceed to next level
    unsigned long long pmd = *(pmd_base + pmdi);        // read value of PUD entry at [base + index]
    unsigned long *page_base;
    if (!pmd){
        page_base = mem_alloc_page();
        *(pmd_base + pmdi) =  (unsigned long long)page_base | TABLE_DESCRIPTOR_ATTR;
    }else{
        page_base = (unsigned long*)GET_NEXT_TABLE_ADDRESS(pmd);
    }

    // now we got a page and page index. let's map it to physical address
    *((unsigned long long*)page_base + page_id) = pa | BLOCK_DESCRIPTOR_ATTR;
}

void enable_mmu(){

}

void memory_cpu_setup(void)
{
//    memory_map_4k(0xFFFF000000FF0000ull, 0x00FF0000);
    memory_map_4k(0xFFFFFFFFFFFF, 0x00FF0000);
    memcpy((unsigned char*)0xFF0000, "Welcome to Thang Cao", 20);
    //asm volatile("brk #0");

    unsigned long b, r;
    asm volatile ("mrs %0, id_aa64mmfr0_el1" : "=r" (r));
    b=r&0xF;
    if(r&(0xF<<28)/*4k*/ || b<1/*36 bits*/) {
        uart_puts("ERROR: 4k granule or 36 bit address space not supported\n");
        return;
    }

    const unsigned long mair =
        mair_attr(NORMAL_MEMORY, MT_NORMAL) |
        mair_attr(NORMAL_MEMORY_NO_CACHING, MT_NORMAL_NO_CACHING) |
        mair_attr(DEVICE_NGNRNE, MT_DEVICE_NGNRNE) |
        mair_attr(DEVICE_NGNRE, MT_DEVICE_NGNRE);

    mair_el1_store(mair);

    // next, specify mapping characteristics in translate control register
    r=  (0b00LL << 37) | // TBI=0, no tagging
        (b << 32) |      // IPS=autodetected
        (0b10LL << 30) | // TG1=4k
        (0b11LL << 28) | // SH1=3 inner
        (0b01LL << 26) | // ORGN1=1 write back
        (0b01LL << 24) | // IRGN1=1 write back
        (0b0LL  << 23) | // EPD1 enable higher half
        (25LL   << 16) | // T1SZ=25, 3 levels (512G)
        (0b00LL << 14) | // TG0=4k
        (0b11LL << 12) | // SH0=3 inner
        (0b01LL << 10) | // ORGN0=1 write back
        (0b01LL << 8) |  // IRGN0=1 write back
        (0b0LL  << 7) |  // EPD0 enable lower half
        (25LL   << 0);   // T0SZ=25, 3 levels (512G)
    asm volatile ("msr tcr_el1, %0; isb" : : "r" (r));

    // tell the MMU where our translation tables are. TTBR_CNP bit not documented, but required
    // lower half, user space
    asm volatile ("msr ttbr0_el1, %0" : : "r" ((unsigned long)PGD));
    // upper half, kernel space
    asm volatile ("msr ttbr1_el1, %0" : : "r" ((unsigned long)PGD));


    // finally, toggle some bits in system control register to enable page translation
    asm volatile ("dsb ish; isb; mrs %0, sctlr_el1" : "=r" (r));

    r|=0xC00800;     // set mandatory reserved bits
    r&=~((1<<25) |   // clear EE, little endian translation tables
         (1<<24) |   // clear E0E
         (1<<19) |   // clear WXN
         (1<<12) |   // clear I, no instruction cache
         (1<<4) |    // clear SA0
         (1<<3) |    // clear SA
         (1<<2) |    // clear C, no cache at all
         (1<<1));    // clear A, no aligment check
    r|=  (1<<0);     // set M, enable MMU
    asm volatile ("msr sctlr_el1, %0; isb" : : "r" (r));
    asm volatile("brk #0");

}

void paging_idmap_setup(void)
{
    static unsigned long idmap[64] __attribute__ ((__aligned__(512)));

    const unsigned long block_size = 0x40000000000ull;
    const unsigned long block_attr =
        PF_TYPE_BLOCK |
        PF_MEM_TYPE_NORMAL |
        PF_READ_WRITE |
        PF_INNER_SHAREABLE |
        PF_ACCESS_FLAG;
    unsigned long phys = 0;

    for (int i = 0; i < sizeof(idmap)/sizeof(idmap[0]); ++i) {
        idmap[i] = phys | block_attr;
        phys += block_size;
    }

    ttbr0_el1_store((unsigned long)idmap);
}

/**
* initialize virtual memory management
*/
void init_vmm(){
    kdebug("init_vmm: Initializing Virtual Memory Managemenr");

    memory_cpu_setup();
    paging_idmap_setup();

}

int get_PGD_index(unsigned long va){
    //return
}
/**
* dump a virtual address pointer to see what is physical address it points to
*/
void va_dump(void* va, unsigned long *page_table){

}
