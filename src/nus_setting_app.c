#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include "nus_setting_app.h"

LOG_MODULE_DECLARE(peripheral_uart);

#define STORAGE_PARTITION	    storage_partition
#define STORAGE_PARTITION_ID	FIXED_PARTITION_ID(STORAGE_PARTITION)

static uint8_t new_mac[6] = {0};
static uint32_t new_baudrate = 0;

static int nus_settings_set(const char *name, size_t len, settings_read_cb read_cb,
					 void *cb_arg)
{
	const char *next;
    int rc = 0;

	LOG_INF("set handler name=%s, len=%d \n", (name), len);
	if (settings_name_steq(name, "mac_address", &next) && !next)
	{
		if (len != sizeof(new_mac))
		{
			return -EINVAL;
		}
		rc = read_cb(cb_arg, new_mac, sizeof(new_mac));
        if(rc >= 0)
        {
             /* key-value pair was properly read. rc contains value length.*/
            LOG_INF("mac: %02X:%02X:%02X:%02X:%02X:%02X\n", new_mac[0], new_mac[1], new_mac[2], new_mac[3], new_mac[4], new_mac[5]);
		    return 0;
        }
	}

    if (settings_name_steq(name, "baudrate", &next) && !next)
	{
		if (len != sizeof(new_baudrate))
		{
			return -EINVAL;
		}
		rc = read_cb(cb_arg, &new_baudrate, sizeof(new_baudrate));
        if(rc >= 0)
        {
             /* key-value pair was properly read. rc contains value length.*/
            LOG_INF("baudrate: %d\n", new_baudrate);
		    return 0;
        }
	}

	return -ENOENT;
}


static int nus_settings_export(int (*storage_func)(const char *name,
    const void *value,
    size_t val_len))
{
    int err = 0;
    err = storage_func("nus/mac_address", new_mac, sizeof(new_mac));
    err += storage_func("nus/baudrate", &new_baudrate, sizeof(new_baudrate));
    return  err;
}

struct settings_handler nus_conf = {
    .name = "nus",
    .h_set = nus_settings_set,
    .h_export = nus_settings_export
};


int save_mac_address(uint8_t *mac, uint8_t len)
{
    int err = 0;

    if(len != sizeof(new_mac))
    {
        return -EINVAL;
    }
    memcpy(new_mac, mac, sizeof(new_mac));
    err = settings_save_one("nus/mac_address", new_mac, sizeof(new_mac));
    if(err)
    {
        LOG_ERR("Failed to save mac address");
    }

    return err;
}

int16_t save_new_baudrate(uint8_t *data, uint8_t len)
{
	int16_t err = 0;
	if(len != sizeof(new_baudrate))
    {
        return -EINVAL;
    }

	new_baudrate = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
	LOG_INF("Baudrate set to: %d", new_baudrate);
	if(new_baudrate < 9600 || new_baudrate > 1000000)
	{
		LOG_WRN("Invalid baudrate value!");
		err = -EINVAL;
	}
	
	err = settings_save_one("nus/baudrate", &new_baudrate, sizeof(new_baudrate));
	return err;
}


uint8_t * get_mac_address(void)
{
    if(settings_load_subtree("nus/mac_address"))
    {
        LOG_ERR("Failed to load nus settings");
        return NULL;
    }
    // LOG_INF("get mac: %02X:%02X:%02X:%02X:%02X:%02X\n", new_mac[0], new_mac[1], new_mac[2], new_mac[3], new_mac[4], new_mac[5]);
    return new_mac;
}

uint32_t get_uart_baudrate(void)
{
	if(settings_load_subtree("nus/baudrate"))
    {
        LOG_ERR("Failed to load nus settings");
        return 9600;
    }
    LOG_INF("get baudrate: %d\n", new_baudrate);

	new_baudrate = ((new_baudrate > 9600 && new_baudrate < 1000000) ? new_baudrate : 9600);
	
    return new_baudrate;
}


void nus_settings_init(void)
{
    settings_subsys_init();
    settings_register(&nus_conf);
}
