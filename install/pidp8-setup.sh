#!/bin/sh
#
# Run this script once to configure Raspberry Pi for operation with pidp8.
#  -Disables serial port.
#  -Installs screen utility for detached emulator operation.
#
# Tested with 2015-02-16-raspbian-wheezy
#
# Author: Mark G Thomas <mark@misty.com> 2015-05-09
#
# References:  http://www.hobbytronics.co.uk/raspberry-pi-serial-port
#
PATH=/usr/sbin:/usr/bin:/sbin:/bin

# Disable serial port getty by commenting out any lines referencing ttyAMA0.
#
test ! -f /etc/inittab.foo && cp -p /etc/inittab /etc/inittab.foo
sed -e '/ttyAMA0/ s/^#*/#/' -i /etc/inittab
telinit q

# Disable serial port bootup info.
#
test ! -f /boot/cmdline.txt_orig && cp -p /boot/cmdline.txt /boot/cmdline.txt_orig
sed -e 's/console\=ttyAMA0,[0-9]* //' -e 's/kgdboc\=ttyAMA0,[0-9]* //' -i /tmp/foo

# Install usbmount utility
#
test ! -f /etc/usbmount/usbmount.conf && aptitude install usbmount

# Install screen utility if not already installed.
#
test ! -x /usr/bin/screen && aptitude install screen

# Set up pidp8 init script, provided pidp8 is installed in /opt/pidp8.
#
if [ ! -x /opt/pidp8/etc/rc.pidp8 ] || [ ! -x /opt/pidp8/bin/pidp8 ]; then
	Skipping init script setup, /opt/pidp8/etc/rc.pidp8 or /opt/pidp8/bin/pidp8 not found.
else
	ln -s /opt/pidp8/etc/rc.pidp8 /etc/init.d/pidp8
	update-rc.d pidp8 defaults
fi

# set up 'pdp' in home directory
#
ln -s /opt/pidp8/etc/pdp.sh /home/pi/pdp.sh

# add pdp.sh to end of pi's .profile 
#
test ! -f /home/pi/.profile.foo && cp -p /home/pi/.profile /home/pi/.profile.foo
sed -e "\$a/home/pi/pdp.sh" -i /home/pi/.profile

