/*
    jbig2dec
    
    Copyright (c) 2002 artofcode LLC.
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
        
    $Id: jbig2.c,v 1.6 2002/06/15 14:12:50 giles Exp $
*/

#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>

#include "jbig2.h"
#include "jbig2_priv.h"
#include "jbig2_arith.h"
#include "jbig2_generic.h"
#include "jbig2_symbol_dict.h"

static void *
jbig2_default_alloc (Jbig2Allocator *allocator, size_t size)
{
  return malloc (size);
}

static void
jbig2_default_free (Jbig2Allocator *allocator, void *p)
{
  free (p);
}

static void *
jbig2_default_realloc (Jbig2Allocator *allocator, void *p, size_t size)
{
  return realloc (p, size);
}

static Jbig2Allocator jbig2_default_allocator =
{
  jbig2_default_alloc,
  jbig2_default_free,
  jbig2_default_realloc
};

void *
jbig2_alloc (Jbig2Allocator *allocator, size_t size)
{
  return allocator->alloc (allocator, size);
}

void
jbig2_free (Jbig2Allocator *allocator, void *p)
{
  allocator->free (allocator, p);
}

void *
jbig2_realloc (Jbig2Allocator *allocator, void *p, size_t size)
{
  return allocator->realloc (allocator, p, size);
}

int
jbig2_error (Jbig2Ctx *ctx, Jbig2Severity severity, int seg_idx,
	     const char *fmt, ...)
{
  char buf[1024];
  va_list ap;
  int n;
  int code;

  va_start (ap, fmt);
  n = vsnprintf (buf, sizeof(buf), fmt, ap);
  va_end (ap);
  if (n < 0 || n == sizeof(buf))
    strcpy (buf, "jbig2_error: error in generating error string");
  code = ctx->error_callback (ctx->error_callback_data, buf, severity, seg_idx);
  if (severity == JBIG2_SEVERITY_FATAL)
    code = -1;
  return code;
}

Jbig2Ctx *
jbig2_ctx_new (Jbig2Allocator *allocator,
	       Jbig2Options options,
	       Jbig2GlobalCtx *global_ctx,
	       Jbig2ErrorCallback error_callback,
	       void *error_callback_data)
{
  Jbig2Ctx *result;

  if (allocator == NULL)
      allocator = &jbig2_default_allocator;

  result = (Jbig2Ctx *)jbig2_alloc(allocator, sizeof(Jbig2Ctx));
  result->allocator = allocator;
  result->options = options;
  result->global_ctx = (const Jbig2Ctx *)global_ctx;
  result->error_callback = error_callback;
  result->error_callback_data = error_callback_data;

  result->state = (options & JBIG2_OPTIONS_EMBEDDED) ?
    JBIG2_FILE_SEQUENTIAL_HEADER :
    JBIG2_FILE_HEADER;

  result->buf = NULL;
  result->sh_list = NULL;
  result->n_sh = 0;
  result->n_sh_max = 1;
  result->sh_ix = 0;

  result->n_results = 0;
  result->n_results_max = 16;
  result->results = (const Jbig2Result **)jbig2_alloc(allocator, result->n_results_max * sizeof(Jbig2Result *));

  return result;
}

int32_t
jbig2_get_int32 (const byte *buf)
{
  return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}

int16_t
jbig2_get_int16 (const byte *buf)
{
  return (buf[0] << 8) | buf[1];
}

static Jbig2SegmentHeader *
jbig2_parse_segment_header (Jbig2Ctx *ctx, uint8_t *buf, size_t buf_size,
			    size_t *p_header_size)
{
  Jbig2SegmentHeader *result;
  byte	rtscarf;
  int32_t rtscarf_long;
  int referred_to_segment_count;
  int referred_to_segment_size;
  int pa_size;
  int offset;

  /* minimum possible size of a jbig2 segment header */
  if (buf_size < 11)
    return NULL;

  result = (Jbig2SegmentHeader *)jbig2_alloc(ctx->allocator,
					     sizeof(Jbig2SegmentHeader));

  /* 7.2.2 */
  result->segment_number = jbig2_get_int32 (buf);

  /* 7.2.3 */
  result->flags = buf[4];

  /* 7.2.4 */
  rtscarf = buf[5];
  if ((rtscarf & 0xe0) == 0xe0)
    {
      rtscarf_long = jbig2_get_int32(buf + 5);
      referred_to_segment_count = rtscarf_long & 0x1fffffff;
      offset = 5 + 4 + (referred_to_segment_count + 1) / 8;
    }
  else
    {
      referred_to_segment_count = (rtscarf >> 5);
      offset = 5 + 1;
    }
  result->referred_to_segment_count = referred_to_segment_count;

  /* 7.2.5 */
  /* todo: read referred to segment numbers */
  /* For now, we skip them. */
  referred_to_segment_size = result->segment_number <= 256 ? 1:
    result->segment_number <= 65536 ? 2:
    4;
  offset += referred_to_segment_count * referred_to_segment_size;

  /* 7.2.6 */
  pa_size = result->flags & 0x40 ? 4 : 1;

  if (offset + pa_size + 4 > buf_size)
    {
      jbig2_free (ctx->allocator, result);
      return NULL;
    }

  if (result->flags & 0x40) {
	result->page_association = jbig2_get_int32(buf + offset);
	offset += 4;
  } else {
	result->page_association = buf[offset++];
  }
  
  /* 7.2.7 */
  result->data_length = jbig2_get_int32 (buf + offset);
  *p_header_size = offset + 4;

  return result;
}

void
jbig2_free_segment_header (Jbig2Ctx *ctx, Jbig2SegmentHeader *sh)
{
  jbig2_free (ctx->allocator, sh);
}

int
jbig2_write (Jbig2Ctx *ctx, const unsigned char *data, size_t size)
{
  const int initial_buf_size = 1024;

  if (ctx->buf == NULL)
    {
      int buf_size = initial_buf_size;

      do
	buf_size <<= 1;
      while (buf_size < size);
      ctx->buf = (byte *)jbig2_alloc (ctx->allocator, size);
      ctx->buf_size = buf_size;
      ctx->buf_rd_ix = 0;
      ctx->buf_wr_ix = 0;
    }
  else if (ctx->buf_wr_ix + size > ctx->buf_size)
    {
      if (ctx->buf_rd_ix <= (ctx->buf_size >> 1) &&
	  ctx->buf_wr_ix - ctx->buf_rd_ix + size <= ctx->buf_size)
	{
	  memcpy (ctx->buf, ctx->buf + ctx->buf_rd_ix,
		  ctx->buf_wr_ix - ctx->buf_rd_ix);
	}
      else
	{
	  byte *buf;
	  int buf_size = initial_buf_size;
	  
	  do
	    buf_size <<= 1;
	  while (buf_size < ctx->buf_wr_ix - ctx->buf_rd_ix + size);
	  buf = (byte *)jbig2_alloc (ctx->allocator, buf_size);
	  memcpy (buf, ctx->buf + ctx->buf_rd_ix,
		  ctx->buf_wr_ix - ctx->buf_rd_ix);
	  jbig2_free (ctx->allocator, ctx->buf);
	  ctx->buf = buf;
	  ctx->buf_size = buf_size;
	}
      ctx->buf_wr_ix -= ctx->buf_rd_ix;
      ctx->buf_rd_ix = 0;
    }
  memcpy (ctx->buf + ctx->buf_wr_ix, data, size);
  ctx->buf_wr_ix += size;

  /* data has now been added to buffer */

  for (;;)
    {
      const byte jbig2_id_string[8] = { 0x97, 0x4a, 0x42, 0x32, 0x0d, 0x0a, 0x1a, 0x0a };
      Jbig2SegmentHeader *sh;
      size_t header_size;
      int code;

      switch (ctx->state)
	{
	case JBIG2_FILE_HEADER:
	  if (ctx->buf_wr_ix - ctx->buf_rd_ix < 9)
	    return 0;
	  if (memcmp(ctx->buf + ctx->buf_rd_ix, jbig2_id_string, 8))
	    return jbig2_error(ctx, JBIG2_SEVERITY_FATAL, -1,
			       "Not a JBIG2 file header");
	  ctx->file_header_flags = ctx->buf[ctx->buf_rd_ix + 8];
	  if (!(ctx->file_header_flags & 2))
	    {
	      if (ctx->buf_wr_ix - ctx->buf_rd_ix < 13)
		return 0;
	      ctx->n_pages = jbig2_get_int32(ctx->buf + ctx->buf_rd_ix + 9);
	      ctx->buf_rd_ix += 13;
	    }
	  else
	    ctx->buf_rd_ix += 9;
	  if (ctx->file_header_flags & 1)
	    {
	      ctx->state = JBIG2_FILE_SEQUENTIAL_HEADER;
	    }
	  else
	    {
	      ctx->state = JBIG2_FILE_RANDOM_HEADERS;
	      ctx->n_sh_max = 16;
	    }
	  break;
	case JBIG2_FILE_SEQUENTIAL_HEADER:
	case JBIG2_FILE_RANDOM_HEADERS:
	  sh = jbig2_parse_segment_header(ctx, ctx->buf + ctx->buf_rd_ix,
					  ctx->buf_wr_ix - ctx->buf_rd_ix,
					  &header_size);
	  if (sh == NULL)
	    return 0;
	  ctx->buf_rd_ix += header_size;

	  if (ctx->sh_list == NULL)
	      ctx->sh_list = jbig2_alloc(ctx->allocator, ctx->n_sh_max *
				     sizeof(Jbig2SegmentHeader *));
	  else if (ctx->n_sh == ctx->n_sh_max)
	    /* Note to rillian: I usually define a macro to make this
	       less ungainly. */
	    ctx->sh_list = (Jbig2SegmentHeader **)jbig2_realloc(ctx->allocator,
								ctx->sh_list,
								(ctx->n_sh_max <<= 2) * sizeof(Jbig2SegmentHeader *));

	  ctx->sh_list[ctx->n_sh++] = sh;
	  if (ctx->state == JBIG2_FILE_RANDOM_HEADERS)
	    {
	      if ((sh->flags & 63) == 51) /* end of file */
		ctx->state = JBIG2_FILE_RANDOM_BODIES;
	    }
	  else
	    ctx->state = JBIG2_FILE_SEQUENTIAL_BODY;
	  break;
	case JBIG2_FILE_SEQUENTIAL_BODY:
	case JBIG2_FILE_RANDOM_BODIES:
	  sh = ctx->sh_list[ctx->sh_ix];
	  if (sh->data_length > ctx->buf_wr_ix - ctx->buf_rd_ix)
	    return 0;
	  code = jbig2_write_segment(ctx, sh, ctx->buf + ctx->buf_rd_ix);
	  ctx->buf_rd_ix += sh->data_length;
	  jbig2_free_segment_header(ctx, sh);
	  ctx->sh_list[ctx->sh_ix] = NULL;
	  if (ctx->state == JBIG2_FILE_RANDOM_BODIES)
	    {
	      ctx->sh_ix++;
	      if (ctx->sh_ix == ctx->n_sh)
		ctx->state = JBIG2_FILE_EOF;
	    }
	  else
	    {
	      ctx->n_sh = 0;
	      ctx->state = JBIG2_FILE_SEQUENTIAL_HEADER;
	    }
	  if (code < 0)
	    {
	      ctx->state = JBIG2_FILE_EOF;
	      return code;
	    }
	  break;
	case JBIG2_FILE_EOF:
	  if (ctx->buf_rd_ix == ctx->buf_wr_ix)
	    return 0;
	  return jbig2_error(ctx, JBIG2_SEVERITY_WARNING, -1,
		      "Garbage beyond end of file");
	}
    }
  return 0;
}

/* get_bits */

void
jbig2_ctx_free (Jbig2Ctx *ctx)
{
  Jbig2Allocator *ca = ctx->allocator;
  int i;
  int32_t seg_ix;

  jbig2_free(ca, ctx->buf);
  if (ctx->sh_list != NULL)
    {
      for (i = ctx->sh_ix; i < ctx->n_sh; i++)
	jbig2_free_segment_header(ctx, ctx->sh_list[i]);
      jbig2_free(ca, ctx->sh_list);
    }

  for (seg_ix = 0; seg_ix < ctx->n_results; seg_ix++)
    {
      const Jbig2Result *result = ctx->results[seg_ix];

      if (result)
	result->free(result, ctx);
    }
  jbig2_free(ca, ctx->results);

  jbig2_free(ca, ctx);
}

Jbig2GlobalCtx *jbig2_make_global_ctx (Jbig2Ctx *ctx)
{
  return (Jbig2GlobalCtx *)ctx;
}

void jbig2_global_ctx_free(Jbig2GlobalCtx *global_ctx)
{
  jbig2_ctx_free((Jbig2Ctx *)global_ctx);
}


const Jbig2Result *
jbig2_get_result(Jbig2Ctx *ctx, int32_t segment_number)
{
  if (segment_number < 0 || segment_number >= ctx->n_results)
    {
      jbig2_error(ctx, JBIG2_SEVERITY_WARNING, segment_number,
		  "Attempting to get invalid segment number");
      return NULL;
    }
  return ctx->results[segment_number];
}

int
jbig2_put_result(Jbig2Ctx *ctx, const Jbig2Result *result)
{
  int32_t segment_number = result->segment_number;
  int32_t i;

  if (ctx->n_results_max <= segment_number)
    {
      const Jbig2Result **new_results;
      do
	ctx->n_results_max <<= 1;
      while (ctx->n_results_max <= segment_number);
      new_results = (const Jbig2Result **)jbig2_realloc(ctx->allocator,
							  ctx->results,
							  ctx->n_results_max * sizeof(Jbig2Result *));
      if (new_results == NULL)
	return jbig2_error(ctx, JBIG2_SEVERITY_FATAL, segment_number,
			   "Allocation failure");
      ctx->results = new_results;
    }
  for (i = ctx->n_results; i < segment_number; i++)
    ctx->results[i] = NULL;
  ctx->results[segment_number] = result;
  if (ctx->n_results < segment_number + 1)
    ctx->n_results = segment_number + 1;
  return 0;
}

typedef struct {
  Jbig2WordStream super;
  const byte *data;
  size_t size;
} Jbig2WordStreamBuf;

/* I'm not committed to keeping the word stream interface. It's handy
   when you think you may be streaming your input, but if you're not
   (as is currently the case), it just adds complexity.
*/

static uint32_t
jbig2_word_stream_buf_get_next_word(Jbig2WordStream *self, int offset)
{
  Jbig2WordStreamBuf *z = (Jbig2WordStreamBuf *)self;
  const byte *data = z->data;
  uint32_t result;

  if (offset + 4 < z->size)
    result = (data[offset] << 24) | (data[offset + 1] << 16) |
      (data[offset + 2] << 8) | data[offset + 3];
  else
    {
      int i;

      result = 0;
      for (i = 0; i < z->size - offset; i++)
	result |= data[offset + i] << ((3 - i) << 3);
    }
  return result;
}

Jbig2WordStream *
jbig2_word_stream_buf_new(Jbig2Ctx *ctx, const byte *data, size_t size)
{
  Jbig2WordStreamBuf *result = (Jbig2WordStreamBuf *)jbig2_alloc(ctx->allocator, sizeof(Jbig2WordStreamBuf));

  result->super.get_next_word = jbig2_word_stream_buf_get_next_word;
  result->data = data;
  result->size = size;

  return &result->super;
}

void
jbig2_word_stream_buf_free(Jbig2Ctx *ctx, Jbig2WordStream *ws)
{
  jbig2_free(ctx->allocator, ws);
}
