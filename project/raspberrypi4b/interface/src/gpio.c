/**
 * Copyright (c) 2015 - present LibDriver All rights reserved
 *
 * The MIT License (MIT)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * @file      gpio.c
 * @brief     gpio source file (libgpiod v2 API)
 * @version   1.0.0
 * @author    Shifeng Li
 * @date      2022-11-11
 *
 * <h3>history</h3>
 * <table>
 * <tr><th>Date        <th>Version  <th>Author      <th>Description
 * <tr><td>2022/11/11  <td>1.0      <td>Shifeng Li  <td>first upload
 * </table>
 */

#include "gpio.h"
#include <gpiod.h>
#include <pthread.h>

/**
 * @brief gpio device name definition
 */
#define GPIO_DEVICE_NAME "/dev/gpiochip0"        /**< gpio device name */

/**
 * @brief gpio device line definition
 */
#define GPIO_DEVICE_LINE 22                      /**< gpio device line */

/**
 * @brief global var definition
 */
static struct gpiod_chip *gs_chip;                        /**< gpio chip handle */
static struct gpiod_line_request *gs_line_request;        /**< gpio line request handle */
static struct gpiod_edge_event_buffer *gs_event_buffer;   /**< gpio edge event buffer */
static pthread_t gs_pid;                                  /**< gpio pthread pid */
extern uint8_t (*g_gpio_irq)(void);                       /**< gpio irq */

/**
 * @brief  gpio interrupt pthread
 * @param  *p pointer to an args buffer
 * @return NULL
 * @note   none
 */
static void *a_gpio_interrupt_pthread(void *p)
{
    int res;
    struct gpiod_edge_event *event;

    /* enable catching cancel signal */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

    /* cancel the pthread at once */
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    /* loop */
    while (1)
    {
        /* wait for the event (negative timeout = wait indefinitely) */
        res = gpiod_line_request_wait_edge_events(gs_line_request, -1);
        if (res == 1)
        {
            /* read the event */
            res = gpiod_line_request_read_edge_events(gs_line_request, gs_event_buffer, 1);
            if (res < 1)
            {
                continue;
            }

            /* get the first event */
            event = gpiod_edge_event_buffer_get_event(gs_event_buffer, 0);

            /* if the rising edge */
            if (gpiod_edge_event_get_event_type(event) == GPIOD_EDGE_EVENT_RISING_EDGE)
            {
                /* check the g_gpio_irq */
                if (g_gpio_irq != NULL)
                {
                    /* run the callback */
                    g_gpio_irq();
                }
            }
        }
    }
}

/**
 * @brief  gpio interrupt init
 * @return status code
 *         - 0 success
 *         - 1 init failed
 * @note   none
 */
uint8_t gpio_interrupt_init(void)
{
    int res;
    unsigned int offset = GPIO_DEVICE_LINE;
    struct gpiod_request_config *req_cfg = NULL;
    struct gpiod_line_config *line_cfg = NULL;
    struct gpiod_line_settings *settings = NULL;

    /* open the gpio chip */
    gs_chip = gpiod_chip_open(GPIO_DEVICE_NAME);
    if (gs_chip == NULL)
    {
        perror("gpio: open failed.\n");

        return 1;
    }

    /* configure line settings: input with rising edge detection */
    settings = gpiod_line_settings_new();
    if (settings == NULL)
    {
        perror("gpio: line settings alloc failed.\n");
        gpiod_chip_close(gs_chip);

        return 1;
    }
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_RISING);

    /* build line config */
    line_cfg = gpiod_line_config_new();
    if (line_cfg == NULL)
    {
        perror("gpio: line config alloc failed.\n");
        gpiod_line_settings_free(settings);
        gpiod_chip_close(gs_chip);

        return 1;
    }
    if (gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings) < 0)
    {
        perror("gpio: add line settings failed.\n");
        gpiod_line_settings_free(settings);
        gpiod_line_config_free(line_cfg);
        gpiod_chip_close(gs_chip);

        return 1;
    }
    gpiod_line_settings_free(settings);

    /* build request config */
    req_cfg = gpiod_request_config_new();
    if (req_cfg == NULL)
    {
        perror("gpio: request config alloc failed.\n");
        gpiod_line_config_free(line_cfg);
        gpiod_chip_close(gs_chip);

        return 1;
    }
    gpiod_request_config_set_consumer(req_cfg, "gpiointerrupt");

    /* request the line */
    gs_line_request = gpiod_chip_request_lines(gs_chip, req_cfg, line_cfg);
    gpiod_request_config_free(req_cfg);
    gpiod_line_config_free(line_cfg);

    if (gs_line_request == NULL)
    {
        perror("gpio: request line failed.\n");
        gpiod_chip_close(gs_chip);

        return 1;
    }

    /* allocate event buffer (capacity 1) */
    gs_event_buffer = gpiod_edge_event_buffer_new(1);
    if (gs_event_buffer == NULL)
    {
        perror("gpio: event buffer alloc failed.\n");
        gpiod_line_request_release(gs_line_request);
        gpiod_chip_close(gs_chip);

        return 1;
    }

    /* create a gpio interrupt pthread */
    res = pthread_create(&gs_pid, NULL, a_gpio_interrupt_pthread, NULL);
    if (res != 0)
    {
        perror("gpio: create pthread failed.\n");
        gpiod_edge_event_buffer_free(gs_event_buffer);
        gpiod_line_request_release(gs_line_request);
        gpiod_chip_close(gs_chip);

        return 1;
    }

    return 0;
}

/**
 * @brief  gpio interrupt deinit
 * @return status code
 *         - 0 success
 *         - 1 deinit failed
 * @note   none
 */
uint8_t gpio_interrupt_deinit(void)
{
    int res;

    /* close the gpio interrupt pthread */
    res = pthread_cancel(gs_pid);
    if (res != 0)
    {
        perror("gpio: delete pthread failed.\n");

        return 1;
    }

    /* free event buffer */
    gpiod_edge_event_buffer_free(gs_event_buffer);

    /* release the line request */
    gpiod_line_request_release(gs_line_request);

    /* close the gpio chip */
    gpiod_chip_close(gs_chip);

    return 0;
}
