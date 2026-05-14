#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_cdc.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdint.h>

#include "usb_dma_handler.h"
#include "ring_buffer.h"

LOG_MODULE_REGISTER(usb_dma, LOG_LEVEL_INF);

/* USB Bulk Endpoint Configuration */
#define USB_BULK_EP_IN  0x81
#define USB_BULK_MAX_PACKET 512

/* Double-buffered USB transfer buffers */
#define NUM_USB_BUFFERS 2
#define SAMPLES_PER_USB_PACKET (USB_BULK_MAX_PACKET / sizeof(struct adc_sample))

static uint8_t usb_tx_buffers[NUM_USB_BUFFERS][USB_BULK_MAX_PACKET] __attribute__((aligned(4)));
static volatile uint32_t usb_ready = 0;
static volatile uint32_t packets_sent = 0;

K_SEM_DEFINE(usb_tx_complete_sem, 1, 1);

int usb_dma_init(void)
{
    int ret;

    LOG_INF("===== USB Device Initialization =====");
    LOG_INF("Bulk endpoint: 0x%02X", USB_BULK_EP_IN);
    LOG_INF("Packet size: %u bytes", USB_BULK_MAX_PACKET);
    LOG_INF("Samples per packet: %u", SAMPLES_PER_USB_PACKET);

    /* Enable USB device */
    ret = usb_enable(NULL);
    if (ret != 0) {
        LOG_ERR("Failed to enable USB: %d", ret);
        return ret;
    }

    usb_ready = 1;
    LOG_INF("USB device enabled and ready");

    return 0;
}

void usb_dma_thread_func(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    struct adc_sample *samples;
    uint32_t samples_available;
    uint32_t samples_to_send;
    uint32_t buffer_idx = 0;
    uint32_t total_packets = 0;

    LOG_INF("USB DMA thread started");

    k_sleep(K_MSEC(500)); /* Wait for USB enumeration */

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
            if (ring_buffer_get((struct adc_sample *)&usb_tx_buffers[buffer_idx],
                               samples_to_send)) {
                
                /* Send USB bulk transfer (in real implementation, use USB driver) */
                LOG_DBG("USB: Sending packet %u (%u samples, %u bytes)",
                        total_packets,
                        samples_to_send,
                        samples_to_send * sizeof(struct adc_sample));

                total_packets++;
                packets_sent = total_packets;

                if (total_packets % 1000 == 0) {
                    uint32_t bytes_sent = total_packets * USB_BULK_MAX_PACKET;
                    LOG_INF("USB: %u packets sent (%.2f MB) | Ring: %u/%u",
                            total_packets,
                            bytes_sent / (1024.0 * 1024.0),
                            ring_buffer_count(),
                            RING_BUFFER_SIZE);
                }

                /* Switch buffer */
                buffer_idx = (buffer_idx + 1) % NUM_USB_BUFFERS;
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
