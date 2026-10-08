#ifndef BOOTSTATE_H
#define BOOTSTATE_H

#include <stdint.h>
#include "bootplan.h"

enum {
    BOOTSTATE_REQUESTED = 0,
    BOOTSTATE_BACKUP_DONE,
    BOOTSTATE_INSTALL_DONE,
    BOOTSTATE_TRIAL_STARTED,
    BOOTSTATE_CONFIRMED,
    BOOTSTATE_ROLLBACK_DONE,
    BOOTSTATE_COUNT
};

void bootstate_read(boot_flags_t *f);
int bootstate_set(uint32_t index); /* 0 = ok, -1 = error */
int bootstate_clear(void);         /* erases the state sector unless already blank */

#endif
