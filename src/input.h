#ifndef BPE_INPUT_H
#define BPE_INPUT_H

extern int g_key_states[256];
extern int g_keys[256];

void install_input_hooks(void);
void input_cleanup(void);

#endif
