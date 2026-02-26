#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PRINT_BUF_SIZE 256
#define LINE_BUF_SIZE 4096

static char line_buf[LINE_BUF_SIZE];
static char to_str_buf[PRINT_BUF_SIZE];
static char file_line_buf[LINE_BUF_SIZE];

static FILE *stream_for(int64_t s) {
  return (s == 1) ? stderr : stdout;
}

void rt_init(void) {}

void rt_print_i64(int64_t value, int64_t stream) {
  fprintf(stream_for(stream), "%lld\n", (long long)value);
}

void rt_print_f64(double value, int64_t stream) {
  fprintf(stream_for(stream), "%g\n", value);
}

void rt_print_cstring(const char *s, int64_t stream) {
  fprintf(stream_for(stream), "%s\n", s ? s : "(null)");
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
