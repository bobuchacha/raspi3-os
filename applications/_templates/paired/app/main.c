#include "app/__DLL_NAME__.h"
#include "app/kernel.h"
#include "logger.h"

long main(void)
{
    const __DLL_NAME__Api *api = __DLL_NAME___open();
    if (!api)
    {
        app_log_error("__APP_NAME__", "failed to open /lib/__DLL_NAME__.dll");
        return 1;
    }
    if (api->abi_version != __DLL_NAME_UPPER___ABI_VERSION)
    {
        app_log_error("__APP_NAME__", "DLL ABI mismatch");
        return 1;
    }

    app_log_info("__APP_NAME__", "shared library ready");
    app_log_info("__APP_NAME__", "__DLL_NAME__->double_value(21) = %ld", api->double_value(21));
    api->print_message("__APP_NAME__");
    return 0;
}