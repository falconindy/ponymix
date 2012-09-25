/* Copyright (c) 2012 Dave Reisner
 *
 * ponymix.c
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
#include <unistd.h>

#include <pulse/pulseaudio.h>

#define UNUSED __attribute__((unused))

#define CLAMP(x, low, high) \
	__extension__ ({ \
		typeof(x) _x = (x); \
		typeof(low) _low = (low); \
		typeof(high) _high = (high); \
		((_x > _high) ? _high : ((_x < _low) ? _low : _x)); \
	})

#define COLOR_RESET    "\033[0m"
#define COLOR_BOLD     "\033[1m"
#define COLOR_ITALIC   "\033[3m"
#define COLOR_UL       "\033[4m"
#define COLOR_BLINK    "\033[5m"
#define COLOR_REV      "\033[7m"
#define COLOR_RED      "\033[31m"
#define COLOR_GREEN    "\033[32m"
#define COLOR_YELLOW   "\033[33m"
#define COLOR_BLUE     "\033[34m"
#define COLOR_MAGENTA  "\033[35m"
#define COLOR_CYAN     "\033[36m"
#define COLOR_WHITE    "\033[37m"

enum mode {
	MODE_DEVICE = 1 << 0,
	MODE_APP    = 1 << 1,
	MODE_ANY    = (1 << 2) - 1,
};

enum action {
	ACTION_DEFAULTS = 0,
	ACTION_LIST,
	ACTION_GETVOL,
	ACTION_SETVOL,
	ACTION_GETBAL,
	ACTION_SETBAL,
	ACTION_ADJBAL,
	ACTION_INCREASE,
	ACTION_DECREASE,
	ACTION_MUTE,
	ACTION_UNMUTE,
	ACTION_TOGGLE,
	ACTION_ISMUTED,
	ACTION_SETDEFAULT,
	ACTION_MOVE,
	ACTION_KILL,
	ACTION_INVALID
};

struct action_t {
	const char *cmd;
	int argreq;
	enum mode argmode;
};

static struct action_t actions[ACTION_INVALID] = {
	[ACTION_DEFAULTS]   = { "defaults",    0, MODE_DEVICE },
	[ACTION_LIST]       = { "list",        0, MODE_ANY },
	[ACTION_GETVOL]     = { "get-volume",  0, MODE_ANY },
	[ACTION_SETVOL]     = { "set-volume",  1, MODE_ANY },
	[ACTION_GETBAL]     = { "get-balance", 0, MODE_ANY },
	[ACTION_SETBAL]     = { "set-balance", 1, MODE_ANY },
	[ACTION_ADJBAL]     = { "adj-balance", 1, MODE_ANY },
	[ACTION_INCREASE]   = { "increase",    1, MODE_ANY },
	[ACTION_DECREASE]   = { "decrease",    1, MODE_ANY },
	[ACTION_MUTE]       = { "mute",        0, MODE_ANY },
	[ACTION_UNMUTE]     = { "unmute",      0, MODE_ANY },
	[ACTION_TOGGLE]     = { "toggle",      0, MODE_ANY },
	[ACTION_ISMUTED]    = { "is-muted",    0, MODE_ANY },
	[ACTION_SETDEFAULT] = { "set-default", 1, MODE_DEVICE },
	[ACTION_MOVE]       = { "move",        2, MODE_APP },
	[ACTION_KILL]       = { "kill",        1, MODE_APP },
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

	struct ops_t {
		pa_operation *(*mute)(pa_context *, uint32_t, int, pa_context_success_cb_t, void *);
		pa_operation *(*setvol)(pa_context *, uint32_t, const pa_cvolume *, pa_context_success_cb_t, void *);
		pa_operation *(*setdefault)(pa_context *, const char *, pa_context_success_cb_t, void *);
		pa_operation *(*move)(pa_context *, uint32_t, uint32_t, pa_context_success_cb_t, void *);
		pa_operation *(*kill)(pa_context *, uint32_t, pa_context_success_cb_t, void *);
	} op;

	struct io_t *next;
	struct io_t *prev;
};

struct cb_data_t {
	struct io_t **list;
	const char *glob;
};

struct pulseaudio_t {
	pa_context *cxt;
	pa_mainloop *mainloop;

	char *default_sink;
	char *default_source;
};

struct colstr_t {
	const char *name;
	const char *over9000;
	const char *veryhigh;
	const char *high;
	const char *mid;
	const char *low;
	const char *verylow;
	const char *mute;
	const char *nc;
};

struct runtime_t {
	enum mode mode;
	const char *pp_name;

	int (*get_default)(struct pulseaudio_t *, struct io_t **);
	int (*get_by_name)(struct pulseaudio_t *, struct io_t **, const char *, enum mode);
};

struct arg_t {
	long value;
	struct io_t *devices;
	struct io_t *target;
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

static void io_list_add(struct io_t **list, struct io_t *node)
{
	struct io_t *head = *list;

	if (head == NULL)
		head = node;
	else {
		head->prev->next = node;
		node->prev = head->prev;
	}

	head->prev = node;
	*list = head;
}

static void populate_levels(struct io_t *node)
{
	node->volume_percent = (int)(((double)pa_cvolume_avg(&node->volume) * 100)
			/ PA_VOLUME_NORM);
	node->balance = (int)((double)pa_cvolume_get_balance(&node->volume,
			&node->channels) * 100);
}

#define IO_NEW(io, info, pp) \
	io = calloc(1, sizeof(struct io_t)); \
	io->idx = info->index; \
	io->mute = info->mute; \
	io->name = strdup(info->name); \
	io->pp_name = pp; \
	memcpy(&io->volume, &info->volume, sizeof(pa_cvolume)); \
	memcpy(&io->channels, &info->channel_map, sizeof(pa_channel_map)); \
	populate_levels(io);

static struct io_t *sink_new(const pa_sink_info *info)
{
	struct io_t *sink;

	IO_NEW(sink, info, "sink");
	sink->desc = strdup(info->description);
	sink->op.mute = pa_context_set_sink_mute_by_index;
	sink->op.setvol = pa_context_set_sink_volume_by_index;
	sink->op.setdefault = pa_context_set_default_sink;

	return sink;
}

static struct io_t *sink_input_new(const pa_sink_input_info *info)
{
	struct io_t *sink;

	IO_NEW(sink, info, "output");
	sink->desc = strdup(
		pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_NAME));
	sink->op.mute = pa_context_set_sink_input_mute;
	sink->op.setvol = pa_context_set_sink_input_volume;
	sink->op.move = pa_context_move_sink_input_by_index;
	sink->op.kill = pa_context_kill_sink_input;

	return sink;
}

static struct io_t *source_new(const pa_source_info *info)
{
	struct io_t *source;

	IO_NEW(source, info, "source");
	source->desc = strdup(info->description);
	source->op.mute = pa_context_set_source_mute_by_index;
	source->op.setvol = pa_context_set_source_volume_by_index;
	source->op.setdefault = pa_context_set_default_source;

	return source;
}

static struct io_t *source_output_new(const pa_source_output_info *info)
{
	struct io_t *source;

	IO_NEW(source, info, "input");
	source->desc = strdup(
		pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_NAME));
	source->op.mute = pa_context_set_source_output_mute;
	source->op.setvol = pa_context_set_source_output_volume;
	source->op.move = pa_context_move_source_output_by_index;
	source->op.kill = pa_context_kill_source_output;

	return source;
}

static void sink_add_cb(pa_context UNUSED *c, const pa_sink_info *i, int eol,
		void *raw)
{
	struct cb_data_t *pony = raw;
	if (eol)
		return;
	if (pony->glob && strstr(pony->glob, i->name) != NULL)
		return;
	io_list_add(pony->list, sink_new(i));
}

static void sink_input_add_cb(pa_context UNUSED *c, const pa_sink_input_info *i, int eol,
		void *raw)
{
	struct cb_data_t *pony = raw;
	if (eol)
		return;
	if (pony->glob && !(strstr(pony->glob, i->name) == NULL ||
			strstr(pony->glob, pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_NAME)) == NULL))
		return;
	io_list_add(pony->list, sink_input_new(i));
}

static void source_add_cb(pa_context UNUSED *c, const pa_source_info *i, int eol, void *raw)
{
	struct cb_data_t *pony = raw;
	if (eol)
		return;
	if (pony->glob && strstr(pony->glob, i->name) != NULL)
		return;
	io_list_add(pony->list, source_new(i));
}

static void source_output_add_cb(pa_context UNUSED *c, const pa_source_output_info *i, int eol,
		void *raw)
{
	struct cb_data_t *pony = raw;
	if (eol)
		return;
	if (pony->glob && !(strstr(pony->glob, i->name) == NULL ||
			strstr(pony->glob, pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_NAME)) == NULL))
		return;
	io_list_add(pony->list, source_output_new(i));
}

static void server_info_cb(pa_context UNUSED *c, const pa_server_info *i, void *raw)
{
	struct pulseaudio_t *pulse = raw;
	pulse->default_sink = strdup(i->default_sink_name);
	pulse->default_source = strdup(i->default_source_name);
}

static void connect_state_cb(pa_context *cxt, void *raw)
{
	enum pa_context_state *state = raw;
	*state = pa_context_get_state(cxt);
}

static void success_cb(pa_context UNUSED *c, int success, void *raw)
{
	int *rc = raw;
	*rc = success;
}

static void pulse_async_wait(struct pulseaudio_t *pulse, pa_operation *op)
{
	while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
		pa_mainloop_iterate(pulse->mainloop, 1, NULL);
}

static int set_volume(struct pulseaudio_t *pulse, struct io_t *dev, long v)
{
	int success = 0;
	pa_cvolume *vol = pa_cvolume_set(&dev->volume, dev->volume.channels,
			(int)fmax((double)(v + .5) * PA_VOLUME_NORM / 100, 0));

	pa_operation *op = dev->op.setvol(pulse->cxt, dev->idx, vol,
			success_cb, &success);
	pulse_async_wait(pulse, op);

	if (success)
		printf("%ld\n", v);
	else {
		int err = pa_context_errno(pulse->cxt);
		fprintf(stderr, "failed to set volume: %s\n", pa_strerror(err));
	}

	pa_operation_unref(op);

	return !success;
}

static int set_balance(struct pulseaudio_t *pulse, struct io_t *dev, long v)
{
	int success = 0;
	pa_cvolume *vol;
	pa_operation *op;

	if (pa_channel_map_valid(&dev->channels) == 0) {
		warnx("can't set balance on that device.");
		return 1;
	}

	vol = pa_cvolume_set_balance(&dev->volume, &dev->channels, v / 100.0);
	op = dev->op.setvol(pulse->cxt, dev->idx, vol, success_cb, &success);
	pulse_async_wait(pulse, op);

	if (success)
		printf("%ld\n", v);
	else {
		int err = pa_context_errno(pulse->cxt);
		warnx("failed to set balance: %s", pa_strerror(err));
	}

	pa_operation_unref(op);

	return !success;
}

static int set_mute(struct pulseaudio_t *pulse, struct io_t *dev, int mute)
{
	int success = 0;
	pa_operation* op;

	/* new effective volume */
	printf("%d\n", mute ? 0 : dev->volume_percent);

	op = dev->op.mute(pulse->cxt, dev->idx, mute,
			success_cb, &success);

	pulse_async_wait(pulse, op);

	if (!success) {
		int err = pa_context_errno(pulse->cxt);
		fprintf(stderr, "failed to mute device: %s\n", pa_strerror(err));
	}

	pa_operation_unref(op);

	return !success;
}

static int kill_client(struct pulseaudio_t *pulse, struct io_t *dev)
{
	int success = 0;
	pa_operation *op;

	if (dev->op.kill == NULL) {
		warnx("only clients can be killed");
		return 1;
	}

	op = dev->op.kill(pulse->cxt, dev->idx, success_cb, &success);
	pulse_async_wait(pulse, op);

	if (!success) {
		int err = pa_context_errno(pulse->cxt);
		fprintf(stderr, "failed to kill client: %s\n", pa_strerror(err));
	}

	pa_operation_unref(op);

	return !success;
}

static int move_client(struct pulseaudio_t *pulse, struct io_t *dev, struct io_t *target)
{
	int success = 0;
	pa_operation* op;

	if (dev->op.move == NULL) {
		warnx("only clients can be moved");
		return 1;
	}

	if (target == NULL) {
		warnx("no destination to move to");
		return 1;
	}

	op = dev->op.move(pulse->cxt, dev->idx, target->idx, success_cb, &success);
	pulse_async_wait(pulse, op);

	if (!success) {
		int err = pa_context_errno(pulse->cxt);
		fprintf(stderr, "failed to move client: %s\n", pa_strerror(err));
	}

	pa_operation_unref(op);

	return !success;
}

static void print_one(struct colstr_t *colstr, struct io_t *dev)
{
	const char *level;
	const char *mute = dev->mute ? "[Muted]" : "";

	if (dev->volume_percent < 20)
		level = colstr->verylow;
	else if (dev->volume_percent < 40)
		level = colstr->low;
	else if (dev->volume_percent < 60)
		level = colstr->mid;
	else if (dev->volume_percent < 80)
		level = colstr->high;
	else if (dev->volume_percent <= 100)
		level = colstr->veryhigh;
	else
		level = colstr->over9000;

	printf("%s%s %d:%s %s\n", colstr->name, dev->pp_name, dev->idx,
			colstr->nc, dev->name);
	printf("  %s\n", dev->desc);
	printf("  Avg. Volume: %s%d%%%s %s%s%s\n", level, dev->volume_percent,
			colstr->nc, colstr->mute, mute, colstr->nc);
}

static void print_all(struct io_t *head)
{
	struct io_t *node = head;
	struct colstr_t colstr;

	if (isatty(fileno(stdout))) {
		colstr.name = COLOR_BOLD;
		colstr.over9000 = COLOR_REV COLOR_RED;
		colstr.veryhigh = COLOR_RED;
		colstr.high = COLOR_MAGENTA;
		colstr.mid = COLOR_YELLOW;
		colstr.low = COLOR_GREEN;
		colstr.verylow = COLOR_BLUE;
		colstr.mute = COLOR_BOLD COLOR_RED;
		colstr.nc = COLOR_RESET;
	} else {
		colstr.name = "";
		colstr.over9000 = "";
		colstr.veryhigh = "";
		colstr.high = "";
		colstr.mid = "";
		colstr.low = "";
		colstr.verylow = "";
		colstr.mute = "";
		colstr.nc = "";
	}

	while (node) {
		print_one(&colstr, node);
		node = node->next;
	}
}

static int populate_sinks(struct pulseaudio_t *pulse, struct io_t **list, const char *filter, enum mode mode)
{
	pa_operation *op;
	struct cb_data_t pony = { .list = list, .glob = filter };

	switch (mode) {
	case MODE_APP:
		op = pa_context_get_sink_input_info_list(pulse->cxt, sink_input_add_cb, &pony);
		break;
	case MODE_DEVICE:
	default:
		op = pa_context_get_sink_info_list(pulse->cxt, sink_add_cb, &pony);
	}

	pulse_async_wait(pulse, op);
	pa_operation_unref(op);
	return 0;
}

static int find_sink(struct pulseaudio_t *pulse, struct io_t **list, const char *name, enum mode mode)
{
	long id;

	if (xstrtol(name, &id) < 0)
		populate_sinks(pulse, list, name, mode);
	else {
		pa_operation *op;
		struct cb_data_t pony = { .list = list };

		switch (mode) {
		case MODE_APP:
			op = pa_context_get_sink_input_info(pulse->cxt, (uint32_t)id, sink_input_add_cb, &pony);
			break;
		case MODE_DEVICE:
		default:
			op = pa_context_get_sink_info_by_index(pulse->cxt, (uint32_t)id, sink_add_cb, &pony);
		}

		pulse_async_wait(pulse, op);
		pa_operation_unref(op);
	}
	return 0;
}

static int get_default_sink(struct pulseaudio_t *pulse, struct io_t **list)
{
	pa_operation *op;
	struct cb_data_t pony = { .list = list };

	op = pa_context_get_sink_info_by_name(pulse->cxt, pulse->default_sink, sink_add_cb, &pony);

	pulse_async_wait(pulse, op);
	pa_operation_unref(op);
	return 0;
}

static int populate_sources(struct pulseaudio_t *pulse, struct io_t **list, const char *filter, enum mode mode)
{
	pa_operation *op;
	struct cb_data_t pony = { .list = list, .glob = filter };

	switch (mode) {
	case MODE_APP:
		op = pa_context_get_source_output_info_list(pulse->cxt, source_output_add_cb, &pony);
		break;
	case MODE_DEVICE:
	default:
		op = pa_context_get_source_info_list(pulse->cxt, source_add_cb, &pony);
	}

	pulse_async_wait(pulse, op);
	pa_operation_unref(op);
	return 0;
}

static int find_source(struct pulseaudio_t *pulse, struct io_t **list, const char *name, enum mode mode)
{
	long id;

	if (xstrtol(name, &id) < 0)
		populate_sources(pulse, list, name, mode);
	else {
		pa_operation *op;
		struct cb_data_t pony = { .list = list };

		switch (mode) {
		case MODE_APP:
			op = pa_context_get_source_output_info(pulse->cxt, (uint32_t)id, source_output_add_cb, &pony);
			break;
		case MODE_DEVICE:
		default:
			op = pa_context_get_source_info_by_index(pulse->cxt, (uint32_t)id, source_add_cb, &pony);
		}

		pulse_async_wait(pulse, op);
		pa_operation_unref(op);
	}

	return 0;
}

static int get_default_source(struct pulseaudio_t *pulse, struct io_t **list)
{
	pa_operation *op;
	struct cb_data_t pony = { .list = list };

	op = pa_context_get_source_info_by_name(pulse->cxt, pulse->default_source, source_add_cb, &pony);

	pulse_async_wait(pulse, op);
	pa_operation_unref(op);
	return 0;
}

static int set_default(struct pulseaudio_t *pulse, struct io_t *dev)
{
	int success = 0;
	pa_operation *op;

	if (dev->op.setdefault == NULL) {
		warnx("valid operation only for devices");
		return 1;
	}

	op = dev->op.setdefault(pulse->cxt, dev->name, success_cb, &success);
	pulse_async_wait(pulse, op);

	if (!success) {
		int err = pa_context_errno(pulse->cxt);
		fprintf(stderr, "failed to set default %s to %s: %s\n", dev->pp_name,
				dev->name, pa_strerror(err));
	}

	pa_operation_unref(op);

	return !success;
}

static int pulse_init(struct pulseaudio_t *pulse)
{
	pa_operation *op;
	enum pa_context_state state = PA_CONTEXT_CONNECTING;

	pulse->mainloop = pa_mainloop_new();
	pulse->cxt = pa_context_new(pa_mainloop_get_api(pulse->mainloop), "bestpony");
	pulse->default_sink = NULL;
	pulse->default_source = NULL;

	pa_context_set_state_callback(pulse->cxt, connect_state_cb, &state);
	pa_context_connect(pulse->cxt, NULL, PA_CONTEXT_NOFLAGS, NULL);
	while (state != PA_CONTEXT_READY && state != PA_CONTEXT_FAILED)
		pa_mainloop_iterate(pulse->mainloop, 1, NULL);

	if (state != PA_CONTEXT_READY) {
		fprintf(stderr, "failed to connect to pulse daemon: %s\n",
				pa_strerror(pa_context_errno(pulse->cxt)));
		return 1;
	}

	op = pa_context_get_server_info(pulse->cxt, server_info_cb, pulse);
	pulse_async_wait(pulse, op);
	pa_operation_unref(op);
	return 0;
}

static void pulse_deinit(struct pulseaudio_t *pulse)
{
	pa_context_disconnect(pulse->cxt);
	pa_mainloop_free(pulse->mainloop);
	free(pulse->default_sink);
	free(pulse->default_source);
}

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fprintf(out, "usage: %s [options] <command>...\n", program_invocation_short_name);
	fputs("\nOptions:\n"
	      " -h, --help                    display this help and exit\n\n"

	      " -d, --device                  set device mode (default)\n"
	      " -a, --app                     set application mode\n\n"

	      " -o<name>, --sink=<name>       control a sink other than the default\n"
	      " -i<name>, --source=<name>     control a source\n", out);

	fputs("\nCommon Commands:\n"
	      "  list                   list available devices\n"
	      "  get-volume             get volume for device\n"
	      "  set-volume VALUE       set volume for device\n"
	      "  get-balance            get balance for device\n"
	      "  set-balance VALUE      set balance for device\n"
	      "  adj-balance VALUE      increase or decrease balance for device\n"
	      "  increase VALUE         increase volume\n", out);
	fputs("  decrease VALUE         decrease volume\n"
	      "  mute                   mute device\n"
	      "  unmute                 unmute device\n"
	      "  toggle                 toggle mute\n"
	      "  is-muted               check if muted\n", out);

	fputs("\nDevice Commands:\n"
	      "  defaults               list default devices (default command)\n"
	      "  set-default DEVICE_ID  set default device by ID\n"

	      "\nApplication Commands:\n"
	      "  move APP_ID DEVICE_ID  move application stream by ID to device ID\n"
	      "  kill APP_ID            kill an application stream by ID\n", out);

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

static enum action string_to_verb(const char *string)
{
	size_t i;

	for (i = 0; i < ACTION_INVALID; i++)
		if (strcmp(actions[i].cmd, string) == 0)
			break;

	return i;
}

static int do_verb(struct pulseaudio_t *pulse, enum action verb, struct arg_t *arg)
{
	struct io_t *device = arg->devices;
	struct io_t *target = arg->target;

	switch (verb) {
	case ACTION_GETVOL:
		printf("%d\n", device->volume_percent);
		return 0;
	case ACTION_SETVOL:
		return set_volume(pulse, device, CLAMP(arg->value, 0, 150));
	case ACTION_GETBAL:
		printf("%d\n", device->balance);
		return 0;
	case ACTION_SETBAL:
		return set_balance(pulse, device, CLAMP(arg->value, -100, 100));
	case ACTION_ADJBAL:
		return set_balance(pulse, device,
				CLAMP(device->balance + arg->value, -100, 100));
	case ACTION_INCREASE:
		if (device->volume_percent > 100) {
			printf("%d\n", device->volume_percent);
			return 0;
		}

		return set_volume(pulse, device,
				CLAMP(device->volume_percent + arg->value, 0, 100));
	case ACTION_DECREASE:
		return set_volume(pulse, device,
				CLAMP(device->volume_percent - arg->value, 0, 100));
	case ACTION_MUTE:
		return set_mute(pulse, device, 1);
	case ACTION_UNMUTE:
		return set_mute(pulse, device, 0);
	case ACTION_TOGGLE:
		return set_mute(pulse, device, !device->mute);
	case ACTION_ISMUTED:
		return !device->mute;
	case ACTION_MOVE:
		return move_client(pulse, device, target);
	case ACTION_KILL:
		return kill_client(pulse, device);
	case ACTION_SETDEFAULT:
		return set_default(pulse, device);
	default:
		errx(EXIT_FAILURE, "internal error: unhandled verb id %d\n", verb);
	}
}

static int get_device(struct pulseaudio_t *pulse, const char *id,
		struct io_t **device, struct runtime_t *run)
{
	int rc = 0;

	/* try to find device by id or a default device */
	if (id && run->get_by_name) {
		rc = run->get_by_name(pulse, device, id, run->mode);
	} else if (run->get_default) {
		rc = run->get_default(pulse, device);
	}

	if (rc != 0)
		return rc;

	/* if no device found, report an error */
	if (!*device) {
		if (!run->get_default)
			warnx("a valid %s id is required", run->pp_name);
		else
			warnx("%s not found: %s", run->pp_name, id ? id : "default");
		return 1;
	}

	/* if more then one device found, report an error */
	if ((*device)->next) {
		warnx("%s does not uniquely identify a %s", id, run->pp_name);
		return 1;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	struct pulseaudio_t pulse;
	enum action verb;
	char *id = NULL;
	int rc = EXIT_SUCCESS;

	struct arg_t arg = { 0, NULL, NULL };
	struct runtime_t run = {
		.mode        = MODE_DEVICE,
		.pp_name     = "sink",
		.get_default = get_default_sink,
		.get_by_name = find_sink
	};

	static const struct option opts[] = {
		{ "app", no_argument, 0, 'a' },
		{ "device", no_argument, 0, 'd' },
		{ "help", no_argument, 0, 'h' },
		{ "sink", optional_argument, 0, 'o' },
		{ "output", optional_argument, 0, 'o' },
		{ "source", optional_argument, 0, 'i' },
		{ "input", optional_argument, 0, 'i' },
		{ 0, 0, 0, 0 },
	};

	for (;;) {
		int opt = getopt_long(argc, argv, "adhi::o::", opts, NULL);
		if (opt == -1)
			break;

		switch (opt) {
		case 'h':
			usage(stdout);
		case 'd':
			run.mode = MODE_DEVICE;
			break;
		case 'a':
			run.mode = MODE_APP;
			break;
		case 'o':
			id = optarg;
			run.get_default = get_default_sink;
			run.get_by_name = find_sink;
			run.pp_name = "sink";
			break;
		case 'i':
			id = optarg;
			run.get_default = get_default_source;
			run.get_by_name = find_source;
			run.pp_name = "source";
			break;
		default:
			return EXIT_FAILURE;
		}
	}

	if (run.mode == MODE_APP)
		run.get_default = NULL;

	if (optind == argc)
		verb = run.mode == MODE_DEVICE ? ACTION_DEFAULTS : ACTION_LIST;
	else
		verb = string_to_verb(argv[optind++]);

	if (verb == ACTION_INVALID)
		errx(EXIT_FAILURE, "unknown action: %s", argv[optind - 1]);

	if (actions[verb].argreq != (argc - optind))
		errx(EXIT_FAILURE, "wrong number of args for %s command (requires %d)",
				argv[optind - 1], actions[verb].argreq);

	if (!(actions[verb].argmode & run.mode))
		errx(EXIT_FAILURE, "wrong mode for %s command", argv[optind - 1]);

	/* initialize connection */
	if (pulse_init(&pulse) != 0)
		return EXIT_FAILURE;

	switch (verb) {
	case ACTION_DEFAULTS:
		get_default_sink(&pulse, &arg.devices);
		get_default_source(&pulse, &arg.devices);
		print_all(arg.devices);
		goto done;
	case ACTION_LIST:
		populate_sinks(&pulse, &arg.devices, NULL, run.mode);
		populate_sources(&pulse, &arg.devices, NULL, run.mode);
		print_all(arg.devices);
		goto done;
	case ACTION_SETVOL:
	case ACTION_SETBAL:
	case ACTION_ADJBAL:
	case ACTION_INCREASE:
	case ACTION_DECREASE:
		if (xstrtol(argv[optind], &arg.value) < 0)
			errx(EXIT_FAILURE, "invalid number: %s", argv[optind]);
	case ACTION_GETVOL:
	case ACTION_GETBAL:
	case ACTION_MUTE:
	case ACTION_UNMUTE:
	case ACTION_TOGGLE:
	case ACTION_ISMUTED:
		rc = get_device(&pulse, id, &arg.devices, &run);
		if (rc)
			goto done;
		break;
	case ACTION_SETDEFAULT:
	case ACTION_KILL:
	case ACTION_MOVE:
		rc = get_device(&pulse, argv[optind], &arg.devices, &run);
		if (rc)
			goto done;
	default:
		break;
	}

	if (verb == ACTION_MOVE) {
		run.mode = MODE_DEVICE;
		rc = get_device(&pulse, argv[optind + 1], &arg.target, &run);
		if (rc)
			goto done;
	}

	rc = do_verb(&pulse, verb, &arg);

done:
	/* shut down */
	pulse_deinit(&pulse);

	return rc;
}

/* vim: set noet ts=2 sw=2: */
