.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

CROSS_COMPILE ?=
ifeq ($(origin CC),default)
CC := $(CROSS_COMPILE)gcc
endif
ifeq ($(origin LD),default)
LD := $(CROSS_COMPILE)ld
endif
CC ?= $(CROSS_COMPILE)gcc
LD ?= $(CROSS_COMPILE)ld
HOSTCC ?= cc
XORRISO ?= xorriso
QEMU ?= qemu-system-x86_64

BUILD ?= build
LIMINE_DIR ?= limine
KERNEL := $(BUILD)/kernel.elf
ISO := $(BUILD)/CrapOS.iso
ISO_ROOT := $(BUILD)/iso
LIMINE_TOOL := $(BUILD)/host/limine

CPPFLAGS ?=
CFLAGS ?= -O2 -g
LDFLAGS ?=
HOSTCFLAGS ?= -O2 -Wall -Wextra
QEMUFLAGS ?= -m 256M -display gtk -serial file:$(BUILD)/serial.log -monitor none -no-reboot -no-shutdown

# Required kernel options follow user flags so host defaults cannot enable PIE/SIMD.
KERNEL_CPPFLAGS := -Iinclude
KERNEL_CFLAGS := -std=gnu17 -Wall -Wextra -Wpedantic -Werror \
	-ffreestanding -fno-stack-protector -fno-stack-check -fno-lto \
	-fno-pie -fno-pic -fno-omit-frame-pointer -fno-asynchronous-unwind-tables \
	-fno-unwind-tables -m64 -march=x86-64 -mno-red-zone -mcmodel=kernel \
	-mno-mmx -mno-sse -mno-sse2 -mno-80387
KERNEL_LDFLAGS := -nostdlib -static -z max-page-size=0x1000 \
	-z noexecstack --build-id=none -T linker.ld

SOURCES := $(shell find src -type f -name '*.c' | LC_ALL=C sort)
OBJECTS := $(patsubst src/%.c,$(BUILD)/obj/%.o,$(SOURCES))
DEPS := $(OBJECTS:.o=.d)
BOOT_FILES := limine-bios.sys limine-bios-cd.bin limine-uefi-cd.bin BOOTX64.EFI

.PHONY: all iso run debug clean help FORCE
all: $(KERNEL)
iso: $(ISO)

# Track tool/flag changes without rebuilding when the configuration is unchanged.
$(BUILD)/compile.flags: FORCE
	@mkdir -p "$(@D)"
	@printf '%s\n' '$(CC)' '$(CPPFLAGS) $(KERNEL_CPPFLAGS)' '$(CFLAGS) $(KERNEL_CFLAGS)' '$(SOURCES)' > "$@.tmp"
	@cmp -s "$@.tmp" "$@" && rm -f "$@.tmp" || mv -f "$@.tmp" "$@"

$(BUILD)/link.flags: FORCE
	@mkdir -p "$(@D)"
	@printf '%s\n' '$(LD)' '$(LDFLAGS) $(KERNEL_LDFLAGS)' > "$@.tmp"
	@cmp -s "$@.tmp" "$@" && rm -f "$@.tmp" || mv -f "$@.tmp" "$@"

$(BUILD)/obj/%.o: src/%.c $(BUILD)/compile.flags
	@mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(KERNEL_CPPFLAGS) $(CFLAGS) $(KERNEL_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(KERNEL): $(OBJECTS) linker.ld $(BUILD)/link.flags
	$(LD) $(LDFLAGS) $(KERNEL_LDFLAGS) $(OBJECTS) -o "$@"

$(BUILD)/host.flags: FORCE
	@mkdir -p "$(@D)"
	@printf '%s\n' '$(HOSTCC)' '$(HOSTCFLAGS)' '$(LIMINE_DIR)' > "$@.tmp"
	@cmp -s "$@.tmp" "$@" && rm -f "$@.tmp" || mv -f "$@.tmp" "$@"

$(LIMINE_TOOL): $(LIMINE_DIR)/limine.c $(LIMINE_DIR)/limine-bios-hdd.h $(BUILD)/host.flags Makefile
	@mkdir -p "$(@D)"
	$(HOSTCC) $(HOSTCFLAGS) -std=c99 "$<" -o "$@"

$(ISO): $(KERNEL) boot/limine.conf $(addprefix $(LIMINE_DIR)/,$(BOOT_FILES)) $(LIMINE_TOOL) Makefile
	mkdir -p "$(ISO_ROOT)/boot/limine" "$(ISO_ROOT)/EFI/BOOT"
	cp "$(KERNEL)" "$(ISO_ROOT)/boot/kernel.elf"
	cp boot/limine.conf "$(ISO_ROOT)/boot/limine/"
	cp $(addprefix $(LIMINE_DIR)/,limine-bios.sys limine-bios-cd.bin limine-uefi-cd.bin) "$(ISO_ROOT)/boot/limine/"
	cp "$(LIMINE_DIR)/BOOTX64.EFI" "$(ISO_ROOT)/EFI/BOOT/"
	$(XORRISO) -as mkisofs -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin -efi-boot-part \
		--efi-boot-image --protective-msdos-label -o "$@" "$(ISO_ROOT)"
	"$(LIMINE_TOOL)" bios-install "$@"

run: $(ISO)
	$(QEMU) $(QEMUFLAGS) -cdrom "$(ISO)"

debug: $(ISO)
	$(QEMU) $(QEMUFLAGS) -cdrom "$(ISO)" -S -s

clean:
	@case "$(abspath $(BUILD))" in "$(CURDIR)"/*) ;; *) echo 'Refusing to clean outside the project directory' >&2; exit 1;; esac
	rm -rf -- "$(abspath $(BUILD))"

help:
	@printf '%s\n' \
		'make [all]  Build the x86-64 kernel' \
		'make iso    Build a BIOS/UEFI bootable ISO' \
		'make run    Open the kernel screen in QEMU' \
		'make debug  Wait for GDB on localhost:1234 (-S -s)' \
		'make clean  Remove the build directory' \
		'Overrides: CROSS_COMPILE, CC, LD, CFLAGS, CPPFLAGS, LDFLAGS,' \
		'           BUILD, LIMINE_DIR, HOSTCC, HOSTCFLAGS, XORRISO, QEMU, QEMUFLAGS'

-include $(DEPS)
