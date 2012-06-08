#Welcome to my fork of pulsemix

You can find the original code at https://github.com/falconindy/pulsemix
and credits goes to falconindy for making the base.

this fork focus on implenting the functionality pavucontrol has, per application volume mixing and sink/source controlling.

###Usage

pulsemix [options] <command>...

Options:
 -h, --help,          display this help and exit
 -s, --stream <index> control a stream instead of the sink itself
 -o, --source         control the default set source/input device

Commands:
  list               list available sinks/output devices
  list-sources       list available sources/input devices
  list-streams       list available streams/applications
  get-volume         get volume
  set-volume VALUE   set volume
  get-balance        get balance for sink
  set-balance VALUE  set balance for sink, pass double -- before the negative number ex( -- -0.5) 
                     range is between -1.0 to 1.0 and 0 being centered
  increase VALUE     increase volume
  decrease VALUE     decrease volume
  mute               mute active sink or stream
  unmute             unmute active sink or stream
  toggle             toggle mute

###Feature completeness

All features are implemented up to a certain degree where some work very well and some are untested (but probably can be easily made fit, once tested).
find any bugs or non working features please report or if you are bored code together a patch and poke me ;).
