#ifndef BOOTPLAN_H
#define BOOTPLAN_H

#include <stdint.h>

typedef struct {
    uint8_t requested;     /* a verified image is waiting in slot B */
    uint8_t backup_done;   /* slot A has been copied to the backup sector */
    uint8_t install_done;  /* slot B has been copied to slot A */
    uint8_t trial_started; /* the new image has been started at least once */
    uint8_t confirmed;     /* the new image reported that it works */
    uint8_t rollback_done; /* the backup has been restored to slot A */
} boot_flags_t;

typedef enum {
    BOOT_ACT_RUN_APP = 0,
    BOOT_ACT_BACKUP,
    BOOT_ACT_INSTALL,
    BOOT_ACT_START_TRIAL,
    BOOT_ACT_ROLLBACK,
    BOOT_ACT_CLEAR_STATE
} boot_action_t;

/* Decides the single next step from the persistent flags. */
boot_action_t boot_next_action(const boot_flags_t *f);

#endif
