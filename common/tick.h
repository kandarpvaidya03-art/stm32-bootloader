#ifndef TICK_H
#define TICK_H

void tick_init(void);   /* start a 1 ms tick (assumes the default 16 MHz clock) */
void tick_stop(void);
int tick_elapsed(void); /* returns 1 once for each millisecond that has passed */

#endif
