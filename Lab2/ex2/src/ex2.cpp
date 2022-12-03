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
#include "queue.h"
#include "heap_lock_monitor.h"
#include "Fmutex.h"
#include "LpcUart.h"
#include "DigitalIoPin.h"

/*****************************************************************************
 * Private types/enumerations/variables
 ****************************************************************************/

static SemaphoreHandle_t semaphore;

/*****************************************************************************
 * Public types/enumerations/variables
 ****************************************************************************/

/*****************************************************************************
 * Private functions
 ****************************************************************************/

/* Sets up system hardware */
static void
prvSetupHardware(void)
{
	SystemCoreClockUpdate();
	Board_Init();

	/* Initial LEDs state is off */
	Board_LED_Set(0, false);
    Board_LED_Set(1, false);
    Board_LED_Set(2, false);
}

static void
vBlinkTask(void *pvParameters)
{
	while(1)
	{
		xSemaphoreTake(semaphore, portMAX_DELAY);
        Board_LED_Set(1, true);
        vTaskDelay(100);
        Board_LED_Set(1, false);
        vTaskDelay(100);
	}
}

static void
vUARTTask(void *pvParameters)
{
	LpcUart *dbgu = static_cast<LpcUart *>(pvParameters);

	char str[80];
	int count = 0;

	/* Set up SWO to PIO1_2 to enable ITM */
	Chip_SWM_MovablePortPinAssign(SWM_SWO_O, 1, 2);

	while (1) {
		count = dbgu->read(str, 80, portTICK_PERIOD_MS * 100);
		str[count] = '\0';
		if(count > 0) {
			dbgu->write(str);
            xSemaphoreGive(semaphore);
		}
		else {
			/* receive timed out */
		}
	}
}

/*****************************************************************************
 * Public functions
 ****************************************************************************/

/* the following is required if runtime statistics are to be collected */
extern "C" {

void
vConfigureTimerForRunTimeStats( void )
{
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
int
main(void)
{
	prvSetupHardware();
	
	heap_monitor_setup();

    semaphore = xSemaphoreCreateBinary();

	LpcPinMap none = { .port = -1, .pin = -1}; // unused pin has negative values in it
	LpcPinMap txpin = { .port = 0, .pin = 18 }; // transmit pin that goes to debugger's UART->USB converter
	LpcPinMap rxpin = { .port = 0, .pin = 13 }; // receive pin that goes to debugger's UART->USB converter
	LpcUartConfig cfg = { 
			.pUART = LPC_USART0, 
			.speed = 115200, 
			.data = UART_CFG_DATALEN_8 | UART_CFG_PARITY_NONE | UART_CFG_STOPLEN_1, 
			.rs485 = false, 
			.tx = txpin, 
			.rx = rxpin, 
			.rts = none, 
			.cts = none 
	};

	LpcUart *uart = new LpcUart(cfg);

	xTaskCreate(vBlinkTask, "vBlinkTask",
				configMINIMAL_STACK_SIZE + 256, NULL, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);
	xTaskCreate(vUARTTask, "vUARTTask",
				configMINIMAL_STACK_SIZE + 256, uart, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);

	/* Start the scheduler */
	vTaskStartScheduler();

	/* Should never arrive here */
	return 1;
}
