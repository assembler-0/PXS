#!/bin/bash
set -e

build -a X64 -t GCC5 -p PxsPkg/PxsPkg.dsc -b DEBUG
