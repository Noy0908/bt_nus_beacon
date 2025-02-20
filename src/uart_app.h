/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef __UART_APP_H_
#define __UART_APP_H_


#define UART_MAX_PAYLOAD_SIZE       300
#define UART_BUF_SIZE               CONFIG_BT_NUS_UART_BUFFER_SIZE
#define UART_WAIT_FOR_BUF_DELAY     K_MSEC(50)
#define UART_WAIT_FOR_RX            CONFIG_BT_NUS_UART_RX_WAIT_TIME


/**
 * @brief Types of uart commands
 *
 * This enum defines the types of uart commands that can be sent to the nRF52
 * from the host MCU, some of the commands are used to control the nRF52 BLE 
 * stack, while others are used to control the nRF52 peripherals.
 * Parts of the commands may need a response from the nRF52.
 */
enum uart_cmd_type {
	/** 
     * @brief ping cmd. response with basic state or ERROR.
     * basic state, 8 bits - Bit number:
     * 0: (1) Connected  (0) Disconnected
     * 1: (1) Paired  (0) Unpaired
     * 2: (1) NUS Ready to send/receive data (0) NUT Not Ready
     * 3: (1) Advertising (0) not advertising
     * 4: (1) stored bond (0) no bond
     */
	HOST_UART_PING_CMD = 0x01,
	/**
	 * @brief host asked to send the payload to the app through NUS.
     * response with SUCCESS or ERROR.
	 */
	HOST_SEND_NUS_DATA_CMD,
	/**
	 * @brief host set the advertisement payload.
     * response with NONE or ERROR.
     * 
     * Payload format:
	 * 1 Byte: Tx power & preferred PHY in connection 
     * 1 Byte: interval (20ms increment)
     * 2 Byte: Manufacturer ID
     * 24 Bbytes: adv payload
	 */
	HOST_SET_ADV_PAYLOAD_CMD,
	/**
	 * @brief host forcefully disconnect the ble connection.
	 * response with SUCCESS or ERROR.
	 */
	HOST_DISCONN_BLE_CMD,
	/**
	 * @brief host set the passkey to nRF5 device to pair.
     * response with SUCCESS or ERROR.
	 */
	HOST_SET_PASSKEY_CMD,
	/**
	 * @brief host set the uart baud rate.
	 * response with SUCCESS or ERROR.
	 */
	HOST_SET_UART_BAUDRATE_CMD,
	/**
	 * @brief host set nRF5 amc address.
	 * response with SUCCESS or ERROR.
	 */
    HOST_SET_BLE_MAC_ADDRESS_CMD,
    /**
	 * @brief host read device information.
	 * response with device information or ERROR.
	 */
    HOST_READ_DEVICE_INFO_CMD,
    /**
	 * @brief host erase ble bond information.
	 * response with SUCCESS or ERROR.
	 */
    HOST_ERASE_BOND_DEVICE_CMD,
    /**
	 * @brief host ask to reset the ble device .
	 * response with SUCCESS or ERROR.
	 */
    HOST_RESET_DEVICE_CMD,
    /**
	 * @brief host read the ble rssi .
	 * response with BLE RSSI or ERROR.
	 */
    HOST_READ_BLE_RSSI_CMD,
	/**
	 * @brief host set the device name .
	 * response with SUCCESS or ERROR.
	 */
    HOST_SET_DEVICE_NAME_CMD,
	/**
	 * @brief slave send URC to host .
	 * response with SUCCESS or ERROR.
	 */
    HOST_REPORT_URC_CMD = 0xFE,
	/**
	 * @brief host commands excute failed .
	 * response with SUCCESS or ERROR.
	 */
    HOST_COMMAND_ERROR_CODE_CMD = 0xFF,
	/**
	 * @brief unused cmd, reserved.
	 *
	 */
	HOST_UNUSED_CMD,
};


struct uart_data_t {
	void *fifo_reserved;
	uint8_t data[UART_MAX_PAYLOAD_SIZE];
	uint16_t len;
};


struct uart_cmd_rsp_t {
	enum uart_cmd_type cmd;
	uint16_t len;
    uint8_t data[UART_MAX_PAYLOAD_SIZE];
};


// extern struct k_fifo fifo_uart_tx_data;
extern struct k_fifo fifo_uart_rx_data;

extern int uart_send_data(struct uart_data_t *tx);

extern void uart_send_URC(uint8_t data, uint16_t len);

extern void handle_uart_data(struct uart_data_t *uart_data);

extern int uart_init(void);


#endif