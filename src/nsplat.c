// Cola entre o mbedTLS e a Xbox: relogio, gmtime e entropia.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <xboxkrnl/xboxkrnl.h>
#include "mbedtls/build_info.h"
#include "mbedtls/platform_util.h"

#ifndef NS_BUILD_TIME
#define NS_BUILD_TIME 1762900000LL   /* 2025-11-11 */
#endif

// Segundos Unix. Se o relogio da Xbox estiver antes da data de compilacao (pilha RTC
// gasta), usa a data de compilacao como minimo para a validacao dos certificados.
long long ns_time(long long *t)
{
    LARGE_INTEGER now;
    KeQuerySystemTime(&now);
    long long unix_s = (now.QuadPart - 116444736000000000LL) / 10000000LL;
    if (unix_s < NS_BUILD_TIME) unix_s = NS_BUILD_TIME;
    if (t) *t = unix_s;
    return unix_s;
}

struct tm *mbedtls_platform_gmtime_r(const mbedtls_time_t *tt, struct tm *tm_buf)
{
    long long t = (long long)*tt;
    long long days = t / 86400, rem = t % 86400;
    if (rem < 0) { rem += 86400; days--; }
    tm_buf->tm_hour = (int)(rem / 3600);
    tm_buf->tm_min = (int)((rem % 3600) / 60);
    tm_buf->tm_sec = (int)(rem % 60);
    tm_buf->tm_wday = (int)((days + 4) % 7);
    if (tm_buf->tm_wday < 0) tm_buf->tm_wday += 7;
    // dias desde 1970 -> data civil (algoritmo de Howard Hinnant)
    days += 719468;
    long long era = (days >= 0 ? days : days - 146096) / 146097;
    long long doe = days - era * 146097;
    long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long y = yoe + era * 400;
    long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long long mp = (5 * doy + 2) / 153;
    long long d = doy - (153 * mp + 2) / 5 + 1;
    long long m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) y++;
    tm_buf->tm_year = (int)(y - 1900);
    tm_buf->tm_mon = (int)(m - 1);
    tm_buf->tm_mday = (int)d;
    tm_buf->tm_yday = 0;
    tm_buf->tm_isdst = 0;
    return tm_buf;
}

static inline uint64_t rdtsc64(void)
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

// Entropia por jitter do contador de ciclos (a Xbox nao tem gerador aleatorio de hardware).
// E fraca comparada com um RNG de hardware: ver README.
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
    (void)data;
    static volatile uint32_t sink;
    for (size_t i = 0; i < len; i++) {
        uint32_t acc = 0;
        for (int bit = 0; bit < 8; bit++) {
            uint64_t a = rdtsc64();
            for (int k = 0; k < 64 + (int)(a & 31); k++) sink += (uint32_t)k * 2654435761u;
            LARGE_INTEGER pc; KeQueryPerformanceCounter();   // toca o hardware para variar o tempo
            (void)pc;
            uint64_t b = rdtsc64();
            acc = (acc << 1) | (uint32_t)((b - a) & 1) ^ (uint32_t)((b >> 3) & 1);
        }
        output[i] = (unsigned char)(acc ^ (rdtsc64() >> 5));
    }
    *olen = len;
    return 0;
}
