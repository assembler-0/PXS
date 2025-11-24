# VoidFrameX - modern UEFI kernel
> A modern EDK2-based monolithic kernel

## Quickstart 
```bash
git clone https://github.com/tianocore/edk2.git
cd edk2
git submodule update --init
make -C BaseTools
git clone https://github.com/assembler-0/VoidFrameX.git VoidFrameXPkg
source edksetup.sh
cd VoidFrameXPkg
./compile.sh
```
