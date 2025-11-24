#ifndef PXS_BOOTINFO_H
#define PXS_BOOTINFO_H

#include <Uefi.h>

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
    PXS_FRAMEBUFFER_INFO Framebuffer;

    // Memory Map
    EFI_MEMORY_DESCRIPTOR *MemoryMap;
    UINTN                 MemoryMapSize;
    UINTN                 MapKey;
    UINTN                 DescriptorSize;
    UINT32                DescriptorVersion;

    // System Tables
    VOID                  *Rsdp;
    VOID                  *Smbios;
    
    // Runtime Services (Careful with this after ExitBootServices)
    // We might map these if we want the kernel to call UEFI Runtime Services
    UINT64                RuntimeServicesPtr; 
} PXS_BOOT_INFO;

#endif // PXS_BOOTINFO_H
