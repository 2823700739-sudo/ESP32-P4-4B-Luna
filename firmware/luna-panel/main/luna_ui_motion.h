#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct { float x,y; bool left,resting; } luna_pet_position_t;
luna_pet_position_t luna_pet_motion(int64_t active_ms);
unsigned luna_ui_next_card(unsigned current, int delta);
