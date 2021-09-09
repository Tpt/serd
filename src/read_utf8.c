// Copyright 2011-2021 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#include "read_utf8.h"
#include "reader.h"

#include "string_utils.h"

#include <stdio.h>

static SerdStatus
skip_invalid_utf8(SerdReader* const reader)
{
  for (int b = peek_byte(reader); b != EOF && ((uint8_t)b & 0x80);) {
    skip_byte(reader, b);
    b = peek_byte(reader);
  }

  return reader->strict ? SERD_BAD_SYNTAX : SERD_FAILURE;
}

static SerdStatus
bad_char(SerdReader* const reader, const char* const fmt, const uint8_t c)
{
  r_err(reader, SERD_BAD_SYNTAX, fmt, c);
  return skip_invalid_utf8(reader);
}

static SerdStatus
read_utf8_continuation_bytes(SerdReader* const reader,
                             uint8_t           bytes[4],
                             uint32_t* const   size,
                             const uint8_t     lead)
{
  *size = utf8_num_bytes(lead);
  if (*size < 1 || *size > 4) {
    return bad_char(reader, "0x%X is not a UTF-8 leading byte", lead);
  }

  bytes[0] = lead;

  for (uint32_t i = 1U; i < *size; ++i) {
    const int b = peek_byte(reader);
    if (b == EOF) {
      return r_err(reader, SERD_NO_DATA, "unexpected end of input");
    }

    const uint8_t byte = (uint8_t)b;
    if (!(byte & 0x80U)) {
      return bad_char(reader, "0x%X is not a UTF-8 continuation byte", byte);
    }

    skip_byte(reader, b);
    bytes[i] = byte;
  }

  return SERD_SUCCESS;
}

SerdStatus
read_utf8_continuation(SerdReader* const reader,
                       SerdNode* const   dest,
                       const uint8_t     lead)
{
  uint32_t size     = 0;
  uint8_t  bytes[8] = {lead, 0U, 0U, 0U, 0U, 0U, 0U, 0U};

  SerdStatus st = read_utf8_continuation_bytes(reader, bytes, &size, lead);
  if (st) {
    return reader->strict ? st : push_bytes(reader, dest, replacement_char, 3);
  }

  return push_bytes(reader, dest, bytes, size);
}

SerdStatus
read_utf8_code_point(SerdReader* const reader,
                     SerdNode* const   dest,
                     uint32_t* const   code,
                     const uint8_t     lead)
{
  uint32_t size     = 0U;
  uint8_t  bytes[8] = {lead, 0U, 0U, 0U, 0U, 0U, 0U, 0U};

  *code = 0U;

  skip_byte(reader, lead);

  SerdStatus st = read_utf8_continuation_bytes(reader, bytes, &size, lead);
  if (st) {
    return reader->strict ? st : push_bytes(reader, dest, replacement_char, 3);
  }

  if (!(st = push_bytes(reader, dest, bytes, size))) {
    *code = parse_counted_utf8_char(bytes, size);
  }

  return st;
}
