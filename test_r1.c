/*************************************************************************
 * Lab 5 Preempt test
 *
 * Two realtime processes, the first starting early and ending late,
 * the other starting and ending during the duration of the first.
 *
 * This checks that the second process is able to preempt the first due to its earlier deadline.
 *
 * Both processes also start after a delay where no process is running, testing the ability to hold.
 *
 * The second process will not hit its deadline, so there will be one double blink at the end.
 *
 * pRT1: ^g g     g g
 * pRT2: ^    r r
 *
 ************************************************************************/

#include "utils.h"
#include "3140_concur.h"
#include "realtime.h"

/*--------------------------*/
/* Parameters for test case */
/*--------------------------*/



/* Stack space for processes */
#define NRT_STACK 20
#define RT_STACK  20



/*--------------------------------------*/
/* Time structs for real-time processes */
/*--------------------------------------*/

realtime_t t_sRT1 = {1, 0};
realtime_t t_dRT1 = {10, 0};


realtime_t t_sRT2 = {3, 0};
realtime_t t_dRT2 = {1, 0};


/*------------------*/
/* Helper functions */
/*------------------*/
void shortDelay(){delay();}
void mediumDelay() {delay(); delay();}



void RT1(void) {
	int i;
	for (i=0; i<4;i++){
		LEDGreen_On();
		shortDelay();
		LEDGreen_Toggle();
		shortDelay();
	}
}

void RT2(void) {
	int i;
	for (i=0; i<2;i++){
		LEDRed_On();
		shortDelay();
		LEDRed_Toggle();
		shortDelay();
	}
}


/*--------------------------------------------*/
/* Main function - start concurrent execution */
/*--------------------------------------------*/
int main(void) {

	LED_Initialize();

    /* Create processes */
    if (process_rt_create(RT1, RT_STACK, &t_sRT1, &t_dRT1) < 0) { return -1; }
    if (process_rt_create(RT2, RT_STACK, &t_sRT2, &t_dRT2) < 0) { return -1; }

    /* Launch concurrent execution */
	process_start();

  LED_Off();
  while(process_deadline_miss>0) {
		LEDRed_On();
		LEDGreen_On();
		shortDelay();
		LED_Off();
		shortDelay();
		process_deadline_miss--;
	}

	/* Hang out in infinite loop (so we can inspect variables if we want) */
	while (1);
	return 0;
}
