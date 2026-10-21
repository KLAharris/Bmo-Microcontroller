#ifndef WAKEWORD_H
#define WAKEWORD_H

#ifdef __cplusplus
extern "C" {
#endif

/* What wakeword_poll() heard. */
#define WW_NONE     0
#define WW_HEY_BMO  1
#define WW_GAMES    2   /* only reported within 4 s after "hey BMO" */
#define WW_MUSIC    3   /* only reported within 4 s after "hey BMO" */

void wakeword_start(void);  /* call once, after MX_I2S2_Init() and MX_USART3_UART_Init() */
int  wakeword_poll(void);   /* call every pass of the main loop; returns WW_* */
void wakeword_set_paused(int on);  /* 1 = stop listening (e.g. while BMO talks or a game runs) */

#ifdef __cplusplus
}
#endif

#endif /* WAKEWORD_H */
