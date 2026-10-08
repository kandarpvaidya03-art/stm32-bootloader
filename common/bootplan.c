#include "bootplan.h"

boot_action_t boot_next_action(const boot_flags_t *f)
{
    if (!f->requested) {
        return BOOT_ACT_RUN_APP;
    }
    if (f->confirmed || f->rollback_done) {
        return BOOT_ACT_CLEAR_STATE;
    }
    if (!f->backup_done) {
        return BOOT_ACT_BACKUP;
    }
    if (!f->install_done) {
        return BOOT_ACT_INSTALL;
    }
    if (!f->trial_started) {
        return BOOT_ACT_START_TRIAL;
    }
    return BOOT_ACT_ROLLBACK;
}
