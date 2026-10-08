#include "install.h"
#include "bootplan.h"
#include "bootstate.h"
#include "flash.h"
#include "image.h"
#include "image_sig.h"
#include "layout.h"
#include "pubkey.h"
#include "uart.h"

int slot_image_ok(uint32_t slot_base)
{
    const uint8_t *slot = (const uint8_t *)slot_base;

    return image_verify(slot, SLOT_SIZE, 0) == IMAGE_OK &&
           image_signature_ok(slot, boot_public_key);
}

/* Erases the destination and copies a verified image into it. */
static int copy_slot(uint32_t dst_base, uint32_t dst_sector, uint32_t src_base)
{
    const uint8_t *src = (const uint8_t *)src_base;
    uint32_t size = (uint32_t)src[IMAGE_OFF_SIZE] |
                    ((uint32_t)src[IMAGE_OFF_SIZE + 1u] << 8) |
                    ((uint32_t)src[IMAGE_OFF_SIZE + 2u] << 16) |
                    ((uint32_t)src[IMAGE_OFF_SIZE + 3u] << 24);
    uint32_t total = (IMAGE_HEADER_SIZE + size + 3u) & ~3u;

    flash_unlock();
    if (flash_erase_sector(dst_sector) != 0) {
        flash_lock();
        return -1;
    }
    for (uint32_t off = 0; off < total; off += 4u) {
        uint32_t word = *(const volatile uint32_t *)(src_base + off);
        if (flash_write_word(dst_base + off, word) != 0) {
            flash_lock();
            return -1;
        }
    }
    flash_lock();
    return slot_image_ok(dst_base) ? 0 : -1;
}

static void do_backup(void)
{
    if (slot_image_ok(SLOT_A_BASE)) {
        uart_puts("boot: backing up slot A\r\n");
        if (copy_slot(BACKUP_BASE, BACKUP_SECTOR, SLOT_A_BASE) != 0) {
            uart_puts("boot: backup failed, update cancelled\r\n");
            (void)bootstate_clear();
            return;
        }
    } else {
        uart_puts("boot: no valid image to back up\r\n");
        flash_unlock();
        (void)flash_erase_sector(BACKUP_SECTOR); /* never restore a stale backup */
        flash_lock();
    }
    (void)bootstate_set(BOOTSTATE_BACKUP_DONE);
}

static void do_install(void)
{
    if (slot_image_ok(SLOT_B_BASE)) {
        uart_puts("boot: installing slot B into slot A\r\n");
        if (copy_slot(SLOT_A_BASE, SLOT_A_SECTOR, SLOT_B_BASE) == 0) {
            (void)bootstate_set(BOOTSTATE_INSTALL_DONE);
            return;
        }
        uart_puts("boot: install failed\r\n");
    } else {
        uart_puts("boot: slot B is not valid\r\n");
    }
    /* Slot A may be half written: force the rollback path. */
    (void)bootstate_set(BOOTSTATE_INSTALL_DONE);
    (void)bootstate_set(BOOTSTATE_TRIAL_STARTED);
}

static void do_rollback(void)
{
    uart_puts("boot: new image not confirmed, rolling back\r\n");
    if (slot_image_ok(BACKUP_BASE)) {
        if (copy_slot(SLOT_A_BASE, SLOT_A_SECTOR, BACKUP_BASE) != 0) {
            uart_puts("boot: restore failed\r\n");
        }
    } else {
        uart_puts("boot: no valid backup to restore\r\n");
    }
    (void)bootstate_set(BOOTSTATE_ROLLBACK_DONE);
}

void install_run_pending(void)
{
    /* The step limit stops a flash fault from looping here forever. */
    for (int step = 0; step < 8; step++) {
        boot_flags_t flags;
        bootstate_read(&flags);

        switch (boot_next_action(&flags)) {
        case BOOT_ACT_BACKUP:
            do_backup();
            break;
        case BOOT_ACT_INSTALL:
            do_install();
            break;
        case BOOT_ACT_START_TRIAL:
            uart_puts("boot: starting new image on trial\r\n");
            (void)bootstate_set(BOOTSTATE_TRIAL_STARTED);
            return;
        case BOOT_ACT_ROLLBACK:
            do_rollback();
            break;
        case BOOT_ACT_CLEAR_STATE:
            (void)bootstate_clear();
            break;
        case BOOT_ACT_RUN_APP:
        default:
            return;
        }
    }
}
