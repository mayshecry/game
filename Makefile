PREFIX =
CC = gcc
HOSTCC ?= gcc
AS = as
OBJCOPY = $(PREFIX)objcopy
.DEFAULT_GOAL := all
.SILENT:
$(info [MAKE] Goal: $(if $(strip $(MAKECMDGOALS)),$(MAKECMDGOALS),all))
CFLAGS = -m64 -std=gnu99 -ffreestanding -Os -Wall -Wextra -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -mgeneral-regs-only -fomit-frame-pointer -fmerge-all-constants -fno-unwind-tables -fno-exceptions -mcmodel=kernel -Iinclude -w
ASFLAGS = --64
LDFLAGS = -m64 -ffreestanding -Os -nostdlib -no-pie -Wl,-m,elf_x86_64 -Wl,-gc-sections -Wl,--strip-all
DIRS = arch drivers lib net game
all: sharkos-game.iso
$(DIRS):
	mkdir -p $@
boot.o: boot.s
	@printf "  AS  %s\n" "$<"; $(AS) $(ASFLAGS) boot.s -o boot.o
arch/early.o: src/arch/early.c include/kernel.h | arch
	@printf "  CC  %s\n" "$<"; $(CC) -c src/arch/early.c -o arch/early.o $(CFLAGS)
arch/io.o: src/arch/io.c include/kernel.h | arch
	@printf "  CC  %s\n" "$<"; $(CC) -c src/arch/io.c -o arch/io.o $(CFLAGS)
arch/interrupts.o: src/arch/interrupts.c include/kernel.h | arch
	@printf "  CC  %s\n" "$<"; $(CC) -c src/arch/interrupts.c -o arch/interrupts.o $(CFLAGS)
arch/cpu.o: src/arch/cpu.c include/kernel.h | arch
	@printf "  CC  %s\n" "$<"; $(CC) -c src/arch/cpu.c -o arch/cpu.o $(CFLAGS)
arch/mtrr.o: src/arch/mtrr.c include/kernel.h | arch
	@printf "  CC  %s\n" "$<"; $(CC) -c src/arch/mtrr.c -o arch/mtrr.o $(CFLAGS)
drivers/keyboard.o: src/drivers/keyboard.c include/kernel.h | drivers
	@printf "  CC  %s\n" "$<"; $(CC) -c src/drivers/keyboard.c -o drivers/keyboard.o $(CFLAGS)
drivers/pci.o: src/drivers/pci.c include/kernel.h | drivers
	@printf "  CC  %s\n" "$<"; $(CC) -c src/drivers/pci.c -o drivers/pci.o $(CFLAGS)
drivers/rtc.o: src/drivers/rtc.c include/kernel.h | drivers
	@printf "  CC  %s\n" "$<"; $(CC) -c src/drivers/rtc.c -o drivers/rtc.o $(CFLAGS)
lib/lib.o: src/lib/lib.c include/kernel.h | lib
	@printf "  CC  %s\n" "$<"; $(CC) -c src/lib/lib.c -o lib/lib.o $(CFLAGS)
lib/globals.o: src/lib/globals.c include/kernel.h | lib
	@printf "  CC  %s\n" "$<"; $(CC) -c src/lib/globals.c -o lib/globals.o $(CFLAGS)
lib/pmm.o: src/lib/pmm.c include/kernel.h | lib
	@printf "  CC  %s\n" "$<"; $(CC) -c src/lib/pmm.c -o lib/pmm.o $(CFLAGS)
net/net.o: src/net/net.c include/kernel.h include/net.h include/tls.h include/firewall.h | net
	@printf "  CC  %s\n" "$<"; $(CC) -c src/net/net.c -o net/net.o $(CFLAGS)
net/vnic.o: src/net/vnic.c include/kernel.h include/net.h | net
	@printf "  CC  %s\n" "$<"; $(CC) -c src/net/vnic.c -o net/vnic.o $(CFLAGS)
net/firewall.o: src/net/firewall.c include/firewall.h include/kernel.h | net
	@printf "  CC  %s\n" "$<"; $(CC) -c src/net/firewall.c -o net/firewall.o $(CFLAGS)
net/crypto.o: src/net/crypto.c include/tls.h include/kernel.h | net
	@printf "  CC  %s\n" "$<"; $(CC) -c src/net/crypto.c -o net/crypto.o $(CFLAGS)
net/tls.o: src/net/tls.c include/tls.h include/kernel.h | net
	@printf "  CC  %s\n" "$<"; $(CC) -c src/net/tls.c -o net/tls.o $(CFLAGS)
net/ed25519.o: src/net/ed25519.c include/tls.h include/kernel.h | net
	@printf "  CC  %s\n" "$<"; $(CC) -c src/net/ed25519.c -o net/ed25519.o $(CFLAGS)
game/fb.o: src/game/fb.c include/kernel.h include/term_font16_data.h | game
	@printf "  CC  %s\n" "$<"; $(CC) -c src/game/fb.c -o game/fb.o $(CFLAGS)
game/game.o: src/game/game.c include/kernel.h include/net.h include/game.h | game
	@printf "  CC  %s\n" "$<"; $(CC) -c src/game/game.c -o game/game.o $(CFLAGS)
game/main.o: src/game/main.c include/kernel.h include/net.h include/firewall.h include/game.h | game
	@printf "  CC  %s\n" "$<"; $(CC) -c src/game/main.c -o game/main.o $(CFLAGS)
sharkos-game.bin: Makefile boot.o arch/early.o arch/io.o arch/interrupts.o arch/cpu.o arch/mtrr.o drivers/keyboard.o drivers/pci.o drivers/rtc.o lib/lib.o lib/globals.o lib/pmm.o net/net.o net/vnic.o net/firewall.o net/crypto.o net/tls.o net/ed25519.o game/fb.o game/game.o game/main.o linker.ld
	@printf "  LD  %s\n" "$@"; $(CC) -T linker.ld -o sharkos-game.bin $(LDFLAGS) boot.o arch/early.o arch/io.o arch/interrupts.o arch/cpu.o arch/mtrr.o drivers/keyboard.o drivers/pci.o drivers/rtc.o lib/lib.o lib/globals.o lib/pmm.o net/net.o net/vnic.o net/firewall.o net/crypto.o net/tls.o net/ed25519.o game/fb.o game/game.o game/main.o -lgcc
LIMINE_DIR ?= limine
LIMINE_BIOS_ASSETS = $(LIMINE_DIR)/limine $(LIMINE_DIR)/limine-bios.sys $(LIMINE_DIR)/limine-bios-cd.bin
LIMINE_UEFI_ASSETS = $(LIMINE_DIR)/limine-uefi-cd.bin $(LIMINE_DIR)/BOOTX64.EFI $(LIMINE_DIR)/BOOTIA32.EFI
sharkos-game.iso: Makefile sharkos-game.bin limine.conf $(LIMINE_BIOS_ASSETS) $(LIMINE_UEFI_ASSETS)
	mkdir -p isodir/boot/limine
	mkdir -p isodir/boot
	mkdir -p isodir/EFI/BOOT
	cp sharkos-game.bin isodir/boot/sharkos-game.bin
	cp limine.conf isodir/boot/limine/limine.conf
	cp limine.conf isodir/limine.conf
	cp limine.conf isodir/boot/limine/limine.cfg
	cp limine.conf isodir/limine.cfg
	cp limine.conf isodir/boot/limine.cfg
	cp $(LIMINE_DIR)/limine-bios.sys isodir/boot/limine/
	cp $(LIMINE_DIR)/limine-bios-cd.bin isodir/boot/limine/
	cp $(LIMINE_DIR)/limine-uefi-cd.bin isodir/boot/limine/
	cp $(LIMINE_DIR)/BOOTX64.EFI isodir/EFI/BOOT/BOOTX64.EFI
	cp $(LIMINE_DIR)/BOOTIA32.EFI isodir/EFI/BOOT/BOOTIA32.EFI
	@printf "  ISO %s\n" "$@"; xorriso -as mkisofs -quiet -R -r -J -b boot/limine/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table --efi-boot boot/limine/limine-uefi-cd.bin -efi-boot-part --efi-boot-image --protective-msdos-label isodir -o sharkos-game.iso
	chmod +x $(LIMINE_DIR)/limine
	$(LIMINE_DIR)/limine bios-install sharkos-game.iso
clean:
	@printf "[CLEAN] Removing generated build outputs\n"
	rm -rf isodir arch drivers lib net game
	rm -f *.o sharkos-game.bin sharkos-game.iso
.PHONY: all clean $(DIRS)
run:
	qemu-system-x86_64 -m 512 -smp 2 -cdrom sharkos-game.iso -boot order=d -vga std -display gtk,zoom-to-fit=on -netdev user,id=net0 -device e1000,netdev=net0
run-serial:
	qemu-system-x86_64 -m 512 -smp 2 -cdrom sharkos-game.iso -boot order=d -vga std -display none -serial stdio -netdev user,id=net0 -device e1000,netdev=net0
