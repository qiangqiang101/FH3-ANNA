/* Small helpers: memory copies and compares, and reads of the game's memory that fail safely. */
#ifndef ANNA_SUPPORT_H
#define ANNA_SUPPORT_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>
#include <stdint.h>

#define ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))

void memory_copy(void *destination, const void *source, size_t size);
int memory_equal(const void *left, const void *right, size_t size);
size_t text_length(const char *text);
/* The first `needle` in the `length` characters of `text`, or NULL. */
const char *text_find(const char *text, size_t length, const char *needle);
/* Copies `size` bytes from `address`; 0 when any of that memory can't be read. */
int memory_read(const void *address, void *out, size_t size);
/* Reads a pointer; 0 when unreadable or NULL. */
int memory_read_pointer(const void *address, void **value);
int float_is_finite(float value);
#endif
