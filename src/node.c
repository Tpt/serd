// Copyright 2011-2022 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#include "node.h"

#include "memory.h"
#include "namespaces.h"
#include "string_utils.h"

#include "exess/exess.h"
#include "serd/node.h"
#include "serd/status.h"
#include "serd/string_view.h"
#include "serd/uri.h"
#include "serd/value.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifndef NDEBUG
#  define MUST_SUCCEED(status) assert(!(status))
#else
#  define MUST_SUCCEED(status) ((void)(status))
#endif

#define NS_XSD "http://www.w3.org/2001/XMLSchema#"

static const SerdNodeFlags meta_mask = (SERD_HAS_DATATYPE | SERD_HAS_LANGUAGE);

static const ExessDatatype value_type_datatypes[] = {
  EXESS_NOTHING,
  EXESS_BOOLEAN,
  EXESS_DOUBLE,
  EXESS_FLOAT,
  EXESS_LONG,
  EXESS_INT,
  EXESS_SHORT,
  EXESS_BYTE,
  EXESS_ULONG,
  EXESS_UINT,
  EXESS_USHORT,
  EXESS_UBYTE,
};

// Round size up to an even multiple of the node alignment
static size_t
serd_node_pad_size(const size_t size)
{
  const size_t n_trailing = size % serd_node_align;
  const size_t n_pad      = n_trailing ? (serd_node_align - n_trailing) : 0U;

  return size + n_pad;
}

static void
serd_node_check_padding(const SerdNode* node)
{
  (void)node;
#ifndef NDEBUG
  if (node) {
    const size_t padded_length = serd_node_pad_length(node->length);
    for (size_t i = 0; i < padded_length - node->length; ++i) {
      assert(serd_node_buffer_c(node)[node->length + i] == '\0');
    }

    serd_node_check_padding(serd_node_datatype(node));
    serd_node_check_padding(serd_node_language(node));
  }
#endif
}

size_t
serd_node_total_size(const SerdNode* const node)
{
  const size_t real_length = serd_node_pad_length(node->length);
  const size_t base_size   = sizeof(SerdNode) + real_length;

  if (!(node->flags & meta_mask)) {
    return base_size;
  }

  const SerdNode* const meta             = serd_node_meta_c(node);
  const size_t          meta_real_length = serd_node_pad_length(meta->length);

  return base_size + sizeof(SerdNode) + meta_real_length;
}

SerdNode*
serd_node_malloc(SerdAllocator* const allocator, const size_t size)
{
  SerdNode* const node = (SerdNode*)serd_aaligned_calloc(
    allocator, serd_node_align, serd_node_pad_size(size));

  assert((uintptr_t)node % serd_node_align == 0);
  return node;
}

SerdNode*
serd_node_try_malloc(SerdAllocator* const allocator, const SerdWriteResult r)
{
  return (r.status && r.status != SERD_OVERFLOW)
           ? NULL
           : serd_node_malloc(allocator, r.count);
}

SerdStatus
serd_node_set(SerdAllocator* const  allocator,
              SerdNode** const      dst,
              const SerdNode* const src)
{
  if (!src) {
    serd_aaligned_free(allocator, *dst);
    *dst = NULL;
    return SERD_SUCCESS;
  }

  const size_t size = serd_node_total_size(src);
  if (!*dst || serd_node_total_size(*dst) < size) {
    serd_aaligned_free(allocator, *dst);
    if (!(*dst = (SerdNode*)serd_aaligned_calloc(
            allocator, serd_node_align, size))) {
      return SERD_BAD_ALLOC;
    }
  }

  assert(*dst);
  memcpy(*dst, src, size);
  return SERD_SUCCESS;
}

/**
   Zero node padding.

   This is used for nodes which live in re-used stack memory during reading,
   which must be normalized before being passed to a sink so comparison will
   work correctly.
*/
void
serd_node_zero_pad(SerdNode* node)
{
  char*        buf           = serd_node_buffer(node);
  const size_t padded_length = serd_node_pad_length(node->length);

  memset(buf + node->length, 0, padded_length - node->length);

  if (node->flags & (SERD_HAS_DATATYPE | SERD_HAS_LANGUAGE)) {
    serd_node_zero_pad(serd_node_meta(node));
  }
}

static SerdWriteResult
result(const SerdStatus status, const size_t count)
{
  const SerdWriteResult result = {status, count};
  return result;
}

static SerdWriteResult
serd_node_construct_simple(const size_t         buf_size,
                           void* const          buf,
                           const SerdNodeType   type,
                           const SerdNodeFlags  flags,
                           const SerdStringView string)
{
  const size_t total_size = sizeof(SerdNode) + serd_node_pad_length(string.len);
  if (!buf || total_size > buf_size) {
    return result(SERD_OVERFLOW, total_size);
  }

  SerdNode* const node = (SerdNode*)buf;

  node->length = string.len;
  node->flags  = flags;
  node->type   = type;

  if (string.buf) {
    memcpy(serd_node_buffer(node), string.buf, string.len);
  }

  serd_node_zero_pad(node);
  return result(SERD_SUCCESS, total_size);
}

SerdWriteResult
serd_node_construct_token(const size_t         buf_size,
                          void* const          buf,
                          const SerdNodeType   type,
                          const SerdStringView string)
{
  return serd_node_construct_simple(buf_size, buf, type, 0U, string);
}

bool
is_langtag(const SerdStringView string)
{
  // First character must be a letter
  size_t i = 0;
  if (!string.len || !is_alpha(string.buf[i])) {
    return false;
  }

  // First component must be all letters
  while (++i < string.len && string.buf[i] && string.buf[i] != '-') {
    if (!is_alpha(string.buf[i])) {
      return false;
    }
  }

  // Following components can have letters and digits
  while (i < string.len && string.buf[i] == '-') {
    while (++i < string.len && string.buf[i] && string.buf[i] != '-') {
      const char c = string.buf[i];
      if (!is_alpha(c) && !is_digit(c)) {
        return false;
      }
    }
  }

  return true;
}

SerdWriteResult
serd_node_construct_literal(const size_t         buf_size,
                            void* const          buf,
                            const SerdStringView string,
                            const SerdNodeFlags  flags,
                            const SerdStringView meta)
{
  if (!(flags & (SERD_HAS_DATATYPE | SERD_HAS_LANGUAGE))) {
    return serd_node_construct_simple(
      buf_size, buf, SERD_LITERAL, flags, string);
  }

  if ((flags & SERD_HAS_DATATYPE) && (flags & SERD_HAS_LANGUAGE)) {
    return result(SERD_BAD_ARG, 0);
  }

  if (!meta.len) {
    return result(SERD_BAD_ARG, 0);
  }

  if (((flags & SERD_HAS_DATATYPE) &&
       (!serd_uri_string_has_scheme(meta.buf) ||
        !strcmp(meta.buf, NS_RDF "langString"))) ||
      ((flags & SERD_HAS_LANGUAGE) && !is_langtag(meta))) {
    return result(SERD_BAD_ARG, 0);
  }

  const size_t padded_length = serd_node_pad_length(string.len);

  const size_t meta_size  = sizeof(SerdNode) + serd_node_pad_length(meta.len);
  const size_t total_size = sizeof(SerdNode) + padded_length + meta_size;
  if (!buf || total_size > buf_size) {
    return result(SERD_OVERFLOW, total_size);
  }

  SerdNode* const node = (SerdNode*)buf;

  node->length = string.len;
  node->flags  = flags;
  node->type   = SERD_LITERAL;

  memcpy(serd_node_buffer(node), string.buf, string.len);

  SerdNode* meta_node = node + 1 + (padded_length / sizeof(SerdNode));
  meta_node->type     = (flags & SERD_HAS_DATATYPE) ? SERD_URI : SERD_LITERAL;
  meta_node->length   = meta.len;
  memcpy(serd_node_buffer(meta_node), meta.buf, meta.len);

  serd_node_zero_pad(node);
  return result(SERD_SUCCESS, total_size);
}

SerdWriteResult
serd_node_construct(const size_t         buf_size,
                    void* const          buf,
                    const SerdNodeType   type,
                    const SerdStringView string,
                    const SerdNodeFlags  flags,
                    const SerdStringView meta)
{
  return ((type == SERD_LITERAL)
            ? serd_node_construct_literal(buf_size, buf, string, flags, meta)
          : (meta.len > 0U)
            ? result(SERD_BAD_ARG, 0)
            : serd_node_construct_token(buf_size, buf, type, string));
}

static ExessDatatype
value_type_datatype(const SerdValueType value_type)
{
  return (value_type > SERD_UBYTE) ? EXESS_NOTHING
                                   : value_type_datatypes[value_type];
}

static const char*
value_type_uri(const SerdValueType value_type)
{
  return exess_datatype_uri(value_type_datatype(value_type));
}

static inline SerdValueType
datatype_value_type(const ExessDatatype datatype)
{
  switch (datatype) {
  case EXESS_NOTHING:
    return SERD_NOTHING;
  case EXESS_BOOLEAN:
    return SERD_BOOL;
  case EXESS_DECIMAL:
  case EXESS_DOUBLE:
    return SERD_DOUBLE;
  case EXESS_FLOAT:
    return SERD_FLOAT;
  case EXESS_INTEGER:
  case EXESS_NON_POSITIVE_INTEGER:
  case EXESS_NEGATIVE_INTEGER:
  case EXESS_LONG:
    return SERD_LONG;
  case EXESS_INT:
    return SERD_INT;
  case EXESS_SHORT:
    return SERD_SHORT;
  case EXESS_BYTE:
    return SERD_BYTE;
  case EXESS_NON_NEGATIVE_INTEGER:
  case EXESS_ULONG:
    return SERD_ULONG;
  case EXESS_UINT:
    return SERD_UINT;
  case EXESS_USHORT:
    return SERD_USHORT;
  case EXESS_UBYTE:
    return SERD_UBYTE;
  case EXESS_POSITIVE_INTEGER:
    return SERD_ULONG;

  case EXESS_DURATION:
  case EXESS_DATETIME:
  case EXESS_TIME:
  case EXESS_DATE:
  case EXESS_HEX:
  case EXESS_BASE64:
    break;
  }

  return SERD_NOTHING;
}

SerdWriteResult
serd_node_construct_value(const size_t    buf_size,
                          void* const     buf,
                          const SerdValue value)
{
  char        temp[EXESS_MAX_DOUBLE_LENGTH + 1] = {0};
  ExessResult r                                 = {EXESS_UNSUPPORTED, 0U};
  switch (value.type) {
  case SERD_NOTHING:
    return result(SERD_BAD_ARG, 0U);
  case SERD_BOOL:
    r = exess_write_boolean(value.data.as_bool, sizeof(temp), temp);
    break;
  case SERD_DOUBLE:
    r = exess_write_double(value.data.as_double, sizeof(temp), temp);
    break;
  case SERD_FLOAT:
    r = exess_write_float(value.data.as_float, sizeof(temp), temp);
    break;
  case SERD_LONG:
    r = exess_write_long(value.data.as_long, sizeof(temp), temp);
    break;
  case SERD_INT:
    r = exess_write_int(value.data.as_int, sizeof(temp), temp);
    break;
  case SERD_SHORT:
    r = exess_write_short(value.data.as_short, sizeof(temp), temp);
    break;
  case SERD_BYTE:
    r = exess_write_byte(value.data.as_byte, sizeof(temp), temp);
    break;
  case SERD_ULONG:
    r = exess_write_ulong(value.data.as_ulong, sizeof(temp), temp);
    break;
  case SERD_UINT:
    r = exess_write_uint(value.data.as_uint, sizeof(temp), temp);
    break;
  case SERD_USHORT:
    r = exess_write_ushort(value.data.as_ushort, sizeof(temp), temp);
    break;
  case SERD_UBYTE:
    r = exess_write_ubyte(value.data.as_ubyte, sizeof(temp), temp);
    break;
  }

  MUST_SUCCEED(r.status); // The only error is buffer overrun

  const char* const datatype_uri = value_type_uri(value.type);
  assert(datatype_uri);

  return serd_node_construct_literal(buf_size,
                                     buf,
                                     serd_substring(temp, r.count),
                                     SERD_HAS_DATATYPE,
                                     serd_string(datatype_uri));
}

SerdWriteResult
serd_node_construct_decimal(const size_t buf_size,
                            void* const  buf,
                            const double value)
{
  char temp[EXESS_MAX_DECIMAL_LENGTH + 1] = {0};

  const ExessResult r = exess_write_decimal(value, sizeof(temp), temp);
  MUST_SUCCEED(r.status); // The only error is buffer overrun

  return serd_node_construct_literal(buf_size,
                                     buf,
                                     serd_substring(temp, r.count),
                                     SERD_HAS_DATATYPE,
                                     serd_string(EXESS_XSD_URI "decimal"));
}

SerdWriteResult
serd_node_construct_integer(const size_t  buf_size,
                            void* const   buf,
                            const int64_t value)
{
  char              temp[24] = {0};
  const ExessResult r        = exess_write_long(value, sizeof(temp), temp);
  MUST_SUCCEED(r.status); // The only error is buffer overrun

  return serd_node_construct_literal(buf_size,
                                     buf,
                                     serd_substring(temp, r.count),
                                     SERD_HAS_DATATYPE,
                                     serd_string(NS_XSD "integer"));
}

SerdWriteResult
serd_node_construct_base64(const size_t      buf_size,
                           void* const       buf,
                           const size_t      value_size,
                           const void* const value)
{
  const SerdStringView xsd_base64Binary = serd_string(NS_XSD "base64Binary");

  // Verify argument sanity
  if (!value || !value_size) {
    return result(SERD_BAD_ARG, 0);
  }

  // Determine the type to use (default to xsd:base64Binary)
  const SerdStringView type        = xsd_base64Binary;
  const size_t         type_length = serd_node_pad_length(type.len);
  const size_t         type_size   = sizeof(SerdNode) + type_length;

  // Find the length of the encoded string (just an O(1) arithmetic expression)
  ExessResult r = exess_write_base64(value_size, value, 0, NULL);

  // Check that the provided buffer is large enough
  const size_t padded_length = serd_node_pad_length(r.count);
  const size_t total_size    = sizeof(SerdNode) + padded_length + type_size;
  if (!buf || total_size > buf_size) {
    return result(SERD_OVERFLOW, total_size);
  }

  SerdNode* const node = (SerdNode*)buf;
  node->length         = r.count;
  node->flags          = SERD_HAS_DATATYPE;
  node->type           = SERD_LITERAL;

  // Write the encoded base64 into the node body
  r = exess_write_base64(
    value_size, value, total_size - sizeof(SerdNode), serd_node_buffer(node));

  MUST_SUCCEED(r.status);
  (void)r;

  // Append datatype
  SerdNode* meta_node = node + 1 + (padded_length / sizeof(SerdNode));
  meta_node->length   = type.len;
  meta_node->flags    = 0U;
  meta_node->type     = SERD_URI;
  memcpy(serd_node_buffer(meta_node), type.buf, type.len);

  return result(SERD_SUCCESS, total_size);
}

static size_t
string_sink(const void* const buf,
            const size_t      size,
            const size_t      nmemb,
            void* const       stream)
{
  char** ptr = (char**)stream;
  memcpy(*ptr, buf, size * nmemb);
  *ptr += size * nmemb;
  return nmemb;
}

SerdWriteResult
serd_node_construct_uri(const size_t      buf_size,
                        void* const       buf,
                        const SerdURIView uri)
{
  const size_t length        = serd_uri_string_length(uri);
  const size_t required_size = sizeof(SerdNode) + serd_node_pad_length(length);
  if (!buf || buf_size < required_size) {
    return result(SERD_OVERFLOW, required_size);
  }

  // Write node header
  SerdNode* const node = (SerdNode*)buf;
  node->length         = length;
  node->flags          = 0U;
  node->type           = SERD_URI;

  // Serialise URI to node body
  char*        ptr           = serd_node_buffer(node);
  const size_t actual_length = serd_write_uri(uri, string_sink, &ptr);
  assert(actual_length == length);

  serd_node_buffer(node)[actual_length] = '\0';
  serd_node_check_padding(node);
  return result(SERD_SUCCESS, required_size);
}

SerdNode*
serd_node_new(SerdAllocator* const allocator,
              const SerdNodeType   type,
              const SerdStringView string,
              const SerdNodeFlags  flags,
              const SerdStringView meta)
{
  SerdWriteResult r = serd_node_construct(0, NULL, type, string, flags, meta);
  if (r.status != SERD_OVERFLOW) {
    return NULL;
  }

  assert(r.count % sizeof(SerdNode) == 0);

  SerdNode* const node =
    serd_node_malloc(allocator, sizeof(SerdNode) + r.count + 1);

  if (node) {
    r = serd_node_construct(r.count, node, type, string, flags, meta);
    MUST_SUCCEED(r.status); // Any error should have been reported above
  }

  return node;
}

SerdNode*
serd_new_token(SerdAllocator* const allocator,
               const SerdNodeType   type,
               const SerdStringView string)
{
  return serd_node_new(allocator, type, string, 0U, serd_empty_string());
}

SerdNode*
serd_new_string(SerdAllocator* const allocator, const SerdStringView str)
{
  return serd_node_new(allocator, SERD_LITERAL, str, 0U, serd_empty_string());
}

SerdNode*
serd_new_literal(SerdAllocator* const allocator,
                 const SerdStringView str,
                 const SerdNodeFlags  flags,
                 const SerdStringView meta)
{
  return serd_node_new(allocator, SERD_LITERAL, str, flags, meta);
}

SerdValue
serd_node_value(const SerdNode* const node)
{
  assert(node);

  const SerdNode* const datatype_node = serd_node_datatype(node);

  const ExessDatatype datatype =
    datatype_node ? exess_datatype_from_uri(serd_node_string(datatype_node))
                  : EXESS_NOTHING;

  const SerdValueType value_type = datatype_value_type(datatype);
  if (value_type == SERD_NOTHING) {
    return serd_nothing();
  }

  ExessValue                value = {false};
  const ExessVariableResult vr =
    exess_read_value(datatype, sizeof(value), &value, serd_node_string(node));

  if (vr.status) {
    return serd_nothing();
  }

  SerdValue result = {value_type, {false}};
  memcpy(&result.data, &value, vr.write_count);

  return result;
}

SerdValue
serd_node_value_as(const SerdNode* const node,
                   const SerdValueType   type,
                   const bool            lossy)
{
  // Get the value as it is
  const SerdValue value = serd_node_value(node);
  if (!value.type || value.type == type) {
    return value;
  }

  const ExessCoercions coercions =
    lossy ? (EXESS_REDUCE_PRECISION | EXESS_ROUND | EXESS_TRUNCATE)
          : EXESS_LOSSLESS;

  const ExessDatatype node_datatype = value_type_datatype(value.type);
  const ExessDatatype datatype      = value_type_datatype(type);
  SerdValue           result        = {type, {false}};

  // Coerce to the desired type
  const ExessResult r = exess_value_coerce(coercions,
                                           node_datatype,
                                           exess_value_size(node_datatype),
                                           &value.data,
                                           datatype,
                                           exess_value_size(datatype),
                                           &result.data);

  if (r.status) {
    result.type = SERD_NOTHING;
  }

  return result;
}

size_t
serd_get_base64_size(const SerdNode* const node)
{
  return exess_base64_decoded_size(serd_node_length(node));
}

SerdWriteResult
serd_get_base64(const SerdNode* const node,
                const size_t          buf_size,
                void* const           buf)
{
  const size_t              max_size = serd_get_base64_size(node);
  const ExessVariableResult r =
    exess_read_base64(buf_size, buf, serd_node_string(node));

  return r.status == EXESS_NO_SPACE ? result(SERD_OVERFLOW, max_size)
         : r.status                 ? result(SERD_BAD_SYNTAX, 0U)
                                    : result(SERD_SUCCESS, r.write_count);
}

SerdNode*
serd_node_copy(SerdAllocator* const allocator, const SerdNode* node)
{
  if (!node) {
    return NULL;
  }

  const size_t size = serd_node_total_size(node);
  SerdNode*    copy =
    (SerdNode*)serd_aaligned_alloc(allocator, serd_node_align, size);

  if (copy) {
    memcpy(copy, node, size);
  }

  return copy;
}

bool
serd_node_equals(const SerdNode* const a, const SerdNode* const b)
{
  if (a == b) {
    return true;
  }

  if (!a || !b || a->length != b->length || a->flags != b->flags ||
      a->type != b->type) {
    return false;
  }

  const size_t length = a->length;
  if (!!memcmp(serd_node_string(a), serd_node_string(b), length)) {
    return false;
  }

  const SerdNodeFlags flags = a->flags;
  if (flags & meta_mask) {
    const SerdNode* const am = serd_node_meta_c(a);
    const SerdNode* const bm = serd_node_meta_c(b);

    return am->length == bm->length && am->type == bm->type &&
           !memcmp(serd_node_string(am), serd_node_string(bm), am->length);
  }

  return true;
}

int
serd_node_compare(const SerdNode* const a, const SerdNode* const b)
{
  assert(a);
  assert(b);

  int cmp = 0;

  if ((cmp = ((int)a->type - (int)b->type)) ||
      (cmp = strcmp(serd_node_string_i(a), serd_node_string_i(b))) ||
      (cmp = ((int)a->flags - (int)b->flags)) ||
      !(a->flags & (SERD_HAS_LANGUAGE | SERD_HAS_DATATYPE))) {
    return cmp;
  }

  assert(a->flags == b->flags);
  assert(a->flags & (SERD_HAS_LANGUAGE | SERD_HAS_DATATYPE));
  assert(b->flags & (SERD_HAS_LANGUAGE | SERD_HAS_DATATYPE));
  const SerdNode* const ma = serd_node_meta_c(a);
  const SerdNode* const mb = serd_node_meta_c(b);

  assert(ma->type == mb->type);
  assert(ma->flags == mb->flags);

  return strcmp(serd_node_string_i(ma), serd_node_string_i(mb));
}

SerdNode*
serd_new_uri(SerdAllocator* const allocator, const SerdStringView string)
{
  return serd_new_token(allocator, SERD_URI, string);
}

SerdNode*
serd_new_parsed_uri(SerdAllocator* const allocator, const SerdURIView uri)
{
  SerdWriteResult r    = serd_node_construct_uri(0U, NULL, uri);
  SerdNode* const node = serd_node_try_malloc(allocator, r);

  if (node) {
    r = serd_node_construct_uri(r.count, node, uri);
    MUST_SUCCEED(r.status);
  }

  return node;
}

typedef struct {
  char*  buf;
  size_t len;
  size_t offset;
} ConstructWriteHead;

static size_t
construct_write(const void* const buf,
                const size_t      size,
                const size_t      nmemb,
                void* const       stream)
{
  const size_t              n_bytes = size * nmemb;
  ConstructWriteHead* const head    = (ConstructWriteHead*)stream;

  if (head->buf && head->offset + n_bytes <= head->len) {
    memcpy(head->buf + head->offset, buf, n_bytes);
  }

  head->offset += n_bytes;
  return n_bytes;
}

SerdWriteResult
serd_node_construct_file_uri(const size_t         buf_size,
                             void* const          buf,
                             const SerdStringView path,
                             const SerdStringView hostname)
{
  SerdNode* const    node  = (SerdNode*)buf;
  ConstructWriteHead head  = {(char*)buf, buf_size, 0U};
  size_t             count = 0U;

  // Write node header
  SerdNode header = {0U, 0U, SERD_URI};
  count += construct_write(&header, sizeof(header), 1, &head);

  // Write URI string node body
  const size_t length =
    serd_write_file_uri(path, hostname, construct_write, &head);

  // Terminate string and pad with at least 1 additional null byte
  const size_t padded_length = serd_node_pad_length(length);
  count += length;
  for (size_t p = 0U; p < padded_length - length; ++p) {
    count += construct_write("", 1, 1, &head);
  }

  if (!buf || count > buf_size) {
    return result(SERD_OVERFLOW, count);
  }

  node->length = length;
  assert(node->length == strlen(serd_node_string(node)));

  return result(SERD_SUCCESS, count);
}

SerdNode*
serd_new_file_uri(SerdAllocator* const allocator,
                  const SerdStringView path,
                  const SerdStringView hostname)
{
  SerdWriteResult r    = serd_node_construct_file_uri(0, NULL, path, hostname);
  SerdNode* const node = serd_node_try_malloc(allocator, r);

  if (node) {
    r = serd_node_construct_file_uri(r.count, node, path, hostname);
    MUST_SUCCEED(r.status);
    assert(serd_node_length(node) == strlen(serd_node_string(node)));
    serd_node_check_padding(node);
  }

  return node;
}

SerdNode*
serd_new_value(SerdAllocator* const allocator, const SerdValue value)
{
  SerdWriteResult r    = serd_node_construct_value(0, NULL, value);
  SerdNode* const node = serd_node_try_malloc(allocator, r);

  if (node) {
    r = serd_node_construct_value(r.count, node, value);
    MUST_SUCCEED(r.status);
    assert(serd_node_length(node) == strlen(serd_node_string(node)));
    serd_node_check_padding(node);
  }

  return node;
}

SerdNode*
serd_new_decimal(SerdAllocator* const allocator, const double d)
{
  SerdWriteResult r    = serd_node_construct_decimal(0, NULL, d);
  SerdNode* const node = serd_node_try_malloc(allocator, r);

  if (node) {
    r = serd_node_construct_decimal(r.count, node, d);
    MUST_SUCCEED(r.status);
    assert(serd_node_length(node) == strlen(serd_node_string(node)));
    serd_node_check_padding(node);
  }

  return node;
}

SerdNode*
serd_new_integer(SerdAllocator* const allocator, const int64_t i)
{
  SerdWriteResult r    = serd_node_construct_integer(0, NULL, i);
  SerdNode* const node = serd_node_try_malloc(allocator, r);

  if (node) {
    r = serd_node_construct_integer(r.count, node, i);
    MUST_SUCCEED(r.status);
    assert(serd_node_length(node) == strlen(serd_node_string(node)));
    serd_node_check_padding(node);
  }

  return node;
}

SerdNode*
serd_new_base64(SerdAllocator* const allocator, const void* buf, size_t size)
{
  SerdWriteResult r    = serd_node_construct_base64(0, NULL, size, buf);
  SerdNode* const node = serd_node_try_malloc(allocator, r);

  if (node) {
    r = serd_node_construct_base64(r.count, node, size, buf);
    MUST_SUCCEED(r.status);
    assert(serd_node_length(node) == strlen(serd_node_string(node)));
    serd_node_check_padding(node);
  }

  return node;
}

SerdNodeType
serd_node_type(const SerdNode* const node)
{
  assert(node);

  return node->type;
}

const char*
serd_node_string(const SerdNode* const node)
{
  assert(node);

  return (const char*)(node + 1);
}

size_t
serd_node_length(const SerdNode* const node)
{
  assert(node);

  return node->length;
}

SerdStringView
serd_node_string_view(const SerdNode* const node)
{
  assert(node);

  const SerdStringView r = {(const char*)(node + 1), node->length};

  return r;
}

SERD_PURE_FUNC
SerdURIView
serd_node_uri_view(const SerdNode* const node)
{
  assert(node);

  return (node->type == SERD_URI) ? serd_parse_uri(serd_node_string(node))
                                  : SERD_URI_NULL;
}

const SerdNode*
serd_node_datatype(const SerdNode* const node)
{
  assert(node);

  if (!(node->flags & SERD_HAS_DATATYPE)) {
    return NULL;
  }

  const SerdNode* const datatype = serd_node_meta_c(node);
  assert(datatype->type == SERD_URI);
  return datatype;
}

const SerdNode*
serd_node_language(const SerdNode* const node)
{
  assert(node);

  if (!(node->flags & SERD_HAS_LANGUAGE)) {
    return NULL;
  }

  const SerdNode* const lang = serd_node_meta_c(node);
  assert(lang->type == SERD_LITERAL);
  return lang;
}

SerdNodeFlags
serd_node_flags(const SerdNode* const node)
{
  return node->flags;
}

void
serd_node_free(SerdAllocator* const allocator, SerdNode* const node)
{
  serd_aaligned_free(allocator, node);
}

#undef MUST_SUCCEED
