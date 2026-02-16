#pragma once

#include <stdint.h>

struct tcb {
    struct tcb *self;
};

void __tls_init(void);
void __tls_init_thread(void);
void __tls_destroy_thread(void);
