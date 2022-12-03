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

/*****************************************************************************
 * Public types/enumerations/variables
 ****************************************************************************/

typedef struct shared_uart
{
	LpcUart *uart;
	Fmutex *guard;
} shared_uart;

/*****************************************************************************
 * Private functions
 ****************************************************************************/

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

static void
vSW1Task(void *pvParameters)
{
	DigitalIoPin sw1(0, 17, true, true, true);
	shared_uart *urt = static_cast<shared_uart*>(pvParameters);
	bool sw1_pressed = false;
	while(1)
	{
		if(sw1.read())
		{
			sw1_pressed = true;
		}
		else if(sw1_pressed){
            urt->guard->lock();
			urt->uart->write("SW1 pressed.\r\n");
			urt->guard->unlock();
			sw1_pressed = false;
        }
		vTaskDelay(1);
	}
}

static void
vSW2Task(void *pvParameters)
{
	DigitalIoPin sw2(1, 11, true, true, true);
	shared_uart *urt = static_cast<shared_uart*>(pvParameters);
	bool sw2_pressed = false;
	while(1)
	{
		if(sw2.read())
		{
			sw2_pressed = true;
		}
		else if(sw2_pressed){
            urt->guard->lock();
			urt->uart->write("SW2 pressed.\r\n");
			urt->guard->unlock();
			sw2_pressed = false;
        }
		vTaskDelay(1);
	}
}

static void
vSW3Task(void *pvParameters)
{
	DigitalIoPin sw3(1, 9, true, true, true);
	shared_uart *urt = static_cast<shared_uart*>(pvParameters);
	bool sw3_pressed = false;
	while(1)
	{
		if(sw3.read())
		{
			sw3_pressed = true;
		}
		else if(sw3_pressed){
            urt->guard->lock();
			urt->uart->write("SW3 pressed.\r\n");
			urt->guard->unlock();
			sw3_pressed = false;
        }
		vTaskDelay(1);
	}
}

void vTestTask(void *pvParameters)
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
	Fmutex *mutex = new Fmutex();
	
	static shared_uart btask {uart, mutex};

	xTaskCreate(vSW1Task, "vSW1Task",
				configMINIMAL_STACK_SIZE + 256, &btask, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);
	xTaskCreate(vSW2Task, "vSW2Task",
				configMINIMAL_STACK_SIZE + 256, &btask, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);
	xTaskCreate(vSW3Task, "vSW3Task",
				configMINIMAL_STACK_SIZE + 256, &btask, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);
	xTaskCreate(vTestTask, "vTestTask",
				configMINIMAL_STACK_SIZE + 256, uart, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);

	/* Start the scheduler */
	vTaskStartScheduler();

	/* Should never arrive here */
	return 1;
}
