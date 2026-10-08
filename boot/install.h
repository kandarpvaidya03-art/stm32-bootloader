#ifndef INSTALL_H
#define INSTALL_H

#include <stdint.h>

/* 1 if the slot holds an image with a valid hash and signature. */
int slot_image_ok(uint32_t slot_base);

/* Carries out pending update steps until an application may be started. */
void install_run_pending(void);

#endif
