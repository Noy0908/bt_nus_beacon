/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef __BLE_APP_H_
#define __BLE_APP_H_


#define DEVICE_NAME 					CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN					(sizeof(DEVICE_NAME) - 1)

#define DYNAMIC_MANUF_DATA_SIZE 		25
#define COMPANY_ID_SIZE					2
#define MAX_ADV_DATA_LEN 				31


extern struct k_sem ble_init_ok;

#endif