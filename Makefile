CC      := arm-none-eabi-gcc
SIZE    := arm-none-eabi-size
CFLAGS  := -mcpu=cortex-m4 -mthumb -Og -g -Wall -Wextra -ffreestanding
LDFLAGS := -nostartfiles -T linker.ld -Wl,-Map=build/blinky.map
SRCS    := startup.c main.c
OBJS    := $(SRCS:%.c=build/%.o)

all: build/blinky.elf

build/%.o: %.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/blinky.elf: $(OBJS) linker.ld
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $@
	$(SIZE) $@

build:
	mkdir -p build

flash: build/blinky.elf
	openocd -f board/st_nucleo_f4.cfg -c "program build/blinky.elf verify reset exit"

clean:
	rm -rf build

.PHONY: all flash clean
