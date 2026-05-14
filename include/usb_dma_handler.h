#ifndef USB_DMA_HANDLER_H
#define USB_DMA_HANDLER_H

#include <stdint.h>

int usb_dma_init(void);
void usb_dma_thread_func(void *arg1, void *arg2, void *arg3);
uint32_t usb_dma_get_packets_sent(void);

#endif /* USB_DMA_HANDLER_H */
