CC      := arm-none-eabi-gcc
SIZE    := arm-none-eabi-size
CFLAGS  := -mcpu=cortex-m4 -mthumb -Og -g -Wall -Wextra -ffreestanding -ffunction-sections -fdata-sections -Icommon -Ithird_party/micro-ecc -DuECC_PLATFORM=0 -DuECC_SUPPORTS_secp160r1=0 -DuECC_SUPPORTS_secp192r1=0 -DuECC_SUPPORTS_secp224r1=0 -DuECC_SUPPORTS_secp256k1=0 -DuECC_SUPPORT_COMPRESSED_POINT=0
LDFLAGS := -nostartfiles -Wl,--gc-sections
OPENOCD := openocd -f board/st_nucleo_f4.cfg

all: build/boot.elf build/app.elf build/app.bin build/app-image.bin

build/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/%.ld: ld/%.ld ld/sections.ld
	mkdir -p build
	cat $^ > $@

build/boot.elf: build/common/startup.o build/common/uart.o build/boot/main.o build/boot/selftest.o build/boot/update.o build/common/flash.o build/common/tick.o build/common/frame.o build/common/crc32.o build/common/image.o build/common/sha256.o build/common/image_sig.o build/third_party/micro-ecc/uECC.o build/boot/pubkey.o build/boot.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -T build/boot.ld -Wl,-Map=build/boot.map $(filter %.o,$^) -o $@
	$(SIZE) $@

build/app.elf: build/common/startup.o build/common/uart.o build/app/main.o build/app.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -T build/app.ld -Wl,-Map=build/app.map $(filter %.o,$^) -o $@
	$(SIZE) $@

flash-boot: build/boot.elf
	$(OPENOCD) -c "program build/boot.elf verify reset exit"

flash-app: build/app-signed.bin
	$(OPENOCD) -c "program build/app-signed.bin 0x08020000 verify reset exit"

erase:
	$(OPENOCD) -c "init" -c "reset halt" -c "stm32f4x mass_erase 0" -c "exit"

clean:
	rm -rf build

.PHONY: all flash-boot flash-app erase clean

HOSTCC := gcc

test:
	mkdir -p build/host
	$(HOSTCC) -Wall -Wextra -Icommon tests/test_crc32.c common/crc32.c -o build/host/test_crc32
	./build/host/test_crc32
	$(HOSTCC) -Wall -Wextra -Icommon tests/test_frame.c common/frame.c common/crc32.c -o build/host/test_frame
	./build/host/test_frame
	$(HOSTCC) -Wall -Wextra -Icommon tests/test_sha256.c common/sha256.c -o build/host/test_sha256
	./build/host/test_sha256
	$(HOSTCC) -Wall -Wextra -Icommon tests/test_image.c common/image.c common/sha256.c -o build/host/test_image
	./build/host/test_image
	$(HOSTCC) -Wall -Wextra -Icommon -Ithird_party/micro-ecc tests/test_image_sig.c common/image_sig.c common/sha256.c third_party/micro-ecc/uECC.c -o build/host/test_image_sig
	./build/host/test_image_sig
	$(HOSTCC) -Wall -Wextra -Icommon tests/test_bootplan.c common/bootplan.c -o build/host/test_bootplan
	./build/host/test_bootplan

.PHONY: test

build/%.bin: build/%.elf
	arm-none-eabi-objcopy -O binary $< $@

PYTHON := python3

build/app-image.bin: build/app.bin tools/imgtool.py
	$(PYTHON) tools/imgtool.py pack build/app.bin $@ --version 1

KEY := keys/signing_key.pem

build/app-signed.bin: build/app-image.bin $(KEY)
	$(PYTHON) tools/imgtool.py sign --key $(KEY) build/app-image.bin $@
