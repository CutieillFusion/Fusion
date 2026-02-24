#ifndef FUSION_RUNTIME_H
#define FUSION_RUNTIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void rt_init(void);

void rt_print_i64(int64_t value);

/* Dynamic loading (dlopen/dlsym). Handles are opaque pointers from dlopen/dlsym. */
typedef void *rt_lib_handle_t;

/* Load .so; returns handle or NULL on failure. On failure, rt_dlerror_last() describes the error (path context). */
rt_lib_handle_t rt_dlopen(const char *path);

/* Resolve symbol in library; returns function/data pointer or NULL on failure. On failure, rt_dlerror_last() describes (symbol context). */
void *rt_dlsym(rt_lib_handle_t handle, const char *symbol_name);

/* Close library. Returns 0 on success, -1 on failure. On failure, rt_dlerror_last() has details. */
int rt_dlclose(rt_lib_handle_t handle);

/* Last error string from dlopen/dlsym/dlclose. Lifetime: until next dl call. Not thread-safe in v1. */
const char *rt_dlerror_last(void);

#ifdef __cplusplus
}
#endif

#endif /* FUSION_RUNTIME_H */
