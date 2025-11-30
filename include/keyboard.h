#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Scancode table size (US QWERTY Scancode Set 1)
#define SCANCODE_TABLE_SIZE 84

// Accessors for scancode-to-character translation
char keyboard_scancode_to_char(uint8_t scancode);
char keyboard_scancode_to_char_shifted(uint8_t scancode);

void keyboard_init(void);
void keyboard_inject_scancode(uint8_t scancode);
void keyboard_reset_state_for_test(void);
void keyboard_handler_main(void);
char keyboard_get_char(void);
bool keyboard_has_char(void);
uint64_t keyboard_read_raw(uint8_t *out, uint64_t max);
void keyboard_clear_modifiers(void);
