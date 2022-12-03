#if defined (__USE_LPCOPEN)
#if defined(NO_BOARD_LIB)
#include "chip.h"
#else
#include "board.h"
#endif
#endif

#include <cr_section_macros.h>
#include "FreeRTOS.h"
#include "task.h"
#include "heap_lock_monitor.h"

/*****************************************************************************
 * Private types/enumerations/variables
 ****************************************************************************/

/*****************************************************************************
 * Public types/enumerations/variables
 ****************************************************************************/

/*****************************************************************************
 * Private functions
 ****************************************************************************/

void morse_send_s(unsigned int dot, unsigned int dash);
void morse_send_o(unsigned int dot, unsigned int dash);

/* Sets up system hardware */
static void prvSetupHardware(void)
{
	SystemCoreClockUpdate();
	Board_Init();

	/* Initial LEDs state is off */
	Board_LED_Set(0, false);
    Board_LED_Set(1, false);
    Board_LED_Set(2, false);
}

static void vSOSTask(void *pvParameters) {
    unsigned int dot = configTICK_RATE_HZ / 20;
    unsigned int dash = dot * 3;

    while(1) {
        morse_send_s(dot, dash); //5d
        vTaskDelay(dash); //3d + 5d = 8d
        morse_send_o(dot, dash); //11d + 8d = 19d
        vTaskDelay(dash); //3d + 19d = 22d
        morse_send_s(dot, dash); //5d + 22d = 27d

        vTaskDelay(dot * 7); //7d + 27d = 34d
    }
}

static void vGreenTask(void *pvParameters) {
    bool LedState = false;
    unsigned int dot = configTICK_RATE_HZ / 20;

    while(1) {
        Board_LED_Set(1, LedState);
		LedState = (bool) !LedState;

        vTaskDelay(dot * 34);
    }
}

/* UART (or output) thread */
static void vUARTTask(void *pvParameters) {
    uint8_t sec = 0;
    uint8_t min = 0;

	while (1) {
		DEBUGOUT("Time: %02d:%02d \r\n", min, sec);
        sec++;
        if(sec >= 60) {
            sec = 0;
            min++;
            if(min >= 60) min = 0;
        }
		/* About a 1s delay here */
		vTaskDelay(configTICK_RATE_HZ);
	}
}

/*****************************************************************************
 * Public functions
 ****************************************************************************/

/* the following is required if runtime statistics are to be collected */
extern "C" {

void vConfigureTimerForRunTimeStats( void ) {
	Chip_SCT_Init(LPC_SCTSMALL1);
	LPC_SCTSMALL1->CONFIG = SCT_CONFIG_32BIT_COUNTER;
	LPC_SCTSMALL1->CTRL_U = SCT_CTRL_PRE_L(255) | SCT_CTRL_CLRCTR_L; // set prescaler to 256 (255 + 1), and start timer
}

}
/* end runtime statictics collection */

/**
 * @brief	main routine for FreeRTOS blinky example
 * @return	Nothing, function should not exit
 */
int main(void)
{
	prvSetupHardware();
	
	heap_monitor_setup();

	/* UART output thread, simply counts seconds */
	xTaskCreate(vUARTTask, "vTaskUart",
				configMINIMAL_STACK_SIZE + 256, NULL, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);

    /* Sends SOS via red LED */
	xTaskCreate(vSOSTask, "vSOSUart",
				configMINIMAL_STACK_SIZE + 256, NULL, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);

    /* Blinks green LED every other SOS */
	xTaskCreate(vGreenTask, "vGreenTask",
				configMINIMAL_STACK_SIZE + 256, NULL, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);

	/* Start the scheduler */
	vTaskStartScheduler();

	/* Should never arrive here */
	return 1;
}

void morse_send_s(unsigned int dot, unsigned int dash) {
    //Send 3 dots.
    for(int i = 0; i < 3; i++) {
        Board_LED_Set(0, true);
        vTaskDelay(dot);
        Board_LED_Set(0, false);
        if(i < 2) vTaskDelay(dot);
    }
}

void morse_send_o(unsigned int dot, unsigned int dash) {
    //Send 3 dashes.
    for(int i = 0; i < 3; i++) {
        Board_LED_Set(0, true);
        vTaskDelay(dash);
        Board_LED_Set(0, false);
        if(i < 2) vTaskDelay(dot);
    }
}
