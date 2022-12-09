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

static DigitalIoPin siga(1, 6, DigitalIoPin::input, true);
static DigitalIoPin sigb(0, 8, DigitalIoPin::input, true);

/*****************************************************************************
 * Public types/enumerations/variables
 ****************************************************************************/

typedef struct TaskData {
	LpcUart *uart;
	Fmutex *guard;
} TaskData;

#ifdef __cplusplus
extern "C" {
#endif
void PIN_INT0_IRQHandler(void) {
	static bool clockwise;
	portBASE_TYPE xHigherPriorityWoken = pdFALSE;
	
	clockwise = siga.read();
	xQueueSendToBackFromISR(q, (void *) &clockwise, &xHigherPriorityWoken);

    Chip_PININT_ClearIntStatus(LPC_GPIO_PIN_INT, PININTCH(0));
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
	Chip_INMUX_PinIntSel(0, 0, 8);

	/* Configure channel interrupt as edge sensitive and falling edge interrupt */
	Chip_PININT_ClearIntStatus(LPC_GPIO_PIN_INT, PININTCH(0));
	Chip_PININT_SetPinModeEdge(LPC_GPIO_PIN_INT, PININTCH(0));
	//Chip_PININT_EnableIntLow(LPC_GPIO_PIN_INT, PININTCH(0));
	Chip_PININT_EnableIntLow(LPC_GPIO_PIN_INT, PININTCH(0));
	Chip_PININT_DisableIntHigh(LPC_GPIO_PIN_INT, PININTCH(0));

	/* Enable interrupt in the NVIC */
	NVIC_ClearPendingIRQ(PIN_INT0_IRQn);
	NVIC_SetPriority(PIN_INT0_IRQn, configMAX_SYSCALL_INTERRUPT_PRIORITY + 1);
	NVIC_EnableIRQ(PIN_INT0_IRQn);
}

static void
vButtonTask(void *pvParameters)
{
	TaskData *t = static_cast<TaskData *>(pvParameters);
	bool clockwise = false;
	int value = 10;
	int prev_timestamp = 0;

	t->uart->write("Started waiting:\r\n");
	while(1)
	{
		if(xQueueReceive(q, &clockwise, (TickType_t) 5000))
		{
			int timestamp = xTaskGetTickCount();
			if (timestamp - prev_timestamp > 70)
			{
				if(clockwise)
				{
					value--;
					char buf[255];
					snprintf(buf, 255, "[%d] clck %d\r\n", timestamp, value);
					t->guard->lock();
					t->uart->write(buf);
					t->guard->unlock();
				}
				else
				{
					value++;
					char buf[255];
					snprintf(buf, 255, "[%d] nclck %d\r\n", timestamp, value);
					t->guard->lock();
					t->uart->write(buf);
					t->guard->unlock();
				}
				prev_timestamp = timestamp;
			}
			vTaskDelay(50);
		}
		else
		{
			int timestamp = xTaskGetTickCount();
			value = 10;
			char buf[255];
			snprintf(buf, 255, "[%d] timeout %d\r\n", timestamp, value);
			t->guard->lock();
			t->uart->write(buf);
			t->guard->unlock();
		}
		vTaskDelay(1);
		
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

	//static DigitalIoPin sw1(0, 17 ,true ,true, false);
	//static DigitalIoPin sw2(1, 11 ,true ,true, false);
	//static DigitalIoPin sw3(1, 9 ,true ,true, false);

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

	q = xQueueCreate(50, sizeof(bool));

	static TaskData t;
	t.uart = uart;
	t.guard = guard;

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
