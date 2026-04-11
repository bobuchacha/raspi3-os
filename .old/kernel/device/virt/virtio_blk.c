#include "device/virt.h"
#include "log.h"
#include "memory.h"
#include "ros.h"
#include "utils.h"

#define VIRTIO_MMIO_MAGIC_VALUE 0x000U
#define VIRTIO_MMIO_VERSION 0x004U
#define VIRTIO_MMIO_DEVICE_ID 0x008U
#define VIRTIO_MMIO_VENDOR_ID 0x00CU
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010U
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014U
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020U
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024U
#define VIRTIO_MMIO_GUEST_PAGE_SIZE 0x028U
#define VIRTIO_MMIO_QUEUE_SEL 0x030U
#define VIRTIO_MMIO_QUEUE_NUM_MAX 0x034U
#define VIRTIO_MMIO_QUEUE_NUM 0x038U
#define VIRTIO_MMIO_QUEUE_ALIGN 0x03CU
#define VIRTIO_MMIO_QUEUE_PFN 0x040U
#define VIRTIO_MMIO_QUEUE_READY 0x044U
#define VIRTIO_MMIO_QUEUE_NOTIFY 0x050U
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060U
#define VIRTIO_MMIO_INTERRUPT_ACK 0x064U
#define VIRTIO_MMIO_STATUS 0x070U
#define VIRTIO_MMIO_QUEUE_DESC_LOW 0x080U
#define VIRTIO_MMIO_QUEUE_DESC_HIGH 0x084U
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW 0x090U
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH 0x094U
#define VIRTIO_MMIO_QUEUE_USED_LOW 0x0A0U
#define VIRTIO_MMIO_QUEUE_USED_HIGH 0x0A4U

#define VIRTIO_MAGIC 0x74726976U
#define VIRTIO_VERSION_LEGACY 1U
#define VIRTIO_VERSION_MODERN 2U
#define VIRTIO_DEVICE_ID_BLOCK 2U
#define VIRTIO_VENDOR_QEMU 0x554D4551U

#define VIRTIO_STATUS_ACKNOWLEDGE 0x01U
#define VIRTIO_STATUS_DRIVER 0x02U
#define VIRTIO_STATUS_DRIVER_OK 0x04U
#define VIRTIO_STATUS_FEATURES_OK 0x08U
#define VIRTIO_STATUS_FAILED 0x80U

#define VIRTIO_F_VERSION_1 32U

#define VIRTQ_DESC_F_NEXT 1U
#define VIRTQ_DESC_F_WRITE 2U

#define VIRTIO_BLK_T_IN 0U
#define VIRTIO_BLK_T_OUT 1U

#define VIRTIO_BLK_STATUS_OK 0U
#define VIRTIO_BLK_STATUS_IOERR 1U

#define VIRTIO_BLK_SECTOR_SIZE 512U
#define VIRTIO_BLK_QUEUE_SIZE 8U
#define VIRTIO_BLK_MMIO_SLOTS 32U
#define VIRTIO_BLK_POLL_LIMIT 1000000U
#define VIRTIO_BLK_LEGACY_QUEUE_ALIGN 4096U
#define VIRTIO_BLK_LEGACY_QUEUE_BYTES (VIRTIO_BLK_LEGACY_QUEUE_ALIGN * 2U)

typedef struct __attribute__((packed)) VirtqDesc {
    ULong addr;
    UInt len;
    unsigned short flags;
    unsigned short next;
} VirtqDesc;

typedef struct __attribute__((packed)) VirtqAvail {
    unsigned short flags;
    unsigned short idx;
    unsigned short ring[VIRTIO_BLK_QUEUE_SIZE];
    unsigned short used_event;
} VirtqAvail;

typedef struct __attribute__((packed)) VirtqUsedElem {
    UInt id;
    UInt len;
} VirtqUsedElem;

typedef struct __attribute__((packed)) VirtqUsed {
    unsigned short flags;
    unsigned short idx;
    VirtqUsedElem ring[VIRTIO_BLK_QUEUE_SIZE];
    unsigned short avail_event;
} VirtqUsed;

typedef struct __attribute__((packed)) VirtioBlkReqHeader {
    UInt type;
    UInt reserved;
    ULong sector;
} VirtioBlkReqHeader;

static UInt virtio_blk_mmio_index;

static volatile unsigned int virtio_blk_spinlock;

static inline unsigned int virtio_blk_try_acquire(volatile unsigned int* lock) {
    unsigned int previous;
    unsigned int status;

    asm volatile(
        "ldaxr %w0, [%2]\n"
        "cbnz %w0, 1f\n"
        "mov %w0, #1\n"
        "stxr %w1, %w0, [%2]\n"
        "cbnz %w1, 2f\n"
        "mov %w0, wzr\n"
        "b 3f\n"
        "1:\n"
        "mov %w1, wzr\n"
        "2:\n"
        "3:\n"
        : "=&r"(previous), "=&r"(status)
        : "r"(lock)
        : "memory");

    return previous == 0U && status == 0U;
}

static inline void virtio_blk_lock(void) {
    while (!virtio_blk_try_acquire(&virtio_blk_spinlock)) {
        asm volatile("yield\n" ::: "memory");
    }
}

static inline void virtio_blk_unlock(void) {
    asm volatile(
        "stlr %w1, [%0]\n"
        :
    : "r"(&virtio_blk_spinlock), "r"(0U)
        : "memory");
}

static volatile UInt* virtio_blk_reg(UInt offset) {
    return (volatile UInt*)(VA_START + VIRT_VIRTIO_MMIO_BASE + (virtio_blk_mmio_index * VIRT_VIRTIO_MMIO_STRIDE) + offset);
}

static inline UInt virtio_blk_read_reg(UInt offset) {
    return *virtio_blk_reg(offset);
}

static inline void virtio_blk_write_reg(UInt offset, UInt value) {
    *virtio_blk_reg(offset) = value;
}

static void virtio_blk_barrier(void) {
    asm volatile("dmb ish" ::: "memory");
}

static void virtio_blk_write_addr(UInt low_offset, UInt high_offset, ULong address) {
    virtio_blk_write_reg(low_offset, (UInt)(address & 0xFFFFFFFFU));
    virtio_blk_write_reg(high_offset, (UInt)(address >> 32));
}

static struct {
    VirtqDesc desc[VIRTIO_BLK_QUEUE_SIZE];
} __attribute__((aligned(4096))) virtio_blk_desc_table;

static VirtqAvail __attribute__((aligned(4096))) virtio_blk_avail;
static VirtqUsed __attribute__((aligned(4096))) virtio_blk_used;
static UByte virtio_blk_legacy_queue[VIRTIO_BLK_LEGACY_QUEUE_BYTES] __attribute__((aligned(VIRTIO_BLK_LEGACY_QUEUE_ALIGN)));
static VirtioBlkReqHeader virtio_blk_request;
static volatile UByte virtio_blk_status_byte;
static Bool virtio_blk_ready;
static unsigned short virtio_blk_last_used_idx;
static UInt virtio_blk_version;
static VirtqDesc* virtio_blk_desc_ring = virtio_blk_desc_table.desc;
static VirtqAvail* virtio_blk_avail_ring = &virtio_blk_avail;
static VirtqUsed* virtio_blk_used_ring = &virtio_blk_used;

static Bool virtio_blk_uses_legacy_layout(void) {
    return virtio_blk_version == VIRTIO_VERSION_LEGACY;
}

static VirtqDesc* virtio_blk_active_desc_ring(void) {
    if (virtio_blk_uses_legacy_layout()) {
        return (VirtqDesc*)virtio_blk_legacy_queue;
    }

    return virtio_blk_desc_ring ? virtio_blk_desc_ring : virtio_blk_desc_table.desc;
}

static VirtqAvail* virtio_blk_active_avail_ring(void) {
    if (virtio_blk_uses_legacy_layout()) {
        return (VirtqAvail*)(virtio_blk_legacy_queue + (sizeof(VirtqDesc) * VIRTIO_BLK_QUEUE_SIZE));
    }

    return virtio_blk_avail_ring ? virtio_blk_avail_ring : &virtio_blk_avail;
}

static VirtqUsed* virtio_blk_active_used_ring(void) {
    if (virtio_blk_uses_legacy_layout()) {
        ULong avail_base = (ULong)(virtio_blk_legacy_queue + (sizeof(VirtqDesc) * VIRTIO_BLK_QUEUE_SIZE));
        ULong used_base = (avail_base + sizeof(VirtqAvail) + (VIRTIO_BLK_LEGACY_QUEUE_ALIGN - 1U)) & ~(ULong)(VIRTIO_BLK_LEGACY_QUEUE_ALIGN - 1U);

        return (VirtqUsed*)used_base;
    }

    return virtio_blk_used_ring ? virtio_blk_used_ring : &virtio_blk_used;
}

static int virtio_blk_submit(UInt type, UInt sector, void* buffer) {
    UInt polls = VIRTIO_BLK_POLL_LIMIT;
    int result = -1;
    VirtqDesc* desc_ring = virtio_blk_active_desc_ring();
    VirtqAvail* avail_ring = virtio_blk_active_avail_ring();
    VirtqUsed* used_ring = virtio_blk_active_used_ring();

    virtio_blk_lock();

    if (!desc_ring || !avail_ring || !used_ring) {
        goto done;
    }

    virtio_blk_desc_ring = desc_ring;
    virtio_blk_avail_ring = avail_ring;
    virtio_blk_used_ring = used_ring;

    virtio_blk_request.type = type;
    virtio_blk_request.reserved = 0;
    virtio_blk_request.sector = sector;
    virtio_blk_status_byte = 0xFFU;

    desc_ring[0].addr = mem_virt_to_phys((unsigned long)&virtio_blk_request);
    desc_ring[0].len = sizeof(virtio_blk_request);
    desc_ring[0].flags = VIRTQ_DESC_F_NEXT;
    desc_ring[0].next = 1;

    desc_ring[1].addr = mem_virt_to_phys((unsigned long)buffer);
    desc_ring[1].len = VIRTIO_BLK_SECTOR_SIZE;
    desc_ring[1].flags = VIRTQ_DESC_F_NEXT | (type == VIRTIO_BLK_T_IN ? VIRTQ_DESC_F_WRITE : 0U);
    desc_ring[1].next = 2;

    desc_ring[2].addr = mem_virt_to_phys((unsigned long)&virtio_blk_status_byte);
    desc_ring[2].len = sizeof(virtio_blk_status_byte);
    desc_ring[2].flags = VIRTQ_DESC_F_WRITE;
    desc_ring[2].next = 0;

    avail_ring->ring[avail_ring->idx % VIRTIO_BLK_QUEUE_SIZE] = 0;
    virtio_blk_barrier();
    avail_ring->idx++;
    virtio_blk_barrier();
    virtio_blk_write_reg(VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    while (polls-- > 0U) {
        virtio_blk_barrier();
        if (used_ring->idx != virtio_blk_last_used_idx) {
            virtio_blk_last_used_idx = used_ring->idx;
            virtio_blk_write_reg(VIRTIO_MMIO_INTERRUPT_ACK, virtio_blk_read_reg(VIRTIO_MMIO_INTERRUPT_STATUS));
            result = virtio_blk_status_byte == VIRTIO_BLK_STATUS_OK ? 0 : -1;
            goto done;
        }
    }

    virtio_blk_ready = false;
    log_error("virtio-blk: request timed out at sector %u", sector);

done:
    virtio_blk_unlock();
    return result;
}

int virtio_blk_init(void) {
    UInt features_lo;
    UInt features_hi;
    UInt status = 0;
    Bool found = false;

    virtio_blk_ready = false;
    virtio_blk_last_used_idx = 0;
    virtio_blk_desc_ring = virtio_blk_desc_table.desc;
    virtio_blk_avail_ring = &virtio_blk_avail;
    virtio_blk_used_ring = &virtio_blk_used;
    memset((void*)&virtio_blk_desc_table, 0, sizeof(virtio_blk_desc_table));
    memset((void*)&virtio_blk_avail, 0, sizeof(virtio_blk_avail));
    memset((void*)&virtio_blk_used, 0, sizeof(virtio_blk_used));
    memset((void*)virtio_blk_legacy_queue, 0, sizeof(virtio_blk_legacy_queue));

    for (virtio_blk_mmio_index = 0; virtio_blk_mmio_index < VIRTIO_BLK_MMIO_SLOTS; ++virtio_blk_mmio_index) {
        UInt version = virtio_blk_read_reg(VIRTIO_MMIO_VERSION);

        if (virtio_blk_read_reg(VIRTIO_MMIO_MAGIC_VALUE) == VIRTIO_MAGIC &&
            (version == VIRTIO_VERSION_LEGACY || version == VIRTIO_VERSION_MODERN) &&
            virtio_blk_read_reg(VIRTIO_MMIO_DEVICE_ID) == VIRTIO_DEVICE_ID_BLOCK) {
            virtio_blk_version = version;
            found = true;
            break;
        }
    }
    if (!found) {
        return -1;
    }
    if (virtio_blk_read_reg(VIRTIO_MMIO_VENDOR_ID) != VIRTIO_VENDOR_QEMU) {
        log_warning("virtio-blk: unexpected vendor 0x%X", virtio_blk_read_reg(VIRTIO_MMIO_VENDOR_ID));
    }

    virtio_blk_write_reg(VIRTIO_MMIO_STATUS, 0);
    status |= VIRTIO_STATUS_ACKNOWLEDGE;
    virtio_blk_write_reg(VIRTIO_MMIO_STATUS, status);
    status |= VIRTIO_STATUS_DRIVER;
    virtio_blk_write_reg(VIRTIO_MMIO_STATUS, status);

    if (virtio_blk_version == VIRTIO_VERSION_MODERN) {
        virtio_blk_write_reg(VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0);
        features_lo = virtio_blk_read_reg(VIRTIO_MMIO_DEVICE_FEATURES);
        virtio_blk_write_reg(VIRTIO_MMIO_DEVICE_FEATURES_SEL, 1);
        features_hi = virtio_blk_read_reg(VIRTIO_MMIO_DEVICE_FEATURES);
        if ((features_hi & (1U << (VIRTIO_F_VERSION_1 - 32U))) == 0U) {
            log_error("virtio-blk: device does not offer VERSION_1");
            virtio_blk_write_reg(VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_FAILED);
            return -1;
        }

        virtio_blk_write_reg(VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
        virtio_blk_write_reg(VIRTIO_MMIO_DRIVER_FEATURES, 0);
        virtio_blk_write_reg(VIRTIO_MMIO_DRIVER_FEATURES_SEL, 1);
        virtio_blk_write_reg(VIRTIO_MMIO_DRIVER_FEATURES, (1U << (VIRTIO_F_VERSION_1 - 32U)));
        (void)features_lo;

        status |= VIRTIO_STATUS_FEATURES_OK;
        virtio_blk_write_reg(VIRTIO_MMIO_STATUS, status);
        if ((virtio_blk_read_reg(VIRTIO_MMIO_STATUS) & VIRTIO_STATUS_FEATURES_OK) == 0U) {
            log_error("virtio-blk: feature negotiation rejected by device");
            virtio_blk_write_reg(VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_FAILED);
            return -1;
        }
    }

    virtio_blk_write_reg(VIRTIO_MMIO_QUEUE_SEL, 0);
    if (virtio_blk_read_reg(VIRTIO_MMIO_QUEUE_NUM_MAX) < VIRTIO_BLK_QUEUE_SIZE) {
        log_error("virtio-blk: queue too small");
        virtio_blk_write_reg(VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_FAILED);
        return -1;
    }

    virtio_blk_write_reg(VIRTIO_MMIO_QUEUE_NUM, VIRTIO_BLK_QUEUE_SIZE);
    if (virtio_blk_version == VIRTIO_VERSION_MODERN) {
        virtio_blk_write_addr(VIRTIO_MMIO_QUEUE_DESC_LOW, VIRTIO_MMIO_QUEUE_DESC_HIGH,
            mem_virt_to_phys((unsigned long)&virtio_blk_desc_table));
        virtio_blk_write_addr(VIRTIO_MMIO_QUEUE_AVAIL_LOW, VIRTIO_MMIO_QUEUE_AVAIL_HIGH,
            mem_virt_to_phys((unsigned long)&virtio_blk_avail));
        virtio_blk_write_addr(VIRTIO_MMIO_QUEUE_USED_LOW, VIRTIO_MMIO_QUEUE_USED_HIGH,
            mem_virt_to_phys((unsigned long)&virtio_blk_used));
        virtio_blk_write_reg(VIRTIO_MMIO_QUEUE_READY, 1);
    }
    else {
        unsigned long queue_base = (unsigned long)virtio_blk_legacy_queue;
        unsigned long desc_base = queue_base;
        unsigned long avail_base = desc_base + (sizeof(VirtqDesc) * VIRTIO_BLK_QUEUE_SIZE);
        unsigned long used_base = (avail_base + sizeof(VirtqAvail) + (VIRTIO_BLK_LEGACY_QUEUE_ALIGN - 1U)) & ~(VIRTIO_BLK_LEGACY_QUEUE_ALIGN - 1U);

        virtio_blk_desc_ring = (VirtqDesc*)desc_base;
        virtio_blk_avail_ring = (VirtqAvail*)avail_base;
        virtio_blk_used_ring = (VirtqUsed*)used_base;
        memset((void*)virtio_blk_desc_ring, 0, sizeof(VirtqDesc) * VIRTIO_BLK_QUEUE_SIZE);
        memset((void*)virtio_blk_avail_ring, 0, sizeof(VirtqAvail));
        memset((void*)virtio_blk_used_ring, 0, sizeof(VirtqUsed));
        virtio_blk_write_reg(VIRTIO_MMIO_GUEST_PAGE_SIZE, VIRTIO_BLK_LEGACY_QUEUE_ALIGN);
        virtio_blk_write_reg(VIRTIO_MMIO_QUEUE_ALIGN, VIRTIO_BLK_LEGACY_QUEUE_ALIGN);
        virtio_blk_write_reg(VIRTIO_MMIO_QUEUE_PFN, (UInt)(mem_virt_to_phys(queue_base) / VIRTIO_BLK_LEGACY_QUEUE_ALIGN));
    }

    status |= VIRTIO_STATUS_DRIVER_OK;
    virtio_blk_write_reg(VIRTIO_MMIO_STATUS, status);
    virtio_blk_ready = true;
    log_info("virt board: virtio-blk MMIO device ready on slot %u version %u", virtio_blk_mmio_index, virtio_blk_version);
    return 0;
}

int virtio_blk_read(void* private, unsigned int begin, int count, void* buf) {
    UByte* cursor = (UByte*)buf;

    (void)private;
    if (!virtio_blk_ready || !buf || count <= 0) {
        return ERROR_INVAILD;
    }

    for (int index = 0; index < count; ++index) {
        if (virtio_blk_submit(VIRTIO_BLK_T_IN, begin + (UInt)index, cursor + (index * VIRTIO_BLK_SECTOR_SIZE)) != 0) {
            return ERROR_INVAILD;
        }
    }
    return count * (int)VIRTIO_BLK_SECTOR_SIZE;
}

int virtio_blk_write(void* private, unsigned int begin, int count, const void* buf) {
    const UByte* cursor = (const UByte*)buf;

    (void)private;
    if (!virtio_blk_ready || !buf || count <= 0) {
        return ERROR_INVAILD;
    }

    for (int index = 0; index < count; ++index) {
        if (virtio_blk_submit(VIRTIO_BLK_T_OUT, begin + (UInt)index, (void*)(cursor + (index * VIRTIO_BLK_SECTOR_SIZE))) != 0) {
            return ERROR_INVAILD;
        }
    }
    return count * (int)VIRTIO_BLK_SECTOR_SIZE;
}

Bool virtio_blk_is_ready(void) {
    return virtio_blk_ready;
}