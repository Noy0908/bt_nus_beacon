#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <uart_async_adapter.h>
#include <zephyr/settings/settings.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/types.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/reboot.h>

#include "uart_app.h"
#include "ble_app.h"
#include "nus_setting_app.h"
#include "error_code.h"

LOG_MODULE_DECLARE(peripheral_uart);

#define BT_MAC_ADDR_SIZE 	6
#define TX_QUEUE_COUNT		20

static const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(nordic_nus_uart));
static struct k_work_delayable uart_work;

K_FIFO_DEFINE(fifo_uart_tx_data);
K_FIFO_DEFINE(fifo_uart_rx_data);

K_MSGQ_DEFINE(tx_send_queue, sizeof(nus_data_t), TX_QUEUE_COUNT, 4);

#ifdef CONFIG_UART_ASYNC_ADAPTER
UART_ASYNC_ADAPTER_INST_DEFINE(async_adapter);
#else
#define async_adapter NULL
#endif


uint8_t device_state = 0;

bool transparent_flag = false;


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


static void update_adv_payload(struct uart_cmd_rsp_t * uart_data, struct uart_cmd_rsp_t *response)
{
	if(uart_data->len)	//write command
	{
		LOG_INF("Update advertising data");
		int16_t err = 0;

		if (uart_data->len > DYNAMIC_MANUF_DATA_SIZE - strlen(device_name)) {
			LOG_WRN("Input string too long. Truncating...");
		}

		manuf_size = MIN((uart_data->len), DYNAMIC_MANUF_DATA_SIZE - strlen(device_name));
		LOG_INF("manuf_size---name_length: %i---%i", manuf_size, strlen(device_name));
		memcpy(dynamic_manuf_data + COMPANY_ID_SIZE, uart_data->data, manuf_size);

		if(is_ble_connected())
		{
			err = update_advertising();
		}	
		// err = update_advertising();
		if(err)
		{
			response->cmd = HOST_COMMAND_ERROR_CODE_CMD;
		}
		else
		{
			response->cmd = HOST_SET_ADV_PAYLOAD_CMD;
		}
		// memcpy(response->data, &err, sizeof(err));
		response->data[0] = (err >> 8) & 0xFF;
		response->data[1] = err & 0xFF;
		response->len = sizeof(err);
	}
	else	//read commmand
	{
		response->cmd = HOST_SET_ADV_PAYLOAD_CMD;
		response->len = manuf_size;
		memcpy(response->data, dynamic_manuf_data + COMPANY_ID_SIZE, manuf_size);
	}
}

static void force_disconnect_ble(struct uart_cmd_rsp_t *response)
{
	int16_t err = disconnect_ble();
	if(err)
	{
		response->cmd = HOST_COMMAND_ERROR_CODE_CMD;
		LOG_WRN("Failed to disconnect BLE connection: %d", err);
	}
	else
	{
		response->cmd = HOST_DISCONN_BLE_CMD;
	}
	response->len = sizeof(err);
	// memcpy(response->data, &err, sizeof(err));
	response->data[0] = (err >> 8) & 0xFF;
	response->data[1] = err & 0xFF;
}

static void set_passkey(struct uart_cmd_rsp_t * uart_data, struct uart_cmd_rsp_t *response)
{ 
	int16_t err = 0;
	if (uart_data->len != PASSKEY_ENTRY_LENGTH) {
		LOG_WRN("Invalid passkey length");
		err = -EINVAL;
	}
	else
	{
		passkey = strtoul(uart_data->data, NULL, 10);
		// memcpy(&passkey, uart_data->data, sizeof(passkey));
		LOG_INF("Passkey set to: %06u", passkey);
		err = bt_passkey_entry(passkey);
	}

	if(err)
	{
		response->cmd = HOST_COMMAND_ERROR_CODE_CMD;
	}
	else
	{
		response->cmd = HOST_SET_PASSKEY_CMD;
	}
	// memcpy(response->data, &err, sizeof(err));
	response->data[0] = (err >> 8) & 0xFF;
	response->data[1] = err & 0xFF;
	response->len = sizeof(err);
}


static void set_uart_baudrate(struct uart_cmd_rsp_t * uart_data, struct uart_cmd_rsp_t *response)
{
	int16_t err = 0;
	if(uart_data->len)
	{
		err = save_new_baudrate(uart_data->data, uart_data->len);
		if(err)
		{
			response->cmd = HOST_COMMAND_ERROR_CODE_CMD;
		}
		else
		{
			response->cmd = HOST_SET_UART_BAUDRATE_CMD;
		}

		response->data[0] = (err >> 8) & 0xFF;
		response->data[1] = err & 0xFF;
		response->len = sizeof(err);
	}
	else			//read command
	{
		response->cmd = HOST_SET_UART_BAUDRATE_CMD;
		uint32_t baudrate = get_uart_baudrate();
		response->data[0] = baudrate>>24 & 0xFF;
		response->data[1] = baudrate>>16 & 0xFF;
		response->data[2] = baudrate>>8 & 0xFF;
		response->data[3] = baudrate & 0xFF;
		response->len = sizeof(baudrate);
	}
}


static void uart_set_mac_address(struct uart_cmd_rsp_t * uart_data, struct uart_cmd_rsp_t *response)
{
	if(uart_data->len)		//write command
	{
		// int16_t err = set_ble_mac_address(uart_data->data, uart_data->len);
		int16_t err = save_mac_address(uart_data->data, uart_data->len);
		if(err)
		{
			response->cmd = HOST_COMMAND_ERROR_CODE_CMD;
		}
		else
		{
			response->cmd = HOST_SET_BLE_MAC_ADDRESS_CMD;
		}
		// memcpy(response->data, &err, sizeof(err));
		response->data[0] = (err >> 8) & 0xFF;
		response->data[1] = err & 0xFF;
		response->len = sizeof(err);
	}
	else			//read command
	{
		response->cmd = HOST_SET_BLE_MAC_ADDRESS_CMD;
		response->len = BT_MAC_ADDR_SIZE;
		get_ble_mac_address(response->data);
	}
}


static void set_device_name(struct uart_cmd_rsp_t * uart_data, struct uart_cmd_rsp_t * response)
{
	int16_t err = 0;

	if(uart_data->len)		//write command
	{
		if (uart_data->len > MAX_NAME_LEN) {
			LOG_WRN("Device name too long. Max length is 15 characters.");
		}
	
		uint8_t name_len = MIN(uart_data->len, MAX_NAME_LEN);
		memcpy(device_name, uart_data->data, name_len);
		device_name[name_len] = '\0';
		LOG_INF("Device name set to: %s", device_name);
		if(!is_ble_connected())
		{
			err = set_ble_device_name(device_name);
			err += update_advertising();
		}
		else
		{
			err = set_ble_device_name(device_name);
		}
	
		if(err)
		{
			response->cmd = HOST_COMMAND_ERROR_CODE_CMD;
		}
		else
		{
			response->cmd = HOST_SET_DEVICE_NAME_CMD;
		}
		// memcpy(response->data, &err, sizeof(err));
		response->data[0] = (err >> 8) & 0xFF;
		response->data[1] = err & 0xFF;
		response->len = sizeof(err);
	}
	else			//read command
	{
		response->cmd = HOST_SET_DEVICE_NAME_CMD;
		response->len = strlen(device_name);
		memcpy(response->data, device_name, response->len);
	}
	
}

static int read_device_info(struct uart_cmd_rsp_t *response)
{
	uint8_t len = 0;
	uint8_t val[BT_MAC_ADDR_SIZE] = {0};

	response->cmd = HOST_READ_DEVICE_INFO_CMD;
	memcpy(response->data, device_name, strlen(device_name));
	len += strlen(device_name);
	response->data[len++] = 0X2C;			//' , ' as the separator

	memcpy(&response->data[len], CONFIG_FW_VERSION, strlen(CONFIG_FW_VERSION));
	len += strlen(CONFIG_FW_VERSION);
	response->data[len++] = 0X2C;			//' , ' as the separator
	
	get_ble_mac_address(val);
	memcpy(&response->data[len], val, sizeof(val));
	len += sizeof(val);

	response->len = len;
	return 0;
}

static void erase_bond_device(struct uart_cmd_rsp_t *response)
{
	int16_t err = erase_bond_peer();
	if(err)
	{
		response->cmd = HOST_COMMAND_ERROR_CODE_CMD;
	}
	else
	{
		response->cmd = HOST_ERASE_BOND_DEVICE_CMD;
		set_device_status(STATUS_PAIRED | STATUS_BONDED, 0);    //clean the paired device status
	}
	// memcpy(response->data, &err, sizeof(err));
	response->data[0] = (err >> 8) & 0xFF;
	response->data[1] = err & 0xFF;
	response->len = sizeof(err);	
}


static void read_ble_rssi(struct uart_cmd_rsp_t *response)
{
	int8_t rssi;
	int16_t err = read_conn_rssi(&rssi);
	if(err)
	{
		response->cmd = HOST_COMMAND_ERROR_CODE_CMD;
		// memcpy(response->data, &err, sizeof(err));
		response->data[0] = (err >> 8) & 0xFF;
		response->data[1] = err & 0xFF;
		response->len = sizeof(err);
	}
	else
	{
		LOG_INF("BLE RSSI: %d", rssi);
		response->cmd = HOST_READ_BLE_RSSI_CMD;
		response->data[0] = (rssi >> 8) & 0xFF;
		response->data[1] = rssi & 0xFF;
		response->len = 2;
	}
}


static void uart_send_response(struct uart_cmd_rsp_t response)
{
	struct uart_data_t *response_payload = k_malloc(sizeof(*response_payload));
	if (response_payload) {
		response_payload->len = 0;
	} else {
		LOG_WRN("Not able to allocate UART send buffer");
	}

	response_payload->data[response_payload->len++] = response.cmd;
	response_payload->data[response_payload->len++] = (response.len >> 8)&0xFF;
	response_payload->data[response_payload->len++] = response.len & 0xFF;
	// memcpy(&response_payload->data[response_payload->len], &response.len, sizeof(response.len));
	// response_payload->len += sizeof(response.len);
	memcpy(&response_payload->data[response_payload->len], response.data, response.len);
	response_payload->len += response.len;

	uart_send_data(response_payload);
}

static void reset_device(void)
{
	struct uart_cmd_rsp_t response = {0};
	response.cmd =	HOST_RESET_DEVICE_CMD;
	response.len = 1;
	response.data[0] = 0;
	uart_send_response(response);
	k_sleep(K_MSEC(100));
	sys_reboot(SYS_REBOOT_COLD);
}


void uart_send_URC(uint8_t data, uint16_t len)
{
	struct uart_cmd_rsp_t response = {0};
	response.cmd = HOST_REPORT_URC_CMD;
	response.len = len;
	// memcpy(response.data, data, len);
	response.data[0] = data;
	uart_send_response(response);
}


// set device state
void set_device_status(uint8_t bitmask, int value) {
    if (value == 1) {
        device_state |= bitmask;  // set bit to 1
    } else {
        device_state &= ~bitmask; // set bit to 0
    }
}

// get device state
uint8_t get_device_status(void) {
    // return (device_state & bitmask) ? 1 : 0;  
	return device_state;
}


void on_packet_nus_data(uint8_t *buffer, uint16_t length)
{
	int err;
	nus_data_t send_buffer = {0};
	
	send_buffer.data = k_calloc(length+1, sizeof(uint8_t));
	if (send_buffer.data != NULL) 
	{
		memcpy(send_buffer.data, buffer, length);		
		send_buffer.length = length;

		if(0 == k_msgq_num_free_get(&tx_send_queue))
		{
			/* message queue is full ,we have to delete the oldest data to reserve room for the new data */
			nus_data_t data_buffer = {0};
			k_msgq_get(&tx_send_queue, &data_buffer, K_NO_WAIT);
			LOG_DBG("Droped data:[%d]:%s\n",data_buffer.length, data_buffer.data);
			k_free(data_buffer.data);
		}

		/** send the uart data to tcp server*/
		err = k_msgq_put(&tx_send_queue, &send_buffer, K_NO_WAIT);
		if (err) {
			LOG_ERR("Message sent error: %d", err);
			k_free(send_buffer.data);
		}
	} 
	else 
	{
		LOG_ERR("Memory not allocated!\n");
	}
}


void handle_uart_data(struct uart_data_t * uart_data)
{
	struct uart_cmd_rsp_t response = {0};

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
		response.cmd = HOST_UART_PING_CMD;
		response.len = sizeof(device_state);
		response.data[0] = get_device_status();
        LOG_INF("Received command HOST_UART_PING_CMD");
        break;
    case HOST_SEND_NUS_DATA_CMD:
		transparent_flag = true;
		response.cmd = HOST_SEND_NUS_DATA_CMD;
		response.len = 2;
		response.data[0] = SUCCEED;
        LOG_INF("Received command HOST_SEND_NUS_DATA_CMD");
        break;
    case HOST_SET_ADV_PAYLOAD_CMD:
        update_adv_payload(&command, &response);
        LOG_INF("Received command HOST_SET_UART_BAUDRATE_CMD");
        break;
    case HOST_DISCONN_BLE_CMD:
		force_disconnect_ble(&response);
        LOG_INF("Received command HOST_DISCONN_BLE_CMD");
        break;
    case HOST_SET_PASSKEY_CMD:
        set_passkey(&command, &response);
        LOG_INF("Received command HOST_SET_PASSKEY_CMD");
        break; 
    case HOST_SET_UART_BAUDRATE_CMD:
        set_uart_baudrate(&command, &response);
        LOG_INF("Received command HOST_SET_UART_BAUDRATE_CMD");
        break;
    case HOST_SET_BLE_MAC_ADDRESS_CMD:
        uart_set_mac_address(&command, &response);
        LOG_INF("Received command HOST_SET_BLE_MAC_ADDRESS_CMD");
        break;
    case HOST_READ_DEVICE_INFO_CMD:
        read_device_info(&response);
        LOG_INF("Received command HOST_READ_DEVICE_INFO_CMD");
        break;
    case HOST_ERASE_BOND_DEVICE_CMD:
        erase_bond_device(&response);
        LOG_INF("Received command HOST_ERASE_BOND_DEVICE_CMD");
        break;
    case HOST_RESET_DEVICE_CMD:
        reset_device();
        LOG_INF("Received command HOST_RESET_DEVICE_CMD");
        break;
    case HOST_READ_BLE_RSSI_CMD:
        read_ble_rssi(&response);
        LOG_INF("Received command HOST_READ_BLE_RSSI_CMD");
        break;
	case HOST_SET_DEVICE_NAME_CMD:
		set_device_name(&command, &response);
		LOG_INF("Received command HOST_SET_DEVICE_NAME_CMD");
		break;
    default:
		response.cmd = HOST_COMMAND_ERROR_CODE_CMD;
		response.len = 2;
		int16_t err = -ENOCMD;
		response.data[0] = (err >> 8) & 0xFF;
		response.data[1] = err & 0xFF;
		LOG_WRN("Received unknown command");
        break;
    }   

	uart_send_response(response);		//send response to host mcu
}


static void reconfigure_uart(const struct device * dev, uint32_t new_value)
{
	struct uart_config cfg = {
        .baudrate = new_value,
        .parity = UART_CFG_PARITY_NONE,
        .stop_bits = UART_CFG_STOP_BITS_1,
        .data_bits = UART_CFG_DATA_BITS_8,
        .flow_ctrl = UART_CFG_FLOW_CTRL_NONE
    };
    int err = uart_configure(dev, &cfg);
	if(err)
	{
		LOG_ERR("Failed to reconfigure UART baudrate: %d", err);
	}
 	// NRF_UARTE0->BAUDRATE =  0x01D60000;
}

int uart_init(void)
{
	int err;
	struct uart_data_t *rx;

	if (!device_is_ready(uart)) {
		return -ENODEV;
	}
	
	reconfigure_uart(uart, get_uart_baudrate());

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

	// uart_send_URC("READY", strlen("READY"));
	uart_send_URC(BLE_READY_URC, BLE_URC_LENGTH);
	set_device_status(STATUS_NUS_READY, 1);    //set the device status to ready

	return err;
}
