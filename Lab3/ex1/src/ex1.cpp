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
vReadUARTTask(void *pvParameters)
{
	TaskData *t = static_cast<TaskData *>(pvParameters);
	char str[81];
	int count = 0;
	while (1)
	{
		int bytes = t->uart->read(str + count, 80 - count, portTICK_PERIOD_MS * 100);
		if(bytes > 0) {
			count += bytes;
			str[count] = '\0';
			t->guard->lock();
			t->uart->write(str + count - bytes, bytes);
			t->guard->unlock();
			if(strchr(str, '\r') != NULL || strchr(str, '\n') != NULL || count >= 80) {
				t->guard->lock();
				t->uart->write('\n');
				t->guard->unlock();
				count--;
				xQueueSendToBack(q1, (void *) &count, portMAX_DELAY);
				count = 0;
			}
		}
		vTaskDelay(1);
	}
}

static void
vReadButtonTask(void *pvParameters)
{
	//TaskData *t = static_cast<TaskData *>(pvParameters);
	DigitalIoPin sw1(0, 17, true, true, true);
	int stop = -1;
	bool sw1_pressed = false;
	while(1)
	{
		if(sw1.read())
		{
			sw1_pressed = true;
		}
		else if(sw1_pressed){
            xQueueSendToBack(q1, (void *) &stop, portMAX_DELAY);
			sw1_pressed = false;
        }
		vTaskDelay(1);
	}
}

static void
vSumQueue(void *pvParameters)
{
	TaskData *t = static_cast<TaskData *>(pvParameters);
	int sum = 0, buf = 0;
	while(1)
	{
		xQueueReceive(q1, &buf, portMAX_DELAY);
		if(buf != -1)
		{
			sum += buf;
			/*
			//DEBUG
			char str[255];
			snprintf(str, 255, "Sum: %d\r\n", sum);
			t->guard->lock();
			t->uart->write(str);
			t->guard->unlock();
			///DEBUG
			*/
		}
		else
		{
			char str[255];
			snprintf(str, 255, "You have typed %d characters\r\n", sum);
			sum = 0;
			t->guard->lock();
			t->uart->write(str);
			t->guard->unlock();
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
	Fmutex *guard = new Fmutex();

	static TaskData t;

	q1 = xQueueCreate(5, sizeof(int));
	t.uart = uart;
	t.guard = guard;

	/* UART output thread, simply counts seconds */
	xTaskCreate(vReadUARTTask, "vReadUARTTask",
				configMINIMAL_STACK_SIZE + 256, &t, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);
	xTaskCreate(vReadButtonTask, "vReadButtonTask",
				configMINIMAL_STACK_SIZE + 256, &t, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);
	xTaskCreate(vSumQueue, "vSumQueue",
				configMINIMAL_STACK_SIZE + 256, &t, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);

	/* Start the scheduler */
	vTaskStartScheduler();

	/* Should never arrive here */
	return 1;
}
