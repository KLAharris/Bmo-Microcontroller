#ifndef MIC_STREAM_H
#define MIC_STREAM_H

void mic_stream_start(void);  /* call once, after MX_I2S2_Init() and MX_USART3_UART_Init() */
void mic_stream_poll(void);   /* call every pass of the main loop */

#endif /* MIC_STREAM_H */
