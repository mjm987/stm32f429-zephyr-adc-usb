#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/cdc_acm.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdint.h>

#include "usb_dma_handler.h"
#include "ring_buffer.h"

LOG_MODULE_REGISTER(usb_dma, LOG_LEVEL_INF);

/* USB CDC ACM Device */
static const struct device *cdc_dev;

/* USB Transfer Configuration */
#define USB_TX_TIMEOUT K_MSEC(100)
#define SAMPLES_PER_USB_PACKET 42  /* 512 bytes / 12 bytes per sample */

static uint8_t usb_tx_buffer[512] __attribute__((aligned(4)));
static volatile uint32_t usb_ready = 0;
static volatile uint32_t packets_sent = 0;

int usb_dma_init(void)
{
	int ret;

	LOG_INF("===== USB CDC ACM Initialization =====");
	LOG_INF("Transport: USB CDC ACM (serial over USB)");
	LOG_INF("Samples per packet: %u", SAMPLES_PER_USB_PACKET);

	/* Get CDC ACM device */
	cdc_dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);
	if (!device_is_ready(cdc_dev)) {
		LOG_ERR("CDC ACM device not ready");
		return -ENODEV;
	}

	/* Enable USB device */
	ret = usb_enable(NULL);
	if (ret != 0) {
		LOG_ERR("Failed to enable USB: %d", ret);
		return ret;
	}

	usb_ready = 1;
	LOG_INF("USB CDC ACM device enabled - waiting for host connection");

	return 0;
}

void usb_dma_thread_func(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	struct adc_sample samples[SAMPLES_PER_USB_PACKET];
	uint32_t samples_available;
	uint32_t samples_to_send;
	int ret;
	uint32_t total_packets = 0;

	LOG_INF("USB DMA thread started");

	k_sleep(K_MSEC(1000)); /* Wait for USB enumeration and host connection */

	while (1) {
		if (!usb_ready) {
			k_sleep(K_MSEC(100));
			continue;
		}

		/* Check ring buffer for available samples */
		samples_available = ring_buffer_count();

		if (samples_available >= SAMPLES_PER_USB_PACKET) {
			samples_to_send = SAMPLES_PER_USB_PACKET;

			/* Get samples from ring buffer */
			if (ring_buffer_get(samples, samples_to_send)) {
				
				/* Send via CDC ACM (USB serial) */
				ret = cdc_acm_write(cdc_dev, (const uint8_t *)samples,
						   samples_to_send * sizeof(struct adc_sample),
						   NULL);
				
				if (ret >= 0) {
					total_packets++;
					packets_sent = total_packets;

					if (total_packets % 1000 == 0) {
						uint32_t bytes_sent = total_packets * SAMPLES_PER_USB_PACKET * 
						                      sizeof(struct adc_sample);
						LOG_INF("USB: %u packets sent (%.2f MB) | Ring: %u/%u",
								total_packets,
								bytes_sent / (1024.0 * 1024.0),
								ring_buffer_count(),
								RING_BUFFER_SIZE);
					}
				} else if (ret == -EBUSY) {
					/* Host not reading, put sample back */
					ring_buffer_put_back(samples, samples_to_send);
					k_sleep(K_MSEC(10));
				} else {
					LOG_WRN("CDC write error: %d", ret);
					k_sleep(K_MSEC(100));
				}
			}
		} else {
			k_sleep(K_USEC(100));
		}
	}
}

uint32_t usb_dma_get_packets_sent(void)
{
	return packets_sent;
}
