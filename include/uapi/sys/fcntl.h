#pragma once

#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_EXCL    0x080
#define O_NOCTTY  0x100
#define O_CREAT   0x200
#define O_CREATE  O_CREAT
#define O_TRUNC   0x400
#define O_APPEND  0x800
#define O_NONBLOCK 0x1000

#define FD_CLOEXEC 1

#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define F_DUPFD 10
