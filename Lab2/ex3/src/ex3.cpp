#if defined (__USE_LPCOPEN)
#if defined(NO_BOARD_LIB)
#include "chip.h"
#else
#include "board.h"
#endif
#endif

#include <cr_section_macros.h>
#include <cstring>
#include <cstdlib>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "heap_lock_monitor.h"
#include "Fmutex.h"
#include "LpcUart.h"
#include "DigitalIoPin.h"
#include "ITM_write.h"

/*****************************************************************************
 * Private types/enumerations/variables
 ****************************************************************************/

static SemaphoreHandle_t semaphore;

/*****************************************************************************
 * Public types/enumerations/variables
 ****************************************************************************/

typedef struct shared_puart {
	LpcUart *uart;
	Fmutex *guard;
} shared_puart;

/*****************************************************************************
 * Private functions
 ****************************************************************************/

/* Sets up system hardware */
static void
prvSetupHardware(void)
{
	SystemCoreClockUpdate();
	Board_Init();
	ITM_init();

	/* Set up SWO to PIO1_2 to enable ITM */
	Chip_SWM_MovablePortPinAssign(SWM_SWO_O, 1, 2);

	/* Initial LEDs state is off */
	Board_LED_Set(0, false);
    Board_LED_Set(1, false);
    Board_LED_Set(2, false);
}

static void
vReadQuestion(void *pvParameters)
{
	shared_puart *uart = static_cast<shared_puart *>(pvParameters);
	char str[61];
	int count = 0;
	
	while (1)
	{
		int bytes = uart->uart->read(str + count, 60 - count, portTICK_PERIOD_MS * 100);
		if(bytes > 0)
		{
			count += bytes;
			str[count] = '\0';
			uart->uart->write(str + count - bytes, bytes);
			if(strchr(str, '\r') != NULL || strchr(str, '\n') != NULL || count >= 60)
			{
				for(int i = 0; i < count; i++)
				{
					if(str[i] == '?')
						xSemaphoreGive(semaphore);
				}
				uart->uart->write(str, count);
				uart->uart->write('\n');
				count = 0;
				uart->guard->lock();
				ITM_write("[You] ");
				ITM_write(str);
				uart->guard->unlock();
			}
		}
	}
}

static void
vOracle(void *pvParameters)
{
	shared_puart *uart = static_cast<shared_puart *>(pvParameters);
	char buf[255];
	const char *answers[6] = {"Friendship will help you to find the answer.\n",
						"Find a needle in a haystack.\n",
						"You already have an answer.\n",
						"Arklys.\n",
						"Was that a question?\n",
						"May the dialectical materialism be with you.\n"};
	while(true)
	{
		xSemaphoreTake(semaphore, portMAX_DELAY);
		uart->guard->lock();
		ITM_write("[Oracle] Hmmm...\n");
		uart->guard->unlock();
		snprintf(buf, 255, "[Oracle] %s", answers[rand() % 6]);
		vTaskDelay(3000);
		uart->guard->lock();
		ITM_write(buf);
		uart->guard->unlock();
		vTaskDelay(2000);
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

    semaphore = xSemaphoreCreateCounting(10, 0);

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

	static shared_puart puart{uart, guard};

	xTaskCreate(vReadQuestion, "vReadQuestion",
				configMINIMAL_STACK_SIZE + 256, &puart, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);
	xTaskCreate(vOracle, "vOracle",
				configMINIMAL_STACK_SIZE + 256, &puart, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);

	/* Start the scheduler */
	vTaskStartScheduler();

	/* Should never arrive here */
	return 1;
}
