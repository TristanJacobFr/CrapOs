#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdbool.h>

/* Initialise the screen console from the Limine framebuffer response. */
bool console_init(void);
void console_print(const char *text);

#endif
