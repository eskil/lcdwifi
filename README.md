lcdwifi
=======

![Screenshot](https://raw.github.com/eskil/lcdwifi/master/lcdwifi.jpg)

Old piece of code for display wifi signal on a VFD 2x16 display.

What does it do ? It shows the signal strength in one line, and on the
second line, a graph with all the previous readings and the tx/rx
load.

My roommates and I build an mp3 player box for the living room, and
used a wifi link for the box. Of course, since the box is under the TV
surrounded by cables as far from the wifi as possible, the link
quality is crap. So I wupped up this little piece of code so that we
could see how the link was doing while transferring music to the box.

Major drawbacks
===============

* Only reads wifi cards bit rate at start time, so the tx/rx load doesn't account for changes in the link speed.  
* Not at all tested for anything but 16x2 lines displays with 5x8 characters.
