#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdint.h>

#include "adc_dma_handler.h"
#include "ring_buffer.h"

LOG_MODULE_REGISTER(adc_dma, LOG_LEVEL_INF);

/* ADC and DMA Devices */
static const struct device *adc_dev;
static const struct device *dma_dev;

/* DMA Configuration */
#define DMA_CHANNEL 0
#define NUM_DMA_BUFFERS 2
#define DMA_BUFFER_SIZE 4096  /* Samples per buffer */

/* Double-buffered DMA buffers - aligned for DMA */
static struct adc_dma_sample dma_buffers[NUM_DMA_BUFFERS][DMA_BUFFER_SIZE] 
    __attribute__((aligned(32)));

static volatile uint32_t active_buffer = 0;
static volatile uint32_t dma_complete_count = 0;

K_SEM_DEFINE(dma_complete_sem, 0, K_SEM_MAX_LIMIT);

/* ADC Channel Configuration (3 channels) */
static struct adc_channel_cfg channel_cfg[] = {
    {
        .gain = ADC_GAIN_1,
        .reference = ADC_REF_INTERNAL,
        .acquisition_time = ADC_ACQ_TIME_DEFAULT,
        .channel_id = 3,   /* PA3  - ADC1_IN3  */
        .differential = 0,
    },
    {
        .gain = ADC_GAIN_1,
        .reference = ADC_REF_INTERNAL,
        .acquisition_time = ADC_ACQ_TIME_DEFAULT,
        .channel_id = 10,  /* PC0  - ADC1_IN10 */
        .differential = 0,
    },
    {
        .gain = ADC_GAIN_1,
        .reference = ADC_REF_INTERNAL,
        .acquisition_time = ADC_ACQ_TIME_DEFAULT,
        .channel_id = 13,  /* PC3  - ADC1_IN13 */
        .differential = 0,
    },
};

#define NUM_CHANNELS ARRAY_SIZE(channel_cfg)

/* ADC Sequence */
static struct adc_sequence_options adc_options = {
    .interval_us = 1,   /* 1 µs = 1 MSPS */
    .extra_samplings = 0,
};

static struct adc_sequence adc_sequence = {
    .channels = (1 << channel_cfg[0].channel_id) | 
                (1 << channel_cfg[1].channel_id) | 
                (1 << channel_cfg[2].channel_id),
    .buffer = NULL,
    .buffer_size = 0,
    .resolution = 12,
    .oversampling = 0,
    .calibrate = true,
    .options = &adc_options,
};

/* DMA Callback - Called when DMA transfer completes */
static void dma_callback(const struct device *dev, void *user_data, 
                         uint32_t channel, int status)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(user_data);
    ARG_UNUSED(channel);

    if (status == DMA_STATUS_COMPLETE) {
        dma_complete_count++;
        k_sem_give(&dma_complete_sem);
    } else if (status == DMA_STATUS_ERROR) {
        LOG_ERR("DMA error detected!");
    }
}

int adc_dma_init(void)
{
    int ret;

    LOG_INF("===== ADC DMA Initialization =====");
    LOG_INF("Mode: Double-buffered DMA with hardware timer");
    LOG_INF("Sampling: 1 MSPS on 3 channels");
    LOG_INF("Buffer size: %u samples per DMA buffer", DMA_BUFFER_SIZE);

    /* Get ADC device */
    adc_dev = DEVICE_DT_GET(DT_INST(0, st_stm32_adc));
    if (!device_is_ready(adc_dev)) {
        LOG_ERR("ADC device not ready");
        return -ENODEV;
    }
    LOG_INF("ADC device ready");

    /* Get DMA device */
    dma_dev = DEVICE_DT_GET(DT_INST(0, st_stm32_dma));
    if (!device_is_ready(dma_dev)) {
        LOG_ERR("DMA device not ready");
        return -ENODEV;
    }
    LOG_INF("DMA device ready");

    /* Configure ADC channels */
    for (int i = 0; i < NUM_CHANNELS; i++) {
        ret = adc_channel_setup(adc_dev, &channel_cfg[i]);
        if (ret != 0) {
            LOG_ERR("Failed to setup channel %d: %d", i, ret);
            return ret;
        }
        LOG_INF("  Channel %d configured (ADC_IN%u)", i, channel_cfg[i].channel_id);
    }

    LOG_INF("ADC DMA initialization complete");
    LOG_INF("Buffers: 0x%p and 0x%p", &dma_buffers[0], &dma_buffers[1]);

    return 0;
}

/* Configure DMA for cyclic transfer between two buffers */
static int setup_dma_double_buffer(uint32_t buffer_index)
{
    struct dma_config dma_cfg = {0};
    struct dma_block_config dma_block = {0};
    int ret;

    /* ADC data register address (hardware dependent) */
    volatile uint32_t *adc_dr = (volatile uint32_t *)0x4001244C;  /* STM32F4 ADC1->DR */

    dma_cfg.dma_slot = 0;  /* ADC1 DMA slot */
    dma_cfg.channel_direction = PERIPHERAL_TO_MEMORY;
    dma_cfg.complete_callback_en = 1;
    dma_cfg.error_callback_en = 1;
    dma_cfg.dma_callback = dma_callback;
    dma_cfg.user_data = NULL;
    dma_cfg.head_block = &dma_block;

    dma_block.source_address = (uint32_t)adc_dr;
    dma_block.dest_address = (uint32_t)&dma_buffers[buffer_index][0];
    dma_block.block_size = DMA_BUFFER_SIZE * sizeof(struct adc_dma_sample);
    dma_block.source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
    dma_block.dest_addr_adj = DMA_ADDR_ADJ_INCREMENT;
    dma_block.source_reload_en = 0;
    dma_block.dest_reload_en = 0;
    dma_block.fifo_mode_control = 2;  /* 1/2 FIFO for DMA2 */
    dma_block.flow_control_mode = DMA_FLOW_CONTROL_PERIPHERAL;

    ret = dma_config(dma_dev, DMA_CHANNEL, &dma_cfg);
    if (ret != 0) {
        LOG_ERR("DMA config failed for buffer %u: %d", buffer_index, ret);
        return ret;
    }

    return 0;
}

void adc_dma_thread_func(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    int ret;
    uint32_t samples_processed = 0;
    uint32_t last_buffer = 0;
    uint32_t buffer_switches = 0;

    LOG_INF("ADC DMA thread started");

    /* Setup initial DMA on buffer 0 */
    ret = setup_dma_double_buffer(0);
    if (ret != 0) {
        LOG_ERR("Initial DMA setup failed");
        return;
    }

    /* Start DMA */
    ret = dma_start(dma_dev, DMA_CHANNEL);
    if (ret != 0) {
        LOG_ERR("DMA start failed: %d", ret);
        return;
    }

    LOG_INF("DMA transfer started on buffer 0");

    while (1) {
        /* Wait for DMA transfer complete */
        ret = k_sem_take(&dma_complete_sem, K_FOREVER);
        
        if (ret == 0) {
            uint32_t completed_buffer = active_buffer;
            
            /* Add completed buffer to ring buffer for USB transfer */
            if (ring_buffer_put_batch((struct adc_sample *)&dma_buffers[completed_buffer][0],
                                      DMA_BUFFER_SIZE)) {
                samples_processed += DMA_BUFFER_SIZE;
            } else {
                LOG_WRN("Ring buffer full - DMA data may be lost!");
            }

            /* Switch to next buffer */
            active_buffer = (active_buffer + 1) % NUM_DMA_BUFFERS;
            buffer_switches++;

            /* Setup DMA for next buffer */
            ret = setup_dma_double_buffer(active_buffer);
            if (ret != 0) {
                LOG_ERR("Failed to setup next DMA buffer");
                break;
            }

            /* Restart DMA on new buffer */
            ret = dma_start(dma_dev, DMA_CHANNEL);
            if (ret != 0) {
                LOG_ERR("DMA restart failed");
                break;
            }

            /* Log progress */
            if (samples_processed % (10000000 / DMA_BUFFER_SIZE) == 0) {
                LOG_INF("ADC: %u samples (%.2f sec) | Buffers: %u | Ring: %u/%u",
                        samples_processed,
                        samples_processed / 1000000.0,
                        buffer_switches,
                        ring_buffer_count(),
                        RING_BUFFER_SIZE);
            }
        }
    }
}

uint32_t adc_dma_get_sample_count(void)
{
    return dma_complete_count * DMA_BUFFER_SIZE;
}

uint32_t adc_dma_get_dma_complete_count(void)
{
    return dma_complete_count;
}
