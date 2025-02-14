/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** @file
 *  @brief Nordic UART Bridge Service (NUS) with beacon sample
 */
// #include <uart_async_adapter.h>

#include <zephyr/types.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <soc.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/logging/log.h>

#include "ble_app.h"
#include "uart_app.h"

LOG_MODULE_REGISTER(peripheral_uart);


#define STACKSIZE 						0x1000
#define PRIORITY 						7

#define RUN_STATUS_LED 					DK_LED1
#define RUN_LED_BLINK_INTERVAL 			1000

#define KEY_PASSKEY_ACCEPT 				DK_BTN1_MSK
#define KEY_PASSKEY_REJECT 				DK_BTN2_MSK

#define FW_VERSION	 					"1.0.0"


void error(void)
{
	LOG_ERR("Panic!");

	while (true) {
		/* Spin for ever */
		k_sleep(K_MSEC(1000));
	}
}

static void configure_gpio(void)
{
	int err;

	err = dk_leds_init();
	if (err) {
		LOG_ERR("Cannot init LEDs (err: %d)", err);
	}
}


int main(void)
{
	int blink_status = 0;
	int err = 0;

	LOG_INF("NUS Beacon sample started, the version is %s", FW_VERSION);

	configure_gpio();

	err = uart_init();
	if (err) {
		error();
	}

	err = nus_ble_init();
	if (err) {
		error();
	}

	for (;;) {
		dk_set_led(RUN_STATUS_LED, (++blink_status) % 2);
		k_sleep(K_MSEC(RUN_LED_BLINK_INTERVAL));
	}
}



void ble_write_thread(void)
{
	/* Don't go any further until BLE is initialized */
	k_sem_take(&ble_init_ok, K_FOREVER);
	struct uart_data_t uart_data = {
		.len = 0,
	};

	for (;;) {
		/* Wait indefinitely for data to be sent over bluetooth */
		struct uart_data_t *buf = k_fifo_get(&fifo_uart_rx_data,
						     K_FOREVER);

		int plen = MIN(sizeof(uart_data.data) - uart_data.len, buf->len);
		int loc = 0;

		while (plen > 0) {
			memcpy(&uart_data.data[uart_data.len], &buf->data[loc], plen);
			uart_data.len += plen;
			loc += plen;
#ifdef CONFIG_BT_NUS_CRLF_UART_TERMINATION
			if (uart_data.len >= sizeof(uart_data.data) ||
			   (uart_data.data[uart_data.len - 1] == '\n') ||
			   (uart_data.data[uart_data.len - 1] == '\r')) {
#endif
				handle_uart_data(&uart_data);		//handle uart command from host mcu
				uart_data.len = 0;
#ifdef CONFIG_BT_NUS_CRLF_UART_TERMINATION
			}
#endif
			plen = MIN(sizeof(uart_data.data), buf->len - loc);
		}

		k_free(buf);
	}
}

K_THREAD_DEFINE(ble_write_thread_id, STACKSIZE, ble_write_thread, NULL, NULL,
		NULL, PRIORITY, 0, 0);
