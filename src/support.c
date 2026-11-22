#include "support.h"

void memory_copy(void *destination, const void *source, size_t size) {
    unsigned char *to = destination;
    const unsigned char *from = source;
    while (size--) *to++ = *from++;
}

int memory_equal(const void *left, const void *right, size_t size) {
    const unsigned char *a = left, *b = right;
    for (size_t i = 0; i < size; ++i)
        if (a[i] != b[i]) return 0;
    return 1;
}

size_t text_length(const char *text) {
    size_t length = 0;
    while (text && text[length]) ++length;
    return length;
}

const char *text_find(const char *text, size_t length, const char *needle) {
    size_t needle_length = text_length(needle);
    if (needle_length > length) return NULL;
    for (size_t at = 0; at + needle_length <= length; ++at)
        if (memory_equal(text + at, needle, needle_length)) return text + at;
    return NULL;
}

/* The process reads its own memory through ReadProcessMemory, which reports unmapped or protected pages
   as a failed read instead of faulting. */
int memory_read(const void *address, void *out, size_t size) {
    SIZE_T done = 0;
    if (!address || !out || size > UINTPTR_MAX - (uintptr_t)address) return 0;
    return ReadProcessMemory(GetCurrentProcess(), address, out, size, &done) && done == size;
}

int memory_read_pointer(const void *address, void **value) {
    return memory_read(address, value, sizeof(*value)) && *value;
}

int float_is_finite(float value) {
    uint32_t bits;
    memory_copy(&bits, &value, sizeof(bits));
    return (bits & 0x7f800000) != 0x7f800000;
}
