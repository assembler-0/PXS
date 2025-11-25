/**
 * @file protocol.h
 */
#pragma once

#include <Uefi.h>

#define PXS_MAGIC 0x21535850

typedef struct {
    UINT64 BaseAddress;
    UINT64 Size;
    UINT32 Width;
    UINT32 Height;
    UINT32 PixelsPerScanLine;
    UINT8  RedMaskSize;
    UINT8  RedFieldPosition;
    UINT8  GreenMaskSize;
    UINT8  GreenFieldPosition;
    UINT8  BlueMaskSize;
    UINT8  BlueFieldPosition;
    UINT8  ReservedMaskSize;
    UINT8  ReservedFieldPosition;
} PXS_FRAMEBUFFER_INFO;

typedef struct {
    // Header
    UINT32                Magic;           ///< "PXS!" (0x21535850)
    UINT32                Version;         ///< Protocol Version
    UINT32                Flags;           ///< Boot Flags

    // Framebuffer
    PXS_FRAMEBUFFER_INFO    Framebuffer;

    // Memory Map
    EFI_MEMORY_DESCRIPTOR   *MemoryMap;
    UINTN                   MemoryMapSize;
    UINTN                   MapKey;
    UINTN                   DescriptorSize;
    UINT32                  DescriptorVersion;

    // System Tables
    VOID                    *Rsdp;
    VOID                    *Smbios;
    UINT64                  RuntimeServicesPtr;

    // Kernel Info
    UINT64                  KernelPhysicalBase;
    UINT64                  KernelFileSize;

    // Modules (Initrd/Ramdisk)
    UINT64                  InitrdAddress;
    UINT64                  InitrdSize;

    // Command Line
    CHAR8                   *CommandLine;
} PXS_BOOT_INFO;
