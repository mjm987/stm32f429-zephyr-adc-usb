#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include <stdint.h>

#include "ring_buffer.h"
#include "adc_dma_handler.h"
#include "usb_dma_handler.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define ADC_DMA_THREAD_STACKSIZE 2048
#define USB_DMA_THREAD_STACKSIZE 2048
#define MONITOR_THREAD_STACKSIZE 1024

/* Thread definitions */
K_THREAD_DEFINE(adc_dma_thread_id, ADC_DMA_THREAD_STACKSIZE, 
                adc_dma_thread_func, NULL, NULL, NULL,
                K_PRIO_COOP(0), 0, 0);

K_THREAD_DEFINE(usb_dma_thread_id, USB_DMA_THREAD_STACKSIZE, 
                usb_dma_thread_func, NULL, NULL, NULL,
                K_PRIO_COOP(1), 0, 0);

K_THREAD_DEFINE(monitor_thread_id, MONITOR_THREAD_STACKSIZE,
                monitor_thread_func, NULL, NULL, NULL,
                K_PRIO_COOP(7), 0, 0);

void monitor_thread_func(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    uint32_t last_samples = 0;
    uint32_t last_dma_count = 0;

    k_sleep(K_SECONDS(2));

    while (1) {
        k_sleep(K_SECONDS(5));

        uint32_t current_samples = adc_dma_get_sample_count();
        uint32_t current_dma_count = adc_dma_get_dma_complete_count();
        uint32_t ring_fill = ring_buffer_count();
        uint32_t samples_diff = current_samples - last_samples;
        uint32_t dma_diff = current_dma_count - last_dma_count;

        double duration = samples_diff / 1000000.0;
        double throughput = (samples_diff * 12) / (1024.0 * 1024.0) / duration;

        LOG_INF("=== System Status ===");
        LOG_INF("Total samples: %u (%.2f sec)", current_samples, current_samples / 1000000.0);
        LOG_INF("DMA transfers: %u (buffers: %u)", current_dma_count, dma_diff);
        LOG_INF("Throughput: %.2f MB/s", throughput);
        LOG_INF("Ring buffer: %u / %u samples (%.1f %%)",
                ring_fill, RING_BUFFER_SIZE,
                (ring_fill * 100.0) / RING_BUFFER_SIZE);
        LOG_INF("USB packets: %u", usb_dma_get_packets_sent());

        last_samples = current_samples;
        last_dma_count = current_dma_count;
    }
}

void main(void)
{
    int ret;

    LOG_INF("======================================");
    LOG_INF("STM32F429-Discovery ADC+USB Application");
    LOG_INF("Hardware: Timer-based 1 MSPS ADC");
    LOG_INF("Transfer: Double-buffered DMA");
    LOG_INF("USB: High-speed bulk transfer");
    LOG_INF("======================================");

    /* Initialize ring buffer */
    ring_buffer_init();

    /* Initialize ADC with DMA */
    ret = adc_dma_init();
    if (ret != 0) {
        LOG_ERR("ADC DMA initialization failed: %d", ret);
        return;
    }

    /* Initialize USB */
    ret = usb_dma_init();
    if (ret != 0) {
        LOG_ERR("USB initialization failed: %d", ret);
        return;
    }

    LOG_INF("=== All subsystems initialized ===");
    LOG_INF("ADC DMA thread: Priority COOP(0) - Highest");
    LOG_INF("USB DMA thread: Priority COOP(1)");
    LOG_INF("Monitor thread: Priority COOP(7) - Lowest");
    LOG_INF("System ready - Data acquisition started!");

    /* Main thread goes to sleep */
    while (1) {
        k_sleep(K_SECONDS(60));
    }
}
