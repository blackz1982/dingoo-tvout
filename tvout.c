/* 
 * Dingoo A-320 TV-Out Tool for Linux
 *
 * Copyright (c) 2010 Ulrich Hecht
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define FBIOA320TVOUT	0x46F0

/* The non-negative values match the FBIOA320TVOUT argument values. */
enum TVStandard { NONE = -1, OFF = 0, NTSC = 1, PAL50 = 2 };

int fbd;

/* set a register on the Chrontel TV encoder */
void i2c(int addr, int val)
{
  char cmd[80];
  sprintf(cmd, "i2cset -y i2c-gpio-1 0x76 0x%x 0x%x", addr, val);
  system(cmd);
}

/* Enable the TV encoder.
   Set pal to non-zero for PAL encoding; default is NTSC.
   These are for the most part the values the native Dingoo firmware
   programs the TV encoder with.  Some of them sound patently absurd, but
   still work.
 */
void ctel_on(enum TVStandard tv)
{
  i2c(4, 0xc1); /* Power State: disable DACs, power down */

  if (tv == PAL50)
    i2c(0xa, 0x13);     /* Video Output Format: TV_BP = 0 (scaler on),
                           SVIDEO = 0 (composite output), DACSW = 01 (CVBS),
                           VOS = 0011 (PAL- B/D/G/H/K/I) */
  else
    i2c(0xa, 0x10);	/* same as above, except VOS = 0000 (NTSC-M) */

  i2c(0xb, 3);  /* Crystal Control: XTALSEL = 0 (use predefined frequency),
                   XTAL = 0011 (12 MHz) */

  i2c(0xd, 3);  /* Input Data Format 2: HIGH = 0, REVERSE = 0, SWAP = 0 (in
                   short: leave data as it is), IDF = 011 (input data
                   RGB565) */

  i2c(0xe, 0);	/* SYNC Control: POUTEN = 0 (no signal on POUT), DES (no
                   embedded sync in data), FLDSEN = 0 (no field select),
                   FLDS = 0 (ignored if FLDSEN = 0), HPO = 0 (negative
                   HSYNC), VPO = 0 (negative VSYNC), SYO = 0 (input sync),
                   DIFFEN = 0 (CMOS input) */

  /* These timings are the ones the native Dingoo firmware uses; the HTI values make
     no sense to me because they don't match the actual pixels encoded (858
     for NTSC, 864 for PAL) according to the datasheet.  We might want to
     try if simply turning on HVAUTO and skipping all this works, too...
   */
  if (tv == PAL50) {
    /* Input Timing: HVAUTO = 0 (timing from HTI, HAI),
       HTI (input horizontal total pixels) = 0x36c (876),
       HAI (input horizontal active pixels) = 0x140 (320)
     */
    i2c(0x11, 0x19);
    /* Input Timing Register 2 (0x12) defaults to 0x40 */
    i2c(0x13, 0x6c);
  }
  else {
    /* Input Timing:
       HTI = 0x2e0 (736),
       HAI = 0x140 (320)
     */
    i2c(0x11, 0x11);
    /* Input Timing Register 2 (0x12) defaults to 0x40 */
    i2c(0x13, 0xe0);
  }
  
  /* HW (HSYNC pulse width) and HO (HSYNC offset) are left at their
     defaults (2 and 4, respectively)
   */
  /* Input Timing Register 4 (0x14) defaults to 0 */
  /* Input Timing Register 5 (0x15) defaults to 4 */
  /* Input Timing Register 6 (0x16) defaults to 2 */
  
  /* VO (VSYNC offset) = 4,
     VTI (input vertical total pixels) = 530 (PAL), 528 (NTSC),
     VAI (input vertical active pixels) = 240
   */
  i2c(0x17, 4);
  /* Input Timing Register 8 (0x18) defaults to 0xf0 */
  if (tv == PAL50)
    i2c(0x19, 0x12);
  else
    i2c(0x19, 0x10);
  /* Input Timing Register 10 (0x1a) defaults to 4 */

  /* TVHA (TV output horizontal active pixels) = 1345 (WTF??) */
  /* Output Timing Register 1 (0x1e) defaults to 5 */
  i2c(0x1f, 0x41);

  /* VP (vertical position) = 512, i.e. no adjustment;
     PCLK clock divider remains at default value (67108864).
   */
  if (tv == PAL50) {
    /* HP (horizontal position) = 503, i.e. adjust -9 pixels */
    i2c(0x23, 0x7a);
    /* UCLK clock divider: numerator 1932288... */
    i2c(0x28, 0x1d);
    i2c(0x29, 0x7c);
    i2c(0x2a, 0);
    /* ...denominator 2160000 */
    i2c(0x2b, 0x20);
    i2c(0x2c, 0xf5);
    i2c(0x2d, 0x80);
  }
  else {
    /* HP (horizontal position) for NTSC = 508, i.e. adjust -4 pixels */
    i2c(0x23, 0x7f);
    /* UCLK clock divider: numerator 1597504... */
    i2c(0x28, 0x18);
    i2c(0x29, 0x60);
    i2c(0x2a, 0x40);
    /* ...denominator 1801800 */
    i2c(0x2b, 0x1b);
    i2c(0x2c, 0x7e);
    i2c(0x2d, 0x48);
  }
  i2c(0x2e, 0x38); /* clock divider integer register (M value for PLL) */

  /* PLL ratio */
  /* PLL Ratio Register 1 defaults to 0x12, PLL1 and PLL2 pre-dividers = 2 */
  i2c(0x30, 0x12); /* PLL3 pre-divider and post-divider 1 = 2 */
  i2c(0x31, 0x13); /* PLL3 post-divider 2 = 3 */
  
  /* FSCISPP (sub-carrier frequency adjustment) remains at 0 */
  /* FSCI Adjustment Register 1 defaults to 0 */
  i2c(0x33, 0);	/* FIXME: This actually is a default value, too. */
  
  i2c(0x39, 0x12); /* FIXME: This looks completely bogus. I don't see it in
                      any dumps, and it isn't even documented.  Should
                      probably be removed. */

  i2c(0x63, 0xc2); /* SEL_R = 1 (double termination) */

  i2c(4, 0);	/* enable DACs, power up; FIXME: I think the first DAC is
                   enough for us. */
}

void ctel_off(void)
{
  i2c(4, 0xc1);	/* disable DACs, power down */
}

void map_io(void)
{
  fbd = open("/dev/fb0", O_RDWR);
}

void lcdc_set(enum TVStandard tv)
{
  ioctl(fbd, FBIOA320TVOUT, tv);
}

int main(int argc, char **argv)
{
  enum TVStandard tv = NONE;
  for (; argc > 1; argc--, argv++) {
    if (!strcmp(argv[1], "--pal")) tv = PAL50;
    else if (!strcmp(argv[1], "--ntsc")) tv = NTSC;
    else if (!strcmp(argv[1], "--off")) tv = OFF;
    else if (!strcmp(argv[1], "--help")) tv = NONE;
    else {
      fprintf(stderr, "Unknown option %s\n", argv[1]);
      return 1;
    }
  }
  if (tv == NONE) {
    fprintf(stderr,
      "Usage: tvout [OPTION...]\n"
      "\n"
      "  --ntsc        output NTSC-M signal\n"
      "  --pal         output PAL-B/D/G/H/K/I signal\n"
      "  --off         turn off TV output and re-enable the SLCD\n"
      "  --help        display this help and exit\n");
    return 0;
  }

  map_io();

  ctel_off();
  lcdc_set(tv);
  if (tv != OFF) {
    ctel_on(tv);
  }
  
  close(fbd);
  return 0;
}
