// Copyright 2011-2021 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#include "block_dumper.h"
#include "system.h"

#include <stddef.h>

SerdStatus
serd_block_dumper_open(SerdBlockDumper* const  dumper,
                       SerdOutputStream* const output,
                       const size_t            block_size)
{
  if (!block_size) {
    return SERD_ERR_BAD_ARG;
  }

  dumper->out        = output;
  dumper->buf        = NULL;
  dumper->size       = 0U;
  dumper->block_size = block_size;

  if (block_size == 1) {
    return SERD_SUCCESS;
  }

  dumper->buf = (char*)serd_allocate_buffer(block_size);
  return dumper->buf ? SERD_SUCCESS : SERD_ERR_INTERNAL;
}

void
serd_block_dumper_flush(SerdBlockDumper* const dumper)
{
  if (dumper->out->stream && dumper->block_size > 1 && dumper->size > 0) {
    dumper->out->write(dumper->buf, 1, dumper->size, dumper->out->stream);
    dumper->size = 0;
  }
}

void
serd_block_dumper_close(SerdBlockDumper* const dumper)
{
  serd_free_aligned(dumper->buf);
}
