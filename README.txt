This is a very strange box that solves a very strange problem with my 
Panasonic KX-T30810 PBX phone switch. 

When AT&T changed one of our three Central Office lines, it didn't work 
with my venerable 30-year-old Panasonic 3x8 phone switch. There was no 
dial tone, no matter which of the CO ports on the switch it was plugged 
into. But the new line works fine with a regular phone, so of course the 
AT&T technician said there's nothing wrong, and closed the service call 
with "no trouble found". 

After much experimentation, and an online discussion at 
https://sundance-communications.com/forum/ubbthreads.php/topics/638453/1 
with an experienced group of phone installers, the diagnosis was that 
something the phone switch is doing just after the phone goes off-hook 
is being interpreted on that new CO line -- and *only* on that CO line 
-- as a "1" being dialed. That stops the dial tone, and any phone number 
dialed after that elicits the error message "You don't need to dial a 1 
to reach that number." 

Rather than try to beg/demand a new line from AT&T that is different in 
some way I can't describe to them, I built a small box that sits between 
the CO line and the phone switch. When the switch takes the line 
off-hook, the box quickly breaks the connection to the CO line and 
simultaneously puts a 500 ohm shunt resistor across the line to maintain 
the off-hook condition. It waits 600 milliseconds for the switch to do 
whatever it does that the CO is interpreting as a dialed digit "1", then 
it reconnects the switch to the CO line and removes the shunt. 

It works! This repository contains the hardware and software design. 
Note that as of this writing the printed circuit board is not yet made, 
and I'm operating with the hand-wired prototype. I'll update this when 
the board is fabricated, built, and working. 

If anyone else has had this problem, or has any insight into what the 
switch is doing "wrong" and why it only causes a problem on one of the 
CO lines, I'd love to hear about it. 

Len Shustek September 29, 2020 

