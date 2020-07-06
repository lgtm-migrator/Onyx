/*
* Copyright (c) 2020 Pedro Falcato
* This file is part of Onyx, and is released under the terms of the MIT License
* check LICENSE at the root directory for more information
*/
#include <limits.h>

#include <onyx/input/state.h>

static constexpr unsigned int keys_per_long = (sizeof(unsigned long) * CHAR_BIT);

extern "C"
void input_state_set_key_state(keycode_t key, bool pressed, struct input_state *is)
{
	unsigned int idx = key / keys_per_long;
	unsigned int bit_idx = key % keys_per_long;

	if(pressed)
		is->keys_pressed[idx] |= (1UL << bit_idx);
	else
		is->keys_pressed[idx] &= ~(1UL << bit_idx);
}

extern "C"
bool input_state_key_is_pressed(keycode_t key, struct input_state *is)
{
	unsigned int idx = key / keys_per_long;
	unsigned int bit_idx = key % keys_per_long;

	return is->keys_pressed[idx] & (1UL << bit_idx);
}

extern "C"
bool input_state_toggle_key(keycode_t key, struct input_state *is)
{
	unsigned int idx = key / keys_per_long;
	unsigned int bit_idx = key % keys_per_long;

	return (is->keys_pressed[idx] ^= (1UL << bit_idx)) & (1UL << bit_idx);
}