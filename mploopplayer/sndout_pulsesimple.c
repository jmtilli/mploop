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
	int32_t *samples;
	unsigned samplessz;
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
	struct sndout_device_pulsesimple *ps = dev->userdata;
	pa_simple_free(sndout_ps_s(dev));
	free(ps->samples);
	free(ps);
}

void snd_out_ps_out(struct sndout_device *dev, const float *samples, unsigned numsamples)
{
	struct sndout_device_pulsesimple *ps = dev->userdata;
	unsigned i;
	int error;
	if (ps->obtained_bits != 32 || ps->obtained_bps != 4 || !ps->obtained_sig)
	{
		abort();
	}
	while (numsamples > 0)
	{
		unsigned numsamplesall = dev->obtained.channels*numsamples;
		unsigned numsamplespossible = ps->samplessz/dev->obtained.channels * dev->obtained.channels;
		if (numsamplesall > numsamplespossible)
		{
			numsamplesall = numsamplespossible;
		}
		for (i = 0; i < numsamplesall; i++)
		{
			if (samples[i] >= 1)
			{
				ps->samples[i] = 2147483647;
			}
			else if (samples[i] <= -1)
			{
				ps->samples[i] = -2147483647;
			}
			else
			{
				ps->samples[i] = 2147483647*(double)samples[i];
			}
		}
		pa_simple_write(ps->s, ps->samples, numsamplesall*sizeof(int32_t), &error);
		samples += numsamplesall;
		numsamples -= numsamplesall/dev->obtained.channels;
	}
}

struct sndout_device *sndout_open_pulsesimple(struct sndout_settings *desired,
                                        struct sndout_settings *obtained)
{
	struct pa_sample_spec desiredps = {.rate = 0};
	struct sndout_device *dev;
	struct sndout_device_pulsesimple *ps;
	struct pa_simple *s;
	int error = 0;
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
	ps->samplessz = obtained->rate*obtained->channels/2; // half a second
	ps->samples = malloc(ps->samplessz*sizeof(*ps->samples));
	if (ps->samples == NULL)
	{
		pa_simple_free(s);
		free(ps);
		return NULL;
	}
	dev = malloc(sizeof(*dev));
	if (!dev)
	{
		free(ps->samples);
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
