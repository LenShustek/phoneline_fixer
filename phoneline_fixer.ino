/* ----------------------------------------------------------------------------
   This is the software for a very strange box that solves a very strange
   problem with my Panasonic KX-T30810 PBX phone switch.

   When AT&T changed one of our three Central Office lines, it didn't work
   with my venerable 30-year-old Panasonic 3x8 phone switch. There was no
   dial tone, no matter which of the CO ports on the switch it was plugged
   into. But the new line works fine with a regular phone, so of course the
   AT&T technician said there's nothing wrong, and closed the service call
   with "no trouble found".

   After much experimentation, and an online discussion at
   https://sundance-communications.com/forum/ubbthreads.php/topics/638453/1
   with a group of experienced phone installers, the diagnosis was that
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
   it reconnects the switch to the CO line and removes the shunt. It works!

   The box contains:
   - two RJ11 connectors, a USB power connector, and a ground/earth connector
   - polarity-reversing switches for both ports
   - voltage measurement circuits for the PBX Tip and Ring lines
   - solid-state relays to connect the PBX Ring to the CO Ring, and to apply
     an off-hook holding shunt to the CO line
   - an Arduino Nano microprocessor; use processor "ATmega328P (Old Bootloader)"

   Len Shustek September 29, 2020

   ------ Change log ---------

   29 Sep 2020, L. Shustek, V1.0, first version

   ----------------------------------------------------------------------------------------------------
   Copyright (c) 2020 Len Shustek; released under The MIT License (MIT)

   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
   associated documentation files (the "Software"), to deal in the Software without restriction,
   including without limitation the rights to use, copy, modify, merge, publish, distribute,
   sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all copies or
   substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
   NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
   -------------------------------------------------------------------------------------------------*/

#define DEBUG 0  // level 0 (none), 1 (a little), or 2 (a lot)
// Note that the debugging output screws up timing, so things don't work exactly right.
// Also: use puTTY in VT100 mode to view it, not the Arduino IDE Serial Monitor.

#define TEST_MOSFETS false

#define CURSOR_UP3 "\e[3A" // these cursor movements work for puTTY VT100, 
#define CURSOR_UP2 "\e[2A" // but not for Arduino's Serial Monitor
#define CURSOR_UP1 "\e[1A" // 

#define PBX_LINE_RING A1     // ring, red, -48V on hook
#define PBX_LINE_TIP  A0     // tip, green, close to ground on hook
#define DISCONNECT_SHUNT 4   // high to disconnect
#define DISCONNECT_CO_PBX 3  // high to disconnect
#define LED 13               // built-in LED on the Nano board

#define MAXSAMPLES 10
#define SAMPLE_INTERVAL 5 // msec

enum line_state_t {ON_HOOK, OFF_HOOK, RINGING } last_linestate;
char const *line_state_name [] = {"on hook", "off hook", "ringing" };

static struct pbx_line_t { //**** info about each PBX line 
   char const *name;  // Tip or Ring
   byte port; // which port reads that analog voltage value
   short int nextsample;
   int samples[MAXSAMPLES]; } // the array of previous samples}
pbx_tip = {"Tip", PBX_LINE_TIP, 0 },
pbx_ring = {"Ring", PBX_LINE_RING, 0 };
static unsigned long count = 0;

// The scaling and biasing circuits were inspired by this paper: http://www.symres.com/files/scalebias.pdf
#define Vref 4.9
#define Vbias 4.9
#define RA 108E3
#define RB 7.15E3
#define RC 158E3

int read_voltage(struct pbx_line_t *p) {
   uint16_t aval = analogRead(p->port);
   float Vout = (float)aval * Vref / 1024;
   float Vin = (Vout * (1 / RA + 1 / RB + 1 / RC) - Vbias / RB) * RA;
   ++count;
   #if 0 && DEBUG
   Serial.print(count);
   Serial.print(": ADC "); Serial.print(aval);
   Serial.print(", Vin "); Serial.print(Vin);
   Serial.print(", Vout "); Serial.print(Vout);
   Serial.println();
   #endif
   int V = Vin >= 0 ? (int) (Vin + 0.5) : (int) (Vin - 0.5); //rounded volts
   p->samples[p->nextsample] = V;
   if (++p->nextsample >= MAXSAMPLES) p->nextsample = 0;
   return V; }

void show_samples(struct pbx_line_t *p) {
   Serial.print(count); Serial.print(", ");
   Serial.print(p->name); Serial.print(": ");
   int i = p->nextsample;
   do {
      Serial.print(p->samples[i]);
      Serial.print(" ");
      if (++i >= MAXSAMPLES) i = 0; }
   while (i != p->nextsample);
   Serial.println("                ");
   Serial.flush(); }

enum line_state_t newlinestate(void) {
   /****** the state rules, based on the voltage we see on the PBX lines...
      Ring between -40 and -60, and Tip between -10 and +10 for 50 msec: on hook
      Ring between -15 and -40, and Tip between -10 and -30 for 50 msec: off hook
      Ring below -70 and Tip above -10 for at least 2 samples: ringing
      otherwise: keep current state  */
   bool onhook = true, offhook = true;
   int ringing_samples = 0;
   for (int i = 0; i < MAXSAMPLES; ++i) {
      int v_ring = pbx_ring.samples[i];
      int v_tip = pbx_tip.samples[i];
      if (v_ring > -40 || v_ring < -60 || v_tip > 10 || v_tip < -10) onhook = false;
      if (v_ring > -15 || v_ring < -40 || v_tip > -10 || v_tip < -30) offhook = false;
      if (v_ring < -70 && v_tip > -10) ++ringing_samples; }
   enum line_state_t new_linestate;
   if (onhook) new_linestate = ON_HOOK;
   else if (offhook) new_linestate = OFF_HOOK;
   else if (ringing_samples >= 2) new_linestate = RINGING;
   else new_linestate = last_linestate;
   return new_linestate; }

void setup(void) {
   #if DEBUG
   delay(1000);
   Serial.begin(57600);
   while (!Serial) ;
   Serial.println("phone fixer started");
   #endif
   pinMode(PBX_LINE_RING, INPUT);
   pinMode(PBX_LINE_TIP, INPUT);
   pinMode(DISCONNECT_SHUNT, OUTPUT);
   pinMode(DISCONNECT_CO_PBX, OUTPUT);
   pinMode(LED, OUTPUT);
   digitalWrite(LED, LOW);
   #if TEST_MOSFETS
   while (1) { // use an analog multimeter to check for connectivity
      digitalWrite(DISCONNECT_SHUNT, true);
      digitalWrite(DISCONNECT_CO_PBX, true);
      delay(1000);
      digitalWrite(LED, HIGH);
      digitalWrite(DISCONNECT_SHUNT, false);
      digitalWrite(DISCONNECT_CO_PBX, false);
      delay(1000);
      digitalWrite(LED, LOW); }
   #endif
   digitalWrite(DISCONNECT_SHUNT, true);    // shunt is out
   digitalWrite(DISCONNECT_CO_PBX, false);  // CO and PBX are connected
   for (int i = 0; i < MAXSAMPLES; ++i) {   // initial fill of the sample arrays
      read_voltage(&pbx_ring);
      read_voltage(&pbx_tip); }
   last_linestate = ON_HOOK; }

void loop(void) {
   enum line_state_t new_linestate;
   read_voltage(&pbx_ring);    // sample the PBX line voltages
   read_voltage(&pbx_tip);
   #if DEBUG >=2
   Serial.print('\r'); Serial.print(CURSOR_UP2);
   show_samples(&pbx_ring);
   show_samples(&pbx_tip);
   #endif
   new_linestate = newlinestate(); 
   if (new_linestate != last_linestate) { // the line state has changed
      #if DEBUG >= 1
      Serial.println(); Serial.println(); Serial.println();
      Serial.print("new state: "); Serial.println(line_state_name[new_linestate]);
      Serial.println(); Serial.flush();
      #endif
      // if on hook then off hook, apply shunt, wait 600 msec, then remove
      if (last_linestate == ON_HOOK && new_linestate == OFF_HOOK) {
         digitalWrite(LED, HIGH);
         digitalWrite(DISCONNECT_SHUNT, false); // put CO shunt in
         digitalWrite(DISCONNECT_CO_PBX, true); // disconnect CO and PBX
         #if DEBUG >=1
         Serial.println("line isolated");
         #endif
         delay(600);
         digitalWrite(DISCONNECT_CO_PBX, false); // connect CO and PBX
         digitalWrite(DISCONNECT_SHUNT, true); // take CO shunt out
         digitalWrite(LED, LOW);
         #if DEBUG >= 1
         Serial.println("line connected");
         Serial.println(); Serial.println(); Serial.println(); Serial.flush();
         #endif
      }
      last_linestate = new_linestate; }
   delay(SAMPLE_INTERVAL); }
//*
