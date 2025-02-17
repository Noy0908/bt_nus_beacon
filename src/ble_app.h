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


extern struct k_sem ble_init_ok;

extern struct bt_conn *current_conn;

extern uint8_t manuf_size;
extern unsigned int passkey;
extern char device_name[MAX_NAME_LEN+1];
extern uint8_t dynamic_manuf_data[DYNAMIC_MANUF_DATA_SIZE + COMPANY_ID_SIZE];

extern int nus_ble_init(void);

extern int update_advertising(void);

#if defined(CONFIG_BT_NUS_SECURITY_ENABLED)
void bt_passkey_entry(unsigned int passkey);
#endif


#endif