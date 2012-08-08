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

#define _GNU_SOURCE
#include <errno.h>
#include <err.h>
#include <getopt.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pulsemix.h"

#define UNUSED __attribute__((unused))

#define CLAMP(x, low, high) \
	__extension__ ({ \
		typeof(x) _x = (x); \
		typeof(low) _low = (low); \
		typeof(high) _high = (high); \
		((_x > _high) ? _high : ((_x < _low) ? _low : _x)); \
	})

static void success_cb(pa_context UNUSED *c, int success, void *raw);
static void state_cb(pa_context UNUSED *c, void *raw);
static void stream_add_cb(pa_context UNUSED *c, const pa_sink_input_info *i, int eol, void *raw);
static void source_add_cb(pa_context UNUSED *c, const pa_source_info *i, int eol, void *raw);
static void sink_add_cb(pa_context UNUSED *c, const pa_sink_info *i, int eol, void *raw);
static void source_info_cb(pa_context UNUSED *c, const pa_server_info *i, void *raw);
static void sink_info_cb(pa_context UNUSED *c, const pa_server_info *i, void *raw);

static void pulse_async_wait(struct pulseaudio_t *pulse, pa_operation *op);
static struct source_t *stream_new(const pa_sink_input_info *stream_info);
static struct source_t *sink_new(const pa_sink_info *sink_info);
static struct source_t *source_new(const pa_source_info *source_info);
static void print_source(struct source_t *source);

/* CALLBAKCS {{{ */
static void success_cb(pa_context UNUSED *c, int success, void *raw)
{
	struct pulseaudio_t *pulse = raw;
	pulse->success = success;
}

static void state_cb(pa_context UNUSED *c, void *raw)
{
	struct pulseaudio_t *pulse = raw;

	switch (pa_context_get_state(pulse->cxt)) {
		case PA_CONTEXT_READY:
			pulse->state = STATE_CONNECTED;
			break;
		case PA_CONTEXT_FAILED:
			pulse->state = STATE_ERROR;
			break;
		case PA_CONTEXT_UNCONNECTED:
		case PA_CONTEXT_AUTHORIZING:
		case PA_CONTEXT_SETTING_NAME:
		case PA_CONTEXT_CONNECTING:
		case PA_CONTEXT_TERMINATED:
			break;
	}
}

static void stream_add_cb(pa_context UNUSED *c, const pa_sink_input_info *i, int eol, void *raw)
{
	struct pulseaudio_t *pulse = raw;
	struct source_t *s, *stream;

	if (eol)
		return;

	stream = stream_new(i);
	if (pulse->source == NULL)
		pulse->source = stream;
	else {
		s = pulse->source;
		stream->next_source = s;
		pulse->source = stream;
	}
}

static void source_add_cb(pa_context UNUSED *c, const pa_source_info *i, int eol, void *raw)
{
	struct pulseaudio_t *pulse = raw;
	struct source_t *s, *source;

	if (eol)
		return;

	source = source_new(i);
	if (pulse->source == NULL)
		pulse->source = source;
	else {
		s = pulse->source;
		source->next_source = s;
		pulse->source = source;
	}
}

static void sink_add_cb(pa_context UNUSED *c, const pa_sink_info *i, int eol, void *raw)
{
	struct pulseaudio_t *pulse = raw;
	struct source_t *s, *sink;

	if (eol)
		return;

	sink = sink_new(i);
	if (pulse->source == NULL)
		pulse->source = sink;
	else {
		s = pulse->source;
		sink->next_source = s;
		pulse->source = sink;
	}
}

static void source_info_cb(pa_context UNUSED *c, const pa_server_info *i, void *raw)
{
	const char **source_name = (const char **)raw;
	*source_name = i->default_source_name;
}

static void sink_info_cb(pa_context UNUSED *c, const pa_server_info *i, void *raw)
{
	const char **sink_name = (const char **)raw;
	*sink_name = i->default_sink_name;
}
/* }}} */

static void pulse_async_wait(struct pulseaudio_t *pulse, pa_operation *op)
{
	while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
		pa_mainloop_iterate(pulse->mainloop, 1, NULL);
}

static struct source_t *stream_new(const pa_sink_input_info *stream_info)
{
	struct source_t *stream = calloc(1, sizeof(struct source_t));
	stream->t = TYPE_STREAM;

	stream->idx            = stream_info->index;
	stream->name           = stream_info->name;
	stream->pp_name        = "Application";
	stream->proplist       = stream_info->proplist;
	stream->desc           = strdup(pa_proplist_gets(stream->proplist, PA_PROP_APPLICATION_NAME));
	stream->volume_percent = (int)(((double)pa_cvolume_avg(&stream->volume) * 100) / PA_VOLUME_NORM);
	stream->mute           = stream_info->mute;
	memcpy(&stream->volume, &stream_info->volume, sizeof(pa_cvolume));

	stream->ops.op_mute  = pa_context_set_sink_input_mute;
	stream->ops.op_vol   = pa_context_set_sink_input_volume;

	return stream;
}

static struct source_t *sink_new(const pa_sink_info *sink_info)
{
	struct source_t *sink = calloc(1, sizeof(struct source_t));
	sink->t = TYPE_SINK;

	sink->idx            = sink_info->index;
	sink->name           = sink_info->name;
	sink->pp_name        = "Output";
	sink->desc           = sink_info->description;
	sink->map            = &sink_info->channel_map;
	sink->volume_percent = (int)(((double)pa_cvolume_avg(&sink->volume) * 100) / PA_VOLUME_NORM);
	sink->mute           = sink_info->mute;
	sink->balance        = pa_cvolume_get_balance(&sink_info->volume, &sink_info->channel_map);
	memcpy(&sink->volume, &sink_info->volume, sizeof(pa_cvolume));

	sink->ops.op_mute  = pa_context_set_sink_mute_by_index;
	sink->ops.op_vol   = pa_context_set_sink_volume_by_index;

	return sink;
}

static struct source_t *source_new(const pa_source_info *source_info)
{
	struct source_t *source = calloc(1, sizeof(struct source_t));
	source->t = TYPE_SOURCE;

	source->idx            = source_info->index;
	source->name           = source_info->name;
	source->pp_name        = "Input";
	source->desc           = source_info->description;
	source->map            = &source_info->channel_map;
	source->volume_percent = (int)(((double)pa_cvolume_avg(&source->volume) * 100) / PA_VOLUME_NORM);
	source->mute           = source_info->mute;
	source->balance        = 0.0f;
	memcpy(&source->volume, &source_info->volume, sizeof(pa_cvolume));

	source->ops.op_mute  = pa_context_set_source_mute_by_index;
	source->ops.op_vol   = pa_context_set_source_volume_by_index;

	return source;
}

static void print_source(struct source_t *source)
{
	char *mute = source->mute ? "[Muted]" : "";
	printf("%s ID: %d\n %s\n %s\n Volume: %d%% %s\n", source->pp_name, source->idx, source->name, source->desc, source->volume_percent, mute);
}

void print_sources(struct pulseaudio_t *pulse)
{
	struct source_t *source = pulse->source;

	while (source) {
		print_source(source);
		source = source->next_source;
	}
}

void pulse_init(struct pulseaudio_t *pulse, const char *clientname)
{
	pulse->mainloop = pa_mainloop_new();
	pulse->mainloop_api = pa_mainloop_get_api(pulse->mainloop);
	pulse->cxt = pa_context_new(pulse->mainloop_api, clientname);
	pulse->state = STATE_CONNECTING;
	pulse->source = NULL;
	pa_context_set_state_callback(pulse->cxt, state_cb, pulse);
}

int pulse_connect(struct pulseaudio_t *pulse)
{
	int r;
	pa_context_connect(pulse->cxt, NULL, PA_CONTEXT_NOFLAGS, NULL);

	while (pulse->state == STATE_CONNECTING)
		pa_mainloop_iterate(pulse->mainloop, 1, &r);

	if(pulse->state == STATE_ERROR) {
		r = pa_context_errno(pulse->cxt);
		fprintf(stderr, "failed to connect to pulse daemon: %s\n", pa_strerror(r));
	}
	return 0;
}

void pulse_deinit(struct pulseaudio_t *pulse)
{
	struct source_t *source = pulse->source;

	pa_context_disconnect(pulse->cxt);
	pa_mainloop_free(pulse->mainloop);

	while (source) {
		source = pulse->source->next_source;
		free(pulse->source);
		pulse->source = source;
	}
}

void clean_source_t(struct pulseaudio_t *pulse)
{
	struct source_t *source = pulse->source;

	while (source) {
		source = pulse->source->next_source;
		free(pulse->source);
		pulse->source = source;
	}
}

void get_streams(struct pulseaudio_t *pulse)
{
	pa_operation *op = pa_context_get_sink_input_info_list(pulse->cxt, stream_add_cb, pulse);
	pulse_async_wait(pulse, op);
	pa_operation_unref(op);
}

void get_stream_by_index(struct pulseaudio_t *pulse, uint32_t idx)
{
	pa_operation *op = pa_context_get_sink_input_info(pulse->cxt, idx, stream_add_cb, pulse);
	pulse_async_wait(pulse, op);
	pa_operation_unref(op);
}

void get_default_sink(struct pulseaudio_t *pulse)
{
	const char *sink_name;
	pa_operation *op = pa_context_get_server_info(pulse->cxt, sink_info_cb, &sink_name);
	pulse_async_wait(pulse, op);
	pa_operation_unref(op);

	get_sink_by_name(pulse, sink_name);
}

void get_sink_by_index(struct pulseaudio_t *pulse, uint32_t idx)
{
	pa_operation *op = pa_context_get_sink_info_by_index(pulse->cxt, idx, sink_add_cb, pulse);
	pulse_async_wait(pulse, op);
	pa_operation_unref(op);
}

void get_sink_by_name(struct pulseaudio_t *pulse, const char *name)
{
	pa_operation *op = pa_context_get_sink_info_by_name(pulse->cxt, name, sink_add_cb, pulse);
	pulse_async_wait(pulse, op);
	pa_operation_unref(op);
}

void get_sinks(struct pulseaudio_t *pulse)
{
	pa_operation *op = pa_context_get_sink_info_list(pulse->cxt, sink_add_cb, pulse);
	pulse_async_wait(pulse, op);
	pa_operation_unref(op);
}

void get_default_source(struct pulseaudio_t *pulse)
{
	const char *source_name;
	pa_operation *op = pa_context_get_server_info(pulse->cxt, source_info_cb, &source_name);
	pulse_async_wait(pulse, op);
	pa_operation_unref(op);

	get_source_by_name(pulse, source_name);
}

void get_source_by_index(struct pulseaudio_t *pulse, uint32_t idx)
{
	pa_operation *op = pa_context_get_source_info_by_index(pulse->cxt, idx, source_add_cb, pulse);
	pulse_async_wait(pulse, op);
	pa_operation_unref(op);
}

void get_source_by_name(struct pulseaudio_t *pulse, const char *name)
{
	pa_operation *op = pa_context_get_source_info_by_name(pulse->cxt, name, source_add_cb, pulse);
	pulse_async_wait(pulse, op);
	pa_operation_unref(op);
}

void get_sources(struct pulseaudio_t *pulse)
{
	pa_operation *op = pa_context_get_source_info_list(pulse->cxt, source_add_cb, pulse);
	pulse_async_wait(pulse, op);
	pa_operation_unref(op);
}

void set_default(struct pulseaudio_t *pulse)
{
	pa_operation *op = NULL;
	const char *type = NULL;

	switch (pulse->source->t) {
		case TYPE_SINK:
			op = pa_context_set_default_sink(pulse->cxt, pulse->source->name, success_cb, pulse);
			type = "input";
			break;
		case TYPE_SOURCE:
			op = pa_context_set_default_source(pulse->cxt, pulse->source->name, success_cb, pulse);
			type = "output";
			break;
		default:
			errx(EXIT_FAILURE, "error cant use application with set-default command.");
			return;
	}

	pulse_async_wait(pulse, op);
	if (!pulse->success) {
		int err = pa_context_errno(pulse->cxt);
		errx(EXIT_FAILURE, "failed to set default %s to %s: %s", type, pulse->source->name, pa_strerror(err));
	}
	pa_operation_unref(op);
}

void get_volume(struct pulseaudio_t *pulse)
{
	printf("%d\n", pulse->source->volume_percent);
}

int set_volume(struct pulseaudio_t *pulse, long v)
{
	pa_cvolume *vol = NULL;

	v = CLAMP(v, 0, 150);
	vol = pa_cvolume_set(&pulse->source->volume, pulse->source->volume.channels,(int)fmax((double)(v + .5) * PA_VOLUME_NORM / 100, 0));
	pa_operation *op = pulse->source->ops.op_vol(pulse->cxt, pulse->source->idx, vol, success_cb, pulse);
	pulse_async_wait(pulse, op);
	pa_operation_unref(op);

	if(pulse->success)
		printf("%ld\n", v);
	else {
		int err = pa_context_errno(pulse->cxt);
		errx(EXIT_FAILURE, "failed to set volume: %s", pa_strerror(err));
	}

	return !pulse->success;
}

void get_balance(struct pulseaudio_t *pulse)
{
	if(pulse->source->t != TYPE_SINK)
		errx(EXIT_FAILURE, "error can only get balance on output devices");

	printf("%.2f\n", pulse->source->balance);
}

int set_balance(struct pulseaudio_t *pulse, float b)
{
	if(pulse->source->t != TYPE_SINK)
		errx(EXIT_FAILURE, "error can only set balance on output devices");

	b = CLAMP(b, -1.0f, 1.0f);

	if(pa_channel_map_valid(pulse->source->map) != 0) {
		pa_cvolume *vol = pa_cvolume_set_balance(&pulse->source->volume, pulse->source->map, b);
		pa_operation *op = pulse->source->ops.op_vol(pulse->cxt, pulse->source->idx, vol, success_cb, pulse);
		pulse_async_wait(pulse, op);

		if(pulse->success)
			printf("%.2f\n", b);
		else {
			int err = pa_context_errno(pulse->cxt);
			errx(EXIT_FAILURE, "failed to set balance: %s", pa_strerror(err));
		}

		pa_operation_unref(op);

		return !pulse->success;
	}

	errx(EXIT_FAILURE, "cant set balance on that input device.");
	return -1;
}

int set_mute(struct pulseaudio_t *pulse, int mute)
{
	pa_operation *op = pulse->source->ops.op_mute(pulse->cxt, pulse->source->idx, mute, success_cb, pulse);
	pulse_async_wait(pulse, op);
	pa_operation_unref(op);

	if (!pulse->success) {
		int err = pa_context_errno(pulse->cxt);
		errx(EXIT_FAILURE, "failed to mute: %s", pa_strerror(err));
	}

	return !pulse->success;
}

/* vim: set noet ts=2 sw=2: */
