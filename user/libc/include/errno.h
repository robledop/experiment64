#pragma once

#include <status.h>

int *__errno_location(void);
#define errno (*__errno_location())
void perror(const char *s);
