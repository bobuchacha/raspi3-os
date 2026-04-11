#ifndef ROS_APP___LIB_UPPER___H
#define ROS_APP___LIB_UPPER___H

#ifdef __cplusplus
extern "C" {
#endif

    int Init(void* base);
    int Deinit(void* base);
    long __LIB_NAME___sum(long left, long right);

#ifdef __cplusplus
}

namespace user::dll::__LIB_NAME__ {
    inline long sum(long left, long right) {
        return __LIB_NAME___sum(left, right);
    }
}
#endif

#endif