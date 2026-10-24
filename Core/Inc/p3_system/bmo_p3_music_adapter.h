#ifndef BMO_P3_MUSIC_ADAPTER_H
#define BMO_P3_MUSIC_ADAPTER_H

#include <stdint.h>
#include "bmo_input.h"

void bmo_p3_music_start(void);
void bmo_p3_music_stop(void);
void bmo_p3_music_update(uint32_t now_ms);
void bmo_p3_music_handle_direction(bmo_input_direction_t direction);
void bmo_p3_music_handle_enter(void);

#endif /* BMO_P3_MUSIC_ADAPTER_H */
