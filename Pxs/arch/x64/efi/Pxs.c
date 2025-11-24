#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Protocol/GraphicsOutput.h>
#include <compiler.h>

#define PXS_LOADER_VERSION "0.0.1"
#define PXS_BUILD_DATE     __DATE__ " " __TIME__

EFI_STATUS EFIAPI UefiMain (
    IN EFI_HANDLE        ImageHandle,
    IN EFI_SYSTEM_TABLE  *SystemTable
) {

    EFI_STATUS Status;
    UINTN MemoryMapSize = 0;
    EFI_MEMORY_DESCRIPTOR *MemoryMap = NULL;
    UINTN MapKey;
    UINTN DescriptorSize;
    UINT32 DescriptorVersion;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop = NULL;

    gST->ConOut->ClearScreen(gST->ConOut);

    Print(L"PXS (Pre-eXecutionSystem) Loader %a\n", PXS_LOADER_VERSION);
    Print(L"Built: %a\n", PXS_BUILD_DATE);
    Print(L"Copyright(C) 2025 assembler-0\n");
    Print(L"UEFI Firmware Version: %d.%02d\n",
        SystemTable->FirmwareRevision >> 16,
        SystemTable->FirmwareRevision & 0xFFFF);

    // Locate Graphics Output Protocol
    Status = gBS->LocateProtocol(
        &gEfiGraphicsOutputProtocolGuid,
        NULL,
        (VOID **)&Gop
    );

    if (!EFI_ERROR(Status)) {
        Print(L"Graphics Output Protocol found\n");
        Print(L"Resolution: %dx%d\n",
            Gop->Mode->Info->HorizontalResolution,
            Gop->Mode->Info->VerticalResolution);
        Print(L"Framebuffer Base: 0x%lx\n", Gop->Mode->FrameBufferBase);
    } else {
        Print(L"Warning: Graphics Output Protocol not found\n");
    }

    // Get memory map size
    Status = gBS->GetMemoryMap(
        &MemoryMapSize,
        MemoryMap,
        &MapKey,
        &DescriptorSize,
        &DescriptorVersion
    );

    if (Status != EFI_BUFFER_TOO_SMALL) {
        Print(L"Error: Unexpected GetMemoryMap result\n");
        return Status;
    }

    // Allocate space for memory map (add extra space for potential allocations)
    MemoryMapSize += 10 * DescriptorSize;
    MemoryMap = AllocatePool(MemoryMapSize);

    if (MemoryMap == NULL) {
        Print(L"Error: Failed to allocate memory map\n");
        return EFI_OUT_OF_RESOURCES;
    }

    // Get the actual memory map
    Status = gBS->GetMemoryMap(
        &MemoryMapSize,
        MemoryMap,
        &MapKey,
        &DescriptorSize,
        &DescriptorVersion
    );

    if (EFI_ERROR(Status)) {
        Print(L"Error: Failed to get memory map\n");
        FreePool(MemoryMap);
        return Status;
    }

    Print(L"Memory map retrieved: %d entries\n", MemoryMapSize / DescriptorSize);

    Print(L"Loader ready. Halting for now...\n");

    while(1) {
        __asm__ __volatile__("hlt");
    }

    return EFI_SUCCESS;
}
