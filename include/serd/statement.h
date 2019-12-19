// Copyright 2011-2022 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#ifndef SERD_STATEMENT_H
#define SERD_STATEMENT_H

#include "serd/attributes.h"

#include <stdint.h>

SERD_BEGIN_DECLS

/**
   @defgroup serd_statement Statements
   @ingroup serd
   @{
*/

/// Index of a node in a statement
typedef enum {
  SERD_SUBJECT   = 0U, ///< Subject
  SERD_PREDICATE = 1U, ///< Predicate ("key")
  SERD_OBJECT    = 2U, ///< Object ("value")
  SERD_GRAPH     = 3U, ///< Graph ("context")
} SerdField;

/// Flags indicating inline abbreviation information for a statement
typedef enum {
  SERD_EMPTY_S      = 1U << 1U, ///< Empty blank node subject
  SERD_EMPTY_O      = 1U << 2U, ///< Empty blank node object
  SERD_ANON_S_BEGIN = 1U << 3U, ///< Start of anonymous subject
  SERD_ANON_O_BEGIN = 1U << 4U, ///< Start of anonymous object
  SERD_ANON_CONT    = 1U << 5U, ///< Continuation of anonymous node
  SERD_LIST_S_BEGIN = 1U << 6U, ///< Start of list subject
  SERD_LIST_O_BEGIN = 1U << 7U, ///< Start of list object
  SERD_LIST_CONT    = 1U << 8U, ///< Continuation of list
} SerdStatementFlag;

/// Bitwise OR of SerdStatementFlag values
typedef uint32_t SerdStatementFlags;

/**
   @}
*/

SERD_END_DECLS

#endif // SERD_STATEMENT_H
