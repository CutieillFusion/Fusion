#include "runtime.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PRINT_BUF_SIZE 256
#define LINE_BUF_SIZE 4096

static char line_buf[LINE_BUF_SIZE];
static char to_str_buf[PRINT_BUF_SIZE];
static char file_line_buf[LINE_BUF_SIZE];

/* Simple string arena for rt_str_dup / rt_str_concat.
 * Not thread-safe (matches rest of stub.c). Strings are reclaimed by rt_shutdown().
 */
typedef struct RtStrNode {
  char *ptr;
  struct RtStrNode *next;
} RtStrNode;

static RtStrNode *rt_str_head = NULL;

void rt_track_string(char *p) {
  if (!p) return;
  RtStrNode *node = (RtStrNode *)malloc(sizeof(RtStrNode));
  if (!node) return;  /* Leak p rather than crashing on OOM. */
  node->ptr = p;
  node->next = rt_str_head;
  rt_str_head = node;
}

static FILE *stream_for(int64_t s) {
  return (s == 1) ? stderr : stdout;
}

void rt_init(void) {}

void rt_shutdown(void) {
  RtStrNode *node = rt_str_head;
  while (node) {
    RtStrNode *next = node->next;
    free(node->ptr);
    free(node);
    node = next;
  }
  rt_str_head = NULL;
}

void rt_print_i64(int64_t value, int64_t stream) {
  fprintf(stream_for(stream), "%lld\n", (long long)value);
}

void rt_print_f64(double value, int64_t stream) {
  fprintf(stream_for(stream), "%g\n", value);
}

void rt_print_cstring(const char *s, int64_t stream) {
  if (!s) {
    fprintf(stream_for(stream), "(null)\n");
    return;
  }
  /* Avoid strlen on obviously invalid pointers (e.g. small integers passed as ptr). */
  if ((uintptr_t)s < 4096) {
    fprintf(stream_for(stream), "(invalid)\n");
    return;
  }
  fprintf(stream_for(stream), "%s\n", s);
}

const char *rt_read_line(void) {
  if (!fgets(line_buf, (int)sizeof(line_buf), stdin))
    return NULL;
  size_t len = strlen(line_buf);
  if (len > 0 && line_buf[len - 1] == '\n')
    line_buf[len - 1] = '\0';
  return line_buf;
}

const char *rt_to_str_i64(int64_t value) {
  (void)snprintf(to_str_buf, sizeof(to_str_buf), "%lld", (long long)value);
  return to_str_buf;
}

const char *rt_to_str_f64(double value) {
  (void)snprintf(to_str_buf, sizeof(to_str_buf), "%g", value);
  return to_str_buf;
}

int64_t rt_from_str_i64(const char *s) {
  if (!s || !*s) return 0;
  return (int64_t)strtoll(s, NULL, 10);
}

double rt_from_str_f64(const char *s) {
  if (!s || !*s) return 0.0;
  return strtod(s, NULL);
}

const char *rt_str_concat(const char *a, const char *b) {
  size_t la = a ? strlen(a) : 0;
  size_t lb = b ? strlen(b) : 0;
  char *out = (char *)malloc(la + lb + 1);
  if (!out) return NULL;
  if (la) memcpy(out, a, la);
  if (lb) memcpy(out + la, b, lb);
  out[la + lb] = '\0';
  rt_track_string(out);
  return out;
}

const char *rt_str_dup(const char *s) {
  if (!s) return NULL;
  size_t n = strlen(s) + 1;
  char *out = (char *)malloc(n);
  if (!out) return NULL;
  memcpy(out, s, n);
   rt_track_string(out);
  return out;
}

void *rt_open(const char *path, const char *mode) {
  if (!path || !mode) return NULL;
  return (void *)fopen(path, mode);
}

void rt_close(void *handle) {
  if (handle)
    fclose((FILE *)handle);
}

const char *rt_read_line_file(void *handle) {
  if (!handle) return NULL;
  if (!fgets(file_line_buf, (int)sizeof(file_line_buf), (FILE *)handle))
    return NULL;
  size_t len = strlen(file_line_buf);
  if (len > 0 && file_line_buf[len - 1] == '\n')
    file_line_buf[len - 1] = '\0';
  return file_line_buf;
}

void rt_write_file_i64(void *handle, int64_t value) {
  if (!handle) return;
  fprintf((FILE *)handle, "%lld\n", (long long)value);
}

void rt_write_file_f64(void *handle, double value) {
  if (!handle) return;
  fprintf((FILE *)handle, "%g\n", value);
}

void rt_write_file_ptr(void *handle, const char *s) {
  if (!handle) return;
  if (s)
    fputs(s, (FILE *)handle);
}

int64_t rt_write_bytes(void *handle, const void *buf, int64_t count) {
  if (!handle || !buf || count < 0) return -1;
  size_t n = (size_t)count;
  if (n != (uint64_t)count) return -1; /* overflow */
  size_t w = fwrite(buf, 1, n, (FILE *)handle);
  return (int64_t)w;
}

int64_t rt_read_bytes(void *handle, void *buf, int64_t count) {
  if (!handle || !buf || count < 0) return -1;
  size_t n = (size_t)count;
  if (n != (uint64_t)count) return -1;
  size_t r = fread(buf, 1, n, (FILE *)handle);
  return (int64_t)r;
}

int64_t rt_eof_file(void *handle) {
  if (!handle) return 1;
  return feof((FILE *)handle) ? 1 : 0;
}

int64_t rt_line_count_file(void *handle) {
  if (!handle) return 0;
  int64_t count = 0;
  int c;
  while ((c = fgetc((FILE *)handle)) != EOF)
    if (c == '\n') count++;
  return count;
}

void rt_panic(const char *msg) {
  if (msg)
    fprintf(stderr, "fusion panic: %s\n", msg);
  else
    fprintf(stderr, "fusion panic\n");
  abort();
}
