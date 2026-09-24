/* Differential-test harness: calls the Farmos runtime float formatter (farm_format_float in
 * runtime/farm_rt.c) directly. Reads one 16-hex-digit binary64 bit pattern per line on stdin,
 * writes the formatted string per line on stdout. Build: clang -O2 -std=c11 -I runtime ... */
#include "farm_rt.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main(void) {
#ifdef _WIN32
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  static char obuf[1 << 20];
  setvbuf(stdout, obuf, _IOFBF, sizeof obuf);
  char line[128], out[64];
  while (fgets(line, sizeof line, stdin)) {
    uint64_t bits = strtoull(line, NULL, 16);
    double d;
    memcpy(&d, &bits, sizeof d);
    int n = farm_format_float(d, out, sizeof out);
    if (n < 0 || n >= (int)sizeof out) { fputs("<<format-error>>\n", stdout); continue; }
    fwrite(out, 1, (size_t)n, stdout);
    fputc('\n', stdout);
  }
  return 0;
}
