#ifndef _SNDOUT_H_
#define _SNDOUT_H_

#include <stdint.h>

struct sndout_device;

struct sndout_settings {
  unsigned rate;
  uint16_t channels;
};

struct sndout_device *sndout_open_sndio(struct sndout_settings *desired,
                                        struct sndout_settings *obtained);

struct sndout_device *sndout_open_sdl2(struct sndout_settings *desired,
                                       struct sndout_settings *obtained);

struct sndout_device *sndout_open_pulsesimple(struct sndout_settings *desired,
                                       struct sndout_settings *obtained);

int snd_out_always_block(struct sndout_device *dev);

void snd_out_block(struct sndout_device *dev);

void snd_out_unpause(struct sndout_device *dev);

void snd_out_pause(struct sndout_device *dev);

void snd_out_discardflush(struct sndout_device *dev);

void snd_out_closeflush(struct sndout_device *dev);

void snd_out_close(struct sndout_device *dev);

void snd_out(struct sndout_device *dev, const float *samples, unsigned numsamples);

#endif
