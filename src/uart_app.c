#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <uart_async_adapter.h>
#include <bluetooth/services/nus.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/types.h>

#include "uart_app.h"
#include "ble_app.h"
#include "error_code.h"

LOG_MODULE_DECLARE(peripheral_uart);


static const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(nordic_nus_uart));
static struct k_work_delayable uart_work;

K_FIFO_DEFINE(fifo_uart_tx_data);
K_FIFO_DEFINE(fifo_uart_rx_data);

#ifdef CONFIG_UART_ASYNC_ADAPTER
UART_ASYNC_ADAPTER_INST_DEFINE(async_adapter);
#else
#define async_adapter NULL
#endif



bool name_changed = false;
uint8_t device_state = 0;

static void uart_cb(const struct device *dev, struct uart_event *evt, void *user_data)
{
	ARG_UNUSED(dev);

	static size_t aborted_len;
	struct uart_data_t *buf;
	static uint8_t *aborted_buf;
	static bool disable_req;

	switch (evt->type) {
	case UART_TX_DONE:
		LOG_DBG("UART_TX_DONE");
		if ((evt->data.tx.len == 0) ||
		    (!evt->data.tx.buf)) {
			return;
		}

		if (aborted_buf) {
			buf = CONTAINER_OF(aborted_buf, struct uart_data_t,
					   data[0]);
			aborted_buf = NULL;
			aborted_len = 0;
		} else {
			buf = CONTAINER_OF(evt->data.tx.buf, struct uart_data_t,
					   data[0]);
		}

		k_free(buf);

		buf = k_fifo_get(&fifo_uart_tx_data, K_NO_WAIT);
		if (!buf) {
			return;
		}

		if (uart_tx(uart, buf->data, buf->len, SYS_FOREVER_MS)) {
			LOG_WRN("Failed to send data over UART");
		}

		break;

	case UART_RX_RDY:
		LOG_DBG("UART_RX_RDY");
		buf = CONTAINER_OF(evt->data.rx.buf, struct uart_data_t, data[0]);
		buf->len += evt->data.rx.len;

		if (disable_req) {
			return;
		}
#ifdef CONFIG_BT_NUS_CRLF_UART_TERMINATION
		if ((evt->data.rx.buf[buf->len - 1] == '\n') ||
		    (evt->data.rx.buf[buf->len - 1] == '\r')) {
#endif
			disable_req = true;
			uart_rx_disable(uart);
#ifdef CONFIG_BT_NUS_CRLF_UART_TERMINATION
		}
#endif
		break;

	case UART_RX_DISABLED:
		LOG_DBG("UART_RX_DISABLED");
		disable_req = false;

		buf = k_malloc(sizeof(*buf));
		if (buf) {
			buf->len = 0;
		} else {
			LOG_WRN("Not able to allocate UART receive buffer");
			k_work_reschedule(&uart_work, UART_WAIT_FOR_BUF_DELAY);
			return;
		}

		uart_rx_enable(uart, buf->data, sizeof(buf->data),
			       UART_WAIT_FOR_RX);

		break;

	case UART_RX_BUF_REQUEST:
		LOG_DBG("UART_RX_BUF_REQUEST");
		buf = k_malloc(sizeof(*buf));
		if (buf) {
			buf->len = 0;
			uart_rx_buf_rsp(uart, buf->data, sizeof(buf->data));
		} else {
			LOG_WRN("Not able to allocate UART receive buffer");
		}

		break;

	case UART_RX_BUF_RELEASED:
		LOG_DBG("UART_RX_BUF_RELEASED");
		buf = CONTAINER_OF(evt->data.rx_buf.buf, struct uart_data_t,
				   data[0]);

		if (buf->len > 0) {
			k_fifo_put(&fifo_uart_rx_data, buf);
		} else {
			k_free(buf);
		}

		break;

	case UART_TX_ABORTED:
		LOG_DBG("UART_TX_ABORTED");
		if (!aborted_buf) {
			aborted_buf = (uint8_t *)evt->data.tx.buf;
		}

		aborted_len += evt->data.tx.len;
		buf = CONTAINER_OF((void *)aborted_buf, struct uart_data_t,
				   data);

		uart_tx(uart, &buf->data[aborted_len],
			buf->len - aborted_len, SYS_FOREVER_MS);

		break;

	default:
		break;
	}
}

static void uart_work_handler(struct k_work *item)
{
	struct uart_data_t *buf;

	buf = k_malloc(sizeof(*buf));
	if (buf) {
		buf->len = 0;
	} else {
		LOG_WRN("Not able to allocate UART receive buffer");
		k_work_reschedule(&uart_work, UART_WAIT_FOR_BUF_DELAY);
		return;
	}

	uart_rx_enable(uart, buf->data, sizeof(buf->data), UART_WAIT_FOR_RX);
}

static bool uart_test_async_api(const struct device *dev)
{
	const struct uart_driver_api *api =
			(const struct uart_driver_api *)dev->api;

	return (api->callback_set != NULL);
}

int uart_send_data(struct uart_data_t *tx)
{
    int err;
    err = uart_tx(uart, tx->data, tx->len, SYS_FOREVER_MS);
    if (err) {
        k_fifo_put(&fifo_uart_tx_data, tx);
    }
    return err; 
}

static void update_adv_payload(struct uart_cmd_rsp_t * uart_data)
{
	LOG_INF("Update advertising data");

	if (uart_data->len > DYNAMIC_MANUF_DATA_SIZE - strlen(device_name)) {
		LOG_WRN("Input string too long. Truncating...");
	}

	manuf_size = MIN((uart_data->len), DYNAMIC_MANUF_DATA_SIZE - strlen(device_name));

	LOG_INF("manuf_size---name_length: %i---%i", manuf_size, strlen(device_name));

	memcpy(dynamic_manuf_data + COMPANY_ID_SIZE, uart_data->data, manuf_size);
	// uint8_t manuf_ad_len = manuf_size + COMPANY_ID_SIZE;
	// sd[0].data_len = manuf_size + COMPANY_ID_SIZE;
	// err = bt_le_adv_update_data(ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	// uint8_t name_len = MIN(strlen(device_name), MAX_NAME_LEN);
	// manuf_size += COMPANY_ID_SIZE;

	update_advertising();
}

static int set_passkey(struct uart_cmd_rsp_t * uart_data)
{ 
	if (uart_data->len != PASSKEY_ENTRY_LENGTH) {
		LOG_WRN("Invalid passkey length");
		return -EINVAL;
	}

	passkey = strtoul(uart_data->data, NULL, 10);
	// memcpy(&passkey, uart_data->data, sizeof(passkey));
	LOG_INF("Passkey set to: %06u", passkey);
	return bt_passkey_entry(passkey);
}


static void set_device_name(struct uart_cmd_rsp_t * uart_data)
{
	if (uart_data->len > MAX_NAME_LEN) {
		LOG_WRN("Device name too long. Max length is 15 characters.");
    }

	uint8_t name_len = MIN(uart_data->len, MAX_NAME_LEN);
	memcpy(device_name, uart_data->data, name_len);
	device_name[name_len] = '\0';
	name_changed = true;
	LOG_INF("Device name set to: %s", device_name);
	bt_set_name(device_name);
	update_advertising();
}


static void uart_send_response(enum uart_cmd_type cmd, void *data, uint16_t len)
{
	struct uart_data_t *response_payload = k_malloc(sizeof(*response_payload));
	if (response_payload) {
		response_payload->len = 0;
	} else {
		LOG_WRN("Not able to allocate UART send buffer");
	}

	response_payload->data[response_payload->len++] = cmd;
	response_payload->data[response_payload->len++] = len >> 8;
	response_payload->data[response_payload->len++] = len & 0xFF;
	memcpy(&response_payload->data[response_payload->len], data, len);
	response_payload->len += len;

	uart_send_data(response_payload);
}


void handle_uart_data(struct uart_data_t * uart_data)
{
    int err = 0;
	// struct uart_cmd_rsp_t response = {0};

    struct uart_cmd_rsp_t command = {0};
	command.cmd = uart_data->data[0];
	command.len = (uart_data->data[1] << 8) | uart_data->data[2];
	if(command.len > UART_MAX_PAYLOAD_SIZE)
	{
		LOG_WRN("Invalid data length");
		return;
	}
	memcpy(command.data, &uart_data->data[3], command.len);

	LOG_INF("Received [%d] data from host MCU, cmd =  %0X", command.len,command.cmd);
    switch (command.cmd)
    {
    case HOST_UART_PING_CMD:
        uart_send_response(HOST_UART_PING_CMD, &device_state, sizeof(device_state));
        LOG_INF("Received command HOST_UART_PING_CMD");
        break;
    case HOST_SEND_NUS_DATA_CMD:
        // if (current_conn) 
        // {
        //     /* In a connection - send data via NUS */
        //     err = bt_nus_send(NULL, cmd_rsp.data, cmd_rsp.len);
        //     if (err) {
        //         // uart_send_response(HOST_SEND_NUS_DATA_CMD, &err, sizeof(err));
        //         LOG_WRN("Failed to send data over BLE connection: %d", err);
        //     }
	    // }
        // else
        // {
        //     LOG_WRN("Not in a connection, buffered data!");
        // }
		// k_msgq_put();	//send data to ble
        LOG_INF("Received command HOST_SEND_NUS_DATA_CMD");
        break;
    case HOST_SET_ADV_PAYLOAD_CMD:
        update_adv_payload(&command);
        LOG_INF("Received command HOST_SET_UART_BAUDRATE_CMD");
        break;
    case HOST_DISCONN_BLE_CMD:
        err = disconnect_ble();
		if(err)
		{
			uart_send_response(HOST_COMMAND_ERROR_CODE_CMD, &err, sizeof(err));
		}
		else
		{
			uart_send_response(HOST_DISCONN_BLE_CMD, &err, sizeof(err));
		}
        LOG_INF("Received command HOST_DISCONN_BLE_CMD");
        break;
    case HOST_SET_PASSKEY_CMD:
        err = set_passkey(&command);
		if(err)
		{
			uart_send_response(HOST_COMMAND_ERROR_CODE_CMD, &err, sizeof(err));
		}
		else
		{
			uart_send_response(HOST_SET_PASSKEY_CMD, &err, sizeof(err));
		}
        LOG_INF("Received command HOST_SET_PASSKEY_CMD");
        break; 
    case HOST_SET_UART_BAUDRATE_CMD:
        // set_uart_baudrate(&command);
        LOG_INF("Received command HOST_SET_UART_BAUDRATE_CMD");
        break;
    case HOST_SET_BLE_MAC_ADDRESS_CMD:
        err = set_ble_mac_address(command.data, command.len);
		if(err)
		{
			uart_send_response(HOST_COMMAND_ERROR_CODE_CMD, &err, sizeof(err));
		}
		else
		{
			uart_send_response(HOST_SET_BLE_MAC_ADDRESS_CMD, &err, sizeof(err));
		}
        LOG_INF("Received command HOST_SET_BLE_MAC_ADDRESS_CMD");
        break;
    case HOST_READ_DEVICE_INFO_CMD:
        // read_device_info();
        LOG_INF("Received command HOST_READ_DEVICE_INFO_CMD");
        break;
    case HOST_ERASE_BOND_DEVICE_CMD:
        // erase_bond_device();
        LOG_INF("Received command HOST_ERASE_BOND_DEVICE_CMD");
        break;
    case HOST_RESET_DEVICE_CMD:
        // reset_device();
        LOG_INF("Received command HOST_RESET_DEVICE_CMD");
        break;
    case HOST_READ_BLE_RSSI_CMD:
        // read_ble_rssi();
        LOG_INF("Received command HOST_READ_BLE_RSSI_CMD");
        break;
	case HOST_SET_DEVICE_NAME_CMD:
		set_device_name(&command);
		LOG_INF("Received command HOST_SET_DEVICE_NAME_CMD");
		break;
    default:
        LOG_WRN("Received unknown command");
        break;
    }

    // uart_send_data(response_payload);           //send response to host mcu
}


int uart_init(void)
{
	int err;
	struct uart_data_t *rx;

	if (!device_is_ready(uart)) {
		return -ENODEV;
	}

	rx = k_malloc(sizeof(*rx));
	if (rx) {
		rx->len = 0;
	} else {
		return -ENOMEM;
	}

	k_work_init_delayable(&uart_work, uart_work_handler);


	if (IS_ENABLED(CONFIG_UART_ASYNC_ADAPTER) && !uart_test_async_api(uart)) {
		/* Implement API adapter */
		uart_async_adapter_init(async_adapter, uart);
		uart = async_adapter;
	}

	err = uart_callback_set(uart, uart_cb, NULL);
	if (err) {
		k_free(rx);
		LOG_ERR("Cannot initialize UART callback");
		return err;
	}

	if (IS_ENABLED(CONFIG_UART_LINE_CTRL)) {
		LOG_INF("Wait for DTR");
		while (true) {
			uint32_t dtr = 0;

			uart_line_ctrl_get(uart, UART_LINE_CTRL_DTR, &dtr);
			if (dtr) {
				break;
			}
			/* Give CPU resources to low priority threads. */
			k_sleep(K_MSEC(100));
		}
		LOG_INF("DTR set");
		err = uart_line_ctrl_set(uart, UART_LINE_CTRL_DCD, 1);
		if (err) {
			LOG_WRN("Failed to set DCD, ret code %d", err);
		}
		err = uart_line_ctrl_set(uart, UART_LINE_CTRL_DSR, 1);
		if (err) {
			LOG_WRN("Failed to set DSR, ret code %d", err);
		}
	}

	err = uart_rx_enable(uart, rx->data, sizeof(rx->data), UART_WAIT_FOR_RX);
	if (err) {
		LOG_ERR("Cannot enable uart reception (err: %d)", err);
		/* Free the rx buffer only because the tx buffer will be handled in the callback */
		k_free(rx);
	}

	return err;
}
