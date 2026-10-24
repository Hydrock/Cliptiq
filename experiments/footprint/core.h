#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int probe_open(const char* path);
int probe_add(int id, const char* format, const unsigned char* data, size_t len);
int probe_count(void);
int probe_representations(void);
int probe_get(int id, const char* format, unsigned char* output, size_t capacity);
int probe_xml(char* output, size_t capacity);
void probe_close(void);
#ifdef __cplusplus
}
#endif
