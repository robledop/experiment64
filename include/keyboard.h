#pragma once

#include <stdint.h>
#include <stdbool.h>

void keyboard_init(void);
void keyboard_handler_main(void);
char keyboard_get_char(void);
bool keyboard_has_char(void);
