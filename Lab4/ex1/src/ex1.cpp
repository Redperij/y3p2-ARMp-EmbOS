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

/*****************************************************************************
 * Private types/enumerations/variables
 ****************************************************************************/

static QueueHandle_t q;

/*****************************************************************************
 * Public types/enumerations/variables
 ****************************************************************************/

typedef struct ButData {
	unsigned int nbutton;
	unsigned int timestamp;
} ButData;

typedef struct TaskData {
	LpcUart *uart;
	Fmutex *guard;
} TaskData;

#ifdef __cplusplus
extern "C" {
#endif
void PIN_INT0_IRQHandler(void) {
	static ButData bd;
	portBASE_TYPE xHigherPriorityWoken = pdFALSE;
	
	bd.nbutton = 1;
	bd.timestamp = 0;
	xQueueSendToBackFromISR(q, (void *) &bd, &xHigherPriorityWoken);

    Chip_PININT_ClearIntStatus(LPC_GPIO_PIN_INT, PININTCH(0));
	portEND_SWITCHING_ISR(xHigherPriorityWoken);
}
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
void PIN_INT1_IRQHandler(void) {
	static ButData bd;
	portBASE_TYPE xHigherPriorityWoken = pdFALSE;
	
	bd.nbutton = 2;
	bd.timestamp = 0;
	xQueueSendToBackFromISR(q, (void *) &bd, &xHigherPriorityWoken);

    Chip_PININT_ClearIntStatus(LPC_GPIO_PIN_INT, PININTCH(1));
	portEND_SWITCHING_ISR(xHigherPriorityWoken);
}
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
void PIN_INT2_IRQHandler(void) {
	static ButData bd;
	portBASE_TYPE xHigherPriorityWoken = pdFALSE;
	
	bd.nbutton = 3;
	bd.timestamp = 0;
	xQueueSendToBackFromISR(q, (void *) &bd, &xHigherPriorityWoken);

    Chip_PININT_ClearIntStatus(LPC_GPIO_PIN_INT, PININTCH(2));
	portEND_SWITCHING_ISR(xHigherPriorityWoken);
}
#ifdef __cplusplus
}
#endif

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
vConfigureInterrupts(void)
{
	/* Initialize PININT driver */
	Chip_PININT_Init(LPC_GPIO_PIN_INT);

	//Filter out sw2 bounce. lpcxpresso specific.
	for(int i = 0; i < 10000; i++);

	/* Enable PININT clock */
	Chip_Clock_EnablePeriphClock(SYSCTL_CLOCK_PININT);

	/* Reset the PININT block */
	Chip_SYSCTL_PeriphReset(RESET_PININT);

	/* Configure interrupt channel for the GPIO pin in INMUX block */
	Chip_INMUX_PinIntSel(0, 0, 17);
	Chip_INMUX_PinIntSel(1, 1, 11);
	Chip_INMUX_PinIntSel(2, 1, 9);

	/* Configure channel interrupt as edge sensitive and falling edge interrupt */
	Chip_PININT_ClearIntStatus(LPC_GPIO_PIN_INT, PININTCH(0));
	Chip_PININT_SetPinModeEdge(LPC_GPIO_PIN_INT, PININTCH(0));
	Chip_PININT_EnableIntLow(LPC_GPIO_PIN_INT, PININTCH(0));

	Chip_PININT_ClearIntStatus(LPC_GPIO_PIN_INT, PININTCH(1));
	Chip_PININT_SetPinModeEdge(LPC_GPIO_PIN_INT, PININTCH(1));
	Chip_PININT_EnableIntLow(LPC_GPIO_PIN_INT, PININTCH(1));

	Chip_PININT_ClearIntStatus(LPC_GPIO_PIN_INT, PININTCH(2));
	Chip_PININT_SetPinModeEdge(LPC_GPIO_PIN_INT, PININTCH(2));
	Chip_PININT_EnableIntLow(LPC_GPIO_PIN_INT, PININTCH(2));

	/* Enable interrupt in the NVIC */
	NVIC_ClearPendingIRQ(PIN_INT0_IRQn);
	NVIC_SetPriority(PIN_INT0_IRQn, configMAX_SYSCALL_INTERRUPT_PRIORITY + 1);
	NVIC_EnableIRQ(PIN_INT0_IRQn);

	NVIC_ClearPendingIRQ(PIN_INT1_IRQn);
	NVIC_SetPriority(PIN_INT1_IRQn, configMAX_SYSCALL_INTERRUPT_PRIORITY + 1);
	NVIC_EnableIRQ(PIN_INT1_IRQn);

	NVIC_ClearPendingIRQ(PIN_INT2_IRQn);
	NVIC_SetPriority(PIN_INT2_IRQn, configMAX_SYSCALL_INTERRUPT_PRIORITY + 1);
	NVIC_EnableIRQ(PIN_INT2_IRQn);
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
				int num = 4;
				int time = 50;
				t->guard->lock();
				t->uart->write('\n');
				t->guard->unlock();
				if(sscanf(str, "filter %d", &time))
				{
					static ButData bd;
					bd.nbutton = num;
					bd.timestamp = time;
					xQueueSendToBack(q, (void *) &bd, portMAX_DELAY);
				}
				else
				{
					t->guard->lock();
					t->uart->write("Take a seat!\r\nWhat did you say?\r\n");
					t->guard->unlock();
				}
				count = 0;
			}
		}
		vTaskDelay(1);
	}
}

static void
vButtonTask(void *pvParameters)
{
	TaskData *t = static_cast<TaskData *>(pvParameters);
	ButData bd;
	unsigned int filter_time = 50; //ms
	unsigned int prev_timestamp = 0;

	t->uart->write("Started waiting:\r\n");
	while(1)
	{
		xQueueReceive(q, &bd, portMAX_DELAY);
		if(bd.nbutton != 4)
		{
			bd.timestamp = xTaskGetTickCount();
			if(bd.timestamp - prev_timestamp > filter_time)
			{
				char buf[255];
				snprintf(buf, 255, "%d ms Button %d\r\n", bd.timestamp - prev_timestamp, bd.nbutton);
				t->guard->lock();
				t->uart->write(buf);
				t->guard->unlock();
			}
			prev_timestamp = bd.timestamp;
		}
		else
		{
			filter_time = bd.timestamp;

			char buf[255];
			snprintf(buf, 255, "Changed filtering time.\r\nNew filter: %d\r\n", filter_time);
			t->guard->lock();
			t->uart->write(buf);
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

	static DigitalIoPin sw1(0, 17 ,true ,true, false);
	static DigitalIoPin sw2(1, 11 ,true ,true, false);
	static DigitalIoPin sw3(1, 9 ,true ,true, false);

	vConfigureInterrupts();

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

	q = xQueueCreate(50, sizeof(ButData));

	static TaskData t;
	t.uart = uart;
	t.guard = guard;

	xTaskCreate(vReadUARTTask, "vReadUARTTask",
				configMINIMAL_STACK_SIZE + 256, &t, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);
	xTaskCreate(vButtonTask, "vButtonTask",
				configMINIMAL_STACK_SIZE + 256, &t, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);

	/* Start the scheduler */
	vTaskStartScheduler();

	/* Should never arrive here */
	delete uart;
	delete guard;
	return 1;
}
