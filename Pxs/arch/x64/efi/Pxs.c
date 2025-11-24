#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/LoadedImage.h>
#include <Guid/FileInfo.h>
#include <Guid/Acpi.h>
#include <Guid/SmBios.h>

#include <compiler.h>
#include <elf.h>
#include <bootinfo.h>

#define PXS_LOADER_VERSION "0.1.0"
#define KERNEL_PATH        L"voidframex.krnl"

// Kernel Entry Point Type
typedef VOID (*KERNEL_ENTRY)(PXS_BOOT_INFO *BootInfo);

// --------------------------------------------------------------------------
// HELPER FUNCTIONS
// --------------------------------------------------------------------------

EFI_STATUS GetFileSize(IN EFI_FILE_HANDLE FileHandle, OUT UINT64 *FileSize) {
    EFI_STATUS Status;
    EFI_FILE_INFO *FileInfo = NULL;
    UINTN FileInfoSize = 0;

    Status = FileHandle->GetInfo(FileHandle, &gEfiFileInfoGuid, &FileInfoSize, NULL);
    if (Status != EFI_BUFFER_TOO_SMALL) {
        return Status;
    }

    FileInfo = AllocatePool(FileInfoSize);
    if (!FileInfo) {
        return EFI_OUT_OF_RESOURCES;
    }

    Status = FileHandle->GetInfo(FileHandle, &gEfiFileInfoGuid, &FileInfoSize, FileInfo);
    if (!EFI_ERROR(Status)) {
        *FileSize = FileInfo->FileSize;
    }
    FreePool(FileInfo);
    return Status;
}

VOID* GetSystemConfigurationTable(EFI_GUID *Guid) {
    for (UINTN i = 0; i < gST->NumberOfTableEntries; i++) {
        if (CompareGuid(Guid, &gST->ConfigurationTable[i].VendorGuid)) {
            return gST->ConfigurationTable[i].VendorTable;
        }
    }
    return NULL;
}

// --------------------------------------------------------------------------
// ELF LOADER
// --------------------------------------------------------------------------

EFI_STATUS LoadElfKernel(
    IN EFI_FILE_HANDLE RootDir,
    IN CHAR16 *FileName,
    OUT EFI_PHYSICAL_ADDRESS *EntryPoint
) {
    EFI_STATUS Status;
    EFI_FILE_HANDLE FileHandle;
    UINT64 FileSize;
    VOID *FileBuffer;
    Elf64_Ehdr *Ehdr;
    Elf64_Phdr *Phdr;
    UINTN i;

    Print(L"Loading Kernel: %s\n", FileName);

    // Open file
    Status = RootDir->Open(RootDir, &FileHandle, FileName, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status)) {
        Print(L"Error: Could not open kernel file. %r\n", Status);
        return Status;
    }

    // Get size
    Status = GetFileSize(FileHandle, &FileSize);
    if (EFI_ERROR(Status)) {
        FileHandle->Close(FileHandle);
        return Status;
    }

    // Allocate buffer for the whole file (simplifies parsing)
    FileBuffer = AllocatePool(FileSize);
    if (!FileBuffer) {
        FileHandle->Close(FileHandle);
        return EFI_OUT_OF_RESOURCES;
    }

    // Read file
    UINTN ReadSize = FileSize;
    Status = FileHandle->Read(FileHandle, &ReadSize, FileBuffer);
    FileHandle->Close(FileHandle);
    if (EFI_ERROR(Status)) {
        FreePool(FileBuffer);
        return Status;
    }

    // Check ELF Header
    Ehdr = (Elf64_Ehdr *)FileBuffer;
    if (Ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
        Ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        Ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
        Ehdr->e_ident[EI_MAG3] != ELFMAG3) {
        Print(L"Error: Invalid ELF Magic\n");
        FreePool(FileBuffer);
        return EFI_LOAD_ERROR;
    }

    if (Ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        Print(L"Error: Not 64-bit ELF\n");
        FreePool(FileBuffer);
        return EFI_LOAD_ERROR;
    }

    Print(L"ELF Header verified. Entry: 0x%lx\n", Ehdr->e_entry);

    // Load Segments
    Phdr = (Elf64_Phdr *)((UINT8 *)FileBuffer + Ehdr->e_phoff);
    for (i = 0; i < Ehdr->e_phnum; i++) {
        if (Phdr[i].p_type == PT_LOAD) {
            UINTN Pages = EFI_SIZE_TO_PAGES(Phdr[i].p_memsz);
            EFI_PHYSICAL_ADDRESS Dest = Phdr[i].p_paddr;

            Print(L"Segment %d: Load @ 0x%lx (0x%lx bytes) -> Pages: %d\n",
                  i, Dest, Phdr[i].p_memsz, Pages);

            // Attempt to allocate at specific address
            Status = gBS->AllocatePages(AllocateAddress, EfiLoaderData, Pages, &Dest);
            if (EFI_ERROR(Status)) {
                Print(L"Error: Failed to allocate pages at 0x%lx. %r\n", Dest, Status);
                FreePool(FileBuffer);
                return Status;
            }

            // Zero the memory first (handles BSS implicitly)
            SetMem((VOID *)Dest, Phdr[i].p_memsz, 0);

            // Copy file data
            CopyMem((VOID *)Dest, (UINT8 *)FileBuffer + Phdr[i].p_offset, Phdr[i].p_filesz);
        }
    }

    *EntryPoint = Ehdr->e_entry;
    FreePool(FileBuffer); // We can free the file buffer now that segments are loaded
    return EFI_SUCCESS;
}

// --------------------------------------------------------------------------
// MAIN
// --------------------------------------------------------------------------

EFI_STATUS EFIAPI UefiMain(
    IN EFI_HANDLE        ImageHandle,
    IN EFI_SYSTEM_TABLE  *SystemTable
) {
    EFI_STATUS Status;
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
    EFI_FILE_HANDLE RootDir;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
    EFI_PHYSICAL_ADDRESS KernelEntry;
    PXS_BOOT_INFO *BootInfo;
    UINTN MemoryMapSize = 0;
    EFI_MEMORY_DESCRIPTOR *MemoryMap = NULL;
    UINTN MapKey;
    UINTN DescriptorSize;
    UINT32 DescriptorVersion;

    gST->ConOut->ClearScreen(gST->ConOut);
    Print(L"PXS Bootloader v%a\n", PXS_LOADER_VERSION);

    // 1. Initialize File System
    Status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&LoadedImage);
    if (EFI_ERROR(Status)) {
        Print(L"Error: LoadedImageProtocol not found\n");
        return Status;
    }

    Status = gBS->HandleProtocol(LoadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&FileSystem);
    if (EFI_ERROR(Status)) {
        Print(L"Error: SimpleFileSystemProtocol not found\n");
        return Status;
    }

    Status = FileSystem->OpenVolume(FileSystem, &RootDir);
    if (EFI_ERROR(Status)) {
        Print(L"Error: Could not open volume\n");
        return Status;
    }

    // 2. Load Kernel
    Status = LoadElfKernel(RootDir, KERNEL_PATH, &KernelEntry);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to load kernel.\n");
        while(1);
    }
    RootDir->Close(RootDir);

    // 3. Setup Graphics
    Status = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
    if (EFI_ERROR(Status)) {
        Print(L"Error: GOP not found\n");
        return Status;
    }

    // 4. Prepare BootInfo
    // We allocate this as EfiLoaderData so the kernel respects it
    Status = gBS->AllocatePool(EfiLoaderData, sizeof(PXS_BOOT_INFO), (VOID **)&BootInfo);
    if (EFI_ERROR(Status)) return EFI_OUT_OF_RESOURCES;

    SetMem(BootInfo, sizeof(PXS_BOOT_INFO), 0);
    BootInfo->Framebuffer.BaseAddress = Gop->Mode->FrameBufferBase;
    BootInfo->Framebuffer.Size = Gop->Mode->FrameBufferSize;
    BootInfo->Framebuffer.Width = Gop->Mode->Info->HorizontalResolution;
    BootInfo->Framebuffer.Height = Gop->Mode->Info->VerticalResolution;
    BootInfo->Framebuffer.PixelsPerScanLine = Gop->Mode->Info->PixelsPerScanLine;

    // RGB Format handling (Simplified)
    if (Gop->Mode->Info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor) {
        BootInfo->Framebuffer.RedFieldPosition = 0;
        BootInfo->Framebuffer.RedMaskSize = 8;
        BootInfo->Framebuffer.GreenFieldPosition = 8;
        BootInfo->Framebuffer.GreenMaskSize = 8;
        BootInfo->Framebuffer.BlueFieldPosition = 16;
        BootInfo->Framebuffer.BlueMaskSize = 8;
        BootInfo->Framebuffer.ReservedFieldPosition = 24;
        BootInfo->Framebuffer.ReservedMaskSize = 8;
    } else if (Gop->Mode->Info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor) {
        BootInfo->Framebuffer.RedFieldPosition = 16;
        BootInfo->Framebuffer.RedMaskSize = 8;
        BootInfo->Framebuffer.GreenFieldPosition = 8;
        BootInfo->Framebuffer.GreenMaskSize = 8;
        BootInfo->Framebuffer.BlueFieldPosition = 0;
        BootInfo->Framebuffer.BlueMaskSize = 8;
        BootInfo->Framebuffer.ReservedFieldPosition = 24;
        BootInfo->Framebuffer.ReservedMaskSize = 8;
    }
    // TODO: Handle Bitmask format if needed

    BootInfo->Rsdp = GetSystemConfigurationTable(&gEfiAcpi20TableGuid);
    if (!BootInfo->Rsdp) {
        BootInfo->Rsdp = GetSystemConfigurationTable(&gEfiAcpi10TableGuid);
    }
    BootInfo->Smbios = GetSystemConfigurationTable(&gEfiSmbiosTableGuid);
    BootInfo->RuntimeServicesPtr = (UINT64)gST->RuntimeServices;

    Print(L"Kernel loaded at 0x%lx\n", KernelEntry);
    Print(L"Framebuffer: %dx%d @ 0x%lx\n", BootInfo->Framebuffer.Width, BootInfo->Framebuffer.Height, BootInfo->Framebuffer.BaseAddress);
    Print(L"Exiting Boot Services...\n");

    // 5. Get Memory Map and Exit Boot Services
    // We need to do this in a loop because allocating memory for the map might change the map size.

    MemoryMapSize = 4096; // Initial guess
    Status = gBS->AllocatePool(EfiLoaderData, MemoryMapSize, (VOID **)&MemoryMap);
    if (EFI_ERROR(Status)) return EFI_OUT_OF_RESOURCES;

    Status = gBS->GetMemoryMap(&MemoryMapSize, MemoryMap, &MapKey, &DescriptorSize, &DescriptorVersion);
    while (Status == EFI_BUFFER_TOO_SMALL) {
        gBS->FreePool(MemoryMap);
        MemoryMapSize += 4096; // Add 4k padding
        Status = gBS->AllocatePool(EfiLoaderData, MemoryMapSize, (VOID **)&MemoryMap);
        if (EFI_ERROR(Status)) return EFI_OUT_OF_RESOURCES;
        Status = gBS->GetMemoryMap(&MemoryMapSize, MemoryMap, &MapKey, &DescriptorSize, &DescriptorVersion);
    }

    if (EFI_ERROR(Status)) {
        Print(L"Error: Could not get memory map\n");
        while(1);
    }

    BootInfo->MemoryMap = MemoryMap;
    BootInfo->MemoryMapSize = MemoryMapSize;
    BootInfo->DescriptorSize = DescriptorSize;
    BootInfo->DescriptorVersion = DescriptorVersion;
    BootInfo->MapKey = MapKey;

    Status = gBS->ExitBootServices(ImageHandle, MapKey);
    if (EFI_ERROR(Status)) {
        Print(L"Error: Could not exit boot services. %r\n", Status);
        // Retry logic usually needed here (get map again, exit again)
        // For simplicity, we halt, but in production code, retry.
        while(1);
    }

    // 6. Jump to Kernel
    // Kernel Entry Convention: void kernel_main(PXS_BOOT_INFO *BootInfo);
    KERNEL_ENTRY Entry = (KERNEL_ENTRY)KernelEntry;
    Entry(BootInfo);

    // Should not reach here
    while(1);
    return EFI_SUCCESS;
}
