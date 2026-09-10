#ifndef INPUT_BINDINGS_H
#define INPUT_BINDINGS_H
#include "two_forty.h"

enum binding_kind { BINDING_NONE, BINDING_KEY, BINDING_ABS };
struct controller_binding { enum binding_kind kind; unsigned int code; int direction; };

/* Setup owns a draft. Live bindings change only after the final release/save. */
struct binding_setup {
    bool active, keyboard, wait_release, ready, complete;
    int step;
    struct controller_binding candidate, pending[TWO_FORTY_BUTTON_COUNT];
    const char *message;
};
void default_bindings(struct controller_binding *bindings);
void default_keyboard_bindings(struct controller_binding *bindings);
bool parse_binding(const char *text, struct controller_binding *binding);
void setup_begin(struct binding_setup *setup, bool keyboard);
void setup_offer(struct binding_setup *setup, struct controller_binding binding);
void setup_release(struct binding_setup *setup, bool released);
#endif
