// Copyright 2011-2022 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#ifndef SERD_READER_H
#define SERD_READER_H

#include "serd/attributes.h"
#include "serd/env.h"
#include "serd/input_stream.h"
#include "serd/sink.h"
#include "serd/syntax.h"
#include "serd/world.h"

#include <stdbool.h>
#include <stddef.h>

SERD_BEGIN_DECLS

/**
   @defgroup serd_reader Reader
   @ingroup serd_reading_writing
   @{
*/

/// Streaming parser that reads a text stream and writes to a statement sink
typedef struct SerdReaderImpl SerdReader;

/// Reader options
typedef enum {
  /**
     Tolerate invalid input where possible.

     This will attempt to ignore invalid input and continue reading.  Invalid
     Unicode characters will be replaced with the replacement character, and
     various other syntactic problems will be ignored.  If there are more
     severe problems, the reader will try to skip the statement and continue
     parsing.  This should work reasonably well for line-based syntaxes like
     NTriples and NQuads, but abbreviated Turtle or TriG may not recover.

     Note that this flag should be used carefully, since it can result in data
     loss.
  */
  SERD_READ_LAX = 1U << 0U,

  /**
     Support reading variable nodes.

     As an extension, serd supports reading variables nodes with SPARQL-like
     syntax, for example "?foo" or "$bar".  This can be used for storing
     graph patterns and templates.
  */
  SERD_READ_VARIABLES = 1U << 1U,

  /**
     Read relative URI references exactly without resolving them.

     Normally, the reader expands all relative URIs against the base URI.  This
     flag disables that, so that URI references are passed to the sink exactly
     as they are in the input.
  */
  SERD_READ_RELATIVE = 1U << 2U,

  /**
     Read blank node labels without adding a prefix unique to the document.

     Normally, the reader adds a prefix like "f1", "f2", and so on, to blank
     node labels, to separate the namespaces from separate input documents.
     This flag disables that, so that blank node labels will be read without
     any prefix added.

     Note that this flag should be used carefully, since it can result in data
     corruption.  Specifically, if data from separate documents parsed with
     this flag is combined, the IDs from each document may clash.
  */
  SERD_READ_GLOBAL = 1U << 3U,

  /**
     Read generated blank node labels exactly without adjusting them.

     Normally, the reader will adapt blank node labels in the input that clash
     with its scheme for generating new ones, for example mapping "_:b123" to
     "_:B123".  This flag disables that, so that blank node labels are passed
     to the sink exactly as they are in the input.

     Note that this flag should be used carefully, since it can result in data
     corruption.  Specifically, if the input is a syntax like Turtle with
     anonymous nodes, the generated IDs for those nodes may clash with IDs from
     the input document.
  */
  SERD_READ_GENERATED = 1U << 4U,
} SerdReaderFlag;

/// Bitwise OR of SerdReaderFlag values
typedef uint32_t SerdReaderFlags;

/// Create a new RDF reader
SERD_API
SerdReader* SERD_ALLOCATED
serd_reader_new(SerdWorld* SERD_NONNULL      world,
                SerdSyntax                   syntax,
                SerdReaderFlags              flags,
                SerdEnv* SERD_NONNULL        env,
                const SerdSink* SERD_NONNULL sink,
                size_t                       stack_size);

/**
   Prepare to read some input.

   This sets up the reader to read from the given input, but will not read any
   bytes from it.  This should be followed by serd_reader_read_chunk() or
   serd_reader_read_document() to actually read the input.

   @param reader The reader.
   @param input An opened input stream to read from.
   @param input_name The name of the input stream for error messages.
   @param block_size The number of bytes to read from the stream at once.
*/
SERD_API
SerdStatus
serd_reader_start(SerdReader* SERD_NONNULL      reader,
                  SerdInputStream* SERD_NONNULL input,
                  const SerdNode* SERD_NULLABLE input_name,
                  size_t                        block_size);

/**
   Read a single "chunk" of data during an incremental read.

   This function will read a single top level description, and return.  This
   may be a directive, statement, or several statements; essentially it reads
   until a '.' is encountered.  This is particularly useful for reading
   directly from a pipe or socket.
*/
SERD_API
SerdStatus
serd_reader_read_chunk(SerdReader* SERD_NONNULL reader);

/**
   Read a complete document from the source.

   This function will continue pulling from the source until a complete
   document has been read.  Note that this may block when used with streams,
   for incremental reading use serd_reader_read_chunk().
*/
SERD_API
SerdStatus
serd_reader_read_document(SerdReader* SERD_NONNULL reader);

/**
   Finish reading from the source.

   This should be called before starting to read from another source.
*/
SERD_API
SerdStatus
serd_reader_finish(SerdReader* SERD_NONNULL reader);

/**
   Skip over bytes in the input until a specific byte is encountered.

   Typically used for recording from errors in a line-based syntax by skipping
   ahead to the next newline.

   @return #SERD_SUCCESS if the given byte was reached, or #SERD_FAILURE if the
   end of input is reached.
*/
SERD_API
SerdStatus
serd_reader_skip_until_byte(SerdReader* SERD_NONNULL reader, uint8_t byte);

/**
   Free `reader`.

   The reader will be finished via `serd_reader_finish()` if necessary.
*/
SERD_API
void
serd_reader_free(SerdReader* SERD_NULLABLE reader);

/**
   @}
*/

SERD_END_DECLS

#endif // SERD_READER_H
