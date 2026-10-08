#include <stdio.h>
#include <string.h>
#include "sha256.h"

static int failures;

#define CHECK(cond)                                           \
    do {                                                      \
        if (!(cond)) {                                        \
            printf("FAIL line %d: %s\n", __LINE__, #cond);    \
            failures++;                                       \
        }                                                     \
    } while (0)

static int digest_is(const uint8_t digest[32], const char *hex)
{
    char text[65];
    for (int i = 0; i < 32; i++) {
        sprintf(&text[2 * i], "%02x", digest[i]);
    }
    return strcmp(text, hex) == 0;
}

static void hash(const char *msg, uint8_t digest[32])
{
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)msg, strlen(msg));
    sha256_final(&ctx, digest);
}

int main(void)
{
    uint8_t digest[32];
    sha256_ctx_t ctx;

    hash("", digest);
    CHECK(digest_is(digest,
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));

    hash("abc", digest);
    CHECK(digest_is(digest,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

    hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", digest);
    CHECK(digest_is(digest,
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));

    /* one million 'a' characters, fed in uneven pieces */
    static uint8_t block[1000];
    memset(block, 'a', sizeof block);
    sha256_init(&ctx);
    for (int i = 0; i < 1000; i++) {
        sha256_update(&ctx, block, 7);
        sha256_update(&ctx, block + 7, sizeof block - 7);
    }
    sha256_final(&ctx, digest);
    CHECK(digest_is(digest,
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));

    if (failures == 0) {
        printf("test_sha256: all checks passed\n");
    }
    return failures != 0;
}
