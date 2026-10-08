#ifndef INPUT_BINDINGS_H
#define INPUT_BINDINGS_H
#include "chirky.h"
#include <linux/input.h>

#include "input_setup.h"

extern const struct controller_binding snes_bindings[CHIRKY_BUTTON_COUNT];
void default_bindings(struct controller_binding *bindings);
void default_keyboard_bindings(struct controller_binding *bindings);
void default_player_keyboard_bindings(struct controller_binding *bindings,unsigned player);
bool parse_binding(const char *text, struct controller_binding *binding);
void controller_binding_label(const struct controller_binding *binding,
    enum controller_profile profile,char *out,size_t size);
#endif
