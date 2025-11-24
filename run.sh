set -e
mkdir root/EFI/BOOT -p
cp build/DEBUG_GCC5/X64/Pxs.efi root/EFI/BOOT/bootx64.efi
qemu-system-x86_64 \
  -bios /usr/share/edk2/x64/OVMF.4m.fd \
  -serial stdio \
  -m 1G \
  -drive format=raw,file=fat:rw:root \
  -net none
