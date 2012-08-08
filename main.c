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

enum action {
	ACTION_LISTDEFAULTS = 0,
	ACTION_LISTAPPLICATIONS,
	ACTION_LISTINPUT,
	ACTION_LISTOUTPUT,
	ACTION_SETDEFAULT,
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

void usage(FILE *out);
enum action string_to_verb(const char *string);
int xstrtof(const char *str, float *out);
int xstrtol(const char *str, long *out);

int main(int argc, char *argv[])
{
	int rc = 0;
	char *sink = NULL;
	char *source = NULL;
	char *stream = NULL;
	enum action verb;
	enum type use = TYPE_INVALID;
	union arg_t value;
	struct pulseaudio_t pulse;

	static const struct option opts[] = {
		{ "help",        no_argument,       0, 'h' },
		{ "application", optional_argument, 0, 'a' },
		{ "input",       optional_argument, 0, 'i' },
		{ "output",      optional_argument, 0, 'o' },
		{ 0 }
	};

	for (;;) {
		char opt = getopt_long(argc, argv, "ha::i::o::", opts, NULL);
		if (opt == -1)
			break;

		switch (opt) {
			case 'h':
				usage(stdout);
				break;
			case 'a':
				use = TYPE_STREAM;
				if (optarg != NULL)
					stream = optarg;
				break;
			case 'i':
				use = TYPE_SOURCE;
				if (optarg != NULL)
					source = optarg;
				break;
			case 'o':
				use = TYPE_SINK;
				if (optarg != NULL)
					sink = optarg;
				break;
			default:
				exit(EXIT_FAILURE);
				break;
		}
	}

	verb = (optind == argc) ? ACTION_INVALID : string_to_verb(argv[optind]);
	if (verb == ACTION_INVALID) {
		if (argv[optind] != NULL)
			errx(EXIT_FAILURE, "unknown command: %s", argv[optind]);
		else
			errx(EXIT_FAILURE, "missing command: Try 'pulsemix --help' for more information.");
	}

	optind++;
	if (verb == ACTION_SETVOL || verb == ACTION_SETBAL || verb == ACTION_INCREASE || verb == ACTION_DECREASE) {
		if (optind == argc)
			errx(EXIT_FAILURE, "missing value for command '%s'", argv[optind - 1]);
		else {
			int r = 0;
			if (verb == ACTION_SETBAL)
				r = xstrtof(argv[optind], &value.f);
			else
				r = xstrtol(argv[optind], &value.l);

			if (r < 0)
				errx(EXIT_FAILURE, "invalid number: %s", argv[optind]);
		}
	}

	pulse_init(&pulse, "pulsemix");
	if (pulse_connect(&pulse) != 0)
		return EXIT_FAILURE;

	if (verb == ACTION_LISTDEFAULTS) {
		get_default_source(&pulse);
		print_sources(&pulse);
		clean_source_t(&pulse);
		get_default_sink(&pulse);
		print_sources(&pulse);
	} else if (verb == ACTION_LISTAPPLICATIONS) {
		get_streams(&pulse);
		print_sources(&pulse);
	} else if (verb == ACTION_LISTINPUT) {
		get_sources(&pulse);
		print_sources(&pulse);
	} else if (verb == ACTION_LISTOUTPUT) {
		get_sinks(&pulse);
		print_sources(&pulse);
	} else if (use == TYPE_INVALID) {
		errx(EXIT_FAILURE, "missing option: Try 'pulsemix --help' for more information.");
	}

	if (use == TYPE_STREAM && stream == NULL) {
		errx(EXIT_FAILURE, "controlling applications needs an ID");
	} else if (use == TYPE_STREAM && stream != NULL) {
		long idx;
		int r = xstrtol(stream, &idx);
		if (r < 0)
			errx(EXIT_FAILURE, "invalid ID: %s", stream);
		else
			get_stream_by_index(&pulse, (uint32_t)idx);

		if (pulse.source == NULL)
			errx(EXIT_FAILURE, "application not found with ID: %s", stream);
	} else if (use == TYPE_SOURCE && source == NULL) {
		get_default_source(&pulse);
	} else if (use == TYPE_SOURCE && source != NULL) {
		long idx;
		int r = xstrtol(source, &idx);
		if (r < 0)
			errx(EXIT_FAILURE, "invalid ID: %s", source);
		else
			get_source_by_index(&pulse, (uint32_t)idx);

		if(pulse.source == NULL)
			errx(EXIT_FAILURE, "input not found with ID: %s", source);
	} else if(use == TYPE_SINK && sink == NULL) {
		get_default_sink(&pulse);
	} else if(use == TYPE_SINK && sink != NULL) {
		long idx;
		int r = xstrtol(sink, &idx);
		if (r < 0)
			errx(EXIT_FAILURE, "invalid ID: %s", sink);
		else
			get_sink_by_index(&pulse, (uint32_t)idx);

		if (pulse.source == NULL)
			errx(EXIT_FAILURE, "output not found with ID: %s", sink);
	}

	switch(verb) {
		case ACTION_SETDEFAULT:
			set_default(&pulse);
			break;
		case ACTION_GETVOL:
			get_volume(&pulse);
			break;
		case ACTION_SETVOL:
			rc = set_volume(&pulse, value.l);
			break;
		case ACTION_GETBAL:
			get_balance(&pulse);
			break;
		case ACTION_SETBAL:
			rc = set_balance(&pulse, value.f);
			break;
		case ACTION_INCREASE:
			rc = set_volume(&pulse, pulse.source->volume_percent + value.l);
			break;
		case ACTION_DECREASE:
			rc = set_volume(&pulse, pulse.source->volume_percent - value.l);
			break;
		case ACTION_MUTE:
			rc = mute(&pulse);
			break;
		case ACTION_UNMUTE:
			rc = unmute(&pulse);
			break;
		case ACTION_TOGGLE:
			rc = set_mute(&pulse, !pulse.source->mute);
			break;
		default:
			break;
	}

	pulse_deinit(&pulse);
	return rc;
}

static void usage(FILE *out)
{
	fprintf(out, "usage: %s [options] <command>...\n", program_invocation_short_name);
	fputs("\nOptions:\n", out);
	fputs("  -h, --help,           display this help and exit\n", out);
	fputs("  -a  --application=id  control a application\n", out);
	fputs("  -i, --input=id        control the input device (if no id given use default set)\n", out);
	fputs("  -o, --output=id       control the output device (if no id given use default set)\n", out);

	fputs("\nCommands:\n", out);
	fputs("  list-defaults      list default set input/output devices\n", out);
	fputs("  list-applications  list available applications\n", out);
	fputs("  list-inputs        list available input devices\n", out);
	fputs("  list-outputs       list available output devices\n", out);
	fputs("  set-default        sets the selected input/output as default\n", out);
	fputs("  get-volume         get volume\n", out);
	fputs("  set-volume VALUE   set volume\n", out);
	fputs("  get-balance        get balance for output\n", out);
	fputs("  set-balance VALUE  set balance for output, pass double -- before the negative number ex( -- -0.5) \n", out);
	fputs("                     range is between -1.0 to 1.0 and 0 being centered\n", out);
	fputs("  increase VALUE     increase volume\n", out);
	fputs("  decrease VALUE     decrease volume\n", out);
	fputs("  mute               mute\n", out);
	fputs("  unmute             unmute\n", out);
	fputs("  toggle             toggle mute\n", out);

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

static enum action string_to_verb(const char *string)
{
	if(strcmp(string, "list-defaults") == 0)
		return ACTION_LISTDEFAULTS;
	else if(strcmp(string, "list-applications") == 0)
		return ACTION_LISTAPPLICATIONS;
	else if(strcmp(string, "list-inputs") == 0)
		return ACTION_LISTINPUT;
	else if(strcmp(string, "list-outputs") == 0)
		return ACTION_LISTOUTPUT;
	else if(strcmp(string, "vset-default") == 0)
		return ACTION_SETDEFAULT;
	else if(strcmp(string, "get-volume") == 0)
		return ACTION_GETVOL;
	else if(strcmp(string, "set-volume") == 0)
		return ACTION_SETVOL;
	else if(strcmp(string, "get-balance") == 0)
		return ACTION_GETBAL;
	else if(strcmp(string, "set-balance") == 0)
		return ACTION_SETBAL;
	else if(strcmp(string, "increase") == 0)
		return ACTION_INCREASE;
	else if(strcmp(string, "decrease") == 0)
		return ACTION_DECREASE;
	else if(strcmp(string, "mute") == 0)
		return ACTION_MUTE;
	else if(strcmp(string, "unmute") == 0)
		return ACTION_UNMUTE;
	else if(strcmp(string, "toggle") == 0)
		return ACTION_TOGGLE;

	return ACTION_INVALID;
}

static int xstrtof(const char *str, float *out)
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

/* vim: set noet ts=2 sw=2: */
