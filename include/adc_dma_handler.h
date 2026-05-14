#ifndef ADC_DMA_HANDLER_H
#define ADC_DMA_HANDLER_H

#include <stdint.h>
#include "ring_buffer.h"

/* DMA Sample Structure */
struct adc_dma_sample {
    uint64_t timestamp;
    uint16_t channel_0;
    uint16_t channel_1;
    uint16_t channel_2;
} __attribute__((packed));

int adc_dma_init(void);
void adc_dma_thread_func(void *arg1, void *arg2, void *arg3);
void monitor_thread_func(void *arg1, void *arg2, void *arg3);
uint32_t adc_dma_get_sample_count(void);
uint32_t adc_dma_get_dma_complete_count(void);

#endif /* ADC_DMA_HANDLER_H */
