#ifndef DRISTY_LED_H
#define DRISTY_LED_H

#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Dristy LED Status Indicator
 *
 * Uses the HuskyLens RGB LED and illumination LED to indicate system state.
 * A drone operator can see at a glance what Dristy is doing.
 *
 * LED modes:
 *   OFF:            all LEDs off (stealth / power saving)
 *   STATUS:         slow pulse colour indicates current mode
 *   DETECTION:      flash on each detection, colour = class
 *   TRACKING:       steady when target locked, blink when searching
 *   LANDING:        fast blink when target acquired, solid when close
 *   ERROR:          red rapid flash
 *
 * Colour coding:
 *   Blue    = detecting / searching
 *   Green   = target locked / tracking
 *   Yellow  = tag detected
 *   Cyan    = optical flow active
 *   Magenta = classification
 *   Red     = error / no model
 *   White   = idle / standby
 * ------------------------------------------------------------------------- */

typedef enum
{
    DRISTY_LED_OFF       = 0,
    DRISTY_LED_STATUS    = 1,
    DRISTY_LED_DETECTION = 2,
    DRISTY_LED_TRACKING  = 3,
    DRISTY_LED_LANDING   = 4,
    DRISTY_LED_ERROR     = 5,
    DRISTY_LED_CUSTOM    = 6,
} dristy_led_mode_t;

typedef struct
{
    uint8_t r, g, b;
} dristy_led_color_t;

/* Predefined colours */
#define DRISTY_LED_BLUE     ((dristy_led_color_t){  0,   0, 255})
#define DRISTY_LED_GREEN    ((dristy_led_color_t){  0, 255,   0})
#define DRISTY_LED_RED      ((dristy_led_color_t){255,   0,   0})
#define DRISTY_LED_YELLOW   ((dristy_led_color_t){255, 255,   0})
#define DRISTY_LED_CYAN     ((dristy_led_color_t){  0, 255, 255})
#define DRISTY_LED_MAGENTA  ((dristy_led_color_t){255,   0, 255})
#define DRISTY_LED_WHITE    ((dristy_led_color_t){255, 255, 255})

/* --- API ---------------------------------------------------------------- */

/* Initialise LED controller */
void dristy_led_init(void);

/* Set LED mode (auto-colours based on pipeline state) */
void dristy_led_set_mode(dristy_led_mode_t mode);

/* Set brightness (0-100%) */
void dristy_led_set_brightness(uint8_t percent);

/* Set custom colour (only used in CUSTOM mode) */
void dristy_led_set_color(dristy_led_color_t color);

/* Tick — call every ~33ms from main loop. Handles blinking/pulsing. */
void dristy_led_tick(void);

/* Set illumination LED (the white front LED) brightness (0-100) */
void dristy_led_set_illumination(uint8_t percent);

#endif /* DRISTY_LED_H */
