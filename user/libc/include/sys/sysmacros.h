#pragma once

#include <sys/types.h>

#define major(dev) (((dev) >> 8) & 0xff)
#define minor(dev) ((dev) & 0xff)
#define makedev(maj, min) (((maj) << 8) | (min))
