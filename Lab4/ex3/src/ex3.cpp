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
#include <cctype>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "heap_lock_monitor.h"
#include "DigitalIoPin.h"
#include "ITM_write.h"
#include "LpcUart.h"
#include "Fmutex.h"

/*****************************************************************************
 * Private types/enumerations/variables
 ****************************************************************************/
static QueueHandle_t q;

/*****************************************************************************
 * Public types/enumerations/variables
 ****************************************************************************/

typedef struct debugEvent {
 const char *format;
 unsigned int data[3];
} debugEvent;

typedef struct TaskData {
	LpcUart *uart;
	Fmutex *guard;
} TaskData;


/*****************************************************************************
 * Private functions
 ****************************************************************************/

void
debug(const char *format, unsigned int d1 = 0, unsigned int d2 = 0, unsigned int d3 = 0)
{
	//debugEvent *e = new debugEvent{format, {d1, d2, d3}};
	static debugEvent e;
	e.format = format;
	e.data[0] = d1;
	e.data[1] = d2;
	e.data[2] = d3;
	xQueueSendToBack(q, (void *) &e, portMAX_DELAY);
}

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
	ITM_init();
}

static void
vDebugTask(void *pvParameters)
{
	TaskData *t = static_cast<TaskData *>(pvParameters);
	debugEvent e;
	while(1)
	{
		xQueueReceive(q, &e, portMAX_DELAY);
		char str[255];
		snprintf(str, 255, e.format, e.data[0], e.data[1], e.data[2]);
		t->guard->lock();
		t->uart->write(str);
		t->guard->unlock();
	}
}

static void
vReadWordTask(void *pvParameters)
{
	TaskData *t = static_cast<TaskData *>(pvParameters);
	const char *format = "Received %d length word at %d\r\n";

	static volatile char str[81];
	char *c = new char;
	unsigned int count = 0;
	bool fword = false;
	while (1)
	{
		if(t->uart->read(c, 1, portTICK_PERIOD_MS * 100))
		{
			//Echo back.
			if(*c == '\r' || *c == '\n')
			{
				t->guard->lock();
				t->uart->write("\r\n", 3);
				t->guard->unlock();
			}
			else
			{
				char buf[2];
				buf[0] = *c;
				buf[1] = '\0';
				t->guard->lock();
				t->uart->write(buf, 2);
				t->guard->unlock();
			}
			
			//Read and save letters until received whitespace after a letter.
			if(!isspace(*c))
			{
				fword = true;
				if(count < 80)
				{
					str[count] = *c;
					count++;
				}
			}
			else if(fword)
			{
				str[count] = '\0';
				debug(format, count, xTaskGetTickCount());
				count = 0;
				fword = false;
			}
		}
		vTaskDelay(1);
	}
	delete c;
}

static void
vButtonTask(void *pvParameters)
{
	DigitalIoPin sw1(0, 17, true, true, true);
	const char *format = "Button held for %dms at %d\r\n";

	unsigned int time = 0;
	bool sw1_pressed = false;
	while(1)
	{
		if(sw1.read())
		{
			if(!sw1_pressed)
				time = 0;
			sw1_pressed = true;
		}
		else if(sw1_pressed){

            debug(format, time, xTaskGetTickCount());
			sw1_pressed = false;
        }
		vTaskDelay(1);
		time++;
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

	q = xQueueCreate(10, sizeof(debugEvent));

	TaskData *t = new TaskData{uart, guard};

	/* UART output thread, simply counts seconds */
	xTaskCreate(vDebugTask, "vDebugTask",
				configMINIMAL_STACK_SIZE + 256, t, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);
	xTaskCreate(vReadWordTask, "vReadWordTask",
				configMINIMAL_STACK_SIZE + 256, t, (tskIDLE_PRIORITY + 2UL),
				(TaskHandle_t *) NULL);
	xTaskCreate(vButtonTask, "vButtonTask",
				configMINIMAL_STACK_SIZE + 256, t, (tskIDLE_PRIORITY + 2UL),
				(TaskHandle_t *) NULL);

	/* Start the scheduler */
	vTaskStartScheduler();

	/* Should never arrive here */
	delete uart;
	delete guard;
	delete t;
	return 1;
}
