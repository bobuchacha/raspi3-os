//
// Created by Jeff on 5/2/2024.
//

#ifndef RASPO3B_OS_CONSOLE_H
#define RASPO3B_OS_CONSOLE_H
typedef enum {
	cmd_help,            /**< Prints available commands to the console. */
	cmd_help_led,        /**< Prints available LED commands to the console. */
	cmd_led_pin,         /**< Changes the GPIO Pin for the LED to control. */
	cmd_led_on,          /**< Turns the LED on. */
	cmd_led_off,         /**< Turns the LED off. */
	cmd_led_irq_on,      /**< Pulses the LED with a `msec` milliseconds interval, using System Timer's Interrupt. */
	cmd_led_irq_off,     /**< Stop the System Timer's Interrupt. */
	cmd_led_on_ms,       /**< Turns the LED on for `msec` milliseconds. */
	cmd_led_blink_times, /**< Blinks LED count times for `msec` milliseconds. */
	cmd_led_blink_sos,   /**< Blinks SOS on LED with a `msec` milliseconds interval. */
	cmd_create_procs,    /**< Creates `proc_num` src processes. */
	cmd_run_procs,       /**< Runs the created src processes concurrently. */
	cmd_kill_procs,      /**< Kills all created src processes. */
	cmd_halt             /**< Halts the system. */
} command;
void console(char *device);
#endif //RASPO3B_OS_CONSOLE_H
