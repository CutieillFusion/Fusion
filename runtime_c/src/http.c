/*
 * HTTP client via libcurl. Single-threaded only: rt_http_status() uses a static
 * variable; Fusion execution is assumed single-threaded.
 */
#include "runtime.h"
#include <curl/curl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int64_t rt_last_http_status = 0;

struct write_buf {
  char *data;
  size_t size;
  size_t cap;
};

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
  struct write_buf *w = (struct write_buf *)userdata;
  size_t n = size * nmemb;
  if (n == 0) return n;
  if (w->size + n + 1 > w->cap) {
    size_t new_cap = w->cap ? w->cap * 2 : 4096;
    while (new_cap < w->size + n + 1) new_cap *= 2;
    char *new_data = (char *)realloc(w->data, new_cap);
    if (!new_data) return 0;
    w->data = new_data;
    w->cap = new_cap;
  }
  memcpy(w->data + w->size, ptr, n);
  w->size += n;
  w->data[w->size] = '\0';
  return n;
}

const char *rt_http_request(const char *method, const char *url, const char *body) {
  rt_last_http_status = 0;
  if (!method || !url) return NULL;

  static int curl_initialized = 0;
  if (!curl_initialized) {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) return NULL;
    curl_initialized = 1;
  }

  CURL *curl = curl_easy_init();
  if (!curl) return NULL;

  struct write_buf w = { NULL, 0, 0 };

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &w);
  /* Body: use POSTFIELDS only when caller provided non-empty body (Fusion uses "" for GET/HEAD/DELETE). */
  if (body && *body != '\0') {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
  }

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    curl_easy_cleanup(curl);
    free(w.data);
    return NULL;
  }

  long code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
  rt_last_http_status = (int64_t)code;

  curl_easy_cleanup(curl);

  const char *out = NULL;
  if (w.data) {
    char *copy = (char *)malloc(w.size + 1);
    if (copy) {
      memcpy(copy, w.data, w.size + 1);
      rt_track_string(copy);
      out = copy;
    }
  }
  free(w.data);
  return out;
}

int64_t rt_http_status(void) {
  return rt_last_http_status;
}
