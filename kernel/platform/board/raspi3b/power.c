#include "device/raspi3b.h"

#define PM_RSTC ((volatile unsigned int *)(PBASE + 0x0010001c))
#define PM_RSTS ((volatile unsigned int *)(PBASE + 0x00100020))
#define PM_WDOG ((volatile unsigned int *)(PBASE + 0x00100024))
#define PM_WDOG_MAGIC 0x5a000000U
#define PM_RSTC_FULLRST 0x00000020U

void device_reboot()
{
    unsigned int value = *PM_RSTS;

    value &= ~0xfffffaaaU;
    *PM_RSTS = PM_WDOG_MAGIC | value;
    *PM_WDOG = PM_WDOG_MAGIC | 10U;
    *PM_RSTC = PM_WDOG_MAGIC | PM_RSTC_FULLRST;

    while (1)
    {
    }
}