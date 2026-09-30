#ifndef INPUT_BINDINGS_H
#define INPUT_BINDINGS_H
#include "chirky.h"
#include <linux/input.h>

#include "input_setup.h"

void default_bindings(struct controller_binding *bindings);
void default_keyboard_bindings(struct controller_binding *bindings);
bool parse_binding(const char *text, struct controller_binding *binding);
#endif
