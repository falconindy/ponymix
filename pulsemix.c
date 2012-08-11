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
};

enum connectstate {
	STATE_CONNECTING = 0,
	STATE_CONNECTED,
	STATE_ERROR
};

enum action {
	ACTION_LIST = 0,
	ACTION_GETVOL,
	ACTION_SETVOL,
	ACTION_INCREASE,
	ACTION_DECREASE,
	ACTION_MUTE,
	ACTION_UNMUTE,
	ACTION_TOGGLE,
	ACTION_SETSINK,
	ACTION_INVALID
};

enum type {
	TYPE_SINK,
	TYPE_SOURCE
};

struct io_t {
	enum type type;
	uint32_t idx;
	char *name;
	char *desc;
	const char *pp_name;
	pa_cvolume volume;
	int volume_percent;
	int mute;

	pa_operation *(*fn_mute)(pa_context *, uint32_t, int, pa_context_success_cb_t, void *);
	pa_operation *(*fn_setvol)(pa_context *, uint32_t, const pa_cvolume *, pa_context_success_cb_t, void *);

	struct io_t *next;
};

struct pulseaudio_t {
	pa_context *cxt;
	pa_mainloop *mainloop;
	pa_mainloop_api *mainloop_api;
	enum connectstate state;
	int success;

	struct io_t *head;
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

static void populate(struct io_t *node)
{
	node->volume_percent = (int)(((double)pa_cvolume_avg(&node->volume) * 100)
			/ PA_VOLUME_NORM);
}

static struct io_t *sink_new(const pa_sink_info *info)
{
	struct io_t *sink = calloc(1, sizeof(struct io_t));

	sink->type = TYPE_SINK;
	sink->idx = info->index;
	sink->name = strdup(info->name);
	sink->desc = strdup(info->description);
	sink->pp_name = "sink";
	memcpy(&sink->volume, &info->volume, sizeof(pa_cvolume));
	sink->mute = info->mute;

	sink->fn_mute = pa_context_set_sink_mute_by_index;
	sink->fn_setvol = pa_context_set_sink_volume_by_index;

	populate(sink);
	return sink;
}

static struct io_t *source_new(const pa_source_info *info)
{
	struct io_t *source = calloc(1, sizeof(struct io_t));

	source->type = TYPE_SOURCE;
	source->idx = info->index;
	source->name = strdup(info->name);
	source->desc = strdup(info->description);
	source->pp_name = "source";
	memcpy(&source->volume, &info->volume, sizeof(pa_cvolume));
	source->mute = info->mute;

	source->fn_mute = pa_context_set_source_mute_by_index;
	source->fn_setvol = pa_context_set_source_volume_by_index;

	populate(source);
	return source;
}

static void sink_add_cb(pa_context UNUSED *c, const pa_sink_info *i, int eol,
		void *raw)
{
	struct pulseaudio_t *pulse = raw;
	struct io_t *sink;

	if (eol)
		return;

	sink = sink_new(i);
	sink->next = pulse->head;
	pulse->head = sink;
}

static void source_add_cb(pa_context UNUSED *c, const pa_source_info *i, int eol, void *raw)
{
	struct pulseaudio_t *pulse = raw;
	struct io_t *source;

	if (eol)
		return;

	source = source_new(i);
	source->next = pulse->head;
	pulse->head = source;
}

static void server_info_cb(pa_context UNUSED *c, const pa_server_info *i,
		void *raw)
{
	const char **sink_name = (const char **)raw;

	*sink_name = i->default_sink_name;
}

static void source_info_cb(pa_context UNUSED *c, const pa_server_info *i, void *raw)
{
	const char **source_name = (const char **)raw;

	*source_name = i->default_source_name;
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
	printf("%d\n", pulse->head->volume_percent);
}

static int set_volume(struct pulseaudio_t *pulse, struct io_t *dev, long v)
{
	pa_cvolume *vol = pa_cvolume_set(&dev->volume, dev->volume.channels,
			(int)fmax((double)(v + .5) * PA_VOLUME_NORM / 100, 0));
	pa_operation *op = pulse->head->fn_setvol(pulse->cxt, dev->idx, vol,
			success_cb, pulse);
	pulse_async_wait(pulse, op);

	if (pulse->success)
		printf("%ld\n", v);
	else {
		int err = pa_context_errno(pulse->cxt);
		fprintf(stderr, "failed to set volume: %s\n", pa_strerror(err));
	}

	pa_operation_unref(op);

	return !pulse->success;
}

static int set_mute(struct pulseaudio_t *pulse, struct io_t *dev, int mute)
{
	pa_operation* op = pulse->head->fn_mute(pulse->cxt, dev->idx, mute,
			success_cb, pulse);
	pulse_async_wait(pulse, op);

	if (!pulse->success) {
		int err = pa_context_errno(pulse->cxt);
		fprintf(stderr, "failed to mute device: %s\n", pa_strerror(err));
	}

	pa_operation_unref(op);

	return !pulse->success;
}

static int unmute(struct pulseaudio_t *pulse, struct io_t *dev)
{
	return set_mute(pulse, dev, 0);
}

static int mute(struct pulseaudio_t *pulse, struct io_t *dev)
{
	return set_mute(pulse, dev, 1);
}

static void print(struct io_t *dev)
{
	printf("%s %2d: %s\n  %s\n  Avg. Volume: %d%%\n", dev->pp_name,
			dev->idx, dev->name, dev->desc, dev->volume_percent);
}

static void print_all(struct pulseaudio_t *pulse)
{
	struct io_t *dev = pulse->head;

	while (dev) {
		print(dev);
		dev = dev->next;
	}
}

static void get_sinks(struct pulseaudio_t *pulse)
{
	pa_operation *op = pa_context_get_sink_info_list(pulse->cxt,
			sink_add_cb, pulse);
	pulse_async_wait(pulse, op);
	pa_operation_unref(op);
}

static void get_sink_by_name(struct pulseaudio_t *pulse, const char *name)
{
	pa_operation *op = pa_context_get_sink_info_by_name(pulse->cxt, name,
			sink_add_cb, pulse);
	pulse_async_wait(pulse, op);
	pa_operation_unref(op);
}

static void get_default_sink(struct pulseaudio_t *pulse)
{
	const char *sink_name;
	pa_operation *op = pa_context_get_server_info(pulse->cxt, server_info_cb,
			&sink_name);
	pulse_async_wait(pulse, op);
	pa_operation_unref(op);

	get_sink_by_name(pulse, sink_name);
}

static void get_source_by_name(struct pulseaudio_t *pulse, const char *name)
{
	pa_operation *op = pa_context_get_source_info_by_name(pulse->cxt, name,
			source_add_cb, pulse);
	pulse_async_wait(pulse, op);
	pa_operation_unref(op);
}

static void get_default_source(struct pulseaudio_t *pulse)
{
	const char *source_name;
	pa_operation *op = pa_context_get_server_info(pulse->cxt, source_info_cb,
			&source_name);
	pulse_async_wait(pulse, op);
	pa_operation_unref(op);

	get_source_by_name(pulse, source_name);
}

static int set_default_sink(struct pulseaudio_t *pulse, const char *sinkname)
{
	pa_operation *op;

	get_sink_by_name(pulse, sinkname);
	if (pulse->head == NULL) {
		warnx("failed to get sink by name\n");
		return 1;
	}

	op = pa_context_set_default_sink(pulse->cxt, sinkname, success_cb, pulse);
	pulse_async_wait(pulse, op);

	if (!pulse->success) {
		int err = pa_context_errno(pulse->cxt);
		fprintf(stderr, "failed to set default sink to %s: %s\n", sinkname, pa_strerror(err));
	}

	pa_operation_unref(op);

	return !pulse->success;
}

static void pulse_init(struct pulseaudio_t *pulse, const char *clientname)
{
	/* allocate */
	pulse->mainloop = pa_mainloop_new();
	pulse->mainloop_api = pa_mainloop_get_api(pulse->mainloop);
	pulse->cxt = pa_context_new(pulse->mainloop_api, clientname);
	pulse->state = STATE_CONNECTING;
	pulse->head = NULL;

	/* set state callback for connection */
	pa_context_set_state_callback(pulse->cxt, state_cb, pulse);
}

static void pulse_deinit(struct pulseaudio_t *pulse)
{
	struct io_t *node = pulse->head;

	pa_context_disconnect(pulse->cxt);
	pa_mainloop_free(pulse->mainloop);

	while (node) {
		node = pulse->head->next;
		free(pulse->head->name);
		free(pulse->head->desc);
		free(pulse->head);
		pulse->head = node;
	}
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
	fputs(" -o, --sink <name>    control a sink other than the default\n", out);
	fputs(" -i, --source <name>  control a source\n", out);

	fputs("\nCommands:\n", out);
	fputs("  list                list available sinks\n", out);
	fputs("  get-volume          get volume for sink\n", out);
	fputs("  set-volume VALUE    set volume for sink\n", out);
	fputs("  increase VALUE      increase volume\n", out);
	fputs("  decrease VALUE      decrease volume\n", out);
	fputs("  mute                mute active sink\n", out);
	fputs("  unmute              unmute active sink\n", out);
	fputs("  toggle              toggle mute\n", out);
	fputs("  set-sink SINKNAME   set default sink\n", out);

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

enum action string_to_verb(const char *string)
{
	if (strcmp(string, "list") == 0)
		return ACTION_LIST;
	else if (strcmp(string, "get-volume") == 0)
		return ACTION_GETVOL;
	else if (strcmp(string, "set-volume") == 0)
		return ACTION_SETVOL;
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
	else if (strcmp(string, "set-sink") == 0)
		return ACTION_SETSINK;

	return ACTION_INVALID;
}

int main(int argc, char *argv[])
{
	struct pulseaudio_t pulse;
	enum action verb;
	char *id = NULL;
	union arg_t value;
	int rc = 0;

	const char *pp_name = "sink";
	void (*fn_get_default)(struct pulseaudio_t *) = get_default_sink;
	void (*fn_get_by_name)(struct pulseaudio_t *, const char*) = get_sink_by_name;

	static const struct option opts[] = {
		{ "help", no_argument, 0, 'h' },
		{ "sink", optional_argument, 0, 'o' },
		{ "source", optional_argument, 0, 'i' },
		{ 0, 0, 0, 0 },
	};

	for (;;) {
		int opt = getopt_long(argc, argv, "ho:i:", opts, NULL);
		if (opt == -1)
			break;

		switch (opt) {
		case 'h':
			usage(stdout);
		case 'o':
			id = optarg;
			fn_get_default = get_default_sink;
			fn_get_by_name = get_sink_by_name;
			pp_name = "sink";
			break;
		case 'i':
			id = optarg;
			fn_get_default = get_default_source;
			fn_get_by_name = get_source_by_name;
			pp_name = "source";
			break;
		default:
			exit(1);
		}
	}

	/* string -> enum */
	verb = (optind == argc) ? ACTION_LIST : string_to_verb(argv[optind]);
	if (verb == ACTION_INVALID)
		errx(EXIT_FAILURE, "unknown action: %s", argv[optind]);

	optind++;
	if (verb == ACTION_SETVOL ||
			verb == ACTION_INCREASE ||
			verb == ACTION_DECREASE)
		if (optind == argc)
			errx(EXIT_FAILURE, "missing value for action '%s'", argv[optind - 1]);
		else {
			/* validate to number */
			int r = xstrtol(argv[optind], &value.l);
			if (r < 0)
				errx(EXIT_FAILURE, "invalid number: %s", argv[optind]);
		}
	else if (verb == ACTION_SETSINK) {
		if (optind == argc)
			errx(EXIT_FAILURE, "missing value for action '%s'", argv[optind - 1]);
		else
			value.c = argv[optind];
	}

	/* initialize connection */
	pulse_init(&pulse, "lolpulse");
	if (pulse_connect(&pulse) != 0)
		return 1;

	if (verb == ACTION_LIST) {
		get_sinks(&pulse);
		print_all(&pulse);
	} else {
		/* determine sink */
		if (id && fn_get_by_name)
			fn_get_by_name(&pulse, id);
		else if (fn_get_default)
			fn_get_default(&pulse);

		if (pulse.head == NULL)
			errx(EXIT_FAILURE, "%s not found: %s", pp_name, id ? id : "default");

		switch (verb) {
		case ACTION_GETVOL:
			get_volume(&pulse);
			break;
		case ACTION_SETVOL:
			rc = set_volume(&pulse, pulse.head, value.l);
			break;
		case ACTION_INCREASE:
			rc = set_volume(&pulse, pulse.head,
					CLAMP(pulse.head->volume_percent + value.l, 0, 150));
			break;
		case ACTION_DECREASE:
			rc = set_volume(&pulse, pulse.head,
					CLAMP(pulse.head->volume_percent - value.l, 0, 150));
			break;
		case ACTION_MUTE:
			rc = mute(&pulse, pulse.head);
			break;
		case ACTION_UNMUTE:
			rc = unmute(&pulse, pulse.head);
			break;
		case ACTION_TOGGLE:
			rc = set_mute(&pulse, pulse.head, !pulse.head->mute);
			break;
		case ACTION_SETSINK:
			rc = set_default_sink(&pulse, value.c);
		default:
			break;
		}
	}

	/* shut down */
	pulse_deinit(&pulse);

	return rc;
}

/* vim: set noet ts=2 sw=2: */
