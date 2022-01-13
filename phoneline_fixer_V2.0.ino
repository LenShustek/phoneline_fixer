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
   that CO line and the phone switch and has variously implemented different
   strategies to fix the problem:

   (1) When the switch takes the line off-hook, the box quickly
   breaks the connection to the CO line and simultaneously puts
   a 500 ohm shunt resistor across the line to maintain the
   off-hook condition. It waits 600 milliseconds for the switch to do
   whatever it does that the CO is interpreting as a dialed digit "1", then
   it reconnects the switch to the CO line and removes the shunt.
  
   This worked for a couple of years, until AT&T changed to line to some other
   "fake copper pair" digital simulation (PG? DPD?), which seems to interpret 
   reconnecting the PBX as enough noise to signal a "1". So we switched to a 
   second strategy.

   (2) When the switch takes the line off-hook, the box quickly puts a
   33uf 450V capacitor (in parallel with a 33K bleed resistor)
   across the tip and ring lines, supressing whatever funny thing
   the phone switch is doing that is interpreted as a pulse-dialed "1".
   It waits 750 msec and then removes the capacitor.
   
   We also changed to computing the voltage across tip and ring by 
   subtracting the voltages as measured relative to ground. That makes us
   independent from the ground reference of the CO loop, which has changed.
   
   This scheme now works -- but we'll see for how long!

   The box contains:
   - two RJ11 connectors, a USB power connector, and a ground/earth connector
   - polarity-reversing switches for both ports
   - voltage measurement circuits for the PBX Tip and Ring lines
   - solid-state relays to connect the PBX Ring to the CO Ring, and to apply
      an off-hook holding shunt (or capacitor) to the CO line
   - an Arduino Nano microprocessor; use processor "ATmega328P (Old Bootloader)"

   Len Shustek September 29, 2020

   ------ Change log ---------

   29 Sep 2020, L. Shustek, V1.0, first version
   12 Jan 2022, L. Shustek, V2.0, Redo for our new CO line=, using strategy 2.

   ----------------------------------------------------------------------------------------------------
   Copyright (c) 2020,2022 Len Shustek; released under The MIT License (MIT)

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

#define DEBUG 0 // level 0 (none), 1 (a little), or 2 (a lot)
// Note that debugging output level 2 screws up timing, so things don't work exactly right.
// Also: use puTTY in VT100 mode to view it, not the Arduino IDE Serial Monitor.

#define TEST_MOSFETS false

#define CURSOR_UP3 "\e[3A" // these cursor movements work for puTTY VT100, 
#define CURSOR_UP2 "\e[2A" // but not for Arduino's Serial Monitor
#define CURSOR_UP1 "\e[1A" // 

#define PBX_LINE_RING A1     // ring, red, -48V on hook
#define PBX_LINE_TIP  A0     // tip, green, close to ground on hook
#define DISCONNECT_SHUNT 4   // high to disconnect
#define DISCONNECT_CO_PBX_RING 3  // high to disconnect
#define DISCONNECT_CO_PBX_TIP 5   // high to disconnect
#define LED 13               // built-in LED on the Nano board

#define SAMPLE_INTERVAL 5 // msec
#define NUM_SAMPLES (50/SAMPLE_INTERVAL)   // 50 msec of samples to determine line state 
//                                            (enough for one 20 Hz ring cycle)
#define NUM_HISTORY (2000/SAMPLE_INTERVAL) // 2 seconds of history for debugging

enum line_state_t {ON_HOOK, OFF_HOOK, RINGING } last_linestate;
char const *line_state_name [] = {"on hook", "off hook", "ringing" };

static struct pbx_line_t { //**** info about each PBX line
   char const *name;  // Tip or Ring
   byte port; // which port reads that analog voltage value
   short int nextslot;
   int8_t history[NUM_HISTORY]; } // the array of many previous samples}
pbx_tip = {"Tip", PBX_LINE_TIP, 0 },
pbx_ring = {"Ring", PBX_LINE_RING, 0 };
static unsigned long count = 0;
bool connected = false, shunted = false;;

// The scaling and biasing circuits that convert -60V to +60V tip and ring voltages
// to the 0-5V range that the Arduino can handle were inspired by this excellent paper: 
// http://www.symres.com/files/scalebias.pdf

#define Vref 5.0
#define Vbias 5.0
#define RA 100.0E3
#define RB 8.2E3
#define RC 9.1E3

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
   p->history[p->nextslot] = V;
   if (++p->nextslot >= NUM_HISTORY) p->nextslot = 0;
   return V; }

void show_samples(struct pbx_line_t *p) {
   Serial.print(count); Serial.print(", ");
   Serial.print(p->name); Serial.print(": ");
   int ndx = p->nextslot - NUM_SAMPLES;
   if (ndx < 0) ndx += NUM_HISTORY;
   do {
      Serial.print(p->history[ndx]);
      Serial.print(" ");
      if (++ndx >= NUM_HISTORY) ndx = 0; }
   while (ndx != p->nextslot);
   Serial.println("                ");
   Serial.flush(); }

void show_history(struct pbx_line_t *p) {
   Serial.print(p->name); Serial.print(" history: ");
   int ndx = p->nextslot;
   int cnt = 0;
   do {
      Serial.print(p->history[ndx]);
      Serial.print(' ');
      if (++cnt > 20) {
         Serial.println(); Serial.print("  ");
         cnt = 0; }
      if (++ndx >= NUM_HISTORY) ndx = 0; }
   while (ndx != p->nextslot);
   Serial.println(); }

enum line_state_t getlinestate(void) {
   /****** the line state rules, based on the voltage we see on the PBX lines *******
      This new version bases the decision only on the difference between tip
      and ring as measured relative to ground, which is the voltage across the line.
      We expect the polarity to be such that tip is more positive than ring when on hook.
        ** between 35 and 60 for the entire sample interval: on hook
        ** between -10 and 20 for the entire sample interval: off hook
        ** greater than 70 or less than -70 for at least 2 samples: ringing (which is 20 Hz AC)
      otherwise we keep the current state  */
   bool onhook = true, offhook = true;
   int ringing_samples = 0;
   int ndx = pbx_ring.nextslot;  // oldest sample (same for both lines); newest is previous one
   for (int i = 0; i < NUM_SAMPLES; ++i) { // look at the most recent NUM_SAMPLES voltage samples
      if (--ndx < 0) ndx = NUM_HISTORY - 1; // go to previous sample
      int vdelta = pbx_tip.history[ndx] - pbx_ring.history[ndx];
      if (vdelta < 35 || vdelta > 60 ) onhook = false;
      if (vdelta < -10 || vdelta > 20 ) offhook = false;
      if (vdelta < -70 || vdelta > 70) ++ringing_samples; }
   enum line_state_t current_linestate;
   if (onhook) current_linestate = ON_HOOK;
   else if (offhook) current_linestate = OFF_HOOK;
   else if (ringing_samples >= 2) current_linestate = RINGING;
   else current_linestate = last_linestate;
   return current_linestate; }

void do_connect(bool connect) {
   digitalWrite(DISCONNECT_CO_PBX_TIP, !connect);
   digitalWrite(DISCONNECT_CO_PBX_RING, !connect);
   connected = connect; }

void do_shunt(bool put_shunt_in) { // resistive shunt, or a capacitor
   digitalWrite(DISCONNECT_SHUNT, !put_shunt_in);
   shunted = put_shunt_in; }

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
   pinMode(DISCONNECT_CO_PBX_TIP, OUTPUT);
   pinMode(DISCONNECT_CO_PBX_RING, OUTPUT);
   pinMode(LED, OUTPUT);
   digitalWrite(LED, LOW);
   #if TEST_MOSFETS
   while (1) { // test code for an analog multimeter check
      do_shunt(false)// CO and PBX connected, no shunt
      do_connect(true);
      delay(2000);
      digitalWrite(LED, HIGH);
      do_shunt(true); // CO and PBX isolated, shunt on CO
      do_connect(false);
      delay(2000);
      digitalWrite(LED, LOW); }
   #endif
   do_shunt(false);    // the capacitor is out
   do_connect(true);   // CO and PBX are (and in this version, always stay) connected
   for (int i = 0; i < NUM_HISTORY; ++i) {   // initial fill of the sample arrays
      read_voltage(&pbx_ring);
      read_voltage(&pbx_tip); }
   last_linestate = ON_HOOK;
   #if DEBUG
   Serial.println("ready");
   #endif
}

void do_disconnect(void) {  // unused in this version
   digitalWrite(LED, HIGH);
   do_shunt(true);  // put CO shunt in
   do_connect(false); // disconnect CO and PBX
   if (DEBUG >= 1) Serial.println("line disconnected");
   delay(750);
   do_connect(true);; // connect CO and PBX
   do_shunt(false);; // take CO shunt out
   digitalWrite(LED, LOW);
   #if DEBUG >= 1
   Serial.println("line reconnected");
   Serial.println(); Serial.println(); Serial.println(); Serial.flush();
   #endif
}

void loop_bad(void) {  // experiments with techniques that didn't work
   if (Serial.available()) {
      char ch = Serial.read();
      if (ch == ' ') { // alternate connect/disconnect
         do_connect(!connected);
         Serial.println(connected ? "connected" : "disconnected"); }
      if (ch == 's') { // alternate shunt in/out
         do_shunt(!shunted);
         Serial.println(shunted ? "shunt is in" : "shunt is out"); }
      if (ch == 't') { // test sequence, starting with everything disconnected
         // assume PBX phone is taken off-hook first
         do_shunt(true);
         delay(600);
         //do_connect(true);
         //delay(50);
         //do_connect(false);
         //delay(100);
         do_connect(true);
         do_shunt(false);
         Serial.println("test done; remember to disconnect"); } } }

void loop(void) {  // the strategy that works
   enum line_state_t new_linestate;
   read_voltage(&pbx_ring);    // sample the PBX line voltages
   read_voltage(&pbx_tip);
   if (DEBUG >= 2) {
      Serial.print('\r'); Serial.print(CURSOR_UP2);
      show_samples(&pbx_ring);
      show_samples(&pbx_tip); }
   new_linestate = getlinestate();
   if (0) {
      static unsigned long shown = 0;
      if (millis() - shown > 3 * 1000) { // show line state every 3 seconds
         Serial.print("current state: "); Serial.println(line_state_name[new_linestate]);
         shown = millis(); } }
   if (new_linestate != last_linestate) { // the line state has changed
      if (DEBUG >= 1) {
         if (DEBUG >= 2) {
            Serial.println(); Serial.println(); Serial.println(); }
         Serial.print("new state: "); Serial.println(line_state_name[new_linestate]);
         if (DEBUG >= 2) Serial.println();
         Serial.flush(); }
      // if on hook then off hook: apply capacitor, wait a while, then remove it
      if (last_linestate == ON_HOOK && new_linestate == OFF_HOOK) {
         digitalWrite(LED, HIGH);
         do_shunt(true);  // put capacitor in
         if (DEBUG) Serial.println("putting cap in");
         delay(750);
         do_shunt(false);; // take capacitor out
         digitalWrite(LED, LOW);
         #if DEBUG
         show_history(&pbx_ring);
         show_history(&pbx_tip);
         #endif
      }
      last_linestate = new_linestate; }
   if (DEBUG) {
      if (Serial.available() && Serial.read() == 'h') {
         show_history(&pbx_ring);
         show_history(&pbx_tip); } }
   delay(SAMPLE_INTERVAL); }
//*
