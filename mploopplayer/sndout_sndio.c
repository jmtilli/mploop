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
};

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
	sio_close(sndout_sio_hdl(dev));
}

void snd_out_sndio_out(struct sndout_device *dev, const float *samples, unsigned numsamples)
{
	struct sndout_device_sndio *sndio = dev->userdata;
	unsigned numsamplesall = dev->obtained.channels*numsamples;
	unsigned i;
	if (sndio->obtained_bits == 8 && sndio->obtained_bps == 1)
	{
		if (sndio->obtained_sig)
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
			sio_write(sndio->hdl, samples2, numsamplesall);
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
			sio_write(sndio->hdl, samples2, numsamplesall);
			free(samples2);
		}
	}
	else if (sndio->obtained_bits == 16 && sndio->obtained_bps == 2)
	{
		if (sndio->obtained_le == SIO_LE_NATIVE)
		{
			if (sndio->obtained_sig)
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
				sio_write(sndio->hdl, samples2, numsamplesall*sizeof(int16_t));
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
				sio_write(sndio->hdl, samples2, numsamplesall*sizeof(uint16_t));
				free(samples2);
			}
		}
		else
		{
			if (sndio->obtained_sig)
			{
				int16_t *samples2 = malloc(numsamplesall*sizeof(int16_t));
				for (i = 0; i < numsamplesall; i++)
				{
					if (samples[i] >= 1)
					{
						samples2[i] = bswap16(32767);
					}
					else if (samples[i] <= -1)
					{
						samples2[i] = bswap16(-32767);
					}
					else
					{
						samples2[i] = bswap16(32767*samples[i]);
					}
				}
				sio_write(sndio->hdl, samples2, numsamplesall*sizeof(int16_t));
				free(samples2);
			}
			else
			{
				uint16_t *samples2 = malloc(numsamplesall*sizeof(uint16_t));
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
				free(samples2);
			}
		}
	}
	else if (sndio->obtained_bits == 32 && sndio->obtained_bps == 4)
	{
		if (sndio->obtained_le == SIO_LE_NATIVE)
		{
			if (sndio->obtained_sig)
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
				sio_write(sndio->hdl, samples2, numsamplesall*sizeof(int32_t));
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
				sio_write(sndio->hdl, samples2, numsamplesall*sizeof(uint32_t));
				free(samples2);
			}
		}
		else
		{
			if (sndio->obtained_sig)
			{
				int32_t *samples2 = malloc(numsamplesall*sizeof(int32_t));
				for (i = 0; i < numsamplesall; i++)
				{
					if (samples[i] >= 1)
					{
						samples2[i] = bswap32(2147483647);
					}
					else if (samples[i] <= -1)
					{
						samples2[i] = bswap32(-2147483647);
					}
					else
					{
						samples2[i] = bswap32(2147483647*samples[i]);
					}
				}
				sio_write(sndio->hdl, samples2, numsamplesall*sizeof(int32_t));
				free(samples2);
			}
			else
			{
				uint32_t *samples2 = malloc(numsamplesall*sizeof(uint32_t));
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
						samples2[i] = bswap32(2147483647U+2147483647U*samples[i]);
					}
				}
				sio_write(sndio->hdl, samples2, numsamplesall*sizeof(uint32_t));
				free(samples2);
			}
		}
	}
	else
	{
		abort();
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
	sio->obtained_bps = obtainedsio.bps;
	sio->obtained_bits = obtainedsio.bits;
	sio->obtained_le = !!obtainedsio.le;
	sio->obtained_sig = !!obtainedsio.sig;
	sio->obtained_msb = !!obtainedsio.msb;
	dev = malloc(sizeof(*dev));
	if (!dev)
	{
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
