# ====================================
# المتغيرات الأساسية
# ====================================
EFIINC=/usr/include/efi
EFILIB=/usr/lib

BOOTX64=BOOTX64.EFI
ESP=esp.img
ISO=ArabOS.iso

OVMF_CODE=/usr/share/OVMF/OVMF_CODE_4M.fd
OVMF_VARS=OVMF_VARS.fd

CC=gcc
LD=ld

# إعدادات اللغة العربية
export LANG=ar_EG.UTF-8
export LC_ALL=ar_EG.UTF-8

# ====================================
# إعدادات المترجم
# ====================================
CFLAGS = \
	-I$(EFIINC) \
	-I$(EFIINC)/x86_64 \
	-I./المحركات \
	-fpic \
	-ffreestanding \
	-fshort-wchar \
	-mno-red-zone \
	-maccumulate-outgoing-args \
	-finput-charset=UTF-8 \
	-fexec-charset=UTF-8 \
	-Wall

LDFLAGS = \
	-nostdlib \
	-znocombreloc \
	-T /usr/lib/elf_x86_64_efi.lds \
	-shared \
	-Bsymbolic

# ====================================
# الأهداف الرئيسية
# ====================================
all: $(BOOTX64)

# ====================================
# تجميع المصادر
# ====================================
محرك_الاقلاع.o: المحركات/محرك_الاقلاع/محرك_الاقلاع.c المحركات/محرك_الاقلاع/محرك_الاقلاع.h
	$(CC) $(CFLAGS) -c $< -o $@

محرك_القائمة.o: المحركات/محرك_الرسم/محرك_القائمة.c المحركات/محرك_الرسم/محرك_القائمة.h
	$(CC) $(CFLAGS) -c $< -o $@

محرك_التمهيد.o: المحركات/محرك_التمهيد/محرك_التمهيد.c المحركات/محرك_التمهيد/محرك_التمهيد.h
	$(CC) $(CFLAGS) -c $< -o $@

محرك_ادارة_الصور.o: المحركات/محرك_الرسم/محرك_ادارة_الصور.c المحركات/محرك_الرسم/محرك_ادارة_الصور.h
	$(CC) $(CFLAGS) -c $< -o $@

محمل_النواه.o: المحركات/محرك_التطبيقات/محمل_النواه.c المحركات/محرك_التطبيقات/محمل_النواه.h
	$(CC) $(CFLAGS) -c $< -o $@

محرك_الذاكرة.o: المحركات/محرك_الذاكرة/محرك_الذاكرة.c المحركات/محرك_الذاكرة/محرك_الذاكرة.h
	$(CC) $(CFLAGS) -c $< -o $@

البوابة.o: البوابة.c المحركات/محرك_الاقلاع/محرك_الاقلاع.h
	$(CC) $(CFLAGS) -c $< -o $@

# ====================================
# ربط مكتبة EFI المشتركة
# ====================================
محرك_الاقلاع.so: محرك_الاقلاع.o محرك_القائمة.o محرك_ادارة_الصور.o محرك_التمهيد.o محمل_النواه.o محرك_الذاكرة.o البوابة.o
	$(LD) \
		$(LDFLAGS) \
		/usr/lib/crt0-efi-x86_64.o \
		محرك_الاقلاع.o \
		محرك_القائمة.o \
		محرك_التمهيد.o \
		محرك_ادارة_الصور.o \
		محرك_الذاكرة.o \
		محمل_النواه.o \
		البوابة.o \
		-L$(EFILIB) \
		-lefi \
		-lgnuefi \
		-o محرك_الاقلاع.so

# ====================================
# تحويل ELF -> EFI
# ====================================
$(BOOTX64): محرك_الاقلاع.so
	objcopy \
		-j .text \
		-j .sdata \
		-j .data \
		-j .dynamic \
		-j .dynsym \
		-j .rel \
		-j .rela \
		-j .reloc \
		--target=efi-app-x86_64 \
		محرك_الاقلاع.so \
		$(BOOTX64)

# ====================================
# بناء صورة FAT لنظام EFI
# ====================================
$(ESP): $(BOOTX64)
	rm -f $(ESP)

	dd if=/dev/zero of=$(ESP) bs=1M count=64
	mkfs.vfat -F 32 -n "ArabOS" $(ESP)

	# إنشاء هيكل المجلدات داخل الصورة
	mmd -i $(ESP) ::EFI
	mmd -i $(ESP) ::EFI/BOOT

	# نسخ ملف BOOTX64.EFI فقط
	mcopy -i $(ESP) \
		$(BOOTX64) \
		::EFI/BOOT/BOOTX64.EFI

# ====================================
# بناء ISO لنظام UEFI
# ====================================
$(ISO): $(BOOTX64) $(ESP)
	rm -rf iso_build

	mkdir -p iso_build/EFI/BOOT

	cp $(BOOTX64) iso_build/EFI/BOOT/BOOTX64.EFI

	cp $(ESP) iso_build/efiboot.img

	xorriso \
		-as mkisofs \
		-R \
		-J \
		-joliet-long \
		-input-charset utf-8 \
		-V "ArabOS" \
		-eltorito-alt-boot \
		-e efiboot.img \
		-no-emul-boot \
		-o $(ISO) \
		iso_build

	rm -rf iso_build

# ====================================
# نسخ متغيرات OVMF
# ====================================
$(OVMF_VARS):
	cp /usr/share/OVMF/OVMF_VARS_4M.fd $(OVMF_VARS)

# ====================================
# تشغيل صورة FAT
# ====================================
run: $(ESP) $(OVMF_VARS)
	qemu-system-x86_64 \
		-machine q35 \
		-m 512M \
		-drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
		-drive if=pflash,format=raw,file=$(OVMF_VARS) \
		-drive format=raw,file=$(ESP)

# ====================================
# تشغيل ISO
# ====================================
runiso: $(ISO) $(OVMF_VARS)
	qemu-system-x86_64 \
		-machine q35 \
		-m 512M \
		-drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
		-drive if=pflash,format=raw,file=$(OVMF_VARS) \
		-cdrom $(ISO)

# ====================================

# ====================================
# تنظيف الملفات
# ====================================
clean:
	rm -f *.o
	rm -f *.so
	rm -f *.EFI
	rm -f *.img
	rm -f *.iso
	rm -f $(OVMF_VARS)

# ====================================
# تعريفات الأهداف الوهمية
# ====================================
.PHONY: all run runiso clean create-images
