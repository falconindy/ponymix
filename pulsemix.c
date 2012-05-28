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

#include <pulse/pulseaudio.h>

#define UNUSED __attribute__((unused))

#define CLAMP(x, low, high) \
	__extension__ ({ \
		typeof(x) _x = (x); \
		typeof(low) _low = (low); \
		typeof(high) _high = (high); \
		((_x > _high) ? _high : ((_x < _low) ? _low : _x)); \
	})

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

enum action {
	ACTION_LIST = 0,
	ACTION_LISTSTREAMS,
	ACTION_LISTSOURCES,
	ACTION_GETVOL,
	ACTION_SETVOL,
	ACTION_GETBAL,
	ACTION_SETBAL,
	ACTION_INCREASE,
	ACTION_DECREASE,
	ACTION_MUTE,
	ACTION_UNMUTE,
	ACTION_TOGGLE,
	ACTION_INVALID
};

enum type {
	SINK = 0,
	SOURCE,
	STREAM
};

struct source_t {
	uint32_t idx;
	const char *name;
	const char *desc;
	const pa_channel_map *map;
	pa_cvolume volume;
  struct pa_proplist *proplist;
	int volume_percent;
	int mute;
	float balance;
	enum type t;

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

int xstrtol(const char *str, long *out)
{
	char *end = NULL;

	if (str == NULL || *str == '\0')
		return -1;
	errno = 0;

	*out = strtol(str, &end, 10);
	if (errno || str == end || (end && *end))
		return -1;

	return 0;
}

int xstrtof(const char *str, float *out)
{
	char *end = NULL;

	if (str == NULL || *str == '\0')
		return -1;
	errno = 0;

	*out = strtof(str, &end);
	if (errno || str == end || (end && *end))
		return -1;

	return 0;
}

static void success_cb(pa_context UNUSED *c, int success, void *raw)
{
	struct pulseaudio_t *pulse = raw;
	pulse->success = success;
}

static void pulse_async_wait(struct pulseaudio_t *pulse, pa_operation *op)
{
	while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
		pa_mainloop_iterate(pulse->mainloop, 1, NULL);
}

static void get_volume(struct pulseaudio_t *pulse)
{
	printf("%d\n", pulse->source->volume_percent);
}

static int set_volume(struct pulseaudio_t *pulse, struct source_t *source, long v)
{
	if(v > 150)
		v = 150;

	pa_cvolume *vol = pa_cvolume_set(&source->volume, source->volume.channels,
			(int)fmax((double)(v + .5) * PA_VOLUME_NORM / 100, 0));

	if(source->t == STREAM)
	{
		pa_operation *op = pa_context_set_sink_input_volume(pulse->cxt, source->idx,
				vol, success_cb, pulse);
		pulse_async_wait(pulse, op);
		pa_operation_unref(op);
	}
	else if(source->t == SINK)
	{
		pa_operation *op = pa_context_set_sink_volume_by_index(pulse->cxt, source->idx, vol, success_cb, pulse);
		pulse_async_wait(pulse, op);
		pa_operation_unref(op);
	}
	else
	{
		pa_operation *op = pa_context_set_source_volume_by_index(pulse->cxt, source->idx, vol, success_cb, pulse);
		pulse_async_wait(pulse, op);
		pa_operation_unref(op);
	}

	if(pulse->success)
		printf("%ld\n", v);
	else 
	{
		int err = pa_context_errno(pulse->cxt);
		fprintf(stderr, "failed to set volume: %s\n", pa_strerror(err));
	}

	return !pulse->success;
}

static void get_balance(struct pulseaudio_t *pulse)
{
	printf("%f\n", pulse->source->balance);
}

static int set_balance(struct pulseaudio_t *pulse, struct source_t *source, float b)
{
	if(b < -1.0f)
		b = -1.0f;
	else if(b > 1.0f)
		b = 1.0f;
	
	if(pa_channel_map_valid(source->map) != 0)
	{
		pa_cvolume *vol = pa_cvolume_set_balance(&source->volume, source->map, b);
		pa_operation *op = pa_context_set_sink_volume_by_index(pulse->cxt, source->idx, vol, success_cb, pulse);
		pulse_async_wait(pulse, op);
		
		if (pulse->success)
			printf("%f\n", b);
		else 
		{
			int err = pa_context_errno(pulse->cxt);
			fprintf(stderr, "failed to set balance: %s\n", pa_strerror(err));
		}

		pa_operation_unref(op);

		return !pulse->success;
	}
	fprintf(stderr, "cant set balance on that sound source.\n");
	return -1;
}

static int set_mute(struct pulseaudio_t *pulse, struct source_t *source, int mute)
{
	if(source->t == STREAM)
	{
		pa_operation* op = pa_context_set_sink_input_mute(pulse->cxt, source->idx, mute, success_cb, pulse);
		pulse_async_wait(pulse, op);
		pa_operation_unref(op);
	}
	else if(source->t == SINK)
	{
		pa_operation* op = pa_context_set_sink_mute_by_index(pulse->cxt, source->idx,	mute, success_cb, pulse);
		pulse_async_wait(pulse, op);
		pa_operation_unref(op);
	}
	else
	{
		pa_operation* op = pa_context_set_source_mute_by_index(pulse->cxt, source->idx, mute, success_cb, pulse);
		pulse_async_wait(pulse, op);
		pa_operation_unref(op);
	}
	
	if(pulse->success)
		printf("%d\n", mute);
	else
	{
		int err = pa_context_errno(pulse->cxt);
		fprintf(stderr, "failed to mute: %s\n", pa_strerror(err));
	}

	return !pulse->success;
}

static int unmute(struct pulseaudio_t *pulse, struct source_t *source)
{
	return set_mute(pulse, source, 0);
}

static int mute(struct pulseaudio_t *pulse, struct source_t *source)
{
	return set_mute(pulse, source, 1);
}

static struct source_t *source_new(const pa_sink_input_info *stream_info, const pa_sink_info *sink_info, const pa_source_info *source_info)
{
	if(stream_info != NULL)
	{
		struct source_t *stream = calloc(1, sizeof(struct source_t));
		stream->idx = stream_info->index;
		stream->name = stream_info->name;
		stream->proplist = stream_info->proplist;
		stream->desc = strdup(pa_proplist_gets(stream->proplist, PA_PROP_APPLICATION_NAME));
		memcpy(&stream->volume, &stream_info->volume, sizeof(pa_cvolume));
		stream->volume_percent = (int)(((double)pa_cvolume_avg(&stream->volume) * 100) / PA_VOLUME_NORM);
		stream->mute = stream_info->mute;
		stream->t = STREAM;
		return stream;
	}
	else if(sink_info != NULL)
	{
		struct source_t *sink = calloc(1, sizeof(struct source_t));

		sink->idx = sink_info->index;
		sink->name = sink_info->name;
		sink->desc = sink_info->description;
		sink->map = &sink_info->channel_map;
		memcpy(&sink->volume, &sink_info->volume, sizeof(pa_cvolume));
		sink->volume_percent = (int)(((double)pa_cvolume_avg(&sink->volume) * 100) / PA_VOLUME_NORM);
		sink->mute = sink_info->mute;
		sink->balance = pa_cvolume_get_balance(&sink_info->volume, &sink_info->channel_map);
		sink->t = SINK;
		return sink;
	}
	else
	{
		struct source_t *source = calloc(1, sizeof(struct source_t));

		source->idx = source_info->index;
		source->name = source_info->name;
		source->desc = source_info->description;
		memcpy(&source->volume, &source_info->volume, sizeof(pa_cvolume));
		source->volume_percent = (int)(((double)pa_cvolume_avg(&source->volume) * 100) / PA_VOLUME_NORM);
		source->mute = source_info->mute;
		source->t = SOURCE;
		return source;
	}
}

static void print_stream(struct source_t *stream)
{
	char *mute = NULL;
	if(stream->mute)
		mute = "true";
	else
		mute = "false";

	printf("stream %2d: %s\n  %s\n  Avg. Volume: %d%% Muted: %s\n",	stream->idx, stream->name, stream->desc, stream->volume_percent, mute);
}

static void print_sink(struct source_t *sink)
{
	char *mute = NULL;
	if(sink->mute)
		mute = "true";
	else
		mute = "false";

	printf("sink %2d: %s\n  %s\n  Avg. Volume: %d%% Muted: %s\n",	sink->idx, sink->name, sink->desc, sink->volume_percent, mute);
}

static void print_source(struct source_t *source)
{
	char *mute = NULL;
	if(source->mute)
		mute = "true";
	else
		mute = "false";

	printf("source %2d: %s\n  %s\n  Avg. Volume: %d%% Muted: %s\n", source->idx, source->name, source->desc, source->volume_percent, mute);
}

static void print_sources(struct pulseaudio_t *pulse)
{
	struct source_t *source = pulse->source;

	while(source) 
	{
		if(source->t == SINK)
			print_sink(source);
		else if(source->t == STREAM)
			print_stream(source);
		else
			print_source(source);

		source = source->next_source;
	}
}

static void sink_info_cb(pa_context UNUSED *c, const pa_server_info *i, void *raw)
{
	const char **sink_name = (const char **)raw;

	*sink_name = i->default_sink_name;
}

static void source_info_cb(pa_context UNUSED *c, const pa_server_info *i, void *raw)
{
	const char **source_name = (const char **)raw;

	*source_name = i->default_source_name;
}

static void stream_add_cb(pa_context UNUSED *c, const pa_sink_input_info *i, int eol, void *raw)
{
	struct pulseaudio_t *pulse = raw;
	struct source_t *s, *stream;

	if(eol)
		return;

	stream = source_new(i, NULL, NULL);

	if(pulse->source == NULL)
		pulse->source = stream;
	else 
	{
		s = pulse->source;
		stream->next_source = s;
		pulse->source = stream;
	}
}

static void sink_add_cb(pa_context UNUSED *c, const pa_sink_info *i, int eol,	void *raw)
{
	struct pulseaudio_t *pulse = raw;
	struct source_t *s, *sink;

	if (eol)
		return;

	sink = source_new(NULL, i, NULL);

	if (pulse->source == NULL)
		pulse->source = sink;
	else 
	{
		s = pulse->source;
		sink->next_source = s;
		pulse->source = sink;
	}
}

static void source_add_cb(pa_context UNUSED *c, const pa_source_info *i, int eol, void *raw)
{
	struct pulseaudio_t *pulse = raw;
	struct source_t *s, *source;

	if(eol)
		return;

	source = source_new(NULL, NULL, i);

	if(pulse->source == NULL)
		pulse->source = source;
	else
	{
		s = pulse->source;
		source->next_source = s;
		pulse->source = source;
	}
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

static void get_streams(struct pulseaudio_t *pulse)
{
	pa_operation *op = pa_context_get_sink_input_info_list(pulse->cxt, stream_add_cb, pulse);
	pulse_async_wait(pulse, op);
	pa_operation_unref(op);
}

static void get_stream_by_index(struct pulseaudio_t *pulse, uint32_t idx)
{
	pa_operation *op = pa_context_get_sink_input_info(pulse->cxt, idx, stream_add_cb, pulse);
	pulse_async_wait(pulse, op);
	pa_operation_unref(op);
}

static void get_sinks(struct pulseaudio_t *pulse)
{
	pa_operation *op = pa_context_get_sink_info_list(pulse->cxt, sink_add_cb, pulse);
	pulse_async_wait(pulse, op);
	pa_operation_unref(op);
}

static void get_sink_by_name(struct pulseaudio_t *pulse, const char *name)
{
	pa_operation *op = pa_context_get_sink_info_by_name(pulse->cxt, name, sink_add_cb, pulse);
	pulse_async_wait(pulse, op);
	pa_operation_unref(op);
}

static void get_sources(struct pulseaudio_t *pulse)
{
	pa_operation *op = pa_context_get_source_info_list(pulse->cxt, source_add_cb, pulse);
	pulse_async_wait(pulse, op);
	pa_operation_unref(op);
}

static void get_source_by_name(struct pulseaudio_t *pulse, const char *name)
{
	pa_operation *op = pa_context_get_source_info_by_name(pulse->cxt, name, source_add_cb, pulse);
	pulse_async_wait(pulse, op);
	pa_operation_unref(op);
}

static void get_default_sink(struct pulseaudio_t *pulse)
{
	const char *sink_name;
	pa_operation *op = pa_context_get_server_info(pulse->cxt, sink_info_cb, &sink_name);
	pulse_async_wait(pulse, op);
	pa_operation_unref(op);

	get_sink_by_name(pulse, sink_name);
}

static void get_default_source(struct pulseaudio_t *pulse)
{
	const char *source_name;
	pa_operation *op = pa_context_get_server_info(pulse->cxt, source_info_cb, &source_name);
	pulse_async_wait(pulse, op);
	pa_operation_unref(op);
	
	get_source_by_name(pulse, source_name);
}

static void pulse_deinit(struct pulseaudio_t *pulse)
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

static void pulse_init(struct pulseaudio_t *pulse, const char *clientname)
{
	/* allocate */
	pulse->mainloop = pa_mainloop_new();
	pulse->mainloop_api = pa_mainloop_get_api(pulse->mainloop);
	pulse->cxt = pa_context_new(pulse->mainloop_api, clientname);
	pulse->state = STATE_CONNECTING;
	pulse->source = NULL;

	/* set state callback for connection */
	pa_context_set_state_callback(pulse->cxt, state_cb, pulse);
}

static int pulse_connect(struct pulseaudio_t *pulse)
{
	int r;

	pa_context_connect(pulse->cxt, NULL, PA_CONTEXT_NOFLAGS, NULL);
	while (pulse->state == STATE_CONNECTING)
		pa_mainloop_iterate(pulse->mainloop, 1, &r);

	if (pulse->state == STATE_ERROR) {
		r = pa_context_errno(pulse->cxt);
		fprintf(stderr, "failed to connect to pulse daemon: %s\n",
				pa_strerror(r));
		return 1;
	}

	return 0;
}

void usage(FILE *out)
{
	fprintf(out, "usage: %s [options] <command>...\n", program_invocation_short_name);
	fputs("\nOptions:\n", out);
	fputs(" -h, --help,          display this help and exit\n", out);
	fputs(" -s, --stream <index> control a stream instead of the sink itself\n", out);
	fputs(" -o, --source         control the default set source/input device\n", out);

	fputs("\nCommands:\n", out);
	fputs("  list               list available sinks/output devices\n", out);
	fputs("  list-sources       list available sources/input devices\n", out);
	fputs("  list-streams       list available streams/applications\n", out);
	fputs("  get-volume         get volume\n", out);
	fputs("  set-volume VALUE   set volume\n", out);
	fputs("  get-balance        get balance for sink\n", out);
	fputs("  set-balance VALUE  set balance for sink, pass double -- before the negative number ex( -- -0.5) \n", out);
	fputs("                     range is between -1.0 to 1.0 and 0 being centered\n", out);
	fputs("  increase VALUE     increase volume\n", out);
	fputs("  decrease VALUE     decrease volume\n", out);
	fputs("  mute               mute active sink or stream\n", out);
	fputs("  unmute             unmute active sink or stream\n", out);
	fputs("  toggle             toggle mute\n", out);

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

enum action string_to_verb(const char *string)
{
	if (strcmp(string, "list") == 0)
		return ACTION_LIST;
	else if (strcmp(string, "list-streams") == 0)
		return ACTION_LISTSTREAMS;
	else if (strcmp(string, "list-sources") == 0)
		return ACTION_LISTSOURCES;
	else if (strcmp(string, "get-volume") == 0)
		return ACTION_GETVOL;
	else if (strcmp(string, "set-volume") == 0)
		return ACTION_SETVOL;
	else if (strcmp(string, "get-balance") == 0)
		return ACTION_GETBAL;
	else if (strcmp(string, "set-balance") == 0)
		return ACTION_SETBAL;
	else if (strcmp(string, "increase") == 0)
		return ACTION_INCREASE;
	else if (strcmp(string, "decrease") == 0)
		return ACTION_DECREASE;
	else if (strcmp(string, "mute") == 0)
		return ACTION_MUTE;
	else if (strcmp(string, "unmute") == 0)
		return ACTION_UNMUTE;
	else if (strcmp(string, "toggle") == 0)
		return ACTION_TOGGLE;

	return ACTION_INVALID;
}

int main(int argc, char *argv[])
{
	struct pulseaudio_t pulse;
	enum action verb;
	char *stream = NULL;
	char *source = NULL;
	union arg_t value;
	int rc = 0;
	
	static const struct option opts[] = 
	{
		{ "help", no_argument, 0, 'h' },
		{ "stream", required_argument, 0, 's'},
		{ "source", no_argument, 0, 'o'},
		{ 0, 0, 0, 0 },
	};

	for (;;) 
	{
		int opt = getopt_long(argc, argv, "hs:o", opts, NULL);
		if (opt == -1)
			break;

		switch (opt) 
		{
		case 'h':
			usage(stdout);
		break;
		case 's':
			stream = optarg;
		break;
		case 'o':
			source = "true";
		break;
		default:
			exit(1);
		break;
		}
	}
	
	verb = (optind == argc) ? ACTION_LIST : string_to_verb(argv[optind]);
	if (verb == ACTION_INVALID)
		errx(EXIT_FAILURE, "unknown action: %s", argv[optind]);
		
	optind++;

	if (verb == ACTION_SETVOL || verb == ACTION_SETBAL || verb == ACTION_INCREASE || verb == ACTION_DECREASE)
	{
		if (optind == argc)
			errx(EXIT_FAILURE, "missing value for action '%s'", argv[optind - 1]);
		else 
		{
			/* validate to number */
			int r = 0;
			if(verb == ACTION_SETBAL)
				r = xstrtof(argv[optind], &value.f);
			else
				r = xstrtol(argv[optind], &value.l);
			
			if (r < 0)
				errx(EXIT_FAILURE, "invalid number: %s", argv[optind]);
		}
	}
	/* initialize connection */
	pulse_init(&pulse, "pulsemix");
	if (pulse_connect(&pulse) != 0)
		return 1;

	if (verb == ACTION_LIST) 
	{
		get_sinks(&pulse);
		print_sources(&pulse);
	}
	else if(verb == ACTION_LISTSTREAMS) 
	{
		get_streams(&pulse);
		print_sources(&pulse);
	}
	else if(verb == ACTION_LISTSOURCES)
	{
		get_sources(&pulse);
		print_sources(&pulse);
	}
	else 
	{
		if(stream)
		{
			long idx = 0;
			int r = xstrtol(stream, &idx);
			if (r < 0)
				errx(EXIT_FAILURE, "invalid number: %s", argv[optind]);
			else
				get_stream_by_index(&pulse, (uint32_t)idx);

			if(pulse.source == NULL)
				errx(EXIT_FAILURE, "stream not found: %s", stream ? stream : "default");
		}
		else if(source)
		{
			get_default_source(&pulse);
		}
		else
		{
			get_default_sink(&pulse);
		}

		switch (verb) 
		{
		case ACTION_GETVOL:
				get_volume(&pulse);
			break;
		case ACTION_SETVOL:
				rc = set_volume(&pulse, pulse.source, value.l);
			break;
		case ACTION_GETBAL:
			if(stream || source)
				fprintf(stderr, "balance is not supported for streams\n");
			else
				get_balance(&pulse);
			break;
		case ACTION_SETBAL:
			if(stream || source)
				fprintf(stderr, "balance is not supported for streams\n");
			else
				rc = set_balance(&pulse, pulse.source, value.f);
			break;
		case ACTION_INCREASE:
				rc = set_volume(&pulse, pulse.source, CLAMP(pulse.source->volume_percent + value.l, 0, 150));
			break;
		case ACTION_DECREASE:
				rc = set_volume(&pulse, pulse.source, CLAMP(pulse.source->volume_percent - value.l, 0, 150));
			break;
		case ACTION_MUTE:
				rc = mute(&pulse, pulse.source);
			break;
		case ACTION_UNMUTE:
				rc = unmute(&pulse, pulse.source);
			break;
		case ACTION_TOGGLE:
				rc = set_mute(&pulse, pulse.source, !pulse.source->mute);
			break;
		default:
			break;
		}
	}

	/* shut down */
	pulse_deinit(&pulse);

	return rc;
}

/* vim: set noet ts=2 sw=2: */
