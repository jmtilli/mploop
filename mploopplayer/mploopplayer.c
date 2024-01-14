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
#include <libavutil/dict.h>
#include <libavutil/replaygain.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>

int leftidx = -1;
int rightidx = -1;
SDL_AudioDeviceID audid;
SDL_AudioSpec obtained;
AVCodecContext *adecctx;
#if SDL_MAJOR_VERSION > 2 || (SDL_MAJOR_VERSION == 2 && SDL_MINOR_VERSION > 0) || (SDL_MAJOR_VERSION == 2 && SDL_MINOR_VERSION == 0 && SDL_PATCHLEVEL >= 7)
SDL_AudioStream *sdl_stream;
#else
SDL_AudioCVT cvt;
#endif
int audio_frame_count;
int chcount;
int data_size;
int obtained_data_size;
char buf[1048576];
int bufloc;
int nonplanar = 0;
float duration;
int audio_format_as_sdl = -12345;
char volfilebuf[PATH_MAX+1];
const char *volfile = NULL;
AVFormatContext *avfctx = NULL;
int aidx;
int usegain = 0;

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

struct escape {
	int64_t escape_time64;
	char escapebuf[16];
	int escapebufsize;
};

struct escape *escapes;
struct pollfd *pfds;
size_t pfds_size;
size_t pfds_capacity;

void pfds_add(int fd) {
	struct escape *escapes_new;
	struct pollfd *pfds_new;
	if (pfds_size >= pfds_capacity) {
		pfds_capacity = 2*pfds_size + 16;
		pfds_new = realloc(pfds, pfds_capacity*sizeof(*pfds));
		escapes_new = realloc(escapes, pfds_capacity*sizeof(*escapes));
		if (pfds_new == NULL || escapes_new == NULL) {
			fprintf(stderr, "Out of memory\n");
			handler_impl();
			exit(1);
		}
		pfds = pfds_new;
		escapes = escapes_new;
	}
	pfds[pfds_size].fd = fd;
	pfds[pfds_size].events = POLLIN;
	pfds[pfds_size].revents = 0;
	escapes[pfds_size].escape_time64 = 0;
	escapes[pfds_size].escapebufsize = 0;
	pfds_size++;
}

/*
 * TODO use termcap to use correct escape sequences of the terminal
 */
int read_char(void)
{
	char ch = '\0';
	int old_flags;
	int i;
	if (poll(pfds, pfds_size, 0) < 1) {
		return '\0';
	}
	if (pfds[0].events & POLLIN) {
		old_flags = fcntl(pfds[0].fd, F_GETFL);
		if (old_flags < 0) {
			return '\0';
		}
		fcntl(pfds[0].fd, F_SETFL, old_flags | O_NONBLOCK);
		for (;;) {
			int newfd = accept(pfds[0].fd, NULL, NULL);
			if (newfd > 0) {
				pfds_add(newfd);
			}
			else if (newfd < 0) {
				break;
			}
		}
		fcntl(pfds[0].fd, F_SETFL, old_flags);
	}
	for (i = 1; i < (int)pfds_size; i++) {
		if (pfds[i].revents & POLLIN) {
			int fd = pfds[i].fd;
			int read_ret;
			struct escape *esc = &escapes[i];
			old_flags = fcntl(fd, F_GETFL);
			if (old_flags < 0) {
				return '\0';
			}
			fcntl(fd, F_SETFL, old_flags | O_NONBLOCK);
			for (;;) {
				read_ret = read(fd, &ch, 1);
				if (read_ret < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
					ch = '\0';
					fcntl(fd, F_SETFL, old_flags);
					break;
				}
				if (read_ret < 0) {
					// FIXME handle
					ch = '\0';
					fcntl(fd, F_SETFL, old_flags);
					break;
				}
				if (read_ret == 0 && fd != 0) {
					pfds[i] = pfds[pfds_size - 1]; // struct assignment
					escapes[i] = escapes[pfds_size - 1]; // struct assign
					close(fd);
					pfds_size--;
					i--;
					break;
				}
				if (ch == '\x1b') {
					esc->escape_time64 = gettime64();
					esc->escapebuf[0] = ch;
					esc->escapebufsize = 1;
					ch = '\0';
				}
				if (gettime64() - esc->escape_time64 >= 1000000 && esc->escapebufsize > 0) {
					esc->escapebufsize = 0;
				}
				if ((gettime64() - esc->escape_time64 < 1000000) && esc->escapebufsize == 1 && ch == '[') {
					esc->escapebuf[1] = ch;
					esc->escapebufsize = 2;
					ch = '\0';
				}
				if ((gettime64() - esc->escape_time64 < 1000000) && esc->escapebufsize == 1 && ch == 'O') {
					esc->escapebufsize = 0;
					ch = '\0';
				}
				if ((gettime64() - esc->escape_time64 < 1000000) && esc->escapebufsize == 2 && (ch == 'H' || ch == 'F' || ch == 'E')) {
					esc->escapebufsize = 0;
					ch = '\0';
				}
				if ((gettime64() - esc->escape_time64 < 1000000) && esc->escapebufsize == 2 && (ch == 'A' || ch == 'B' || ch == 'C' || ch == 'D')) {
					if (ch == 'A') {
						esc->escapebufsize = 0;
						fcntl(fd, F_SETFL, old_flags);
						return EXT_UP;
					}
					if (ch == 'B') {
						esc->escapebufsize = 0;
						fcntl(fd, F_SETFL, old_flags);
						return EXT_DOWN;
					}
					if (ch == 'C') {
						esc->escapebufsize = 0;
						fcntl(fd, F_SETFL, old_flags);
						return EXT_RIGHT;
					}
					if (ch == 'D') {
						esc->escapebufsize = 0;
						fcntl(fd, F_SETFL, old_flags);
						return EXT_LEFT;
					}
					ch = '\0';
				}
				if ((gettime64() - esc->escape_time64 < 1000000) && esc->escapebufsize == 2 && (ch == '5' || ch == '6')) {
					esc->escapebuf[2] = ch;
					esc->escapebufsize = 3;
					ch = '\0';
				}
				if ((gettime64() - esc->escape_time64 < 1000000) && esc->escapebufsize >= 2 && (ch == '~')) {
					if (esc->escapebufsize == 3 && esc->escapebuf[0] == '\x1b' && esc->escapebuf[1] == '[' && esc->escapebuf[2] == '5') {
						esc->escapebufsize = 0;
						fcntl(fd, F_SETFL, old_flags);
						return EXT_PGUP;
					}
					if (esc->escapebufsize == 3 && esc->escapebuf[0] == '\x1b' && esc->escapebuf[1] == '[' && esc->escapebuf[2] == '6') {
						esc->escapebufsize = 0;
						fcntl(fd, F_SETFL, old_flags);
						return EXT_PGDOWN;
					}
					ch = '\0';
					esc->escapebufsize = 0;
				}
				if (ch != '\0') {
					fcntl(0, F_SETFL, old_flags);
					return ch;
				}
			}
			fcntl(fd, F_SETFL, old_flags);
		}
	}
	return '\0';
}

char *old_status = NULL;

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
	if (old_status && strcmp(buf, old_status) == 0)
	{
		free(buf);
		return;
	}
	printf("%s", buf);
	for (i = 0; i < max_len - len; i++)
	{
		putchar(' ');
	}
	putchar('\r');
	free(old_status);
	old_status = buf;
}

int seeks = 0;
int64_t pts = 0;

float volume_db = 0.0;
float gain_db = 0.0;
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

void handle_chars(int ch)
{
	if (ch == '?') {
		seeks = INT_MIN/2;
	}
	if (ch == '%') {
		seeks -= 1;
	}
	if (ch == '&') {
		seeks += 1;
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
	if (ch == '/' || ch == '9') {
		FILE *f;
		volume_db -= 1;
		volume_mul = powf(10.0, (gain_db+volume_db)/20.0);
		f = fopen(volfile, "w");
		if (f) {
			fprintf(f, "%f\n", volume_db);
			fclose(f);
		}
	}
	if (ch == '*' || ch == '0') {
		FILE *f;
		volume_db += 1;
		volume_mul = powf(10.0, (gain_db+volume_db)/20.0);
		f = fopen(volfile, "w");
		if (f) {
			fprintf(f, "%f\n", volume_db);
			fclose(f);
		}
	}
}

void output_audio_frame(AVFrame *frame)
{
	int i;
	int chid;
	int chmap[2] = {leftidx, rightidx};
#if 0
	size_t unpadded_linesize = frame->nb_samples * av_get_bytes_per_sample(frame->format);
#endif
#if LIBAVCODEC_VERSION_MAJOR > 57 || (LIBAVCODEC_VERSION_MAJOR == 57 && LIBAVCODEC_VERSION_MINOR >= 61)
	pts = frame->pts;
	float mytime = ((float)frame->pts) * ((float)avfctx->streams[aidx]->time_base.num) / (float)avfctx->streams[aidx]->time_base.den;
#else
	pts = frame->pkt_pts;
	float mytime = ((float)frame->pkt_pts) * ((float)avfctx->streams[aidx]->time_base.num) / (float)avfctx->streams[aidx]->time_base.den;
#endif
	print_status("[V: %.1f] A: %.1f / %.1f", volume_db, mytime, duration);
	fflush(stdout);
	bufloc = 0;
	for (i = 0; i < frame->nb_samples; i++) {
		for (chid = 0; chid < (chcount>2 ? 2 : chcount); chid++) {
			int ch = chmap[chid];
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
#if SDL_MAJOR_VERSION > 2 || (SDL_MAJOR_VERSION == 2 && SDL_MINOR_VERSION > 0) || (SDL_MAJOR_VERSION == 2 && SDL_MINOR_VERSION == 0 && SDL_PATCHLEVEL >= 7)
	int ret = SDL_AudioStreamPut(sdl_stream, buf, bufloc);
	if (ret < 0) {
		fprintf(stderr, "Cannot put data to SDL audio stream\n");
		handler_impl();
		exit(1);
	}
	//int avail = SDL_AudioStreamAvailable(sdl_stream);
	int gotten = SDL_AudioStreamGet(sdl_stream, buf, sizeof(buf));
#else
	int ret;
	cvt.len = bufloc;
	cvt.buf = (uint8_t*)buf;
	SDL_ConvertAudio(&cvt);
	int gotten = cvt.len_cvt;
#endif
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
				handle_chars(ch);
				if (ch == '\n' || ch == 'q') {
					handler_impl();
					exit(0);
				}
				if (ch == ' ' || ch == 'p') {
					for (;;) {
						if (poll(pfds, pfds_size, -1) < 1) {
							handler_impl();
							exit(1);
						}
						ch = read_char();
						handle_chars(ch);
						if (ch == '\n' || ch == 'q') {
							handler_impl();
							exit(0);
						}
						if (ch == ' ' || ch == 'p') {
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

int print_prefix = 1;

struct AVClassContainer {
	struct AVClass *class;
};

enum avoid_state {
	AVOID_NONE,
	AVOID_OGG_MSG,
	AVOID_OPUS_MSG,
};

enum avoid_state avoid_state = AVOID_NONE;

void log_cb(void *avcl, int level, const char *fmt, va_list ap)
{
	char *rawlinebuf = NULL;
	int rawlinesize;
	char *linebuf = NULL;
	int linesize;
	struct AVClassContainer *container = (struct AVClassContainer*)avcl;
	va_list ap2;
	va_list ap3;
	va_list ap4;
	if (level > av_log_get_level())
	{
		return;
	}
	va_copy(ap2, ap);
	va_copy(ap3, ap);
	va_copy(ap4, ap);
	rawlinesize = vsnprintf(NULL, 0, fmt, ap);
#if LIBAVUTIL_VERSION_MAJOR > 55 || (LIBAVUTIL_VERSION_MAJOR == 55 && LIBAVUTIL_VERSION_MINOR >= 23)
	linesize = av_log_format_line2(avcl, level, fmt, ap2, NULL, 0, &print_prefix);
#else
	linesize = rawlinesize+2048;
#endif
	va_end(ap2);
	if (linesize < 0) {
		fprintf(stderr, "Error when logging\n");
		handler_impl();
		exit(1);
	}
	linebuf = malloc(linesize+1);
	if (linebuf == NULL)
	{
		fprintf(stderr, "Out of memory\n");
		handler_impl();
		exit(1);
	}
	rawlinebuf = malloc(rawlinesize+1);
	if (rawlinebuf == NULL)
	{
		fprintf(stderr, "Out of memory\n");
		handler_impl();
		exit(1);
	}
#if LIBAVUTIL_VERSION_MAJOR > 55 || (LIBAVUTIL_VERSION_MAJOR == 55 && LIBAVUTIL_VERSION_MINOR >= 23)
	if (av_log_format_line2(avcl, level, fmt, ap3, linebuf, linesize+1, &print_prefix) < 0) {
		fprintf(stderr, "Error when logging\n");
		handler_impl();
		exit(1);
	}
#else
	av_log_format_line(avcl, level, fmt, ap3, linebuf, linesize+1, &print_prefix);
#endif
	va_end(ap3);
	if (vsnprintf(rawlinebuf, rawlinesize+1, fmt, ap4) < 0) {
		fprintf(stderr, "Error when logging\n");
		handler_impl();
		exit(1);
	}
	va_end(ap4);
	if (container) {
		struct AVClass *class = container->class;
		if (class && class->item_name) {
			const char *item_name = class->item_name(container);
			const char *ogg_suffix = " bytes of comment header remain\n";
			if (strcmp(item_name, "ogg") == 0 && avoid_state == AVOID_OGG_MSG && level == AV_LOG_INFO)
			{
				if (strlen(rawlinebuf) > strlen(ogg_suffix) && strcmp(rawlinebuf+strlen(rawlinebuf)-strlen(ogg_suffix), ogg_suffix) == 0) {
					free(linebuf);
					free(rawlinebuf);
					return;
				}
			}
			if ((strcmp(item_name, "opus") == 0 || strcmp(item_name, "vorbis") == 0 || strcmp(item_name, "mp3float") == 0) && avoid_state == AVOID_OPUS_MSG && level == AV_LOG_WARNING)
			{
				if (strcmp(rawlinebuf, "Could not update timestamps for skipped samples.\n") == 0) {
					free(linebuf);
					free(rawlinebuf);
					return;
				}
				if (strcmp(rawlinebuf, "Could not update timestamps for discarded samples.\n") == 0) {
					free(linebuf);
					free(rawlinebuf);
					return;
				}
			}
		}
	}
	fprintf(stderr, "%s", linebuf);
	free(linebuf);
	free(rawlinebuf);
}

void usage(const char *argv0)
{
	fprintf(stderr, "Usage: %s [-g gain_db] file.ogg\n", argv0);
	exit(1);
}

const char *langsuffixes[] = {
	"aar", "abk", "ace", "ach", "ada", "ady", "afa", "afh", "afr", "ain",
	"aka", "akk", "alb", "ale", "alg", "alt", "amh", "ang", "anp", "apa",
	"ara", "arc", "arg", "arm", "arn", "arp", "art", "arw", "asm", "ast",
	"ath", "aus", "ava", "ave", "awa", "aym", "aze", "bad", "bai", "bak",
	"bal", "bam", "ban", "baq", "bas", "bat", "bej", "bel", "bem", "ben",
	"ber", "bho", "bih", "bik", "bin", "bis", "bla", "bnt", "bod", "bos",
	"bra", "bre", "btk", "bua", "bug", "bul", "bur", "byn", "cad", "cai",
	"car", "cat", "cau", "ceb", "cel", "ces", "cha", "chb", "che", "chg",
	"chi", "chk", "chm", "chn", "cho", "chp", "chr", "chu", "chv", "chy",
	"cmc", "cnr", "cop", "cor", "cos", "cpe", "cpf", "cpp", "cre", "crh",
	"crp", "csb", "cus", "cym", "cze", "dak", "dan", "dar", "day", "del",
	"den", "deu", "dgr", "din", "div", "doi", "dra", "dsb", "dua", "dum",
	"dut", "dyu", "dzo", "efi", "egy", "eka", "ell", "elx", "eng", "enm",
	"epo", "est", "eus", "ewe", "ewo", "fan", "fao", "fas", "fat", "fij",
	"fil", "fin", "fiu", "fon", "fra", "fre", "frm", "fro", "frr", "frs",
	"fry", "ful", "fur", "gaa", "gay", "gba", "gem", "geo", "ger", "gez",
	"gil", "gla", "gle", "glg", "glv", "gmh", "goh", "gon", "gor", "got",
	"grb", "grc", "gre", "grn", "gsw", "guj", "gwi", "hai", "hat", "hau",
	"haw", "heb", "her", "hil", "him", "hin", "hit", "hmn", "hmo", "hrv",
	"hsb", "hun", "hup", "hye", "iba", "ibo", "ice", "ido", "iii", "ijo",
	"iku", "ile", "ilo", "ina", "inc", "ind", "ine", "inh", "ipk", "ira",
	"iro", "isl", "ita", "jav", "jbo", "jpn", "jpr", "jrb", "kaa", "kab",
	"kac", "kal", "kam", "kan", "kar", "kas", "kat", "kau", "kaw", "kaz",
	"kbd", "kha", "khi", "khm", "kho", "kik", "kin", "kir", "kmb", "kok",
	"kom", "kon", "kor", "kos", "kpe", "krc", "krl", "kro", "kru", "kua",
	"kum", "kur", "kut", "lad", "lah", "lam", "lao", "lat", "lav", "lez",
	"lim", "lin", "lit", "lol", "loz", "ltz", "lua", "lub", "lug", "lui",
	"lun", "luo", "lus", "mac", "mad", "mag", "mah", "mai", "mak", "mal",
	"man", "mao", "map", "mar", "mas", "may", "mdf", "mdr", "men", "mga",
	"mic", "min", "mis", "mkd", "mkh", "mlg", "mlt", "mnc", "mni", "mno",
	"moh", "mon", "mos", "mri", "msa", "mul", "mun", "mus", "mwl", "mwr",
	"mya", "myn", "myv", "nah", "nai", "nap", "nau", "nav", "nbl", "nde",
	"ndo", "nds", "nep", "new", "nia", "nic", "niu", "nld", "nno", "nob",
	"nog", "non", "nor", "nqo", "nso", "nub", "nwc", "nya", "nym", "nyn",
	"nyo", "nzi", "oci", "oji", "ori", "orm", "osa", "oss", "ota", "oto",
	"paa", "pag", "pal", "pam", "pan", "pap", "pau", "peo", "per", "phi",
	"phn", "pli", "pol", "pon", "por", "pra", "pro", "pus", "que", "raj",
	"rap", "rar", "roa", "roh", "rom", "ron", "rum", "run", "rup", "rus",
	"sad", "sag", "sah", "sai", "sal", "sam", "san", "sas", "sat", "scn",
	"sco", "sel", "sem", "sga", "sgn", "shn", "sid", "sin", "sio", "sit",
	"sla", "slk", "slo", "slv", "sma", "sme", "smi", "smj", "smn", "smo",
	"sms", "sna", "snd", "snk", "sog", "som", "son", "sot", "spa", "sqi",
	"srd", "srn", "srp", "srr", "ssa", "ssw", "suk", "sun", "sus", "sux",
	"swa", "swe", "syc", "syr", "tah", "tai", "tam", "tat", "tel", "tem",
	"ter", "tet", "tgk", "tgl", "tha", "tib", "tig", "tir", "tiv", "tkl",
	"tlh", "tli", "tmh", "tog", "ton", "tpi", "tsi", "tsn", "tso", "tuk",
	"tum", "tup", "tur", "tut", "tvl", "twi", "tyv", "udm", "uga", "uig",
	"ukr", "umb", "und", "urd", "uzb", "vai", "ven", "vie", "vol", "vot",
	"wak", "wal", "war", "was", "wel", "wen", "wln", "wol", "xal", "xho",
	"yao", "yap", "yid", "yor", "ypk", "zap", "zbl", "zen", "zgh", "zha",
	"zho", "znd", "zul", "zun", "zxx", "zza",

	// local languages:
	"qaa", "qab", "qac", "qad", "qae", "qaf", "qag", "qah", "qai", "qaj",
	"qak", "qal", "qam", "qan", "qao", "qap", "qaq", "qar", "qas", "qat",
	"qau", "qav", "qaw", "qax", "qay", "qaz", "qba", "qbb", "qbc", "qbd",
	"qbe", "qbf", "qbg", "qbh", "qbi", "qbj", "qbk", "qbl", "qbm", "qbn",
	"qbo", "qbp", "qbq", "qbr", "qbs", "qbt", "qbu", "qbv", "qbw", "qbx",
	"qby", "qbz", "qca", "qcb", "qcc", "qcd", "qce", "qcf", "qcg", "qch",
	"qci", "qcj", "qck", "qcl", "qcm", "qcn", "qco", "qcp", "qcq", "qcr",
	"qcs", "qct", "qcu", "qcv", "qcw", "qcx", "qcy", "qcz", "qda", "qdb",
	"qdc", "qdd", "qde", "qdf", "qdg", "qdh", "qdi", "qdj", "qdk", "qdl",
	"qdm", "qdn", "qdo", "qdp", "qdq", "qdr", "qds", "qdt", "qdu", "qdv",
	"qdw", "qdx", "qdy", "qdz", "qea", "qeb", "qec", "qed", "qee", "qef",
	"qeg", "qeh", "qei", "qej", "qek", "qel", "qem", "qen", "qeo", "qep",
	"qeq", "qer", "qes", "qet", "qeu", "qev", "qew", "qex", "qey", "qez",
	"qfa", "qfb", "qfc", "qfd", "qfe", "qff", "qfg", "qfh", "qfi", "qfj",
	"qfk", "qfl", "qfm", "qfn", "qfo", "qfp", "qfq", "qfr", "qfs", "qft",
	"qfu", "qfv", "qfw", "qfx", "qfy", "qfz", "qga", "qgb", "qgc", "qgd",
	"qge", "qgf", "qgg", "qgh", "qgi", "qgj", "qgk", "qgl", "qgm", "qgn",
	"qgo", "qgp", "qgq", "qgr", "qgs", "qgt", "qgu", "qgv", "qgw", "qgx",
	"qgy", "qgz", "qha", "qhb", "qhc", "qhd", "qhe", "qhf", "qhg", "qhh",
	"qhi", "qhj", "qhk", "qhl", "qhm", "qhn", "qho", "qhp", "qhq", "qhr",
	"qhs", "qht", "qhu", "qhv", "qhw", "qhx", "qhy", "qhz", "qia", "qib",
	"qic", "qid", "qie", "qif", "qig", "qih", "qii", "qij", "qik", "qil",
	"qim", "qin", "qio", "qip", "qiq", "qir", "qis", "qit", "qiu", "qiv",
	"qiw", "qix", "qiy", "qiz", "qja", "qjb", "qjc", "qjd", "qje", "qjf",
	"qjg", "qjh", "qji", "qjj", "qjk", "qjl", "qjm", "qjn", "qjo", "qjp",
	"qjq", "qjr", "qjs", "qjt", "qju", "qjv", "qjw", "qjx", "qjy", "qjz",
	"qka", "qkb", "qkc", "qkd", "qke", "qkf", "qkg", "qkh", "qki", "qkj",
	"qkk", "qkl", "qkm", "qkn", "qko", "qkp", "qkq", "qkr", "qks", "qkt",
	"qku", "qkv", "qkw", "qkx", "qky", "qkz", "qla", "qlb", "qlc", "qld",
	"qle", "qlf", "qlg", "qlh", "qli", "qlj", "qlk", "qll", "qlm", "qln",
	"qlo", "qlp", "qlq", "qlr", "qls", "qlt", "qlu", "qlv", "qlw", "qlx",
	"qly", "qlz", "qma", "qmb", "qmc", "qmd", "qme", "qmf", "qmg", "qmh",
	"qmi", "qmj", "qmk", "qml", "qmm", "qmn", "qmo", "qmp", "qmq", "qmr",
	"qms", "qmt", "qmu", "qmv", "qmw", "qmx", "qmy", "qmz", "qna", "qnb",
	"qnc", "qnd", "qne", "qnf", "qng", "qnh", "qni", "qnj", "qnk", "qnl",
	"qnm", "qnn", "qno", "qnp", "qnq", "qnr", "qns", "qnt", "qnu", "qnv",
	"qnw", "qnx", "qny", "qnz", "qoa", "qob", "qoc", "qod", "qoe", "qof",
	"qog", "qoh", "qoi", "qoj", "qok", "qol", "qom", "qon", "qoo", "qop",
	"qoq", "qor", "qos", "qot", "qou", "qov", "qow", "qox", "qoy", "qoz",
	"qpa", "qpb", "qpc", "qpd", "qpe", "qpf", "qpg", "qph", "qpi", "qpj",
	"qpk", "qpl", "qpm", "qpn", "qpo", "qpp", "qpq", "qpr", "qps", "qpt",
	"qpu", "qpv", "qpw", "qpx", "qpy", "qpz", "qqa", "qqb", "qqc", "qqd",
	"qqe", "qqf", "qqg", "qqh", "qqi", "qqj", "qqk", "qql", "qqm", "qqn",
	"qqo", "qqp", "qqq", "qqr", "qqs", "qqt", "qqu", "qqv", "qqw", "qqx",
	"qqy", "qqz", "qra", "qrb", "qrc", "qrd", "qre", "qrf", "qrg", "qrh",
	"qri", "qrj", "qrk", "qrl", "qrm", "qrn", "qro", "qrp", "qrq", "qrr",
	"qrs", "qrt", "qru", "qrv", "qrw", "qrx", "qry", "qrz", "qsa", "qsb",
	"qsc", "qsd", "qse", "qsf", "qsg", "qsh", "qsi", "qsj", "qsk", "qsl",
	"qsm", "qsn", "qso", "qsp", "qsq", "qsr", "qss", "qst", "qsu", "qsv",
	"qsw", "qsx", "qsy", "qsz", "qta", "qtb", "qtc", "qtd", "qte", "qtf",
	"qtg", "qth", "qti", "qtj", "qtk", "qtl", "qtm", "qtn", "qto", "qtp",
	"qtq", "qtr", "qts", "qtt", "qtu", "qtv", "qtw", "qtx", "qty", "qtz",
};

int skip_key(const char *key)
{
	size_t i;
	size_t sl = strlen(key);
	if (sl >= 4 && key[sl-4] == '-')
	{
		for (i = 0; i < sizeof(langsuffixes)/sizeof(*langsuffixes); i++)
		{
			if (strcmp(key+sl-3, langsuffixes[i]) == 0)
			{
				return 1;
			}
		}
	}
	if (sl >= 5)
	{
		if (strcmp(key+sl-5, "-sort") == 0)
		{
			return 1;
		}
	}
#if 0 // Covered by the previous if block
	if (sl >= 9 && key[sl-9] == '-' && key[sl-5] == '-')
	{
		char langsort[10];
		for (i = 0; i < sizeof(langsuffixes)/sizeof(*langsuffixes); i++)
		{
			if (snprintf(langsort, sizeof(langsort), "-%s-sort", langsuffixes[i]) >= (int)sizeof(langsort)) {
				abort();
			}
			if (strcmp(key+sl-9, langsort) == 0)
			{
				return 1;
			}
		}
	}
#endif
	return 0;
}

const double offset = 6.0;
int has_albumgain = 0;
int has_trackgain = 0;
double albumgain = 0;
double trackgain = 0;
double magic_ref = 89;
double ref = 89;

const char *mangle_vorbiskey(const char *key, int *colon)
{
	const char *vorbiskey = key;
	*colon = 1;
	if (strcmp(key, "TRACKNUMBER") == 0) {
		vorbiskey = "Track number";
	}
	else if (strcmp(key, "COPYRIGHT") == 0) {
		vorbiskey = "Copyright";
		*colon = 0;
	}
	else if (strcmp(key, "") == 0) {
		vorbiskey = "Comment";
	}
	return vorbiskey;
}

const char *get_vorbiskey(const char *key, const char *value)
{
	const char *vorbiskey = key;
	if (strcmp(key, "album_artist") == 0) {
		vorbiskey = "ALBUMARTIST";
	}
	else if (strcmp(key, "track") == 0) {
		vorbiskey = "TRACKNUMBER";
	}
	else if (strcmp(key, "disc") == 0) {
		vorbiskey = "DISCNUMBER";
	}
	else if (strcmp(key, "comment") == 0) {
		vorbiskey = "DESCRIPTION";
	}
	else if (strcmp(key, "album") == 0) {
		vorbiskey = "ALBUM";
	}
	else if (strcmp(key, "artist") == 0) {
		vorbiskey = "ARTIST";
	}
	else if (strcmp(key, "composer") == 0) {
		vorbiskey = "COMPOSER";
	}
	else if (strcmp(key, "copyright") == 0) {
		vorbiskey = "COPYRIGHT";
	}
	else if (strcmp(key, "creation_time") == 0) {
		vorbiskey = "DATE";
	}
	else if (strcmp(key, "date") == 0) {
		vorbiskey = "DATE";
	}
	else if (strcmp(key, "encoder") == 0) {
		vorbiskey = "ENCODER";
	}
	else if (strcmp(key, "encoded_by") == 0) {
		vorbiskey = "ENCODED-BY";
	}
	else if (strcmp(key, "filename") == 0) {
		vorbiskey = "FILENAME"; // invented by me
	}
	else if (strcmp(key, "genre") == 0) {
		vorbiskey = "GENRE";
	}
	else if (strcmp(key, "language") == 0) {
		vorbiskey = "LANGUAGE"; // invention?
	}
	else if (strcmp(key, "performer") == 0) {
		vorbiskey = "PERFORMER";
	}
	else if (strcmp(key, "publisher") == 0) {
		vorbiskey = "PUBLISHER";
	}
	else if (strcmp(key, "service_name") == 0) {
		vorbiskey = "SERVICENAME"; // invented by me
	}
	else if (strcmp(key, "service_provider") == 0) {
		vorbiskey = "SERVICEPROVIDER"; // invented by me
	}
	else if (strcmp(key, "title") == 0) {
		vorbiskey = "TITLE";
	}
	else if (strcmp(key, "variant_bitrate") == 0) {
		vorbiskey = "VARIANTBITRATE"; // invented by me
	}
	else if (strcmp(key, "replaygain_album_gain") == 0) {
		if (value)
		{
			char *endptr;
			float tmp = strtof(value, &endptr);
			if ((*endptr == '\0' ||
			    strcmp(endptr, "dB") == 0 || strcmp(endptr, " dB") == 0 ||
			    strcmp(endptr, "DB") == 0 || strcmp(endptr, " DB") == 0 ||
			    strcmp(endptr, "db") == 0 || strcmp(endptr, " db") == 0) &&
			    has_albumgain != 128)
			{
				albumgain = tmp + offset;
				has_albumgain = 1;
			}
		}
		vorbiskey = NULL;
	}
	else if (strcmp(key, "replaygain_track_gain") == 0) {
		if (value)
		{
			char *endptr;
			float tmp = strtof(value, &endptr);
			if ((*endptr == '\0' ||
			    strcmp(endptr, "dB") == 0 || strcmp(endptr, " dB") == 0 ||
			    strcmp(endptr, "DB") == 0 || strcmp(endptr, " DB") == 0 ||
			    strcmp(endptr, "db") == 0 || strcmp(endptr, " db") == 0) &&
			    has_trackgain != 128)
			{
				trackgain = tmp + offset;
				has_trackgain = 1;
			}
		}
		vorbiskey = NULL;
	}
	else if (strcmp(key, "R128_ALBUM_GAIN") == 0) {
		if (value)
		{
			char *endptr;
			float tmp = strtof(value, &endptr);
			if (*endptr == '\0')
			{
				// Opus has -23 LUFS, ReplayGain -18 LUFS
				albumgain = tmp/256.0 + offset + 5.0;
				has_albumgain = 128;
			}
		}
		vorbiskey = NULL;
	}
	else if (strcmp(key, "R128_TRACK_GAIN") == 0) {
		if (value)
		{
			char *endptr;
			float tmp = strtof(value, &endptr);
			if (*endptr == '\0')
			{
				// Opus has -23 LUFS, ReplayGain -18 LUFS
				trackgain = tmp/256.0 + offset + 5.0;
				has_trackgain = 128;
			}
		}
		vorbiskey = NULL;
	}
	else if (strcmp(key, "REPLAYGAIN_ALBUM_GAIN") == 0) {
		if (value)
		{
			char *endptr;
			float tmp = strtof(value, &endptr);
			if ((*endptr == '\0' ||
			    strcmp(endptr, "dB") == 0 || strcmp(endptr, " dB") == 0 ||
			    strcmp(endptr, "DB") == 0 || strcmp(endptr, " DB") == 0 ||
			    strcmp(endptr, "db") == 0 || strcmp(endptr, " db") == 0) &&
			    has_albumgain != 128)
			{
				albumgain = tmp + offset;
				has_albumgain = 1;
			}
		}
		vorbiskey = NULL;
	}
	else if (strcmp(key, "REPLAYGAIN_TRACK_GAIN") == 0) {
		if (value)
		{
			char *endptr;
			float tmp = strtof(value, &endptr);
			if ((*endptr == '\0' ||
			    strcmp(endptr, "dB") == 0 || strcmp(endptr, " dB") == 0 ||
			    strcmp(endptr, "DB") == 0 || strcmp(endptr, " DB") == 0 ||
			    strcmp(endptr, "db") == 0 || strcmp(endptr, " db") == 0) &&
			    has_trackgain != 128)
			{
				trackgain = tmp + offset;
				has_trackgain = 1;
			}
		}
		vorbiskey = NULL;
	}
	else if (strcmp(key, "replaygain_reference_loudness") == 0) {
		if (value)
		{
			char *endptr;
			float tmp = strtof(value, &endptr);
			if (*endptr == '\0' ||
			    strcmp(endptr, "dB") == 0 || strcmp(endptr, " dB") == 0 ||
			    strcmp(endptr, "DB") == 0 || strcmp(endptr, " DB") == 0 ||
			    strcmp(endptr, "db") == 0 || strcmp(endptr, " db") == 0)
			{
				ref = tmp;
			}
		}
		vorbiskey = NULL;
	}
	else if (strcmp(key, "REPLAYGAIN_REFERENCE_LOUDNESS") == 0) {
		if (value)
		{
			char *endptr;
			float tmp = strtof(value, &endptr);
			if (*endptr == '\0' ||
			    strcmp(endptr, "dB") == 0 || strcmp(endptr, " dB") == 0 ||
			    strcmp(endptr, "DB") == 0 || strcmp(endptr, " DB") == 0 ||
			    strcmp(endptr, "db") == 0 || strcmp(endptr, " db") == 0)
			{
				ref = tmp;
			}
		}
		vorbiskey = NULL;
	}
	else if (strcmp(key, "MP3GAIN_MINMAX") == 0) {
		vorbiskey = NULL;
	}
	else if (strcmp(key, "MP3GAIN_ALBUM_MINMAX") == 0) {
		vorbiskey = NULL;
	}
	else if (strcmp(key, "REPLAYGAIN_ALBUM_PEAK") == 0) {
		vorbiskey = NULL;
	}
	else if (strcmp(key, "REPLAYGAIN_TRACK_PEAK") == 0) {
		vorbiskey = NULL;
	}
	else if (strcmp(key, "replaygain_album_peak") == 0) {
		vorbiskey = NULL;
	}
	else if (strcmp(key, "replaygain_track_peak") == 0) {
		vorbiskey = NULL;
	}
	else if (strcmp(key, "replaygain_album_minmax") == 0) {
		vorbiskey = NULL;
	}
	else if (strcmp(key, "replaygain_track_minmax") == 0) {
		vorbiskey = NULL;
	}
	else if (strcmp(key, "major_brand") == 0) {
		vorbiskey = NULL;
	}
	else if (strcmp(key, "minor_version") == 0) {
		vorbiskey = NULL;
	}
	else if (strcmp(key, "compatible_brands") == 0) {
		vorbiskey = NULL;
	}
	else if (strcmp(key, "handler_name") == 0) {
		vorbiskey = NULL;
	}
	return vorbiskey;
}
// replaygain_track_gain, REPLAYGAIN_TRACK_GAIN, R128_TRACK_GAIN
// replaygain_album_gain, REPLAYGAIN_ALBUM_GAIN, R128_ALBUM_GAIN
// REPLAYGAIN_REFERENCE_LOUDNESS
// skip: REPLAYGAIN_ALBUM_PEAK, REPLAYGAIN_TRACK_PEAK, replaygain_track_peak, replaygain_track_minmax, replaygain_album_peak, replaygain_album_minmax
// others: convert to uppercase

struct KeyVal {
	char *key;
	char *val;
	struct KeyVal *next;
};

struct KeyVal *first_kv = NULL;
struct KeyVal *last_kv = NULL;

void add_keyval(const char *key, size_t keysz, const char *val, size_t valsz)
{
	struct KeyVal *cur;
	cur = malloc(sizeof(*cur));
	if (cur == NULL) {
		fprintf(stderr, "Out of memory\n");
		handler_impl();
		exit(1);
	}
	cur->key = malloc(keysz+1);
	if (cur->key == NULL) {
		fprintf(stderr, "Out of memory\n");
		handler_impl();
		exit(1);
	}
	cur->val = malloc(valsz+1);
	if (cur->val == NULL) {
		fprintf(stderr, "Out of memory\n");
		handler_impl();
		exit(1);
	}
	memcpy(cur->key, key, keysz);
	cur->key[keysz] = '\0';
	memcpy(cur->val, val, valsz);
	cur->val[valsz] = '\0';
	cur->next = NULL;
	if (first_kv == NULL)
	{
		first_kv = cur;
		last_kv = cur;
	}
	else
	{
		last_kv->next = cur;
		last_kv = cur;
	}
}

uint32_t get_u32_le(const void *ptr)
{
	const char *cptr = ptr;
	return
		((uint8_t)cptr[0]) |
		(((uint8_t)cptr[1])<<8) |
		(((uint8_t)cptr[2])<<16) |
		(((uint8_t)cptr[3])<<24);
}

uint32_t decode_syncsafe(const void *ptr)
{
	const char *cptr = ptr;
	return (((uint32_t)(uint8_t)cptr[0])<<21)
	     | (((uint32_t)(uint8_t)cptr[1])<<14)
	     | (((uint32_t)(uint8_t)cptr[2])<<7)
	     | (((uint32_t)(uint8_t)cptr[3])<<0);
}

int fskip(FILE *f, size_t sz)
{
	size_t i;
	for (i = 0; i < sz; i++)
	{
		int ch;
		ch = getc(f);
		if (ch == EOF)
		{
			return 0;
		}
	}
	return 1;
}

int maybe_unsync_read_aftersz(FILE *f, int unsync, void *buf, size_t sz, uint32_t *szout)
{
	char *cbufstart = buf;
	char *cbuf = buf;
	int cur_unsync = 0;
	size_t bytes_read = 0;
	if (!unsync)
	{
		return fread(buf, 1, sz, f) == sz;
	}
	else
	{
		while (bytes_read < sz)
		{
			int ch;
			ch = getc(f);
			if (ch == EOF)
			{
				return 0;
			}
			bytes_read++;
			if (!cur_unsync &&
			    cbuf - cbufstart > 0 && *(cbuf-1) == '\xff' &&
			    ch == '\x00')
			{
				cur_unsync = 1;
			}
			else
			{
				*cbuf++ = (uint8_t)ch;
				cur_unsync = 0;
			}
		}
		*szout = (cbuf - cbufstart);
		return 1;
	}
}

int maybe_unsync_read(FILE *f, int unsync, void *buf, size_t sz)
{
	char *cbufstart = buf;
	char *cbuf = buf;
	int cur_unsync = 0;
	if (!unsync)
	{
		return fread(buf, 1, sz, f) == sz;
	}
	else
	{
		while ((size_t)(cbuf - cbufstart) < sz)
		{
			int ch;
			ch = getc(f);
			if (ch == EOF)
			{
				return 0;
			}
			if (!cur_unsync &&
			    cbuf - cbufstart > 0 && *(cbuf-1) == '\xff' &&
			    ch == '\x00')
			{
				cur_unsync = 1;
			}
			else
			{
				*cbuf++ = (uint8_t)ch;
				cur_unsync = 0;
			}
		}
		return 1;
	}
}

size_t u16cmp(char *frame, size_t framesz, const char *key, size_t keysz)
{
	size_t i;
	if (frame[0] == '\x00' || frame[0] == '\x03')
	{
		if (memcmp(frame+1, key, keysz) != 0)
		{
			return 0;
		}
		memmove(frame, frame+1+keysz, (framesz-1-keysz));
		return framesz-1-keysz;
	}
	if (frame[0] == '\x02' && framesz >= 3 && (frame[1] != '\xfe' || frame[2] != '\xff'))
	{
		if (framesz-1 < 2*keysz)
		{
			return 0;
		}
		for (i = 0; i < keysz; i++)
		{
			if (frame[1+2*i+0] != '\x00' ||
			    frame[1+2*i+1] != key[i])

			{
				return 0;
			}
		}
		for (i = 0; i < (framesz-1-2*keysz)/2; i++)
		{
			if (frame[1+2*keysz+2*i+0] != '\x00')
			{
				return 0;
			}
		}
		for (i = 0; i < (framesz-1-2*keysz)/2; i++)
		{
			frame[i] = frame[1+2*keysz+2*i+1];
		}
		return (framesz-1-2*keysz)/2;
	}
	if ((frame[0] == '\x01' || frame[0] == '\x02') && framesz >= 3 && (frame[1] != '\xfe' || frame[2] != '\xff'))
	{
		if (framesz-3 < 2*keysz)
		{
			return 0;
		}
		for (i = 0; i < keysz; i++)
		{
			if (frame[3+2*i+0] != '\x00' ||
			    frame[3+2*i+1] != key[i])

			{
				return 0;
			}
		}
		for (i = 0; i < (framesz-3-2*keysz)/2; i++)
		{
			if (frame[3+2*keysz+2*i+0] != '\x00')
			{
				return 0;
			}
		}
		for (i = 0; i < (framesz-3-2*keysz)/2; i++)
		{
			frame[i] = frame[3+2*keysz+2*i+1];
		}
		return (framesz-3-2*keysz)/2;
	}
	if ((frame[0] == '\x01') && framesz >= 3 && frame[1] == '\xff' && frame[2] == '\xfe')
	{
		if (framesz-3 < 2*keysz)
		{
			return 0;
		}
		for (i = 0; i < keysz; i++)
		{
			if (frame[3+2*i+0] != key[i] ||
			    frame[3+2*i+1] != '\x00')

			{
				return 0;
			}
		}
		for (i = 0; i < (framesz-3-2*keysz)/2; i++)
		{
			if (frame[3+2*keysz+2*i+1] != '\x00')
			{
				return 0;
			}
		}
		for (i = 0; i < (framesz-3-2*keysz)/2; i++)
		{
			frame[i] = frame[3+2*keysz+2*i+0];
		}
		return (framesz-3-2*keysz)/2;
	}
	return 0;
}

void read_id3v24tag(FILE *f)
{
	char id3header5[5];
	uint8_t flags;
	uint32_t size;
	int unsync_all;
	//int experimental;
	int exthdr;
	//int footerpresent;
	if (fread(id3header5, 1, 5, f) != 5)
	{
		return;
	}
	flags = (uint8_t)id3header5[0];
	if ((id3header5[1]>>7) || 
	    (id3header5[2]>>7) ||
	    (id3header5[3]>>7) ||
	    (id3header5[4]>>7))
	{
		return;
	}
	size = decode_syncsafe(&id3header5[1]);
	unsync_all = (flags>>7)&1;
	exthdr = (flags>>6)&1;
	//experimental = (flags>>5)&1;
	//footerpresent = (flags>>4)&1;
	if (flags&15)
	{
		return;
	}
	if (exthdr)
	{
		char exthdr[4];
		uint32_t sz;
		if (fread(exthdr, 1, 4, f) != 4)
		{
			return;
		}
		sz = decode_syncsafe(exthdr);
		if (!fskip(f, sz))
		{
			return;
		}
	}
	for (;;)
	{
		char framehdr[10];
		char *frame;
		uint32_t framesz;
		uint16_t flags;
		int grouping_identity;
		int compression;
		int encryption;
		int frame_unsync;
		int data_length;
		if (ftell(f) > size+10)
		{
			return;
		}
		if (fread(framehdr, 1, 10, f) != 10)
		{
			return;
		}
		if (ftell(f) > size+10)
		{
			return;
		}
		framesz = decode_syncsafe(&framehdr[4]);
		flags =
			(((uint16_t)(uint8_t)framehdr[8]) << 8) |
			(((uint16_t)(uint8_t)framehdr[9]) << 0);
		grouping_identity = (flags >> 6)&1;
		compression = (flags >> 3)&1;
		encryption = (flags >> 2)&1;
		frame_unsync = (flags >> 1)&1;
		data_length = (flags >> 0)&1;
		if (data_length)
		{
			if (framesz < 4)
			{
				return;
			}
			fskip(f, 4);
			framesz -= 4;
		}
		frame = malloc(framesz+1);
		if (!frame)
		{
			fprintf(stderr, "Out of memory\n");
			handler_impl();
			exit(1);
		}
		if (!maybe_unsync_read_aftersz(f, unsync_all || frame_unsync, frame, framesz, &framesz))
		{
			free(frame);
			return;
		}
		if (ftell(f) > size+10)
		{
			free(frame);
			return;
		}
		if (grouping_identity)
		{
			free(frame); // don't know how to handle this
			continue;
		}
		if (compression || encryption)
		{
			free(frame);
			continue;
		}
		if (data_length)
		{
			free(frame); // probably compressed or encrypted
			continue;
		}
		if (memcmp(framehdr, "TXXX", 4) != 0)
		{
			free(frame);
			continue;
		}
		if (framesz && (frame[0] == '\x00' || frame[0] == '\x01' || frame[0] == '\x02'))
		{
			char *x;
			size_t res;
			size_t xsz;
			x = "replaygain_reference_loudness";
			xsz = strlen(x);
			res = u16cmp(frame, framesz, x, xsz+1);
			if (res)
			{
				char *endptr = NULL;
				float ref_loudness;
				frame[res] = '\0';
				ref_loudness = strtof(frame, &endptr);
				if (*endptr == '\x00' || strcmp(endptr, "dB") == 0 || strcmp(endptr, " dB") == 0)
				{
					ref = ref_loudness;
				}
				free(frame);
				continue;
			}
			x = "replaygain_track_gain";
			xsz = strlen(x);
			res = u16cmp(frame, framesz, x, xsz+1);
			if (res)
			{
				char *endptr = NULL;
				float track_gain;
				frame[res] = '\0';
				track_gain = strtof(frame, &endptr);
				if (*endptr == '\x00' || strcmp(endptr, "dB") == 0 || strcmp(endptr, " dB") == 0)
				{
					trackgain = track_gain + offset;
					has_trackgain = 1;
				}
				free(frame);
				continue;
			}
			x = "replaygain_album_gain";
			xsz = strlen(x);
			res = u16cmp(frame, framesz, x, xsz+1);
			if (res)
			{
				char *endptr = NULL;
				float album_gain;
				frame[res] = '\0';
				album_gain = strtof(frame, &endptr);
				if (*endptr == '\x00' || strcmp(endptr, "dB") == 0 || strcmp(endptr, " dB") == 0)
				{
					albumgain = album_gain + offset;
					has_albumgain = 1;
				}
				free(frame);
				continue;
			}
		}
		free(frame);
	}
}

void read_id3v23tag(FILE *f)
{
	char id3header5[5];
	uint8_t flags;
	uint32_t size;
	int unsync;
	//int experimental;
	int exthdr;
	if (fread(id3header5, 1, 5, f) != 5)
	{
		return;
	}
	flags = (uint8_t)id3header5[0];
	if ((id3header5[1]>>7) || 
	    (id3header5[2]>>7) ||
	    (id3header5[3]>>7) ||
	    (id3header5[4]>>7))
	{
		return;
	}
	size = decode_syncsafe(&id3header5[1]);
	unsync = (flags>>7)&1;
	exthdr = (flags>>6)&1;
	//experimental = (flags>>5)&1;
	if (flags&31)
	{
		return;
	}
	if (exthdr)
	{
		char exthdr[4];
		char exthdr2[10];
		uint32_t sz;
		if (!maybe_unsync_read(f, unsync, exthdr, 4))
		{
			return;
		}
		sz = 
			(((uint32_t)(uint8_t)exthdr[0])<<24) |
			(((uint32_t)(uint8_t)exthdr[1])<<16) |
			(((uint32_t)(uint8_t)exthdr[2])<<8) |
			(((uint32_t)(uint8_t)exthdr[3])<<0);
		if (sz != 6 && sz != 10)
		{
			return;
		}
		if (!maybe_unsync_read(f, unsync, exthdr2, sz))
		{
			return;
		}
	}
	for (;;)
	{
		char framehdr[10];
		char *frame;
		uint32_t framesz;
		//uint16_t flags;
		if (ftell(f) > size+10)
		{
			return;
		}
		if (!maybe_unsync_read(f, unsync, framehdr, 10))
		{
			return;
		}
		if (ftell(f) > size+10)
		{
			return;
		}
		framesz =
			(((uint32_t)(uint8_t)framehdr[4]) << 24) |
			(((uint32_t)(uint8_t)framehdr[5]) << 16) |
			(((uint32_t)(uint8_t)framehdr[6]) << 8) |
			(((uint32_t)(uint8_t)framehdr[7]) << 0);
		//flags =
		//	(((uint16_t)(uint8_t)framehdr[8]) << 8) |
		//	(((uint16_t)(uint8_t)framehdr[9]) << 0);
		frame = malloc(framesz+1);
		if (!frame)
		{
			fprintf(stderr, "Out of memory\n");
			handler_impl();
			exit(1);
		}
		if (!maybe_unsync_read(f, unsync, frame, framesz))
		{
			free(frame);
			return;
		}
		if (ftell(f) > size+10)
		{
			free(frame);
			return;
		}
		if (memcmp(framehdr, "TXXX", 4) != 0)
		{
			free(frame);
			continue;
		}
		if (framesz && (frame[0] == '\x00' || frame[0] == '\x01' || frame[0] == '\x02'))
		{
			char *x;
			size_t res;
			size_t xsz;
			x = "replaygain_reference_loudness";
			xsz = strlen(x);
			res = u16cmp(frame, framesz, x, xsz+1);
			if (res)
			{
				char *endptr = NULL;
				float ref_loudness;
				frame[res] = '\0';
				ref_loudness = strtof(frame, &endptr);
				if (*endptr == '\x00' || strcmp(endptr, "dB") == 0 || strcmp(endptr, " dB") == 0)
				{
					ref = ref_loudness;
				}
				free(frame);
				continue;
			}
			x = "replaygain_track_gain";
			xsz = strlen(x);
			res = u16cmp(frame, framesz, x, xsz+1);
			if (res)
			{
				char *endptr = NULL;
				float track_gain;
				frame[res] = '\0';
				track_gain = strtof(frame, &endptr);
				if (*endptr == '\x00' || strcmp(endptr, "dB") == 0 || strcmp(endptr, " dB") == 0)
				{
					trackgain = track_gain + offset;
					has_trackgain = 1;
				}
				free(frame);
				continue;
			}
			x = "replaygain_album_gain";
			xsz = strlen(x);
			res = u16cmp(frame, framesz, x, xsz+1);
			if (res)
			{
				char *endptr = NULL;
				float album_gain;
				frame[res] = '\0';
				album_gain = strtof(frame, &endptr);
				if (*endptr == '\x00' || strcmp(endptr, "dB") == 0 || strcmp(endptr, " dB") == 0)
				{
					albumgain = album_gain + offset;
					has_albumgain = 1;
				}
				free(frame);
				continue;
			}
		}
		free(frame);
	}
}

void read_id3v22tag(FILE *f)
{
	char id3header5[5];
	uint8_t flags;
	uint32_t size;
	int unsync;
	int compression;
	if (fread(id3header5, 1, 5, f) != 5)
	{
		return;
	}
	flags = (uint8_t)id3header5[0];
	if ((id3header5[1]>>7) || 
	    (id3header5[2]>>7) ||
	    (id3header5[3]>>7) ||
	    (id3header5[4]>>7))
	{
		return;
	}
	size = decode_syncsafe(&id3header5[1]);
	unsync = (flags>>7)&1;
	compression = (flags>>6)&1;
	if (compression)
	{
		return;
	}
	if (flags&63)
	{
		return;
	}
	for (;;)
	{
		char framehdr[6];
		char *frame;
		uint32_t framesz;
		if (ftell(f) > size+10)
		{
			return;
		}
		if (!maybe_unsync_read(f, unsync, framehdr, 6))
		{
			return;
		}
		if (ftell(f) > size+10)
		{
			return;
		}
		framesz =
			(((uint32_t)(uint8_t)framehdr[3]) << 16) |
			(((uint32_t)(uint8_t)framehdr[4]) << 8) |
			(((uint32_t)(uint8_t)framehdr[5]) << 0);
		frame = malloc(framesz+1);
		if (!frame)
		{
			fprintf(stderr, "Out of memory\n");
			handler_impl();
			exit(1);
		}
		if (!maybe_unsync_read(f, unsync, frame, framesz))
		{
			free(frame);
			return;
		}
		if (ftell(f) > size+10)
		{
			free(frame);
			return;
		}
		if (memcmp(framehdr, "TXX", 3) != 0)
		{
			free(frame);
			continue;
		}
		if (framesz && (frame[0] == '\x00' || frame[0] == '\x01'))
		{
			char *x;
			size_t res;
			size_t xsz;
			x = "replaygain_reference_loudness";
			xsz = strlen(x);
			res = u16cmp(frame, framesz, x, xsz+1);
			if (res)
			{
				char *endptr = NULL;
				float ref_loudness;
				frame[res] = '\0';
				ref_loudness = strtof(frame, &endptr);
				if (*endptr == '\x00' || strcmp(endptr, "dB") == 0 || strcmp(endptr, " dB") == 0)
				{
					ref = ref_loudness;
				}
				free(frame);
				continue;
			}
			x = "replaygain_track_gain";
			xsz = strlen(x);
			res = u16cmp(frame, framesz, x, xsz+1);
			if (res)
			{
				char *endptr = NULL;
				float track_gain;
				frame[res] = '\0';
				track_gain = strtof(frame, &endptr);
				if (*endptr == '\x00' || strcmp(endptr, "dB") == 0 || strcmp(endptr, " dB") == 0)
				{
					trackgain = track_gain + offset;
					has_trackgain = 1;
				}
				free(frame);
				continue;
			}
			x = "replaygain_album_gain";
			xsz = strlen(x);
			res = u16cmp(frame, framesz, x, xsz+1);
			if (res)
			{
				char *endptr = NULL;
				float album_gain;
				frame[res] = '\0';
				album_gain = strtof(frame, &endptr);
				if (*endptr == '\x00' || strcmp(endptr, "dB") == 0 || strcmp(endptr, " dB") == 0)
				{
					albumgain = album_gain + offset;
					has_albumgain = 1;
				}
				free(frame);
				continue;
			}
		}
		free(frame);
	}
}


void read_id3v2tag(const char *fname)
{
	char id3header[5];
	FILE *f;
	f = fopen(fname, "rb");
	if (f == NULL)
	{
		return;
	}
	if (fread(id3header, 1, 5, f) != 5)
	{
		fclose(f);
		return;
	}
	if (memcmp(id3header, "ID3", 3) != 0)
	{
		fclose(f);
		return;
	}
	if ((uint8_t)id3header[3] == 2)
	{
		read_id3v22tag(f);
	}
	else if ((uint8_t)id3header[3] == 3)
	{
		read_id3v23tag(f);
	}
	else if ((uint8_t)id3header[3] == 4)
	{
		read_id3v24tag(f);
	}
	fclose(f);
}

void read_apetag(const char *fname)
{
	FILE *f;
	char apefooter[32];
	char apeheader[32];
	char id3v1tag[128];
	uint32_t apelen;
	uint32_t apeitems;
	//uint32_t apeflags;
	uint32_t apelen2;
	uint32_t apeitems2;
	//uint32_t apeflags2;
	long file_len;
	int has_id3 = 0;
	f = fopen(fname, "rb");
	if (f == NULL)
	{
		return;
	}
	fseek(f, 0, SEEK_END);
	file_len = ftell(f);
	if (file_len < 0)
	{
		fclose(f);
		return;
	}
	fseek(f, -32, SEEK_END);
	if (fread(apefooter, 1, 32, f) != 32)
	{
		fclose(f);
		return;
	}
	if (memcmp(apefooter, "APETAGEX", 8) != 0)
	{
		fseek(f, -128, SEEK_END);
		if (fread(id3v1tag, 1, 128, f) != 128)
		{
			fclose(f);
			return;
		}
		if (memcmp(id3v1tag, "TAG", 3) == 0)
		{
			has_id3 = 1;
			fseek(f, -128-32, SEEK_END);
			if (fread(apefooter, 1, 32, f) != 32)
			{
				fclose(f);
				return;
			}
		}
	}
	if (memcmp(apefooter, "APETAGEX", 8) != 0)
	{
		fclose(f);
		return;
	}
	if (get_u32_le(&apefooter[8]) != 2000)
	{
		fclose(f);
		return;
	}
	apelen = get_u32_le(&apefooter[12]);
	apeitems = get_u32_le(&apefooter[16]);
	//apeflags = get_u32_le(&apefooter[20]);
	if ((unsigned long)file_len < (unsigned long)apelen)
	{
		fclose(f);
		return;
	}
	if ((unsigned long)file_len >= (unsigned long)apelen + (has_id3 ? (128+32) : 32))
	{
		uint32_t itemid;
		fseek(f, -(long)apelen-(has_id3 ? (128+32) : 32), SEEK_END);
		if (fread(apeheader, 1, 32, f) != 32)
		{
			fclose(f);
			return;
		}
		if (memcmp(apeheader, "APETAGEX", 8) != 0)
		{
			fclose(f);
			return;
		}
		if (get_u32_le(&apeheader[8]) != 2000)
		{
			fclose(f);
			return;
		}
		apelen2 = get_u32_le(&apeheader[12]);
		apeitems2 = get_u32_le(&apeheader[16]);
		//apeflags2 = get_u32_le(&apeheader[20]);
		if (apelen2 != apelen || apeitems2 != apeitems)
		{
			fclose(f);
			return;
		}
		for (itemid = 0; itemid < apeitems2; itemid++)
		{
			char tagheader[8];
			char *key = NULL;
			char *val = NULL;
			size_t keysz = 0;
			size_t keycap = 0;
			uint32_t taglen, tagflags;
			if (fread(tagheader, 1, 8, f) != 8) {
				fclose(f);
				return;
			}
			taglen = get_u32_le(&tagheader[0]);
			tagflags = get_u32_le(&tagheader[4]);
			for (;;) {
				int ret;
				ret = getc(f);
				if (ret == EOF) {
					free(key);
					fclose(f);
					return;
				}
				if (keysz + 1 > keycap)
				{
					char *key2;
					key2 = realloc(key, 2*keycap+16);
					if (key2 == NULL)
					{
						fprintf(stderr, "Out of memory\n");
						handler_impl();
						exit(1);
					}
					keycap = 2*keycap+16;
					key = key2;
				}
				if (ret == 0) {
					key[keysz] = '\0';
					break;
				}
				key[keysz++] = (char)ret;
			}
			val = malloc(taglen+1);
			if (val == NULL)
			{
				fprintf(stderr, "Out of memory\n");
				handler_impl();
				exit(1);
			}
			val[taglen] = '\0';
			if (fread(val, 1, taglen, f) != taglen)
			{
				free(key);
				free(val);
				fclose(f);
				return;
			}
			if ((tagflags & 6) == 0) {
				if (strcmp(key, "MP3GAIN_MINMAX") == 0 ||
				    strcmp(key, "MP3GAIN_ALBUM_MINMAX") == 0 ||
				    strcmp(key, "REPLAYGAIN_TRACK_PEAK") == 0 ||
				    strcmp(key, "REPLAYGAIN_ALBUM_PEAK") == 0)
				{
					free(key);
					free(val);
					continue;
				}
				if (strcmp(key, "REPLAYGAIN_TRACK_GAIN") == 0)
				{
					char *endptr;
					float tmp = strtof(val, &endptr);
					if ((*endptr == '\0' ||
					    strcmp(endptr, "dB") == 0 || strcmp(endptr, " dB") == 0 ||
					    strcmp(endptr, "DB") == 0 || strcmp(endptr, " DB") == 0 ||
					    strcmp(endptr, "db") == 0 || strcmp(endptr, " db") == 0) &&
					    has_trackgain != 128)
					{
						trackgain = tmp + offset;
						has_trackgain = 1;
					}
					free(key);
					free(val);
					continue;
				}
				if (strcmp(key, "REPLAYGAIN_ALBUM_GAIN") == 0)
				{
					char *endptr;
					float tmp = strtof(val, &endptr);
					if ((*endptr == '\0' ||
					    strcmp(endptr, "dB") == 0 || strcmp(endptr, " dB") == 0 ||
					    strcmp(endptr, "DB") == 0 || strcmp(endptr, " DB") == 0 ||
					    strcmp(endptr, "db") == 0 || strcmp(endptr, " db") == 0) &&
					    has_albumgain != 128)
					{
						albumgain = tmp + offset;
						has_albumgain = 1;
					}
					// Track => Tracknumber
					free(key);
					free(val);
					continue;
				}
				if (strcasecmp(key, "album") == 0)
				{
					const char *key2 = "ALBUM";
					add_keyval(key2, strlen(key2), val, taglen);
					free(key);
					free(val);
					continue;
				}
				if (strcasecmp(key, "artist") == 0)
				{
					const char *key2 = "ARTIST";
					add_keyval(key2, strlen(key2), val, taglen);
					free(key);
					free(val);
					continue;
				}
				if (strcasecmp(key, "genre") == 0)
				{
					const char *key2 = "GENRE";
					add_keyval(key2, strlen(key2), val, taglen);
					free(key);
					free(val);
					continue;
				}
				if (strcasecmp(key, "title") == 0)
				{
					const char *key2 = "TITLE";
					add_keyval(key2, strlen(key2), val, taglen);
					free(key);
					free(val);
					continue;
				}
				if (strcasecmp(key, "year") == 0)
				{
					const char *key2 = "YEAR";
					add_keyval(key2, strlen(key2), val, taglen);
					free(key);
					free(val);
					continue;
				}
				if (strcasecmp(key, "track") == 0)
				{
					const char *key2 = "TRACKNUMBER";
					add_keyval(key2, strlen(key2), val, taglen);
					free(key);
					free(val);
					continue;
				}
				add_keyval(key, strlen(key), val, taglen);
			}
			free(key);
			free(val);
		}
	}
	fclose(f);
}

int main(int argc, char **argv)
{
	int ret;
	const AVCodec *dec;
	AVFrame *frame = NULL;
	AVPacket *packet = NULL;
	int opt;
	FILE *npf = NULL;
	char *endptr;
	char *writenp = NULL;
	size_t fnamebuflen;
	char *fnamebuf;
	const char *homedir;
	int first;
	int sock = -1;
	int usesock = 0;
	int urlmode = 0;
	const char *sockarg = NULL;
	struct sockaddr_un sun;
	uint32_t inqueue;
	uint32_t bytes_per_sec;
	struct KeyVal *cur;
#if 0
	AVStream *audio_stream;
#endif

	while ((opt = getopt(argc, argv, "us:g:GW:")) != -1) {
		switch (opt) {
			case 'u':
				urlmode = 1;
				break;
			case 's':
				sockarg = optarg;
				usesock = 1;
				break;
			case 'G':
				usegain = 1;
				break;
			case 'W':
				writenp = optarg;
				break;
			case 'g':
				if (!optarg)
				{
					usage(argv[0]);
				}
				gain_db = strtof(optarg, &endptr);
				if (*optarg == '\0' || *endptr != '\0') {
					usage(argv[0]);
				}
				volume_mul = powf(10.0, (gain_db+volume_db)/20.0);
				break;
			default: // '?'
				usage(argv[0]);
				break;
		}
	}

	pfds = malloc(2*sizeof(*pfds));
	escapes = malloc(2*sizeof(*escapes));
	if (pfds == NULL) {
		fprintf(stderr, "Out of memory\n");
		handler_impl();
		exit(1);
	}

	if (usesock) {
		sock = socket(AF_UNIX, SOCK_STREAM, 0);
	}
	sun.sun_family = AF_UNIX;
	homedir = getenv("HOME");
	pfds[0].fd = -1;
	pfds[0].events = 0;
	if (sock > 0 && homedir && *homedir != '\0') {
		//if (snprintf(sun.sun_path, sizeof(sun.sun_path), "%s/.mploop/sock", homedir) < (int)sizeof(sun.sun_path)) {
		if (snprintf(sun.sun_path, sizeof(sun.sun_path), "%s", sockarg) < (int)sizeof(sun.sun_path)) {
			unlink(sun.sun_path);
			if (bind(sock, (const struct sockaddr*)&sun, sizeof(sun)) == 0) {
				if (listen(sock, 16) == 0) {
					pfds[0].fd = sock;
					pfds[0].events = POLLIN;
				}
			}
		}
	}

	pfds_capacity = 2;
	pfds_size = 2;
	pfds[1].fd = 0;
	pfds[1].events = POLLIN;
	escapes[1].escapebufsize = 0;
	escapes[1].escape_time64 = 0;

	if (homedir && *homedir != '\0') {
		if (snprintf(volfilebuf, sizeof(volfilebuf), "%s/.mploop/vol.txt", homedir) < (int)sizeof(volfilebuf)) {
			FILE *f;
			volfile = volfilebuf;
			f = fopen(volfile, "r");
			if (f) {
				char volline[256];
				if (fgets(volline, (int)sizeof(volline), f))
				{
					volume_db = strtof(volline, &endptr);
					volume_mul = powf(10.0, (gain_db+volume_db)/20.0);
				}
				fclose(f);
			}
		}
	}
	if (argc != optind + 1) {
		usage(argv[0]);
	}
	if (!urlmode && access(argv[optind], R_OK) != 0) {
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
	snprintf(fnamebuf, fnamebuflen, urlmode ? "%s" : "file:%s", argv[optind]);

	SDL_Init(SDL_INIT_AUDIO);
#if LIBAVFORMAT_VERSION_MAJOR > 58 || (LIBAVFORMAT_VERSION_MAJOR == 58 && LIBAVFORMAT_VERSION_MINOR >= 9)
#else
	av_register_all();
#endif
	av_log_set_callback(log_cb);

	avoid_state = AVOID_OGG_MSG; // Avoid opus message from ogg: "693 bytes of comment header remain"
	if (avformat_open_input(&avfctx, fnamebuf, NULL, NULL) < 0) {
		fprintf(stderr, "File %s is probably not an audio file, can't open it\n", argv[optind]);
		handler_impl();
		exit(1);
	}
	avoid_state = AVOID_NONE;
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

	if (!urlmode) {
		read_apetag(argv[optind]);
		read_id3v2tag(argv[optind]);
	}

	AVDictionary *whole_file_metadata = avfctx->metadata;
	AVDictionary *stream_metadata = avfctx->streams[aidx]->metadata;
	const AVDictionaryEntry *e = NULL;
	e = NULL;
	while ((e = av_dict_get(whole_file_metadata, "", e, AV_DICT_IGNORE_SUFFIX)) != NULL) {
		get_vorbiskey(e->key, e->value);
	}
	e = NULL;
	while ((e = av_dict_get(stream_metadata, "", e, AV_DICT_IGNORE_SUFFIX)) != NULL) {
		get_vorbiskey(e->key, e->value);
	}
	printf("================================================================================\n");
	if (usegain)
	{
		const struct AVReplayGain *av_gain = NULL;
#if LIBAVFORMAT_VERSION_MAJOR < 59
		int size = 0;
#else
		size_t size = 0;
#endif
		if (has_albumgain == 128) {
			gain_db += albumgain;
		} else if (has_trackgain == 128) {
			gain_db += trackgain;
		} else if (has_albumgain) {
			gain_db += albumgain + (magic_ref - ref);
		} else if (has_trackgain) {
			gain_db += trackgain + (magic_ref - ref);
		} else {
			// TODO add optional support for ReplayGain in APE tag
#if LIBAVCODEC_VERSION_MAJOR > 60 || (LIBAVCODEC_VERSION_MAJOR == 60 && LIBAVCODEC_VERSION_MINOR >= 30)
			const struct AVPacketSideData *sd;
			sd = av_packet_side_data_get(
					avfctx->streams[aidx]->codecpar->coded_side_data,
					avfctx->streams[aidx]->codecpar->nb_coded_side_data,
					AV_PKT_DATA_REPLAYGAIN);
			av_gain = (const AVReplayGain*)sd->data;
			size = sd->size;
#else
			av_gain = (AVReplayGain*)av_stream_get_side_data(avfctx->streams[aidx], AV_PKT_DATA_REPLAYGAIN, &size);
#endif
			if (av_gain != NULL) {
				if (size != sizeof(*av_gain)) {
					fprintf(stderr, "Error: invalid ReplayGain data\n");
				} else {
					if (av_gain->album_gain != INT32_MIN) {
						gain_db += av_gain->album_gain/(float)100000 + offset;
					} else if (av_gain->track_gain != INT32_MIN) {
						gain_db += av_gain->track_gain/(float)100000 + offset;
					}
				}
			}
		}
		printf("Applying gain: %.2f dB\n", gain_db);
		volume_mul = powf(10.0, (gain_db+volume_db)/20.0);
	} else {
		printf("Applying gain: %.2f dB\n", gain_db);
	}
	printf("File: %s\n", argv[optind]);
	if (writenp)
	{
		npf = fopen(writenp, "w");
		if (npf != NULL)
		{
			fprintf(npf, "FILE=%s\n", argv[optind]);
		}
	}
	e = NULL;
	//printf("Whole file metadata\n");
	while ((e = av_dict_get(whole_file_metadata, "", e, AV_DICT_IGNORE_SUFFIX)) != NULL) {
		const char *vkey;
		int colon;
		if (skip_key(e->key) || get_vorbiskey(e->key, NULL) == NULL) {
			continue;
		}
		vkey = get_vorbiskey(e->key, NULL);
		if (npf != NULL)
		{
			fprintf(npf, "%s=%s\n", vkey, e->value);
		}
		vkey = mangle_vorbiskey(vkey, &colon);
		if (*vkey) {
			printf("%c", toupper(*vkey));
			vkey++;
		}
		while (*vkey) {
			printf("%c", tolower(*vkey));
			vkey++;
		}
		if (colon) {
			printf(":");
		}
		printf(" ");
		printf("%s\n", e->value);
	}
	e = NULL;
	//printf("Stream metadata\n");
	while ((e = av_dict_get(stream_metadata, "", e, AV_DICT_IGNORE_SUFFIX)) != NULL) {
		const char *vkey;
		int colon;
		if (skip_key(e->key) || get_vorbiskey(e->key, NULL) == NULL) {
			continue;
		}
		vkey = get_vorbiskey(e->key, NULL);
		if (npf != NULL)
		{
			fprintf(npf, "%s=%s\n", vkey, e->value);
		}
		vkey = mangle_vorbiskey(vkey, &colon);
		if (*vkey) {
			printf("%c", toupper(*vkey));
			vkey++;
		}
		while (*vkey) {
			printf("%c", tolower(*vkey));
			vkey++;
		}
		if (colon) {
			printf(":");
		}
		printf(" ");
		printf("%s\n", e->value);
	}
	cur = first_kv;
	while (cur != NULL) {
		const char *vkey;
		int colon;
		if (npf != NULL)
		{
			fprintf(npf, "%s=%s\n", cur->key, cur->val);
		}
		vkey = mangle_vorbiskey(cur->key, &colon);
		if (*vkey) {
			printf("%c", toupper(*vkey));
			vkey++;
		}
		while (*vkey) {
			printf("%c", tolower(*vkey));
			vkey++;
		}
		if (colon) {
			printf(":");
		}
		printf(" ");
		printf("%s\n", cur->val);
		cur = cur->next;
	}
	if (npf != NULL)
	{
		fclose(npf);
	}
	printf("--------------------------------------------------------------------------------\n");
#if LIBAVCODEC_VERSION_MAJOR > 57 || (LIBAVCODEC_VERSION_MAJOR == 57 && LIBAVCODEC_VERSION_MINOR >= 25)
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
#else
	dec = avcodec_find_decoder(avfctx->streams[aidx]->codec->codec_id);
	if (!dec) {
		fprintf(stderr, "File %s is probably not an audio file, can't find decoder\n", argv[optind]);
		handler_impl();
		exit(1);
	}
	adecctx = avfctx->streams[aidx]->codec;
#endif
	if (avcodec_open2(adecctx, dec, NULL) < 0) {
		fprintf(stderr, "Cannot open audio codec, probably out of memory\n");
		handler_impl();
		exit(1);
	}
//#if LIBAVFORMAT_VERSION_MAJOR > 59 || (LIBAVFORMAT_VERSION_MAJOR == 59 && LIBAVFORMAT_VERSION_MINOR >= 19)
#if LIBAVCODEC_VERSION_MAJOR > 59 || (LIBAVCODEC_VERSION_MAJOR == 59 && LIBAVCODEC_VERSION_MINOR >= 24)
	if (adecctx->ch_layout.nb_channels == 1) {
		leftidx = 0;
		rightidx = 0;
		chcount = adecctx->ch_layout.nb_channels;
	} else {
		if (leftidx < 0) {
			leftidx = av_channel_layout_index_from_channel(&adecctx->ch_layout, AV_CHAN_STEREO_LEFT);
		}
		if (leftidx < 0) {
			leftidx = av_channel_layout_index_from_channel(&adecctx->ch_layout, AV_CHAN_FRONT_LEFT);
		}
		if (rightidx < 0) {
			rightidx = av_channel_layout_index_from_channel(&adecctx->ch_layout, AV_CHAN_STEREO_RIGHT);
		}
		if (rightidx < 0) {
			rightidx = av_channel_layout_index_from_channel(&adecctx->ch_layout, AV_CHAN_FRONT_RIGHT);
		}
		chcount = adecctx->ch_layout.nb_channels;
	}
	if (leftidx < 0 || rightidx < 0) {
		fprintf(stderr, "Invalid channel conf\n");
		handler_impl();
		exit(1);
	}
#else
	leftidx = 0;
	rightidx = (adecctx->channels > 1) ? 1 : 0;
	if (adecctx->channel_layout == 0 && adecctx->channels == 1) {
		//chcount = 1;
		chcount = adecctx->channels;
	} else if (adecctx->channel_layout == 0 && adecctx->channels == 2) {
		//chcount = 2;
		chcount = adecctx->channels;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_MONO) {
		//chcount = 1;
		chcount = adecctx->channels;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_STEREO) {
		//chcount = 2;
		chcount = adecctx->channels;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_2POINT1) {
		//chcount = 3;
		chcount = adecctx->channels;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_2_1) {
		//chcount = 3;
		chcount = adecctx->channels;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_SURROUND) {
		//chcount = 3;
		chcount = adecctx->channels;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_3POINT1) {
		//chcount = 4;
		chcount = adecctx->channels;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_4POINT0) {
		//chcount = 4;
		chcount = adecctx->channels;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_4POINT1) {
		//chcount = 5;
		chcount = adecctx->channels;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_2_2) {
		//chcount = 4;
		chcount = adecctx->channels;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_QUAD) {
		//chcount = 4;
		chcount = adecctx->channels;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_5POINT0) {
		//chcount = 5;
		chcount = adecctx->channels;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_5POINT1) {
		//chcount = 6;
		chcount = adecctx->channels;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_5POINT0_BACK) {
		//chcount = 5;
		chcount = adecctx->channels;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_5POINT1_BACK) {
		//chcount = 6;
		chcount = adecctx->channels;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_6POINT0) {
		//chcount = 6;
		chcount = adecctx->channels;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_6POINT0_FRONT) {
		//chcount = 6;
		chcount = adecctx->channels;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_HEXAGONAL) {
		//chcount = 6;
		chcount = adecctx->channels;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_6POINT1) {
		//chcount = 7;
		chcount = adecctx->channels;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_6POINT1_BACK) {
		//chcount = 7;
		chcount = adecctx->channels;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_6POINT1_FRONT) {
		//chcount = 7;
		chcount = adecctx->channels;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_7POINT0) {
		//chcount = 7;
		chcount = adecctx->channels;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_7POINT0_FRONT) {
		//chcount = 7;
		chcount = adecctx->channels;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_7POINT1) {
		//chcount = 8;
		chcount = adecctx->channels;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_7POINT1_WIDE) {
		//chcount = 8;
		chcount = adecctx->channels;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_7POINT1_WIDE_BACK) {
		//chcount = 8;
		chcount = adecctx->channels;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_OCTAGONAL) {
		//chcount = 8;
		chcount = adecctx->channels;
	} else if (adecctx->channel_layout == AV_CH_LAYOUT_STEREO_DOWNMIX) {
		//chcount = 2;
		chcount = adecctx->channels;
	} else {
		fprintf(stderr, "Unsupported channel conf %lld\n", (long long)adecctx->channel_layout);
		if (adecctx->channels >= 2) {
			fprintf(stderr, "Guessing first two channels are left and right");
			chcount = adecctx->channels;
		} else {
			handler_impl();
			exit(1);
		}
	}
#endif
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
#if LIBAVUTIL_VERSION_MAJOR > 55 || (LIBAVUTIL_VERSION_MAJOR == 55 && LIBAVUTIL_VERSION_MINOR >= 21)
		else if (adecctx->sample_fmt == AV_SAMPLE_FMT_S64) {
			//printf("nonplanar\n");
			nonplanar = 1;
			audio_format_as_sdl = AUDIO_S32SYS;
		}
		else if (adecctx->sample_fmt == AV_SAMPLE_FMT_S64P) {
			audio_format_as_sdl = AUDIO_S32SYS;
		}
#endif
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
	//const char *name = SDL_GetAudioDeviceName(0,0);
	const char *name = NULL;
	audid = SDL_OpenAudioDevice(name, 0, &desired, &obtained, SDL_AUDIO_ALLOW_ANY_CHANGE);
	if (audid <= 0) {
		fprintf(stderr, "Cannot open SDL audio\n");
		handler_impl();
		exit(1);
	}
#if SDL_MAJOR_VERSION > 2 || (SDL_MAJOR_VERSION == 2 && SDL_MINOR_VERSION > 0) || (SDL_MAJOR_VERSION == 2 && SDL_MINOR_VERSION == 0 && SDL_PATCHLEVEL >= 7)
	sdl_stream = SDL_NewAudioStream(audio_format_as_sdl, chcount > 2 ? 2 : chcount, adecctx->sample_rate, obtained.format, obtained.channels, obtained.freq);
#else
	SDL_BuildAudioCVT(&cvt, audio_format_as_sdl, chcount > 2 ? 2 : chcount, adecctx->sample_rate, obtained.format, obtained.channels, obtained.freq);
#endif
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
#if LIBAVCODEC_VERSION_MAJOR > 57 || (LIBAVCODEC_VERSION_MAJOR == 57 && LIBAVCODEC_VERSION_MINOR >= 13)
	packet = av_packet_alloc();
	if (!packet) {
		fprintf(stderr, "Cannot allocate packet, probably out of memory\n");
		handler_impl();
		exit(1);
	}
#else
	packet = malloc(sizeof(*packet));
	if (!packet) {
		fprintf(stderr, "Cannot allocate packet, probably out of memory\n");
		handler_impl();
		exit(1);
	}
	av_init_packet(packet);
#endif
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
	first = 1;
	while (av_read_frame(avfctx, packet) >= 0) {
		if (packet->stream_index == aidx) {
#if LIBAVCODEC_VERSION_MAJOR > 57 || (LIBAVCODEC_VERSION_MAJOR == 57 && LIBAVCODEC_VERSION_MINOR >= 37)
			if (first) {
				avoid_state = AVOID_OPUS_MSG; // Avoid opus message "Could not update timestamps for skipped samples."
			}
			ret = avcodec_send_packet(adecctx, packet);
			if (first) {
				avoid_state = AVOID_NONE;
				//first = 0; // Ugh, occurs at end too
			}
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
#else
			int got_frame;
			if (avcodec_decode_audio4(adecctx, frame, &got_frame, packet) < 0) {
				fprintf(stderr, "Cannot decode audio, probably corrupted file\n");
				handler_impl();
				exit(1);
			}
			if (got_frame) {
				output_audio_frame(frame);
				av_frame_unref(frame);
			}
#endif
		}
		av_packet_unref(packet);
		if (seeks != 0) {
			int64_t ts = pts + ((int64_t)seeks)*avfctx->streams[aidx]->time_base.den/avfctx->streams[aidx]->time_base.num;
			if (seeks < INT_MIN/4)
			{
				ts = 0;
			}
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
#if LIBAVCODEC_VERSION_MAJOR > 57 || (LIBAVCODEC_VERSION_MAJOR == 57 && LIBAVCODEC_VERSION_MINOR >= 37)
	if (adecctx) {
		ret = avcodec_send_packet(adecctx, NULL);
	}
#endif
	bytes_per_sec = (uint32_t)(obtained.freq*obtained.channels*obtained_data_size);
	while ((inqueue = (uint32_t)SDL_GetQueuedAudioSize(audid)) > 0) {
		usleep((useconds_t)(inqueue*0.9e6/bytes_per_sec)+300);
	}
	handler_impl();
	return 0;
}
