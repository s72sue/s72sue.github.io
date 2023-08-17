/*************************************************************************************************************************

ECE 224 Project - Part 1

Sugandha Sharma
s72sharm

Mark John Agsalog
mjagsalog

*************************************************************************************************************************/

// Libraries required
#include "alt_types.h"  		// define types used by Altera code, e.g. alt_u8
#include <stdio.h>
#include <unistd.h>  		
#include<stdlib.h>
#include "system.h"  			// constants such as the base addresses of any PIOs
								// defined in the hardware
#include "sys/alt_irq.h"  		// required when using interrupts
#include <io.h>
#include "altera_avalon_timer_regs.h"  // timer register constants




//****************************************************	Global Variables  *****************************************************//

alt_u8 led_state = (alt_u8)0x00;								//LED State
volatile alt_u8 pio_response_flag = (alt_u8)0x00;
volatile alt_u8 count_flag = (alt_u8)0x00;						//Used to determine if the timer IRQ has been set
volatile alt_u8 button_flag = (alt_u8)0x00;						//Used to check if either button 0 or 1 has been pressed
volatile alt_u16 switch_state = (alt_16)0x0000;					//Used to hold the initial the of switches
//volatile alt_u8 count = (alt_u8)0x00;							//Used to keep a count of polling cycles in phaseII
//volatile alt_u8 egm_current_state = (alt_u8)0x01  ;			// used for alternating between capturing egm states of 0 and 1
int egm_current_state;   										// used for alternating between capturing egm states of 0 and 1
int count;														//used for keeping the count of the pulses

//*********************************************************	 IRQs  *********************************************************//

/*First timerISR for phase1 of lab 1
 * used for timing LEDs
 */
#ifdef TIMER_0_BASE
static void timer_ISR(void* context, alt_u32 id)
{
	IOWR(TIMER_0_BASE, 0, 0x0);	    // acknowledge the interrupt by clearing the TO bit in the status register
    count_flag = 0x1;	   			// set the flag with a non zero value
}
#endif


/*Second timerISR for phase1 of lab 1
 * used for timing the seven segment display
 */
#ifdef TIMER_1_BASE
static void timer1_ISR(void* context, alt_u32 id)
{
	IOWR(TIMER_1_BASE, 0, 0x0);	    // acknowledge the interrupt by clearing the TO bit in the status register
    count_flag = 0x2;	   			// set the flag with a non zero value
}
#endif


//*************************************************************************************************************************//

/*timer for the periodic  polling used in phase two
 * of the lab. Used for capturing the pulse state
 */
#ifdef TIMER_1_BASE
static void pulse_timer_ISR(void* context, alt_u32 id)
{
	//If it is a falling edge, turn off the pio response and set the current state of the pulse to zero
   	if ( (IORD(PIO_PULSE_BASE, 0) ==  0) && (egm_current_state == 1) ) {
   		IOWR(PIO_RESPONSE_BASE, 0, 0);
   		egm_current_state = 0;
   	}

   	//if it is a rising edge, turn on the pio response and the current state of the pulse to one
   	if ( (IORD(PIO_PULSE_BASE, 0) ==  1) && (egm_current_state == 0) ) {
   		IOWR(PIO_RESPONSE_BASE, 0, 1);
   		egm_current_state = 1;
   	  	count++;						//increment the number of pulses caught
   	}

   	IOWR(TIMER_1_BASE, 0, 0x0);			//acknowledge the interrupt by clearing the TO bit in the status register
}
#endif

//*************************************************************************************************************************//

/* button ISR used for phase1 of the lab
 * Used to service the button interrupts
 */
#ifdef BUTTON_PIO_BASE
#ifdef SWITCH_PIO_BASE
static void button_ISR(void* context, alt_u32 id)
{
	//GETS SWITCH VALUES, AND STORES ALL 16 SWITCH VALUES
	switch_state = IORD(SWITCH_PIO_BASE, 0) & 0xffff;

	//CHECKS IF BUTTONS ARE PRESSED
	alt_u8 buttons;
	buttons = IORD(BUTTON_PIO_BASE, 3) & 0x03;	//Check if the first two buttons are pressed
	button_flag = buttons;						//Set  button flag to be the current button flag status
	IOWR(BUTTON_PIO_BASE, 3, 0x0); 				// reset edge capture register to clear interrupt

	IOWR(TIMER_0_BASE, 1, 0x07);	  			 // Initialize timer control - start timer, run continuously, enable interrupts
}
#endif
#endif


//*************************************************************************************************************************//

/*ISR for servicing the interrupt requests
 * made in the phase 2 of the lab. Used to
 * capture the state of the pulse everytime
 * a pulse event occurs
 */
#ifdef PIO_RESPONSE_BASE
#ifdef PIO_PULSE_BASE
static void piopulse_ISR(void* context, alt_u32 id)
{
	//Falling edge, turn on the pio response
	if (IORD(PIO_PULSE_BASE, 0) ==  0)
		IOWR(PIO_RESPONSE_BASE, 0, 0);

	//Rising edge, turn on the pio response
	if (IORD(PIO_PULSE_BASE, 0) ==  1)
	{
		IOWR(PIO_RESPONSE_BASE, 0, 1);
		pio_response_flag++;
	}

	//Clear the pio pulse flag by writing any value to the offset of 3
	IOWR(PIO_PULSE_BASE, 3, 0x0);
}
#endif
#endif

//*************************************************************************************************************************//


//*******************************************************		Methods	 **************************************************//

/*method for controlling the seven segment
 * display in phase 1 of the lab
 */
void seven_segment_control(alt_u8 display)
{
	switch (display)
	{
		case 0x00:
			//Turn on Sev Segment 1
			IOWR(SEVEN_SEG_RIGHT_PIO_BASE, 0, 0xffffff00);
			break;
		case 0x01:
			//Turn off Sev Segment 1
			IOWR(SEVEN_SEG_RIGHT_PIO_BASE, 0, 0xffff00ff);
			break;

		case 0xff:
			//Turn off entire seven segment
			IOWR(SEVEN_SEG_RIGHT_PIO_BASE, 0, 0xffffffff);
			IOWR(SEVEN_SEG_MIDDLE_PIO_BASE, 0, 0xffff);
			IOWR(SEVEN_SEG_PIO_BASE, 0, 0xffff);
			break;
	}
}

//*************************************************************************************************************************//

/*method for implementing phase two of the lab using
 * interrupts. This is designed for capturing data
 * collectively for 1960 test cases to observe the trend
 * for analysis
 */
int p2_interrupts_trend(void)
{
	printf("\n Testing LAB1 PhaseII INTERRUPTS \n");
	int i = 0;
	int j = 1;
	int k = 1;
	int x = 0;

	#ifdef PIO_RESPONSE_BASE
	#ifdef PIO_PULSE_BASE
	  alt_irq_register( PIO_PULSE_IRQ, (void*)0, piopulse_ISR );	  	/* set up the interrupt vector */
	  IOWR(PIO_PULSE_BASE, 3, 0x0);	 							/* reset the edge capture register by writing to it (any value will do) */
	  IOWR(PIO_PULSE_BASE, 2, 0x01);  								/* enable interrupts for all pio*/
	#endif
	#endif


	//Interrupt Test Cases
			//Total amount of test cases: 1960
			printf("Interrupt Test: 1960 test cases \n");
			for (i = 1; i < 200; i = i + 20){
				for (j = 1; j <= 14 ; j++ )
				{
					for (k = 1; k <= 14 ; k++ )
					{
						printf("i:   %d   j:  %d  k: %d \n", i,j,k);


						init(j, k);
						while(pio_response_flag <= 100) {
							printf("pio_response_flag: %d \n", pio_response_flag);
							background(i);
						}
						finalize();
						pio_response_flag = 0;

					}
				}
			}
				return(0);
}


//*************************************************************************************************************************//

/*method for implementing phase two of the lab using
 * interrupts. This is designed for capturing data
 * for only one test case at a time for demonstration purpose
 */
int p2_interrupts(void)
{
	int period, dutyCycle,	granularity;				//granularity, period and duty cycle of the pulse
	printf("\n Testing LAB1 PhaseII INTERRUPTS \n");

	//Prompt the user to enter the period and duty cycle of the pulse
	printf("Period: ");
	scanf("%d", &period);
	printf("Duty Cycle: ");
	scanf("%d", &dutyCycle);
	printf("Granularity: ");
	scanf("%d", &granularity);

	#ifdef PIO_RESPONSE_BASE
	#ifdef PIO_PULSE_BASE
	  alt_irq_register( PIO_PULSE_IRQ, (void*)0, piopulse_ISR );	  	// set up the interrupt vector
	  IOWR(PIO_PULSE_BASE, 3, 0x0);	 									// reset the edge capture register by writing to it (any value will do)
	  IOWR(PIO_PULSE_BASE, 2, 0x01);  									// enable interrupts for all pio
	#endif
	#endif

	//initialize the egm to start generating pulses
	init(period, dutyCycle);

	//loop to make sure that we catch and record results for at least 100 pulses
	while(pio_response_flag <= 100) {
		background(granularity);			//start executing the background task with granularity specified as a parameter
	}

	//print the results
	finalize();

	//clear the flag which keeps a count of the number of pulses caught
	pio_response_flag = 0;

	return(0);
}


//*************************************************************************************************************************//

/*method for implementing phase two of the lab using
 * polling. This is designed for capturing data
 * collectively for 3920 test cases to observer the trend
 * for analysis
 */
int p2_polling_trend(void)
{

	printf("\n Testing LAB1 PhaseII POLLING \n");
	int i = 0;
	int j = 1;
	int k = 1;
	int x = 0;
	count = 0;
	egm_current_state = 1;


	  //Periodic Polling:  3920 test cases
	  	   // alt_irq_register(TIMER_1_IRQ, (void*)0, pulse_timer_ISR); 	// initialize timer interrupt vector

	  	  	for (i = 0; i < 200; i = i + 20){		//background
	  	  		for (j = 1; j <= 14 ; j++ ){		//in_period
	  	  			for (k = 1; k <= 14 ; k++ ){	//duty cycle
	  	  				for (x = 0; x < 2; x ++){

	  	  					printf("i:   %d   j:  %d  k: %d \n", i,j,k);
	  	  					alt_u32 timerPeriod;

	  	  					timerPeriod = 0.00005*TIMER_0_FREQ;
	  	  					alt_irq_register(TIMER_1_IRQ, (void*)0, pulse_timer_ISR); 	// initialize timer interrupt vector
	  	  				   // initialize timer period
	  	  				   IOWR(TIMER_1_BASE, 2, (alt_u16)timerPeriod);
	  	  				   IOWR(TIMER_1_BASE, 3, (alt_u16)(timerPeriod >> 16));
	  	  				   IOWR(TIMER_1_BASE, 0, 0x0);	   						// clear timer interrupt bit in status register
	  	  				   IOWR(TIMER_1_BASE, 1, 0x7);	   		// Initialize timer control - start timer, run continuously, enable interrupts

	  	  				   //initialize EGM
	  	  					init(j, k);

	  	  					while (count <= 100){
	  	  						background(i);
	  	  						//count++;
	  	  					}
	  	  					//Display the results
	  	  					finalize();
	  	  					count = 0;
	  	  					egm_current_state = 1;
	  	  			}
	  	  		}
	  	  	}
	  	  }
	return(0);
}


//*************************************************************************************************************************//

/*method for implementing phase two of the lab using
 * poling. This is designed for capturing data  for
 * only one test case at a time for demonstration purpose
 */
int p2_polling(void)
{
	int period, dutyCycle, granularity;					//granularity, period and duty cycle of the pulse
	printf("\n Testing LAB1 PhaseII POLLING \n");

	//Prompt the user to enter the period and duty cycle of the pulse
	printf("Period: ");
	scanf("%d", &period);
	printf("Duty Cycle: ");
	scanf("%d", &dutyCycle);
	printf("Granularity: ");
	scanf("%d", &granularity);
	count = 0;
	egm_current_state = 1;				//initialise the current state of the pulse to be 1 (assumption that the state is 1 when the pulse begins)

	alt_u32 timerPeriod;
	timerPeriod = 0.00005*TIMER_0_FREQ;
	alt_irq_register(TIMER_1_IRQ, (void*)0, pulse_timer_ISR); 	// initialize timer interrupt vector
	// initialize timer period
	IOWR(TIMER_1_BASE, 2, (alt_u16)timerPeriod);
	IOWR(TIMER_1_BASE, 3, (alt_u16)(timerPeriod >> 16));
	IOWR(TIMER_1_BASE, 0, 0x0);	   						// clear timer interrupt bit in status register
	IOWR(TIMER_1_BASE, 1, 0x7);	   // Initialize timer control - start timer, run continuously, enable interrupts

	//initialize the egm to start generating pulses
	init(period, dutyCycle);

	//loop to make sure that we catch and record results for at least 100 pulses
	while (count <= 100){
		background(granularity);
	}

	//Display the results
	finalize();

	return(0);
}

//*************************************************************************************************************************//

/*main method for prompting the user to enter a
 * phase number to test lab1 implementation
 */
int main(void)
{
	int phase;

	printf("\n\nWELCOME to LAB1\n");

	//prompt the user enter a number to test the functionality
	printf("\nEnter 1 for phase1 ");
	printf("\nEnter 2 for phase2 Interrupt Sychronization method");
	printf("\nEnter 3 for phase2 Periodic Polling Sychronization method ");
	printf("\nEnter 4 for phase2 Trend in Interrupt Sychronization method");
	printf("\nEnter 5 for phase2 Trend in Periodic Polling Sychronization method ");
	scanf("%d", &phase);
	if(phase == 1) {
	  lab1p1();					//test phase one of the lab
	}
	if(phase == 2) {
	   p2_interrupts();			//test phase two implemented with interrupts
	}
	if(phase == 3) {
	   p2_polling();			//test phase two implemented with polling
	}
	if(phase == 4) {
		p2_interrupts_trend();		//test phase two implemented with interrupts
	}
	if(phase == 5) {
	   p2_polling_trend();			//test phase two implemented with polling
	}

	return(0);
}

//*************************************************************************************************************************//

/*This method tests the first two push buttons as required
 * by the phase one of the lab. Button one is for LEDs and
 * button 2 is for the seven segment display
 */
void lab1p1()
{
	alt_u8 timer_exists = 0x00;			//	flag used to determine if the timer exists
	alt_u32 timerPeriod;  				// 	32 bit period used for timer

	#ifdef BUTTON_PIO_BASE
	  alt_irq_register( BUTTON_PIO_IRQ, (void*)0, button_ISR );	  	/* set up the interrupt vector */
	  IOWR(BUTTON_PIO_BASE, 3, 0x0);	 							/* reset the edge capture register by writing to it (any value will do) */
	  IOWR(BUTTON_PIO_BASE, 2, 0xf);  								/* enable interrupts for all four buttons*/
	#endif

	#ifdef LED_PIO_BASE
	  IOWR(LED_PIO_BASE, 0, 0x00);	  /* initially turn off all LEDs */
	#endif

	#ifdef SEVEN_SEG_MIDDLE_PIO_BASE
	  seven_segment_control(0xff);
	#endif

	#ifdef TIMER_0_BASE
	   timerPeriod = 1 * TIMER_0_FREQ;						// calculate timer period for 2 seconds
	   alt_irq_register(TIMER_0_IRQ, (void*)0, timer_ISR); 	// initialize timer interrupt vector

	   // initialize timer period
	   IOWR(TIMER_0_BASE, 2, (alt_u16)timerPeriod);
	   IOWR(TIMER_0_BASE, 3, (alt_u16)(timerPeriod >> 16));
	   IOWR(TIMER_0_BASE, 0, 0x0);	   						// clear timer interrupt bit in status register

	   timer_exists = 0x01;			//flag = timer exists.
	#endif

	/* output initial message */
	printf("\n\nPush a button to turn on an LED\n");

	//Loops indefinitely indefinitely as long as timer exists
	while( timer_exists != 0 )
	{
		//Only enters this if-statement an interrupt triggers the button flag to be on
		if (button_flag != 0x00)
		{
			alt_u8 active_buttons = 0x00;		//	used to determine which button is active
			alt_u8 stop_timer_count = 0x08; 			// stops the timer when
			alt_u16 sevseg_count = 0x0000;
			alt_u16 led_count = 0x0000;
			alt_u16 led_switch_states;
			alt_u16 seven_seg_switch_states;
			alt_u16 switch_condition;
			int total_count = 0x00;

			//Initializing the starting state of active switches and buttons
			active_buttons = active_buttons | button_flag;
			button_flag = 0x00;
			led_switch_states = switch_state & 0x00ff;
			seven_seg_switch_states = switch_state & 0x00ff;

			printf("Starting loop\n");
			while (total_count != stop_timer_count)
			{
				if (count_flag != 0)
				{
					//If the button isr ran:
					if (button_flag != 0x00)
					{
						if (button_flag == 0x01)
						{
							led_switch_states = switch_state & 0x00ff;		//update the switches activated
							led_count = 0;					//restart led count
						}
						else if (button_flag == 0x02)
						{
							seven_seg_switch_states = switch_state & 0x00ff;
							sevseg_count = 0;					//restart led count
						}
						active_buttons = active_buttons | button_flag;	//update active buttons
						button_flag = 0x00;								//clear the button flag
						total_count = 0;								//restart count
					}

					//case for when button 1 is pressed
					if ((active_buttons & 0x01) == 0x01)
					{
						if (!(led_count > 0x08)){
							//flash on if the led is on
							IOWR(LED_PIO_BASE, 0, 0x00);
							if (led_switch_states & (0x01 << led_count++))
								IOWR(LED_PIO_BASE, 0, 0x01);
							if (led_count == 0x08)
							{
								IOWR(LED_PIO_BASE, 0, 0x00);
								active_buttons = active_buttons ^ 0x01;
							}
						}
					}

					//case for when button 2 is pressed
					if ((active_buttons & 0x02) == 0x02)
					{
						if (!(sevseg_count > 0x08))
						{
							seven_segment_control(0xff);
							if (seven_seg_switch_states & (0x01 << sevseg_count++))
								seven_segment_control(0x00);
							if (sevseg_count == 0x08)
							{
								seven_segment_control(0xff);
								active_buttons = active_buttons ^ 0x02;
							}
						}
					}
					total_count++;
					count_flag = 0;
				}
			}
			//Turn off all displays and LEDs
			IOWR(LED_PIO_BASE, 0, 0x00);
			seven_segment_control(0xff);

			//stop timer
			IOWR(TIMER_0_BASE, 1, 0x8);

			printf("Finished loop\n");
		}
	}
}


//*************************************************************************************************************************//


/**********************************************************************
  Copyright(c) 2007 C.C.W. Hulls, P.Eng., Students, staff, and faculty
  members at the University of Waterloo are granted a non-exclusive right
  to copy, modify, or use this software for non-commercial teaching,
  learning, and research purposes provided the author(s) are acknowledged.
**********************************************************************/
