Test release of PiDP-8 software     DISCARD & DELETE AFTER JULY 1, 2015
=======================================================================

Please read carefully... 
- use install script only on a spare SD card with fresh Raspbian, 
  just to avoid any risk.
- test version. Meaning please provide feedback and expect imperfection.


This test release should run on any Raspberry Pi, even without the PiDP-8 front panel hardware. Of course you can't use the front panel switch commands. Without the front panel, it's just a version of simh.


A. The pidp8 program itself
---------------------------

1 - Unpack into /opt/pidp8
2 - cd /opt/pidp8/src, sudo make
3 - cd /opt/pidp8/src/scanswitch, sudo make

You should be able to run /opt/pidp8/bin/pidp8 after you have disabled the Raspberry Pi's GPIO serial port. The install script below will do that for you, or you can do it manually (google on how to). 
In short: 'sudo /opt/pidp8/bin/pidp8 /opt/pidp8/bootscripts/0.script' would start you up with OS/8. Pidp8 needs sudo, as it accesses the gpio ports.


B. The install script
---------------------

IMPORTANT: although the install script below is preliminary, the pidp8 program itself is pretty much final. 
Running the install script:

4 - cd /opt/pidp8/install, sudo ./pidp8-setup.sh

The install script:
- installs packages usbmount and screen (useful but not required to run pidp8)
- disables the Pi's GPIO serial port (required for standard version of pidp8)
- inserts pidp8 into the boot process of the Pi,
  making use of the screen command to make it a persistent process.

Please refer to screen's documentation. You'll need to know about Ctrl-A d, and other basic commands of screen. Using screen allows you to connect to the pidp from any terminal. The pdp.sh script does this for you at login.

Regarding the install script, this is very much a preliminary version! I'm far from being a Linux sysadmin expert, but Mark Thomas kindly suggested how to install & operate the pidp8 program under screen. The elegant script bits are his, the rest (you'll see) is mine.

This *temporary* version is released to let you try pidp8, and to sollicit suggestions on how to improve the install process from its current messy state.


Feedback requested!

Oscar
oscar.vermeulen@hotmail.com

