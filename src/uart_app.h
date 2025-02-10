/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef __UART_APP_H_
#define __UART_APP_H_


#define UART_BUF_SIZE               CONFIG_BT_NUS_UART_BUFFER_SIZE
#define UART_WAIT_FOR_BUF_DELAY     K_MSEC(50)
#define UART_WAIT_FOR_RX            CONFIG_BT_NUS_UART_RX_WAIT_TIME



struct uart_data_t {
	void *fifo_reserved;
	uint8_t data[UART_BUF_SIZE];
	uint16_t len;
};


// extern struct k_fifo fifo_uart_tx_data;
extern struct k_fifo fifo_uart_rx_data;


extern int uart_send_data(struct uart_data_t *tx);

extern int uart_init(void);


#endif