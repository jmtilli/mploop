#include <pulse/error.h>
#include <pulse/simple.h>
#include <stdlib.h>
#include "sndout.h"
#include "sndoutdev.h"

struct sndout_device_pulsesimple {
	int obtained_bps;
	int obtained_bits;
	int obtained_sig;
	struct pa_simple *s;
};

static inline struct pa_simple *sndout_ps_s(struct sndout_device *dev)
{
	struct sndout_device_pulsesimple *ps = dev->userdata;
	return ps->s;
}

int snd_out_ps_always_block(struct sndout_device *dev)
{
	return 1;
}

void snd_out_ps_block(struct sndout_device *dev)
{
	return;
}

void snd_out_ps_unpause(struct sndout_device *dev)
{
	//sio_start(sndout_sio_hdl(dev));
}

void snd_out_ps_pause(struct sndout_device *dev)
{
	//sio_stop(sndout_sio_hdl(dev));
}

void snd_out_ps_discardflush(struct sndout_device *dev)
{
}

void snd_out_ps_closeflush(struct sndout_device *dev)
{
	int error;
	pa_simple_drain(sndout_ps_s(dev), &error);
}

void snd_out_ps_close(struct sndout_device *dev)
{
	pa_simple_free(sndout_ps_s(dev));
}

void snd_out_ps_out(struct sndout_device *dev, const float *samples, unsigned numsamples)
{
	struct sndout_device_pulsesimple *ps = dev->userdata;
	unsigned numsamplesall = dev->obtained.channels*numsamples;
	unsigned i;
	int error;
	if (ps->obtained_bits == 8 && ps->obtained_bps == 1)
	{
		if (ps->obtained_sig)
		{
			char *samples2 = malloc(numsamplesall);
			for (i = 0; i < numsamplesall; i++)
			{
				if (samples[i] >= 1)
				{
					samples2[i] = 127;
				}
				else if (samples[i] <= -1)
				{
					samples2[i] = -127;
				}
				else
				{
					samples2[i] = 127*samples[i];
				}
			}
			pa_simple_write(ps->s, samples2, numsamplesall, &error);
			free(samples2);
		}
		else
		{
			unsigned char *samples2 = malloc(numsamplesall);
			for (i = 0; i < numsamplesall; i++)
			{
				if (samples[i] >= 1)
				{
					samples2[i] = 254;
				}
				else if (samples[i] <= -1)
				{
					samples2[i] = 0;
				}
				else
				{
					samples2[i] = 127+127*samples[i];
				}
			}
			pa_simple_write(ps->s, samples2, numsamplesall, &error);
			free(samples2);
		}
	}
	else if (ps->obtained_bits == 16 && ps->obtained_bps == 2)
	{
		if (ps->obtained_sig)
		{
			int16_t *samples2 = malloc(numsamplesall*sizeof(int16_t));
			for (i = 0; i < numsamplesall; i++)
			{
				if (samples[i] >= 1)
				{
					samples2[i] = 32767;
				}
				else if (samples[i] <= -1)
				{
					samples2[i] = -32767;
				}
				else
				{
					samples2[i] = 32767*samples[i];
				}
			}
			pa_simple_write(ps->s, samples2, numsamplesall*sizeof(int16_t), &error);
			free(samples2);
		}
		else
		{
			uint16_t *samples2 = malloc(numsamplesall*sizeof(uint16_t));
			for (i = 0; i < numsamplesall; i++)
			{
				if (samples[i] >= 1)
				{
					samples2[i] = 32767*2;
				}
				else if (samples[i] <= -1)
				{
					samples2[i] = 0;
				}
				else
				{
					samples2[i] = 32767+32767*samples[i];
				}
			}
			pa_simple_write(ps->s, samples2, numsamplesall*sizeof(uint16_t), &error);
			free(samples2);
		}
	}
	else if (ps->obtained_bits == 32 && ps->obtained_bps == 4)
	{
		if (ps->obtained_sig)
		{
			int32_t *samples2 = malloc(numsamplesall*sizeof(int32_t));
			for (i = 0; i < numsamplesall; i++)
			{
				if (samples[i] >= 1)
				{
					samples2[i] = 2147483647;
				}
				else if (samples[i] <= -1)
				{
					samples2[i] = -2147483647;
				}
				else
				{
					samples2[i] = 2147483647*samples[i];
				}
			}
			pa_simple_write(ps->s, samples2, numsamplesall*sizeof(int32_t), &error);
			free(samples2);
		}
		else
		{
			uint32_t *samples2 = malloc(numsamplesall*sizeof(uint32_t));
			for (i = 0; i < numsamplesall; i++)
			{
				if (samples[i] >= 1)
				{
					samples2[i] = 2147483647U*2;
				}
				else if (samples[i] <= -1)
				{
					samples2[i] = 0;
				}
				else
				{
					samples2[i] = 2147483647U+2147483647U*samples[i];
				}
			}
			pa_simple_write(ps->s, samples2, numsamplesall*sizeof(uint32_t), &error);
			free(samples2);
		}
	}
	else
	{
		abort();
	}
}

struct sndout_device *sndout_open_pulsesimple(struct sndout_settings *desired,
                                        struct sndout_settings *obtained)
{
	struct pa_sample_spec desiredps;
	struct sndout_device *dev;
	struct sndout_device_pulsesimple *ps;
	struct pa_simple *s;
	int error;
	desiredps.format = PA_SAMPLE_S32NE;
	desiredps.rate = desired->rate;
	desiredps.channels = desired->channels;
	s = pa_simple_new(NULL, "mploopplayer", PA_STREAM_PLAYBACK, NULL, "sndout", &desiredps, NULL, NULL, &error);
	if (!s)
	{
		return NULL;
	}
	obtained->channels = desired->channels;
	obtained->rate = desired->rate;
	ps = malloc(sizeof(*ps));
	if (!ps)
	{
		pa_simple_free(s);
		return NULL;
	}
	ps->s = s;
	ps->obtained_bps = 4;
	ps->obtained_bits = 32;
	ps->obtained_sig = 1;
	dev = malloc(sizeof(*dev));
	if (!dev)
	{
		pa_simple_free(s);
		free(ps);
		return NULL;
	}
	dev->obtained.channels = desired->channels;
	dev->obtained.rate = desired->rate;
	dev->userdata = ps;
	dev->always_block = snd_out_ps_always_block;
	dev->block = snd_out_ps_block;
	dev->unpause = snd_out_ps_unpause;
	dev->pause = snd_out_ps_pause;
	dev->discardflush = snd_out_ps_discardflush;
	dev->closeflush = snd_out_ps_closeflush;
	dev->close = snd_out_ps_close;
	dev->out = snd_out_ps_out;
	return dev;
}
