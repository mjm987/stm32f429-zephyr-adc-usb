#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdint.h>

#include "adc_dma_handler.h"
#include "ring_buffer.h"

LOG_MODULE_REGISTER(adc_dma, LOG_LEVEL_INF);

/* ADC Device */
static const struct device *adc_dev;

/* ADC Buffer for sequential reads */
#define ADC_BUFFER_SIZE 3  /* One sample per channel */
static int16_t adc_buffer[ADC_BUFFER_SIZE];

/* Sampling metrics */
static volatile uint32_t samples_taken = 0;
static uint64_t sample_timestamp = 0;

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
static struct adc_sequence adc_sequence = {
	.channels = (1 << channel_cfg[0].channel_id) | 
	            (1 << channel_cfg[1].channel_id) | 
	            (1 << channel_cfg[2].channel_id),
	.buffer = adc_buffer,
	.buffer_size = sizeof(adc_buffer),
	.resolution = 12,
	.oversampling = 0,
	.calibrate = true,
};

int adc_dma_init(void)
{
	int ret;

	LOG_INF("===== ADC Initialization (Zephyr 4.x) =====");
	LOG_INF("Mode: Software-triggered sampling");
	LOG_INF("Sampling: 1 MSPS on 3 channels");
	LOG_INF("Resolution: 12-bit");

	/* Get ADC device using device tree */
	adc_dev = DEVICE_DT_GET_ONE(st_stm32_adc);
	if (!device_is_ready(adc_dev)) {
		LOG_ERR("ADC device not ready");
		return -ENODEV;
	}
	LOG_INF("ADC device ready: %s", adc_dev->name);

	/* Configure ADC channels */
	for (int i = 0; i < NUM_CHANNELS; i++) {
		ret = adc_channel_setup(adc_dev, &channel_cfg[i]);
		if (ret != 0) {
			LOG_ERR("Failed to setup channel %d: %d", i, ret);
			return ret;
		}
		LOG_INF("  Channel %d configured (ADC_IN%u)", i, channel_cfg[i].channel_id);
	}

	LOG_INF("ADC initialization complete");

	return 0;
}

void adc_dma_thread_func(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	int ret;
	uint32_t samples_processed = 0;
	struct adc_sample sample;
	uint64_t start_time = k_uptime_ticks();

	LOG_INF("ADC thread started - sampling at 1 MSPS");

	while (1) {
		/* Read ADC channels */
		ret = adc_read(adc_dev, &adc_sequence);
		if (ret != 0) {
			LOG_WRN("ADC read failed: %d", ret);
			k_sleep(K_USEC(10));
			continue;
		}

		/* Prepare sample for ring buffer */
		sample.timestamp = k_uptime_ticks();
		sample.channel_0 = adc_buffer[0] & 0xFFF;  /* 12-bit mask */
		sample.channel_1 = adc_buffer[1] & 0xFFF;
		sample.channel_2 = adc_buffer[2] & 0xFFF;

		/* Add to ring buffer */
		if (ring_buffer_put(&sample)) {
			samples_processed++;
		} else {
			LOG_WRN("Ring buffer full - sample dropped!");
		}

		/* Log progress every 1 million samples */
		if (samples_processed % 1000000 == 0) {
			uint64_t elapsed_ticks = k_uptime_ticks() - start_time;
			double elapsed_sec = elapsed_ticks / 1000.0;
			double throughput = (samples_processed * 12) / (1024.0 * 1024.0) / elapsed_sec;
			
			LOG_INF("ADC: %u samples (%.2f sec) | Ring: %u/%u | Throughput: %.2f MB/s",
				samples_processed,
				elapsed_sec,
				ring_buffer_count(),
				RING_BUFFER_SIZE,
				throughput);
		}

		/* Maintain 1 MSPS sampling rate */
		k_sleep(K_USEC(1));
	}
}

uint32_t adc_dma_get_sample_count(void)
{
	return samples_taken;
}

uint32_t adc_dma_get_dma_complete_count(void)
{
	return samples_taken / ADC_BUFFER_SIZE;
}
