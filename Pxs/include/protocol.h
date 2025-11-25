/**
 * @file protocol.h
 */
#pragma once

#include <Uefi.h>

// typedef struct {
//   UINT32                Type;
//   EFI_PHYSICAL_ADDRESS    PhysicalStart;
//   EFI_VIRTUAL_ADDRESS     VirtualStart;
//   UINT64                NumberOfPages;
//   UINT64                Attribute;
// } EFI_MEMORY_DESCRIPTOR;

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
    // Framebuffer
    PXS_FRAMEBUFFER_INFO    Framebuffer;

    // Memory Map
    EFI_MEMORY_DESCRIPTOR   *MemoryMap;
    UINTN            MemoryMapSize;
    UINTN            MapKey;
    UINTN            DescriptorSize;
    UINT32                DescriptorVersion;

    // System Tables
    VOID                    *Rsdp;
    VOID                    *Smbios;
    UINT64                RuntimeServicesPtr;

    // Kernel Info
    UINT64                KernelPhysicalBase;
} PXS_BOOT_INFO;
