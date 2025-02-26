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
#include "nus_setting_app.h"

LOG_MODULE_REGISTER(peripheral_uart);


#define STACKSIZE 						0x2000
#define PRIORITY 						7

#define RUN_STATUS_LED 					DK_LED1
#define RUN_LED_BLINK_INTERVAL 			1000

#define NUS_TRANSPARENT_EXIT 			DK_BTN1_MSK
#define KEY_PASSKEY_REJECT 				DK_BTN2_MSK




void error(void)
{
	LOG_ERR("Panic!");

	while (true) {
		/* Spin for ever */
		k_sleep(K_MSEC(1000));
	}
}


static void button_changed(uint32_t button_state, uint32_t has_changed)
{
	uint32_t buttons = button_state & has_changed;

	if (transparent_flag) {
		if (buttons & NUS_TRANSPARENT_EXIT) {
			transparent_flag = false;
		}

		// if (buttons & KEY_PASSKEY_REJECT) {
		// 	num_comp_reply(false);
		// }
	}
}

static void configure_gpio(void)
{
	int err;

	err = dk_buttons_init(button_changed);
	if (err) {
		LOG_ERR("Cannot init buttons (err: %d)", err);
	}

	err = dk_leds_init();
	if (err) {
		LOG_ERR("Cannot init LEDs (err: %d)", err);
	}
}


int main(void)
{
	int blink_status = 0;
	int err = 0;

	LOG_INF("NUS Beacon sample started, the version is %s", CONFIG_FW_VERSION);

	configure_gpio();

	nus_settings_init();

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

	int ret = 0;
	nus_data_t send_packet = {0};
	static uint8_t resend_count = 0;
	struct uart_data_t uart_data = {
		.len = 0,
	};

	for (;;) 
	{
		/* Wait 300 for data from UART to be processed */
		struct uart_data_t *buf = k_fifo_get(&fifo_uart_rx_data, K_MSEC(300));
		if(buf)
		{
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
					if(!transparent_flag)
					{
						handle_uart_data(&uart_data);		//handle uart command from host mcu
					}
					else
					{
						on_packet_nus_data(uart_data.data, uart_data.len);
					}
					
					uart_data.len = 0;
	#ifdef CONFIG_BT_NUS_CRLF_UART_TERMINATION
				}
	#endif
				plen = MIN(sizeof(uart_data.data), buf->len - loc);
			}

			k_free(buf);
		}

		/* check if there are data need to be sent over bluetooth */
		if (k_msgq_peek(&tx_send_queue, &send_packet) == 0)  
		{
			// LOG_INF("[%d]:%s\n",send_packet.length, send_packet.data);
			ret = ble_send_uart_data(&send_packet);
			if(0 == ret)
			{
				/* send successfully, delete the queue message and free memory */
				k_msgq_get(&tx_send_queue, &send_packet, K_NO_WAIT);
				k_free(send_packet.data);
				resend_count = 0;
				// LOG_HEXDUMP_INF(send_packet.data, send_packet.length, "NUS send:\n");
			}
			else if(ret == -ENOTCONN)
			{
				// LOG_WRN("BLE is not connected, we need to reconnect it\n");
			}
			else
			{
				if(resend_count++ >= 3)
				{
					/* send failed, we need to wait more time */
					LOG_WRN("BLE send failed, will resend it.\n");
				}
			}
		}
	}
}

K_THREAD_DEFINE(ble_write_thread_id, STACKSIZE, ble_write_thread, NULL, NULL,
		NULL, PRIORITY, 0, 0);
