// Copyright 2011-2020 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#include "byte_source.h"

#include "memory.h"
#include "system.h"

#include "serd/node.h"
#include "serd/string_view.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

SerdStatus
serd_byte_source_page(SerdByteSource* const source)
{
  uint8_t* const buf =
    (source->block_size > 1 ? source->block : &source->read_byte);

  const size_t n_read =
    source->in->read(buf, 1, source->block_size, source->in->stream);

  source->buf_size  = n_read;
  source->read_head = 0;
  source->eof       = false;

  if (n_read < source->block_size) {
    buf[n_read] = '\0';
    if (n_read == 0) {
      source->eof = true;
      return (source->in->error(source->in->stream) ? SERD_BAD_READ
                                                    : SERD_FAILURE);
    }
  }

  return SERD_SUCCESS;
}

static void
serd_byte_source_init_buffer(SerdAllocator* const  allocator,
                             SerdByteSource* const source)
{
  if (source->block_size > 1) {
    source->block = (uint8_t*)serd_aaligned_alloc(
      allocator, SERD_PAGE_SIZE, source->block_size);

    if ((source->read_buf = source->block)) {
      memset(source->block, '\0', source->block_size);
    }
  } else {
    source->read_buf = &source->read_byte;
  }
}

SerdByteSource*
serd_byte_source_new_input(SerdAllocator* const   allocator,
                           SerdInputStream* const input,
                           const SerdNode* const  name,
                           const size_t           block_size)
{
  assert(input);
  assert(block_size);
  assert(input->stream);

  SerdNode* const source_name =
    name ? serd_node_copy(allocator, name)
         : serd_new_string(allocator, serd_string("input"));

  if (!source_name) {
    return NULL;
  }

  SerdByteSource* source =
    (SerdByteSource*)serd_acalloc(allocator, 1, sizeof(SerdByteSource));

  if (!source) {
    serd_node_free(allocator, source_name);
    return NULL;
  }

  source->name           = source_name;
  source->in             = input;
  source->block_size     = block_size;
  source->buf_size       = block_size;
  source->caret.document = source->name;
  source->caret.line     = 1U;
  source->caret.col      = 1U;

  serd_byte_source_init_buffer(allocator, source);
  if (block_size > 1 && !source->block) {
    serd_node_free(allocator, source_name);
    serd_afree(allocator, source);
    return NULL;
  }

  return source;
}

void
serd_byte_source_free(SerdAllocator* const  allocator,
                      SerdByteSource* const source)
{
  if (source) {
    if (source->block_size > 1) {
      serd_aaligned_free(allocator, source->block);
    }

    serd_node_free(allocator, source->name);
    serd_afree(allocator, source);
  }
}

SerdStatus
serd_byte_source_prepare(SerdByteSource* const source)
{
  source->prepared = true;

  if (source->block_size > 1) {
    return serd_byte_source_page(source);
  }

  return serd_byte_source_advance(source);
}
