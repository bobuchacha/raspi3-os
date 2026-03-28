#define LOG_ENABLE_TRACE 0

#include "device/raspi3b.h"
#include "device/sd.h"
#include "printf.h"
#include "log.h"
#include "timer.h"

#define MMIO_BASE PBASE
#define EMMC_ARG2 ((volatile unsigned int*)(MMIO_BASE + 0x00300000))
#define EMMC_BLKSIZECNT ((volatile unsigned int*)(MMIO_BASE + 0x00300004))
#define EMMC_ARG1 ((volatile unsigned int*)(MMIO_BASE + 0x00300008))
#define EMMC_CMDTM ((volatile unsigned int*)(MMIO_BASE + 0x0030000C))
#define EMMC_RESP0 ((volatile unsigned int*)(MMIO_BASE + 0x00300010))
#define EMMC_RESP1 ((volatile unsigned int*)(MMIO_BASE + 0x00300014))
#define EMMC_RESP2 ((volatile unsigned int*)(MMIO_BASE + 0x00300018))
#define EMMC_RESP3 ((volatile unsigned int*)(MMIO_BASE + 0x0030001C))
#define EMMC_DATA ((volatile unsigned int*)(MMIO_BASE + 0x00300020))
#define EMMC_STATUS ((volatile unsigned int*)(MMIO_BASE + 0x00300024))
#define EMMC_CONTROL0 ((volatile unsigned int*)(MMIO_BASE + 0x00300028))
#define EMMC_CONTROL1 ((volatile unsigned int*)(MMIO_BASE + 0x0030002C))
#define EMMC_INTERRUPT ((volatile unsigned int*)(MMIO_BASE + 0x00300030))
#define EMMC_INT_MASK ((volatile unsigned int*)(MMIO_BASE + 0x00300034))
#define EMMC_INT_EN ((volatile unsigned int*)(MMIO_BASE + 0x00300038))
#define EMMC_SLOTISR_VER ((volatile unsigned int*)(MMIO_BASE + 0x003000FC))

#define CMD_NEED_APP 0x80000000
#define CMD_RSPNS_48 0x00020000
#define CMD_ERRORS_MASK 0xfff9c004
#define CMD_RCA_MASK 0xffff0000

#define CMD_GO_IDLE 0x00000000
#define CMD_ALL_SEND_CID 0x02010000
#define CMD_SEND_REL_ADDR 0x03020000
#define CMD_CARD_SELECT 0x07030000
#define CMD_SEND_IF_COND 0x08020000
#define CMD_STOP_TRANS 0x0C030000
#define CMD_READ_SINGLE 0x11220010
#define CMD_READ_MULTI 0x12220032
#define CMD_SET_BLOCKCNT 0x17020000
#define CMD_WRITE_SINGLE 0x18220000
#define CMD_WRITE_MULTI 0x19220022
#define CMD_APP_CMD 0x37000000
#define CMD_SET_BUS_WIDTH (0x06020000 | CMD_NEED_APP)
#define CMD_SEND_OP_COND (0x29020000 | CMD_NEED_APP)
#define CMD_SEND_SCR (0x33220010 | CMD_NEED_APP)

#define SR_READ_AVAILABLE 0x00000800
#define SR_WRITE_AVAILABLE 0x00000400
#define SR_DAT_INHIBIT 0x00000002
#define SR_CMD_INHIBIT 0x00000001
#define SR_APP_CMD 0x00000020

#define INT_DATA_TIMEOUT 0x00100000
#define INT_CMD_TIMEOUT 0x00010000
#define INT_READ_RDY 0x00000020
#define INT_WRITE_RDY 0x00000010
#define INT_DATA_DONE 0x00000002
#define INT_CMD_DONE 0x00000001
#define INT_ERROR_MASK 0x017E8000

#define C0_HCTL_HS_EN 0x00000004
#define C0_HCTL_DWITDH 0x00000002

#define C1_SRST_HC 0x01000000
#define C1_TOUNIT_MAX 0x000e0000
#define C1_CLK_GENSEL 0x00000020
#define C1_CLK_EN 0x00000004
#define C1_CLK_STABLE 0x00000002
#define C1_CLK_INTLEN 0x00000001

#define HOST_SPEC_NUM 0x00ff0000
#define HOST_SPEC_NUM_SHIFT 16
#define HOST_SPEC_V2 1

#define SCR_SD_BUS_WIDTH_4 0x00000400
#define SCR_SUPP_SET_BLKCNT 0x02000000
#define SCR_SUPP_CCS 0x00000001

#define ACMD41_CMD_COMPLETE 0x80000000
#define ACMD41_VOLTAGE 0x00ff8000
#define ACMD41_CMD_CCS 0x40000000
#define ACMD41_ARG_HC 0x51ff8000

unsigned long sd_scr[2], sd_ocr, sd_rca, sd_err, sd_hv;
static Bool sd_initialized;

void sd_fill_handoff(RosBootHandoff* handoff) {
    if (!handoff) {
        return;
    }

    handoff->sd_scr[0] = sd_scr[0];
    handoff->sd_scr[1] = sd_scr[1];
    handoff->sd_ocr = sd_ocr;
    handoff->sd_rca = sd_rca;
    handoff->sd_err = sd_err;
    handoff->sd_hv = sd_hv;
}

static int sd_status(unsigned int mask) {
    int cnt = 1000000;

    while ((*EMMC_STATUS & mask) && !(*EMMC_INTERRUPT & INT_ERROR_MASK) && cnt--) {
        wait_msec(1);
    }
    return (cnt <= 0 || (*EMMC_INTERRUPT & INT_ERROR_MASK)) ? SD_ERROR : SD_OK;
}

static int sd_int(unsigned int mask) {
    unsigned int r;
    unsigned int m = mask | INT_ERROR_MASK;
    int cnt = 1000000;

    while (!(*EMMC_INTERRUPT & m) && cnt--) {
        wait_msec(1);
    }
    r = *EMMC_INTERRUPT;
    if (cnt <= 0 || (r & INT_CMD_TIMEOUT) || (r & INT_DATA_TIMEOUT)) {
        *EMMC_INTERRUPT = r;
        return SD_TIMEOUT;
    }
    if (r & INT_ERROR_MASK) {
        *EMMC_INTERRUPT = r;
        return SD_ERROR;
    }
    *EMMC_INTERRUPT = mask;
    return 0;
}

static int sd_cmd(unsigned int code, unsigned int arg) {
    int r = 0;

    sd_err = SD_OK;
    if (code & CMD_NEED_APP) {
        r = sd_cmd(CMD_APP_CMD | (sd_rca ? CMD_RSPNS_48 : 0), sd_rca);
        if (sd_rca && !r) {
            sd_err = SD_ERROR;
            return 0;
        }
        code &= ~CMD_NEED_APP;
    }
    if (sd_status(SR_CMD_INHIBIT)) {
        sd_err = SD_TIMEOUT;
        return 0;
    }
    *EMMC_ARG1 = arg;
    *EMMC_CMDTM = code;
    if (code == CMD_SEND_OP_COND) {
        wait_msec(1000);
    } else if (code == CMD_SEND_IF_COND || code == CMD_APP_CMD) {
        wait_msec(100);
    }
    if ((r = sd_int(INT_CMD_DONE))) {
        log_error("Failed to send EMMC command");
        sd_err = r;
        return 0;
    }
    r = *EMMC_RESP0;
    if (code == CMD_GO_IDLE || code == CMD_APP_CMD) {
        return 0;
    }
    if (code == (CMD_APP_CMD | CMD_RSPNS_48)) {
        return r & SR_APP_CMD;
    }
    if (code == CMD_SEND_OP_COND) {
        return r;
    }
    if (code == CMD_SEND_IF_COND) {
        return r == arg ? SD_OK : SD_ERROR;
    }
    if (code == CMD_ALL_SEND_CID) {
        r |= *EMMC_RESP3;
        r |= *EMMC_RESP2;
        r |= *EMMC_RESP1;
        return r;
    }
    if (code == CMD_SEND_REL_ADDR) {
        sd_err = (((r & 0x1fff)) | ((r & 0x2000) << 6) | ((r & 0x4000) << 8) | ((r & 0x8000) << 8)) & CMD_ERRORS_MASK;
        return r & CMD_RCA_MASK;
    }
    return r & CMD_ERRORS_MASK;
}

int sd_readblock(unsigned int lba, unsigned char* buffer, unsigned int num) {
    int r;
    int c = 0;
    int d;
    unsigned int* buf = (unsigned int*)buffer;

    if (num < 1) {
        num = 1;
    }
    if (sd_status(SR_DAT_INHIBIT)) {
        sd_err = SD_TIMEOUT;
        return 0;
    }
    if (sd_scr[0] & SCR_SUPP_CCS) {
        if (num > 1 && (sd_scr[0] & SCR_SUPP_SET_BLKCNT)) {
            sd_cmd(CMD_SET_BLOCKCNT, num);
            if (sd_err) {
                return 0;
            }
        }
        *EMMC_BLKSIZECNT = (num << 16) | 512;
        sd_cmd(num == 1 ? CMD_READ_SINGLE : CMD_READ_MULTI, lba);
        if (sd_err) {
            return 0;
        }
    } else {
        *EMMC_BLKSIZECNT = (1 << 16) | 512;
    }
    while (c < (int)num) {
        if (!(sd_scr[0] & SCR_SUPP_CCS)) {
            sd_cmd(CMD_READ_SINGLE, (lba + (unsigned int)c) * 512);
            if (sd_err) {
                return 0;
            }
        }
        if ((r = sd_int(INT_READ_RDY))) {
            kerror("sd_readblock: Timeout waiting for ready to read");
            sd_err = r;
            return 0;
        }
        for (d = 0; d < 128; d++) {
            buf[d] = *EMMC_DATA;
        }
        c++;
        buf += 128;
    }
    if (num > 1 && !(sd_scr[0] & SCR_SUPP_SET_BLKCNT) && (sd_scr[0] & SCR_SUPP_CCS)) {
        sd_cmd(CMD_STOP_TRANS, 0);
    }
    return sd_err != SD_OK || c != (int)num ? 0 : (int)(num * 512);
}

int sd_writeblock(unsigned char* buffer, unsigned int lba, unsigned int num) {
    int r;
    int c = 0;
    int d;
    unsigned int* buf = (unsigned int*)buffer;

    if (num < 1) {
        num = 1;
    }
    if (sd_status(SR_DAT_INHIBIT | SR_WRITE_AVAILABLE)) {
        sd_err = SD_TIMEOUT;
        return 0;
    }
    if (sd_scr[0] & SCR_SUPP_CCS) {
        if (num > 1 && (sd_scr[0] & SCR_SUPP_SET_BLKCNT)) {
            sd_cmd(CMD_SET_BLOCKCNT, num);
            if (sd_err) {
                return 0;
            }
        }
        *EMMC_BLKSIZECNT = (num << 16) | 512;
        sd_cmd(num == 1 ? CMD_WRITE_SINGLE : CMD_WRITE_MULTI, lba);
        if (sd_err) {
            return 0;
        }
    } else {
        *EMMC_BLKSIZECNT = (1 << 16) | 512;
    }
    while (c < (int)num) {
        if (!(sd_scr[0] & SCR_SUPP_CCS)) {
            sd_cmd(CMD_WRITE_SINGLE, (lba + (unsigned int)c) * 512);
            if (sd_err) {
                return 0;
            }
        }
        if ((r = sd_int(INT_WRITE_RDY))) {
            log_error("Timeout waiting for ready to write");
            sd_err = r;
            return 0;
        }
        for (d = 0; d < 128; d++) {
            *EMMC_DATA = buf[d];
        }
        c++;
        buf += 128;
    }
    if ((r = sd_int(INT_DATA_DONE))) {
        log_error("Timeout waiting for data done");
        sd_err = r;
        return 0;
    }
    if (num > 1 && !(sd_scr[0] & SCR_SUPP_SET_BLKCNT) && (sd_scr[0] & SCR_SUPP_CCS)) {
        sd_cmd(CMD_STOP_TRANS, 0);
    }
    return sd_err != SD_OK || c != (int)num ? 0 : (int)(num * 512);
}

static int sd_clk(unsigned int f) {
    unsigned int d;
    unsigned int c = 41666666 / f;
    unsigned int x;
    unsigned int s = 32;
    unsigned int h = 0;
    int cnt = 100000;

    while ((*EMMC_STATUS & (SR_CMD_INHIBIT | SR_DAT_INHIBIT)) && cnt--) {
        wait_msec(1);
    }
    if (cnt <= 0) {
        log_error("Timeout waiting for inhibit flag");
        return SD_ERROR;
    }
    *EMMC_CONTROL1 &= ~C1_CLK_EN;
    wait_msec(10);
    x = c - 1;
    if (!x) {
        s = 0;
    } else {
        if (!(x & 0xffff0000u)) {
            x <<= 16;
            s -= 16;
        }
        if (!(x & 0xff000000u)) {
            x <<= 8;
            s -= 8;
        }
        if (!(x & 0xf0000000u)) {
            x <<= 4;
            s -= 4;
        }
        if (!(x & 0xc0000000u)) {
            x <<= 2;
            s -= 2;
        }
        if (!(x & 0x80000000u)) {
            x <<= 1;
            s -= 1;
        }
        if (s > 0) {
            s--;
        }
        if (s > 7) {
            s = 7;
        }
    }
    if (sd_hv > HOST_SPEC_V2) {
        d = c;
    } else {
        d = 1 << s;
    }
    if (d <= 2) {
        d = 2;
        s = 0;
    }
    if (sd_hv > HOST_SPEC_V2) {
        h = (d & 0x300) >> 2;
    }
    d = ((d & 0x0ff) << 8) | h;
    *EMMC_CONTROL1 = (*EMMC_CONTROL1 & 0xffff003f) | d;
    wait_msec(10);
    *EMMC_CONTROL1 |= C1_CLK_EN;
    wait_msec(10);
    while (!(*EMMC_CONTROL1 & C1_CLK_STABLE) && cnt--) {
        wait_msec(10);
    }
    if (cnt <= 0) {
        log_error("Failed to get stable clock");
        return SD_ERROR;
    }
    return 0;
}

int sd_init(void) {
    long r;
    int cnt = 100000;
    int ccs = 0;

    if (sd_initialized == true) {
        return SD_OK;
    }

    r = *GPFSEL4;
    r &= ~(7 << (7 * 3));
    *GPFSEL4 = r;
    *GPPUD = 2;
    wait_cycles(150);
    *GPPUDCLK1 = (1 << 15);
    wait_cycles(150);
    *GPPUD = 0;
    *GPPUDCLK1 = 0;
    r = *GPHEN1;
    r |= 1 << 15;
    *GPHEN1 = r;

    r = *GPFSEL4;
    r |= (7 << (8 * 3)) | (7 << (9 * 3));
    *GPFSEL4 = r;
    *GPPUD = 2;
    wait_cycles(150);
    *GPPUDCLK1 = (1 << 16) | (1 << 17);
    wait_cycles(150);
    *GPPUD = 0;
    *GPPUDCLK1 = 0;

    r = *GPFSEL5;
    r |= (7 << (0 * 3)) | (7 << (1 * 3)) | (7 << (2 * 3)) | (7 << (3 * 3));
    *GPFSEL5 = r;
    *GPPUD = 2;
    wait_cycles(150);
    *GPPUDCLK1 = (1 << 18) | (1 << 19) | (1 << 20) | (1 << 21);
    wait_cycles(150);
    *GPPUD = 0;
    *GPPUDCLK1 = 0;

    *EMMC_CONTROL0 = 0;
    sd_hv = (*EMMC_SLOTISR_VER & HOST_SPEC_NUM) >> HOST_SPEC_NUM_SHIFT;
    *EMMC_CONTROL1 |= C1_SRST_HC;
    do {
        wait_msec(10);
    } while ((*EMMC_CONTROL1 & C1_SRST_HC) && cnt--);
    if (cnt <= 0) {
        log_error("Failed to reset EMMC");
        return SD_ERROR;
    }

    *EMMC_CONTROL1 |= C1_CLK_INTLEN | C1_TOUNIT_MAX;
    wait_msec(10);

    if ((r = sd_clk(400000))) {
        return (int)r;
    }
    *EMMC_INT_EN = 0xffffffff;
    *EMMC_INT_MASK = 0xffffffff;

    sd_scr[0] = sd_scr[1] = sd_rca = sd_err = 0;
    sd_cmd(CMD_GO_IDLE, 0);
    if (sd_err) {
        return (int)sd_err;
    }
    sd_cmd(CMD_SEND_IF_COND, 0x000001AA);
    if (sd_err) {
        return (int)sd_err;
    }

    cnt = 6;
    r = 0;
    while (!(r & ACMD41_CMD_COMPLETE) && cnt--) {
        wait_cycles(400);
        r = sd_cmd(CMD_SEND_OP_COND, ACMD41_ARG_HC);
        if (sd_err != SD_TIMEOUT && sd_err != SD_OK) {
            log_error("EMMC ACMD41 returned error");
            return (int)sd_err;
        }
    }
    if (!(r & ACMD41_CMD_COMPLETE) || !cnt) {
        return SD_TIMEOUT;
    }
    if (!(r & ACMD41_VOLTAGE)) {
        return SD_ERROR;
    }
    if (r & ACMD41_CMD_CCS) {
        ccs = SCR_SUPP_CCS;
    }

    sd_cmd(CMD_ALL_SEND_CID, 0);
    sd_rca = sd_cmd(CMD_SEND_REL_ADDR, 0);
    if (sd_err) {
        return (int)sd_err;
    }
    if ((r = sd_clk(25000000))) {
        return (int)r;
    }
    sd_cmd(CMD_CARD_SELECT, sd_rca);
    if (sd_err) {
        return (int)sd_err;
    }
    if (sd_status(SR_DAT_INHIBIT)) {
        return SD_TIMEOUT;
    }
    *EMMC_BLKSIZECNT = (1 << 16) | 8;
    sd_cmd(CMD_SEND_SCR, 0);
    if (sd_err) {
        return (int)sd_err;
    }
    if (sd_int(INT_READ_RDY)) {
        return SD_TIMEOUT;
    }
    r = 0;
    cnt = 100000;
    while (r < 2 && cnt) {
        if (*EMMC_STATUS & SR_READ_AVAILABLE) {
            sd_scr[r++] = *EMMC_DATA;
        } else {
            wait_msec(1);
        }
    }
    if (r != 2) {
        return SD_TIMEOUT;
    }
    if (sd_scr[0] & SCR_SD_BUS_WIDTH_4) {
        sd_cmd(CMD_SET_BUS_WIDTH, sd_rca | 2);
        if (sd_err) {
            return (int)sd_err;
        }
        *EMMC_CONTROL0 |= C0_HCTL_DWITDH;
    }

    if (sd_scr[0] & SCR_SUPP_SET_BLKCNT) {
        sd_scr[0] &= ~SCR_SUPP_CCS;
    }
    sd_scr[0] |= ccs;
    sd_initialized = true;
    return SD_OK;
}

int sd_block_read(void* private, unsigned int begin, int count, void* buf) {
    (void)private;
    return sd_readblock(begin, (unsigned char*)buf, (unsigned int)count);
}

int sd_block_write(void* private, unsigned int begin, int count, const void* buf) {
    (void)private;
    return sd_writeblock((unsigned char*)buf, begin, (unsigned int)count);
}
