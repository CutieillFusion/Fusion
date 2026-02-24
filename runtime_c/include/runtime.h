#ifndef FUSION_RUNTIME_H
#define FUSION_RUNTIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void rt_init(void);

void rt_print_i64(int64_t value);

#ifdef __cplusplus
}
#endif

#endif /* FUSION_RUNTIME_H */
