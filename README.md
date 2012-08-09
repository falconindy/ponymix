#Welcome to my fork of pulsemix

You can find the original code at https://github.com/falconindy/pulsemix
and credits goes to falconindy for making the base.

This fork focus on implementing the functionality pavucontrol has, per
application volume mixing and sink/source controlling.

###Usage

```
Usage: pulsemix [options] <command>...

 Options:
  -h, --help,           display this help and exit
  -a  --application=id  control a application
  -i, --input=id        control the input device (if no id given use default set)
  -o, --output=id       control the output device (if no id given use default set)

 Commands:
  list-defaults         list default set input/output devices
  list-applications     list available applications
  list-inputs           list available input devices
  list-outputs          list available output devices
  set-default           sets the selected input/output as default
  get-volume            get volume
  set-volume VALUE      set volume
  get-balance           get balance for output
  set-balance VALUE     set balance for output, pass double between -1.0 to 1.0
  increase VALUE        increase volume
  decrease VALUE        decrease volume
  mute                  mute
  unmute                unmute
  is-muted              check if muted
  toggle                toggle mute
```

###Feature completeness

All features are implemented up to a certain degree where some work very
well and some are untested (but probably can be easily made fit, once
tested). Find any bugs or non working features please report or if you
are bored code together a patch and poke me ;).
