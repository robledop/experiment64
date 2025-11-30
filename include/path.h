#pragma once

#include <stddef.h>

#define PATH_MAX_LEN 256
#define PATH_MAX_SEGMENTS 64

// Safely copy a string with bounds checking
void path_safe_copy(char *dst, size_t dst_size, const char *src);

// Simplify a path in-place by resolving . and .. components
void path_simplify(char *path, size_t path_size);

// Build an absolute path from a base directory and input path
// If input is absolute, it's used directly; otherwise it's joined with base
void path_build_absolute(const char *base, const char *input, char *output, size_t size);
