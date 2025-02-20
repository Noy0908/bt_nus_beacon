/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

 #ifndef __NUS_SETTING_APP_H_
 #define __NUS_SETTING_APP_H_
 


 int save_mac_address(uint8_t *mac, uint8_t len);

 uint8_t * get_mac_address(void);

 void nus_settings_init(void);

 #endif