#include "runtime.h"
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#define RT_DL_ERRBUF_SIZE 512
static char rt_dl_error_buf[RT_DL_ERRBUF_SIZE];

static void capture_dlerror(const char *context) {
  const char *e = dlerror();
  if (e) {
    size_t n = strlen(e);
    size_t ctx_len = context ? strlen(context) : 0;
    if (ctx_len + n + 4 < RT_DL_ERRBUF_SIZE) {
      if (context)
        snprintf(rt_dl_error_buf, sizeof(rt_dl_error_buf), "%s: %s", context, e);
      else
        snprintf(rt_dl_error_buf, sizeof(rt_dl_error_buf), "%s", e);
    } else {
      strncpy(rt_dl_error_buf, e, RT_DL_ERRBUF_SIZE - 1);
      rt_dl_error_buf[RT_DL_ERRBUF_SIZE - 1] = '\0';
    }
  } else {
    rt_dl_error_buf[0] = '\0';
  }
}

rt_lib_handle_t rt_dlopen(const char *path) {
  rt_dl_error_buf[0] = '\0';
  rt_lib_handle_t h = (rt_lib_handle_t)dlopen(path, RTLD_NOW);
  if (!h)
    capture_dlerror(path);
  return h;
}

void *rt_dlsym(rt_lib_handle_t handle, const char *symbol_name) {
  rt_dl_error_buf[0] = '\0';
  dlerror(); /* clear previous error per dlsym(3) */
  void *sym = dlsym(handle, symbol_name);
  if (!sym)
    capture_dlerror(symbol_name);
  return sym;
}

int rt_dlclose(rt_lib_handle_t handle) {
  rt_dl_error_buf[0] = '\0';
  int r = dlclose(handle);
  if (r != 0)
    capture_dlerror("dlclose");
  return r == 0 ? 0 : -1;
}

const char *rt_dlerror_last(void) {
  return rt_dl_error_buf[0] ? rt_dl_error_buf : NULL;
}
