#include "app/kernel.h"
#include "logger.h"

long main(void)
{
    app_log_info("__APP_NAME__", "hello from application program");
    return 0;
}