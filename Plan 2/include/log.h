#ifndef _LOG_H_
#define _LOG_H_

#define LOG_ERROR "!"
#define LOG_WARNING "*"
#define LOG_INFO "i"
#define LOG_TEST "T"
#define LOG_FAIL "F"

#define log(value, level) \
do { \
	kprint("(" level ") %s (" __FILE__ ":%d) ", __func__, __LINE__); \
    kprint(value); \
} while(0)

#define log_fail(value) log(value, LOG_FAIL)
#define log_test(value) log(value, LOG_TEST)
#define log_info(value) log(value, LOG_INFO)
#define log_warning(value) log(value, LOG_WARNING)
#define log_error(value) log(value, LOG_ERROR)

#endif