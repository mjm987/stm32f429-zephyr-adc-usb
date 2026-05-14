#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <math.h>
#include <libusb-1.0/libusb.h>

struct adc_sample {
    uint64_t timestamp;
    uint16_t channel_0;
    uint16_t channel_1;
    uint16_t channel_2;
} __attribute__((packed));

#define VENDOR_ID  0x2FE3
#define PRODUCT_ID 0x0100
#define USB_ENDPOINT_IN  0x81
#define USB_TIMEOUT 5000

struct stats {
    double min, max, mean, stddev;
    uint32_t samples;
};

static volatile int running = 1;

void signal_handler(int sig) {
    running = 0;
}

struct stats calculate_stats(uint16_t *data, uint32_t count)
{
    struct stats s = {0};
    double sum = 0, sum_sq = 0;

    if (count == 0) return s;

    s.min = data[0];
    s.max = data[0];
    s.samples = count;

    for (uint32_t i = 0; i < count; i++) {
        double val = data[i];
        if (val < s.min) s.min = val;
        if (val > s.max) s.max = val;
        sum += val;
        sum_sq += val * val;
    }

    s.mean = sum / count;
    s.stddev = sqrt((sum_sq / count) - (s.mean * s.mean));

    return s;
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <num_samples> <csv_file>\n", argv[0]);
        fprintf(stderr, "Example: %s 1000000 adc_data.csv\n", argv[0]);
        return 1;
    }

    uint32_t target_samples = atoi(argv[1]);
    const char *csv_file = argv[2];

    libusb_device_handle *dev_handle = NULL;
    libusb_context *ctx = NULL;
    FILE *csv_output = NULL;
    uint8_t buffer[4096];
    int actual_length, ret;
    uint32_t total_samples = 0;

    /* Allocate storage for channel data */
    uint16_t *ch0_data = malloc(target_samples * sizeof(uint16_t));
    uint16_t *ch1_data = malloc(target_samples * sizeof(uint16_t));
    uint16_t *ch2_data = malloc(target_samples * sizeof(uint16_t));

    if (!ch0_data || !ch1_data || !ch2_data) {
        fprintf(stderr, "Memory allocation failed\n");
        return 1;
    }

    signal(SIGINT, signal_handler);

    ret = libusb_init(&ctx);
    if (ret < 0) {
        fprintf(stderr, "libusb init failed: %s\n", libusb_error_name(ret));
        return 1;
    }

    dev_handle = libusb_open_device_with_vid_pid(ctx, VENDOR_ID, PRODUCT_ID);
    if (!dev_handle) {
        fprintf(stderr, "Device not found\n");
        libusb_exit(ctx);
        return 1;
    }

    ret = libusb_claim_interface(dev_handle, 0);
    if (ret < 0) {
        fprintf(stderr, "Failed to claim interface\n");
        libusb_close(dev_handle);
        libusb_exit(ctx);
        return 1;
    }

    csv_output = fopen(csv_file, "w");
    if (!csv_output) {
        fprintf(stderr, "Failed to open CSV file\n");
        libusb_close(dev_handle);
        libusb_exit(ctx);
        return 1;
    }

    fprintf(csv_output, "Timestamp_us,Channel_0,Channel_1,Channel_2\n");

    printf("Collecting %u samples...\n", target_samples);

    while (running && total_samples < target_samples) {
        ret = libusb_bulk_transfer(dev_handle, USB_ENDPOINT_IN, buffer, sizeof(buffer),
                                   &actual_length, USB_TIMEOUT);

        if (ret == 0 && actual_length > 0) {
            struct adc_sample *samples = (struct adc_sample *)buffer;
            int num_samples = actual_length / sizeof(struct adc_sample);

            for (int i = 0; i < num_samples && total_samples < target_samples; i++) {
                ch0_data[total_samples] = samples[i].channel_0;
                ch1_data[total_samples] = samples[i].channel_1;
                ch2_data[total_samples] = samples[i].channel_2;

                fprintf(csv_output, "%llu,%u,%u,%u\n",
                        (unsigned long long)samples[i].timestamp,
                        samples[i].channel_0,
                        samples[i].channel_1,
                        samples[i].channel_2);

                total_samples++;
            }

            if (total_samples % 100000 == 0) {
                printf("Collected: %u / %u samples\n", total_samples, target_samples);
            }
        }
    }

    printf("\n\n=== Data Collection Complete ===");
    printf("\nTotal samples: %u\n\n", total_samples);

    /* Calculate and display statistics */
    printf("=== Channel Statistics ===");
    printf("\n");
    
    struct stats s0 = calculate_stats(ch0_data, total_samples);
    printf("Channel 0: Min=%d, Max=%d, Mean=%.2f, StdDev=%.2f\n",
           (int)s0.min, (int)s0.max, s0.mean, s0.stddev);

    struct stats s1 = calculate_stats(ch1_data, total_samples);
    printf("Channel 1: Min=%d, Max=%d, Mean=%.2f, StdDev=%.2f\n",
           (int)s1.min, (int)s1.max, s1.mean, s1.stddev);

    struct stats s2 = calculate_stats(ch2_data, total_samples);
    printf("Channel 2: Min=%d, Max=%d, Mean=%.2f, StdDev=%.2f\n",
           (int)s2.min, (int)s2.max, s2.mean, s2.stddev);

    printf("\nCSV file written: %s\n", csv_file);

    fclose(csv_output);
    free(ch0_data);
    free(ch1_data);
    free(ch2_data);
    libusb_release_interface(dev_handle, 0);
    libusb_close(dev_handle);
    libusb_exit(ctx);

    return 0;
}
