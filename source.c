/*
Calcuclock firmware v0.1 - Charlie Bruce, 2013


TECH NOTES:

Uses timer2 for 32.768khz timekeeping ("real time")
Uses timer0 for delay, delayMicrosecond, as Arduino does.
uses timer1 as display update, approximately once per millisecond.
When it hasn't been pressed for a while it goes into a very deep sleep - only C/CE/ON can wake it.
In deep sleep, nothing is running.

Brown-out detection is off in sleep, on when running? Or do we use the ADC to check bvattery level?
WDT disabled.




 */

#include <avr/sleep.h>  //Needed for sleep_mode
#include <avr/power.h>  //Needed for powering down perihperals such as the ADC/TWI and Timers
#include <stdint.h>     //Needed for uint8_t, suits Charlie's coding style better
#include <util/delay.h> //Needed for small delays such as when showing LEDs

//The state of each segment (A..DP for devices left-right).
volatile uint8_t segstates[6];

//Pin numbers for the 7-segment display
const uint8_t segs[8] = {8,9,10,11,12,13,6,7};
const uint8_t cols[6] = {4,5,A2,A3,A4,A5};

const uint8_t numbersToSegments[11] =
{
		/*0*/0b00111111,
		/*1*/0b00000110,
		/*2*/0b01011011,
		/*3*/0b01001111,
		/*4*/0b01100110,
		/*5*/0b01101101,
		/*6*/0b01111101,
		/*7*/0b00000111,
		/*8*/0b01111111,
		/*9*/0b01100111,   //Note: If you prefer curly 9s, use 0b01101111 instead
		/*BLANK*/0
};

//Input buttons
const uint8_t ceButton = 2;
const uint8_t btnsA = A0;
const uint8_t btnsB = A1;

#define SEGMENT_OFF HIGH
#define SEGMENT_ON LOW

#define COLUMN_OFF LOW
#define COLUMN_ON HIGH

//preload timer with 65535 - 4000 - 4,000 cycles at 8mhz is 2khz.
#define PWM_TIME (65535-4000)

#define SUNDAY 0
#define MONDAY 1
#define TUESDAY 2
#define MWEDNESDAY 3
#define THURSDAY 4
#define FRIDAY 5
#define SATURDAY 6

//Time variables - GMT
volatile uint8_t seconds = 30;
volatile uint8_t minutes = 59;
volatile uint8_t hours = 22;

//Date variables - GMT
volatile int year = 2013;
volatile int month = 7;
volatile int day = 26;

//Timezone - 0 is GMT, 1 is BST
//where 1 means that the displayed time is one hour greater than GMT
uint8_t timezone = 0;
void setup(){

	//All inputs, no pullups
	for(int x=1;x<18;x++){
		pinMode(x, INPUT);
		digitalWrite(x, LOW);
	}

	pinMode(ceButton, INPUT); //C/CE/ON button
	digitalWrite(ceButton, HIGH); //Internal pull-up on this button.

	//Set up the 7-segment display pins as outputs.
	for(uint8_t i=0;i<8;i++){
		pinMode(segs[i], OUTPUT);
		digitalWrite(segs[i], SEGMENT_OFF);
	}
	for(uint8_t i=0;i<6;i++){
		pinMode(cols[i], OUTPUT);
	}

	//Turn off unused hardware on the chip to save power.
	power_twi_disable();
	power_spi_disable();

	//  power_usart0_disable();
	//Debug only - remove these in the final version.
	Serial.begin(9600);

	//Set up timer 1 - display update
	TCCR1A = 0;
	TCCR1B = 0;

	TCNT1 = PWM_TIME;
	TCCR1B |= (1 << CS10);    //no prescaler

	//  TCNT1 = 62410;            //10hz
	//  TCCR1B |= (1 << CS12);    //256 prescaler

	TIMSK1 |= (1 << TOIE1);   //enable timer overflow interrupt

	//Set up timer 2 - real time clock
	TCCR2A = 0;
	TCCR2B = (1<<CS22)|(1<<CS20); //1-second resolution
	//TCCR2B = (1<<CS22)|(1<<CS21)|(1<<CS20); //8-second resolution saves power.
	ASSR = (1<<AS2); //Enable asynchronous operation
	TIMSK2 = (1<<TOIE2); //Enable the timer 2 interrupt

	//Interrupt when CE pressed
	EICRA = (1<<ISC01); //falling edge (button press not release)
	EIMSK = (1<<INT0); //Enable the interrupt INT0



	//Enable interrupts
	sei();


}

void loop() {

    
        //Accounta for timezone (tzc_ means timezone-corrected)
        //Seconds, minutes never change between timezones, only hours/days/months
        //This only allows for positive timezone change of (timezone) hours WRT. GMT. Wouldn't try this over more than a 23 hour shift
        
        uint8_t tzc_hours = hours + timezone;
        uint8_t tzc_days = day;
        uint8_t tzc_month = month;
        int tzc_year = year;
        
        if (tzc_hours >= 24) 
        {
          tzc_hours = tzc_hours % 24;
          tzc_days++;
        
          if(tzc_days > daysInMonth(tzc_year, tzc_month)) {
            tzc_days = 1;
            tzc_month++; 
            
            if(tzc_month == 13){
               tzc_month = 1;
               tzc_year++;
            }
          }  
        }
        
        
        
        
	segstates[0] = numbersToSegments[(tzc_days/10)%10];
	segstates[1] = numbersToSegments[tzc_days%10]|(1<<7);
	segstates[2] = numbersToSegments[(tzc_month/10)%10];
	segstates[3] = numbersToSegments[tzc_month%10]|(1<<7);
	segstates[4] = numbersToSegments[((tzc_year-2000)/10)%10];
	segstates[5] = numbersToSegments[(tzc_year-2000)%10];

	//  segstates[5] = numbersToSegments[readKeypad()];

	_delay_ms(2000);

        
	segstates[0] = numbersToSegments[(tzc_hours/10)%10];
	segstates[1] = numbersToSegments[tzc_hours%10]|(1<<7);
	segstates[2] = numbersToSegments[(minutes/10)%10];
	segstates[3] = numbersToSegments[minutes%10]|(1<<7);
	segstates[4] = numbersToSegments[(seconds/10)%10];
	segstates[5] = numbersToSegments[seconds%10];

        _delay_ms(2000);
	//dow(2013,07,26) returns 5 to indicate Friday.
}


//32.768kHz interrupt handler - this overflows once a second (or once every 8 seconds for maximum power savings?)
//This updates GMT time. BST transition is handled separately
SIGNAL(TIMER2_OVF_vect){

  	seconds++;
	minutes +=(seconds/60);
	seconds = seconds % 60;
	hours += (minutes/60);
	minutes = minutes % 60;

	if (hours == 24) {
		//Advance once a day.
		hours = 0;
		day++;

                if (day > daysInMonth(year, month)) {
                   //If we get to, for example, day 32 of January, this will be true
                   //So we need to advance to the next month
                   month++;
                   day = 1;
                   
                   if(month > 12) {
                    year++;
                    month = 1;
                   //Happy New Year! 
                   }
                 
                }
		

	}


        //BST begins at 01:00 GMT on the last Sunday of March and ends at 01:00 GMT on the last Sunday of October
           
        //Fire at 1AM on Sundays
        if ((seconds == 0) && (minutes == 0) && (hours == 1) && (dayOfWeek(year, month, day) == SUNDAY)) {
           if((month == 3) && ((day+7)>31)){
             //If it's the last Sunday of the month we're entering BST
             timezone = 1;
           } else
           if((month == 10) && ((day+7)>31)){
             //If it's the last Sunday of the month we're leaving BST
             timezone = 0;
           }            
        }
        



}

//The interrupt occurs when you push the button
SIGNAL(INT0_vect){

}


//This interrupt (overflow) should happen once every few milliseconds.
//Display update - works with an even brightness. TODO move this to an interrupt every millisecond

SIGNAL(TIMER1_OVF_vect){
	updateDisplay();
	TCNT1 = PWM_TIME;
}


//TODO draw accounting for brightness better than this (primitive left-right shifting)
//TODO optimise - this is an interrupt, so it should be FAST otherwise we'll miss button C/CE button presses
//(if this consumes more than a few hundred cycles, it's using too many. This runs every 4000 cycles so it shouldn't take moe than 400 or so.
//OR we could nest an interrupt, at the risk of making things VERY messy...
volatile uint8_t onFrame = 0;
volatile uint8_t onSegment = 0;
void updateDisplay() {

	digitalWrite(segs[onSegment], SEGMENT_OFF);
	onFrame++;
	onFrame = onFrame % 16;
	onSegment = onFrame%8;

	if(onFrame<8) {
		for(uint8_t c=0;c<3;c++) {
			digitalWrite(cols[c+3],COLUMN_OFF);
			digitalWrite(cols[c], ((segstates[c] & (1 << onSegment))?COLUMN_ON:COLUMN_OFF));
		}
	}
	else {
		for(uint8_t c=3;c<6;c++) {
			digitalWrite(cols[c-3],COLUMN_OFF);
			digitalWrite(cols[c], ((segstates[c] & (1 << onSegment))?COLUMN_ON:COLUMN_OFF));
		}
	}

	digitalWrite(segs[onSegment], SEGMENT_ON);
}


//We have designed the resistor ladder to do the following
//for PinsA
//7 - 0
//4 - 128
//1 - 256
//0 - 384
//8 - 512
//5 - 640
//2 - 768
//. - 896
//Nothing - 1023

//same pattern but for pinsB

static const uint8_t keymapA[] = {1,2,3,4,5,6,7,8, 9};


uint8_t readKeypad(void) {
	//Switch the ADC on

	//Read the pins until the range is low enough to consider it "settled".

	int val = 1023;// analogRead(btnsA);

	//Find out what key this value corresponds with ie 0-63 is key0, 64-191 is key1, ..
	uint8_t keycnt = 0;
	while((val-128)>=-64)
		keycnt++;

	return keymapA[0];

	//Switch the ADC off to save power.
}


//Day of week - 0=Sunday, 1=Monday, 2=Tuesday, 3=Wednesday, 4=Thursday, 5=Friday, 6=Saturday
int dayOfWeek(int y, int m, int d)
{
	static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
	y -= m < 3;
	return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}


//Is the current year a leap year? ie does February have a 29th?
boolean leapYear(int y) {
        //Here's the weird set of rules for determining if the year is a leap year...
	if ((y%400) == 0)
		return true;
	else if ((y%100) == 0)
		return false;
	else if ((y%4) == 0)
		return true;
	else
		return false;
}

//"30 days have September, April, June and November, except February, which has 28, or 29 in a leap year."
const uint8_t dim[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
uint8_t daysInMonth(int y, int m) {
  if (leapYear(y) && (m==2))
    return 29;
  else
    return dim[m];  
}