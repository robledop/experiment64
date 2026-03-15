#pragma once

#include <sys/wait.h>
#include <wm/window.h>

void crash_dialog_show(window_t *parent, const char *app_name, int signal, const crash_info_t *info);
