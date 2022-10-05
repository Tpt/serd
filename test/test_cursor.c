// Copyright 2011-2021 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#undef NDEBUG

#include "serd/serd.h"

#include <assert.h>
#include <stddef.h>

static void
test_copy(void)
{
  assert(!serd_cursor_copy(NULL, NULL));

  SerdWorld* const  world = serd_world_new(NULL);
  SerdModel* const  model = serd_model_new(world, SERD_ORDER_SPO, 0U);
  SerdCursor* const begin = serd_model_begin(model);
  SerdCursor* const copy  = serd_cursor_copy(NULL, begin);

  assert(serd_cursor_equals(copy, begin));

  serd_cursor_free(copy);
  serd_cursor_free(begin);
  serd_model_free(model);
  serd_world_free(world);
}

static void
test_comparison(void)
{
  SerdWorld* const world = serd_world_new(NULL);
  SerdNodes* const nodes = serd_world_nodes(world);
  SerdModel* const model = serd_model_new(world, SERD_ORDER_SPO, 0U);

  serd_model_add_index(model, SERD_ORDER_OPS);

  const SerdNode* const a =
    serd_nodes_uri(nodes, serd_string("http://example.org/a"));
  const SerdNode* const b =
    serd_nodes_uri(nodes, serd_string("http://example.org/b"));
  const SerdNode* const c =
    serd_nodes_uri(nodes, serd_string("http://example.org/c"));

  // Add a single statement
  assert(!serd_model_add(model, a, b, c, NULL));

  // Make cursors that point to the statement but via different patterns
  SerdCursor* const c1 = serd_model_find(model, a, NULL, NULL, NULL);
  SerdCursor* const c2 = serd_model_find(model, a, b, NULL, NULL);
  SerdCursor* const c3 = serd_model_find(model, NULL, b, c, NULL);

  // Ensure that they refer to the same statement but are not equal
  assert(serd_cursor_get(c1) == serd_cursor_get(c2));
  assert(serd_cursor_get(c2) == serd_cursor_get(c3));
  assert(!serd_cursor_equals(c1, c2));
  assert(!serd_cursor_equals(c2, c3));
  assert(!serd_cursor_equals(c1, c3));

  // Check that none are equal to begin (which has a different mode) or end
  SerdCursor* const begin = serd_model_begin(model);
  assert(!serd_cursor_equals(c1, begin));
  assert(!serd_cursor_equals(c2, begin));
  assert(!serd_cursor_equals(c3, begin));
  assert(!serd_cursor_equals(c1, serd_model_end(model)));
  assert(!serd_cursor_equals(c2, serd_model_end(model)));
  assert(!serd_cursor_equals(c3, serd_model_end(model)));
  serd_cursor_free(begin);

  // Check that a cursor that points to it via the same pattern is equal
  SerdCursor* const c4 = serd_model_find(model, a, b, NULL, NULL);
  assert(serd_cursor_get(c4) == serd_cursor_get(c1));
  assert(serd_cursor_equals(c4, c2));
  assert(!serd_cursor_equals(c4, c3));
  serd_cursor_free(c4);

  // Advance everything to the end
  assert(serd_cursor_advance(c1) == SERD_FAILURE);
  assert(serd_cursor_advance(c2) == SERD_FAILURE);
  assert(serd_cursor_advance(c3) == SERD_FAILURE);

  // Check that they are now equal, and equal to the model's end
  assert(serd_cursor_equals(c1, c2));
  assert(serd_cursor_equals(c1, serd_model_end(model)));
  assert(serd_cursor_equals(c2, serd_model_end(model)));

  serd_cursor_free(c3);
  serd_cursor_free(c2);
  serd_cursor_free(c1);
  serd_model_free(model);
  serd_world_free(world);
}

int
main(void)
{
  test_copy();
  test_comparison();

  return 0;
}
