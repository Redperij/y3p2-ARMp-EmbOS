#if defined (__USE_LPCOPEN)
#if defined(NO_BOARD_LIB)
#include "chip.h"
#else
#include "board.h"
#endif
#endif

#include <cr_section_macros.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "heap_lock_monitor.h"
#include "Fmutex.h"
#include "LpcUart.h"
#include "DigitalIoPin.h"

typedef struct TaskData {
	LpcUart *uart;
	Fmutex *guard;
} TaskData;

/*****************************************************************************
 * Private types/enumerations/variables
 ****************************************************************************/

static QueueHandle_t q1;

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
vRandNumTask(void *pvParameters)
{
    //TaskData *t = static_cast<TaskData *>(pvParameters);
    int randn = 0;
    int randdelay = 0;
    while (1)
	{
        randn = rand() % 400 + 100;
        randdelay = rand() % 400 + 100;
        xQueueSendToBack(q1, (void *) &randn, portMAX_DELAY);
	    vTaskDelay(randdelay);
	}
}

static void
vReadButtonTask(void *pvParameters)
{
	//TaskData *t = static_cast<TaskData *>(pvParameters);
	DigitalIoPin sw1(0, 17, true, true, true);
	int help = 112;
	bool sw1_pressed = false;
	while(1)
	{
		if(sw1.read())
		{
			sw1_pressed = true;
		}
		else if(sw1_pressed){
            xQueueSendToFront(q1, (void *) &help, portMAX_DELAY);
			sw1_pressed = false;
        }
		vTaskDelay(1);
	}
}

static void
vPrintQueue(void *pvParameters)
{
	TaskData *t = static_cast<TaskData *>(pvParameters);
	int buf = 0;
	while(1)
	{
		xQueueReceive(q1, &buf, portMAX_DELAY);
        char str[255];
		if(buf != 112)
            snprintf(str, 255, "%d\r\n", buf);
		else
			snprintf(str, 255, "%d Help me\r\n", buf);
		t->guard->lock();
		t->uart->write(str);
		t->guard->unlock();
        if(buf == 112) vTaskDelay(800);
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
	Fmutex *guard = new Fmutex();

	static TaskData t;

	q1 = xQueueCreate(5, sizeof(int));
	t.uart = uart;
	t.guard = guard;

	/* UART output thread, simply counts seconds */
	xTaskCreate(vRandNumTask, "vRandNumTask",
				configMINIMAL_STACK_SIZE + 256, &t, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);
	xTaskCreate(vReadButtonTask, "vReadButtonTask",
				configMINIMAL_STACK_SIZE + 256, &t, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);
	xTaskCreate(vPrintQueue, "vPrintQueue",
				configMINIMAL_STACK_SIZE + 256, &t, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);

	/* Start the scheduler */
	vTaskStartScheduler();

	/* Should never arrive here */
	return 1;
}
