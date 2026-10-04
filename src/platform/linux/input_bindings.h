#ifndef INPUT_BINDINGS_H
#define INPUT_BINDINGS_H
#include "chirky.h"
#include <linux/input.h>

#include "input_setup.h"

void default_bindings(struct controller_binding *bindings);
void default_keyboard_bindings(struct controller_binding *bindings);
bool parse_binding(const char *text, struct controller_binding *binding);
enum controller_label_profile { CONTROLLER_LABEL_GENERIC, CONTROLLER_LABEL_SNES_PICO };
void controller_binding_label(const struct controller_binding *binding,
    enum controller_label_profile profile,char *out,size_t size);
#endif
