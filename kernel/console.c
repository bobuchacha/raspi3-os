//
// Created by Jeff on 5/2/2024.
//

#include "console.h"
/**
 * \ingroup console
 * @file console.c
 * @brief Implementation of a minimal console.
 */

#include "ros.h"
#include "lib/string.h"
#include "hardware/uart/uart.h"

#define LED_PIN 0  // TODO: correct me
#define PROMPT_LENGTH 16

/* Initialize default LED pin per device */
unsigned char led_pin_num = LED_PIN;

char *console_init(char *device) {
    static char prompt[PROMPT_LENGTH];

    /* Create prompt */
    strcpy(prompt, "root@");
    strcat(prompt, device);
    strcat(prompt, "#");

    return prompt;
}

int console_get_cmd(char *input) {
    // if (strcmp(input, "help") == 0)
    //     return cmd_help;
    // else if (strcmp(input, "help_led") == 0)
    //     return cmd_help_led;
    // else if (strcmp(input, "led_pin") == 0)
    //     return cmd_led_pin;
    // else if (strcmp(input, "led_on") == 0)
    //     return cmd_led_on;
    // else if (strcmp(input, "led_off") == 0)
    //     return cmd_led_off;
    // else if (strcmp(input, "led_irq_on") == 0)
    //     return cmd_led_irq_on;
    // else if (strcmp(input, "led_irq_off") == 0)
    //     return cmd_led_irq_off;
    // else if (strcmp(input, "led_on_ms") == 0)
    //     return cmd_led_on_ms;
    // else if (strcmp(input, "led_blink_times") == 0)
    //     return cmd_led_blink_times;
    // else if (strcmp(input, "led_blink_sos") == 0)
    //     return cmd_led_blink_sos;
    // else if (strcmp(input, "create_procs") == 0)
    //     return cmd_create_procs;
    // else if (strcmp(input, "run_procs") == 0)
    //     return cmd_run_procs;
    // else if (strcmp(input, "kill_procs") == 0)
    //     return cmd_kill_procs;
    // else if (strcmp(input, "halt") == 0)
    //     return cmd_halt;
    // else
    return -1;
}

void console(char *device) {
    char *input;
    char *args;
    char *prompt;
    int msec, count, pin_num, proc_num;

    /* Get prompt */
    prompt = console_init(device);

    /* Print console info */
    printf("\n");
    printf("This is a minimal console, type 'help' to see the available commands. (Maximum Input Length: %d)\n",
           MAX_INPUT_LENGTH);

    /* Main functionality */
    while (1) {
        /* Print prompt */
        printf("%s ", prompt);
        /* Read from serial */
        input = uart_gets();
        printf("\n");
        // printf("--DEBUG-- input: %s\n", input);

        /* Find given command */
        command cmd = console_get_cmd(input);

        switch (cmd) {
            // case cmd_help:
            //     console_help();
            //     break;
            // case cmd_help_led:
            //     console_help_led();
            //     break;
            // case cmd_led_pin:
            //     printf("Enter GPIO Pin: ");
            //     args = uart_gets();
            //     printf("\n");
            //     pin_num = atoi(args);
            //     if (pin_num <= 0) {
            //         printf("Not valid Pin: %s\n", args);
            //         break;
            //     }
            //     if (led_init(pin_num) < 0) {
            //         printf("Error: Not a valid GPIO PIN\n");
            //         break;
            //     }
            //     printf("Changed LED Pin to %d.\n", pin_num);
            //     led_pin_num = pin_num;
            //     break;
            // case cmd_led_on:
            //     printf("Turning LED on.\n");
            //     //led_on(led_pin_num);
            //     break;
            // case cmd_led_off:
            //     printf("Turning LED off.\n");
            //     //led_off(led_pin_num);
            //     break;
            // case cmd_led_irq_on:
            //     printf("Enter milliseconds: ");
            //     args = uart_gets();
            //     printf("\n");
            //     msec = atoi(args);
            //     if (msec <= 0) {
            //         printf("Not valid ms: %s\n", args);
            //         break;
            //     }
            //     else if (msec <= LED_MSEC_IRQ) {
            //         printf("Must be > %dms\n", LED_MSEC_IRQ);
            //         break;
            //     }
            //     printf("Started System Timer Interrupt every %dms.\n", msec);
            //     // timer_1_init((uint32_t) msec);
            //     break;
            // case cmd_led_irq_off:
            //     printf("Stopped System Timer Interrupt.\n");
            //     // timer_1_stop();
            //     break;
            // case cmd_led_on_ms:
            //     printf("Enter milliseconds: ");
            //     args = uart_gets();
            //     printf("\n");
            //     msec = atoi(args);
            //     if (msec <= 0) {
            //         printf("Not valid ms: %s\n", args);
            //         break;
            //     }
            //     printf("Turning LED on for %dms\n", msec);
            //     // led_on_ms(led_pin_num, (uint32_t) msec);
            //     break;

            // case cmd_led_blink_times:
            //     printf("Enter milliseconds: ");
            //     args = uart_gets();
            //     printf("\n");
            //     msec = atoi(args);
            //     if (msec <= 0) {
            //         printf("Not valid ms: %s\n", args);
            //         break;
            //     }
            //     printf("Enter times to blink: ");
            //     args = uart_gets();
            //     printf("\n");
            //     count = atoi(args);
            //     if (count <= 0) {
            //         printf("Not a valid number: %s\n", args);
            //         break;
            //     }
            //     printf("Blink LED %d times with a %dms pulse.\n", count, msec);
            //     // led_blink_times(led_pin_num, (size_t) count, (uint32_t) msec);
            //     break;
            // case cmd_led_blink_sos:
            //     printf("Enter milliseconds: ");
            //     args = uart_gets();
            //     printf("\n");
            //     msec = atoi(args);
            //     if (msec <= 0) {
            //         printf("Not valid ms: %s\n", args);
            //         break;
            //     }
            //     printf("Blink SOS on LED, with a %dms time interval.\n", msec);
            //     // led_blink_sos(led_pin_num, (uint32_t) msec);
            //     break;
            // case cmd_create_procs:
            //     printf("Enter number of process(es): ");
            //     args = uart_gets();
            //     printf("\n");
            //     proc_num = atoi(args);
            //     if (proc_num <= 0 || proc_num >= NR_TASKS) {
            //         printf("Not a valid number: %s\n", args);
            //         printf("Total processes must be: 0 < procs < %d \n", NR_TASKS);
            //         break;
            //     }
            //     printf("Creating %d process(es)...\n", proc_num);
            //     // create_processes(proc_num);
            //     printf("Done\n");
            //     break;
            // case cmd_run_procs:
            //     /* Initialize Timer 3 for scheduler */
            //     printf("Initializing Timer 3...");
            //     // timer_3_init(2000);
            //     printf("Done\n");
            //     /* Schedule */
            //     printf("Entering in scheduling mode...\n");
            //     while(1) {
            //         /*
            //         * Core scheduler function.
            //         * Checks whether there is a new task,
            //         * that needs to preempt the current one.
            //         */
            //         // schedule();
            //         /* Continue or Stop, based on user input */
            //         // timer_3_stop();
            //         printf("Continue? [y/n]: ");
            //         args = uart_gets();
            //         printf("\n");
            //         if (strcmp(args, "n") == 0)
            //             break;
            //         else {
            //             timer_3_init(2000);
            //             continue;
            //         }
            //     }
            //     /* Stop Timer 3 from calling the scheduler */
            //     // timer_3_stop();
            //     break;
            // case cmd_kill_procs:
            //     printf("Killing all process(es)...\n");
            //     // kill_processes();
            //     printf("Done\n");
            //     break;
            // case cmd_halt:
            //     printf("Halt.\n");
            //     printf("So long and thanks for all the fish...\n");
            //     return;
            default:
                printf("%s: command not found\n", input);
                printf("type 'help' to see the available commands\n");
                break;
        }
    }

}

void console_help() {
    printf("Available commands:\n");
    printf("    help:\n");
    printf("        Prints available commands to the console.\n");
    printf("    help_led:\n");
    printf("        Prints available LED commands to the console.\n");
    printf("    create_procs:\n");
    printf("        Creates proc_num kernel processes.\n");
    printf("    run_procs:\n");
    printf("        Runs the created kernel processes concurrently.\n");
    printf("    kill_procs:\n");
    printf("        Kills all created kernel processes.\n");
    printf("    halt:\n");
    printf("        Halts the system.\n");
}

void console_help_led() {
    printf("Available LED commands:\n");
    printf("    led_pin:\n");
    printf("        Changes the GPIO Pin for the LED to control.\n");
    printf("    led_on:\n");
    printf("        Turns the LED on.\n");
    printf("    led_off:\n");
    printf("        Turns the LED off.\n");
    printf("    led_irq_on:\n");
    printf("        Pulses the LED with a msec milliseconds interval, using System Timer's Interrupt.\n");
    printf("    led_irq_off:\n");
    printf("        Stop the System Timer's Interrupt.\n");
    printf("    led_on_ms:\n");
    printf("        Turns the LED on for msec milliseconds.\n");
    printf("    led_blink_times:\n");
    printf("        Blinks LED count times for msec milliseconds.\n");
    printf("    led_blink_sos:\n");
    printf("        Blinks SOS on LED with a msec milliseconds interval.\n");
}