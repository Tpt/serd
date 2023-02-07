// Copyright 2011-2020 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#include "reader.h"

#include "serd/input_stream.h"
#include "serd/log.h"

#include "byte_source.h"
#include "memory.h"
#include "namespaces.h"
#include "node.h"
#include "read_nquads.h"
#include "read_ntriples.h"
#include "stack.h"
#include "statement.h"
#include "world.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static SerdStatus
serd_reader_prepare(SerdReader* reader);

SerdStatus
r_err(SerdReader* const reader, const SerdStatus st, const char* const fmt, ...)
{
  va_list args; // NOLINT(cppcoreguidelines-init-variables)
  va_start(args, fmt);

  serd_vlogf_at(
    reader->world, SERD_LOG_LEVEL_ERROR, &reader->source->caret, fmt, args);

  va_end(args);
  return st;
}

SerdStatus
skip_horizontal_whitespace(SerdReader* const reader)
{
  while (peek_byte(reader) == '\t' || peek_byte(reader) == ' ') {
    eat_byte(reader);
  }

  return SERD_SUCCESS;
}

SerdStatus
serd_reader_skip_until_byte(SerdReader* const reader, const uint8_t byte)
{
  int c = peek_byte(reader);

  while (c != byte && c != EOF) {
    skip_byte(reader, c);
    c = peek_byte(reader);
  }

  return c == EOF ? SERD_FAILURE : SERD_SUCCESS;
}

void
set_blank_id(SerdReader* const reader,
             SerdNode* const   node,
             const size_t      buf_size)
{
  char* const buf = (char*)(node + 1);

  node->length = (size_t)snprintf(
    buf, buf_size, "%sb%u", reader->bprefix, reader->next_id++);
}

size_t
genid_length(const SerdReader* const reader)
{
  return reader->bprefix_len + 10; // + "b" + UINT32_MAX
}

bool
tolerate_status(const SerdReader* const reader, const SerdStatus status)
{
  if (status == SERD_SUCCESS || status == SERD_FAILURE) {
    return true;
  }

  if (status == SERD_BAD_STACK || status == SERD_BAD_WRITE ||
      status == SERD_NO_DATA || status == SERD_BAD_CALL ||
      status == SERD_BAD_CURSOR) {
    return false;
  }

  return !reader->strict;
}

SerdNode*
blank_id(SerdReader* const reader)
{
  SerdNode* const ref =
    push_node_padded(reader, genid_length(reader), SERD_BLANK, "", 0);

  if (ref) {
    set_blank_id(reader, ref, genid_length(reader) + 1);
  }

  return ref;
}

SerdNode*
push_node_padded(SerdReader* const  reader,
                 const size_t       max_length,
                 const SerdNodeType type,
                 const char* const  str,
                 const size_t       length)
{
  // Push a null byte to ensure the previous node was null terminated
  char* terminator = (char*)serd_stack_push(&reader->stack, 1);
  if (!terminator) {
    return NULL;
  }
  *terminator = 0;

  void* mem = serd_stack_push_aligned(
    &reader->stack, sizeof(SerdNode) + max_length + 1, sizeof(SerdNode));

  if (!mem) {
    return NULL;
  }

  SerdNode* const node = (SerdNode*)mem;

  node->length = length;
  node->flags  = 0;
  node->type   = type;

  char* buf = (char*)(node + 1);
  memcpy(buf, str, length + 1);

  return node;
}

SerdNode*
push_node(SerdReader* const  reader,
          const SerdNodeType type,
          const char* const  str,
          const size_t       length)
{
  return push_node_padded(reader, length, type, str, length);
}

SerdStatus
emit_statement_at(SerdReader* const reader,
                  const ReadContext ctx,
                  SerdNode* const   o,
                  SerdCaret* const  caret)
{
  if (reader->stack.size + (2 * sizeof(SerdNode)) > reader->stack.buf_size) {
    return SERD_BAD_STACK;
  }

  /* Zero the pad of the object node on the top of the stack.  Lower nodes
     (subject and predicate) were already zeroed by subsequent pushes. */
  serd_node_zero_pad(o);

  const SerdStatement statement = {{ctx.subject, ctx.predicate, o, ctx.graph},
                                   caret};

  const SerdStatus st =
    serd_sink_write_statement(reader->sink, *ctx.flags, &statement);

  *ctx.flags = 0;
  return st;
}

SerdStatus
emit_statement(SerdReader* const reader,
               const ReadContext ctx,
               SerdNode* const   o)
{
  return emit_statement_at(reader, ctx, o, &reader->source->caret);
}

static SerdStatus
read_statement(SerdReader* const reader)
{
  return read_n3_statement(reader);
}

SerdStatus
serd_reader_read_document(SerdReader* const reader)
{
  assert(reader);

  if (!reader->source) {
    return SERD_BAD_CALL;
  }

  if (!(reader->flags & SERD_READ_GLOBAL)) {
    reader->bprefix_len = (size_t)snprintf(reader->bprefix,
                                           sizeof(reader->bprefix),
                                           "f%u",
                                           ++reader->world->next_document_id);
  }

  if (reader->syntax != SERD_SYNTAX_EMPTY && !reader->source->prepared) {
    SerdStatus st = serd_reader_prepare(reader);
    if (st) {
      return st;
    }
  }

  switch (reader->syntax) {
  case SERD_SYNTAX_EMPTY:
    break;
  case SERD_TURTLE:
    return read_turtleTrigDoc(reader);
  case SERD_NTRIPLES:
    return read_ntriplesDoc(reader);
  case SERD_NQUADS:
    return read_nquadsDoc(reader);
  case SERD_TRIG:
    return read_turtleTrigDoc(reader);
  }

  return SERD_SUCCESS;
}

SerdReader*
serd_reader_new(SerdWorld* const      world,
                const SerdSyntax      syntax,
                const SerdReaderFlags flags,
                SerdEnv* const        env,
                const SerdSink* const sink,
                const size_t          stack_size)
{
  assert(world);
  assert(env);
  assert(sink);

  if (stack_size < 3 * sizeof(SerdNode) + 192 + serd_node_align) {
    return NULL;
  }

  SerdReader* me = (SerdReader*)serd_wcalloc(world, 1, sizeof(SerdReader));
  if (!me) {
    return NULL;
  }

  me->world   = world;
  me->sink    = sink;
  me->env     = env;
  me->stack   = serd_stack_new(world->allocator, stack_size, serd_node_align);
  me->syntax  = syntax;
  me->flags   = flags;
  me->next_id = 1;
  me->strict  = !(flags & SERD_READ_LAX);

  if (!me->stack.buf) {
    serd_wfree(world, me);
    return NULL;
  }

  // Reserve a bit of space at the end of the stack to zero pad nodes
  me->stack.buf_size -= serd_node_align;

  me->rdf_first = push_node(me, SERD_URI, NS_RDF "first", 48);
  me->rdf_rest  = push_node(me, SERD_URI, NS_RDF "rest", 47);
  me->rdf_nil   = push_node(me, SERD_URI, NS_RDF "nil", 46);
  me->rdf_type  = push_node(me, SERD_URI, NS_RDF "type", 47);

  // The initial stack size check should cover this
  assert(me->rdf_first);
  assert(me->rdf_rest);
  assert(me->rdf_nil);
  assert(me->rdf_type);

  if (!(flags & SERD_READ_GLOBAL)) {
    me->bprefix[0]  = 'f';
    me->bprefix[1]  = '0';
    me->bprefix_len = 2;
  }

  return me;
}

void
serd_reader_free(SerdReader* const reader)
{
  if (!reader) {
    return;
  }

  serd_reader_finish(reader);

  serd_aaligned_free(reader->world->allocator, reader->stack.buf);
  serd_wfree(reader->world, reader);
}

static SerdStatus
skip_bom(SerdReader* const me)
{
  if (serd_byte_source_peek(me->source) == 0xEF) {
    if (serd_byte_source_advance(me->source) ||
        serd_byte_source_peek(me->source) != 0xBB ||
        serd_byte_source_advance(me->source) ||
        serd_byte_source_peek(me->source) != 0xBF ||
        serd_byte_source_advance(me->source)) {
      r_err(me, SERD_BAD_SYNTAX, "corrupt byte order mark");
      return SERD_BAD_SYNTAX;
    }
  }

  return SERD_SUCCESS;
}

SerdStatus
serd_reader_start(SerdReader* const      reader,
                  SerdInputStream* const input,
                  const SerdNode* const  input_name,
                  const size_t           block_size)
{
  assert(reader);
  assert(input);

  if (!block_size || !input->stream) {
    return SERD_BAD_ARG;
  }

  serd_reader_finish(reader);

  assert(!reader->source);
  reader->source = serd_byte_source_new_input(
    reader->world->allocator, input, input_name, block_size);

  return reader->source ? SERD_SUCCESS : SERD_BAD_ALLOC;
}

static SerdStatus
serd_reader_prepare(SerdReader* const reader)
{
  SerdStatus st = serd_byte_source_prepare(reader->source);
  if (st == SERD_SUCCESS) {
    st = skip_bom(reader);
  } else if (st == SERD_FAILURE) {
    reader->source->eof = true;
  }
  return st;
}

SerdStatus
serd_reader_read_chunk(SerdReader* const reader)
{
  assert(reader);

  SerdStatus st = SERD_SUCCESS;
  if (!reader->source) {
    return SERD_BAD_CALL;
  }

  if (!reader->source->prepared) {
    st = serd_reader_prepare(reader);
  } else if (reader->source->eof) {
    st = serd_byte_source_advance(reader->source);
  }

  return st ? st : read_statement(reader);
}

SerdStatus
serd_reader_finish(SerdReader* const reader)
{
  assert(reader);

  serd_byte_source_free(reader->world->allocator, reader->source);
  reader->source = NULL;
  return SERD_SUCCESS;
}
