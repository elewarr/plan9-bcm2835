plan9-bcm
=========

Plan 9 BCM

examples
========
```
cd '#G'/gpio
echo scheme bcm > ctl (... board, wpi)
echo 0 > GPIO0
echo 0 > OK
echo function in GPIO0 > ctl
echo function out GPIO0 > ctl
echo function 1 GPIO0 > ctl (... 2 3 4 5, refer to BCM manual)
echo event edge-rising enable GPIO0 > ctl
echo event edge-rising disable GPIO0 > ctl
echo event edge-falling enable GPIO0 > ctl
echo pull up GPIO0 > ctl
echo pull down GPIO0 > ctl
cat event
cat GPIO0
```

schemes
=======
```
cpu% echo scheme bcm > ctl
cpu% lc
1		11		15		18		21		23		25		7		9		ctl
10		14		17		2		22		24		4		8		OK		event
cpu% 
```

```
cpu% echo scheme board > ctl
cpu% lc
CE0		GPIO0	GPIO2	GPIO4	GPIO6	MISO	OK		SCL		SDA		ctl
CE1		GPIO1	GPIO3	GPIO5	GPIO7	MOSI	RxD		SCLK	TxD		event
cpu% 
```

```
cpu% echo scheme wpi > ctl
cpu% lc
0		10		12		14		16		3		5		7		9		ctl
1		11		13		15		2		4		6		8		OK		event
cpu% 
```
