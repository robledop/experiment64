#pragma once

#define IFNAMSIZ 16

struct ifreq {
    char ifr_name[IFNAMSIZ];
    union {
        int ifr_flags;
    };
};
