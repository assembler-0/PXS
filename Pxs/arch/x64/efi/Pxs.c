#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/BaseLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/Rng.h>
#include <Guid/FileInfo.h>
#include <Guid/Acpi.h>
#include <Guid/SmBios.h>

#include <compiler.h>
#include <elf.h>
#include <include/protocol.h>

#define PXS_LOADER_VERSION "0.1.0"
#define DEFAULT_KERNEL_PATH L"voidframex.krnl"
#define DEFAULT_CONFIG_PATH L"pxs.cfg"

// Kernel Entry Point Type
typedef VOID (__sysv_abi *KERNEL_ENTRY)(PXS_BOOT_INFO *BootInfo);

typedef struct {
    CHAR16 KernelPath[256];
    CHAR16 InitrdPath[256];
    CHAR8  CmdLine[512];
    UINTN  Timeout;
    BOOLEAN KaslrEnabled;
} PXS_CONFIG;

// --------------------------------------------------------------------------
// HELPER FUNCTIONS
// --------------------------------------------------------------------------

VOID WaitForInput() {
    EFI_INPUT_KEY Key;
    UINTN EventIndex;
    gST->ConIn->Reset(gST->ConIn, FALSE);
    gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &EventIndex);
    gST->ConIn->ReadKeyStroke(gST->ConIn, &Key);
}

VOID FatalError(IN CHAR16 *Message, IN EFI_STATUS Status) {
    Print(L"\nCRITICAL ERROR: %s\n", Message);
    if (EFI_ERROR(Status)) {
        Print(L"Status Code: %r\n", Status);
    }
    Print(L"\nPress any key to reboot...\n");
    WaitForInput();
    gST->RuntimeServices->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
}

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

EFI_STATUS LoadFile(
    IN EFI_FILE_HANDLE RootDir,
    IN CHAR16 *FileName,
    OUT VOID **Buffer,
    OUT UINT64 *Size
) {
    EFI_STATUS Status;
    EFI_FILE_HANDLE FileHandle;
    UINT64 FileSize;
    VOID *FileBuffer;

    Status = RootDir->Open(RootDir, &FileHandle, FileName, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status)) return Status;

    Status = GetFileSize(FileHandle, &FileSize);
    if (EFI_ERROR(Status)) {
        FileHandle->Close(FileHandle);
        return Status;
    }

    FileBuffer = AllocatePool(FileSize);
    if (!FileBuffer) {
        FileHandle->Close(FileHandle);
        return EFI_OUT_OF_RESOURCES;
    }

    UINTN ReadSize = FileSize;
    Status = FileHandle->Read(FileHandle, &ReadSize, FileBuffer);
    FileHandle->Close(FileHandle);

    if (EFI_ERROR(Status)) {
        FreePool(FileBuffer);
        return Status;
    }

    *Buffer = FileBuffer;
    *Size = FileSize;
    return EFI_SUCCESS;
}

UINT64 GetBestEntropy() {
    EFI_STATUS Status;
    UINT64 Seed = 0;

    // 1. Try UEFI RNG Protocol
    EFI_RNG_PROTOCOL *Rng;
    Status = gBS->LocateProtocol(&gEfiRngProtocolGuid, NULL, (VOID **)&Rng);
    if (!EFI_ERROR(Status)) {
        Status = Rng->GetRNG(Rng, NULL, sizeof(Seed), (UINT8*)&Seed);
        if (!EFI_ERROR(Status) && Seed != 0) {
            return Seed;
        }
    }

    // 2. Try RDRAND (Hardware Instruction)
    UINT32 Eax, Ecx, Edx;
    // CPUID Leaf 1, ECX[30] = RDRAND
    // Preserve RBX as it is callee-saved and used by PIC
    __asm__ __volatile__ (
        "pushq %%rbx\n\t"
        "cpuid\n\t"
        "popq %%rbx"
        : "=a" (Eax), "=c" (Ecx), "=d" (Edx)
        : "a" (1)
        : "cc"
    );

    if (Ecx & (1 << 30)) {
        UINT8 Success = 0;
        // Retry loop for RDRAND underflow
        for (int i = 0; i < 10; i++) {
            __asm__ __volatile__ (
                "rdrand %0; setc %1"
                : "=r" (Seed), "=qm" (Success)
            );
            if (Success) return Seed;
        }
    }

    // 3. Fallback: Mix Time and TSC
    EFI_TIME Time;
    gST->RuntimeServices->GetTime(&Time, NULL);

    Seed = __builtin_ia32_rdtsc();
    Seed ^= ((UINT64)Time.Nanosecond << 32);
    Seed ^= ((UINT64)Time.Year << 16) | ((UINT64)Time.Month << 8) | Time.Day;
    Seed ^= ((UINT64)Time.Hour << 24) | ((UINT64)Time.Minute << 16) | ((UINT64)Time.Second << 8);

    // Simple mixing step (XOR-shift style)
    Seed ^= (Seed << 13);
    Seed ^= (Seed >> 7);
    Seed ^= (Seed << 17);

    return Seed;
}

// Simple config parser
// Format: KEY=VALUE
// KERNEL=path
// INITRD=path
// CMDLINE=string
VOID LoadConfig(
    IN EFI_FILE_HANDLE RootDir,
    IN CHAR16 *ConfigName,
    OUT PXS_CONFIG *Config
) {
    EFI_STATUS Status;
    VOID *Buffer;
    UINT64 Size;
    CHAR8 *AsciiBuffer;

    // Set defaults
    StrCpyS(Config->KernelPath, 256, DEFAULT_KERNEL_PATH);
    Config->InitrdPath[0] = L'\0';
    Config->CmdLine[0] = '\0';
    Config->Timeout = 3;
    Config->KaslrEnabled = TRUE;

    Status = LoadFile(RootDir, ConfigName, &Buffer, &Size);
    if (EFI_ERROR(Status)) {
        Print(L"Config '%s' not found. Using defaults.\n", ConfigName);
        return;
    }

    AsciiBuffer = (CHAR8*)Buffer;
    UINTN Start = 0;
    UINTN End = 0;

    while (End < Size) {
        // Find end of line
        while (End < Size && AsciiBuffer[End] != '\n' && AsciiBuffer[End] != '\r') {
            End++;
        }

        // Process line [Start, End)
        if (End > Start) {
            // Skip leading whitespace
            while (Start < End && (AsciiBuffer[Start] == ' ' || AsciiBuffer[Start] == '\t')) {
                Start++;
            }

            // Check for KERNEL=
            if (AsciiStrnCmp(&AsciiBuffer[Start], "KERNEL=", 7) == 0) {
                UINTN ValStart = Start + 7;
                UINTN ValLen = End - ValStart;
                // Convert to CHAR16
                for (UINTN i=0; i < ValLen && i < 255; i++) {
                    Config->KernelPath[i] = (CHAR16)AsciiBuffer[ValStart + i];
                }
                Config->KernelPath[ValLen < 255 ? ValLen : 255] = L'\0';
            }
            // Check for INITRD=
            else if (AsciiStrnCmp(&AsciiBuffer[Start], "INITRD=", 7) == 0) {
                UINTN ValStart = Start + 7;
                UINTN ValLen = End - ValStart;
                for (UINTN i=0; i < ValLen && i < 255; i++) {
                    Config->InitrdPath[i] = (CHAR16)AsciiBuffer[ValStart + i];
                }
                Config->InitrdPath[ValLen < 255 ? ValLen : 255] = L'\0';
            }
            // Check for CMDLINE=
            else if (AsciiStrnCmp(&AsciiBuffer[Start], "CMDLINE=", 8) == 0) {
                UINTN ValStart = Start + 8;
                UINTN ValLen = End - ValStart;
                for (UINTN i=0; i < ValLen && i < 511; i++) {
                    Config->CmdLine[i] = AsciiBuffer[ValStart + i];
                }
                Config->CmdLine[ValLen < 511 ? ValLen : 511] = '\0';
            }
            // Check for TIMEOUT=
            else if (AsciiStrnCmp(&AsciiBuffer[Start], "TIMEOUT=", 8) == 0) {
                UINTN ValStart = Start + 8;
                UINTN ValLen = End - ValStart;
                UINTN TimeoutVal = 0;
                for (UINTN i = 0; i < ValLen; i++) {
                    CHAR8 c = AsciiBuffer[ValStart + i];
                    if (c >= '0' && c <= '9') {
                        TimeoutVal = TimeoutVal * 10 + (c - '0');
                    } else {
                        break; // Stop at non-digit
                    }
                }
                Config->Timeout = TimeoutVal;
            }
            // Check for KASLR=
            else if (AsciiStrnCmp(&AsciiBuffer[Start], "KASLR=", 6) == 0) {
                UINTN ValStart = Start + 6;
                // Check for 0 or FALSE
                if (AsciiBuffer[ValStart] == '0') {
                    Config->KaslrEnabled = FALSE;
                } else if (AsciiStrnCmp(&AsciiBuffer[ValStart], "FALSE", 5) == 0) {
                    Config->KaslrEnabled = FALSE;
                }
            }
        }

        // Skip newline chars
        while (End < Size && (AsciiBuffer[End] == '\n' || AsciiBuffer[End] == '\r')) {
            End++;
        }
        Start = End;
    }
    SetMem(Buffer, Size, 0); // Secure wipe
    FreePool(Buffer);
    Print(L"Config Loaded: Kernel=%s, KASLR=%s\n", Config->KernelPath, Config->KaslrEnabled ? L"ON" : L"OFF");
    if (Config->CmdLine[0] != '\0') {
        Print(L"CmdLine: %a\n", Config->CmdLine);
    }
}


// --------------------------------------------------------------------------
// ELF LOADER
// --------------------------------------------------------------------------

EFI_STATUS LoadElfKernel(
    IN EFI_FILE_HANDLE RootDir,
    IN PXS_CONFIG *Config,
    OUT EFI_PHYSICAL_ADDRESS *EntryPoint,
    OUT UINT64 *KernelBase,
    OUT UINT64 *KernelSize
) {
    EFI_STATUS Status;
    UINT64 FileSize;
    VOID *FileBuffer;
    Elf64_Ehdr *Ehdr;
    Elf64_Phdr *Phdr;
    UINTN i;
    Print(L"Loading Kernel: %s\n", Config->KernelPath);

    Status = LoadFile(RootDir, Config->KernelPath, &FileBuffer, &FileSize);
    if (EFI_ERROR(Status)) {
        Print(L"Error: Could not open kernel file '%s'. %r\n", Config->KernelPath, Status);
        return Status;
    }

    *KernelSize = FileSize;

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

    // Calculate Total Kernel Size (Phys Min to Phys Max)
    Phdr = (Elf64_Phdr *)((UINT8 *)FileBuffer + Ehdr->e_phoff);
    UINT64 MinPhys = 0xFFFFFFFFFFFFFFFF;
    UINT64 MaxPhys = 0;

    for (i = 0; i < Ehdr->e_phnum; i++) {
        if (Phdr[i].p_type == PT_LOAD) {
            if (Phdr[i].p_paddr < MinPhys) MinPhys = Phdr[i].p_paddr;
            UINT64 End = Phdr[i].p_paddr + Phdr[i].p_memsz;
            if (End > MaxPhys) MaxPhys = End;
        }
    }

    // Align MinPhys down and MaxPhys up to Page Boundaries
    UINT64 BaseOffset = MinPhys & ~(0xFFF);
    UINT64 TotalSize = (MaxPhys + 0xFFF) & (~(0xFFF) - BaseOffset);
    UINTN TotalPages = EFI_SIZE_TO_PAGES(TotalSize);
    Print(L"Image Size: 0x%lx bytes (%d Pages)\n", TotalSize, TotalPages);

    if (MinPhys == 0) {
        Print(L"WARNING: Kernel linked at 0x0. This is a Null Pointer Trap Hazard\n");
        Print(L"         Consider linking higher (e.g., 1MB+) or using -Ttext.\n");
    }

    // KASLR Logic
    EFI_PHYSICAL_ADDRESS LoadBase = 0;
    UINT64 Slide = 0;
    BOOLEAN KaslrSuccess = FALSE;
    UINT64 RandomSeed = 0;

    if (Config->KaslrEnabled) {
        RandomSeed = GetBestEntropy();

        if (RandomSeed != 0) {
            // Try 64 times to find a slot
            for (int attempt = 0; attempt < 64; attempt++) {
                // Simple LCG for next attempt if needed
                RandomSeed = RandomSeed * 6364136223846793005ULL + 1;
                // Constrain to 2MB - 1GB range
                UINT64 Candidate = 0x200000 + (RandomSeed % (0x40000000 - TotalSize));
                Candidate &= ~(0x1FFFFF); // Align to 2MB

                Status = gBS->AllocatePages(AllocateAddress, EfiLoaderData, TotalPages, &Candidate);
                if (!EFI_ERROR(Status)) {
                    LoadBase = Candidate;
                    Slide = LoadBase - BaseOffset;
                    KaslrSuccess = TRUE;
                    Print(L"KASLR: Loaded at 0x%lx (Slide: 0x%lx)\n", LoadBase, Slide);
                    break;
                }
            }
        }
    } else {
        Print(L"KASLR: Disabled by config.\n");
    }

    if (!KaslrSuccess) {
        if (Config->KaslrEnabled) {
            Print(L"KASLR failed. Fallback to fixed address.\n");
        }
        LoadBase = BaseOffset;
        Status = gBS->AllocatePages(AllocateAddress, EfiLoaderData, TotalPages, &LoadBase);
        if (EFI_ERROR(Status)) {
             FreePool(FileBuffer);
             return Status;
        }
    }
    // Zero the entire allocated block
    SetMem((VOID *)LoadBase, TotalSize, 0);

    // Load Segments
    for (i = 0; i < Ehdr->e_phnum; i++) {
        if (Phdr[i].p_type == PT_LOAD) {
            UINT64 PhysAddr = Phdr[i].p_paddr + Slide;
            UINT64 OffsetInAlloc = PhysAddr - LoadBase;
            // Print(L"Segment %d: Load @ 0x%lx\n", i, PhysAddr);
            CopyMem((VOID *)(LoadBase + OffsetInAlloc), (UINT8 *)FileBuffer + Phdr[i].p_offset, Phdr[i].p_filesz);
        }
    }
    *EntryPoint = Ehdr->e_entry + Slide;
    *KernelBase = LoadBase;
    SetMem(FileBuffer, FileSize, 0); // Secure wipe
    FreePool(FileBuffer);
    return EFI_SUCCESS;
}

// --------------------------------------------------------------------------
// MAIN
// --------------------------------------------------------------------------

EFI_STATUS EFIAPI UefiMain(
    IN EFI_HANDLE        ImageHandle,
    IN EFI_SYSTEM_TABLE  *SystemTable
)
{
    EFI_STATUS Status;
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
    EFI_FILE_HANDLE RootDir;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
    EFI_PHYSICAL_ADDRESS KernelEntry;
    PXS_BOOT_INFO *BootInfo;
    PXS_CONFIG Config;
    UINTN MemoryMapSize = 0;
    EFI_MEMORY_DESCRIPTOR *MemoryMap = NULL;
    UINTN MapKey;
    UINTN DescriptorSize;
    UINT32 DescriptorVersion;
    VOID *InitrdBuffer = NULL;
    UINT64 InitrdSize = 0;

    gST->ConOut->ClearScreen(gST->ConOut);
    Print(L"[-- PXS v%a --]\n", PXS_LOADER_VERSION);

    // 1. Initialize File System
    Status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&LoadedImage);
    if (EFI_ERROR(Status)) {
        FatalError(L"LoadedImageProtocol not found", Status);
    }

    Status = gBS->HandleProtocol(LoadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&FileSystem);
    if (EFI_ERROR(Status)) {
        FatalError(L"SimpleFileSystemProtocol not found", Status);
    }

    Status = FileSystem->OpenVolume(FileSystem, &RootDir);
    if (EFI_ERROR(Status)) {
        FatalError(L"Could not open volume", Status);
    }

    // 2. Load Configuration
    LoadConfig(RootDir, DEFAULT_CONFIG_PATH, &Config);

    // 3. Prepare BootInfo
    Status = gBS->AllocatePool(EfiLoaderData, sizeof(PXS_BOOT_INFO), (VOID **)&BootInfo);
    if (EFI_ERROR(Status)) FatalError(L"Failed to allocate BootInfo", Status);
    SetMem(BootInfo, sizeof(PXS_BOOT_INFO), 0);

    BootInfo->Magic = PXS_MAGIC;
    BootInfo->Version = 1; // Protocol Version 1
    BootInfo->Flags = 0;

    // Copy Command Line
    UINTN CmdLineLen = AsciiStrLen(Config.CmdLine);
    if (CmdLineLen > 0) {
        VOID *CmdLineBuffer;
        Status = gBS->AllocatePool(EfiLoaderData, CmdLineLen + 1, &CmdLineBuffer);
        if (!EFI_ERROR(Status)) {
            CopyMem(CmdLineBuffer, Config.CmdLine, CmdLineLen + 1);
            BootInfo->CommandLine = (CHAR8*)CmdLineBuffer;
        }
    } else {
        BootInfo->CommandLine = NULL;
    }

    // 4. Load Initrd (if specified)
    if (StrLen(Config.InitrdPath) > 0) {
        Print(L"Loading Initrd: %s\n", Config.InitrdPath);
        Status = LoadFile(RootDir, Config.InitrdPath, &InitrdBuffer, &InitrdSize);
        if (EFI_ERROR(Status)) {
            Print(L"Warning: Failed to load Initrd '%s'. Continuing...\n", Config.InitrdPath);
        } else {
            BootInfo->InitrdAddress = (UINT64)InitrdBuffer;
            BootInfo->InitrdSize = InitrdSize;
            Print(L"Initrd Loaded @ 0x%lx (Size: %ld bytes)\n", BootInfo->InitrdAddress, BootInfo->InitrdSize);
        }
    }

    // 5. Load Kernel
    Status = LoadElfKernel(RootDir, &Config, &KernelEntry, &BootInfo->KernelPhysicalBase, &BootInfo->KernelFileSize);
    if (EFI_ERROR(Status)) {
        FatalError(L"Failed to load kernel", Status);
    }
    RootDir->Close(RootDir);

    // 6. Setup Graphics
    Status = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
    if (EFI_ERROR(Status)) {
        Print(L"Warning: GOP not found. Headless mode.\n");
    } else {
        BootInfo->Framebuffer.BaseAddress = Gop->Mode->FrameBufferBase;
        BootInfo->Framebuffer.Size = Gop->Mode->FrameBufferSize;
        BootInfo->Framebuffer.Width = Gop->Mode->Info->HorizontalResolution;
        BootInfo->Framebuffer.Height = Gop->Mode->Info->VerticalResolution;
        BootInfo->Framebuffer.PixelsPerScanLine = Gop->Mode->Info->PixelsPerScanLine;

        if (Gop->Mode->Info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor) {
            BootInfo->Framebuffer.RedFieldPosition = 0; BootInfo->Framebuffer.RedMaskSize = 8;
            BootInfo->Framebuffer.GreenFieldPosition = 8; BootInfo->Framebuffer.GreenMaskSize = 8;
            BootInfo->Framebuffer.BlueFieldPosition = 16; BootInfo->Framebuffer.BlueMaskSize = 8;
            BootInfo->Framebuffer.ReservedFieldPosition = 24; BootInfo->Framebuffer.ReservedMaskSize = 8;
        } else if (Gop->Mode->Info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor) {
            BootInfo->Framebuffer.RedFieldPosition = 16; BootInfo->Framebuffer.RedMaskSize = 8;
            BootInfo->Framebuffer.GreenFieldPosition = 8; BootInfo->Framebuffer.GreenMaskSize = 8;
            BootInfo->Framebuffer.BlueFieldPosition = 0; BootInfo->Framebuffer.BlueMaskSize = 8;
            BootInfo->Framebuffer.ReservedFieldPosition = 24; BootInfo->Framebuffer.ReservedMaskSize = 8;
        }
    }

    BootInfo->Rsdp = GetSystemConfigurationTable(&gEfiAcpi20TableGuid);
    if (!BootInfo->Rsdp) {
        BootInfo->Rsdp = GetSystemConfigurationTable(&gEfiAcpi10TableGuid);
    }
    BootInfo->Smbios = GetSystemConfigurationTable(&gEfiSmbiosTableGuid);
    BootInfo->RuntimeServicesPtr = (UINT64)gST->RuntimeServices;

    // Security Canary Generation
    BootInfo->SecurityCanary = GetBestEntropy();
    Print(L"Security Canary: 0x%lx\n", BootInfo->SecurityCanary);

    if (BootInfo->Rsdp) {
        Print(L"RSDP found at 0x%lx\n", (UINT64)BootInfo->Rsdp);
    } else {
        Print(L"Warning: RSDP not found\n");
    }
    if (BootInfo->Smbios) {
        Print(L"SMBIOS found at 0x%lx\n", (UINT64)BootInfo->Smbios);
    }

    Print(L"Kernel loaded at 0x%lx (Entry: 0x%lx)\n", BootInfo->KernelPhysicalBase, KernelEntry);
    Print(L"Preparing for exit...\n");

    if (Config.Timeout > 0) {
        gBS->Stall(Config.Timeout * 1000000);
    }

    Print(L"[-- PXS INITIALIZATION COMPLETE --] -- exiting boot services...\n");

    // 7. Get Memory Map
    MemoryMapSize = 4096;
    Status = gBS->AllocatePool(EfiLoaderData, MemoryMapSize, (VOID **)&MemoryMap);
    if (EFI_ERROR(Status)) FatalError(L"Alloc MemoryMap failed", Status);

    Status = gBS->GetMemoryMap(&MemoryMapSize, MemoryMap, &MapKey, &DescriptorSize, &DescriptorVersion);
    while (Status == EFI_BUFFER_TOO_SMALL) {
        gBS->FreePool(MemoryMap);
        MemoryMapSize += 4096;
        Status = gBS->AllocatePool(EfiLoaderData, MemoryMapSize, (VOID **)&MemoryMap);
        if (EFI_ERROR(Status)) FatalError(L"Alloc MemoryMap retry failed", Status);
        Status = gBS->GetMemoryMap(&MemoryMapSize, MemoryMap, &MapKey, &DescriptorSize, &DescriptorVersion);
    }

    if (EFI_ERROR(Status)) {
        FatalError(L"GetMemoryMap failed", Status);
    }

    BootInfo->MemoryMap = MemoryMap;
    BootInfo->MemoryMapSize = MemoryMapSize;
    BootInfo->DescriptorSize = DescriptorSize;
    BootInfo->DescriptorVersion = DescriptorVersion;
    BootInfo->MapKey = MapKey;

    Status = gBS->ExitBootServices(ImageHandle, MapKey);
    if (EFI_ERROR(Status)) {
        Print(L"ExitBootServices failed. Retrying...\n");
        // Retry mechanism as per UEFI spec
        Status = gBS->GetMemoryMap(&MemoryMapSize, MemoryMap, &MapKey, &DescriptorSize, &DescriptorVersion);
        if (EFI_ERROR(Status)) FatalError(L"GetMemoryMap(2) failed", Status);
        BootInfo->MapKey = MapKey; // Update key
        Status = gBS->ExitBootServices(ImageHandle, MapKey);
        if (EFI_ERROR(Status)) {
            FatalError(L"ExitBootServices(2) failed", Status);
        }
    }

    // 8. Jump to Kernel
    KERNEL_ENTRY Entry = (KERNEL_ENTRY)KernelEntry;
    Entry(BootInfo);

    // Should not reach here
    while(1);
    return EFI_SUCCESS;
}
