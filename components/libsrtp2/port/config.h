#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <inttypes.h>  // For PRIx64 etc. (used in upstream debug_print calls)
#include <stddef.h>  // For size_t definition

/* Define if building for a CISC machine (e.g. Intel). */
#define CPU_CISC 1

/* Report errors to stdout. */
#define ERR_REPORTING_STDOUT 1

/* Define to 1 if you have the <arpa/inet.h> header file. */
#define HAVE_ARPA_INET_H 1

/* Define to 1 if you have the <netinet/in.h> header file. */
#define HAVE_NETINET_IN_H 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the <sys/socket.h> header file. */
#define HAVE_SYS_SOCKET_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to 1 if you want to use external crypto. */
#define SRTP_CRYPTO_MBEDTLS 1

/* Define package info */
#define PACKAGE_NAME "libsrtp2"
#define PACKAGE_VERSION "2.4.2"
#define PACKAGE_STRING PACKAGE_NAME " " PACKAGE_VERSION

/* The size of `unsigned long', as computed by sizeof. Hardcoded
 * values are correct for 32-bit ESP targets (xtensa lx6/lx7,
 * RISC-V rv32). On the IDF Linux target the build runs natively
 * on the host (typically 64-bit), so use the compiler-provided
 * __SIZEOF_LONG__ instead — that expands to 4 on ESP and 8 on
 * x86_64 / arm64 hosts. Without this, libsrtp's cipher
 * self-tests in `srtp_crypto_kernel_init()` fail with
 * srtp_err_status_alloc_fail / _algo_fail and `srtp_init()`
 * returns non-OK on Linux IDF target. */
#define SIZEOF_UNSIGNED_LONG       __SIZEOF_LONG__

/* The size of `unsigned long long', as computed by sizeof. */
#define SIZEOF_UNSIGNED_LONG_LONG  __SIZEOF_LONG_LONG__

/* Force SRTP to use system types */
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UINT64_T 1
#define HAVE_UINT32_T 1
#define HAVE_UINT16_T 1
#define HAVE_UINT8_T 1
#define HAVE_INT64_T 1
#define HAVE_INT32_T 1
#define HAVE_INT16_T 1
#define HAVE_INT8_T 1

/* Prevent SRTP from redefining types */
#define INTEGERS_H

/* Prevent srtp_ssrc_t redefinition */
#ifdef __XTENSA__
#define SRTP_KERNEL_COMPAT
#endif

/* Prevent in_addr_t redefinition */
#ifndef _LWIP_INET_H
typedef uint32_t in_addr_t;
#endif

#define GCM 1
#define MBEDTLS 1
#define ERR_REPORTING_STDOUT 1

#endif /* CONFIG_H */
