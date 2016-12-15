/****************************************************************************
 *
 *   Copyright (c) 2012-2016 PX4 Development Team. All rights reserved.
 *         Author: David Sidrane <david_s5@nscdg.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/**
 * @file tap-v2_init.c
 *
 * tap-v2-specific early startup code.  This file implements the
 * board_app_initialize() function that is called early by nsh during startup.
 *
 * Code here is run before the rcS script is invoked; it should start required
 * subsystems and perform board-specific initialization.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <px4_config.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <debug.h>
#include <errno.h>

#include <nuttx/arch.h>
#include <nuttx/board.h>
#include <nuttx/analog/adc.h>

#include "board_config.h"
#include "stm32_uart.h"

#include <arch/board/board.h>

#include <drivers/drv_hrt.h>
#include <drivers/drv_led.h>

#include <systemlib/px4_macros.h>
#include <systemlib/cpuload.h>
#include <systemlib/err.h>
#include <systemlib/hardfault_log.h>
#include <systemlib/systemlib.h>

# if defined(FLASH_BASED_PARAMS)
#  include <systemlib/flashparams/flashfs.h>
#endif


/****************************************************************************
 * Pre-Processor Definitions
 ****************************************************************************/

/* Configuration ************************************************************/

/* Debug ********************************************************************/

#ifdef CONFIG_CPP_HAVE_VARARGS
#  ifdef CONFIG_DEBUG
#    define message(...) syslog(__VA_ARGS__)
#  else
#    define message(...) printf(__VA_ARGS__)
#  endif
#else
#  ifdef CONFIG_DEBUG
#    define message syslog
#  else
#    define message printf
#  endif
#endif

/*
 * Ideally we'd be able to get these from up_internal.h,
 * but since we want to be able to disable the NuttX use
 * of leds for system indication at will and there is no
 * separate switch, we need to build independent of the
 * CONFIG_ARCH_LEDS configuration switch.
 */
__BEGIN_DECLS
extern void led_init(void);
extern void led_on(int led);
extern void led_off(int led);
__END_DECLS

/****************************************************************************
 * Protected Functions
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/************************************************************************************
 * Name: stm32_boardinitialize
 *
 * Description:
 *   All STM32 architectures must provide the following entry point.  This entry point
 *   is called early in the intitialization -- after all memory has been configured
 *   and mapped but before any devices have been initialized.
 *
 ************************************************************************************/

__EXPORT void stm32_boardinitialize(void)
{
	/* Hold power state */

	board_pwr_init(0);

	/* configure LEDs */

	board_autoled_initialize();

	/* SDIO PWR OFF (active high, init is clear) */

	stm32_configgpio(GPIO_SD_PW_EN);

	/* Sereial EE PROM R.O. (active high, init is clear) */

	stm32_configgpio(GPIO_EEPROM_WP);


	/* TEMP ctrl Off (active high, init is clear) */

	stm32_configgpio(GPIO_TEMP_CONT);


	/* Select Debug Port */

	stm32_configgpio(GPIO_S0);
	stm32_configgpio(GPIO_S1);
	stm32_configgpio(GPIO_S2);

	/* Radio Off (active low, init is set) */

	stm32_configgpio(GPIO_PCON_RADIO);


	/* configure always-on ADC pins */

	stm32_configgpio(GPIO_ADC1_IN10);


	/* configure USB interfaces */

	stm32_usbinitialize();

	/* configure SPI interfaces */

	stm32_spiinitialize();

}

/****************************************************************************
 * Name: board_app_initialize
 *
 * Description:
 *   Perform architecture specific initialization
 *
 ****************************************************************************/

__EXPORT int board_app_initialize(uintptr_t arg)
{
	int result;

#if defined(CONFIG_HAVE_CXX) && defined(CONFIG_HAVE_CXXINITIALIZE)

	/* run C++ ctors before we go any further */

	up_cxxinitialize();

#	if defined(CONFIG_EXAMPLES_NSH_CXXINITIALIZE)
#  		error CONFIG_EXAMPLES_NSH_CXXINITIALIZE Must not be defined! Use CONFIG_HAVE_CXX and CONFIG_HAVE_CXXINITIALIZE.
#	endif

#else
#  error platform is dependent on c++ both CONFIG_HAVE_CXX and CONFIG_HAVE_CXXINITIALIZE must be defined.
#endif

	/* configure the high-resolution time/callout interface */
	hrt_init();

	/* configure the DMA allocator */

	if (board_dma_alloc_init() < 0) {
		message("DMA alloc FAILED");
	}

	/* configure CPU load estimation */
#ifdef CONFIG_SCHED_INSTRUMENTATION
	cpuload_initialize_once();
#endif

	/* set up the serial DMA polling */
	static struct hrt_call serial_dma_call;
	struct timespec ts;

	/*
	 * Poll at 1ms intervals for received bytes that have not triggered
	 * a DMA event.
	 */
	ts.tv_sec = 0;
	ts.tv_nsec = 1000000;

	hrt_call_every(&serial_dma_call,
		       ts_to_abstime(&ts),
		       ts_to_abstime(&ts),
		       (hrt_callout)stm32_serial_dma_poll,
		       NULL);

	board_pwr_init(1);

#if defined(CONFIG_STM32_BBSRAM)

	/* NB. the use of the console requires the hrt running
	 * to poll the DMA
	 */

	/* Using Battery Backed Up SRAM */

	int filesizes[CONFIG_STM32_BBSRAM_FILES + 1] = BSRAM_FILE_SIZES;

	stm32_bbsraminitialize(BBSRAM_PATH, filesizes);

#if defined(CONFIG_STM32_SAVE_CRASHDUMP)

	/* Panic Logging in Battery Backed Up Files */

	/*
	 * In an ideal world, if a fault happens in flight the
	 * system save it to BBSRAM will then reboot. Upon
	 * rebooting, the system will log the fault to disk, recover
	 * the flight state and continue to fly.  But if there is
	 * a fault on the bench or in the air that prohibit the recovery
	 * or committing the log to disk, the things are too broken to
	 * fly. So the question is:
	 *
	 * Did we have a hard fault and not make it far enough
	 * through the boot sequence to commit the fault data to
	 * the SD card?
	 */

	/* Do we have an uncommitted hard fault in BBSRAM?
	 *  - this will be reset after a successful commit to SD
	 */
	int hadCrash = hardfault_check_status("boot");

	if (hadCrash == OK) {

		message("[boot] There is a hard fault logged. Hold down the SPACE BAR," \
			" while booting to halt the system!\n");

		/* Yes. So add one to the boot count - this will be reset after a successful
		 * commit to SD
		 */

		int reboots = hardfault_increment_reboot("boot", false);

		/* Also end the misery for a user that holds for a key down on the console */

		int bytesWaiting;
		ioctl(fileno(stdin), FIONREAD, (unsigned long)((uintptr_t) &bytesWaiting));

		if (reboots > 2 || bytesWaiting != 0) {

			/* Since we can not commit the fault dump to disk. Display it
			 * to the console.
			 */

			hardfault_write("boot", fileno(stdout), HARDFAULT_DISPLAY_FORMAT, false);

			message("[boot] There were %d reboots with Hard fault that were not committed to disk - System halted %s\n",
				reboots,
				(bytesWaiting == 0 ? "" : " Due to Key Press\n"));


			/* For those of you with a debugger set a break point on up_assert and
			 * then set dbgContinue = 1 and go.
			 */

			/* Clear any key press that got us here */

			static volatile bool dbgContinue = false;
			int c = '>';

			while (!dbgContinue) {

				switch (c) {

				case EOF:


				case '\n':
				case '\r':
				case ' ':
					continue;

				default:

					putchar(c);
					putchar('\n');

					switch (c) {

					case 'D':
					case 'd':
						hardfault_write("boot", fileno(stdout), HARDFAULT_DISPLAY_FORMAT, false);
						break;

					case 'C':
					case 'c':
						hardfault_rearm("boot");
						hardfault_increment_reboot("boot", true);
						break;

					case 'B':
					case 'b':
						dbgContinue = true;
						break;

					default:
						break;
					} // Inner Switch

					message("\nEnter B - Continue booting\n" \
						"Enter C - Clear the fault log\n" \
						"Enter D - Dump fault log\n\n?>");
					fflush(stdout);

					if (!dbgContinue) {
						c = getchar();
					}

					break;

				} // outer switch
			} // for

		} // inner if
	} // outer if

#endif // CONFIG_STM32_SAVE_CRASHDUMP
#endif // CONFIG_STM32_BBSRAM

	/* initial LED state */
	drv_led_start();
	led_off(LED_AMBER);
	led_off(LED_BLUE);

	result = board_i2c_initialize();

	if (result != OK) {
		led_on(LED_AMBER);
		return -ENODEV;
	}

#if defined(FLASH_BASED_PARAMS)
	static sector_descriptor_t  sector_map[] = {
		{1, 16 * 1024, 0x08004000},
		{2, 16 * 1024, 0x08008000},
		{0, 0, 0},
	};

	/* Initalizee the flashfs layer to use heap allocated memory */

	result = parameter_flashfs_init(sector_map, NULL, 0);

	if (result != OK) {
		message("[boot] FAILED to init params in FLASH %d\n", result);
		led_on(LED_AMBER);
		return -ENODEV;
	}

#endif

	/* Init the microSD slot */

	result = board_sdio_initialize();

	if (result != OK) {
		led_on(LED_AMBER);
		return -ENODEV;
	}

	return OK;
}

static void copy_reverse(stack_word_t *dest, stack_word_t *src, int size)
{
	while (size--) {
		*dest++ = *src--;
	}
}

__EXPORT void board_crashdump(uintptr_t currentsp, FAR void *tcb, FAR const uint8_t *filename, int lineno)
{
	/* We need a chunk of ram to save the complete context in.
	 * Since we are going to reboot we will use &_sdata
	 * which is the lowest memory and the amount we will save
	 * _should be_ below any resources we need herein.
	 * Unfortunately this is hard to test. See dead below
	 */

	fullcontext_s *pdump = (fullcontext_s *)&_sdata;

	(void)enter_critical_section();

	struct tcb_s *rtcb = (struct tcb_s *)tcb;

	/* Zero out everything */

	memset(pdump, 0, sizeof(fullcontext_s));

	/* Save Info */

	pdump->info.lineno = lineno;

	if (filename) {

		int offset = 0;
		unsigned int len = strlen((char *)filename) + 1;

		if (len > sizeof(pdump->info.filename)) {
			offset = len - sizeof(pdump->info.filename) ;
		}

		strncpy(pdump->info.filename, (char *)&filename[offset], sizeof(pdump->info.filename));
	}

	/* Save the value of the pointer for current_regs as debugging info.
	 * It should be NULL in case of an ASSERT and will aid in cross
	 * checking the validity of system memory at the time of the
	 * fault.
	 */

	pdump->info.current_regs = (uintptr_t) CURRENT_REGS;

	/* Save Context */


#if CONFIG_TASK_NAME_SIZE > 0
	strncpy(pdump->info.name, rtcb->name, CONFIG_TASK_NAME_SIZE);
#endif

	pdump->info.pid = rtcb->pid;


	/* If  current_regs is not NULL then we are in an interrupt context
	 * and the user context is in current_regs else we are running in
	 * the users context
	 */

	if (CURRENT_REGS) {
		pdump->info.stacks.interrupt.sp = currentsp;

		pdump->info.flags |= (eRegsPresent | eUserStackPresent | eIntStackPresent);
		memcpy(pdump->info.regs, (void *)CURRENT_REGS, sizeof(pdump->info.regs));
		pdump->info.stacks.user.sp = pdump->info.regs[REG_R13];

	} else {

		/* users context */
		pdump->info.flags |= eUserStackPresent;

		pdump->info.stacks.user.sp = currentsp;
	}

	if (pdump->info.pid == 0) {

		pdump->info.stacks.user.top = g_idle_topstack - 4;
		pdump->info.stacks.user.size = CONFIG_IDLETHREAD_STACKSIZE;

	} else {
		pdump->info.stacks.user.top = (uint32_t) rtcb->adj_stack_ptr;
		pdump->info.stacks.user.size = (uint32_t) rtcb->adj_stack_size;;
	}

#if CONFIG_ARCH_INTERRUPTSTACK > 3

	/* Get the limits on the interrupt stack memory */

	pdump->info.stacks.interrupt.top = (uint32_t)&g_intstackbase;
	pdump->info.stacks.interrupt.size  = (CONFIG_ARCH_INTERRUPTSTACK & ~3);

	/* If In interrupt Context save the interrupt stack data centered
	 * about the interrupt stack pointer
	 */

	if ((pdump->info.flags & eIntStackPresent) != 0) {
		stack_word_t *ps = (stack_word_t *) pdump->info.stacks.interrupt.sp;
		copy_reverse(pdump->istack, &ps[arraySize(pdump->istack) / 2], arraySize(pdump->istack));
	}

	/* Is it Invalid? */

	if (!(pdump->info.stacks.interrupt.sp <= pdump->info.stacks.interrupt.top &&
	      pdump->info.stacks.interrupt.sp > pdump->info.stacks.interrupt.top - pdump->info.stacks.interrupt.size)) {
		pdump->info.flags |= eInvalidIntStackPrt;
	}

#endif

	/* If In interrupt context or User save the user stack data centered
	 * about the user stack pointer
	 */
	if ((pdump->info.flags & eUserStackPresent) != 0) {
		stack_word_t *ps = (stack_word_t *) pdump->info.stacks.user.sp;
		copy_reverse(pdump->ustack, &ps[arraySize(pdump->ustack) / 2], arraySize(pdump->ustack));
	}

	/* Is it Invalid? */

	if (!(pdump->info.stacks.user.sp <= pdump->info.stacks.user.top &&
	      pdump->info.stacks.user.sp > pdump->info.stacks.user.top - pdump->info.stacks.user.size)) {
		pdump->info.flags |= eInvalidUserStackPtr;
	}

	int rv = stm32_bbsram_savepanic(HARDFAULT_FILENO, (uint8_t *)pdump, sizeof(fullcontext_s));

	/* Test if memory got wiped because of using _sdata */

	if (rv == -ENXIO) {
		char *dead = "Memory wiped - dump not saved!";

		while (*dead) {
			up_lowputc(*dead++);
		}

	} else if (rv == -ENOSPC) {

		/* hard fault again */

		up_lowputc('!');
	}


#if defined(CONFIG_BOARD_RESET_ON_CRASH)
	px4_systemreset(false);
#endif
}
