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

enum connectstate {
	STATE_CONNECTING = 0,
	STATE_CONNECTED,
	STATE_ERROR
};

enum mode {
	MODE_DEVICE = 0,
	MODE_APP,
	MODE_INVALID
};

enum action {
	ACTION_DEFAULTS = 0,
	ACTION_LIST,
	ACTION_GETVOL,
	ACTION_SETVOL,
	ACTION_GETBAL,
	ACTION_SETBAL,
	ACTION_INCREASE,
	ACTION_DECREASE,
	ACTION_MUTE,
	ACTION_UNMUTE,
	ACTION_TOGGLE,
	ACTION_ISMUTED,
	ACTION_SETDEFAULT,
	ACTION_INVALID
};

struct io_t {
	uint32_t idx;
	char *name;
	char *desc;
	const char *pp_name;
	pa_cvolume volume;
	pa_channel_map channels;
	int volume_percent;
	int balance;
	int mute;

	pa_operation *(*fn_mute)(pa_context *, uint32_t, int, pa_context_success_cb_t, void *);
	pa_operation *(*fn_setvol)(pa_context *, uint32_t, const pa_cvolume *, pa_context_success_cb_t, void *);
	pa_operation *(*fn_setdefault)(pa_context *, const char *, pa_context_success_cb_t, void *);

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

static int xstrtol(const char *str, long *out)
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
	node->balance = (int)((double)pa_cvolume_get_balance(&node->volume,
			&node->channels) * 100);
}

static struct io_t *sink_new(const pa_sink_info *info)
{
	struct io_t *sink = calloc(1, sizeof(struct io_t));

	sink->idx = info->index;
	sink->name = strdup(info->name);
	sink->desc = strdup(info->description);
	sink->pp_name = "sink";
	memcpy(&sink->volume, &info->volume, sizeof(pa_cvolume));
	memcpy(&sink->channels, &info->channel_map, sizeof(pa_channel_map));
	sink->mute = info->mute;

	sink->fn_mute = pa_context_set_sink_mute_by_index;
	sink->fn_setvol = pa_context_set_sink_volume_by_index;
	sink->fn_setdefault = pa_context_set_default_sink;

	populate(sink);
	return sink;
}

static struct io_t *sink_input_new(const pa_sink_input_info *info)
{
	struct io_t *sink = calloc(1, sizeof(struct io_t));

	sink->idx = info->index;
	sink->name = strdup(info->name);
	sink->desc = strdup(pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_NAME));
	sink->pp_name = "sink";
	memcpy(&sink->volume, &info->volume, sizeof(pa_cvolume));
	memcpy(&sink->channels, &info->channel_map, sizeof(pa_channel_map));
	sink->mute = info->mute;

	sink->fn_mute = pa_context_set_sink_input_mute;
	sink->fn_setvol = pa_context_set_sink_input_volume;
	sink->fn_setdefault = NULL;

	populate(sink);
	return sink;
}

static struct io_t *source_new(const pa_source_info *info)
{
	struct io_t *source = calloc(1, sizeof(struct io_t));

	source->idx = info->index;
	source->name = strdup(info->name);
	source->desc = strdup(info->description);
	source->pp_name = "source";
	memcpy(&source->volume, &info->volume, sizeof(pa_cvolume));
	memcpy(&source->channels, &info->channel_map, sizeof(pa_channel_map));
	source->mute = info->mute;

	source->fn_mute = pa_context_set_source_mute_by_index;
	source->fn_setvol = pa_context_set_source_volume_by_index;
	source->fn_setdefault = pa_context_set_default_source;

	populate(source);
	return source;
}

static struct io_t *source_output_new(const pa_source_output_info *info)
{
	struct io_t *source = calloc(1, sizeof(struct io_t));

	source->idx = info->index;
	source->name = strdup(info->name);
	source->desc = strdup(pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_NAME));
	source->pp_name = "source";
	memcpy(&source->volume, &info->volume, sizeof(pa_cvolume));
	memcpy(&source->channels, &info->channel_map, sizeof(pa_channel_map));
	source->mute = info->mute;

	source->fn_mute = pa_context_set_source_output_mute;
	source->fn_setvol = pa_context_set_source_output_volume;
	source->fn_setdefault = NULL;

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

static void sink_input_add_cb(pa_context UNUSED *c, const pa_sink_input_info *i, int eol,
		void *raw)
{
	struct pulseaudio_t *pulse = raw;
	struct io_t *sink;

	if (eol)
		return;

	sink = sink_input_new(i);
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

static void source_output_add_cb(pa_context UNUSED *c, const pa_source_output_info *i, int eol,
		void *raw)
{
	struct pulseaudio_t *pulse = raw;
	struct io_t *source;

	if (eol)
		return;

	source = source_output_new(i);
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

static int set_balance(struct pulseaudio_t *pulse, struct io_t *dev, long v)
{
	pa_cvolume *vol;
	pa_operation *op;

	if (pa_channel_map_valid(&dev->channels) == 0)
		errx(EXIT_FAILURE, "can't set balance on that device.");

	vol = pa_cvolume_set_balance(&dev->volume, &dev->channels, v / 100.0);
	op = dev->fn_setvol(pulse->cxt, dev->idx, vol, success_cb, pulse);
	pulse_async_wait(pulse, op);

	if (pulse->success)
		printf("%ld\n", v);
	else {
		int err = pa_context_errno(pulse->cxt);
		errx(EXIT_FAILURE, "failed to set balance: %s", pa_strerror(err));
	}

	pa_operation_unref(op);

	return !pulse->success;
}

static int set_mute(struct pulseaudio_t *pulse, struct io_t *dev, int mute)
{
	pa_operation* op;

	/* new effective volume */
	printf("%d\n", mute ? 0 : dev->volume_percent);

	op = pulse->head->fn_mute(pulse->cxt, dev->idx, mute,
			success_cb, pulse);

	pulse_async_wait(pulse, op);

	if (!pulse->success) {
		int err = pa_context_errno(pulse->cxt);
		fprintf(stderr, "failed to mute device: %s\n", pa_strerror(err));
	}

	pa_operation_unref(op);

	return !pulse->success;
}

static void print(struct io_t *dev)
{
	printf("%s %2d: %s\n  %s\n  Avg. Volume: %d%% %s\n", dev->pp_name,
			dev->idx, dev->name, dev->desc, dev->volume_percent,
			dev->mute ? "[Muted]" : "");
}

static void print_all(struct pulseaudio_t *pulse)
{
	struct io_t *dev = pulse->head;

	while (dev) {
		print(dev);
		dev = dev->next;
	}
}

static void get_sinks(struct pulseaudio_t *pulse, enum mode mode)
{
	pa_operation *op;

	if (mode == MODE_APP)
		op = pa_context_get_sink_input_info_list(pulse->cxt, sink_input_add_cb, pulse);
	else
		op = pa_context_get_sink_info_list(pulse->cxt, sink_add_cb, pulse);

	pulse_async_wait(pulse, op);
	pa_operation_unref(op);
}

static void get_sink_by_name(struct pulseaudio_t *pulse, const char *name, enum mode mode)
{
	pa_operation *op;

	if (mode == MODE_APP) {
		long id;
		int r = xstrtol(name, &id);
		if (r < 0)
			errx(EXIT_FAILURE, "application sink not id: %s", name);
		op = pa_context_get_sink_input_info(pulse->cxt, (uint32_t)id, sink_input_add_cb, pulse);
	} else
		op = pa_context_get_sink_info_by_name(pulse->cxt, name, sink_add_cb, pulse);

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

	get_sink_by_name(pulse, sink_name, MODE_DEVICE);
}

static void get_sources(struct pulseaudio_t *pulse, enum mode mode)
{
	pa_operation *op;

	if (mode == MODE_APP)
		op = pa_context_get_source_output_info_list(pulse->cxt, source_output_add_cb, pulse);
	else
		op = pa_context_get_source_info_list(pulse->cxt, source_add_cb, pulse);

	pulse_async_wait(pulse, op);
	pa_operation_unref(op);
}

static void get_source_by_name(struct pulseaudio_t *pulse, const char *name, enum mode mode)
{
	pa_operation *op;

	if (mode == MODE_APP) {
		long id;
		int r = xstrtol(name, &id);
		if (r < 0)
			errx(EXIT_FAILURE, "application source not id: %s", name);
		op = pa_context_get_source_output_info(pulse->cxt, (uint32_t)id, source_output_add_cb, pulse);
	} else
		op = pa_context_get_source_info_by_name(pulse->cxt, name, source_add_cb, pulse);

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

	get_source_by_name(pulse, source_name, MODE_DEVICE);
}

static int set_default(struct pulseaudio_t *pulse, struct io_t *dev)
{
	pa_operation *op;

	if (dev->fn_setdefault == NULL)
		errx(EXIT_FAILURE, "valid operation only for devices");

	op = dev->fn_setdefault(pulse->cxt, dev->name, success_cb, pulse);
	pulse_async_wait(pulse, op);

	if (!pulse->success) {
		int err = pa_context_errno(pulse->cxt);
		fprintf(stderr, "failed to set default %s to %s: %s\n", dev->pp_name,
				dev->name, pa_strerror(err));
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
	pa_context_connect(pulse->cxt, NULL, PA_CONTEXT_NOFLAGS, NULL);
	while (pulse->state == STATE_CONNECTING)
		pa_mainloop_iterate(pulse->mainloop, 1, NULL);

	if (pulse->state == STATE_ERROR) {
		int r = pa_context_errno(pulse->cxt);
		fprintf(stderr, "failed to connect to pulse daemon: %s\n",
				pa_strerror(r));
		return 1;
	}

	return 0;
}

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fprintf(out, "usage: %s [options] <command>...\n", program_invocation_short_name);
	fputs("\nOptions:\n", out);
	fputs(" -h, --help           display this help and exit\n", out);
	fputs(" -d, --device         set device mode\n", out);
	fputs(" -a, --app            set application mode\n", out);
	fputs(" -o, --sink=<name>    control a sink other than the default\n", out);
	fputs(" -i, --source=<name>  control a source\n", out);

	/* TODO: add kill */
	fputs("\nCommands:\n", out);
	fputs("  defaults            list default devices\n", out);
	fputs("  list                list available devices\n", out);
	fputs("  get-volume          get volume for device\n", out);
	fputs("  set-volume VALUE    set volume for device\n", out);
	fputs("  get-balance         get balance for device\n", out);
	fputs("  set-balance VALUE   set balance for device\n", out);
	fputs("  increase VALUE      increase volume\n", out);
	fputs("  decrease VALUE      decrease volume\n", out);
	fputs("  mute                mute device\n", out);
	fputs("  unmute              unmute device\n", out);
	fputs("  toggle              toggle mute\n", out);
	fputs("  is-muted            check if muted\n", out);
	fputs("  set-default NAME    set default device\n", out);

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

static enum action string_to_verb(const char *string)
{
	if (strcmp(string, "defaults") == 0)
		return ACTION_DEFAULTS;
	else if (strcmp(string, "list") == 0)
		return ACTION_LIST;
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
	else if (strcmp(string, "is-muted") == 0)
		return ACTION_ISMUTED;
	else if (strcmp(string, "set-default") == 0)
		return ACTION_SETDEFAULT;

	return ACTION_INVALID;
}

int main(int argc, char *argv[])
{
	struct pulseaudio_t pulse;
	enum action verb;
	char *id = NULL;
	long value = 0;
	enum mode mode = MODE_DEVICE;
	int rc = 0;

	const char *pp_name = "sink";
	void (*fn_get_default)(struct pulseaudio_t *) = get_default_sink;
	void (*fn_get_by_name)(struct pulseaudio_t *, const char *, enum mode) = get_sink_by_name;

	static const struct option opts[] = {
		{ "help", no_argument, 0, 'h' },
		{ "device", no_argument, 0, 'd' },
		{ "app", no_argument, 0, 'a' },
		{ "sink", optional_argument, 0, 'o' },
		{ "source", optional_argument, 0, 'i' },
		{ 0, 0, 0, 0 },
	};

	for (;;) {
		int opt = getopt_long(argc, argv, "hao:i:", opts, NULL);
		if (opt == -1)
			break;

		switch (opt) {
		case 'h':
			usage(stdout);
		case 'd':
			mode = MODE_DEVICE;
			break;
		case 'a':
			mode = MODE_APP;
			break;
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
	verb = (optind == argc) ? ACTION_DEFAULTS : string_to_verb(argv[optind]);
	if (verb == ACTION_INVALID)
		errx(EXIT_FAILURE, "unknown action: %s", argv[optind]);

	optind++;
	if (verb == ACTION_SETVOL ||
			verb == ACTION_SETBAL ||
			verb == ACTION_INCREASE ||
			verb == ACTION_DECREASE)
		if (optind == argc)
			errx(EXIT_FAILURE, "missing value for action '%s'", argv[optind - 1]);
		else {
			/* validate to number */
			int r = xstrtol(argv[optind], &value);
			if (r < 0)
				errx(EXIT_FAILURE, "invalid number: %s", argv[optind]);
		}
	else if (verb == ACTION_SETDEFAULT) {
		if (optind == argc)
			errx(EXIT_FAILURE, "missing value for action '%s'", argv[optind - 1]);
		else
			id = argv[optind];
	}

	/* initialize connection */
	pulse_init(&pulse, "lolpulse");
	if (pulse_connect(&pulse) != 0)
		return 1;

	if (verb == ACTION_DEFAULTS) {
		get_default_sink(&pulse);
		get_default_source(&pulse);
		print_all(&pulse);
	} else if (verb == ACTION_LIST) {
		get_sinks(&pulse, mode);
		get_sources(&pulse, mode);
		print_all(&pulse);
	} else {
		/* determine sink */
		if (id && fn_get_by_name)
			fn_get_by_name(&pulse, id, mode);
		else if (!mode && verb != ACTION_SETDEFAULT && fn_get_default)
			fn_get_default(&pulse);

		if (pulse.head == NULL) {
			if (mode && !id)
				errx(EXIT_FAILURE, "%s id not set, no default operations", pp_name);
			else
				errx(EXIT_FAILURE, "%s not found: %s", pp_name, id ? id : "default");
		}

		switch (verb) {
		case ACTION_GETVOL:
			printf("%d\n", pulse.head->volume_percent);
			break;
		case ACTION_SETVOL:
			rc = set_volume(&pulse, pulse.head, value);
			break;
		case ACTION_GETBAL:
			printf("%d\n", pulse.head->balance);
			break;
		case ACTION_SETBAL:
			rc = set_balance(&pulse, pulse.head,
					CLAMP(value, -100, 100));
			break;
		case ACTION_INCREASE:
			rc = set_volume(&pulse, pulse.head,
					CLAMP(pulse.head->volume_percent + value, 0, 150));
			break;
		case ACTION_DECREASE:
			rc = set_volume(&pulse, pulse.head,
					CLAMP(pulse.head->volume_percent - value, 0, 150));
			break;
		case ACTION_MUTE:
			rc = set_mute(&pulse, pulse.head, 1);
			break;
		case ACTION_UNMUTE:
			rc = set_mute(&pulse, pulse.head, 0);
			break;
		case ACTION_TOGGLE:
			rc = set_mute(&pulse, pulse.head, !pulse.head->mute);
			break;
		case ACTION_ISMUTED:
			rc = !pulse.head->mute;
			break;
		case ACTION_SETDEFAULT:
			rc = set_default(&pulse, pulse.head);
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
