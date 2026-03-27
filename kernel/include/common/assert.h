#ifndef _ASSERT_H_
#define _ASSERT_H_

#define ASSERT(n, ...) if (!(n)) kpanic(__VA_ARGS__)

#endif