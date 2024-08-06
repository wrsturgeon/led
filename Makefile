.PHONY: build clean install
.PRECIOUS: %.o %.d %.elf

USB:=$(shell find /dev -name '*usb*' 2>/dev/null | head -n 1)

COREFLAGS:=-g -Os -Wall -Wextra -flto -mmcu=atmega4809
COMPFLAGS:=-c -std=c11 -fno-exceptions -ffunction-sections -fdata-sections -MMD -DF_CPU=16000000L -DARDUINO=10607 -DARDUINO_AVR_NANO_EVERY -DARDUINO_ARCH_MEGAAVR -DAVR_NANO_4809_328MODE -DMILLIS_USE_TIMERB3 -DNO_EXTERNAL_I2C_PULLUP

build: main.hex

upload: main.hex
	@if [[ -z '$(USB)' ]]; then \
	  echo 'Please connect a USB (none found)'; \
	  exit 1; \
	fi
	@echo 'Resetting $(USB)...'
	stty --file $(USB) 1200
	avrdude -Cavrdude.conf -v -V -cjtag2updi -P$(USB) -b115200 -pm4809 -e -D -Uflash:w:$<:i -Ufuse2:w:0x01:m -Ufuse5:w:0xC9:m -Ufuse8:w:0x00:m

%.o %.d: %.c
	avr-gcc $(COREFLAGS) $(COMPFLAGS) $< -o $*.o

%.elf: %.o
	avr-gcc $(COREFLAGS) -fuse-linker-plugin -Wl,--gc-sections -Wl,--section-start=.text=0x0 -o $@ $^ -lm -Wl,-Map,$*.map
	avr-size -A $@

%.bin: %.elf
	avr-objcopy -O binary -R .eeprom $< $@

%.eep: %.elf
	avr-objcopy -O ihex -j .eeprom --set-section-flags=.eeprom=alloc,load --no-change-warnings --change-section-lma .eeprom=0 $< $@

%.hex: %.elf
	avr-objcopy -O ihex -R .eeprom $< $@

clean:
	rm -fr *.bin *.d *.eep *.elf *.hex *.map *.o
