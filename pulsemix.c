/* gcc -lm -lpulse pulsemix.c -o pulsemix */

#define _GNU_SOURCE
#include <errno.h>
#include <err.h>
#include <getopt.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pulse/pulseaudio.h>

#define UNUSED __attribute__((unused))

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

struct sink_t {
	uint32_t idx;
	const char *name;
	const char *desc;
	pa_cvolume volume;
	int volume_percent;
	bool mute;

	struct sink_t *next_sink;
};

struct pulseaudio_t {
	pa_context *cxt;
	pa_mainloop *mainloop;
	pa_mainloop_api *mainloop_api;
	enum connectstate state;

	struct sink_t *sinks;
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

static int pulse_async_wait(struct pulseaudio_t *pulse, pa_operation *op)
{
	int r;

	while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
		pa_mainloop_iterate(pulse->mainloop, 1, &r);
	
	return r;
}

static void sink_get_volume(struct pulseaudio_t *pulse)
{
	printf("%s: %d%%\n", pulse->sinks->name, pulse->sinks->volume_percent);
}

static void sink_set_volume(struct pulseaudio_t *pulse, struct sink_t *sink, long v)
{
	pa_cvolume *vol = pa_cvolume_set(&sink->volume, sink->volume.channels,
			(pa_volume_t)(int)fmax(((double)v * PA_VOLUME_NORM / 100), 0));
	pa_operation *op = pa_context_set_sink_volume_by_index(pulse->cxt,
			sink->idx, vol, NULL, NULL);
	pulse_async_wait(pulse, op);

	pa_operation_unref(op);
}

static void sink_set_mute(struct pulseaudio_t *pulse, struct sink_t *sink, int mute)
{
	pa_operation* op = pa_context_set_sink_mute_by_index(pulse->cxt, sink->idx,
			mute, NULL, NULL);
	pulse_async_wait(pulse, op);

	pa_operation_unref(op);
}

static void sink_unmute(struct pulseaudio_t *pulse, struct sink_t *sink)
{
	sink_set_mute(pulse, sink, 0);
}

static void sink_mute(struct pulseaudio_t *pulse, struct sink_t *sink)
{
	sink_set_mute(pulse, sink, 1);
}

static struct sink_t *pulse_sink_new(const pa_sink_info *info)
{
	struct sink_t *sink = calloc(1, sizeof(struct sink_t));

	sink->idx = info->index;
	sink->name = info->name;
	sink->desc = info->description;
	sink->volume.channels = info->volume.channels;

	memcpy(&sink->volume, &info->volume, sizeof(pa_cvolume));
	sink->volume_percent = (int)(((double)pa_cvolume_avg(&sink->volume) * 100.)
			/ PA_VOLUME_NORM);
	sink->mute = info->mute;

	return sink;
}

static void print_sink(struct sink_t *sink)
{
	printf("sink %2d: %s\n  %s\n  Avg. Volume: %d%%\n",
			sink->idx, sink->name, sink->desc, sink->volume_percent);
}

static void print_sinks(struct pulseaudio_t *pulse)
{
	struct sink_t *sink = pulse->sinks;

	while (sink) {
		print_sink(sink);
		sink = sink->next_sink;
	}
}

static void server_info_cb(pa_context UNUSED *c, const pa_server_info *i, void *raw)
{
	const char **sink_name = (const char **)raw;

	*sink_name = i->default_sink_name;
}

static void sink_add_cb(pa_context UNUSED *c, const pa_sink_info *i, int eol,
		void *raw)
{
	struct pulseaudio_t *pulse = raw;
	struct sink_t *s, *sink;

	if (eol)
		return;

	sink = pulse_sink_new(i);

	if (pulse->sinks == NULL)
		pulse->sinks = sink;
	else {
		s = pulse->sinks;
		s->next_sink = sink;
		pulse->sinks = sink;
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
	pa_operation *op = pa_context_get_server_info(pulse->cxt, server_info_cb, &sink_name);
	pulse_async_wait(pulse, op);
	pa_operation_unref(op);

	get_sink_by_name(pulse, sink_name);
}

static void set_default_sink(struct pulseaudio_t *pulse, const char *sinkname)
{
	pa_operation *op;

	get_sink_by_name(pulse, sinkname);
	if (pulse->sinks == NULL) {
		warnx("failed to get sink by name\n");
		return;
	}
 
	op = pa_context_set_default_sink(pulse->cxt, sinkname, NULL, pulse);
	pulse_async_wait(pulse, op);
	pa_operation_unref(op);
}

static void pulse_deinit(struct pulseaudio_t *pulse)
{
	pa_context_disconnect(pulse->cxt);
	pa_mainloop_free(pulse->mainloop);
	free(pulse->sinks);
}

static void pulse_init(struct pulseaudio_t *pulse, const char *clientname)
{
	/* allocate */
	pulse->mainloop = pa_mainloop_new();
	pulse->mainloop_api = pa_mainloop_get_api(pulse->mainloop);
	pulse->cxt = pa_context_new(pulse->mainloop_api, clientname);
	pulse->state = STATE_CONNECTING;
	pulse->sinks = NULL;

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
	fprintf(out, "usage: %s <command> [options]\n", program_invocation_short_name);
	fputs("\nOptions:\n", out);
	fputs(" -h, --help,        display this help and exit\n", out);
	fputs(" -s, --sink <name>  control a sink other than the default\n", out);

	fputs("\nCommands:\n", out);
	fputs("  list               list available sinks\n", out);
	fputs("  get-volume         get volume for sink\n", out);
	fputs("  set-volume         set volume for sink\n", out);
	fputs("  increase           increase volume\n", out);
	fputs("  decrease           decrease volume\n", out);
	fputs("  mute               mute active sink\n", out);
	fputs("  unmute             unmute active sink\n", out);
	fputs("  toggle             toggle mute\n", out);
	fputs("  set-sink           set default sink\n", out);

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

	return ACTION_INVALID;
}

int main(int argc, char *argv[])
{
	struct pulseaudio_t pulse;
	enum action verb;
	char *sink = NULL;
	union arg_t value;

	static const struct option opts[] = {
		{ "help", no_argument, 0, 'h' },
		{ "sink", required_argument, 0, 's' },
	};

	for (;;) {
		int optidx;
		int opt = getopt_long(argc, argv, "hs:", opts, &optidx);
		if (opt == -1)
			break;

		switch (opt) {
		case 'h':
			usage(stdout);
		case 's':
			sink = optarg;
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
		print_sinks(&pulse);
	} else {
		/* determine sink */
		if (sink) {
			get_sink_by_name(&pulse, sink);
		} else
			get_default_sink(&pulse);

		if(pulse.sinks == NULL)
			errx(EXIT_FAILURE, "sink not found: %s", sink ? sink : "default");

		switch (verb) {
		case ACTION_GETVOL:
			sink_get_volume(&pulse);
			break;
		case ACTION_SETVOL:
			sink_set_volume(&pulse, pulse.sinks, value.l);
			break;
		case ACTION_INCREASE:
			sink_set_volume(&pulse, pulse.sinks, pulse.sinks->volume_percent + value.l);
			break;
		case ACTION_DECREASE:
			sink_set_volume(&pulse, pulse.sinks, pulse.sinks->volume_percent - value.l);
			break;
		case ACTION_MUTE:
			sink_mute(&pulse, pulse.sinks);
			break;
		case ACTION_UNMUTE:
			sink_unmute(&pulse, pulse.sinks);
			break;
		case ACTION_TOGGLE:
			sink_set_mute(&pulse, pulse.sinks, !pulse.sinks->mute);
			break;
		case ACTION_SETSINK:
			set_default_sink(&pulse, value.c);
		default:
			break;
		}
	}

	/* shut down */
	pulse_deinit(&pulse);

	return 0;
}

/* vim: set noet ts=2 sw=2: */
