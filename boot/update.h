#ifndef UPDATE_H
#define UPDATE_H

#include <stdint.h>
#include "frame.h"

/* Implemented in main.c. */
void send_frame(uint8_t type, const uint8_t *payload, uint16_t len);

/* Returns 1 if the frame was an update message, 0 if the type is not ours. */
int update_handle_frame(const frame_parser_t *f);

#endif
