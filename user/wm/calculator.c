#include <wm/calculator.h>
#include "stdlib.h"

/**
 * @brief Handle calculator button presses.
 *
 * Updates the calculator textbox contents based on which button triggered the event.
 *
 * @param button Button that generated the event.
 * @param x Cursor X coordinate relative to the button (unused).
 * @param y Cursor Y coordinate relative to the button (unused).
 */
void calculator_button_handler([[maybe_unused]] const button_t* button, [[maybe_unused]] int x, [[maybe_unused]] int y)
{
    auto calculator = (calculator_t*)button->window.parent;

    if (button == calculator->button_0)
    {
        if (!(calculator->text_box->window.title[0] == '0' && calculator->text_box->window.title[1] == 0))
        {
            window_append_title((window_t*)calculator->text_box, "0");
        }
    }

    if (button == calculator->button_1)
    {
        if (!(calculator->text_box->window.title[0] == '0' && calculator->text_box->window.title[1] == 0))
        {
            window_append_title((window_t*)calculator->text_box, "1");
        }
        else
        {
            window_set_title((window_t*)calculator->text_box, "1");
        }
    }

    if (button == calculator->button_2)
    {
        if (!(calculator->text_box->window.title[0] == '0' && calculator->text_box->window.title[1] == 0))
        {
            window_append_title((window_t*)calculator->text_box, "2");
        }
        else
        {
            window_set_title((window_t*)calculator->text_box, "2");
        }
    }

    if (button == calculator->button_3)
    {
        if (!(calculator->text_box->window.title[0] == '0' && calculator->text_box->window.title[1] == 0))
        {
            window_append_title((window_t*)calculator->text_box, "3");
        }
        else
        {
            window_set_title((window_t*)calculator->text_box, "3");
        }
    }

    if (button == calculator->button_4)
    {
        if (!(calculator->text_box->window.title[0] == '0' && calculator->text_box->window.title[1] == 0))
        {
            window_append_title((window_t*)calculator->text_box, "4");
        }
        else
        {
            window_set_title((window_t*)calculator->text_box, "4");
        }
    }

    if (button == calculator->button_5)
    {
        if (!(calculator->text_box->window.title[0] == '0' && calculator->text_box->window.title[1] == 0))
        {
            window_append_title((window_t*)calculator->text_box, "5");
        }
        else
        {
            window_set_title((window_t*)calculator->text_box, "5");
        }
    }

    if (button == calculator->button_6)
    {
        if (!(calculator->text_box->window.title[0] == '0' && calculator->text_box->window.title[1] == 0))
        {
            window_append_title((window_t*)calculator->text_box, "6");
        }
        else
        {
            window_set_title((window_t*)calculator->text_box, "6");
        }
    }

    if (button == calculator->button_7)
    {
        if (!(calculator->text_box->window.title[0] == '0' && calculator->text_box->window.title[1] == 0))
        {
            window_append_title((window_t*)calculator->text_box, "7");
        }
        else
        {
            window_set_title((window_t*)calculator->text_box, "7");
        }
    }

    if (button == calculator->button_8)
    {
        if (!(calculator->text_box->window.title[0] == '0' && calculator->text_box->window.title[1] == 0))
        {
            window_append_title((window_t*)calculator->text_box, "8");
        }
        else
        {
            window_set_title((window_t*)calculator->text_box, "8");
        }
    }

    if (button == calculator->button_9)
    {
        if (!(calculator->text_box->window.title[0] == '0' && calculator->text_box->window.title[1] == 0))
        {
            window_append_title((window_t*)calculator->text_box, "9");
        }
        else
        {
            window_set_title((window_t*)calculator->text_box, "9");
        }
    }

    if (button == calculator->button_c)
    {
        window_set_title((window_t*)calculator->text_box, "0");
    }
}

/**
 * @brief Create a new calculator window with buttons and display.
 *
 * @return Pointer to the newly allocated calculator window or nullptr on failure.
 */
calculator_t* calculator_new(void)
{
    auto calculator = (calculator_t*)malloc(sizeof(calculator_t));
    if (!calculator)
    {
        return calculator;
    }

    if (!window_init((window_t*)calculator,
                     0,
                     0,
                     (2 * WIN_BORDER_WIDTH) + 145,
                     WIN_TITLE_HEIGHT + WIN_BORDER_WIDTH + 170,
                     0,
                     nullptr))
    {
        free(calculator);
        return nullptr;
    }

    window_set_title((window_t*)calculator, "Calculator");

    calculator->button_7 = button_new(WIN_BORDER_WIDTH + 5, WIN_TITLE_HEIGHT + 30, 30, 30);
    window_set_title((window_t*)calculator->button_7, "7");
    window_insert_child((window_t*)calculator, (window_t*)calculator->button_7);

    calculator->button_8 = button_new(WIN_BORDER_WIDTH + 40, WIN_TITLE_HEIGHT + 30, 30, 30);
    window_set_title((window_t*)calculator->button_8, "8");
    window_insert_child((window_t*)calculator, (window_t*)calculator->button_8);

    calculator->button_9 = button_new(WIN_BORDER_WIDTH + 75, WIN_TITLE_HEIGHT + 30, 30, 30);
    window_set_title((window_t*)calculator->button_9, "9");
    window_insert_child((window_t*)calculator, (window_t*)calculator->button_9);

    calculator->button_add = button_new(WIN_BORDER_WIDTH + 110, WIN_TITLE_HEIGHT + 30, 30, 30);
    window_set_title((window_t*)calculator->button_add, "+");
    window_insert_child((window_t*)calculator, (window_t*)calculator->button_add);

    calculator->button_4 = button_new(WIN_BORDER_WIDTH + 5, WIN_TITLE_HEIGHT + 65, 30, 30);
    window_set_title((window_t*)calculator->button_4, "4");
    window_insert_child((window_t*)calculator, (window_t*)calculator->button_4);

    calculator->button_5 = button_new(WIN_BORDER_WIDTH + 40, WIN_TITLE_HEIGHT + 65, 30, 30);
    window_set_title((window_t*)calculator->button_5, "5");
    window_insert_child((window_t*)calculator, (window_t*)calculator->button_5);

    calculator->button_6 = button_new(WIN_BORDER_WIDTH + 75, WIN_TITLE_HEIGHT + 65, 30, 30);
    window_set_title((window_t*)calculator->button_6, "6");
    window_insert_child((window_t*)calculator, (window_t*)calculator->button_6);

    calculator->button_sub = button_new(WIN_BORDER_WIDTH + 110, WIN_TITLE_HEIGHT + 65, 30, 30);
    window_set_title((window_t*)calculator->button_sub, "-");
    window_insert_child((window_t*)calculator, (window_t*)calculator->button_sub);

    calculator->button_1 = button_new(WIN_BORDER_WIDTH + 5, WIN_TITLE_HEIGHT + 100, 30, 30);
    window_set_title((window_t*)calculator->button_1, "1");
    window_insert_child((window_t*)calculator, (window_t*)calculator->button_1);

    calculator->button_2 = button_new(WIN_BORDER_WIDTH + 40, WIN_TITLE_HEIGHT + 100, 30, 30);
    window_set_title((window_t*)calculator->button_2, "2");
    window_insert_child((window_t*)calculator, (window_t*)calculator->button_2);

    calculator->button_3 = button_new(WIN_BORDER_WIDTH + 75, WIN_TITLE_HEIGHT + 100, 30, 30);
    window_set_title((window_t*)calculator->button_3, "3");
    window_insert_child((window_t*)calculator, (window_t*)calculator->button_3);

    calculator->button_mul = button_new(WIN_BORDER_WIDTH + 110, WIN_TITLE_HEIGHT + 100, 30, 30);
    window_set_title((window_t*)calculator->button_mul, "*");
    window_insert_child((window_t*)calculator, (window_t*)calculator->button_mul);

    calculator->button_c = button_new(WIN_BORDER_WIDTH + 5, WIN_TITLE_HEIGHT + 135, 30, 30);
    window_set_title((window_t*)calculator->button_c, "C");
    window_insert_child((window_t*)calculator, (window_t*)calculator->button_c);

    calculator->button_0 = button_new(WIN_BORDER_WIDTH + 40, WIN_TITLE_HEIGHT + 135, 30, 30);
    window_set_title((window_t*)calculator->button_0, "0");
    window_insert_child((window_t*)calculator, (window_t*)calculator->button_0);

    calculator->button_ent = button_new(WIN_BORDER_WIDTH + 75, WIN_TITLE_HEIGHT + 135, 30, 30);
    window_set_title((window_t*)calculator->button_ent, "=");
    window_insert_child((window_t*)calculator, (window_t*)calculator->button_ent);

    calculator->button_div = button_new(WIN_BORDER_WIDTH + 110, WIN_TITLE_HEIGHT + 135, 30, 30);
    window_set_title((window_t*)calculator->button_div, "/");
    window_insert_child((window_t*)calculator, (window_t*)calculator->button_div);

    // We'll use the same handler to handle all the buttons
    calculator->button_1->onmousedown = calculator->button_2->onmousedown = calculator->button_3->onmousedown =
        calculator->button_4->onmousedown = calculator->button_5->onmousedown = calculator->button_6->onmousedown =
        calculator->button_7->onmousedown = calculator->button_8->onmousedown = calculator->button_9->onmousedown =
        calculator->button_0->onmousedown = calculator->button_add->onmousedown =
        calculator->button_sub->onmousedown = calculator->button_mul->onmousedown =
        calculator->button_div->onmousedown = calculator->button_ent->onmousedown =
        calculator->button_c->onmousedown = calculator_button_handler;

    // Create the textbox
    calculator->text_box = textbox_new(WIN_BORDER_WIDTH + 5, WIN_TITLE_HEIGHT + 5, 135, 20);
    window_set_title((window_t*)calculator->text_box, "0");
    window_insert_child((window_t*)calculator, (window_t*)calculator->text_box);

    return calculator;
}
