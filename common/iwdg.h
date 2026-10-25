#ifndef IWDG_H
#define IWDG_H

/* Independent watchdog, clocked from the ~32 kHz LSI oscillator.
 * Once started it cannot be stopped until the next reset. */
void iwdg_start(void); /* timeout about 4 s (2.7 to 7.5 s across the LSI's tolerance) */
void iwdg_kick(void);  /* reload the counter; harmless if the watchdog is not running */

/* Reset cause, read once at start-up. Clears the flags afterwards. */
int reset_was_watchdog(void);

#endif
