#include <SDL.h>
#include <stdlib.h>
#include <unistd.h>
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
static inline float bswapflt(float f)
{
	uint32_t u32;
	memcpy(&u32, &f, sizeof(u32));
	u32 = bswap32(u32);
	memcpy(&f, &u32, sizeof(u32));
	return f;
}

struct sndout_device_sdl2 {
	int obtained_bps;
	int obtained_bits;
	int obtained_le;
	int obtained_sig;
	int obtained_msb;
	int obtained_is_float;
	SDL_AudioDeviceID audid;
};

static inline SDL_AudioDeviceID sndout_sdl_audid(struct sndout_device *dev)
{
	struct sndout_device_sdl2 *sdl2 = dev->userdata;
	return sdl2->audid;
}

int snd_out_sdl2_always_block(struct sndout_device *dev)
{
	return 0;
}

void snd_out_sdl2_block(struct sndout_device *dev)
{
	struct sndout_device_sdl2 *sdl2 = dev->userdata;
	uint32_t bytes_per_sec = (uint32_t)(dev->obtained.rate*dev->obtained.channels*sdl2->obtained_bps);
	uint32_t target_samples = (uint32_t)(0.15*bytes_per_sec);
	for (;;)
	{
		uint32_t inqueue = (uint32_t)SDL_GetQueuedAudioSize(sdl2->audid);
		if (inqueue <= target_samples)
		{
			break;
		}
		usleep((useconds_t)((inqueue - target_samples)*1e6/bytes_per_sec));
	}
	return;
}

void snd_out_sdl2_unpause(struct sndout_device *dev)
{
	SDL_PauseAudioDevice(sndout_sdl_audid(dev), 0);
}

void snd_out_sdl2_pause(struct sndout_device *dev)
{
	SDL_PauseAudioDevice(sndout_sdl_audid(dev), 1);
}

void snd_out_sdl2_discardflush(struct sndout_device *dev)
{
	SDL_ClearQueuedAudio(sndout_sdl_audid(dev));
	SDL_PauseAudioDevice(sndout_sdl_audid(dev), 1);
}

void snd_out_sdl2_closeflush(struct sndout_device *dev)
{
	struct sndout_device_sdl2 *sdl2 = dev->userdata;
	uint32_t bytes_per_sec = (uint32_t)(dev->obtained.rate*dev->obtained.channels*sdl2->obtained_bps);
	uint32_t inqueue;
	while ((inqueue = (uint32_t)SDL_GetQueuedAudioSize(sdl2->audid)) > 0) {
		usleep((useconds_t)(inqueue*0.9e6/bytes_per_sec)+300);
	}
}

void snd_out_sdl2_close(struct sndout_device *dev)
{
	SDL_CloseAudioDevice(sndout_sdl_audid(dev));
}

void snd_out_sdl2_out(struct sndout_device *dev, const float *samples, unsigned numsamples)
{
	struct sndout_device_sdl2 *sdl2 = dev->userdata;
	SDL_AudioDeviceID audid = sdl2->audid;
	unsigned numsamplesall = dev->obtained.channels*numsamples;
	unsigned i;
	if (sdl2->obtained_is_float)
	{
		if (sdl2->obtained_le == (SDL_BYTEORDER != SDL_BIG_ENDIAN))
		{
			SDL_QueueAudio(audid, samples, numsamplesall);
		}
		else
		{
			float *samples2 = malloc(numsamplesall*sizeof(float));
			for (i = 0; i < numsamplesall; i++)
			{
				samples2[i] = bswapflt(samples[i]);
			}
			SDL_QueueAudio(audid, samples2, numsamplesall);
			free(samples2);
		}
	}
	else if (sdl2->obtained_bits == 8 && sdl2->obtained_bps == 1)
	{
		if (sdl2->obtained_sig)
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
			SDL_QueueAudio(audid, samples2, numsamplesall);
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
			SDL_QueueAudio(audid, samples2, numsamplesall);
			free(samples2);
		}
	}
	else if (sdl2->obtained_bits == 16 && sdl2->obtained_bps == 2)
	{
		if (sdl2->obtained_le == (SDL_BYTEORDER != SDL_BIG_ENDIAN))
		{
			if (sdl2->obtained_sig)
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
				SDL_QueueAudio(audid, samples2, numsamplesall*sizeof(int16_t));
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
				SDL_QueueAudio(audid, samples2, numsamplesall*sizeof(uint16_t));
				free(samples2);
			}
		}
		else
		{
			if (sdl2->obtained_sig)
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
				SDL_QueueAudio(audid, samples2, numsamplesall*sizeof(int16_t));
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
				SDL_QueueAudio(audid, samples2, numsamplesall*sizeof(uint16_t));
				free(samples2);
			}
		}
	}
	else if (sdl2->obtained_bits == 32 && sdl2->obtained_bps == 4)
	{
		if (sdl2->obtained_le == (SDL_BYTEORDER != SDL_BIG_ENDIAN))
		{
			if (sdl2->obtained_sig)
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
				SDL_QueueAudio(audid, samples2, numsamplesall*sizeof(int32_t));
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
				SDL_QueueAudio(audid, samples2, numsamplesall*sizeof(uint32_t));
				free(samples2);
			}
		}
		else
		{
			if (sdl2->obtained_sig)
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
				SDL_QueueAudio(audid, samples2, numsamplesall*sizeof(int32_t));
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
				SDL_QueueAudio(audid, samples2, numsamplesall*sizeof(uint32_t));
				free(samples2);
			}
		}
	}
	else
	{
		abort();
	}
}

struct sndout_device *sndout_open_sdl2(struct sndout_settings *desired,
                                       struct sndout_settings *obtained)
{
	SDL_AudioDeviceID audid;
	SDL_AudioSpec desiredsdl;
	SDL_AudioSpec obtainedsdl;
	struct sndout_device *dev;
	struct sndout_device_sdl2 *sdl2;

	SDL_Init(SDL_INIT_AUDIO);
	desiredsdl.freq = desired->rate;
	desiredsdl.format = AUDIO_S32;
	desiredsdl.channels = desired->channels;
	desiredsdl.samples = 4096;
	desiredsdl.callback = NULL;
	desiredsdl.userdata = NULL;
	audid = SDL_OpenAudioDevice(NULL, 0, &desiredsdl, &obtainedsdl, SDL_AUDIO_ALLOW_ANY_CHANGE);
	if (audid <= 0)
	{
		return NULL;
	}
	obtained->channels = obtainedsdl.channels;
	obtained->rate = obtainedsdl.freq;
	sdl2 = malloc(sizeof(*sdl2));
	if (!sdl2)
	{
		SDL_CloseAudioDevice(audid);
		return NULL;
	}
	sdl2->audid = audid;
	switch (obtainedsdl.format)
	{
		case AUDIO_F32MSB:
		case AUDIO_F32LSB:
		case AUDIO_S32MSB:
		case AUDIO_S32LSB:
			sdl2->obtained_bps = 4;
			sdl2->obtained_bits = 32;
			break;
		case AUDIO_S16MSB:
		case AUDIO_S16LSB:
		case AUDIO_U16MSB:
		case AUDIO_U16LSB:
			sdl2->obtained_bps = 2;
			sdl2->obtained_bits = 16;
			break;
		case AUDIO_S8:
		case AUDIO_U8:
			sdl2->obtained_bps = 1;
			sdl2->obtained_bits = 8;
			break;
		default:
			SDL_CloseAudioDevice(audid);
			free(sdl2);
			return NULL;
	}
	switch (obtainedsdl.format)
	{
		case AUDIO_F32LSB:
		case AUDIO_F32MSB:
			sdl2->obtained_sig = 1;
			sdl2->obtained_is_float = 1;
			break;
		case AUDIO_S32MSB:
		case AUDIO_S32LSB:
		case AUDIO_S16MSB:
		case AUDIO_S16LSB:
		case AUDIO_S8:
			sdl2->obtained_sig = 1;
			sdl2->obtained_is_float = 0;
			break;
		case AUDIO_U16MSB:
		case AUDIO_U16LSB:
		case AUDIO_U8:
			sdl2->obtained_sig = 0;
			sdl2->obtained_is_float = 0;
			break;
		default:
			SDL_CloseAudioDevice(audid);
			free(sdl2);
			return NULL;
	}
	switch (obtainedsdl.format)
	{
		case AUDIO_S32LSB:
		case AUDIO_S16LSB:
		case AUDIO_U16LSB:
		case AUDIO_F32LSB:
			sdl2->obtained_le = 1;
			break;
		case AUDIO_S32MSB:
		case AUDIO_S16MSB:
		case AUDIO_U16MSB:
		case AUDIO_F32MSB:
			sdl2->obtained_le = 0;
			break;
		case AUDIO_U8:
		case AUDIO_S8:
			sdl2->obtained_le = 0;
			break;
	}
	sdl2->obtained_msb = 1;
	dev = malloc(sizeof(*dev));
	if (!dev)
	{
		SDL_CloseAudioDevice(audid);
		free(sdl2);
		return NULL;
	}
	dev->obtained.channels = obtainedsdl.channels;
	dev->obtained.rate = obtainedsdl.freq;
	dev->userdata = sdl2;
	dev->always_block = snd_out_sdl2_always_block;
	dev->block = snd_out_sdl2_block;
	dev->unpause = snd_out_sdl2_unpause;
	dev->pause = snd_out_sdl2_pause;
	dev->discardflush = snd_out_sdl2_discardflush;
	dev->closeflush = snd_out_sdl2_closeflush;
	dev->close = snd_out_sdl2_close;
	dev->out = snd_out_sdl2_out;
	return dev;
}
