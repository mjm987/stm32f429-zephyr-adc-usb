#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/logging/log.h>

#define RING_BUFFER_SIZE 131072  /* ~131ms @ 1 MSPS, size for DMA transfers */

struct adc_sample {
	uint64_t timestamp;   /* Microsecond timestamp */
	uint16_t channel_0;   /* 12-bit ADC value */
	uint16_t channel_1;   /* 12-bit ADC value */
	uint16_t channel_2;   /* 12-bit ADC value */
} __attribute__((packed));

void ring_buffer_init(void);
bool ring_buffer_put(const struct adc_sample *sample);
bool ring_buffer_put_batch(const struct adc_sample *samples, uint32_t count);
bool ring_buffer_put_back(const struct adc_sample *samples, uint32_t count);
bool ring_buffer_get(struct adc_sample *sample, uint32_t count);
bool ring_buffer_peek(struct adc_sample *sample, uint32_t count);
uint32_t ring_buffer_count(void);
uint32_t ring_buffer_available(void);

#endif /* RING_BUFFER_H */
