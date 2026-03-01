#pragma once

#define O_RDONLY  0x000 // open for reading only
#define O_WRONLY  0x001 // open for writing only
#define O_RDWR    0x002 // open for reading and writing
#define O_CREATE  0x200 // create file if it does not exist
#define O_TRUNC   0x400 // truncate file upon open
#define O_APPEND   0x800  // append on each write
#define O_NONBLOCK 0x1000 // non-blocking I/O

#define FD_CLOEXEC 1

#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
