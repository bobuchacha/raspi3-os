#include "block_device.h"

#include "console.h"

#if defined(BOARD_VIRT)

#define VIRTIO_MMIO_BASE 0x0A000000UL
#define VIRTIO_MMIO_STRIDE 0x200UL
#define VIRTIO_MMIO_MAGIC_VALUE 0x000U
#define VIRTIO_MMIO_VERSION 0x004U
#define VIRTIO_MMIO_DEVICE_ID 0x008U
#define VIRTIO_MMIO_VENDOR_ID 0x00CU
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010U
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014U
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020U
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024U
#define VIRTIO_MMIO_QUEUE_SEL 0x030U
#define VIRTIO_MMIO_QUEUE_NUM_MAX 0x034U
#define VIRTIO_MMIO_QUEUE_NUM 0x038U
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
#define VIRTIO_VERSION_1 2U
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
#define VIRTIO_BLK_STATUS_OK 0U
#define VIRTIO_BLK_SECTOR_SIZE 512U
#define VIRTIO_BLK_QUEUE_SIZE 8U
#define VIRTIO_BLK_SLOT_COUNT 32U
#define VIRTIO_BLK_POLL_LIMIT 1000000U

typedef struct __attribute__((packed)) VirtqDesc {
    U64 addr;
    U32 len;
    U16 flags;
    U16 next;
} VirtqDesc;

typedef struct __attribute__((packed)) VirtqAvail {
    U16 flags;
    U16 idx;
    U16 ring[VIRTIO_BLK_QUEUE_SIZE];
    U16 used_event;
} VirtqAvail;

typedef struct __attribute__((packed)) VirtqUsedElem {
    U32 id;
    U32 len;
} VirtqUsedElem;

typedef struct __attribute__((packed)) VirtqUsed {
    U16 flags;
    U16 idx;
    VirtqUsedElem ring[VIRTIO_BLK_QUEUE_SIZE];
    U16 avail_event;
} VirtqUsed;

typedef struct __attribute__((packed)) VirtioBlkRequest {
    U32 type;
    U32 reserved;
    U64 sector;
} VirtioBlkRequest;

static struct {
    VirtqDesc entries[VIRTIO_BLK_QUEUE_SIZE];
} __attribute__((aligned(4096))) g_desc_table;

static VirtqAvail g_avail __attribute__((aligned(4096)));
static VirtqUsed g_used __attribute__((aligned(4096)));
static VirtioBlkRequest g_request;
static volatile U8 g_status_byte;
static U16 g_last_used_idx;
static U32 g_slot_index;
static bool g_block_ready;

static volatile U32* boot_block_reg(U32 offset) {
    return (volatile U32*)(VIRTIO_MMIO_BASE + (g_slot_index * VIRTIO_MMIO_STRIDE) + offset);
}

static U32 boot_block_read_reg(U32 offset) {
    return *boot_block_reg(offset);
}

static void boot_block_write_reg(U32 offset, U32 value) {
    *boot_block_reg(offset) = value;
}

static void boot_block_write_addr(U32 low_offset, U32 high_offset, U64 address) {
    boot_block_write_reg(low_offset, (U32)(address & 0xffffffffU));
    boot_block_write_reg(high_offset, (U32)(address >> 32));
}

static void boot_block_barrier(void) {
    __asm__ volatile("dsb sy" ::: "memory");
}

static Status boot_block_find_device(void) {
    for (g_slot_index = 0; g_slot_index < VIRTIO_BLK_SLOT_COUNT; ++g_slot_index) {
        if ((boot_block_read_reg(VIRTIO_MMIO_MAGIC_VALUE) == VIRTIO_MAGIC) &&
            (boot_block_read_reg(VIRTIO_MMIO_VERSION) == VIRTIO_VERSION_1) &&
            (boot_block_read_reg(VIRTIO_MMIO_DEVICE_ID) == VIRTIO_DEVICE_ID_BLOCK)) {
            return StatusOK;
        }
    }

    return StatusNotFound;
}

static Status boot_block_submit(U32 sector, void* buffer) {
    U32 polls = VIRTIO_BLK_POLL_LIMIT;
    volatile U8* used_index_bytes = ((volatile U8*)&g_used) + sizeof(U16);
    volatile U8* status_byte = &g_status_byte;

    g_request.type = VIRTIO_BLK_T_IN;
    g_request.reserved = 0U;
    g_request.sector = sector;
    g_status_byte = 0xffU;

    g_desc_table.entries[0].addr = (U64)(Uptr)&g_request;
    g_desc_table.entries[0].len = sizeof(g_request);
    g_desc_table.entries[0].flags = VIRTQ_DESC_F_NEXT;
    g_desc_table.entries[0].next = 1U;

    g_desc_table.entries[1].addr = (U64)(Uptr)buffer;
    g_desc_table.entries[1].len = VIRTIO_BLK_SECTOR_SIZE;
    g_desc_table.entries[1].flags = VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE;
    g_desc_table.entries[1].next = 2U;

    g_desc_table.entries[2].addr = (U64)(Uptr)&g_status_byte;
    g_desc_table.entries[2].len = sizeof(g_status_byte);
    g_desc_table.entries[2].flags = VIRTQ_DESC_F_WRITE;
    g_desc_table.entries[2].next = 0U;

    g_avail.ring[g_avail.idx % VIRTIO_BLK_QUEUE_SIZE] = 0U;
    boot_block_barrier();
    g_avail.idx++;
    boot_block_barrier();
    boot_block_write_reg(VIRTIO_MMIO_QUEUE_NOTIFY, 0U);

    while (polls-- > 0U) {
        U16 used_index;

        boot_block_barrier();
        used_index = (U16)used_index_bytes[0] | ((U16)used_index_bytes[1] << 8);
        if (used_index != g_last_used_idx) {
            g_last_used_idx = used_index;
            boot_block_write_reg(VIRTIO_MMIO_INTERRUPT_ACK, boot_block_read_reg(VIRTIO_MMIO_INTERRUPT_STATUS));
            return (*status_byte == VIRTIO_BLK_STATUS_OK) ? StatusOK : StatusIoError;
        }
    }

    g_block_ready = false;
    boot_console_puts("[bootloader] virtio-blk request timed out\n");
    return StatusBusy;
}

Status boot_block_init(void) {
    U32 status = 0U;

    memzero(&g_desc_table, sizeof(g_desc_table));
    memzero(&g_avail, sizeof(g_avail));
    memzero(&g_used, sizeof(g_used));
    g_last_used_idx = 0U;
    g_block_ready = false;

    if (boot_block_find_device() != StatusOK) {
        return StatusNotFound;
    }
    if (boot_block_read_reg(VIRTIO_MMIO_VENDOR_ID) != VIRTIO_VENDOR_QEMU) {
        return StatusNotSupported;
    }

    boot_block_write_reg(VIRTIO_MMIO_STATUS, 0U);
    status |= VIRTIO_STATUS_ACKNOWLEDGE;
    boot_block_write_reg(VIRTIO_MMIO_STATUS, status);
    status |= VIRTIO_STATUS_DRIVER;
    boot_block_write_reg(VIRTIO_MMIO_STATUS, status);

    boot_block_write_reg(VIRTIO_MMIO_DEVICE_FEATURES_SEL, 1U);
    if ((boot_block_read_reg(VIRTIO_MMIO_DEVICE_FEATURES) & (1U << (VIRTIO_F_VERSION_1 - 32U))) == 0U) {
        boot_block_write_reg(VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_FAILED);
        return StatusNotSupported;
    }

    boot_block_write_reg(VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0U);
    boot_block_write_reg(VIRTIO_MMIO_DRIVER_FEATURES, 0U);
    boot_block_write_reg(VIRTIO_MMIO_DRIVER_FEATURES_SEL, 1U);
    boot_block_write_reg(VIRTIO_MMIO_DRIVER_FEATURES, (1U << (VIRTIO_F_VERSION_1 - 32U)));

    status |= VIRTIO_STATUS_FEATURES_OK;
    boot_block_write_reg(VIRTIO_MMIO_STATUS, status);
    if ((boot_block_read_reg(VIRTIO_MMIO_STATUS) & VIRTIO_STATUS_FEATURES_OK) == 0U) {
        boot_block_write_reg(VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_FAILED);
        return StatusNotSupported;
    }

    boot_block_write_reg(VIRTIO_MMIO_QUEUE_SEL, 0U);
    if (boot_block_read_reg(VIRTIO_MMIO_QUEUE_NUM_MAX) < VIRTIO_BLK_QUEUE_SIZE) {
        boot_block_write_reg(VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_FAILED);
        return StatusNoSpace;
    }

    boot_block_write_reg(VIRTIO_MMIO_QUEUE_NUM, VIRTIO_BLK_QUEUE_SIZE);
    boot_block_write_addr(VIRTIO_MMIO_QUEUE_DESC_LOW, VIRTIO_MMIO_QUEUE_DESC_HIGH, (U64)(Uptr)&g_desc_table);
    boot_block_write_addr(VIRTIO_MMIO_QUEUE_AVAIL_LOW, VIRTIO_MMIO_QUEUE_AVAIL_HIGH, (U64)(Uptr)&g_avail);
    boot_block_write_addr(VIRTIO_MMIO_QUEUE_USED_LOW, VIRTIO_MMIO_QUEUE_USED_HIGH, (U64)(Uptr)&g_used);
    boot_block_write_reg(VIRTIO_MMIO_QUEUE_READY, 1U);
    boot_block_barrier();

    status |= VIRTIO_STATUS_DRIVER_OK;
    boot_block_write_reg(VIRTIO_MMIO_STATUS, status);
    g_block_ready = true;
    return StatusOK;
}

Status boot_block_read(U32 lba, U32 sector_count, void* buffer) {
    U8* bytes = (U8*)buffer;
    U32 index;

    if ((!g_block_ready) || (buffer == NULL) || (sector_count == 0U)) {
        return StatusInvalidArgument;
    }

    for (index = 0; index < sector_count; ++index) {
        Status status = boot_block_submit(lba + index, bytes + ((Size)index * VIRTIO_BLK_SECTOR_SIZE));

        if (status != StatusOK) {
            return status;
        }
    }

    return StatusOK;
}

bool boot_block_is_ready(void) {
    return g_block_ready;
}

#else

Status boot_block_init(void) {
    return StatusNotSupported;
}

Status boot_block_read(U32 lba, U32 sector_count, void* buffer) {
    (void)lba;
    (void)sector_count;
    (void)buffer;
    return StatusNotSupported;
}

bool boot_block_is_ready(void) {
    return false;
}

#endif