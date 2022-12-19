// Copyright 2011-2020 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#ifndef SERD_SRC_STACK_H
#define SERD_SRC_STACK_H

#include "memory.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/** An offset to start the stack at. Note 0 is reserved for NULL. */
#define SERD_STACK_BOTTOM sizeof(void*)

/** A dynamic stack in memory. */
typedef struct {
  char*  buf;      ///< Stack memory
  size_t buf_size; ///< Allocated size of buf (>= size)
  size_t size;     ///< Conceptual size of stack in buf
} SerdStack;

/** An offset to start the stack at. Note 0 is reserved for NULL. */
#define SERD_STACK_BOTTOM sizeof(void*)

static inline SerdStack
serd_stack_new(ZixAllocator* const allocator, size_t size, size_t align)
{
  const size_t aligned_size = (size + (align - 1)) / align * align;

  SerdStack stack;
  stack.buf      = (char*)zix_aligned_alloc(allocator, align, aligned_size);
  stack.buf_size = size;
  stack.size     = SERD_STACK_BOTTOM;

  if (stack.buf) {
    memset(stack.buf, 0, size);
  }

  return stack;
}

static inline void
serd_stack_clear(SerdStack* stack)
{
  stack->size = SERD_STACK_BOTTOM;
}

static inline bool
serd_stack_is_empty(const SerdStack* stack)
{
  return stack->size <= SERD_STACK_BOTTOM;
}

static inline void
serd_stack_free(ZixAllocator* const allocator, SerdStack* stack)
{
  zix_aligned_free(allocator, stack->buf);
  stack->buf      = NULL;
  stack->buf_size = 0;
  stack->size     = 0;
}

static inline void*
serd_stack_push(SerdStack* stack, size_t n_bytes)
{
  const size_t new_size = stack->size + n_bytes;
  if (stack->buf_size < new_size) {
    return NULL;
  }

  char* const ret = (stack->buf + stack->size);
  stack->size     = new_size;
  return ret;
}

static inline void
serd_stack_pop(SerdStack* stack, size_t n_bytes)
{
  assert(stack->size >= n_bytes);
  stack->size -= n_bytes;
}

static inline void
serd_stack_pop_to(SerdStack* stack, size_t n_bytes)
{
  assert(stack->size >= n_bytes);
  memset(stack->buf + n_bytes, 0, stack->size - n_bytes);
  stack->size = n_bytes;
}

static inline void*
serd_stack_push_aligned(SerdStack* stack, size_t n_bytes, size_t align)
{
  // Push padding if necessary
  const size_t pad = align - stack->size % align;
  if (pad > 0) {
    void* padding = serd_stack_push(stack, pad);
    if (!padding) {
      return NULL;
    }
    memset(padding, 0, pad);
  }

  // Push requested space at aligned location
  return serd_stack_push(stack, n_bytes);
}

#endif // SERD_SRC_STACK_H
