#Welcome to my fork of pulsemix

You can find the original code at https://github.com/falconindy/pulsemix
and credits goes to falconindy for making the base.

this fork focus on implenting the functionality pavucontrol has, per application volume mixing and sink/source controlling.

###Usage

* pulsemix [options] [command]..  
* and if no options are given it will control the default sink/output

* Options:
    * -h, --help,          
    * -s, --stream [index]
    * -o, --source

* Commands:
    * list
    * list-sources
    * list-streams
    * get-volume
    * set-volume VALUE
    * get-balance
    * set-balance VALUE
       * to set balance for sink, pass double -- before the negative number ex( -- -0.5) range is between -1.0 to 1.0 and 0 being centered
    * increase VALUE
    * decrease VALUE
    * mute
    * unmute
    * toggle

###Feature completeness

All features are implemented up to a certain degree where some work very well and some are untested (but probably can be easily made fit, once tested).
find any bugs or non working features please report or if you are bored code together a patch and poke me ;).
