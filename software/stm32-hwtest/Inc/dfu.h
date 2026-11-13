#ifndef DFU_H
#define DFU_H

/* Enter the ST system bootloader (USB DFU 0483:DF11).
   Sets the DFU magic in the .noinit block and resets; early_boot_check()
   performs the actual jump from the clean post-reset state.
   Safe to call from any context, including the USB ISR. Never returns. */
void dfu_request(void) __attribute__((noreturn));

#endif
