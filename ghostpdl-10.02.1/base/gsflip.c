/* Copyright (C) 2001-2023 Artifex Software, Inc.
   All Rights Reserved.

   This software is provided AS-IS with no warranty, either express or
   implied.

   This software is distributed under license and may not be copied,
   modified or distributed except as expressly authorized under the terms
   of the license contained in the file LICENSE in this distribution.

   Refer to licensing information at http://www.artifex.com or contact
   Artifex Software, Inc.,  39 Mesa Street, Suite 108A, San Francisco,
   CA 94129, USA, for further information.
*/


/* Routines for "flipping" image data */
#include "gx.h"
#include "gserrors.h"		/* for rangecheck in sample macros */
#include "gsbitops.h"
#include "gsbittab.h"
#include "gsflip.h"

#define ARCH_HAS_BYTE_REGS 1

/* Transpose a block of bits between registers. */
#define TRANSPOSE(r,s,mask,shift)\
  r ^= (temp = ((s >> shift) ^ r) & mask);\
  s ^= temp << shift

/* Define the size of byte temporaries.  On Intel CPUs, this should be */
/* byte, but on all other CPUs, it should be uint. */
#if ARCH_HAS_BYTE_REGS
typedef byte byte_var;
#else
typedef uint byte_var;
#endif

#define VTAB(v80,v40,v20,v10,v8,v4,v2,v1)\
  bit_table_8(0,v80,v40,v20,v10,v8,v4,v2,v1)

/* Convert 3Mx1 to 3x1. */
static int
flip3x1(byte * buffer, const byte ** planes, int offset, int nbytes)
{
    byte *out = buffer;
    const byte *in1 = planes[0] + offset;
    const byte *in2 = planes[1] + offset;
    const byte *in3 = planes[2] + offset;
    int n = nbytes;
    static const bits32 tab3x1[256] = {
        VTAB(0x800000, 0x100000, 0x20000, 0x4000, 0x800, 0x100, 0x20, 4)
    };

    for (; n > 0; out += 3, ++in1, ++in2, ++in3, --n) {
        bits32 b24 = tab3x1[*in1] | (tab3x1[*in2] >> 1) | (tab3x1[*in3] >> 2);

        out[0] = (byte) (b24 >> 16);
        out[1] = (byte) (b24 >> 8);
        out[2] = (byte) b24;
    }
    return 0;
}

/* Convert 3Mx2 to 3x2. */
static int
flip3x2(byte * buffer, const byte ** planes, int offset, int nbytes)
{
    byte *out = buffer;
    const byte *in1 = planes[0] + offset;
    const byte *in2 = planes[1] + offset;
    const byte *in3 = planes[2] + offset;
    int n = nbytes;
    static const bits32 tab3x2[256] = {
        VTAB(0x800000, 0x400000, 0x20000, 0x10000, 0x800, 0x400, 0x20, 0x10)
    };

    for (; n > 0; out += 3, ++in1, ++in2, ++in3, --n) {
        bits32 b24 = tab3x2[*in1] | (tab3x2[*in2] >> 2) | (tab3x2[*in3] >> 4);

        out[0] = (byte) (b24 >> 16);
        out[1] = (byte) (b24 >> 8);
        out[2] = (byte) b24;
    }
    return 0;
}

/* Convert 3Mx4 to 3x4. */
static int
flip3x4(byte * buffer, const byte ** planes, int offset, int nbytes)
{
    byte *out = buffer;
    const byte *in1 = planes[0] + offset;
    const byte *in2 = planes[1] + offset;
    const byte *in3 = planes[2] + offset;
    int n = nbytes;

    for (; n > 0; out += 3, ++in1, ++in2, ++in3, --n) {
        byte_var b1 = *in1, b2 = *in2, b3 = *in3;

        out[0] = (b1 & 0xf0) | (b2 >> 4);
        out[1] = (b3 & 0xf0) | (b1 & 0xf);
        out[2] = (byte) (b2 << 4) | (b3 & 0xf);
    }
    return 0;
}

/* Convert 3Mx8 to 3x8. */
static int
flip3x8(byte * buffer, const byte ** planes, int offset, int nbytes)
{
    byte *out = buffer;
    const byte *in1 = planes[0] + offset;
    const byte *in2 = planes[1] + offset;
    const byte *in3 = planes[2] + offset;
    int n = nbytes;

    for (; n > 0; out += 3, ++in1, ++in2, ++in3, --n) {
        out[0] = *in1;
        out[1] = *in2;
        out[2] = *in3;
    }
    return 0;
}

/* Convert 3Mx12 to 3x12. */
static int
flip3x12(byte * buffer, const byte ** planes, int offset, int nbytes)
{
    byte *out = buffer;
    const byte *pa = planes[0] + offset;
    const byte *pb = planes[1] + offset;
    const byte *pc = planes[2] + offset;
    int n = nbytes;

    /*
     * We assume that the input is an integral number of pixels, and
     * round up n to a multiple of 3.
     */
    for (; n > 0; out += 9, pa += 3, pb += 3, pc += 3, n -= 3) {
        byte_var a1 = pa[1], b0 = pb[0], b1 = pb[1], b2 = pb[2], c1 = pc[1];

        out[0] = pa[0];
        out[1] = (a1 & 0xf0) | (b0 >> 4);
        out[2] = (byte) ((b0 << 4) | (b1 >> 4));
        out[3] = pc[0];
        out[4] = (c1 & 0xf0) | (a1 & 0xf);
        out[5] = pa[2];
        out[6] = (byte) ((b1 << 4) | (b2 >> 4));
        out[7] = (byte) ((b2 << 4) | (c1 & 0xf));
        out[8] = pc[2];
    }
    return 0;
}

/* Convert 3Mx16 to 3x16. */
static int
flip3x16(byte * buffer, const byte ** planes, int offset, int nbytes)
{
    byte *out = buffer;
    const byte *pa = planes[0] + offset;
    const byte *pb = planes[1] + offset;
    const byte *pc = planes[2] + offset;
    int n = nbytes;

    for (; n > 0; out += 6, pa += 2, pb += 2, pc += 2, n -= 2) {
        out[0] = pa[0];
        out[1] = pa[1];
        out[2] = pb[0];
        out[3] = pb[1];
        out[4] = pc[0];
        out[5] = pc[1];
    }
    return 0;
}

/* Convert 4Mx1 to 4x1. */
static int
flip4x1(byte * buffer, const byte ** planes, int offset, int nbytes)
{
    byte *out = buffer;
    const byte *in1 = planes[0] + offset;
    const byte *in2 = planes[1] + offset;
    const byte *in3 = planes[2] + offset;
    const byte *in4 = planes[3] + offset;
    int n = nbytes;

    for (; n > 0; out += 4, ++in1, ++in2, ++in3, ++in4, --n) {
        byte_var b1 = *in1, b2 = *in2, b3 = *in3, b4 = *in4;
        byte_var temp;

        /* Transpose blocks of 1 */
        TRANSPOSE(b1, b2, 0x55, 1);
        TRANSPOSE(b3, b4, 0x55, 1);
        /* Transpose blocks of 2 */
        TRANSPOSE(b1, b3, 0x33, 2);
        TRANSPOSE(b2, b4, 0x33, 2);
        /* There's probably a faster way to do this.... */
        out[0] = (b1 & 0xf0) | (b2 >> 4);
        out[1] = (b3 & 0xf0) | (b4 >> 4);
        out[2] = (byte) ((b1 << 4) | (b2 & 0xf));
        out[3] = (byte) ((b3 << 4) | (b4 & 0xf));
    }
    return 0;
}

/* Convert 4Mx2 to 4x2. */
static int
flip4x2(byte * buffer, const byte ** planes, int offset, int nbytes)
{
    byte *out = buffer;
    const byte *in1 = planes[0] + offset;
    const byte *in2 = planes[1] + offset;
    const byte *in3 = planes[2] + offset;
    const byte *in4 = planes[3] + offset;
    int n = nbytes;

    for (; n > 0; out += 4, ++in1, ++in2, ++in3, ++in4, --n) {
        byte_var b1 = *in1, b2 = *in2, b3 = *in3, b4 = *in4;
        byte_var temp;

        /* Transpose blocks of 4x2 */
        TRANSPOSE(b1, b3, 0x0f, 4);
        TRANSPOSE(b2, b4, 0x0f, 4);
        /* Transpose blocks of 2x1 */
        TRANSPOSE(b1, b2, 0x33, 2);
        TRANSPOSE(b3, b4, 0x33, 2);
        out[0] = b1;
        out[1] = b2;
        out[2] = b3;
        out[3] = b4;
    }
    return 0;
}

/* Convert 4Mx4 to 4x4. */
static int
flip4x4(byte * buffer, const byte ** planes, int offset, int nbytes)
{
    byte *out = buffer;
    const byte *in1 = planes[0] + offset;
    const byte *in2 = planes[1] + offset;
    const byte *in3 = planes[2] + offset;
    const byte *in4 = planes[3] + offset;
    int n = nbytes;

    for (; n > 0; out += 4, ++in1, ++in2, ++in3, ++in4, --n) {
        byte_var b1 = *in1, b2 = *in2, b3 = *in3, b4 = *in4;

        out[0] = (b1 & 0xf0) | (b2 >> 4);
        out[1] = (b3 & 0xf0) | (b4 >> 4);
        out[2] = (byte) ((b1 << 4) | (b2 & 0xf));
        out[3] = (byte) ((b3 << 4) | (b4 & 0xf));
    }
    return 0;
}

/* Convert 4Mx8 to 4x8. */
static int
flip4x8(byte * buffer, const byte ** planes, int offset, int nbytes)
{
    byte *out = buffer;
    const byte *in1 = planes[0] + offset;
    const byte *in2 = planes[1] + offset;
    const byte *in3 = planes[2] + offset;
    const byte *in4 = planes[3] + offset;
    int n = nbytes;

    for (; n > 0; out += 4, ++in1, ++in2, ++in3, ++in4, --n) {
        out[0] = *in1;
        out[1] = *in2;
        out[2] = *in3;
        out[3] = *in4;
    }
    return 0;
}

/* Convert 4Mx12 to 4x12. */
static int
flip4x12(byte * buffer, const byte ** planes, int offset, int nbytes)
{
    byte *out = buffer;
    const byte *pa = planes[0] + offset;
    const byte *pb = planes[1] + offset;
    const byte *pc = planes[2] + offset;
    const byte *pd = planes[3] + offset;
    int n = nbytes;

    /*
     * We assume that the input is an integral number of pixels, and
     * round up n to a multiple of 3.
     */
    for (; n > 0; out += 12, pa += 3, pb += 3, pc += 3, pd += 3, n -= 3) {
        byte_var a1 = pa[1], b1 = pb[1], c1 = pc[1], d1 = pd[1];

        {
            byte_var v0;

            out[0] = pa[0];
            v0 = pb[0];
            out[1] = (a1 & 0xf0) | (v0 >> 4);
            out[2] = (byte) ((v0 << 4) | (b1 >> 4));
            out[3] = pc[0];
            v0 = pd[0];
            out[4] = (c1 & 0xf0) | (v0 >> 4);
            out[5] = (byte) ((v0 << 4) | (d1 >> 4));
        }
        {
            byte_var v2;

            v2 = pa[2];
            out[6] = (byte) ((a1 << 4) | (v2 >> 4));
            out[7] = (byte) ((v2 << 4) | (b1 & 0xf));
            out[8] = pb[2];
            v2 = pc[2];
            out[9] = (byte) ((c1 << 4) | (v2 >> 4));
            out[10] = (byte) ((v2 << 4) | (d1 & 0xf));
            out[11] = pd[2];
        }
    }
    return 0;
}

/* Convert 4Mx16 to 4x16. */
static int
flip4x16(byte * buffer, const byte ** planes, int offset, int nbytes)
{
    byte *out = buffer;
    const byte *pa = planes[0] + offset;
    const byte *pb = planes[1] + offset;
    const byte *pc = planes[2] + offset;
    const byte *pd = planes[3] + offset;
    int n = nbytes;

    for (; n > 0; out += 8, pa += 2, pb += 2, pc += 2, pd += 2, n -= 2) {
        out[0] = pa[0];
        out[1] = pa[1];
        out[2] = pb[0];
        out[3] = pb[1];
        out[4] = pc[0];
        out[5] = pc[1];
        out[6] = pd[0];
        out[7] = pd[1];
    }
    return 0;
}

/* Convert NMx{1,2,4,8} to Nx{1,2,4,8}. */
static int
flipNx1to8(byte * buffer, const byte ** planes, int offset, int nbytes,
           int num_planes, int bits_per_sample)
{
    /* This is only needed for DeviceN colors, so it can be slow. */
    uint mask = (1 << bits_per_sample) - 1;
    int bi, pi;
    byte *dptr = buffer;
    int dbit = 0;
    byte dbbyte = 0;

    for (bi = 0; bi < nbytes * 8; bi += bits_per_sample) {
        for (pi = 0; pi < num_planes; ++pi) {
            const byte *sptr = planes[pi] + offset + (bi >> 3);
            uint value = (*sptr >> (8 - (bi & 7) - bits_per_sample)) & mask;

            if (sample_store_next8(value, &dptr, &dbit, bits_per_sample, &dbbyte) < 0)
                return_error(gs_error_rangecheck);
        }
    }
    sample_store_flush(dptr, dbit, dbbyte);
    return 0;
}

/* Convert NMx12 to Nx12. */
static int
flipNx12(byte * buffer, const byte ** planes, int offset, int nbytes,
         int num_planes, int ignore_bits_per_sample)
{
    /* This is only needed for DeviceN colors, so it can be slow. */
    int bi, pi;
    byte *dptr = buffer;
    int dbit = 0;
    byte dbbyte = 0;

    for (bi = 0; bi < nbytes * 8; bi += 12) {
        for (pi = 0; pi < num_planes; ++pi) {
            const byte *sptr = planes[pi] + offset + (bi >> 3);
            uint value =
                (bi & 4 ? ((*sptr & 0xf) << 8) | sptr[1] :
                 (*sptr << 4) | (sptr[1] >> 4));

            sample_store_next_12(value, &dptr, &dbit, &dbbyte);
        }
    }
    sample_store_flush(dptr, dbit, dbbyte);
    return 0;
}

/* Convert NMx16 to Nx16. */
static int
flipNx16(byte * buffer, const byte ** planes, int offset, int nbytes,
         int num_planes, int ignore_bits_per_sample)
{
    /* This is only needed for DeviceN colors, so it can be slow. */
    int bi, pi;
    byte *dptr = buffer;

    for (bi = 0; bi < nbytes; bi += 2) {
        for (pi = 0; pi < num_planes; ++pi) {
            const byte *sptr = planes[pi] + offset + bi;
            dptr[0] = sptr[0];
            dptr[1] = sptr[1];
            dptr += 2;
        }
    }
    return 0;
}

/* Flip data given number of planes and bits per pixel. */
typedef int (*image_flip_proc) (byte *, const byte **, int, int);
static int
flip_fail(byte * buffer, const byte ** planes, int offset, int nbytes)
{
    return -1;
}
static const image_flip_proc image_flip3_procs[17] = {
    flip_fail, flip3x1, flip3x2, flip_fail, flip3x4,
    flip_fail, flip_fail, flip_fail, flip3x8,
    flip_fail, flip_fail, flip_fail, flip3x12,
    flip_fail, flip_fail, flip_fail, flip3x16
};
static const image_flip_proc image_flip4_procs[17] = {
    flip_fail, flip4x1, flip4x2, flip_fail, flip4x4,
    flip_fail, flip_fail, flip_fail, flip4x8,
    flip_fail, flip_fail, flip_fail, flip4x12,
    flip_fail, flip_fail, flip_fail, flip4x16
};
typedef int (*image_flipN_proc) (byte *, const byte **, int, int, int, int);
static int
flipN_fail(byte * buffer, const byte ** planes, int offset, int nbytes,
           int num_planes, int bits_per_sample)
{
    return -1;
}
static const image_flipN_proc image_flipN_procs[17] = {
    flipN_fail, flipNx1to8, flipNx1to8, flipN_fail, flipNx1to8,
    flipN_fail, flipN_fail, flipN_fail, flipNx1to8,
    flipN_fail, flipN_fail, flipN_fail, flipNx12,
    flipN_fail, flipN_fail, flipN_fail, flipNx16
};

/* Here is the public interface to all of the above. */
int
image_flip_planes(byte * buffer, const byte ** planes, int offset, int nbytes,
                  int num_planes, int bits_per_sample)
{
    if (bits_per_sample < 1 || bits_per_sample > 16)
        return -1;
    switch (num_planes) {

    case 3:
        return image_flip3_procs[bits_per_sample]
            (buffer, planes, offset, nbytes);
    case 4:
        return image_flip4_procs[bits_per_sample]
            (buffer, planes, offset, nbytes);
    default:
        if (num_planes < 0)
            return -1;
        return image_flipN_procs[bits_per_sample]
            (buffer, planes, offset, nbytes, num_planes, bits_per_sample);
    }
}
