/*
 * Spec for SMAPI charge threshold control:
 *
 * To issue a SMAPI request, set EAX to 0x5380, then do:
 *  out <smapi port>, al
 *  out 0x4f, al
 *
 * The SMAPI call takes input in ebc, ecx, edi, and esi.
 *
 * On error, ah will contain a nonzero value.  0xA6 apparently means
 * "try again".
 *
 * If SMAPI is present, CMOS bytes 0x7C and 0x7D will contain 0x5349
 * (little endian) and CMOS bytes 0x7E and 0x7F will contain the port
 * (also little endian).
 *
 * On X200s, the port is 0xB2 (for usermode testing).
 *
 * ebx should contain:
 *  0x2116 to get the start threshold
 *  0x2117 to set the start threshold
 *  0x211a to get the stop threshold
 *  0x211b to set the stop threshold
 *
 * ch should contain the battery number (1 or 2).  Other inputs to
 * "get" calls should be zero.
 *
 * On "get", the answer goes in cl, and ch will have its low bit set.
 *
 * On "set", the threshold goes in cl (0 = default).  edi and esi
 * should be read by "get" and passed back into "set" verbatim.
 * Do not issue another SMAPI call for 50ms after a "set" or the system
 * may forget the value you just set.
 *
 * Setting 0 for stop means 100%.  Setting 100% is invalid.
 *
 * A start threshold of 0 means 98%.
 */

#include <stdint.h>
#include <sys/io.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

uint16_t smapi_port1 = 0xB2;
const uint16_t smapi_port2 = 0x4F;

int get_threshold(int bat, int start, uint8_t *val)
{
  int eax, ecx, errcode;

  asm volatile ("out %%al, %2\n\t"
		"out %%al, $0x4F"
		: "=c" (ecx), "=a" (eax)
		: "d" (smapi_port1),
		  "a" (0x5380), "b" (start ? 0x2116 : 0x211a),
		  "c" ((bat+1) << 8), "S" (0), "D" (0)
		: "cc");

  errcode = (eax >> 8) & 0xFF;
  if (errcode == 0xA6)
    return -EAGAIN;
  else if (errcode != 0)
    return -EIO;

  if ((ecx & 0x0100) != 0x0100)
    return -EIO;

  *val = ecx & 0xFF;
  return 0;
}

int set_threshold(int bat, int start, uint8_t val)
{
  int eax, ecx, errcode;
  int esi, edi;

  /* First query, but keep ESI and EDI. */
  asm volatile ("out %%al, %4\n\t"
		"out %%al, $0x4F"
		: "=c" (ecx), "=a" (eax), "=S" (esi), "=D" (edi)
		: "d" (smapi_port1),
		  "a" (0x5380), "b" (start ? 0x2116 : 0x211a),
		  "c" ((bat+1) << 8), "S" (0), "D" (0)
		: "cc");

  errcode = (eax >> 8) & 0xFF;
  if (errcode == 0xA6)
    return -EAGAIN;
  else if (errcode != 0)
    return -EIO;

  if ((ecx & 0x0100) != 0x0100)
    return -EIO;

  printf("%X %X %X\n", esi, edi, ecx);

  /* Now set. */
  asm volatile ("out %%al, %1\n\t"
		"out %%al, $0x4F"
		: "=a" (eax)
		: "d" (smapi_port1),
		  "a" (0x5380), "b" (start ? 0x2117 : 0x211b),
		  "c" (((bat+1) << 8) | val), "S" (esi), "D" (edi)
		: "cc");
  
  usleep(50000);

  errcode = (eax >> 8) & 0xFF;
  if (errcode == 0xA6)
    return -EAGAIN;
  else if (errcode != 0)
    return -EIO;

  return 0;
}

int main(int argc, char **argv)
{
  uint8_t start, stop;
  int err;

  fprintf(stderr, "Request IO permissions.\n");

  if (ioperm(smapi_port1, smapi_port1, 1) != 0) {
    perror("ioperm");
    return 1;
  }

  if (ioperm(smapi_port2, smapi_port2, 1) != 0) {
    perror("ioperm");
    return 1;
  }

  if (argc == 1) {
    fprintf(stderr, "Get BAT0 start.\n");
    err = get_threshold(0, 1, &start);
    if (err != 0) {
      errno = -err;
      perror("get_threshold");
      return 1;
    }

    fprintf(stderr, "Get BAT0 stop.\n");
    err = get_threshold(0, 0, &stop);
    if (err != 0) {
      errno = -err;
      perror("get_threshold");
      return 1;
    }

    printf("start = %d, stop = %d\n", (int)start, (int)stop);
  } else if (argc == 3) {
    start = (uint8_t)atoi(argv[1]);
    stop = (uint8_t)atoi(argv[2]);

    fprintf(stderr, "Set BAT0 start.\n");
    err = set_threshold(0, 1, start);
    if (err != 0) {
      errno = -err;
      perror("set_threshold");
      return 1;
    }

    fprintf(stderr, "Set BAT0 stop.\n");
    err = set_threshold(0, 0, stop);
    if (err != 0) {
      errno = -err;
      perror("set_threshold");
      return 1;
    }
  }

  return 0;
}
