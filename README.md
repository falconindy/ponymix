#ponymix - command line control for PulseAudio

ponymix is a command line program to control devices and audio streams.

##Installation

ponymix is built and installed via make:

	$ make
	# make install

Note that ponymix installs into /usr by default; to change this, edit the
Makefile.

##Usage

ponymix's basic use syntax is *ponymix [options] operation [arguments]*.
Complete details are available in the man page, ponymix(1).

ponymix has two major modes, *device* and *application*, selected by the
options -d (--device) and -a (--app) respectively; ponymix operates in device
mode by default.  When invoked without an operation, ponymix in device mode
displays the numeric index, sink/source name, short name, and volume of the
default sink and source; in application mode, it displays the numeric index,
stream name, stream originating program, and volume.  Note that both ponymix
and PulseAudio refer to hardware inputs as sources and hardware outputs as
sinks; ponymix refers to application streams as either inputs or outputs.

Device Mode:
```
sink 0: alsa_output.pci-0000_00_1b.0.analog-stereo
Built-in Audio Analog Stereo
Avg. Volume: 48%
```

Application Mode:
```
output 335: Pulse to tsukikage
Music Player Daemon
Avg. Volume: 72%
```

When invoked with no operations, options, or arguments, ponymix operates in
device mode and displays the information of the default sink (hardware output)
and source (hardware input), equivalent to:

	ponymix -d defaults

ponymix has a wide range of operations; for instance, to see all hardware
sources and sinks, one can use:
	
	ponymix list

If one wanted to see all active application streams, they would instead use:

	ponymix -a list

ponymix is able to manipulate sources, sinks, and streams, e.g.:
	
	ponymix increase 4

This increases the volume of the default sink by 4%; *decrease* would decrease
the volume, and *toggle* would toggle the mute state.  ponymix can also adjust
the balance of the sink or source.  When performing any increase, decrease, or
set operation, ponymix returns the new value on stdout.

---

In order to examine or control specific sources, sinks, and streams, one may
use the minor modes *output* or *input*, selected by the options -o (--output)
and -i (--input) respectively.  These modes allow you to perform several useful
functions; for instance, if you wanted to change your default output sound
card, you could simply use the *set-default* operation: 

	ponymix -o set-default 2

This sets the default output to the sink with numeric index 2.  In order to
move your applications, you would use the *move* operation:

	ponymix -ao move 31 2

This would move the output stream with numeric index 31 to the sink with
numeric index 2.  -o or -i must be specified in either case.

##Licensing
ponymix is provided under the MIT license.
