#ifndef _SNDOUTDEV_H_
#define _SNDOUTDEV_H_

#include <stdlib.h>
#include "sndout.h"

struct sndout_device {
	int (*always_block)(struct sndout_device *dev);
	void (*block)(struct sndout_device *dev);
	void (*unpause)(struct sndout_device *dev);
	void (*pause)(struct sndout_device *dev);
	void (*discardflush)(struct sndout_device *dev);
	void (*closeflush)(struct sndout_device *dev);
	void (*close)(struct sndout_device *dev);
	void (*out)(struct sndout_device *dev, const float *samples, unsigned numsamples);
	struct sndout_settings obtained;
	void *userdata;
};

#endif
