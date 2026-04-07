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
 * @file      wire.c
 * @brief     wire source file (libgpiod v2 API)
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

#include "wire.h"
#include <gpiod.h>

/**
 * @brief gpio device name definition
 */
#define GPIO_DEVICE_NAME "/dev/gpiochip0"        /**< gpio device name */

/**
 * @brief gpio device line definition
 */
#define GPIO_DEVICE_LINE       17                /**< gpio device line */
#define GPIO_DEVICE_CLOCK_LINE 27                /**< gpio device clock line */

/**
 * @brief global var definition
 */
static struct gpiod_chip *gs_chip;                        /**< gpio chip handle */
static struct gpiod_line_request *gs_line_request;        /**< gpio line request handle */
static struct gpiod_chip *gs_clock_chip;                  /**< gpio clock chip handle */
static struct gpiod_line_request *gs_clock_line_request;  /**< gpio clock line request handle */
static volatile uint8_t gs_read_write_flag;               /**< read write flag (0=input, 1=output, 2=uninit) */

/**
 * @brief      helper: request a single line as input
 * @param[in]  chip      gpiod chip handle
 * @param[in]  offset    line offset
 * @param[out] req       pointer to receive the new request
 * @param[in]  consumer  consumer string
 * @return     0 on success, 1 on failure
 */
static uint8_t s_request_input(struct gpiod_chip *chip, unsigned int offset,
                                struct gpiod_line_request **req, const char *consumer)
{
    struct gpiod_line_settings *settings;
    struct gpiod_line_config *line_cfg;
    struct gpiod_request_config *req_cfg;

    settings = gpiod_line_settings_new();
    if (settings == NULL)
        return 1;
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);

    line_cfg = gpiod_line_config_new();
    if (line_cfg == NULL)
    {
        gpiod_line_settings_free(settings);
        return 1;
    }
    if (gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings) < 0)
    {
        gpiod_line_settings_free(settings);
        gpiod_line_config_free(line_cfg);
        return 1;
    }
    gpiod_line_settings_free(settings);

    req_cfg = gpiod_request_config_new();
    if (req_cfg == NULL)
    {
        gpiod_line_config_free(line_cfg);
        return 1;
    }
    gpiod_request_config_set_consumer(req_cfg, consumer);

    *req = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
    gpiod_request_config_free(req_cfg);
    gpiod_line_config_free(line_cfg);

    return (*req != NULL) ? 0 : 1;
}

/**
 * @brief      helper: request a single line as output with initial value
 * @param[in]  chip          gpiod chip handle
 * @param[in]  offset        line offset
 * @param[out] req           pointer to receive the new request
 * @param[in]  consumer      consumer string
 * @param[in]  initial_value initial output value (0 or 1)
 * @return     0 on success, 1 on failure
 */
static uint8_t s_request_output(struct gpiod_chip *chip, unsigned int offset,
                                 struct gpiod_line_request **req, const char *consumer,
                                 int initial_value)
{
    struct gpiod_line_settings *settings;
    struct gpiod_line_config *line_cfg;
    struct gpiod_request_config *req_cfg;

    settings = gpiod_line_settings_new();
    if (settings == NULL)
        return 1;
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
    gpiod_line_settings_set_output_value(settings,
        initial_value ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE);

    line_cfg = gpiod_line_config_new();
    if (line_cfg == NULL)
    {
        gpiod_line_settings_free(settings);
        return 1;
    }
    if (gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings) < 0)
    {
        gpiod_line_settings_free(settings);
        gpiod_line_config_free(line_cfg);
        return 1;
    }
    gpiod_line_settings_free(settings);

    req_cfg = gpiod_request_config_new();
    if (req_cfg == NULL)
    {
        gpiod_line_config_free(line_cfg);
        return 1;
    }
    gpiod_request_config_set_consumer(req_cfg, consumer);

    *req = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
    gpiod_request_config_free(req_cfg);
    gpiod_line_config_free(line_cfg);

    return (*req != NULL) ? 0 : 1;
}

/**
 * @brief  wire bus init
 * @return status code
 *         - 0 success
 *         - 1 init failed
 * @note   none
 */
uint8_t wire_init(void)
{
    /* open the gpio chip */
    gs_chip = gpiod_chip_open(GPIO_DEVICE_NAME);
    if (gs_chip == NULL)
    {
        perror("gpio: open failed.\n");

        return 1;
    }

    /* flag uninit — direction will be requested on first read/write */
    gs_read_write_flag = 2;
    gs_line_request = NULL;

    /* set high */
    return wire_write(1);
}

/**
 * @brief  wire bus deinit
 * @return status code
 *         - 0 success
 * @note   none
 */
uint8_t wire_deinit(void)
{
    if (gs_line_request != NULL)
    {
        gpiod_line_request_release(gs_line_request);
        gs_line_request = NULL;
    }

    /* close the chip */
    gpiod_chip_close(gs_chip);

    return 0;
}

/**
 * @brief      wire bus read data
 * @param[out] *value pointer to a data buffer
 * @return     status code
 *             - 0 success
 *             - 1 read failed
 * @note       none
 */
uint8_t wire_read(uint8_t *value)
{
    int res;

    /* switch to input if needed */
    if (gs_read_write_flag != 0)
    {
        if (gs_line_request != NULL)
        {
            gpiod_line_request_release(gs_line_request);
            gs_line_request = NULL;
        }

        if (s_request_input(gs_chip, GPIO_DEVICE_LINE, &gs_line_request, "gpio_input") != 0)
        {
            return 1;
        }

        gs_read_write_flag = 0;
    }

    /* read the value */
    res = gpiod_line_request_get_value(gs_line_request, GPIO_DEVICE_LINE);
    if (res < 0)
    {
        return 1;
    }

    /* set the value (ACTIVE=1, INACTIVE=0) */
    *value = (res == GPIOD_LINE_VALUE_ACTIVE) ? 1 : 0;

    return 0;
}

/**
 * @brief     wire bus write data
 * @param[in] value write data
 * @return    status code
 *            - 0 success
 *            - 1 write failed
 * @note      none
 */
uint8_t wire_write(uint8_t value)
{
    enum gpiod_line_value lv;

    /* switch to output if needed */
    if (gs_read_write_flag != 1)
    {
        if (gs_line_request != NULL)
        {
            gpiod_line_request_release(gs_line_request);
            gs_line_request = NULL;
        }

        if (s_request_output(gs_chip, GPIO_DEVICE_LINE, &gs_line_request,
                              "gpio_output", value) != 0)
        {
            return 1;
        }

        gs_read_write_flag = 1;

        /* initial value was already set in the request */
        return 0;
    }

    /* set the value */
    lv = value ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE;
    if (gpiod_line_request_set_value(gs_line_request, GPIO_DEVICE_LINE, lv) != 0)
    {
        return 1;
    }

    return 0;
}

/**
 * @brief  wire clock bus init
 * @return status code
 *         - 0 success
 *         - 1 init failed
 * @note   none
 */
uint8_t wire_clock_init(void)
{
    /* open the gpio chip */
    gs_clock_chip = gpiod_chip_open(GPIO_DEVICE_NAME);
    if (gs_clock_chip == NULL)
    {
        perror("gpio: open failed.\n");

        return 1;
    }

    /* request clock line as output, initial high */
    if (s_request_output(gs_clock_chip, GPIO_DEVICE_CLOCK_LINE,
                          &gs_clock_line_request, "gpio_output", 1) != 0)
    {
        perror("gpio: request clock line failed.\n");
        gpiod_chip_close(gs_clock_chip);

        return 1;
    }

    /* set high */
    return wire_clock_write(1);
}

/**
 * @brief  wire clock bus deinit
 * @return status code
 *         - 0 success
 * @note   none
 */
uint8_t wire_clock_deinit(void)
{
    if (gs_clock_line_request != NULL)
    {
        gpiod_line_request_release(gs_clock_line_request);
        gs_clock_line_request = NULL;
    }

    /* close the chip */
    gpiod_chip_close(gs_clock_chip);

    return 0;
}

/**
 * @brief     wire clock bus write data
 * @param[in] value write data
 * @return    status code
 *            - 0 success
 *            - 1 write failed
 * @note      none
 */
uint8_t wire_clock_write(uint8_t value)
{
    enum gpiod_line_value lv;

    lv = value ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE;

    /* write the value */
    if (gpiod_line_request_set_value(gs_clock_line_request, GPIO_DEVICE_CLOCK_LINE, lv) != 0)
    {
        return 1;
    }

    return 0;
}
