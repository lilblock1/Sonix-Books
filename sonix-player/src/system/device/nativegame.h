#ifndef NATIVEGAME_H
#define NATIVEGAME_H
#include <stdbool.h>
#include <stdint.h>
#define NATIVEGAME_W 480
#define NATIVEGAME_H_PIX 360
// GUI starts one managed game; worker owns all libretro calls and audio.
bool nativegame_start(const char *id, const char *rom);
void nativegame_stop(void);
bool nativegame_busy(void);
bool nativegame_failed(void);
bool nativegame_present(uint16_t *pixels, uint32_t *serial);
void nativegame_keys(uint16_t keys);
void nativegame_save(void);
void nativegame_load(void);
// 0 uses native timing. Per-ROM cap is loaded and saved by the worker.
void nativegame_pause(bool paused);
bool nativegame_set_fps(unsigned fps);
void nativegame_get_fps(unsigned *limit, double *native, bool *save_error);
#endif
