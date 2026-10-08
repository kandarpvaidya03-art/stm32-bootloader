CC      := arm-none-eabi-gcc
SIZE    := arm-none-eabi-size
CFLAGS  := -mcpu=cortex-m4 -mthumb -Og -g -Wall -Wextra -ffreestanding
LDFLAGS := -nostartfiles
OPENOCD := openocd -f board/st_nucleo_f4.cfg

all: build/boot.elf build/app.elf

build/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/%.ld: ld/%.ld ld/sections.ld
	mkdir -p build
	cat $^ > $@

build/boot.elf: build/common/startup.o build/boot/main.o build/boot.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -T build/boot.ld -Wl,-Map=build/boot.map $(filter %.o,$^) -o $@
	$(SIZE) $@

build/app.elf: build/common/startup.o build/app/main.o build/app.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -T build/app.ld -Wl,-Map=build/app.map $(filter %.o,$^) -o $@
	$(SIZE) $@

flash-boot: build/boot.elf
	$(OPENOCD) -c "program build/boot.elf verify reset exit"

flash-app: build/app.elf
	$(OPENOCD) -c "program build/app.elf verify reset exit"

erase:
	$(OPENOCD) -c "init" -c "reset halt" -c "stm32f4x mass_erase 0" -c "exit"

clean:
	rm -rf build

.PHONY: all flash-boot flash-app erase clean
