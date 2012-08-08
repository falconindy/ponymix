/* Copyright (c) 2012 Dave Reisner
 *
 * pulsemix.c
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


#include <pulse/pulseaudio.h>

union arg_t {
	long l;
	char *c;
	float f;
};

enum connectstate {
	STATE_CONNECTING = 0,
	STATE_CONNECTED,
	STATE_ERROR
};

enum type {
	TYPE_INVALID = 0,
	TYPE_SINK,
	TYPE_SOURCE,
	TYPE_STREAM
};

struct source_t;

struct source_ops_t {
	pa_operation *(* op_mute)(pa_context *, uint32_t, int, pa_context_success_cb_t, void *);
	pa_operation *(* op_vol)(pa_context *, uint32_t, const pa_cvolume *, pa_context_success_cb_t, void *);
	void (* op_print)(struct source_t *);
};

struct source_t {
	enum type t;
	struct source_ops_t ops;

	uint32_t idx;
	const char *name;
	const char *desc;
	const pa_channel_map *map;
	pa_cvolume volume;
	struct pa_proplist *proplist;
	int volume_percent;
	int mute;
	float balance;

	struct source_t *next_source;
};

struct pulseaudio_t {
	pa_context *cxt;
	pa_mainloop *mainloop;
	pa_mainloop_api *mainloop_api;
	enum connectstate state;
	int success;

	struct source_t *source;
};

void pulse_init(struct pulseaudio_t *pulse, const char *clientname);
int pulse_connect(struct pulseaudio_t *pulse);
void pulse_deinit(struct pulseaudio_t *pulse);
void clean_source_t(struct pulseaudio_t *pulse);

void get_streams(struct pulseaudio_t *pulse);
void get_stream_by_index(struct pulseaudio_t *pulse, uint32_t idx);
void get_default_sink(struct pulseaudio_t *pulse);
void get_sink_by_index(struct pulseaudio_t *pulse, uint32_t idx);
void get_sink_by_name(struct pulseaudio_t *pulse, const char *name);
void get_sinks(struct pulseaudio_t *pulse);
void get_default_source(struct pulseaudio_t *pulse);
void get_source_by_index(struct pulseaudio_t *pulse, uint32_t idx);
void get_source_by_name(struct pulseaudio_t *pulse, const char *name);
void get_sources(struct pulseaudio_t *pulse);

void print_sources(struct pulseaudio_t *pulse);

void set_default(struct pulseaudio_t *pulse);
void get_volume(struct pulseaudio_t *pulse);
int set_volume(struct pulseaudio_t *pulse, long v);
void get_balance(struct pulseaudio_t *pulse);
int set_balance(struct pulseaudio_t *pulse, float b);
int set_mute(struct pulseaudio_t *pulse, int mute);
int unmute(struct pulseaudio_t *pulse);
int mute(struct pulseaudio_t *pulse);

/* vim: set noet ts=2 sw=2: */
