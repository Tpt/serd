// Copyright 2011-2022 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#ifndef SERD_NODE_H
#define SERD_NODE_H

#include "serd/attributes.h"
#include "serd/uri.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

SERD_BEGIN_DECLS

/**
   @defgroup serd_node Node
   @ingroup serd
   @{
*/

/**
   An RDF node.

   A node is a single contiguous chunk of data, but the representation is
   opaque to the public.
*/
typedef struct SerdNodeImpl SerdNode;

/**
   Type of a node.

   An abstract RDF node can be either a resource or a literal.  This type is
   more precise to preserve syntactic differences and support additional
   features.
*/
typedef enum {
  /**
     Literal value.

     A literal optionally has either a language, or a datatype (not both).
  */
  SERD_LITERAL = 1,

  /**
     URI (absolute or relative).

     Value is an unquoted URI string, which is either a relative reference
     with respect to the current base URI (e.g. "foo/bar"), or an absolute
     URI (e.g. "http://example.org/foo").
     @see [RFC3986](http://tools.ietf.org/html/rfc3986)
  */
  SERD_URI = 2,

  /**
     CURIE, a shortened URI.

     Value is an unquoted CURIE string relative to the current environment,
     e.g. "rdf:type".  @see [CURIE Syntax 1.0](http://www.w3.org/TR/curie)
  */
  SERD_CURIE = 3,

  /**
     A blank node.

     Value is a blank node ID without any syntactic prefix, like "id3", which
     is meaningful only within this serialisation.  @see [RDF 1.1
     Turtle](http://www.w3.org/TR/turtle/#grammar-production-BLANK_NODE_LABEL)
  */
  SERD_BLANK = 4,
} SerdNodeType;

/// Node flags, which ORed together make a #SerdNodeFlags
typedef enum {
  SERD_HAS_NEWLINE  = 1U << 0U, ///< Contains line breaks ('\\n' or '\\r')
  SERD_HAS_QUOTE    = 1U << 1U, ///< Contains quotes ('"')
  SERD_HAS_DATATYPE = 1U << 2U, ///< Literal node has datatype
  SERD_HAS_LANGUAGE = 1U << 3U, ///< Literal node has language
} SerdNodeFlag;

/// Bitwise OR of SerdNodeFlag values
typedef uint32_t SerdNodeFlags;

/**
   Create a new "token" node that is just a string.

   "Token" is just a shorthand used in this API to refer to a node that is not
   a typed or tagged literal.  This can be used to create URIs, blank nodes,
   CURIEs, and simple string literals.
*/
SERD_API
SerdNode* SERD_ALLOCATED
serd_new_token(SerdNodeType type, SerdStringView string);

/// Create a new plain literal string node from `str`
SERD_API
SerdNode* SERD_ALLOCATED
serd_new_string(SerdStringView string);

/**
   Create a new literal node from `str`.

   Either `datatype_uri` or `lang` can be given, but not both, unless
   `datatype_uri` is rdf:langString in which case it is ignored.
*/
SERD_API
SerdNode* SERD_ALLOCATED
serd_new_literal(SerdStringView string,
                 SerdStringView datatype_uri,
                 SerdStringView lang);

/// Create a new blank node
SERD_API
SerdNode* SERD_ALLOCATED
serd_new_blank(SerdStringView string);

/// Create a new CURIE node
SERD_API
SerdNode* SERD_ALLOCATED
serd_new_curie(SerdStringView string);

/// Create a new URI node
SERD_API
SerdNode* SERD_ALLOCATED
serd_new_uri(SerdStringView string);

/// Create a new URI from a URI view
SERD_API
SerdNode* SERD_ALLOCATED
serd_new_parsed_uri(SerdURIView uri);

/**
   Create a new file URI node from a file system path and optional hostname.

   Backslashes in Windows paths will be converted, and other characters will be
   percent encoded as necessary.

   If `path` is relative, `hostname` is ignored.
*/
SERD_API
SerdNode* SERD_ALLOCATED
serd_new_file_uri(SerdStringView path, SerdStringView hostname);

/**
   Create a new node by serialising `d` into an xsd:decimal string.

   The resulting node will always contain a '.', start with a digit, and end
   with a digit (i.e. will have a leading and/or trailing '0' if necessary).
   It will never be in scientific notation.  A maximum of `frac_digits` digits
   will be written after the decimal point, but trailing zeros will
   automatically be omitted (except one if `d` is a round integer).

   Note that about 16 and 8 fractional digits are required to precisely
   represent a double and float, respectively.

   @param d The value for the new node.
   @param frac_digits The maximum number of digits after the decimal place.
   @param datatype Datatype of node, or NULL for xsd:decimal.
*/
SERD_API
SerdNode* SERD_ALLOCATED
serd_new_decimal(double                        d,
                 unsigned                      frac_digits,
                 const SerdNode* SERD_NULLABLE datatype);

/**
   Create a new node by serialising `i` into an xsd:integer string.

   @param i Integer value to serialise.
   @param datatype Datatype of node, or NULL for xsd:integer.
*/
SERD_API
SerdNode* SERD_ALLOCATED
serd_new_integer(int64_t i, const SerdNode* SERD_NULLABLE datatype);

/**
   Create a node by serialising `buf` into an xsd:base64Binary string.

   This function can be used to make a serialisable node out of arbitrary
   binary data, which can be decoded using serd_base64_decode().

   @param buf Raw binary input data.
   @param size Size of `buf`.
   @param wrap_lines Wrap lines at 76 characters to conform to RFC 2045.
   @param datatype Datatype of node, or NULL for xsd:base64Binary.
*/
SERD_API
SerdNode* SERD_ALLOCATED
serd_new_blob(const void* SERD_NONNULL      buf,
              size_t                        size,
              bool                          wrap_lines,
              const SerdNode* SERD_NULLABLE datatype);

/// Return a deep copy of `node`
SERD_API
SerdNode* SERD_ALLOCATED
serd_node_copy(const SerdNode* SERD_NULLABLE node);

/// Free any data owned by `node`
SERD_API
void
serd_node_free(SerdNode* SERD_NULLABLE node);

/// Return the type of a node
SERD_PURE_API
SerdNodeType
serd_node_type(const SerdNode* SERD_NONNULL node);

/// Return the string contents of a node
SERD_CONST_API
const char* SERD_NONNULL
serd_node_string(const SerdNode* SERD_NONNULL node);

/// Return the length of the node's string in bytes (excluding terminator)
SERD_PURE_API
size_t
serd_node_length(const SerdNode* SERD_NULLABLE node);

/**
   Return a view of the string in a node.

   This is a convenience wrapper for serd_node_string() and serd_node_length()
   that can be used to get both in a single call.
*/
SERD_PURE_API
SerdStringView
serd_node_string_view(const SerdNode* SERD_NONNULL node);

/**
   Return a parsed view of the URI in a node.

   It is best to check the node type before calling this function, though it is
   safe to call on non-URI nodes.  In that case, it will return a null view
   with all fields zero.

   Note that this parses the URI string contained in the node, so it is a good
   idea to keep the value if you will be using it several times in the same
   scope.
*/
SERD_PURE_API
SerdURIView
serd_node_uri_view(const SerdNode* SERD_NONNULL node);

/// Return the additional flags of a node
SERD_PURE_API
SerdNodeFlags
serd_node_flags(const SerdNode* SERD_NONNULL node);

/**
   Return the optional datatype of a literal node.

   The datatype, if present, is always a URI, typically something like
   <http://www.w3.org/2001/XMLSchema#boolean>.
*/
SERD_PURE_API
const SerdNode* SERD_NULLABLE
serd_node_datatype(const SerdNode* SERD_NONNULL node);

/**
   Return the optional language tag of a literal node.

   The language tag, if present, is a well-formed BCP 47 (RFC 4647) language
   tag like "en-ca".  Note that these must be handled case-insensitively, for
   example, the common form "en-CA" is valid, but lowercase is considered
   canonical here.
*/
SERD_PURE_API
const SerdNode* SERD_NULLABLE
serd_node_language(const SerdNode* SERD_NONNULL node);

/**
   Return true iff `a` is equal to `b`.

   For convenience, either argument may be null, which isn't considered equal
   to any node.

   @return True if `a` and `b` point to equal nodes, or are both null.
*/
SERD_PURE_API
bool
serd_node_equals(const SerdNode* SERD_NULLABLE a,
                 const SerdNode* SERD_NULLABLE b);

/**
   @}
*/

SERD_END_DECLS

#endif // SERD_NODE_H
