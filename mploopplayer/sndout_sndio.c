#include <sndio.h>
#include <stdlib.h>
#include "sndout.h"
#include "sndoutdev.h"

static inline uint16_t bswap16(uint16_t u16)
{
	u16 =
		((u16 & 0xFF00U) >>  8) |
		((u16 & 0x00FFU) <<  8);
	return u16;
}
static inline uint32_t bswap32(uint32_t u32)
{
	u32 =
		((u32 & 0xFF000000U) >> 24) |
		((u32 & 0x00FF0000U) >>  8) |
		((u32 & 0x0000FF00U) <<  8) |
		((u32 & 0x000000FFU) << 24);
	return u32;
}

struct sndout_device_sndio {
	int obtained_bps;
	int obtained_bits;
	int obtained_le;
	int obtained_sig;
	int obtained_msb;
	struct sio_hdl *hdl;
	union {
		char *ch;
		unsigned char *uch;
		int16_t *i16;
		uint16_t *u16;
		int32_t *i32;
		uint32_t *u32;
	} samplesu;
	size_t samplessz;
};

static void free_samples(struct sndout_device_sndio *sio)
{
	if (sio->obtained_bits == 8 && sio->obtained_bps == 1)
	{
		if (sio->obtained_sig)
		{
			free(sio->samplesu.ch);
		}
		else
		{
			free(sio->samplesu.uch);
		}
	}
	else if (sio->obtained_bits == 16 && sio->obtained_bps == 2)
	{
		if (sio->obtained_sig)
		{
			free(sio->samplesu.i16);
		}
		else
		{
			free(sio->samplesu.u16);
		}
	}
	else if (sio->obtained_bits == 32 && sio->obtained_bps == 4)
	{
		if (sio->obtained_sig)
		{
			free(sio->samplesu.i32);
		}
		else
		{
			free(sio->samplesu.u32);
		}
	}
	else
	{
		abort();
	}
}

static inline struct sio_hdl *sndout_sio_hdl(struct sndout_device *dev)
{
	struct sndout_device_sndio *sndio = dev->userdata;
	return sndio->hdl;
}

int snd_out_sndio_always_block(struct sndout_device *dev)
{
	return 1;
}

void snd_out_sndio_block(struct sndout_device *dev)
{
	return;
}

void snd_out_sndio_unpause(struct sndout_device *dev)
{
	sio_start(sndout_sio_hdl(dev));
}

void snd_out_sndio_pause(struct sndout_device *dev)
{
	sio_stop(sndout_sio_hdl(dev));
}

void snd_out_sndio_discardflush(struct sndout_device *dev)
{
#if 0
	sio_flush(sndout_sio_hdl(dev));
#else
	sio_stop(sndout_sio_hdl(dev));
#endif
}

void snd_out_sndio_closeflush(struct sndout_device *dev)
{
}

void snd_out_sndio_close(struct sndout_device *dev)
{
	struct sndout_device_sndio *sndio = dev->userdata;
	sio_close(sndout_sio_hdl(dev));
	free_samples(sndio);
	free(sndio);
}

void snd_out_sndio_out(struct sndout_device *dev, const float *samples, unsigned numsamples)
{
	struct sndout_device_sndio *sndio = dev->userdata;
	unsigned i;
	while (numsamples > 0)
	{
		unsigned numsamplesall = dev->obtained.channels*numsamples;
		unsigned numsamplespossible = sndio->samplessz/dev->obtained.channels*dev->obtained.channels;
		if (numsamplesall > numsamplespossible)
		{
			numsamplesall = numsamplespossible;
		}
		if (sndio->obtained_bits == 8 && sndio->obtained_bps == 1)
		{
			if (sndio->obtained_sig)
			{
				char *samples2 = sndio->samplesu.ch;
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
				sio_write(sndio->hdl, samples2, numsamplesall);
			}
			else
			{
				unsigned char *samples2 = sndio->samplesu.uch;
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
				sio_write(sndio->hdl, samples2, numsamplesall);
			}
		}
		else if (sndio->obtained_bits == 16 && sndio->obtained_bps == 2)
		{
			if (sndio->obtained_le == SIO_LE_NATIVE)
			{
				if (sndio->obtained_sig)
				{
					int16_t *samples2 = sndio->samplesu.i16;
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
					sio_write(sndio->hdl, samples2, numsamplesall*sizeof(int16_t));
				}
				else
				{
					uint16_t *samples2 = sndio->samplesu.u16;
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
					sio_write(sndio->hdl, samples2, numsamplesall*sizeof(uint16_t));
				}
			}
			else
			{
				if (sndio->obtained_sig)
				{
					int16_t *samples2 = sndio->samplesu.i16;
					for (i = 0; i < numsamplesall; i++)
					{
						if (samples[i] >= 1)
						{
							samples2[i] = (int16_t)bswap16((uint16_t)32767);
						}
						else if (samples[i] <= -1)
						{
							samples2[i] = (int16_t)bswap16((uint16_t)-32767);
						}
						else
						{
							samples2[i] = (int16_t)bswap16((uint16_t)(32767*samples[i]));
						}
					}
					sio_write(sndio->hdl, samples2, numsamplesall*sizeof(int16_t));
				}
				else
				{
					uint16_t *samples2 = sndio->samplesu.u16;
					for (i = 0; i < numsamplesall; i++)
					{
						if (samples[i] >= 1)
						{
							samples2[i] = bswap16(32767*2);
						}
						else if (samples[i] <= -1)
						{
							samples2[i] = bswap16(0);
						}
						else
						{
							samples2[i] = bswap16(32767+32767*samples[i]);
						}
					}
					sio_write(sndio->hdl, samples2, numsamplesall*sizeof(uint16_t));
				}
			}
		}
		else if (sndio->obtained_bits == 32 && sndio->obtained_bps == 4)
		{
			if (sndio->obtained_le == SIO_LE_NATIVE)
			{
				if (sndio->obtained_sig)
				{
					int32_t *samples2 = sndio->samplesu.i32;
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
							samples2[i] = 2147483647*(double)samples[i];
						}
					}
					sio_write(sndio->hdl, samples2, numsamplesall*sizeof(int32_t));
				}
				else
				{
					uint32_t *samples2 = sndio->samplesu.u32;
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
							samples2[i] = 2147483647U+2147483647U*(double)samples[i];
						}
					}
					sio_write(sndio->hdl, samples2, numsamplesall*sizeof(uint32_t));
				}
			}
			else
			{
				if (sndio->obtained_sig)
				{
					int32_t *samples2 = sndio->samplesu.i32;
					for (i = 0; i < numsamplesall; i++)
					{
						if (samples[i] >= 1)
						{
							samples2[i] = (int32_t)bswap32((uint32_t)2147483647);
						}
						else if (samples[i] <= -1)
						{
							samples2[i] = (int32_t)bswap32((uint32_t)-2147483647);
						}
						else
						{
							samples2[i] = (int32_t)bswap32((uint32_t)2147483647*(double)samples[i]);
						}
					}
					sio_write(sndio->hdl, samples2, numsamplesall*sizeof(int32_t));
				}
				else
				{
					uint32_t *samples2 = sndio->samplesu.u32;
					for (i = 0; i < numsamplesall; i++)
					{
						if (samples[i] >= 1)
						{
							samples2[i] = bswap32(2147483647U*2);
						}
						else if (samples[i] <= -1)
						{
							samples2[i] = bswap32(0);
						}
						else
						{
							samples2[i] = bswap32(2147483647U+2147483647U*(double)samples[i]);
						}
					}
					sio_write(sndio->hdl, samples2, numsamplesall*sizeof(uint32_t));
				}
			}
		}
		else
		{
			abort();
		}
		samples += numsamplesall;
		numsamples -= numsamplesall/dev->obtained.channels;
	}
}

struct sndout_device *sndout_open_sndio(struct sndout_settings *desired,
                                        struct sndout_settings *obtained)
{
	struct sio_hdl *hdl;
	struct sio_par desiredsio;
	struct sio_par obtainedsio;
	struct sndout_device *dev;
	struct sndout_device_sndio *sio;
	int isnull = 0;
	hdl = sio_open(SIO_DEVANY, SIO_PLAY, 0);
	if (hdl == NULL)
	{
		return NULL;
	}
	sio_initpar(&desiredsio);
	desiredsio.bits = 32;
	desiredsio.bps = SIO_BPS(32);
	desiredsio.sig = 1;
	desiredsio.le = SIO_LE_NATIVE;
	desiredsio.msb = 1;
	desiredsio.rate = desired->rate;
	desiredsio.pchan = desired->channels;
	if (!sio_setpar(hdl, &desiredsio))
	{
		sio_close(hdl);
		return NULL;
	}
	if (!sio_getpar(hdl, &obtainedsio))
	{
		sio_close(hdl);
		return NULL;
	}
	obtained->channels = obtainedsio.pchan;
	obtained->rate = obtainedsio.rate;
	sio = malloc(sizeof(*sio));
	if (!sio)
	{
		sio_close(hdl);
		return NULL;
	}
	sio->hdl = hdl;
	sio->obtained_bps = (int)obtainedsio.bps;
	sio->obtained_bits = (int)obtainedsio.bits;
	sio->obtained_le = !!obtainedsio.le;
	sio->obtained_sig = !!obtainedsio.sig;
	sio->obtained_msb = !!obtainedsio.msb;
	sio->samplessz = obtained->rate*obtained->channels/2; // half a second
	if (sio->obtained_bits == 8 && sio->obtained_bps == 1)
	{
		if (sio->obtained_sig)
		{
			sio->samplesu.ch = malloc(sio->samplessz);
			isnull = (sio->samplesu.ch == NULL);
		}
		else
		{
			sio->samplesu.uch = malloc(sio->samplessz);
			isnull = (sio->samplesu.uch == NULL);
		}
	}
	else if (sio->obtained_bits == 16 && sio->obtained_bps == 2)
	{
		if (sio->obtained_sig)
		{
			sio->samplesu.i16 = malloc(sio->samplessz*sizeof(*sio->samplesu.i16));
			isnull = (sio->samplesu.i16 == NULL);
		}
		else
		{
			sio->samplesu.u16 = malloc(sio->samplessz*sizeof(*sio->samplesu.u16));
			isnull = (sio->samplesu.u16 == NULL);
		}
	}
	else if (sio->obtained_bits == 32 && sio->obtained_bps == 4)
	{
		if (sio->obtained_sig)
		{
			sio->samplesu.i32 = malloc(sio->samplessz*sizeof(*sio->samplesu.i32));
			isnull = (sio->samplesu.i32 == NULL);
		}
		else
		{
			sio->samplesu.u32 = malloc(sio->samplessz*sizeof(*sio->samplesu.u32));
			isnull = (sio->samplesu.u32 == NULL);
		}
	}
	else
	{
		sio_close(hdl);
		free(sio);
		return NULL;
	}
	if (isnull)
	{
		sio_close(hdl);
		free(sio);
		return NULL;
	}
	dev = malloc(sizeof(*dev));
	if (!dev)
	{
		free_samples(sio);
		sio_close(hdl);
		free(sio);
		return NULL;
	}
	dev->obtained.channels = obtainedsio.pchan;
	dev->obtained.rate = obtainedsio.rate;
	dev->userdata = sio;
	dev->always_block = snd_out_sndio_always_block;
	dev->block = snd_out_sndio_block;
	dev->unpause = snd_out_sndio_unpause;
	dev->pause = snd_out_sndio_pause;
	dev->discardflush = snd_out_sndio_discardflush;
	dev->closeflush = snd_out_sndio_closeflush;
	dev->close = snd_out_sndio_close;
	dev->out = snd_out_sndio_out;
	return dev;
}
