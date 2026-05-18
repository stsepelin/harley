#include "format.h"
#include <stdio.h>

void format_km_grouped(uint32_t km, char *out, size_t out_size)
{
    char digits[16];
    int n = snprintf(digits, sizeof(digits), "%u", (unsigned)km);

    size_t gi = 0;
    int digits_until_comma = ((n - 1) % 3) + 1;
    for (int i = 0; i < n && gi + 1 < out_size; i++) {
        out[gi++] = digits[i];
        digits_until_comma--;
        if (digits_until_comma == 0 && i < n - 1 && gi + 1 < out_size) {
            out[gi++] = ',';
            digits_until_comma = 3;
        }
    }
    out[gi] = '\0';
}

void format_km_tenth(uint32_t meters, char *out, size_t out_size)
{
    uint32_t tenths_km = meters / 100;
    unsigned km     = (unsigned)(tenths_km / 10);
    unsigned tenths = (unsigned)(tenths_km % 10);
    snprintf(out, out_size, "%u.%u", km, tenths);
}
