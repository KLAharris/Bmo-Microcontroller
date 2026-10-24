#ifndef BMO_P4_ADAPTER_H
#define BMO_P4_ADAPTER_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Register P4 Snake and Simon games with the P3 Game Manager.
 *
 * IMPORTANT:
 * Call this AFTER bmo_state_init(), because that function
 * initializes the Game Manager and clears its callbacks.
 */
void bmo_p4_adapter_register(void);

#ifdef __cplusplus
}
#endif

#endif /* BMO_P4_ADAPTER_H */
