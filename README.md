# PXS Bootloader

PXS (Pre-eXecutionSystem) is a UEFI bootloader designed to load 64-bit ELF kernels.

## Features
- Loads 64-bit ELF executables (`kernel.elf`) from the boot partition.
- maps kernel segments to their physical addresses specified in the ELF Program Headers (`p_paddr`).
- Sets up a high-resolution Framebuffer (GOP).
- Gathers System Information (ACPI, SMBIOS, Memory Map).
- Exits UEFI Boot Services and jumps to the kernel entry point.

## Kernel Interface

The kernel entry point should match the following signature:

```c
typedef struct {
    // ... (See include/bootinfo.h)
} PXS_BOOT_INFO;

void kernel_main(PXS_BOOT_INFO *BootInfo);
```

### BootInfo Structure
The `BootInfo` structure provides the kernel with essential system details:
- **Framebuffer**: Base address, resolution, pitch, and color masks.
- **Memory Map**: Complete UEFI memory map (required for physical memory management).
- **System Tables**: Pointers to ACPI (RSDP) and SMBIOS entry points.

## Build
This package is designed to be built within the EDK2 environment.
ensure `PxsPkg` is in your `PACKAGES_PATH`.

```bash
build -p PxsPkg/PxsPkg.dsc -a X64 -t GCC5
```

## Installation
1. Build the loader (`Pxs.efi`).
2. Rename `Pxs.efi` to `BOOTX64.EFI` and place it in `\EFI\BOOT\` on the ESP.
3. Place your kernel executable at `\kernel.elf` on the root of the ESP.

```
