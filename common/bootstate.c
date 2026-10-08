#include "bootstate.h"
#include "flash.h"
#include "layout.h"

#define FLAG_SET   0x5AFE5AFEu
#define FLAG_BLANK 0xFFFFFFFFu

static uint32_t flag_word(uint32_t index)
{
    return *(const volatile uint32_t *)(STATE_BASE + 4u * index);
}

void bootstate_read(boot_flags_t *f)
{
    f->requested = flag_word(BOOTSTATE_REQUESTED) == FLAG_SET;
    f->backup_done = flag_word(BOOTSTATE_BACKUP_DONE) == FLAG_SET;
    f->install_done = flag_word(BOOTSTATE_INSTALL_DONE) == FLAG_SET;
    f->trial_started = flag_word(BOOTSTATE_TRIAL_STARTED) == FLAG_SET;
    f->confirmed = flag_word(BOOTSTATE_CONFIRMED) == FLAG_SET;
    f->rollback_done = flag_word(BOOTSTATE_ROLLBACK_DONE) == FLAG_SET;
}

int bootstate_set(uint32_t index)
{
    if (index >= BOOTSTATE_COUNT) {
        return -1;
    }
    if (flag_word(index) == FLAG_SET) {
        return 0;
    }
    flash_unlock();
    int result = flash_write_word(STATE_BASE + 4u * index, FLAG_SET);
    flash_lock();
    if (result != 0 || flag_word(index) != FLAG_SET) {
        return -1;
    }
    return 0;
}

int bootstate_clear(void)
{
    int blank = 1;
    for (uint32_t i = 0; i < BOOTSTATE_COUNT; i++) {
        if (flag_word(i) != FLAG_BLANK) {
            blank = 0;
        }
    }
    if (blank) {
        return 0;
    }
    flash_unlock();
    int result = flash_erase_sector(STATE_SECTOR);
    flash_lock();
    return result;
}
