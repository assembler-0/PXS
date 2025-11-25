# PxsPkg - UEFI Bootloader for VoidFrameX

PxsPkg is a custom UEFI bootloader designed to load ELF64 kernels with support for KASLR, Initrd, and a custom boot protocol.

## Features

- **ELF64 Loading:** Parses and loads ELF64 executables.
- **KASLR:** Kernel Address Space Layout Randomization support.
- **Configuration:** `pxs.cfg` file support for flexible boot options.
- **Initrd:** Support for loading an initial ramdisk.
- **Protocol:** Passes a comprehensive `PXS_BOOT_INFO` structure to the kernel.

## Configuration (pxs.cfg)

The bootloader looks for a file named `pxs.cfg` in the root directory of the boot partition.

**Format:**
```ini
KERNEL=filename.krnl
INITRD=ramdisk.img
CMDLINE=arg1 arg2 key=value
TIMEOUT=5
```

**Options:**
- `KERNEL`: Path to the kernel ELF file (Default: `voidframex.krnl`).
- `INITRD`: Path to the Initrd/Ramdisk file (Optional).
- `CMDLINE`: Command line string passed to the kernel (Max 511 chars).
- `TIMEOUT`: Time in seconds to wait before booting (Default: 3).

## Building

1. Ensure you have the EDK2 environment set up.
2. Run `./compile.sh` to build the bootloader.
3. The output `Pxs.efi` will be in the `build/` directory.

## Boot Protocol

The kernel entry point receives a pointer to the `PXS_BOOT_INFO` structure:

```c
typedef struct {
    uint32_t Magic;          
    uint32_t Version;         // Protocol Version
    // ... Framebuffer, MemoryMap, ACPI, etc.
} PXS_BOOT_INFO;
```
See `protocol.h` for the full definition.
