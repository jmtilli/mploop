#include <stdio.h>
#include <stdarg.h>
#include <termios.h>
#include <signal.h>
#include <math.h>
#include <SDL2/SDL.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavutil/timestamp.h>
#include <libavutil/samplefmt.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>

SDL_AudioDeviceID audid;
SDL_AudioSpec obtained;
AVCodecContext *adecctx;
SDL_AudioStream *sdl_stream;
int audio_frame_count;
int chcount;
int data_size;
int obtained_data_size;
char buf[1048576];
int bufloc;
int nonplanar = 0;
float duration;
int audio_format_as_sdl = -12345;

void handler_impl(void)
{
	struct termios term;
	tcgetattr(fileno(stdin), &term);
	term.c_lflag |= ECHO;
	term.c_lflag |= ICANON;
	tcsetattr(fileno(stdin), 0, &term);
	if (write(1, "\n", 1) < 0) {
		exit(1);
	}
}

void handler(int signum)
{
	(void)signum;
	handler_impl();
	exit(0);
}

int max_len = 0;

const int EXT_LEFT = 65335+1;
const int EXT_RIGHT = 65335+2;
const int EXT_UP = 65335+3;
const int EXT_DOWN = 65335+4;
const int EXT_PGUP = 65335+5;
const int EXT_PGDOWN = 65335+6;

// My current terminal:
// right: "\x1b[C"
// left: "\x1b[D"
// up: "\x1b[A"
// down: "\x1b[B"
// page up: "\x1b[5~"
// page down: "\x1b[6~"
//

int64_t gettime64(void)
{
	struct timespec tp;
	struct timeval tv;
	if (clock_gettime(CLOCK_MONOTONIC, &tp) == 0) {
		return ((int64_t)tp.tv_sec)*1000*1000 + (int64_t)(tp.tv_nsec/1000);
	}
	gettimeofday(&tv, NULL);
	return ((int64_t)tv.tv_sec)*1000*1000 + (int64_t)(tv.tv_usec);
}

int64_t escape_time64 = 0;
char escapebuf[16];
int escapebufsize = 0;

/*
 * TODO use termcap to use correct escape sequences of the terminal
 */
int read_char(void)
{
	char ch;
	int old_flags;
	struct pollfd pfd = {};
#if 0
	struct termios attrs;
	struct termios attrs2;
#endif
	pfd.fd = 0;
	pfd.events = POLLIN;
	pfd.revents = 0;
	if (poll(&pfd, 1, 0) < 1) {
		return '\0';
	}
	old_flags = fcntl(0, F_GETFL);
	if (old_flags < 0) {
		return '\0';
	}
	fcntl(0, F_SETFL, old_flags | O_NONBLOCK);
#if 0
	if (tcgetattr(0, &attrs) < 0) {
		return '\0';
	}
	attrs2 = attrs;
	attrs2.c_lflag &= ~ICANON;
	tcsetattr(0, TCSANOW, &attrs2);
#endif
	if (read(0, &ch, 1) < 1) {
		ch = '\0';
	}
	if (ch == '\x1b') {
		escape_time64 = gettime64();
		escapebuf[0] = ch;
		escapebufsize = 1;
		ch = '\0';
	}
	if (gettime64() - escape_time64 >= 1000000 && escapebufsize > 0) {
		escapebufsize = 0;
	}
	if ((gettime64() - escape_time64 < 1000000) && escapebufsize == 1 && ch == '[') {
		escapebuf[1] = ch;
		escapebufsize = 2;
		ch = '\0';
	}
	if ((gettime64() - escape_time64 < 1000000) && escapebufsize == 1 && ch == 'O') {
		escapebufsize = 0;
		ch = '\0';
	}
	if ((gettime64() - escape_time64 < 1000000) && escapebufsize == 2 && (ch == 'H' || ch == 'F' || ch == 'E')) {
		escapebufsize = 0;
		ch = '\0';
	}
	if ((gettime64() - escape_time64 < 1000000) && escapebufsize == 2 && (ch == 'A' || ch == 'B' || ch == 'C' || ch == 'D')) {
		if (ch == 'A') {
			escapebufsize = 0;
			fcntl(0, F_SETFL, old_flags);
			return EXT_UP;
		}
		if (ch == 'B') {
			escapebufsize = 0;
			fcntl(0, F_SETFL, old_flags);
			return EXT_DOWN;
		}
		if (ch == 'C') {
			escapebufsize = 0;
			fcntl(0, F_SETFL, old_flags);
			return EXT_RIGHT;
		}
		if (ch == 'D') {
			escapebufsize = 0;
			fcntl(0, F_SETFL, old_flags);
			return EXT_LEFT;
		}
		ch = '\0';
	}
	if ((gettime64() - escape_time64 < 1000000) && escapebufsize == 2 && (ch == '5' || ch == '6')) {
		escapebuf[2] = ch;
		escapebufsize = 3;
		ch = '\0';
	}
	if ((gettime64() - escape_time64 < 1000000) && escapebufsize >= 2 && (ch == '~')) {
		if (escapebufsize == 3 && escapebuf[0] == '\x1b' && escapebuf[1] == '[' && escapebuf[2] == '5') {
			escapebufsize = 0;
			fcntl(0, F_SETFL, old_flags);
			return EXT_PGUP;
		}
		if (escapebufsize == 3 && escapebuf[0] == '\x1b' && escapebuf[1] == '[' && escapebuf[2] == '6') {
			escapebufsize = 0;
			fcntl(0, F_SETFL, old_flags);
			return EXT_PGDOWN;
		}
		ch = '\0';
		escapebufsize = 0;
	}
#if 0
	tcsetattr(0, TCSANOW, &attrs);
#endif
	fcntl(0, F_SETFL, old_flags);
	return ch;
}

void print_status(const char *fmt, ...)
{
	va_list ap;
	int len;
	int i;
	char *buf;
	va_start(ap, fmt);
	len = vsnprintf(NULL, 0, fmt, ap);
	if (len > max_len)
	{
		max_len = len;
	}
	va_end(ap);
	buf = malloc(len+1);
	if (buf == NULL) {
		fprintf(stderr, "Cannot allocate line buffer, probably out of memory\n");
		handler_impl();
		exit(1);
	}
	va_start(ap, fmt);
	vsnprintf(buf, len+1, fmt, ap);
	va_end(ap);
	for (i = 0; i < max_len; i++)
	{
		putchar(' ');
	}
	putchar('\r');
	printf("%s\r", buf);
	free(buf);
}

int seeks = 0;
int64_t pts = 0;

float volume_mul = 1.0;

static inline float for_every_sample(float x)
{
	float res = x * volume_mul;
	if (res > 1) {
		res = 1;
	} else if (res < -1) {
		res = -1;
	}
	return res;
}
static inline int32_t for_every_sample32(int32_t x)
{
	float res = ((float)x) * volume_mul;
	if (res >= (float)INT32_MAX) {
		return INT32_MAX;
	}
	else if (res <= (float)INT32_MIN) {
		return INT32_MIN;
	}
	return ((int32_t)res);
}
static inline int32_t for_every_sample16(int16_t x)
{
	float res = ((float)x) * volume_mul;
	if (res >= (float)INT16_MAX) {
		return INT16_MAX;
	}
	else if (res <= (float)INT16_MIN) {
		return INT16_MIN;
	}
	return ((int16_t)res);
}
static inline uint8_t for_every_sample8(uint8_t x)
{
	float res = x-128; // range: -128 to 127
	res *= volume_mul;
	if (res >= 127) {
		return 127 + 128;
	}
	if (res <= -128) {
		return -128 + 128;
	}
	return (uint8_t)(res + 128);
}

void output_audio_frame(AVFrame *frame)
{
	int i;
	int ch;
#if 0
	size_t unpadded_linesize = frame->nb_samples * av_get_bytes_per_sample(frame->format);
#endif
	pts = frame->pts;
	float mytime = ((float)frame->pts) * ((float)adecctx->time_base.num) / (float)adecctx->time_base.den;
	print_status("A: %.1f / %.1f", mytime, duration);
	fflush(stdout);
	bufloc = 0;
	for (i = 0; i < frame->nb_samples; i++) {
		for (ch = 0; ch < (chcount>2 ? 2 : chcount); ch++) {
			if (bufloc + data_size > (int)sizeof(buf)) {
				fprintf(stderr, "Buffer size insufficient, very strange audio file\n");
				handler_impl();
				exit(1);
			}
			if (nonplanar) {
				if (data_size == 1) {
					uint8_t x;
					memcpy(&x, frame->data[0] + data_size*(i*chcount+ch), 1);
					x = for_every_sample8(x);
					memcpy(&buf[bufloc], &x, 1);
					bufloc += 1;
				}
				if (data_size == 2) {
					int16_t x;
					memcpy(&x, frame->data[0] + data_size*(i*chcount+ch), 2);
					x = for_every_sample16(x);
					memcpy(&buf[bufloc], &x, 2);
					bufloc += 2;
				}
				if (data_size == 4 && audio_format_as_sdl == AUDIO_F32SYS) {
					float x;
					memcpy(&x, frame->data[0] + data_size*(i*chcount+ch), 4);
					x = for_every_sample(x);
					memcpy(&buf[bufloc], &x, 4);
					bufloc += 4;
				}
				else if (data_size == 4) {
					int32_t x;
					memcpy(&x, frame->data[0] + data_size*(i*chcount+ch), 4);
					x = for_every_sample32(x);
					memcpy(&buf[bufloc], &x, 4);
					bufloc += 4;
				}
				if (data_size == 8 && audio_format_as_sdl == AUDIO_F32SYS) {
					double x;
					float xf;
					memcpy(&x, frame->data[0] + data_size*(i*chcount+ch), 8);
					xf = (float)x;
					xf = for_every_sample(xf);
					memcpy(&buf[bufloc], &xf, 4);
					bufloc += 4;
				}
				else if (data_size == 8) {
					int64_t x;
					int32_t x32;
					memcpy(&x, frame->data[0] + data_size*(i*chcount+ch), 8);
					x32 = x>>32;
					x32 = for_every_sample32(x32);
					memcpy(&buf[bufloc], &x32, 4);
					bufloc += 4;
				}
			} else {
				if (data_size == 1) {
					uint8_t x;
					memcpy(&x, frame->data[ch] + data_size*i, 1);
					x = for_every_sample8(x);
					memcpy(&buf[bufloc], &x, 1);
					bufloc += 1;
				}
				if (data_size == 2) {
					int16_t x;
					memcpy(&x, frame->data[ch] + data_size*i, 2);
					x = for_every_sample16(x);
					memcpy(&buf[bufloc], &x, 2);
					bufloc += 2;
				}
				if (data_size == 4 && audio_format_as_sdl == AUDIO_F32SYS) {
					float x;
					memcpy(&x, frame->data[ch] + data_size*i, 4);
					x = for_every_sample(x);
					memcpy(&buf[bufloc], &x, 4);
					bufloc += 4;
				}
				else if (data_size == 4) {
					int32_t x;
					memcpy(&x, frame->data[ch] + data_size*i, 4);
					x = for_every_sample32(x);
					memcpy(&buf[bufloc], &x, 4);
					bufloc += 4;
				}
				if (data_size == 8 && audio_format_as_sdl == AUDIO_F32SYS) {
					double x;
					float xf;
					memcpy(&x, frame->data[ch] + data_size*i, 8);
					xf = (float)x;
					xf = for_every_sample(xf);
					memcpy(&buf[bufloc], &xf, 4);
					bufloc += 4;
				}
				else if (data_size == 8) {
					int64_t x;
					int32_t x32;
					memcpy(&x, frame->data[ch] + data_size*i, 8);
					x32 = x>>32;
					x32 = for_every_sample32(x32);
					memcpy(&buf[bufloc], &x32, 4);
					bufloc += 4;
				}
			}
		}
	}
	//printf("Bufloc %d\n", bufloc);
	int ret = SDL_AudioStreamPut(sdl_stream, buf, bufloc);
	if (ret < 0) {
		fprintf(stderr, "Cannot put data to SDL audio stream\n");
		handler_impl();
		exit(1);
	}
	//int avail = SDL_AudioStreamAvailable(sdl_stream);
	int gotten = SDL_AudioStreamGet(sdl_stream, buf, sizeof(buf));
	//printf("Gotten %d\n", gotten);
	uint32_t bytes_per_sec = (uint32_t)(obtained.freq*obtained.channels*obtained_data_size);
	uint32_t target_samples = (uint32_t)(0.15*bytes_per_sec);
	for (;;) {
		uint32_t inqueue = (uint32_t)SDL_GetQueuedAudioSize(audid);
		if (inqueue <= target_samples) {
			break;
		}
		else {
			int ch;
			while ((ch = read_char()) != '\0') {
				if (ch == '\n' || ch == 'q') {
					handler_impl();
					exit(0);
				}
				if (ch == EXT_UP) {
					seeks += 60;
					//printf("\n\nUP\n\n");
				}
				if (ch == EXT_DOWN) {
					seeks -= 60;
					//printf("\n\nDOWN\n\n");
				}
				if (ch == EXT_LEFT) {
					seeks -= 10;
					//printf("\n\nLEFT\n\n");
				}
				if (ch == EXT_RIGHT) {
					seeks += 10;
					//printf("\n\nRIGHT\n\n");
				}
				if (ch == EXT_PGUP) {
					seeks += 600;
					//printf("\n\nPGUP\n\n");
				}
				if (ch == EXT_PGDOWN) {
					seeks -= 600;
					//printf("\n\nPGDOWN\n\n");
				}
				if (ch == ' ') {
					for (;;) {
						struct pollfd pfd = {};
						pfd.fd = 0;
						pfd.events = POLLIN;
						pfd.revents = 0;
						if (poll(&pfd, 1, -1) < 1) {
							handler_impl();
							exit(1);
						}
						ch = read_char();
						if (ch == '\n' || ch == 'q') {
							handler_impl();
							exit(0);
						}
						if (ch == ' ') {
							break;
						}
					}
				}
			}
			inqueue = (uint32_t)SDL_GetQueuedAudioSize(audid);
			if (inqueue > target_samples) {
				usleep((useconds_t)((inqueue - target_samples)*1e6/bytes_per_sec));
			}
		}
	}
#if 0
	while ((uint32_t)SDL_GetQueuedAudioSize(audid) > (uint32_t)(0.15*obtained.freq*obtained.channels*obtained_data_size)) {
		usleep(10000);
	}
#endif
	ret = SDL_QueueAudio(audid, buf, gotten);
	if (ret != 0) {
		fprintf(stderr, "Cannot queue data to SDL audio output\n");
		handler_impl();
		exit(1);
	}
}

void usage(const char *argv0)
{
	fprintf(stderr, "Usage: %s [-g gain_db] file.ogg\n", argv0);
	exit(1);
}

int main(int argc, char **argv)
{
	int aidx;
	int ret;
	const AVCodec *dec;
	AVFormatContext *avfctx = NULL;
	AVFrame *frame = NULL;
	AVPacket *packet = NULL;
	int opt;
	float gain_db;
	char *endptr;
	size_t fnamebuflen;
	char *fnamebuf;
#if 0
	AVStream *audio_stream;
#endif

	while ((opt = getopt(argc, argv, "g:")) != -1) {
		switch (opt) {
			case 'g':
				if (!optarg)
				{
					usage(argv[0]);
				}
				gain_db = strtof(optarg, &endptr);
				if (*optarg == '\0' || *endptr != '\0') {
					usage(argv[0]);
				}
				volume_mul = powf(10.0, gain_db/20.0);
				break;
			default: // '?'
				usage(argv[0]);
				break;
		}
	}
	if (argc != optind + 1) {
		usage(argv[0]);
	}
	if (access(argv[optind], R_OK) != 0) {
		fprintf(stderr, "Cannot access file: %s\n", argv[optind]);
		handler_impl();
		exit(1);
	}
	fnamebuflen = strlen(argv[optind]) + 6;
	fnamebuf = malloc(fnamebuflen);
	if (fnamebuf == NULL) {
		fprintf(stderr, "Cannot allocate file name buffer, probably out of memory\n");
		handler_impl();
		exit(1);
	}
	snprintf(fnamebuf, fnamebuflen, "file:%s", argv[optind]);

	SDL_Init(SDL_INIT_AUDIO);

#if 0
	//const char *vorbis = "file:/home/juhis/vboxshared/Music/Ellips/Yhden naisen hautajaiset/01-Maailma on rikki.ogg";
	const char *vorbis = "file:/home/juhis/vboxshared/Music/Haloo Helsinki!/Haloo Box! 4 (Maailma On Tehty Meitä Varten)/01-Avautumisraita.ogg";
	const char *flac = "file:/home/juhis/vboxshared/Music/Haloo Helsinki!/Haloo Box! 4 (Maailma On Tehty Meitä Varten)/test/01-Avautumisraita.flac";
	const char *oggflac = "file:/home/juhis/vboxshared/Music/Haloo Helsinki!/Haloo Box! 4 (Maailma On Tehty Meitä Varten)/test/01-Avautumisraita.oga";
	const char *aac = "file:/home/juhis/vboxshared/Music/Haloo Helsinki!/Haloo Box! 4 (Maailma On Tehty Meitä Varten)/test/01-Avautumisraita.aac";
	const char *mp3 = "file:/home/juhis/vboxshared/Music/Haloo Helsinki!/Haloo Box! 4 (Maailma On Tehty Meitä Varten)/test/01-Avautumisraita.mp3";
#endif

	if (avformat_open_input(&avfctx, fnamebuf, NULL, NULL) < 0) {
		fprintf(stderr, "File %s is probably not an audio file, can't open it\n", argv[optind]);
		handler_impl();
		exit(1);
	}
	if (avformat_find_stream_info(avfctx, NULL) < 0) {
		fprintf(stderr, "File %s is probably not an audio file, can't find stream info\n", argv[optind]);
		handler_impl();
		exit(1);
	}
	duration = avfctx->duration/(float)AV_TIME_BASE;
	ret = av_find_best_stream(avfctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	if (ret < 0) {
		fprintf(stderr, "File %s is probably not an audio file, can't find best stream\n", argv[optind]);
		handler_impl();
		exit(1);
	}
	aidx = ret;
	dec = avcodec_find_decoder(avfctx->streams[aidx]->codecpar->codec_id);
	if (!dec) {
		fprintf(stderr, "File %s is probably not an audio file, can't find decoder\n", argv[optind]);
		handler_impl();
		exit(1);
	}
	adecctx = avcodec_alloc_context3(dec);
	if (!adecctx) {
		fprintf(stderr, "Cannot allocate decoder context, probably out of memory\n");
		handler_impl();
		exit(1);
	}
	if ((ret = avcodec_parameters_to_context(adecctx, avfctx->streams[aidx]->codecpar)) < 0) {
		fprintf(stderr, "Cannot move parameters to decoder context, probably out of memory\n");
		handler_impl();
		exit(1);
	}
	if (avcodec_open2(adecctx, dec, NULL) < 0) {
		fprintf(stderr, "Cannot open audio codec, probably out of memory\n");
		handler_impl();
		exit(1);
	}
	if (adecctx->channel_layout == AV_CH_LAYOUT_MONO) {
		chcount = 1;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_STEREO) {
		chcount = 2;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_2POINT1) {
		chcount = 3;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_2_1) {
		chcount = 3;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_SURROUND) {
		chcount = 3;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_3POINT1) {
		chcount = 4;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_4POINT0) {
		chcount = 4;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_4POINT1) {
		chcount = 5;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_2_2) {
		chcount = 4;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_QUAD) {
		chcount = 4;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_5POINT0) {
		chcount = 5;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_5POINT1) {
		chcount = 6;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_5POINT0_BACK) {
		chcount = 5;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_5POINT1_BACK) {
		chcount = 6;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_6POINT0) {
		chcount = 6;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_6POINT0_FRONT) {
		chcount = 6;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_HEXAGONAL) {
		chcount = 6;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_6POINT1) {
		chcount = 7;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_6POINT1_BACK) {
		chcount = 7;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_6POINT1_FRONT) {
		chcount = 7;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_7POINT0) {
		chcount = 7;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_7POINT0_FRONT) {
		chcount = 7;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_7POINT1) {
		chcount = 8;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_7POINT1_WIDE) {
		chcount = 8;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_7POINT1_WIDE_BACK) {
		chcount = 8;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_OCTAGONAL) {
		chcount = 8;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_STEREO_DOWNMIX) {
		chcount = 2;
	} else {
		fprintf(stderr, "Unsupported channel conf %lld\n", (long long)adecctx->channel_layout);
		handler_impl();
		exit(1);
	}
	data_size = av_get_bytes_per_sample(adecctx->sample_fmt);
	if (data_size < 0 || (data_size != 1 && data_size != 2 && data_size != 4 && data_size != 8)) {
		fprintf(stderr, "Fatal error: shouldn't occur\n");
		handler_impl();
		exit(1);
	}


	/* For planar sample formats, each audio channel is in a separate data
	 * plane, and linesize is the buffer size, in bytes, for a single
	 * plane. All data planes must be the same size. For packed sample
	 * formats, only the first data plane is used, and samples for each
	 * channel are interleaved. In this case, linesize is the buffer size,
	 * in bytes, for the 1 plane.
	 */
	if (data_size == 8) {
		if (adecctx->sample_fmt == AV_SAMPLE_FMT_DBL) {
			//printf("nonplanar\n");
			nonplanar = 1;
			audio_format_as_sdl = AUDIO_F32SYS;
		}
		else if (adecctx->sample_fmt == AV_SAMPLE_FMT_DBLP) {
			audio_format_as_sdl = AUDIO_F32SYS;
		}
		else if (adecctx->sample_fmt == AV_SAMPLE_FMT_S64) {
			//printf("nonplanar\n");
			nonplanar = 1;
			audio_format_as_sdl = AUDIO_S32SYS;
		}
		else if (adecctx->sample_fmt == AV_SAMPLE_FMT_S64P) {
			audio_format_as_sdl = AUDIO_S32SYS;
		}
	}
	else if (data_size == 4) {
		if (adecctx->sample_fmt == AV_SAMPLE_FMT_FLT) {
			//printf("nonplanar\n");
			nonplanar = 1;
			audio_format_as_sdl = AUDIO_F32SYS;
		}
		else if (adecctx->sample_fmt == AV_SAMPLE_FMT_FLTP) {
			audio_format_as_sdl = AUDIO_F32SYS;
		}
		else if (adecctx->sample_fmt == AV_SAMPLE_FMT_S32) {
			//printf("nonplanar\n");
			nonplanar = 1;
			audio_format_as_sdl = AUDIO_S32SYS;
		}
		else if (adecctx->sample_fmt == AV_SAMPLE_FMT_S32P) {
			audio_format_as_sdl = AUDIO_S32SYS;
		}
	}
	else if (data_size == 2) {
		if (adecctx->sample_fmt == AV_SAMPLE_FMT_S16) {
			//printf("nonplanar\n");
			nonplanar = 1;
			audio_format_as_sdl = AUDIO_S16SYS;
		}
		else if (adecctx->sample_fmt == AV_SAMPLE_FMT_S16P) {
			audio_format_as_sdl = AUDIO_S16SYS;
		}
	}
	else if (data_size == 1) {
		if (adecctx->sample_fmt == AV_SAMPLE_FMT_U8) {
			//printf("nonplanar\n");
			audio_format_as_sdl = AUDIO_U8;
		}
		else if (adecctx->sample_fmt == AV_SAMPLE_FMT_U8P) {
			audio_format_as_sdl = AUDIO_U8;
		}
	}
	if (audio_format_as_sdl == -12345) {
		fprintf(stderr, "Unknown audio format given by decoder\n");
		handler_impl();
		exit(1);
	}
	//printf("Rate %d\n", adecctx->sample_rate);
	SDL_AudioSpec desired;
	desired.freq = adecctx->sample_rate;
	desired.format = AUDIO_F32;
	desired.channels = 2;
	desired.samples = 4096;
	desired.callback = NULL;
	desired.userdata = NULL;
	//const char *name = SDL_GetAudioDeviceName(1,0);
	const char *name = SDL_GetAudioDeviceName(0,0);
	//name = NULL;
	audid = SDL_OpenAudioDevice(name, 0, &desired, &obtained, SDL_AUDIO_ALLOW_ANY_CHANGE);
	if (audid <= 0) {
		fprintf(stderr, "Cannot open SDL audio\n");
		handler_impl();
		exit(1);
	}
	sdl_stream = SDL_NewAudioStream(audio_format_as_sdl, chcount > 2 ? 2 : chcount, adecctx->sample_rate, obtained.format, obtained.channels, obtained.freq);
	if (obtained.format == AUDIO_S32 || obtained.format == AUDIO_S32LSB || obtained.format == AUDIO_S32MSB || obtained.format == AUDIO_S32SYS || obtained.format == AUDIO_F32 || obtained.format == AUDIO_F32LSB || obtained.format == AUDIO_F32MSB || obtained.format == AUDIO_F32SYS) {
		obtained_data_size = 4;
	}
	else if (obtained.format == AUDIO_S16 || obtained.format == AUDIO_S16LSB || obtained.format == AUDIO_S16MSB || obtained.format == AUDIO_S16SYS || obtained.format == AUDIO_U16 || obtained.format == AUDIO_U16LSB || obtained.format == AUDIO_U16MSB || obtained.format == AUDIO_U16SYS) {
		obtained_data_size = 2;
	}
	else if (obtained.format == AUDIO_U8 || obtained.format == AUDIO_S8) {
		obtained_data_size = 1;
	}
	else {
		fprintf(stderr, "Unknown format obtained for SDL audio devicen");
		handler_impl();
		exit(1);
	}
#if 0
	audio_stream = avfctx->streams[aidx];
#endif
	frame = av_frame_alloc();
	if (!frame) {
		fprintf(stderr, "Cannot allocate frame, probably out of memory\n");
		handler_impl();
		exit(1);
	}
	packet = av_packet_alloc();
	if (!packet) {
		fprintf(stderr, "Cannot allocate packet, probably out of memory\n");
		handler_impl();
		exit(1);
	}
	SDL_PauseAudioDevice(audid, 0);
	struct termios term;
	tcgetattr(fileno(stdin), &term);
	term.c_lflag &= ~ECHO;
	term.c_lflag &= ~ICANON;
	tcsetattr(fileno(stdin), 0, &term);
	signal(SIGINT, handler);
	signal(SIGTERM, handler);
	signal(SIGUSR1, handler);
	signal(SIGUSR2, handler);
	signal(SIGPROF, handler);
	signal(SIGALRM, handler);
	signal(SIGVTALRM, handler);
	// SIGHUP: hangup on terminal, no need to reset the terminal
	// SIGPIPE: can't write to terminal output, probably no need to reset the terminal
	while (av_read_frame(avfctx, packet) >= 0) {
		if (packet->stream_index == aidx) {
			ret = avcodec_send_packet(adecctx, packet);
			if (ret < 0) {
				fprintf(stderr, "Cannot send packet to AV codec, probably corrupted file\n");
				handler_impl();
				exit(1);
			}
			while (ret >= 0) {
				ret = avcodec_receive_frame(adecctx, frame);
				if (ret < 0) {
					if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
						ret = 0;
						break;
					}
					fprintf(stderr, "Cannot receive frame from AV codec, probably corrupted file\n");
					handler_impl();
					exit(1);
				}
				output_audio_frame(frame);
				av_frame_unref(frame);
			}
		}
		av_packet_unref(packet);
		if (seeks != 0) {
			int64_t ts = pts + seeks*adecctx->time_base.den/adecctx->time_base.num;
			if (ts < 0) {
				ts = 0;
			}
			avformat_seek_file(avfctx, aidx, INT64_MIN, ts, INT64_MAX, 0);
			SDL_ClearQueuedAudio(audid);
			seeks = 0;
		}
		if (ret < 0) {
			break;
		}
	}
	if (adecctx) {
		ret = avcodec_send_packet(adecctx, NULL);
	}
	handler_impl();
	return 0;
}
