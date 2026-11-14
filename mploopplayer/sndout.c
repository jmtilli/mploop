#include <sndio.h>
#include <stdlib.h>
#include "sndout.h"
#include "sndoutdev.h"

int snd_out_always_block(struct sndout_device *dev)
{
	return dev->always_block(dev);
}
void snd_out_block(struct sndout_device *dev)
{
	dev->block(dev);
}
void snd_out_unpause(struct sndout_device *dev)
{
	dev->unpause(dev);
}
void snd_out_pause(struct sndout_device *dev)
{
	dev->pause(dev);
}
void snd_out_discardflush(struct sndout_device *dev)
{
	dev->discardflush(dev);
}
void snd_out_closeflush(struct sndout_device *dev)
{
	dev->closeflush(dev);
}
void snd_out_close(struct sndout_device *dev)
{
	if (!dev)
	{
		return;
	}
	dev->close(dev);
	free(dev);
}
void snd_out(struct sndout_device *dev, const float *samples, unsigned numsamples)
{
	dev->out(dev, samples, numsamples);
}
