CC      := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
SIZE    := arm-none-eabi-size
HOSTCC  := gcc
PYTHON  := python3
OPENOCD := openocd -f board/st_nucleo_f4.cfg
KEY     := keys/signing_key.pem

UECC_DEFS := -DuECC_PLATFORM=0 -DuECC_SUPPORTS_secp160r1=0 -DuECC_SUPPORTS_secp192r1=0 -DuECC_SUPPORTS_secp224r1=0 -DuECC_SUPPORTS_secp256k1=0 -DuECC_SUPPORT_COMPRESSED_POINT=0
CFLAGS  := -mcpu=cortex-m4 -mthumb -Og -g -Wall -Wextra -ffreestanding -ffunction-sections -fdata-sections -Icommon -Ithird_party/micro-ecc $(UECC_DEFS)
LDFLAGS := -nostartfiles -Wl,--gc-sections

BOOT_OBJS := $(addprefix build/,common/startup.o common/uart.o common/tick.o common/flash.o common/frame.o common/crc32.o common/sha256.o common/image.o common/image_sig.o common/bootplan.o common/bootstate.o third_party/micro-ecc/uECC.o boot/main.o boot/selftest.o boot/update.o boot/install.o boot/pubkey.o)
APP_OBJS  := $(addprefix build/,common/startup.o common/uart.o common/flash.o common/bootstate.o)

APPS := v1 v2 bad
APP_VER_v1  := 1
APP_VER_v2  := 2
APP_VER_bad := 3
build/app-v1/main.o:  APP_DEFS := -DAPP_VERSION=1
build/app-v2/main.o:  APP_DEFS := -DAPP_VERSION=2 -DAPP_BLINK_DELAY=600000u
build/app-bad/main.o: APP_DEFS := -DAPP_VERSION=3 -DAPP_CONFIRM=0 -DAPP_BLINK_DELAY=150000u

# CI builds this: firmware plus unsigned images (no private key needed).
all: build/boot.elf $(APPS:%=build/app-%-image.bin)

# Needs the private key, so it only works on the developer's machine.
signed: $(APPS:%=build/app-%-signed.bin)

.SECONDARY:

build/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/app-%/main.o: app/main.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(APP_DEFS) -c $< -o $@

build/%.ld: ld/%.ld ld/sections.ld
	mkdir -p build
	cat $^ > $@

build/boot.elf: $(BOOT_OBJS) build/boot.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -T build/boot.ld -Wl,-Map=build/boot.map $(filter %.o,$^) -o $@
	$(SIZE) $@

build/app-%.elf: build/app-%/main.o $(APP_OBJS) build/app.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -T build/app.ld -Wl,-Map=build/app-$*.map $(filter %.o,$^) -o $@
	$(SIZE) $@

build/%.bin: build/%.elf
	$(OBJCOPY) -O binary $< $@

build/app-%-image.bin: build/app-%.bin tools/imgtool.py
	$(PYTHON) tools/imgtool.py pack $< $@ --version $(APP_VER_$*)

build/app-%-signed.bin: build/app-%-image.bin $(KEY)
	$(PYTHON) tools/imgtool.py sign --key $(KEY) $< $@

TESTS := crc32 frame sha256 image image_sig bootplan
TEST_SRCS_crc32     := common/crc32.c
TEST_SRCS_frame     := common/frame.c common/crc32.c
TEST_SRCS_sha256    := common/sha256.c
TEST_SRCS_image     := common/image.c common/sha256.c
TEST_SRCS_image_sig := common/image_sig.c common/sha256.c third_party/micro-ecc/uECC.c
TEST_SRCS_bootplan  := common/bootplan.c

test: $(TESTS:%=run-test-%)

run-test-%:
	mkdir -p build/host
	$(HOSTCC) -Wall -Wextra -Icommon -Ithird_party/micro-ecc tests/test_$*.c $(TEST_SRCS_$*) -o build/host/test_$*
	./build/host/test_$*

flash-boot: build/boot.elf
	$(OPENOCD) -c "program build/boot.elf verify reset exit"

flash-app: build/app-v1-signed.bin
	$(OPENOCD) -c "program build/app-v1-signed.bin 0x08020000 verify reset exit"

erase:
	$(OPENOCD) -c "init" -c "reset halt" -c "stm32f4x mass_erase 0" -c "exit"

clean:
	rm -rf build

.PHONY: all signed test flash-boot flash-app erase clean
