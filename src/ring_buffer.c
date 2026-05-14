#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdint.h>

#include "ring_buffer.h"

LOG_MODULE_REGISTER(ring_buf, LOG_LEVEL_DBG);

static struct adc_sample ring_buffer[RING_BUFFER_SIZE] __attribute__((aligned(32)));
static volatile uint32_t write_pos = 0;
static volatile uint32_t read_pos = 0;
static K_MUTEX_DEFINE(buffer_lock);

void ring_buffer_init(void)
{
    write_pos = 0;
    read_pos = 0;
    memset(ring_buffer, 0, sizeof(ring_buffer));
    LOG_INF("Ring buffer initialized - %u samples capacity (%.2f ms @ 1 MSPS)",
            RING_BUFFER_SIZE,
            RING_BUFFER_SIZE / 1000.0);
}

bool ring_buffer_put(const struct adc_sample *sample)
{
    k_mutex_lock(&buffer_lock, K_FOREVER);

    uint32_t next_write = (write_pos + 1) % RING_BUFFER_SIZE;

    if (next_write == read_pos) {
        k_mutex_unlock(&buffer_lock);
        return false;
    }

    memcpy(&ring_buffer[write_pos], sample, sizeof(struct adc_sample));
    write_pos = next_write;

    k_mutex_unlock(&buffer_lock);
    return true;
}

bool ring_buffer_put_batch(const struct adc_sample *samples, uint32_t count)
{
    if (count == 0) {
        return true;
    }

    k_mutex_lock(&buffer_lock, K_FOREVER);

    uint32_t available = (write_pos >= read_pos) 
                         ? (RING_BUFFER_SIZE - (write_pos - read_pos) - 1)
                         : (read_pos - write_pos - 1);

    if (available < count) {
        k_mutex_unlock(&buffer_lock);
        return false;
    }

    /* Copy samples */
    for (uint32_t i = 0; i < count; i++) {
        uint32_t pos = (write_pos + i) % RING_BUFFER_SIZE;
        memcpy(&ring_buffer[pos], &samples[i], sizeof(struct adc_sample));
    }

    write_pos = (write_pos + count) % RING_BUFFER_SIZE;

    k_mutex_unlock(&buffer_lock);
    return true;
}

bool ring_buffer_get(struct adc_sample *sample, uint32_t count)
{
    if (count == 0) {
        return true;
    }

    k_mutex_lock(&buffer_lock, K_FOREVER);

    uint32_t available = (write_pos >= read_pos) 
                         ? (write_pos - read_pos) 
                         : (RING_BUFFER_SIZE - read_pos + write_pos);

    if (available < count) {
        k_mutex_unlock(&buffer_lock);
        return false;
    }

    for (uint32_t i = 0; i < count; i++) {
        uint32_t pos = (read_pos + i) % RING_BUFFER_SIZE;
        memcpy(&sample[i], &ring_buffer[pos], sizeof(struct adc_sample));
    }

    read_pos = (read_pos + count) % RING_BUFFER_SIZE;

    k_mutex_unlock(&buffer_lock);
    return true;
}

bool ring_buffer_peek(struct adc_sample *sample, uint32_t count)
{
    if (count == 0) {
        return true;
    }

    k_mutex_lock(&buffer_lock, K_FOREVER);

    uint32_t available = (write_pos >= read_pos) 
                         ? (write_pos - read_pos) 
                         : (RING_BUFFER_SIZE - read_pos + write_pos);

    if (available < count) {
        k_mutex_unlock(&buffer_lock);
        return false;
    }

    for (uint32_t i = 0; i < count; i++) {
        uint32_t pos = (read_pos + i) % RING_BUFFER_SIZE;
        memcpy(&sample[i], &ring_buffer[pos], sizeof(struct adc_sample));
    }

    k_mutex_unlock(&buffer_lock);
    return true;
}

uint32_t ring_buffer_count(void)
{
    k_mutex_lock(&buffer_lock, K_FOREVER);

    uint32_t available = (write_pos >= read_pos) 
                         ? (write_pos - read_pos) 
                         : (RING_BUFFER_SIZE - read_pos + write_pos);

    k_mutex_unlock(&buffer_lock);
    return available;
}

uint32_t ring_buffer_available(void)
{
    k_mutex_lock(&buffer_lock, K_FOREVER);

    uint32_t available = (write_pos >= read_pos) 
                         ? (RING_BUFFER_SIZE - (write_pos - read_pos) - 1)
                         : (read_pos - write_pos - 1);

    k_mutex_unlock(&buffer_lock);
    return available;
}
