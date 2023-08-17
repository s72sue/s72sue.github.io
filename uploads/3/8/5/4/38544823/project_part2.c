/**************************************************************************************************************************
 
  ECE 224 - Project - Part2 (Audio Interfacing)
 
  Sugandha Sharma
  s72sharm

  Mark John Agsalog
  mjagsalog

 
 *************************************************************************************************************************/


#include "alt_types.h"  	// define types used by Altera code, e.g. alt_u8
#include <stdio.h>
#include <unistd.h>
#include <io.h>
#include "system.h"			
#include "SD_Card.h"
#include <string.h> 		// fat.h depends on SD_Card.h and string.h
#include "fat.h"
#include <math.h>



//****************************************************	Global Variables  *****************************************************//

data_file df;
FILE *lcd;
int cc[4000];
//alt_u8 play = 0x0;
alt_u8 mode = 0x0;

int stop = 0;
int play = 0;
int next = 0;
int previous = 0;
int lcd_update = 0;
int disable_change = 0;
alt_u8 switch_state = (alt_u8)0x00; 	//stores current switch state

//define OFF					0
//#define NORMAL_SPEED		1
#define DOUBLE_SPEED		1
#define HALF_SPEED			2
#define DELAY_CHANNEL		4
#define REVERSE_PLAY		8

BYTE delay_buffer[88200] = {0};


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////*	Interrupt Service Routines	*////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


/*
 * ISR to handle the interrupts generated whenever the buttons are pressed.
 * It handles button presses by updating the state of certain state variables.
 */
#ifdef BUTTON_PIO_BASE
static void button_ISR(void* context, alt_u32 id)
{
	//CHECKS IF BUTTONS ARE PRESSED
	alt_u8 buttons = (alt_u8)0x00;  //store the button state for checking which button was pressed
	buttons = IORD(BUTTON_PIO_BASE, 3) & 0xf;  //Check if the first 4 buttons are pressed (getting the least significant bits)

	alt_u8 state = (alt_u8)0x00;   //store button state to display song name appropriately on LCD
	state = IORD(BUTTON_PIO_BASE, 0);  //use the bits set in edge capture register to update LCD display

	switch_state = IORD(SWITCH_PIO_BASE, 0) & 0xF;

	if( state != 15 ) {
		switch (buttons)
		{
			//Stop song: If stop button is pressed, change the stop variable value
			//and enable changing to next and previous songs
			case 1:
				stop = 1;
				play = 0;
				disable_change = 0;
				break;

			//Play song: If play button is pressed, change the play variable value
			//and disable changing to next and previous songs. Also update the
			//lcd_update variable which handle displaying buffering on the lcd
			case 2:
				play = 1;
				lcd_update = 1;
				disable_change = 1;
				break;

			//Cycle song forward: If next button is pressed, change the next variable value
			//only if it is allowed to change to next and previous songs
			case 4:
				if (disable_change != 1){
					next = 1;
				}
				break;

			//Cycle song backward: If previous button is pressed, change the previous variable value
			//only if it is allowed to change to next and previous songs
			case 8:
				if (disable_change != 1){
					previous = 1;
				}
				break;

			//Do nothing
			default :
				break;
		}
	}
	IOWR(BUTTON_PIO_BASE, 3, 0x0); 				// reset edge capture register to clear interrupt
}
#endif


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////*		Methods		*////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void init()
{
	#ifdef BUTTON_PIO_BASE
	  alt_irq_register( BUTTON_PIO_IRQ, (void*)0, button_ISR );	  	/* set up the interrupt vector */
	  IOWR(BUTTON_PIO_BASE, 3, 0x0);	 							/* reset the edge capture register by writing to it (any value will do) */
	  IOWR(BUTTON_PIO_BASE, 2, 0xf);  								/* enable interrupts for all four buttons*/
	#endif


	  SD_card_init();			//Initialize SD Card
	  init_mbr();				//Initialize MBR
	  init_bs();				//Initialize BS
	  init_audio_codec();		//Initialize audio codec
	  LCD_Init();				//Initialize lcd

}

// *************************************************************************************************************************

/*method to build the cluster chain everytime before playing a song
 * It takes play_speed as an argument which is used to update the
 * playing mode on the LCD
 */
int buildCluster(int play_speed)
{
	//Search for filetype, store data in data_file df
	UINT32 filetype = search_for_filetype("WAV", &df, 0, 1);
	if (filetype) {
		//fprintf(lcd, "Error! No WAV files were found\n\n");
		return 0;
	}
	else {
		LCD_Display(df.Name, play_speed);
		//Build cluster chain using information obtain from df
		int length = 1 + ceil(df.FileSize/(512*32));  //number of clusters (file size / length of one cluster)

		//if play is pressed, display buffering on LCD before the song starts playing
		if (lcd_update == 1) {
			LCD_Init();
			LCD_Show_Text(df.Name);
			LCD_Line2();
			LCD_Show_Text("Buffering..");
			lcd_update = 0;
		}

		//build the cluster chain for playing the song
		build_cluster_chain(cc, length, &df);
		LCD_Display(df.Name, play_speed);
		return 1;
	}
}

// *************************************************************************************************************************

/* Whenever the system is in stop state, it gets stuck in a loop which
 * checks for the buttons being pressed and updates the corresponding variables accordingly]
 * It also takes care of whether the next, previous or the current song needs to be played
 */
void stop_song() {

	stop = 1;
	while(1){

		//Case when play button is pressed.
		//Decrement the file number to play the same song
		if(play == 1) {
			file_number -= 1;
			play = 0;
			stop = 0;
			return;
		}

		//case when next button is pressed
		//No need to update the file number
		else if(next == 1) {
			next = 0;
			return;
		}

		//case when previous button is pressed
		//Decrement the file number to go to the previous song
		else if(previous == 1 ) {
			previous = 0;
			if(file_number - 2 >= 0) {
				file_number -= 2;

			}
			return;
		}

		//capture the current switch states
		switch_state = IORD(SWITCH_PIO_BASE, 0) & 0xF;
		usleep(45000);

	}

}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////// *Four Different Operating Modes* ///////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


/* Method to play the song at the normal speed.
 * This is done by playing sets of two channels in each iteration of
 * the loop i.e left left in first iteration, right right in second
 * iteration and so on.  This process is repeated over the entire span
 * of the song by using a for loop.
 */
void normal_speed()
{
	int sector = 0;
	int i;
	BYTE buffer[512] = {0};

	//if the file exists
	if ( buildCluster(0) == 1 ){
		while (1){
			int result = get_rel_sector( &df, buffer, cc, sector++);
			if (result == -1)
				break;

			for (i = 0; i < 512; i+=2)
			{
				UINT16 tmp; //Create a 16-bit variable to pass to the FIFO

				//Play the song only if the stop button has not been pressed
				//exit the method if stop button press has been detected
				if(stop == 1) {
					return;
				}

				if( !IORD( AUD_FULL_BASE, 0 ) ) //Check if the FIFO is not full
				{
					tmp = ( buffer[ i + 1 ] << 8 ) | ( buffer[ i ] );
					IOWR( AUDIO_0_BASE, 0, tmp );
					while (IORD( AUD_FULL_BASE, 0 )){}
				}
			}
		}
	}

	disable_change = 0;
}

// *************************************************************************************************************************

/* Method to play the song at double speed.
 * This is done by playing the left left channel followed by the right right channel
 * and then skipping one set of left left and right right channel. This process is
 * repeated over the entire span of the song by using a for loop.
 */
void double_speed()
{
	int sector = 0;
	int i;
	BYTE buffer[512] = {0};

	if ( buildCluster(1) == 1 ){
		while (1){
			int result = get_rel_sector( &df, buffer, cc, sector++);
			if (result == -1)
				break;

			for (i = 0; i < 512; i+=8)	//Increments channels by 4 (ie 2 channels are played, 2 are skipped)
			{
				UINT16 tmp;
				//exit the method if stop button press has been detected
				if(stop == 1) {
					return;
				}

				if( !IORD( AUD_FULL_BASE, 0 ) ) //Check if the FIFO is not full
				{
					//Plays first channel
					tmp = ( buffer[ i + 1 ] << 8 ) | ( buffer[ i ] );
					IOWR( AUDIO_0_BASE, 0, tmp );
					while (IORD( AUD_FULL_BASE, 0 )){}

					//Play second channel
					tmp = ( buffer[ i + 3 ] << 8 ) | ( buffer[ i + 2 ] );
					IOWR( AUDIO_0_BASE, 0, tmp );
					while (IORD( AUD_FULL_BASE, 0 )){}
				}
			}
		}
	}
	disable_change = 0;
}

// *************************************************************************************************************************

/* Method to play the song at half speed.
 * This is done by playing the left left channel followed by the right right channel
 * and then replaying the same set of left left and right right channels to create
 * the half speed effect. This process is repeated over the entire span of the song by using a for loop.
 */
void half_speed()
{
	int sector = 0;
	int i;
	BYTE buffer[512] = {0};

	if ( buildCluster(2) == 1 ){
		while (1){
			int result = get_rel_sector( &df, buffer, cc, sector++);
			if (result == -1)
				break;

			for (i = 0; i < 512; i+=4)	//Increments channels by 2 (plays lower
			{
				UINT16 tmp;
				//exit the method if stop button press has been detected
				if(stop == 1) {
					return;
				}

				if( !IORD( AUD_FULL_BASE, 0 ) ) //Check if the FIFO is not full
				{
					//Plays First channel
					tmp = ( buffer[ i + 1 ] << 8 ) | ( buffer[ i ] );
					IOWR( AUDIO_0_BASE, 0, tmp );
					while (IORD( AUD_FULL_BASE, 0 )){}

					//Plays Second channel
					tmp = ( buffer[ i + 3 ] << 8 ) | ( buffer[ i + 2 ] );
					IOWR( AUDIO_0_BASE, 0, tmp );
					while (IORD( AUD_FULL_BASE, 0 )){}

					//Repeat to create the half_speed effect
					tmp = ( buffer[ i + 1 ] << 8 ) | ( buffer[ i ] );
					IOWR( AUDIO_0_BASE, 0, tmp );
					while (IORD( AUD_FULL_BASE, 0 )){}
					tmp = ( buffer[ i + 3 ] << 8 ) | ( buffer[ i + 2 ] );
					IOWR( AUDIO_0_BASE, 0, tmp );
					while (IORD( AUD_FULL_BASE, 0 )){}
				}
			}
		}
	}

	disable_change = 0;
}

// *************************************************************************************************************************


/*Create two buffers: One containing the song and the other - a circular buffer
 * Go over the original buffer and 1 )play the LL channels and 2) copy the RR channels to the delayed buffer
 * Keep playing the LL channels from the original buffer and zeros from the circular buffer as long as the song is not finished.
 * Now once the LL channels are finished , go over the delayed buffer which now contains remaining RR channels
 * Play the remaining RR channels from the delayed buffer
 */
void delay_channel(){

	BYTE buffer[512] = {0};				//original buffer to hold the song
	//BYTE delay_buffer[88200] = {0};		//buffer to play the delayed song (by 1 second)
	int delay = 88200;
	UINT16 tmp;
	UINT16 tmp2;

	int j = 0;
	while (j<delay)
	{
		delay_buffer [j] = 0;
		j++;
	}

	if (buildCluster(3) == 1) {

		int count_delayed = 2;   //counter to go over the circular buffer
		int i;
		int last_position;					//for the case when sector is finished and only delayed RR channels need to be played
		int sector = 0;

		//Read all the sectors
		while (1){
			int result = get_rel_sector( &df, buffer, cc, sector++);
			if (result == -1)
				break;

			i=0;
			while( i<512 )
			{
				//exit the method if stop button press has been detected
				if(stop == 1)
					return;

				//play the Left channels
				while( IORD( AUD_FULL_BASE, 0 ) ){}
				tmp = ( buffer[ i + 1 ] << 8 ) | ( buffer[ i ] );
				IOWR( AUDIO_0_BASE, 0, tmp );
				i+=2;			//increment i to copy the right channels

				//play zeros until 1 second and then play the remaining stored right channels
				while( IORD( AUD_FULL_BASE, 0 ) ){}
				tmp2 = ( delay_buffer[ count_delayed + 1 ] << 8 ) | ( delay_buffer[ count_delayed ] );
				IOWR( AUDIO_0_BASE, 0, tmp2 );

				//Copy the Right channels to the circular buffer
				delay_buffer[count_delayed]=buffer[i];
				delay_buffer[count_delayed+1]=buffer[i+1];
				count_delayed+=4;
				i+=2;			//increment i to play the next set of left channels

				//reinitialise the counter of the circular buffer if 1 second is complete
				if(count_delayed >= delay-1)
					count_delayed = 2;
			}
		}
		last_position = count_delayed;

		//Once the entire sector is finished, the remaining right channels if any need to be played
		while( stop != 1 && count_delayed < delay )
		{
			if(stop == 1)
				return;

			if( !IORD( AUD_FULL_BASE, 0 ) )
			{
				tmp2 = ( delay_buffer[ count_delayed + 1 ] << 8 ) | ( delay_buffer[ count_delayed ] );
				IOWR( AUDIO_0_BASE, 0, tmp2 );
				count_delayed = count_delayed + 2;
			}
		}
		count_delayed=0;

		//play channels starting from 0 of the circular buffer (which were copied in the last cycle
		while( count_delayed <= last_position && stop!=1 )
		{
			if(stop == 1)
				return;

			if( !IORD( AUD_FULL_BASE, 0 ) )
			{
				tmp2 = ( delay_buffer[ count_delayed + 1 ] << 8 ) | ( delay_buffer[ count_delayed ] );
				IOWR( AUDIO_0_BASE, 0, tmp2 );
				count_delayed = count_delayed + 2;
			}
		}
	}
	disable_change = 0;
}

// *************************************************************************************************************************

/* Method to play the song in a reverse manner.
 * This is done by playing the sectors starting from the last sector
 * and moving towards the first sector. Moreover the channels in the
 * sector are also played backwards to produce exact reverse effect of the song.
 * This is acheived by still sending the left channel first, followed by the right channel to the CODEC.
 * This process is repeated over the entire span of the song by using a for loop.
 */
void reverse_play()
{
	int i;
	BYTE buffer[512] = {0};

	printf("reverse speed\n\n");
	int sector = ceil(df.FileSize / BPB_BytsPerSec);

	if ( buildCluster(4) == 1 ){
		while (1){
			int result = get_rel_sector( &df, buffer, cc, --sector);
			if (result == -1)
				break;

			//Must start at upper channel limit, and decrement 1 channel at a time
			for (i = 512 - 1; i >=0; i-=2)
			{
				UINT16 tmp;
				//exit the method if stop button press has been detected
				if(stop == 1)
					return;

				if( !IORD( AUD_FULL_BASE, 0 ) )
				{
					//Must still play lower byte first, then upper byte
					tmp = ( buffer[ i ] << 8 ) | ( buffer[ i - 1 ] );
					IOWR( AUDIO_0_BASE, 0, tmp );
					while (IORD( AUD_FULL_BASE, 0 )){}
				}
			}
		}
	}

	disable_change = 0;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
 * The main method is where the execution of the program begins
 * It keeps track of the operating mode and plays the song accordingly
 * It uses the methods corresponding to different modes (normal, half speed,
 * double speed, reverse song and delayed song).
 */
int main()
{
	init();

	stop_song();		//put the program in the stop loop to track the button presses
	printf("File Number at start: %d \n",file_number);
	switch_state = IORD(SWITCH_PIO_BASE, 0) & 0xF;			//capture the current state of the dip switches


	//loop to make sure that the right mode is played after coming out of the stop state
	while(1){
			switch (switch_state)
			{
				case DOUBLE_SPEED:
					double_speed();
					break;
				case HALF_SPEED:
					half_speed();
					break;
				case DELAY_CHANNEL:
					delay_channel();
					break;
				case REVERSE_PLAY:
					reverse_play();
					break;
				default:
					normal_speed();
					break;
			}

			// once the song is played, put the program in the stop loop to keep
			// track of the subsequent button presses
			stop_song();
	}
	return 0;
}

// *************************************************************************************************************************