#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>

/* Match the firmware structure */
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

static volatile int running = 1;

void signal_handler(int sig) {
    running = 0;
}

int main(int argc, char *argv[])
{
    libusb_device_handle *dev_handle = NULL;
    libusb_context *ctx = NULL;
    FILE *output_file = NULL;
    uint8_t buffer[4096];
    int actual_length;
    int ret;
    uint32_t total_samples = 0;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <output_file>\n", argv[0]);
        fprintf(stderr, "Example: %s adc_data.raw\n", argv[0]);
        return 1;
    }

    signal(SIGINT, signal_handler);

    /* Initialize libusb */
    ret = libusb_init(&ctx);
    if (ret < 0) {
        fprintf(stderr, "Failed to initialize libusb: %s\n", libusb_error_name(ret));
        return 1;
    }

    /* Open device */
    dev_handle = libusb_open_device_with_vid_pid(ctx, VENDOR_ID, PRODUCT_ID);
    if (!dev_handle) {
        fprintf(stderr, "Device not found (VID: 0x%04X, PID: 0x%04X)\n", VENDOR_ID, PRODUCT_ID);
        libusb_exit(ctx);
        return 1;
    }

    printf("Device found!\n");

    /* Claim interface */
    ret = libusb_claim_interface(dev_handle, 0);
    if (ret < 0) {
        fprintf(stderr, "Failed to claim interface: %s\n", libusb_error_name(ret));
        libusb_close(dev_handle);
        libusb_exit(ctx);
        return 1;
    }

    /* Open output file */
    output_file = fopen(argv[1], "wb");
    if (!output_file) {
        fprintf(stderr, "Failed to open output file: %s\n", argv[1]);
        libusb_release_interface(dev_handle, 0);
        libusb_close(dev_handle);
        libusb_exit(ctx);
        return 1;
    }

    printf("Reading ADC data from device...\n");
    printf("Writing to: %s\n", argv[1]);
    printf("Press Ctrl+C to stop\n\n");

    /* Read data */
    while (running) {
        ret = libusb_bulk_transfer(dev_handle, USB_ENDPOINT_IN, buffer, sizeof(buffer),
                                   &actual_length, USB_TIMEOUT);

        if (ret == 0 && actual_length > 0) {
            fwrite(buffer, 1, actual_length, output_file);
            
            int num_samples = actual_length / sizeof(struct adc_sample);
            total_samples += num_samples;

            if (total_samples % 100000 == 0) {
                printf("Received: %u samples (%.2f MB)\n", total_samples,
                       (total_samples * sizeof(struct adc_sample)) / (1024.0 * 1024.0));
            }
        } else if (ret == LIBUSB_ERROR_TIMEOUT) {
            continue;
        } else if (ret < 0) {
            fprintf(stderr, "USB transfer error: %s\n", libusb_error_name(ret));
            break;
        }
    }

    printf("\n\nTransfer complete!\n");
    printf("Total samples received: %u\n", total_samples);
    printf("Data file size: %.2f MB\n", 
           (total_samples * sizeof(struct adc_sample)) / (1024.0 * 1024.0));

    fclose(output_file);
    libusb_release_interface(dev_handle, 0);
    libusb_close(dev_handle);
    libusb_exit(ctx);

    return 0;
}
