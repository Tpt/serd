// Copyright 2018 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#undef NDEBUG

#include "serd/serd.h"

#include <assert.h>
#include <stdio.h>

static const size_t min_stack_size = 4U * sizeof(size_t) + 256U;
static const size_t max_stack_size = 1024U;

static SerdStatus
test_size(SerdWorld* const  world,
          const char* const str,
          const SerdSyntax  syntax,
          const size_t      stack_size)
{
  SerdSink*         sink   = serd_sink_new(NULL, NULL);
  SerdReader* const reader = serd_reader_new(world, syntax, sink, stack_size);
  assert(reader);

  serd_reader_start_string(reader, str);
  const SerdStatus st = serd_reader_read_document(reader);
  serd_reader_free(reader);
  serd_sink_free(sink);

  return st;
}

static void
test_all_sizes(SerdWorld* const  world,
               const char* const str,
               const SerdSyntax  syntax)
{
  // Ensure reading with the maximum stack size succeeds
  SerdStatus st = test_size(world, str, syntax, max_stack_size);
  assert(!st);

  // Test with an increasingly smaller stack
  for (size_t size = max_stack_size; size > min_stack_size; --size) {
    if ((st = test_size(world, str, syntax, size))) {
      assert(st == SERD_ERR_OVERFLOW);
    }
  }

  assert(st == SERD_ERR_OVERFLOW);
}

static void
test_ntriples_overflow(void)
{
  static const char* const test_strings[] = {
    "<http://example.org/s> <http://example.org/p> <http://example.org/o> .",
    NULL,
  };

  SerdWorld* const world = serd_world_new();

  for (const char* const* t = test_strings; *t; ++t) {
    test_all_sizes(world, *t, SERD_NTRIPLES);
  }

  serd_world_free(world);
}

static void
test_turtle_overflow(void)
{
  static const char* const test_strings[] = {
    "<http://example.org/s> <http://example.org/p> <http://example.org/> .",
    "<http://example.org/s> <http://example.org/p> "
    "<thisisanabsurdlylongurischeme://because/testing/> .",
    "<http://example.org/s> <http://example.org/p> 1234 .",
    "<http://example.org/s> <http://example.org/p> (1 2 3 4) .",
    "<http://example.org/s> <http://example.org/p> ((((((((42)))))))) .",
    "<http://example.org/s> <http://example.org/p> \"literal\" .",
    "<http://example.org/s> <http://example.org/p> _:blank .",
    "<http://example.org/s> <http://example.org/p> true .",
    "<http://example.org/s> <http://example.org/p> \"\"@en .",
    "(((((((((42))))))))) <http://example.org/p> <http://example.org/o> .",
    "@prefix eg: <http://example.org/ns/test> .",
    "@base <http://example.org/base> .",

    "@prefix eg: <http://example.org/> . \neg:s eg:p eg:o .\n",

    "@prefix ug.dot: <http://example.org/> . \nug.dot:s ug.dot:p ug.dot:o .\n",

    // NOLINTNEXTLINE(bugprone-suspicious-missing-comma)
    "@prefix øøøøøøøøø: <http://example.org/long> . \n"
    "<http://example.org/somewhatlongsubjecttooffsetthepredicate> øøøøøøøøø:p "
    "øøøøøøøøø:o .\n",

    // NOLINTNEXTLINE(bugprone-suspicious-missing-comma)
    "<http://example.org/subject/with/a/long/path> "
    "<http://example.org/predicate/with/a/long/path> "
    "<http://example.org/object/with/a/long/path> .",

    // NOLINTNEXTLINE(bugprone-suspicious-missing-comma)
    "<http://example.org/s> <http://example.org/p> "
    "\"typed\"^^<http://example.org/Datatype> .",

    // NOLINTNEXTLINE(bugprone-suspicious-missing-comma)
    "@prefix eg: <http://example.org/ns/test> .\n"
    "<http://example.org/s> <http://example.org/p> "
    "\"typed\"^^eg:Datatype .",

    // NOLINTNEXTLINE(bugprone-suspicious-missing-comma)
    "@prefix eg: <http://example.org/ns/test> .\n"
    "<http://example.org/s> <http://example.org/p> eg:foo .",

    // NOLINTNEXTLINE(bugprone-suspicious-missing-comma)
    "@prefix prefix: <http://example.org/testing/curies> .\n"
    "prefix:subject prefix:predicate prefix:object .\n",

    // NOLINTNEXTLINE(bugprone-suspicious-missing-comma)
    "@prefix prefix: <http://example.org/testing/curies> .\n"
    "prefix:subjectthatwillcomearoundtobeingfinishedanycharacternow "
    "prefix:predicate prefix:object .\n",

    // NOLINTNEXTLINE(bugprone-suspicious-missing-comma)
    "@prefix eg: <http://example.org/> .\n"
    "eg:s eg:p [ eg:p [ eg:p [ eg:p [ eg:p eg:o ] ] ] ] .\n",

    // NOLINTNEXTLINE(bugprone-suspicious-missing-comma)
    "@prefix eg: <http://example.org/> .\n"
    "eg:s eg:p ( 1 2 3 ( 4 5 6 ( 7 8 9 ) ) ) .\n",

    // NOLINTNEXTLINE(bugprone-suspicious-missing-comma)
    "@prefix eg: <http://example.org/ns/test> .\n"
    "<http://example.org/s> <http://example.org/p> eg:%99 .",

    // NOLINTNEXTLINE(bugprone-suspicious-missing-comma)
    "@prefix øøøøøøøøø: <http://example.org/long> .\n"
    "<http://example.org/somewhatlongsubjecttooffsetthepredicate> øøøøøøøøø:p "
    "øøøøøøøøø:o .\n",

    // NOLINTNEXTLINE(bugprone-suspicious-missing-comma)
    "@base <http://example.org/ns/test> .\n"
    "<http://example.org/s> <http://example.org/p> <rel> .",

    NULL,
  };

  SerdWorld* const world = serd_world_new();

  for (const char* const* t = test_strings; *t; ++t) {
    test_all_sizes(world, *t, SERD_TURTLE);
  }

  serd_world_free(world);
}

int
main(void)
{
  test_ntriples_overflow();
  test_turtle_overflow();
  return 0;
}
