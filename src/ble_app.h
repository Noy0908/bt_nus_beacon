/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef __BLE_APP_H_
#define __BLE_APP_H_

#include <zephyr/kernel.h>
#include <dk_buttons_and_leds.h>



#define DEVICE_NAME 					CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN					(sizeof(DEVICE_NAME) - 1)

#define MAX_NAME_LEN 				    15
#define DYNAMIC_MANUF_DATA_SIZE 		25
#define COMPANY_ID_SIZE					2
#define PASSKEY_ENTRY_LENGTH            6

#define CON_STATUS_LED 					DK_LED2


/** unsolicated response */
#define BLE_URC_LENGTH				    0x01        //length of unsolicated response

#define BLE_READY_URC				    0x01        //ble ready after device reboot
#define BLE_CONNECTED_URC				0x02        //BLE connect successfully
#define BLE_DISCONNECTED_URC			0x03        //BLE disconnect





extern struct k_sem ble_init_ok;

// extern struct bt_conn *current_conn;

extern uint8_t manuf_size;
extern unsigned int passkey;
extern char device_name[MAX_NAME_LEN+1];
extern uint8_t dynamic_manuf_data[DYNAMIC_MANUF_DATA_SIZE + COMPANY_ID_SIZE];

extern int nus_ble_init(void);

extern bool is_ble_connected(void);

extern int update_advertising(void);

extern int disconnect_ble(void);

extern int set_ble_device_name(char *name);

extern int set_ble_mac_address(uint8_t  *val, uint8_t len);

extern int get_ble_mac_address(uint8_t *val);

extern int16_t read_conn_rssi(int8_t *rssi);

#if defined(CONFIG_BT_NUS_SECURITY_ENABLED)
int bt_passkey_entry(unsigned int passkey);
#endif


#endif