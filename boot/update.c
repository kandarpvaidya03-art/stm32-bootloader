#include "update.h"
#include "crc32.h"
#include "flash.h"
#include "image.h"
#include "image_sig.h"
#include "pubkey.h"
#include "uart.h"

#define SLOT_B_BASE   0x08040000u
#define SLOT_B_SECTOR 6u
#define SLOT_SIZE     0x20000u

#define MSG_START 0x10u
#define MSG_DATA  0x11u
#define MSG_END   0x12u
#define MSG_ACK   0x81u
#define MSG_NACK  0x82u

#define ERR_BAD_LENGTH 0x20u
#define ERR_TOO_BIG    0x21u
#define ERR_FLASH      0x22u
#define ERR_STATE      0x23u
#define ERR_OFFSET     0x24u
#define ERR_IMAGE_CRC  0x25u
#define ERR_IMAGE_INVALID 0x26u
#define ERR_SIGNATURE 0x27u

static struct {
    int active;
    uint32_t size;
    uint32_t crc;
    uint32_t offset;
} up;

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void ack_u32(uint32_t value)
{
    const uint8_t b[4] = {(uint8_t)value, (uint8_t)(value >> 8),
                          (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
    send_frame(MSG_ACK, b, 4);
}

static void nack(uint8_t code)
{
    send_frame(MSG_NACK, &code, 1);
}

static void on_start(const frame_parser_t *f)
{
    up.active = 0;
    if (f->len != 8u) {
        nack(ERR_BAD_LENGTH);
        return;
    }
    uint32_t size = rd32(f->payload);
    uint32_t crc = rd32(f->payload + 4);
    if (size == 0u || size > SLOT_SIZE) {
        nack(ERR_TOO_BIG);
        return;
    }

    flash_unlock();
    int result = flash_erase_sector(SLOT_B_SECTOR);
    flash_lock();
    if (result != 0) {
        nack(ERR_FLASH);
        return;
    }

    up.active = 1;
    up.size = size;
    up.crc = crc;
    up.offset = 0;
    ack_u32(0);
}

static void on_data(const frame_parser_t *f)
{
    if (!up.active) {
        nack(ERR_STATE);
        return;
    }
    if (f->len < 5u) {
        nack(ERR_BAD_LENGTH);
        return;
    }
    uint32_t offset = rd32(f->payload);
    const uint8_t *data = f->payload + 4;
    uint32_t n = (uint32_t)f->len - 4u;

    if (offset < up.offset && offset + n == up.offset) {
        ack_u32(up.offset); /* repeat of the chunk we just wrote */
        return;
    }
    if (offset != up.offset) {
        nack(ERR_OFFSET);
        return;
    }
    if (n > up.size - up.offset) {
        nack(ERR_BAD_LENGTH);
        return;
    }
    if ((n % 4u) != 0u && offset + n != up.size) {
        nack(ERR_BAD_LENGTH); /* only the final chunk may be a partial word */
        return;
    }

    flash_unlock();
    for (uint32_t i = 0; i < n; i += 4u) {
        uint32_t word = 0xFFFFFFFFu; /* padding stays in the erased state */
        for (uint32_t b = 0; b < 4u && i + b < n; b++) {
            word &= ~(0xFFu << (8u * b));
            word |= (uint32_t)data[i + b] << (8u * b);
        }
        if (flash_write_word(SLOT_B_BASE + offset + i, word) != 0) {
            flash_lock();
            up.active = 0;
            nack(ERR_FLASH);
            return;
        }
    }
    flash_lock();

    up.offset += n;
    ack_u32(up.offset);
}

static void on_end(void)
{
    if (!up.active || up.offset != up.size) {
        up.active = 0;
        nack(ERR_STATE);
        return;
    }
    up.active = 0;

    uint32_t crc = crc32_update(0, (const uint8_t *)SLOT_B_BASE, up.size);
    if (crc != up.crc) {
        nack(ERR_IMAGE_CRC);
        return;
    }
    if (image_verify((const uint8_t *)SLOT_B_BASE, SLOT_SIZE, 0) != IMAGE_OK) {
        nack(ERR_IMAGE_INVALID);
        return;
    }
    if (!image_signature_ok((const uint8_t *)SLOT_B_BASE, boot_public_key)) {
        nack(ERR_SIGNATURE);
        return;
    }
    uart_puts("update: image stored in slot B, CRC ok\r\n");
    ack_u32(up.size);
}

int update_handle_frame(const frame_parser_t *f)
{
    switch (f->type) {
    case MSG_START:
        on_start(f);
        return 1;
    case MSG_DATA:
        on_data(f);
        return 1;
    case MSG_END:
        on_end();
        return 1;
    default:
        return 0;
    }
}
