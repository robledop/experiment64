#pragma once
#include <wm/button.h>
#include <wm/textbox.h>

typedef struct calculator {
    window_t window; //'inherit' Window
    textbox_t *text_box;
    button_t *button_1;
    button_t *button_2;
    button_t *button_3;
    button_t *button_4;
    button_t *button_5;
    button_t *button_6;
    button_t *button_7;
    button_t *button_8;
    button_t *button_9;
    button_t *button_0;
    button_t *button_add;
    button_t *button_sub;
    button_t *button_div;
    button_t *button_mul;
    button_t *button_ent;
    button_t *button_c;
} calculator_t;

calculator_t *calculator_new(void);
