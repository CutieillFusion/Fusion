#include "runtime.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifndef _WIN32
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#endif

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

void rt_print_cstring(const char *s, int64_t stream) {
  if (!s) {
    fprintf(stream_for(stream), "(null)");
    return;
  }
  /* Avoid strlen on obviously invalid pointers (e.g. small integers passed as ptr). */
  if ((uintptr_t)s < 4096) {
    fprintf(stream_for(stream), "(invalid)");
    return;
  }
  fprintf(stream_for(stream), "%s", s);
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

const char *rt_str_upper(const char *s) {
  if (!s) return NULL;
  size_t n = strlen(s);
  char *out = (char *)malloc(n + 1);
  if (!out) return NULL;
  for (size_t i = 0; i < n; i++) out[i] = (char)toupper((unsigned char)s[i]);
  out[n] = '\0';
  rt_track_string(out);
  return out;
}

const char *rt_str_lower(const char *s) {
  if (!s) return NULL;
  size_t n = strlen(s);
  char *out = (char *)malloc(n + 1);
  if (!out) return NULL;
  for (size_t i = 0; i < n; i++) out[i] = (char)tolower((unsigned char)s[i]);
  out[n] = '\0';
  rt_track_string(out);
  return out;
}

int64_t rt_str_contains(const char *haystack, const char *needle) {
  if (!haystack || !needle) return 0;
  return strstr(haystack, needle) ? 1 : 0;
}

const char *rt_str_strip(const char *s) {
  if (!s) return NULL;
  while (*s && isspace((unsigned char)*s)) s++;
  size_t n = strlen(s);
  while (n > 0 && isspace((unsigned char)s[n - 1])) n--;
  char *out = (char *)malloc(n + 1);
  if (!out) return NULL;
  memcpy(out, s, n);
  out[n] = '\0';
  rt_track_string(out);
  return out;
}

int64_t rt_str_find(const char *haystack, const char *needle) {
  if (!haystack || !needle) return -1;
  const char *p = strstr(haystack, needle);
  if (!p) return -1;
  return (int64_t)(p - haystack);
}

const char *rt_str_split(const char *s, const char *delim) {
  if (!s) return NULL;
  /* Empty or NULL delimiter: return 1-element array with copy of s */
  if (!delim || !*delim) {
    char *block = (char *)malloc(8 + 8);
    if (!block) return NULL;
    *(int64_t *)block = 1;
    char *copy = (char *)malloc(strlen(s) + 1);
    if (!copy) { free(block); return NULL; }
    strcpy(copy, s);
    rt_track_string(copy);
    ((char **)(block + 8))[0] = copy;
    rt_track_string(block);
    return (const char *)(block + 8);
  }
  size_t dlen = strlen(delim);
  /* First pass: count splits */
  int64_t count = 1;
  const char *p = s;
  while ((p = strstr(p, delim)) != NULL) { count++; p += dlen; }
  /* Allocate: 8-byte header + count*8 bytes for pointers */
  char *block = (char *)malloc(8 + (size_t)count * 8);
  if (!block) return NULL;
  *(int64_t *)block = count;
  char **ptrs = (char **)(block + 8);
  /* Second pass: extract pieces */
  p = s;
  for (int64_t i = 0; i < count; i++) {
    const char *next = (i < count - 1) ? strstr(p, delim) : NULL;
    size_t piece_len = next ? (size_t)(next - p) : strlen(p);
    char *piece = (char *)malloc(piece_len + 1);
    if (!piece) { piece = (char *)malloc(1); if (piece) piece[0] = '\0'; }
    else { memcpy(piece, p, piece_len); piece[piece_len] = '\0'; }
    rt_track_string(piece);
    ptrs[i] = piece;
    p = next ? next + dlen : p + piece_len;
  }
  rt_track_string(block);
  return (const char *)(block + 8);
}

int64_t rt_str_eq(const char *a, const char *b) {
    if (a == b) return 1;           // same pointer or both NULL
    if (!a || !b) return 0;         // exactly one NULL
    return strcmp(a, b) == 0 ? 1 : 0;
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

static char chr_buf[2] = {0, 0};

const char *rt_chr(int64_t code) {
  chr_buf[0] = (char)(unsigned char)code;
  return chr_buf;
}

void rt_flush(int64_t stream) {
  fflush(stream_for(stream));
}

int64_t rt_read_key(void) {
#ifndef _WIN32
  struct termios saved, raw;
  if (tcgetattr(STDIN_FILENO, &saved) < 0) return 0;
  raw = saved;
  raw.c_lflag &= ~(ICANON | ECHO);
  raw.c_iflag &= ~(ICRNL);
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSANOW, &raw);

  unsigned char c;
  ssize_t n = read(STDIN_FILENO, &c, 1);
  if (n <= 0) { tcsetattr(STDIN_FILENO, TCSANOW, &saved); return 0; }

  if (c == 27) { /* ESC - possible arrow key sequence */
    unsigned char seq[2];
    ssize_t n1 = read(STDIN_FILENO, &seq[0], 1);
    if (n1 <= 0) { tcsetattr(STDIN_FILENO, TCSANOW, &saved); return 27; }
    if (seq[0] == '[') {
      ssize_t n2 = read(STDIN_FILENO, &seq[1], 1);
      if (n2 <= 0) { tcsetattr(STDIN_FILENO, TCSANOW, &saved); return 27; }
      tcsetattr(STDIN_FILENO, TCSANOW, &saved);
      switch (seq[1]) {
        case 'A': return 256; /* Up */
        case 'B': return 257; /* Down */
        case 'C': return 258; /* Right */
        case 'D': return 259; /* Left */
        default:  return 27;
      }
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    return 27;
  }

  tcsetattr(STDIN_FILENO, TCSANOW, &saved);
  return (int64_t)c;
#else
  return 0; /* Not implemented on Windows */
#endif
}

int64_t rt_terminal_height(void) {
#ifndef _WIN32
  struct winsize ws;
  int fd = open("/dev/tty", O_RDONLY);
  if (fd < 0) fd = STDIN_FILENO;
  int ok = ioctl(fd, TIOCGWINSZ, &ws);
  if (fd != STDIN_FILENO) close(fd);
  if (ok < 0) return 0;
  return (int64_t)ws.ws_row;
#else
  return 0;
#endif
}

int64_t rt_terminal_width(void) {
#ifndef _WIN32
  struct winsize ws;
  int fd = open("/dev/tty", O_RDONLY);
  if (fd < 0) fd = STDIN_FILENO;
  int ok = ioctl(fd, TIOCGWINSZ, &ws);
  if (fd != STDIN_FILENO) close(fd);
  if (ok < 0) return 0;
  return (int64_t)ws.ws_col;
#else
  return 0;
#endif
}

void rt_panic(const char *msg) {
  fflush(stdout);
  if (msg)
    fprintf(stderr, "fusion panic: %s\n", msg);
  else
    fprintf(stderr, "fusion panic\n");
  abort();
}
