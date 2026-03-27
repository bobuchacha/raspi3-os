#include "app/kernel.hpp"
#include "logger.h"

class SampleApp
{
public:
    void run() const
    {
        app_log_info("__APP_NAME__", "hello from application program");
    }
};

long main(void)
{
    SampleApp app;
    app.run();
    return 0;
}