/*
    jbig2dec
    
    Copyright (c) 2002 artofcode LLC.
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
        
    $Id: jbig2_generic.h,v 1.3 2002/02/19 07:09:16 giles Exp $
*/

/* Table 2 */
typedef struct {
  bool MMR;
  int32_t GBW;
  int32_t GBH;
  int GBTEMPLATE;
  bool TPGDON;
  bool USESKIP;
  /* SKIP */
  byte gbat[8];
} Jbig2GenericRegionParams;

/* 6.2 */
int
jbig2_decode_generic_region(Jbig2Ctx *ctx,
			    int32_t seg_number,
			    const Jbig2GenericRegionParams *params,
			    Jbig2ArithState *as,
			    byte *gbreg,
			    Jbig2ArithCx *GB_stats);

/* 7.4 */
int
jbig2_immediate_generic_region(Jbig2Ctx *ctx, Jbig2SegmentHeader *sh,
			       const uint8_t *segment_data);
