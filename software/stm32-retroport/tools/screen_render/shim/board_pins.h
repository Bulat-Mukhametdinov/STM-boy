/* Host-side shim for board_pins.h.
 *
 * The real header maps GPIO ports/pins for the physical board. The render
 * harness never touches GPIO, and the only firmware header that includes it
 * (display_config.h) does not use any of those pin macros in the code we
 * compile, so an empty stand-in is enough.
 */
#ifndef HOST_SHIM_BOARD_PINS_H
#define HOST_SHIM_BOARD_PINS_H

#endif /* HOST_SHIM_BOARD_PINS_H */
