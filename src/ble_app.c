#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <bluetooth/services/nus.h>
#include <zephyr/settings/settings.h>
#include <bluetooth/services/nus.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/types.h>
#include <zephyr/bluetooth/hci_vs.h>

#include "ble_app.h"
#include "uart_app.h"
#include "nus_setting_app.h"
#include "error_code.h"

LOG_MODULE_DECLARE(peripheral_uart);

K_SEM_DEFINE(ble_init_ok, 0, 1);

static struct bt_conn *auth_conn;
static struct k_work advertise_start_work;

bool name_changed = false;

struct bt_conn *current_conn;

uint8_t manuf_size = 0;
unsigned int passkey = 0;

char device_name[MAX_NAME_LEN+1] = DEVICE_NAME;
uint8_t dynamic_manuf_data[DYNAMIC_MANUF_DATA_SIZE + COMPANY_ID_SIZE] =
	{CONFIG_BT_COMPANY_ID};

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_VAL),
	BT_DATA(BT_DATA_NAME_COMPLETE, device_name, DEVICE_NAME_LEN),
};


// static struct bt_data sd[] = {
// 	BT_DATA(BT_DATA_NAME_COMPLETE, device_name, DEVICE_NAME_LEN),
// 	BT_DATA(BT_DATA_MANUFACTURER_DATA, dynamic_manuf_data, sizeof(dynamic_manuf_data)),
// };

static void advertise_start_without_sd(struct k_work *work)
{
	int err;
	struct bt_le_adv_param adv_params = *(BT_LE_ADV_CONN_ONE_TIME);
	adv_params.interval_min = CONFIG_BT_NUS_ADVERTISING_INTERVAL;
	adv_params.interval_max = CONFIG_BT_NUS_ADVERTISING_INTERVAL; 

	if (manuf_size > 0) {
		uint8_t name_len = MIN(strlen(device_name), MAX_NAME_LEN);

		struct bt_data new_ad[] = {
			BT_DATA(BT_DATA_NAME_COMPLETE, device_name, name_len),
			BT_DATA(BT_DATA_MANUFACTURER_DATA, dynamic_manuf_data, manuf_size + COMPANY_ID_SIZE),
		};
		err = bt_le_adv_start(&adv_params, new_ad, ARRAY_SIZE(new_ad), NULL, 0);
	}
	else
	{
		if(name_changed)
		{
			uint8_t name_len = MIN(strlen(device_name), MAX_NAME_LEN);

			struct bt_data new_ad[] = {
				BT_DATA(BT_DATA_NAME_COMPLETE, device_name, name_len),
				// BT_DATA(BT_DATA_MANUFACTURER_DATA, dynamic_manuf_data, manuf_size + COMPANY_ID_SIZE),
			};
			err = bt_le_adv_start(&adv_params, new_ad, ARRAY_SIZE(new_ad), NULL, 0);
		}
		else
		{
			err = bt_le_adv_start(&adv_params, ad, ARRAY_SIZE(ad), NULL, 0);
		}
	}

	if (err) {
		LOG_ERR("Advertising failed to start (err %d)", err);
		return;
	}

	set_device_status(STATUS_ADVERTISING, 1);    //set the device status to advertising

	LOG_INF("Advertising successfully started");
}


static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (err) {
		LOG_ERR("Connection failed, err 0x%02x %s", err, bt_hci_err_to_str(err));
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Connected %s", addr);

	current_conn = bt_conn_ref(conn);

	dk_set_led_on(CON_STATUS_LED);

	// uart_send_URC("CONNECTED", strlen("CONNECTED"));
	uart_send_URC(BLE_CONNECTED_URC, BLE_URC_LENGTH);
	set_device_status(STATUS_CONNECTED, 1);    //set the device status to connected
	set_device_status(STATUS_ADVERTISING, 0);    //clean the advertising status
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Disconnected: %s, reason 0x%02x %s", addr, reason, bt_hci_err_to_str(reason));

	if (auth_conn) {
		bt_conn_unref(auth_conn);
		auth_conn = NULL;
	}

	if (current_conn) {
		bt_conn_unref(current_conn);
		current_conn = NULL;
		dk_set_led_off(CON_STATUS_LED);
	}
	// uart_send_URC("DISCONNECTED", strlen("DISCONNECTED"));
	uart_send_URC(BLE_DISCONNECTED_URC, BLE_URC_LENGTH);
	set_device_status(STATUS_CONNECTED, 0);    //set the device status to disconnected
}


#ifdef CONFIG_BT_NUS_SECURITY_ENABLED
static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		LOG_INF("Security changed: %s level %u", addr, level);
	} else {
		LOG_WRN("Security failed: %s level %u err %d %s", addr, level, err,
			bt_security_err_to_str(err));
	}
}
#endif

static void on_conn_recycled(void)
{
	k_work_submit(&advertise_start_work);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected    = connected,
	.disconnected = disconnected,
	.recycled     = on_conn_recycled,
#ifdef CONFIG_BT_NUS_SECURITY_ENABLED
	.security_changed = security_changed,
#endif
};

#if defined(CONFIG_BT_NUS_SECURITY_ENABLED)
static void auth_passkey_entry(struct bt_conn *conn)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_INF("Passkey entered %s: %06u", addr, passkey);
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Pairing cancelled: %s", addr);
}


static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Pairing completed: %s, bonded: %d", addr, bonded);
	set_device_status(STATUS_PAIRED | STATUS_BONDED, 1);    //set the device status to paired and bonded
}


static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Pairing failed conn: %s, reason %d %s", addr, reason,
		bt_security_err_to_str(reason));
}

static struct bt_conn_auth_cb conn_auth_callbacks = {
	.passkey_entry = auth_passkey_entry,
	.cancel = auth_cancel,
};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed
};

int bt_passkey_entry(unsigned int passkey)
{
	return bt_conn_auth_passkey_entry(current_conn, passkey);
}
#else
static struct bt_conn_auth_cb conn_auth_callbacks;
static struct bt_conn_auth_info_cb conn_auth_info_callbacks;
#endif

static void bt_receive_cb(struct bt_conn *conn, const uint8_t *const data,
			  uint16_t len)
{
	char addr[BT_ADDR_LE_STR_LEN] = {0};

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, ARRAY_SIZE(addr));

	LOG_INF("Received data from: %s", addr);

	for (uint16_t pos = 0; pos != len;) {
		struct uart_data_t *tx = k_malloc(sizeof(*tx));

		if (!tx) {
			LOG_WRN("Not able to allocate UART send data buffer");
			return;
		}

		/* Keep the last byte of TX buffer for potential LF char. */
		size_t tx_data_size = sizeof(tx->data) - 1;

		if ((len - pos) > tx_data_size) {
			tx->len = tx_data_size;
		} else {
			tx->len = (len - pos);
		}

		memcpy(tx->data, &data[pos], tx->len);

		pos += tx->len;

		/* Append the LF character when the CR character triggered
		 * transmission from the peer.
		 */
		if ((pos == len) && (data[len - 1] == '\r')) {
			tx->data[tx->len] = '\n';
			tx->len++;
		}

		// err = uart_tx(uart, tx->data, tx->len, SYS_FOREVER_MS);
		// if (err) {
		// 	k_fifo_put(&fifo_uart_tx_data, tx);
		// }
		uart_send_data(tx);
	}
}

static struct bt_nus_cb nus_cb = {
	.received = bt_receive_cb,
};


bool is_ble_connected(void)
{
	if (current_conn) {
		return true;
	}
	else
	{
		return false;
	}
}


int update_advertising(void)
{
	int err = 0;
	uint8_t name_len = MIN(strlen(device_name), MAX_NAME_LEN);

	if (manuf_size > 0) 
	{
		manuf_size = MIN(DYNAMIC_MANUF_DATA_SIZE - strlen(device_name), manuf_size);
		struct bt_data new_ad[] = {
			BT_DATA(BT_DATA_NAME_COMPLETE, device_name, name_len),
			BT_DATA(BT_DATA_MANUFACTURER_DATA, dynamic_manuf_data, manuf_size + COMPANY_ID_SIZE),
		};
		err = bt_le_adv_update_data(new_ad, ARRAY_SIZE(new_ad), NULL, 0);
		LOG_INF("Update advertise payload[%d]: %s\n", manuf_size, dynamic_manuf_data + COMPANY_ID_SIZE);
	}
	else
	{
		struct bt_data new_ad[] = {
			BT_DATA(BT_DATA_NAME_COMPLETE, device_name, name_len),
			// BT_DATA(BT_DATA_MANUFACTURER_DATA, dynamic_manuf_data, manuf_size + COMPANY_ID_SIZE),
		};
		err = bt_le_adv_update_data(new_ad, ARRAY_SIZE(new_ad), NULL, 0);
	}

	return err;
}


// void ble_send_uart_data(struct uart_data_t * uart_data)
// {
// 	int err = 0;

// 	if (current_conn) {
// 		/* In a connection - send data via NUS */
// 		err = bt_nus_send(NULL, uart_data->data, uart_data->len);
// 		if (err) {
// 			LOG_WRN("Failed to send data over BLE connection: %d", err);
// 		}
// 	} else {
// 		/* Not in a connection - update scan response data */
// 		// if (uart_data->len < 2) {
// 		// 	LOG_INF("Disable scan response");
// 		// 	err = bt_le_adv_update_data(ad, ARRAY_SIZE(ad), NULL, 0);
// 		// 	if (err != 0) {
// 		// 		LOG_WRN("Faileed to update scan response dataq: %d", err);
// 		// 	}
// 		// } else 
// 		{
// 			LOG_INF("Update advertising data");

// 			if (uart_data->len > DYNAMIC_MANUF_DATA_SIZE - strlen(device_name)) {
// 				LOG_WRN("Input string too long. Truncating...");
// 			}

// 			manuf_size = MIN((uart_data->len), DYNAMIC_MANUF_DATA_SIZE - strlen(device_name));

// 			LOG_INF("manuf_size---name_length: %i---%i", manuf_size, strlen(device_name));

// 			memcpy(dynamic_manuf_data + COMPANY_ID_SIZE, uart_data->data, manuf_size);
// 			// uint8_t manuf_ad_len = manuf_size + COMPANY_ID_SIZE;
// 			// sd[0].data_len = manuf_size + COMPANY_ID_SIZE;
// 			// err = bt_le_adv_update_data(ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
// 			uint8_t name_len = MIN(strlen(device_name), MAX_NAME_LEN);
// 			manuf_size += COMPANY_ID_SIZE;
// 			struct bt_data new_ad[] = {
// 				BT_DATA(BT_DATA_NAME_COMPLETE, device_name, name_len),
// 				BT_DATA(BT_DATA_MANUFACTURER_DATA, dynamic_manuf_data, manuf_size),
// 			};
// 			err = bt_le_adv_update_data(new_ad, ARRAY_SIZE(new_ad), NULL, 0);
// 			if (err != 0) {
// 				LOG_WRN("Faileed to update adv dataq: %d", err);
// 			}
// 			else
// 			{
// 				LOG_INF("Update advertise payload[%d]: %s\n", manuf_size, dynamic_manuf_data + COMPANY_ID_SIZE);
// 			}
// 		}
// 	}
// }

int disconnect_ble(void)
{ 
	int err = 0;
	if (current_conn) {
		err = bt_conn_disconnect(current_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		if (err) {
			LOG_ERR("Failed to disconnect Bluetooth link (err %d)\n", err);
		}
	}
	else
	{
		err = -ENOTCONN;
	}
	return err;
}

int set_ble_device_name(char *name)
{
	int err = 0;
	if (strlen(name) > MAX_NAME_LEN) {
		LOG_WRN("Device name too long. Max length is 15 characters.");
		err = -EINVAL;
	}

	LOG_INF("Device name set to: %s", name);
	name_changed = true;
	err = bt_set_name(name);
	
	return err;
}

#if 1
int set_ble_mac_address(uint8_t  *val, uint8_t len)
{
	if (len != BT_ADDR_SIZE) {
		LOG_WRN("Invalid passkey length");
		return -EINVAL;
	}

	bt_addr_le_t custom_addr = {
		.type = BT_ADDR_LE_RANDOM,
	};
	memcpy(custom_addr.a.val, val, sizeof(custom_addr.a.val));

	int err = bt_id_create(&custom_addr, NULL);
	if (err) {
		LOG_ERR("Failed to set custom MAC address (err %d)\n", err);
	}
	return err;
}
#else
int set_ble_mac_address(uint8_t  *val, uint8_t len)
{
	if (len != BT_ADDR_SIZE) {
		LOG_WRN("Invalid passkey length");
		return -EINVAL;
	}

	int err = 0;
	struct net_buf *buf;
	struct bt_hci_cp_vs_write_bd_addr *cp;

	buf = bt_hci_cmd_create(BT_HCI_OP_VS_WRITE_BD_ADDR, sizeof(*cp));
	if (!buf) {
		return -ENOBUFS;
	}

	cp = net_buf_add(buf, sizeof(*cp));
	memcpy(cp->bdaddr.val, val, BT_ADDR_SIZE);

	err = bt_hci_cmd_send_sync(BT_HCI_OP_VS_WRITE_BD_ADDR, buf, NULL);
	if (err) {
		LOG_ERR("Failed to set custom MAC address (err %d)\n", err);
		return err;
	}

	err = bt_setup_public_id_addr();
	if (err) {
		LOG_ERR("Failed to set public address (err %d)\n", err);
		return err;
	}

	return err;
}
#endif


int get_ble_mac_address(uint8_t *val)
{
	bt_addr_le_t addrs[CONFIG_BT_ID_MAX];
	size_t count = CONFIG_BT_ID_MAX;

	bt_id_get(addrs, &count);
	if (count > 0) {
		// for(uint8_t i = 0; i < BT_ADDR_SIZE; i++)
		// {
		// 	*(val + i) = addrs[0].a.val[BT_ADDR_SIZE -1 -i];
		// }
		memcpy(val, addrs[0].a.val, sizeof(addrs[0].a.val));
		return 0;
	}
	return -ENODATA;
}

int erase_bond_peer(void)
{
	int err = bt_unpair(BT_ID_DEFAULT, BT_ADDR_LE_ANY);
	if (err) {
		LOG_INF("Cannot delete bond (err: %d)\n", err);
	}
	return err;
}


int16_t read_conn_rssi(int8_t *rssi)
{
    struct net_buf *buf, *rsp = NULL;
    struct bt_hci_cp_read_rssi *cp;
    struct bt_hci_rp_read_rssi *rp;

    int16_t err;
	uint16_t conn_handle;
	bt_hci_get_conn_handle(current_conn, &conn_handle);

    buf = bt_hci_cmd_create(BT_HCI_OP_READ_RSSI, sizeof(*cp));
    if (!buf) {
        LOG_ERR("Unable to allocate command buffer\n");
        return -ENOBUFS;
    }

    cp = net_buf_add(buf, sizeof(*cp));
    cp->handle = sys_cpu_to_le16(conn_handle);

    err = bt_hci_cmd_send_sync(BT_HCI_OP_READ_RSSI, buf, &rsp);
    if (err) {
        uint8_t reason = rsp ?
            ((struct bt_hci_rp_read_rssi *)rsp->data)->status : 0;
        LOG_ERR("Read RSSI err: %d reason 0x%02x\n", err, reason);
        return err;
    }

    rp = (void *)rsp->data;
    *rssi = rp->rssi;

    net_buf_unref(rsp);
	return err;
}



int nus_ble_init(void)
{
    int err;

    if (IS_ENABLED(CONFIG_BT_NUS_SECURITY_ENABLED)) {
		err = bt_conn_auth_cb_register(&conn_auth_callbacks);
		if (err) {
			printk("Failed to register authorization callbacks.\n");
			return err;
		}

		err = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
		if (err) {
			printk("Failed to register authorization info callbacks.\n");
			return err;
		}
	}

	k_work_init(&advertise_start_work, advertise_start_without_sd);

	uint8_t *mac_val = get_mac_address();
	if(mac_val && mac_val[0] != 0)
	{
		set_ble_mac_address(mac_val, BT_ADDR_SIZE);
	}

    err = bt_enable(NULL);
	if (err) {
		printk("Failed to enable ble stack!.\n");
		return err;
	}
	
   
	LOG_INF("Bluetooth initialized");

	k_sem_give(&ble_init_ok);

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
		// if(bt_get_name())
		// {
		// 	memset(device_name, 0, sizeof(device_name));
		// 	memcpy(device_name, bt_get_name(), strlen(bt_get_name()));
		// }
		
	}

	err = bt_nus_init(&nus_cb);
	if (err) {
		LOG_ERR("Failed to initialize UART service (err: %d)", err);
		return err;
	}

	k_work_submit(&advertise_start_work);

    return err;
}