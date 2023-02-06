// Copyright 2011-2020 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#include "byte_sink.h"
#include "env.h"
#include "node.h"
#include "serd_internal.h"
#include "sink.h"
#include "stack.h"
#include "string_utils.h"
#include "uri_utils.h"
#include "world.h"

#include "serd/buffer.h"
#include "serd/env.h"
#include "serd/event.h"
#include "serd/node.h"
#include "serd/sink.h"
#include "serd/statement.h"
#include "serd/status.h"
#include "serd/stream.h"
#include "serd/string_view.h"
#include "serd/syntax.h"
#include "serd/uri.h"
#include "serd/world.h"
#include "serd/writer.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
  CTX_NAMED, ///< Normal non-anonymous context
  CTX_BLANK, ///< Anonymous blank node
  CTX_LIST   ///< Anonymous list
} ContextType;

typedef struct {
  ContextType type;
  SerdNode*   graph;
  SerdNode*   subject;
  SerdNode*   predicate;
  bool        indented_object;
} WriteContext;

static const WriteContext WRITE_CONTEXT_NULL = {CTX_NAMED,
                                                NULL,
                                                NULL,
                                                NULL,
                                                false};

typedef enum {
  SEP_NONE,        ///< Placeholder for nodes or nothing
  SEP_END_S,       ///< End of a subject ('.')
  SEP_END_P,       ///< End of a predicate (';')
  SEP_END_O,       ///< End of an object (',')
  SEP_S_P,         ///< Between a subject and predicate (whitespace)
  SEP_P_O,         ///< Between a predicate and object (whitespace)
  SEP_ANON_BEGIN,  ///< Start of anonymous node ('[')
  SEP_ANON_S_P,    ///< Between start of anonymous node and predicate
  SEP_ANON_END,    ///< End of anonymous node (']')
  SEP_LIST_BEGIN,  ///< Start of list ('(')
  SEP_LIST_SEP,    ///< List separator (whitespace)
  SEP_LIST_END,    ///< End of list (')')
  SEP_GRAPH_BEGIN, ///< Start of graph ('{')
  SEP_GRAPH_END,   ///< End of graph ('}')
} Sep;

typedef uint32_t SepMask; ///< Bitfield of separator flags

#define SEP_ALL ((SepMask)-1)
#define M(s) (1U << (s))

typedef struct {
  const char* str;             ///< Sep string
  size_t      len;             ///< Length of sep string
  int         indent;          ///< Indent delta
  SepMask     pre_space_after; ///< Leading space if after given seps
  SepMask     pre_line_after;  ///< Leading newline if after given seps
  SepMask     post_line_after; ///< Trailing newline if after given seps
} SepRule;

static const SepRule rules[] = {
  {"", 0, +0, SEP_NONE, SEP_NONE, SEP_NONE},
  {".\n", 2, -1, SEP_ALL, SEP_NONE, SEP_NONE},
  {";", 1, +0, SEP_ALL, SEP_NONE, SEP_ALL},
  {",", 1, +0, SEP_ALL, SEP_NONE, ~(M(SEP_ANON_END) | M(SEP_LIST_END))},
  {"", 0, +1, SEP_NONE, SEP_NONE, SEP_ALL},
  {" ", 1, +0, SEP_NONE, SEP_NONE, SEP_NONE},
  {"[", 1, +1, M(SEP_END_O), SEP_NONE, SEP_NONE},
  {"", 0, +0, SEP_NONE, SEP_ALL, SEP_NONE},
  {"]", 1, -1, SEP_NONE, ~M(SEP_ANON_BEGIN), SEP_NONE},
  {"(", 1, +1, M(SEP_END_O), SEP_NONE, SEP_ALL},
  {"", 0, +0, SEP_NONE, SEP_ALL, SEP_NONE},
  {")", 1, -1, SEP_NONE, SEP_ALL, SEP_NONE},
  {"{", 1, +1, SEP_ALL, SEP_NONE, SEP_NONE},
  {"}", 1, -1, SEP_NONE, SEP_NONE, SEP_ALL},
};

struct SerdWriterImpl {
  SerdWorld*      world;
  SerdSink        iface;
  SerdSyntax      syntax;
  SerdWriterFlags flags;
  SerdEnv*        env;
  SerdNode*       root_node;
  SerdURIView     root_uri;
  SerdStack       anon_stack;
  SerdByteSink    byte_sink;
  WriteContext    context;
  char*           bprefix;
  size_t          bprefix_len;
  Sep             last_sep;
  int             indent;
  bool            empty;
};

typedef enum { WRITE_STRING, WRITE_LONG_STRING } TextContext;

static SerdStatus
serd_writer_set_base_uri(SerdWriter* writer, const SerdNode* uri);

static SerdStatus
serd_writer_set_prefix(SerdWriter*     writer,
                       const SerdNode* name,
                       const SerdNode* uri);

static bool
write_node(SerdWriter*        writer,
           const SerdNode*    node,
           SerdField          field,
           SerdStatementFlags flags);

static bool
supports_abbrev(const SerdWriter* writer)
{
  return writer->syntax == SERD_TURTLE || writer->syntax == SERD_TRIG;
}

static bool
supports_uriref(const SerdWriter* writer)
{
  return writer->syntax == SERD_TURTLE || writer->syntax == SERD_TRIG;
}

static SerdStatus
free_context(SerdWriter* writer)
{
  serd_node_free(writer->context.graph);
  serd_node_free(writer->context.subject);
  serd_node_free(writer->context.predicate);
  return SERD_SUCCESS;
}

static SerdStatus
push_context(SerdWriter* writer, const WriteContext new_context)
{
  WriteContext* top =
    (WriteContext*)serd_stack_push(&writer->anon_stack, sizeof(WriteContext));

  if (!top) {
    return SERD_ERR_OVERFLOW;
  }

  *top            = writer->context;
  writer->context = new_context;
  return SERD_SUCCESS;
}

static void
pop_context(SerdWriter* writer)
{
  assert(!serd_stack_is_empty(&writer->anon_stack));

  free_context(writer);
  writer->context =
    *(WriteContext*)(writer->anon_stack.buf + writer->anon_stack.size -
                     sizeof(WriteContext));

  serd_stack_pop(&writer->anon_stack, sizeof(WriteContext));
}

static SerdNode*
ctx(SerdWriter* writer, const SerdField field)
{
  SerdNode* node = NULL;
  if (field == SERD_SUBJECT) {
    node = writer->context.subject;
  } else if (field == SERD_PREDICATE) {
    node = writer->context.predicate;
  } else if (field == SERD_GRAPH) {
    node = writer->context.graph;
  }

  return node && node->type ? node : NULL;
}

static size_t
sink(const void* buf, size_t len, SerdWriter* writer)
{
  return serd_byte_sink_write(buf, len, &writer->byte_sink);
}

// Write a single character, as an escape for single byte characters
// (Caller prints any single byte characters that don't need escaping)
static size_t
write_character(SerdWriter* writer, const uint8_t* utf8, size_t* size)
{
  char           escape[11] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  const uint32_t c          = parse_utf8_char(utf8, size);
  switch (*size) {
  case 0:
    serd_world_errorf(
      writer->world, SERD_ERR_BAD_ARG, "invalid UTF-8 start: %X\n", utf8[0]);
    return 0;
  case 1:
    snprintf(escape, sizeof(escape), "\\u%04X", utf8[0]);
    return sink(escape, 6, writer);
  default:
    break;
  }

  if (!(writer->flags & SERD_WRITE_ASCII)) {
    // Write UTF-8 character directly to UTF-8 output
    return sink(utf8, *size, writer);
  }

  if (c <= 0xFFFF) {
    snprintf(escape, sizeof(escape), "\\u%04X", c);
    return sink(escape, 6, writer);
  }

  snprintf(escape, sizeof(escape), "\\U%08X", c);
  return sink(escape, 10, writer);
}

static bool
uri_must_escape(const int c)
{
  switch (c) {
  case ' ':
  case '"':
  case '<':
  case '>':
  case '\\':
  case '^':
  case '`':
  case '{':
  case '|':
  case '}':
    return true;
  default:
    return !in_range(c, 0x20, 0x7E);
  }
}

static size_t
write_uri(SerdWriter* writer, const char* utf8, size_t n_bytes)
{
  size_t len = 0;
  for (size_t i = 0; i < n_bytes;) {
    size_t j = i; // Index of next character that must be escaped
    for (; j < n_bytes; ++j) {
      if (uri_must_escape(utf8[j])) {
        break;
      }
    }

    // Bulk write all characters up to this special one
    len += sink(&utf8[i], j - i, writer);
    if ((i = j) == n_bytes) {
      break; // Reached end
    }

    // Write UTF-8 character
    size_t size = 0;
    len += write_character(writer, (const uint8_t*)utf8 + i, &size);
    i += size;
    if (size == 0) {
      // Corrupt input, write percent-encoded bytes and scan to next start
      char escape[4] = {0, 0, 0, 0};
      for (; i < n_bytes && (utf8[i] & 0x80); ++i) {
        snprintf(escape, sizeof(escape), "%%%02X", (uint8_t)utf8[i]);
        len += sink(escape, 3, writer);
      }
    }
  }
  return len;
}

static size_t
write_uri_from_node(SerdWriter* writer, const SerdNode* node)
{
  return write_uri(writer, serd_node_string(node), node->length);
}

static bool
lname_must_escape(const char c)
{
  /* This arbitrary list of characters, most of which have nothing to do with
     Turtle, must be handled as special cases here because the RDF and SPARQL
     WGs are apparently intent on making the once elegant Turtle a baroque
     and inconsistent mess, throwing elegance and extensibility completely
     out the window for no good reason.

     Note '-', '.', and '_' are also in PN_LOCAL_ESC, but are valid unescaped
     in local names, so they are not escaped here. */

  switch (c) {
  case '\'':
  case '!':
  case '#':
  case '$':
  case '%':
  case '&':
  case '(':
  case ')':
  case '*':
  case '+':
  case ',':
  case '/':
  case ';':
  case '=':
  case '?':
  case '@':
  case '~':
    return true;
  default:
    break;
  }
  return false;
}

static size_t
write_lname(SerdWriter* writer, const char* utf8, size_t n_bytes)
{
  size_t len = 0;
  for (size_t i = 0; i < n_bytes; ++i) {
    size_t j = i; // Index of next character that must be escaped
    for (; j < n_bytes; ++j) {
      if (lname_must_escape(utf8[j])) {
        break;
      }
    }

    // Bulk write all characters up to this special one
    len += sink(&utf8[i], j - i, writer);
    if ((i = j) == n_bytes) {
      break; // Reached end
    }

    // Write escape
    len += sink("\\", 1, writer);
    len += sink(&utf8[i], 1, writer);
  }

  return len;
}

static size_t
write_text(SerdWriter* writer,
           TextContext ctx,
           const char* utf8,
           size_t      n_bytes)
{
  size_t len                  = 0;
  size_t n_consecutive_quotes = 0;
  for (size_t i = 0; i < n_bytes;) {
    if (utf8[i] != '"') {
      n_consecutive_quotes = 0;
    }

    // Fast bulk write for long strings of printable ASCII
    size_t j = i;
    for (; j < n_bytes; ++j) {
      if (utf8[j] == '\\' || utf8[j] == '"' ||
          (!in_range(utf8[j], 0x20, 0x7E))) {
        break;
      }
    }

    len += sink(&utf8[i], j - i, writer);
    if ((i = j) == n_bytes) {
      break; // Reached end
    }

    const char in = utf8[i++];
    if (ctx == WRITE_LONG_STRING) {
      n_consecutive_quotes = (in == '\"') ? (n_consecutive_quotes + 1) : 0;

      switch (in) {
      case '\\':
        len += sink("\\\\", 2, writer);
        continue;
      case '\b':
        len += sink("\\b", 2, writer);
        continue;
      case '\n':
      case '\r':
      case '\t':
      case '\f':
        len += sink(&in, 1, writer); // Write character as-is
        continue;
      case '\"':
        if (n_consecutive_quotes >= 3 || i == n_bytes) {
          // Two quotes in a row, or quote at string end, escape
          len += sink("\\\"", 2, writer);
        } else {
          len += sink(&in, 1, writer);
        }
        continue;
      default:
        break;
      }
    } else if (ctx == WRITE_STRING) {
      switch (in) {
      case '\\':
        len += sink("\\\\", 2, writer);
        continue;
      case '\n':
        len += sink("\\n", 2, writer);
        continue;
      case '\r':
        len += sink("\\r", 2, writer);
        continue;
      case '\t':
        len += sink("\\t", 2, writer);
        continue;
      case '"':
        len += sink("\\\"", 2, writer);
        continue;
      default:
        break;
      }
      if (writer->syntax == SERD_TURTLE) {
        switch (in) {
        case '\b':
          len += sink("\\b", 2, writer);
          continue;
        case '\f':
          len += sink("\\f", 2, writer);
          continue;
        default:
          break;
        }
      }
    }

    // Write UTF-8 character
    size_t size = 0;
    len += write_character(writer, (const uint8_t*)utf8 + i - 1, &size);

    if (size == 0) {
      // Corrupt input, write replacement character and scan to the next start
      len += sink(replacement_char, sizeof(replacement_char), writer);
      for (; i < n_bytes && (utf8[i] & 0x80); ++i) {
      }
    } else {
      i += size - 1;
    }
  }
  return len;
}

static size_t
uri_sink(const void* buf, size_t size, size_t nmemb, void* stream)
{
  (void)size;
  assert(size == 1);
  return write_uri((SerdWriter*)stream, (const char*)buf, nmemb);
}

static void
write_newline(SerdWriter* writer)
{
  if (writer->flags & SERD_WRITE_TERSE) {
    sink(" ", 1, writer);
  } else {
    sink("\n", 1, writer);
    for (int i = 0; i < writer->indent; ++i) {
      sink("\t", 1, writer);
    }
  }
}

static void
write_top_level_sep(SerdWriter* writer)
{
  if (!writer->empty && !(writer->flags & SERD_WRITE_TERSE)) {
    write_newline(writer);
  }
}

static bool
write_sep(SerdWriter* writer, const Sep sep)
{
  const SepRule* rule = &rules[sep];

  // Adjust indent, but tolerate if it would become negative
  if ((rule->pre_line_after & (1U << writer->last_sep)) ||
      (rule->post_line_after & (1U << writer->last_sep))) {
    writer->indent = ((rule->indent >= 0 || writer->indent >= -rule->indent)
                        ? writer->indent + rule->indent
                        : 0);
  }

  // Write newline or space before separator if necessary
  if (rule->pre_line_after & (1U << writer->last_sep)) {
    write_newline(writer);
  } else if (rule->pre_space_after & (1U << writer->last_sep)) {
    sink(" ", 1, writer);
  }

  // Write actual separator string
  sink(rule->str, rule->len, writer);

  // Write newline after separator if necessary
  if (rule->post_line_after & (1U << writer->last_sep)) {
    write_newline(writer);
    writer->last_sep = SEP_NONE;
  } else {
    writer->last_sep = sep;
  }

  if (sep == SEP_END_S) {
    writer->indent = 0;
  }

  return true;
}

static SerdStatus
reset_context(SerdWriter* writer, bool graph)
{
  // Free any lingering contexts in case there was an error
  while (!serd_stack_is_empty(&writer->anon_stack)) {
    pop_context(writer);
  }

  if (graph && writer->context.graph) {
    memset(writer->context.graph, 0, sizeof(SerdNode));
  }

  if (writer->context.subject) {
    memset(writer->context.subject, 0, sizeof(SerdNode));
  }

  if (writer->context.predicate) {
    memset(writer->context.predicate, 0, sizeof(SerdNode));
  }

  writer->context.indented_object = false;
  writer->empty                   = false;

  serd_stack_clear(&writer->anon_stack);

  return SERD_SUCCESS;
}

static bool
is_inline_start(const SerdWriter*  writer,
                SerdField          field,
                SerdStatementFlags flags)
{
  return (supports_abbrev(writer) &&
          ((field == SERD_SUBJECT && (flags & SERD_ANON_S)) ||
           (field == SERD_OBJECT && (flags & SERD_ANON_O))));
}

static bool
write_literal(SerdWriter* const        writer,
              const SerdNode* const    node,
              const SerdStatementFlags flags)
{
  writer->last_sep = SEP_NONE;

  const SerdNode* datatype = serd_node_datatype(node);
  const SerdNode* lang     = serd_node_language(node);
  const char*     node_str = serd_node_string(node);
  const char*     type_uri = datatype ? serd_node_string(datatype) : NULL;
  if (supports_abbrev(writer) && type_uri) {
    if (!strncmp(type_uri, NS_XSD, sizeof(NS_XSD) - 1) &&
        (!strcmp(type_uri + sizeof(NS_XSD) - 1, "boolean") ||
         !strcmp(type_uri + sizeof(NS_XSD) - 1, "integer"))) {
      sink(node_str, node->length, writer);
      return true;
    }

    if (!strncmp(type_uri, NS_XSD, sizeof(NS_XSD) - 1) &&
        !strcmp(type_uri + sizeof(NS_XSD) - 1, "decimal") &&
        strchr(node_str, '.') && node_str[node->length - 1] != '.') {
      /* xsd:decimal literals without trailing digits, e.g. "5.", can
         not be written bare in Turtle.  We could add a 0 which is
         prettier, but changes the text and breaks round tripping.
      */
      sink(node_str, node->length, writer);
      return true;
    }
  }

  if (supports_abbrev(writer) &&
      (node->flags & (SERD_HAS_NEWLINE | SERD_HAS_QUOTE))) {
    sink("\"\"\"", 3, writer);
    write_text(writer, WRITE_LONG_STRING, node_str, node->length);
    sink("\"\"\"", 3, writer);
  } else {
    sink("\"", 1, writer);
    write_text(writer, WRITE_STRING, node_str, node->length);
    sink("\"", 1, writer);
  }
  if (lang && serd_node_string(lang)) {
    sink("@", 1, writer);
    sink(serd_node_string(lang), lang->length, writer);
  } else if (type_uri) {
    sink("^^", 2, writer);
    return write_node(writer, datatype, (SerdField)-1, flags);
  }
  return true;
}

// Return true iff `buf` is a valid prefixed name prefix or suffix
static bool
is_name(const char* buf, const size_t len)
{
  // TODO: This is more strict than it should be
  for (size_t i = 0; i < len; ++i) {
    if (!(is_alpha(buf[i]) || is_digit(buf[i]))) {
      return false;
    }
  }

  return true;
}

static bool
write_uri_node(SerdWriter* const     writer,
               const SerdNode* const node,
               const SerdField       field)
{
  const SerdNode* prefix     = NULL;
  SerdStringView  suffix     = {NULL, 0};
  const char*     node_str   = serd_node_string(node);
  const bool      has_scheme = serd_uri_string_has_scheme(node_str);
  if (supports_abbrev(writer)) {
    if (field == SERD_PREDICATE && !strcmp(node_str, NS_RDF "type")) {
      return sink("a", 1, writer) == 1;
    }

    if (!strcmp(node_str, NS_RDF "nil")) {
      return sink("()", 2, writer) == 2;
    }

    if (has_scheme && !(writer->flags & SERD_WRITE_UNQUALIFIED) &&
        serd_env_qualify(writer->env, node, &prefix, &suffix) &&
        is_name(serd_node_string(prefix), serd_node_length(prefix)) &&
        is_name(suffix.buf, suffix.len)) {
      write_uri_from_node(writer, prefix);
      sink(":", 1, writer);
      write_uri(writer, suffix.buf, suffix.len);
      return true;
    }
  }

  if (!has_scheme && !supports_uriref(writer) &&
      !serd_env_base_uri(writer->env)) {
    serd_world_errorf(writer->world,
                      SERD_ERR_BAD_ARG,
                      "syntax does not support URI reference <%s>\n",
                      node_str);
    return false;
  }

  sink("<", 1, writer);
  if (!(writer->flags & SERD_WRITE_UNRESOLVED) &&
      serd_env_base_uri(writer->env)) {
    const SerdURIView  base_uri = serd_env_base_uri_view(writer->env);
    SerdURIView        uri      = serd_parse_uri(node_str);
    SerdURIView        abs_uri  = serd_resolve_uri(uri, base_uri);
    bool               rooted   = uri_is_under(&base_uri, &writer->root_uri);
    const SerdURIView* root     = rooted ? &writer->root_uri : &base_uri;

    if (writer->syntax == SERD_NTRIPLES || writer->syntax == SERD_NQUADS ||
        !uri_is_under(&abs_uri, root) || !uri_is_related(&abs_uri, &base_uri)) {
      serd_write_uri(abs_uri, uri_sink, writer);
    } else {
      serd_write_uri(serd_relative_uri(uri, base_uri), uri_sink, writer);
    }
  } else {
    write_uri_from_node(writer, node);
  }

  sink(">", 1, writer);
  return true;
}

static bool
write_curie(SerdWriter* const writer, const SerdNode* const node)
{
  writer->last_sep = SEP_NONE;

  SerdStringView prefix = {NULL, 0};
  SerdStringView suffix = {NULL, 0};
  SerdStatus     st     = SERD_SUCCESS;
  switch (writer->syntax) {
  case SERD_NTRIPLES:
  case SERD_NQUADS:
    if ((st = serd_env_expand(writer->env, node, &prefix, &suffix))) {
      serd_world_errorf(writer->world,
                        st,
                        "undefined namespace prefix '%s'\n",
                        serd_node_string(node));
      return false;
    }
    sink("<", 1, writer);
    write_uri(writer, prefix.buf, prefix.len);
    write_uri(writer, suffix.buf, suffix.len);
    sink(">", 1, writer);
    break;
  case SERD_TURTLE:
  case SERD_TRIG:
    write_lname(writer, serd_node_string(node), node->length);
    break;
  }

  return true;
}

static bool
write_blank(SerdWriter* const        writer,
            const SerdNode*          node,
            const SerdField          field,
            const SerdStatementFlags flags)
{
  const char* node_str = serd_node_string(node);
  if (supports_abbrev(writer)) {
    if (is_inline_start(writer, field, flags)) {
      return write_sep(writer, SEP_ANON_BEGIN);
    }

    if ((field == SERD_SUBJECT && (flags & SERD_LIST_S)) ||
        (field == SERD_OBJECT && (flags & SERD_LIST_O))) {
      return write_sep(writer, SEP_LIST_BEGIN);
    }

    if (field == SERD_SUBJECT && (flags & SERD_EMPTY_S)) {
      writer->last_sep = SEP_NONE; // Treat "[]" like a node
      return sink("[]", 2, writer) == 2;
    }
  }

  sink("_:", 2, writer);
  if (writer->bprefix &&
      !strncmp(node_str, writer->bprefix, writer->bprefix_len)) {
    sink(node_str + writer->bprefix_len,
         node->length - writer->bprefix_len,
         writer);
  } else {
    sink(node_str, node->length, writer);
  }

  writer->last_sep = SEP_NONE;
  return true;
}

static bool
write_node(SerdWriter* const        writer,
           const SerdNode* const    node,
           const SerdField          field,
           const SerdStatementFlags flags)
{
  switch (node->type) {
  case SERD_LITERAL:
    return write_literal(writer, node, flags);
  case SERD_URI:
    return write_uri_node(writer, node, field);
  case SERD_CURIE:
    return write_curie(writer, node);
  case SERD_BLANK:
    return write_blank(writer, node, field, flags);
  }

  return false;
}

static bool
is_resource(const SerdNode* node)
{
  return node && node->type > SERD_LITERAL;
}

static void
write_pred(SerdWriter* writer, SerdStatementFlags flags, const SerdNode* pred)
{
  write_node(writer, pred, SERD_PREDICATE, flags);
  write_sep(writer, SEP_P_O);
  serd_node_set(&writer->context.predicate, pred);
}

static bool
write_list_obj(SerdWriter* const        writer,
               const SerdStatementFlags flags,
               const SerdNode* const    predicate,
               const SerdNode* const    object)
{
  if (!strcmp(serd_node_string(object), NS_RDF "nil")) {
    write_sep(writer, SEP_LIST_END);
    return true;
  }

  if (!strcmp(serd_node_string(predicate), NS_RDF "first")) {
    write_node(writer, object, SERD_OBJECT, flags);
  } else {
    write_sep(writer, SEP_LIST_SEP);
  }

  return false;
}

static SerdStatus
serd_writer_write_statement(SerdWriter* const          writer,
                            const SerdStatementFlags   flags,
                            const SerdStatement* const statement)
{
  assert(!((flags & SERD_ANON_S) && (flags & SERD_LIST_S)));
  assert(!((flags & SERD_ANON_O) && (flags & SERD_LIST_O)));

  SerdStatus            st        = SERD_SUCCESS;
  const SerdNode* const subject   = serd_statement_subject(statement);
  const SerdNode* const predicate = serd_statement_predicate(statement);
  const SerdNode* const object    = serd_statement_object(statement);
  const SerdNode* const graph     = serd_statement_graph(statement);

  if (!is_resource(subject) || !is_resource(predicate) || !object) {
    return SERD_ERR_BAD_ARG;
  }

#define TRY(write_result)      \
  do {                         \
    if (!(write_result)) {     \
      return SERD_ERR_UNKNOWN; \
    }                          \
  } while (0)

  if (writer->syntax == SERD_NTRIPLES || writer->syntax == SERD_NQUADS) {
    TRY(write_node(writer, subject, SERD_SUBJECT, flags));
    sink(" ", 1, writer);
    TRY(write_node(writer, predicate, SERD_PREDICATE, flags));
    sink(" ", 1, writer);
    TRY(write_node(writer, object, SERD_OBJECT, flags));
    if (writer->syntax == SERD_NQUADS && graph) {
      sink(" ", 1, writer);
      TRY(write_node(writer, graph, SERD_GRAPH, flags));
    }
    sink(" .\n", 3, writer);
    return SERD_SUCCESS;
  }

  if ((graph && !serd_node_equals(graph, writer->context.graph)) ||
      (!graph && ctx(writer, SERD_GRAPH))) {
    if (ctx(writer, SERD_SUBJECT)) {
      write_sep(writer, SEP_END_S);
    }

    if (ctx(writer, SERD_GRAPH)) {
      write_sep(writer, SEP_GRAPH_END);
    }

    write_top_level_sep(writer);
    reset_context(writer, true);

    if (graph) {
      TRY(write_node(writer, graph, SERD_GRAPH, flags));
      write_sep(writer, SEP_GRAPH_BEGIN);
      serd_node_set(&writer->context.graph, graph);
    }
  }

  if (writer->context.type == CTX_LIST) {
    if (write_list_obj(writer, flags, predicate, object)) {
      // Reached end of list
      pop_context(writer);
      return SERD_SUCCESS;
    }
  } else if (serd_node_equals(subject, writer->context.subject)) {
    if (serd_node_equals(predicate, writer->context.predicate)) {
      // Abbreviate S P
      if (!writer->context.indented_object &&
          !(flags & (SERD_ANON_O | SERD_LIST_O))) {
        ++writer->indent;
        writer->context.indented_object = true;
      }

      write_sep(writer, SEP_END_O);
      write_node(writer, object, SERD_OBJECT, flags);
    } else {
      // Abbreviate S
      if (writer->context.indented_object && writer->indent > 0) {
        --writer->indent;
        writer->context.indented_object = false;
      }

      Sep sep = ctx(writer, SERD_PREDICATE) ? SEP_END_P : SEP_S_P;
      write_sep(writer, sep);
      write_pred(writer, flags, predicate);
      write_node(writer, object, SERD_OBJECT, flags);
    }
  } else {
    // No abbreviation
    if (writer->context.indented_object && writer->indent > 0) {
      --writer->indent;
      writer->context.indented_object = false;
    }

    if (serd_stack_is_empty(&writer->anon_stack)) {
      if (ctx(writer, SERD_SUBJECT)) {
        write_sep(writer, SEP_END_S); // Terminate last subject
      }
      write_top_level_sep(writer);
    }

    if (serd_stack_is_empty(&writer->anon_stack)) {
      write_node(writer, subject, SERD_SUBJECT, flags);
      if (!(flags & (SERD_ANON_S | SERD_LIST_S))) {
        write_sep(writer, SEP_S_P);
      } else if (flags & SERD_ANON_S) {
        write_sep(writer, SEP_ANON_S_P);
      }
    } else {
      write_sep(writer, SEP_ANON_S_P);
    }

    reset_context(writer, false);
    serd_node_set(&writer->context.subject, subject);

    if (!(flags & SERD_LIST_S)) {
      write_pred(writer, flags, predicate);
    }

    write_node(writer, object, SERD_OBJECT, flags);
  }

  // Push context for anonymous or list subject if necessary
  if (flags & (SERD_ANON_S | SERD_LIST_S)) {
    const bool is_list = (flags & SERD_LIST_S);

    const WriteContext ctx = {is_list ? CTX_LIST : CTX_BLANK,
                              serd_node_copy(graph),
                              serd_node_copy(subject),
                              is_list ? NULL : serd_node_copy(predicate),
                              false};
    if ((st = push_context(writer, ctx))) {
      return st;
    }
  }

  // Push context for anonymous or list object if necessary
  if (flags & (SERD_ANON_O | SERD_LIST_O)) {
    const bool is_list = (flags & SERD_LIST_O);

    const WriteContext ctx = {is_list ? CTX_LIST : CTX_BLANK,
                              serd_node_copy(graph),
                              serd_node_copy(object),
                              NULL,
                              false};
    if ((st = push_context(writer, ctx))) {
      serd_node_free(ctx.graph);
      serd_node_free(ctx.subject);
      return st;
    }
  }

  if (!(flags & (SERD_ANON_S | SERD_LIST_S | SERD_ANON_O | SERD_LIST_O))) {
    // Update current context to this statement
    serd_node_set(&writer->context.graph, graph);
    serd_node_set(&writer->context.subject, subject);
    serd_node_set(&writer->context.predicate, predicate);
  }

  return SERD_SUCCESS;
}

static SerdStatus
serd_writer_end_anon(SerdWriter* writer, const SerdNode* node)
{
  if (writer->syntax == SERD_NTRIPLES || writer->syntax == SERD_NQUADS) {
    return SERD_SUCCESS;
  }

  if (serd_stack_is_empty(&writer->anon_stack)) {
    return serd_world_errorf(writer->world,
                             SERD_ERR_UNKNOWN,
                             "unexpected end of anonymous node `%s'\n",
                             serd_node_string(node));
  }

  write_sep(writer, SEP_ANON_END);
  pop_context(writer);

  if (serd_node_equals(node, writer->context.subject)) {
    // Now-finished anonymous node is the new subject with no other context
    memset(writer->context.predicate, 0, sizeof(SerdNode));
  }

  return SERD_SUCCESS;
}

static SerdStatus
serd_writer_on_event(SerdWriter* writer, const SerdEvent* event)
{
  switch (event->type) {
  case SERD_BASE:
    return serd_writer_set_base_uri(writer, event->base.uri);
  case SERD_PREFIX:
    return serd_writer_set_prefix(
      writer, event->prefix.name, event->prefix.uri);
  case SERD_STATEMENT:
    return serd_writer_write_statement(
      writer, event->statement.flags, event->statement.statement);
  case SERD_END:
    return serd_writer_end_anon(writer, event->end.node);
  }

  return SERD_ERR_BAD_ARG;
}

SerdStatus
serd_writer_finish(SerdWriter* writer)
{
  if (ctx(writer, SERD_SUBJECT)) {
    write_sep(writer, SEP_END_S);
  }

  if (ctx(writer, SERD_GRAPH)) {
    write_sep(writer, SEP_GRAPH_END);
  }

  serd_byte_sink_flush(&writer->byte_sink);

  // Free any lingering contexts in case there was an error
  while (!serd_stack_is_empty(&writer->anon_stack)) {
    pop_context(writer);
  }

  free_context(writer);
  writer->indent  = 0;
  writer->context = WRITE_CONTEXT_NULL;
  return SERD_SUCCESS;
}

SerdWriter*
serd_writer_new(SerdWorld*      world,
                SerdSyntax      syntax,
                SerdWriterFlags flags,
                SerdEnv*        env,
                SerdWriteFunc   ssink,
                void*           stream)
{
  const WriteContext context = WRITE_CONTEXT_NULL;
  SerdWriter*        writer  = (SerdWriter*)calloc(1, sizeof(SerdWriter));

  writer->world      = world;
  writer->syntax     = syntax;
  writer->flags      = flags;
  writer->env        = env;
  writer->root_node  = NULL;
  writer->root_uri   = SERD_URI_NULL;
  writer->anon_stack = serd_stack_new(SERD_PAGE_SIZE);
  writer->context    = context;
  writer->empty      = true;
  writer->byte_sink  = serd_byte_sink_new(
    ssink, stream, (flags & SERD_WRITE_BULK) ? SERD_PAGE_SIZE : 1);

  writer->iface.handle   = writer;
  writer->iface.on_event = (SerdEventFunc)serd_writer_on_event;

  return writer;
}

void
serd_writer_chop_blank_prefix(SerdWriter* writer, const char* prefix)
{
  free(writer->bprefix);
  writer->bprefix_len = 0;
  writer->bprefix     = NULL;

  const size_t prefix_len = prefix ? strlen(prefix) : 0;
  if (prefix_len) {
    writer->bprefix_len = prefix_len;
    writer->bprefix     = (char*)malloc(writer->bprefix_len + 1);
    memcpy(writer->bprefix, prefix, writer->bprefix_len + 1);
  }
}

static SerdStatus
serd_writer_set_base_uri(SerdWriter* writer, const SerdNode* uri)
{
  if (serd_node_equals(serd_env_base_uri(writer->env), uri)) {
    return SERD_SUCCESS;
  }

  if (uri && serd_node_type(uri) != SERD_URI) {
    return SERD_ERR_BAD_ARG;
  }

  const SerdStringView uri_string =
    uri ? serd_node_string_view(uri) : serd_empty_string();

  SerdStatus st = SERD_SUCCESS;
  if (!(st = serd_env_set_base_uri(writer->env, uri_string))) {
    if (writer->syntax == SERD_TURTLE || writer->syntax == SERD_TRIG) {
      if (ctx(writer, SERD_GRAPH) || ctx(writer, SERD_SUBJECT)) {
        sink(" .\n\n", 4, writer);
        reset_context(writer, true);
      }

      if (uri) {
        sink("@base <", 7, writer);
        sink(serd_node_string(uri), uri->length, writer);
        sink("> .\n", 4, writer);
      }
    }
  }

  writer->indent = 0;
  reset_context(writer, true);
  return st;
}

SerdStatus
serd_writer_set_root_uri(SerdWriter* writer, const SerdNode* uri)
{
  serd_node_free(writer->root_node);
  writer->root_node = NULL;
  writer->root_uri  = SERD_URI_NULL;

  if (uri) {
    writer->root_node = serd_node_copy(uri);
    writer->root_uri  = serd_node_uri_view(writer->root_node);
  }

  return SERD_SUCCESS;
}

SerdStatus
serd_writer_set_prefix(SerdWriter*     writer,
                       const SerdNode* name,
                       const SerdNode* uri)
{
  SerdStatus st = SERD_SUCCESS;

  if (name->type != SERD_LITERAL || uri->type != SERD_URI) {
    return SERD_ERR_BAD_ARG;
  }

  if ((st = serd_env_set_prefix(writer->env,
                                serd_node_string_view(name),
                                serd_node_string_view(uri)))) {
    return st;
  }

  if (writer->syntax == SERD_TURTLE || writer->syntax == SERD_TRIG) {
    if (ctx(writer, SERD_GRAPH) || ctx(writer, SERD_SUBJECT)) {
      sink(" .\n\n", 4, writer);
      reset_context(writer, true);
    }
    sink("@prefix ", 8, writer);
    sink(serd_node_string(name), name->length, writer);
    sink(": <", 3, writer);
    write_uri_from_node(writer, uri);
    sink("> .\n", 4, writer);
  }

  writer->indent = 0;
  return reset_context(writer, true);
}

void
serd_writer_free(SerdWriter* writer)
{
  if (!writer) {
    return;
  }

  serd_writer_finish(writer);
  serd_stack_free(&writer->anon_stack);
  free(writer->bprefix);
  serd_byte_sink_free(&writer->byte_sink);
  serd_node_free(writer->root_node);
  free(writer);
}

const SerdSink*
serd_writer_sink(SerdWriter* writer)
{
  return &writer->iface;
}

size_t
serd_buffer_sink(const void* const buf,
                 const size_t      size,
                 const size_t      nmemb,
                 void* const       stream)
{
  assert(size == 1);
  (void)size;

  SerdBuffer* buffer = (SerdBuffer*)stream;
  buffer->buf        = (char*)realloc(buffer->buf, buffer->len + nmemb);
  memcpy((uint8_t*)buffer->buf + buffer->len, buf, nmemb);
  buffer->len += nmemb;
  return nmemb;
}

char*
serd_buffer_sink_finish(SerdBuffer* const stream)
{
  serd_buffer_sink("", 1, 1, stream);
  return (char*)stream->buf;
}
