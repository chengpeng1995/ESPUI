#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
bool board_audio_init(void);
bool board_audio_record_open(void);
bool board_audio_play_open(int volume);
bool board_audio_read(int16_t* slots,size_t samples);
bool board_audio_write(int16_t* stereo,size_t samples);
void board_audio_close(void);
void board_audio_volume(int volume);
#ifdef __cplusplus
}
#endif
