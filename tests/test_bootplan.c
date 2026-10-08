#include <stdio.h>
#include <string.h>
#include "bootplan.h"

static int failures;

#define CHECK(cond)                                           \
    do {                                                      \
        if (!(cond)) {                                        \
            printf("FAIL line %d: %s\n", __LINE__, #cond);    \
            failures++;                                       \
        }                                                     \
    } while (0)

/* The effect of completing an action. */
static void apply(boot_flags_t *f, boot_action_t action)
{
    switch (action) {
    case BOOT_ACT_BACKUP:
        f->backup_done = 1;
        break;
    case BOOT_ACT_INSTALL:
        f->install_done = 1;
        break;
    case BOOT_ACT_START_TRIAL:
        f->trial_started = 1;
        break;
    case BOOT_ACT_ROLLBACK:
        f->rollback_done = 1;
        break;
    case BOOT_ACT_CLEAR_STATE:
        memset(f, 0, sizeof *f);
        break;
    default:
        break;
    }
}

int main(void)
{
    boot_flags_t f;

    /* no update pending */
    memset(&f, 0, sizeof f);
    CHECK(boot_next_action(&f) == BOOT_ACT_RUN_APP);

    /* successful update, one step per boot decision */
    f.requested = 1;
    CHECK(boot_next_action(&f) == BOOT_ACT_BACKUP);
    apply(&f, BOOT_ACT_BACKUP);
    CHECK(boot_next_action(&f) == BOOT_ACT_INSTALL);
    apply(&f, BOOT_ACT_INSTALL);
    CHECK(boot_next_action(&f) == BOOT_ACT_START_TRIAL);
    apply(&f, BOOT_ACT_START_TRIAL);
    f.confirmed = 1; /* the new application confirms itself */
    CHECK(boot_next_action(&f) == BOOT_ACT_CLEAR_STATE);
    apply(&f, BOOT_ACT_CLEAR_STATE);
    CHECK(boot_next_action(&f) == BOOT_ACT_RUN_APP);

    /* new image resets before confirming */
    memset(&f, 0, sizeof f);
    f.requested = f.backup_done = f.install_done = f.trial_started = 1;
    CHECK(boot_next_action(&f) == BOOT_ACT_ROLLBACK);
    apply(&f, BOOT_ACT_ROLLBACK);
    CHECK(boot_next_action(&f) == BOOT_ACT_CLEAR_STATE);
    apply(&f, BOOT_ACT_CLEAR_STATE);
    CHECK(boot_next_action(&f) == BOOT_ACT_RUN_APP);

    /* power lost during a step: the flag is not set, so the same step repeats */
    memset(&f, 0, sizeof f);
    f.requested = 1;
    CHECK(boot_next_action(&f) == BOOT_ACT_BACKUP);
    CHECK(boot_next_action(&f) == BOOT_ACT_BACKUP);
    f.backup_done = 1;
    CHECK(boot_next_action(&f) == BOOT_ACT_INSTALL);
    CHECK(boot_next_action(&f) == BOOT_ACT_INSTALL);

    /* from every possible flag combination, an application start is
     * reached within a few steps: no combination can loop forever */
    for (unsigned bits = 0; bits < 64u; bits++) {
        f.requested = (bits >> 0) & 1u;
        f.backup_done = (bits >> 1) & 1u;
        f.install_done = (bits >> 2) & 1u;
        f.trial_started = (bits >> 3) & 1u;
        f.confirmed = (bits >> 4) & 1u;
        f.rollback_done = (bits >> 5) & 1u;

        int reached = 0;
        for (int step = 0; step < 8 && !reached; step++) {
            boot_action_t action = boot_next_action(&f);
            if (action == BOOT_ACT_RUN_APP || action == BOOT_ACT_START_TRIAL) {
                reached = 1;
            }
            apply(&f, action);
        }
        CHECK(reached);
    }

    if (failures == 0) {
        printf("test_bootplan: all checks passed\n");
    }
    return failures != 0;
}
