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
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#define FBIOA320TVOUT	0x46F0

/* The non-negative values match the FBIOA320TVOUT argument values. */
enum TVStandard {
	NONE = -1, OFF = 0, NTSC = 1, PAL_50 = 2, PAL_60 = 3, PAL_M = 4,
};

static int fbd;
static int i2cfd;

static int i2c_open(void)
{
  i2cfd = open("/dev/i2c-0", O_RDWR);
  if (!i2cfd) {
    perror("Unable to open i2c dev file");
    return -1;
  }

  if (ioctl(i2cfd, I2C_SLAVE, 0x76) < 0) {
    perror("Unable to set slave address");
    return -2;
  }

  return 0;
}

static int i2c_close(void)
{
  if (close(i2cfd) < 0) {
    perror("Unable to close i2c dev file");
    return -1;
  }
  return 0;
}

/* set a register on the Chrontel TV encoder */
static int i2c(int addr, int val)
{
  union i2c_smbus_data data;
  struct i2c_smbus_ioctl_data args;

  data.byte = val;
  args.data = &data;
  args.read_write = I2C_SMBUS_WRITE;
  args.size = I2C_SMBUS_BYTE_DATA;
  args.command = addr;

  if (ioctl(i2cfd, I2C_SMBUS, &args) < 0) {
    perror("Unable to write byte");
    return -1;
  }

  return 0;
}

/* Enable the TV encoder.
   Set pal to non-zero for PAL encoding; default is NTSC.
   These are for the most part the values the native Dingoo firmware
   programs the TV encoder with.  Some of them sound patently absurd, but
   still work.
 */
void ctel_on(enum TVStandard tv)
{
  if (tv == OFF)
    return;

  i2c_open();

  i2c(3, 0x00); /* Reset everything; enters power-down state */
  i2c(3, 0x03); /* Finish reset */

  if (tv == PAL_50) {
    i2c(0xa, 0x13);     /* Video Output Format: TV_BP = 0 (scaler on),
                           SVIDEO = 0 (composite output), DACSW = 01 (CVBS),
                           VOS = 0011 (PAL- B/D/G/H/K/I) */
  } else if (tv == PAL_60) {
    i2c(0xa, 0x17); /* same as above, except VOS = 0111 (PAL-60) */
  } else if (tv == PAL_M) {
    i2c(0xa, 0x14); /* same as above, except VOS = 0100 (PAL-M) */
  } else {
    i2c(0xa, 0x10); /* same as above, except VOS = 0000 (NTSC-M) */
  }

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
  if (tv == PAL_50) {
    /* Input Timing: HVAUTO = 0 (timing from HTI, HAI),
       HTI (input horizontal total pixels) = 0x36c (876),
       HAI (input horizontal active pixels) = 0x140 (320)
     */
    i2c(0x11, 0x19);
    /* Input Timing Register 2 (0x12) defaults to 0x40 */
    i2c(0x13, 0x6c);
  } else {
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
     VTI (input vertical total pixels) = 548 (PAL), 544 (NTSC/PAL-60),
     VAI (input vertical active pixels) = 240
   */
  i2c(0x17, 4);
  /* Input Timing Register 8 (0x18) defaults to 0xf0 */
  if (tv == PAL_50) {
    i2c(0x19, 0x12);
  } else {
    i2c(0x19, 0x10);
  }
  /* Input Timing Register 10 (0x1a) defaults to 4 */

  /* TVHA (TV output horizontal active pixels) = 1345 (WTF??) */
  /* Output Timing Register 1 (0x1e) defaults to 5 */
  i2c(0x1f, 0x41);

  /* VP (vertical position) = 512, i.e. no adjustment;
     PCLK clock divider remains at default value (67108864).
   */
  if (tv == PAL_50) {
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
  } else {
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

  i2c(0x63, 0xc2); /* SEL_R = 1 (double termination) */

  i2c(4, 0x08);	/* enable DAC0, power up */
  i2c_close();
}

void ctel_off(void)
{
  i2c_open();
  i2c(4, 0xc1);	/* disable DACs, power down */
  i2c_close();
}

int lcdc_set(enum TVStandard tv)
{
  int ret = ioctl(fbd, FBIOA320TVOUT, tv);
  if (ret < 0) perror("Failed to select TV-out mode");
  return ret;
}

int main(int argc, char **argv)
{
  enum TVStandard tv = NONE;
  for (; argc > 1; argc--, argv++) {
    if (!strcmp(argv[1], "--pal")) tv = PAL_50;
    else if (!strcmp(argv[1], "--pal-m")) tv = PAL_M;
    else if (!strcmp(argv[1], "--pal-60")) tv = PAL_60;
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
      "  --pal-m       output PAL-M signal\n"
      "  --pal-60      output PAL-encoded signal at 60 Hz\n"
      "  --off         turn off TV output and re-enable the SLCD\n"
      "  --help        display this help and exit\n");
    return 0;
  }

  fbd = open("/dev/fb0", O_RDWR);

  ctel_off();
  if (lcdc_set(tv) >= 0) {
    ctel_on(tv);
  }

  close(fbd);
  return 0;
}
