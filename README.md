plan9-bcm
=========

Plan 9 BCM

examples
========
echo scheme bcm|board|wpi > ctl
echo 0 > GPIO0
echo 0 > OK
echo function in GPIO0 > ctl
echo function out GPIO0 > ctl
echo function 1 GPIO0 > ctl (… 2 3 4 5, refer to BCM manual)
echo event edge-rising enable GPIO0 > ctl
echo event edge-rising disable GPIO0 > ctl
echo event edge-falling enable GPIO0 > ctl
echo pull up GPIO0 > ctl
echo pull down GPIO0 > ctl
cat /dev/gpio/event
