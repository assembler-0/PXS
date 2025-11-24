#pragma once

/* ========================
 * BASIC COMPILER DETECTION
 * ======================== */
#if defined(__clang__)
#  define COMPILER_CLANG 1
#elif defined(__GNUC__)
#  define COMPILER_GCC 1
#else
#  error "Unsupported compiler"
#endif

/* ========================
 * FUNCTION ATTRIBUTES
 * ======================== */

#define __noreturn      __attribute__((noreturn))
#define __noinline      __attribute__((noinline))
#define __flatten       __attribute__((flatten))
#define __hot           __attribute__((hot))
#define __cold          __attribute__((cold))
#define __unused        __attribute__((unused))
#define __used          __attribute__((used))

#define __optimize(x)   __attribute__((optimize(x)))

/* ========================
 * MEMORY / LAYOUT ATTRIBUTES
 * ======================== */

#define __aligned(x)    __attribute__((aligned(x)))
#define __packed        __attribute__((packed))
#define __weak          __attribute__((weak))
#define __alias(x)      __attribute__((alias(x)))
#define __section(x)    __attribute__((section(x)))
#define __visibility(x) __attribute__((visibility(x)))

/* ========================
 * ABI ATTRIBUTES (UEFI!)
 * ======================== */

#if defined(__x86_64__)
#  define __ms_abi      __attribute__((ms_abi))
#  define __sysv_abi    __attribute__((sysv_abi))
#else
#  define __ms_abi
#  define __sysv_abi
#endif

/* ========================
 * INIT / EXIT SECTIONS
 *   Similar to Linux’s .initcall
 * ======================== */

#define __init          __section(".init.text") __cold
#define __init_data     __section(".init.data")
#define __late_init     __section(".late_init")
#define __exit          __section(".exit.text") __cold

/* ========================
 * BRANCHING / FLOW
 * ======================== */

#define __fallthrough   __attribute__((fallthrough))
#define __unreachable() __builtin_unreachable()
#define __likely(x)     __builtin_expect(!!(x), 1)
#define __unlikely(x)   __builtin_expect(!!(x), 0)

/* ========================
 * BARRIERS
 * ======================== */

#define barrier()       __asm__ __volatile__("" ::: "memory")
#define cpu_relax()     __asm__ __volatile__("pause" ::: "memory")

/* ========================
 * POINTER CHECKING
 * ======================== */

#define __malloc_like      __attribute__((malloc))
