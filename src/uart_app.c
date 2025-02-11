#include <uart_async_adapter.h>
#include <bluetooth/services/nus.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>

#include "uart_app.h"
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

uint8_t basic_state = 0;


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

static void uart_send_response(enum uart_cmd_type cmd, uint8_t *data, uint16_t len)
{
    struct uart_data_t *response_payload = k_malloc(sizeof(*response_payload));
	if (response_payload) {
		response_payload->len = len + 3;
        response_payload->data[0] = cmd;
        response_payload->data[1] = len & 0xFF;
        response_payload->data[2] = (len >> 8) & 0xFF;
        memcpy(&response_payload->data[3], data, len);

        uart_send_data(response_payload);
	} else {
		LOG_WRN("Not able to allocate UART send buffer");
	}
}

extern struct bt_conn *current_conn;
void handle_uart_data(struct uart_data_t * uart_data)
{
    int err = 0;
    // struct uart_data_t *response_payload = k_malloc(sizeof(*response_payload));
	// if (response_payload) {
	// 	response_payload->len = 0;
	// } else {
	// 	LOG_WRN("Not able to allocate UART send buffer");
	// }

    struct uart_cmd_rsp_t *cmd_rsp = (struct uart_cmd_rsp_t *)uart_data->data;
    switch (cmd_rsp->cmd)
    {
    case HOST_UART_PING_CMD:
        uart_send_response(HOST_UART_PING_CMD, &basic_state, sizeof(basic_state));
        LOG_INF("Received command HOST_UART_PING_CMD");
        break;
    case HOST_SEND_NUS_DATA_CMD:
        if (current_conn) 
        {
            /* In a connection - send data via NUS */
            err = bt_nus_send(NULL, cmd_rsp->data, cmd_rsp->len);
            if (err) {
                // uart_send_response(HOST_SEND_NUS_DATA_CMD, &err, sizeof(err));
                LOG_WRN("Failed to send data over BLE connection: %d", err);
            }
	    }
        else
        {
            LOG_WRN("Not in a connection!");
        }
        LOG_INF("Received command HOST_SEND_NUS_DATA_CMD");
        break;
    case HOST_SET_ADV_PAYLOAD_CMD:
        // update_adv_payload(cmd_rsp->data, cmd_rsp->len);
        LOG_INF("Received command HOST_SET_UART_BAUDRATE_CMD");
        break;
    case HOST_DISCONN_BLE_CMD:
        // disconnect_ble();
        LOG_INF("Received command HOST_DISCONN_BLE_CMD");
        break;
    case HOST_SET_PASSKEY_CMD:
        // set_passkey(cmd_rsp->data, cmd_rsp->len);
        LOG_INF("Received command HOST_SET_PASSKEY_CMD");
        break; 
    case HOST_SET_UART_BAUDRATE_CMD:
        // set_uart_baudrate(cmd_rsp->data, cmd_rsp->len);
        LOG_INF("Received command HOST_SET_UART_BAUDRATE_CMD");
        break;
    case HOST_SET_BLE_MAC_ADDRESS_CMD:
        // set_ble_mac_address(cmd_rsp->data, cmd_rsp->len);
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
