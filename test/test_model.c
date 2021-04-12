// Copyright 2011-2021 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#undef NDEBUG

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "serd/serd.h"

#define WILDCARD_NODE NULL

#define NS_RDF "http://www.w3.org/1999/02/22-rdf-syntax-ns#"
#define RDF_FIRST NS_RDF "first"
#define RDF_REST NS_RDF "rest"
#define RDF_NIL NS_RDF "nil"

#define N_OBJECTS_PER 2U

typedef const SerdNode* Quad[4];

typedef struct {
  Quad query;
  int  expected_num_results;
} QueryTest;

static const SerdNode*
uri(SerdWorld* world, const unsigned num)
{
  char str[] = "http://example.org/0000000000";

  snprintf(str + 19, 11, "%010u", num);

  return serd_nodes_uri(serd_world_nodes(world), serd_string(str));
}

static int
generate(SerdWorld*      world,
         SerdModel*      model,
         size_t          n_quads,
         const SerdNode* graph)
{
  SerdNodes* nodes = serd_world_nodes(world);
  SerdStatus st    = SERD_SUCCESS;

  for (unsigned i = 0; i < n_quads; ++i) {
    unsigned num = (i * N_OBJECTS_PER) + 1U;

    const SerdNode* ids[2 + N_OBJECTS_PER];
    for (unsigned j = 0; j < 2 + N_OBJECTS_PER; ++j) {
      ids[j] = uri(world, num++);
    }

    for (unsigned j = 0; j < N_OBJECTS_PER; ++j) {
      st = serd_model_add(model, ids[0], ids[1], ids[2 + j], graph);
      assert(!st);
    }
  }

  // Add some literals

  // (98 4 "hello") and (98 4 "hello"^^<5>)
  const SerdNode* hello = serd_nodes_string(nodes, serd_string("hello"));

  const SerdNode* hello_gb = serd_nodes_literal(
    nodes, serd_string("hello"), SERD_HAS_LANGUAGE, serd_string("en-gb"));

  const SerdNode* hello_us = serd_nodes_literal(
    nodes, serd_string("hello"), SERD_HAS_LANGUAGE, serd_string("en-us"));

  const SerdNode* hello_t4 =
    serd_nodes_literal(nodes,
                       serd_string("hello"),
                       SERD_HAS_DATATYPE,
                       serd_node_string_view(uri(world, 4)));

  const SerdNode* hello_t5 =
    serd_nodes_literal(nodes,
                       serd_string("hello"),
                       SERD_HAS_DATATYPE,
                       serd_node_string_view(uri(world, 5)));

  assert(!serd_model_add(model, uri(world, 98), uri(world, 4), hello, graph));
  assert(
    !serd_model_add(model, uri(world, 98), uri(world, 4), hello_t5, graph));

  // (96 4 "hello"^^<4>) and (96 4 "hello"^^<5>)
  assert(
    !serd_model_add(model, uri(world, 96), uri(world, 4), hello_t4, graph));
  assert(
    !serd_model_add(model, uri(world, 96), uri(world, 4), hello_t5, graph));

  // (94 5 "hello") and (94 5 "hello"@en-gb)
  assert(!serd_model_add(model, uri(world, 94), uri(world, 5), hello, graph));
  assert(
    !serd_model_add(model, uri(world, 94), uri(world, 5), hello_gb, graph));

  // (92 6 "hello"@en-us) and (92 6 "hello"@en-gb)
  assert(
    !serd_model_add(model, uri(world, 92), uri(world, 6), hello_us, graph));
  assert(
    !serd_model_add(model, uri(world, 92), uri(world, 6), hello_gb, graph));

  // (14 6 "bonjour"@fr) and (14 6 "salut"@fr)
  const SerdNode* const bonjour = serd_nodes_literal(
    nodes, serd_string("bonjour"), SERD_HAS_LANGUAGE, serd_string("fr"));

  const SerdNode* const salut = serd_nodes_literal(
    nodes, serd_string("salut"), SERD_HAS_LANGUAGE, serd_string("fr"));

  assert(!serd_model_add(model, uri(world, 14), uri(world, 6), bonjour, graph));
  assert(!serd_model_add(model, uri(world, 14), uri(world, 6), salut, graph));

  // Attempt to add duplicates
  assert(serd_model_add(model, uri(world, 14), uri(world, 6), salut, graph));

  // Add a blank node subject
  const SerdNode* ablank = serd_nodes_blank(nodes, serd_string("ablank"));

  assert(!serd_model_add(model, ablank, uri(world, 6), salut, graph));

  // Add statement with URI object
  assert(!serd_model_add(model, ablank, uri(world, 6), uri(world, 7), graph));

  return EXIT_SUCCESS;
}

static int
test_read(SerdWorld*      world,
          SerdModel*      model,
          const SerdNode* g,
          const unsigned  n_quads)
{
  SerdNodes* const nodes = serd_nodes_new();

  SerdCursor*          cursor = serd_model_begin(model);
  const SerdStatement* prev   = NULL;
  for (; !serd_cursor_equals(cursor, serd_model_end(model));
       serd_cursor_advance(cursor)) {
    const SerdStatement* statement = serd_cursor_get(cursor);
    assert(statement);
    assert(serd_statement_subject(statement));
    assert(serd_statement_predicate(statement));
    assert(serd_statement_object(statement));
    assert(!serd_statement_equals(statement, prev));
    assert(!serd_statement_equals(prev, statement));
    prev = statement;
  }

  // Attempt to increment past end
  assert(serd_cursor_advance(cursor) == SERD_ERR_BAD_CURSOR);
  serd_cursor_free(cursor);

  const SerdStringView s = serd_string("hello");

  const SerdNode* plain_hello = serd_nodes_string(nodes, s);

  const SerdNode* type4_hello = serd_nodes_literal(
    nodes, s, SERD_HAS_DATATYPE, serd_node_string_view(uri(world, 4)));

  const SerdNode* type5_hello = serd_nodes_literal(
    nodes, s, SERD_HAS_DATATYPE, serd_node_string_view(uri(world, 5)));

  const SerdNode* gb_hello =
    serd_nodes_literal(nodes, s, SERD_HAS_LANGUAGE, serd_string("en-gb"));

  const SerdNode* us_hello =
    serd_nodes_literal(nodes, s, SERD_HAS_LANGUAGE, serd_string("en-us"));

#define NUM_PATTERNS 18

  QueryTest patterns[NUM_PATTERNS] = {
    {{NULL, NULL, NULL}, (int)(n_quads * N_OBJECTS_PER) + 12},
    {{uri(world, 1), WILDCARD_NODE, WILDCARD_NODE}, 2},
    {{uri(world, 9), uri(world, 9), uri(world, 9)}, 0},
    {{uri(world, 1), uri(world, 2), uri(world, 4)}, 1},
    {{uri(world, 3), uri(world, 4), WILDCARD_NODE}, 2},
    {{WILDCARD_NODE, uri(world, 2), uri(world, 4)}, 1},
    {{WILDCARD_NODE, WILDCARD_NODE, uri(world, 4)}, 1},
    {{uri(world, 1), WILDCARD_NODE, WILDCARD_NODE}, 2},
    {{uri(world, 1), WILDCARD_NODE, uri(world, 4)}, 1},
    {{WILDCARD_NODE, uri(world, 2), WILDCARD_NODE}, 2},
    {{uri(world, 98), uri(world, 4), plain_hello}, 1},
    {{uri(world, 98), uri(world, 4), type5_hello}, 1},
    {{uri(world, 96), uri(world, 4), type4_hello}, 1},
    {{uri(world, 96), uri(world, 4), type5_hello}, 1},
    {{uri(world, 94), uri(world, 5), plain_hello}, 1},
    {{uri(world, 94), uri(world, 5), gb_hello}, 1},
    {{uri(world, 92), uri(world, 6), gb_hello}, 1},
    {{uri(world, 92), uri(world, 6), us_hello}, 1}};

  Quad match = {uri(world, 1), uri(world, 2), uri(world, 4), g};
  assert(serd_model_ask(model, match[0], match[1], match[2], match[3]));

  Quad nomatch = {uri(world, 1), uri(world, 2), uri(world, 9), g};
  assert(
    !serd_model_ask(model, nomatch[0], nomatch[1], nomatch[2], nomatch[3]));

  assert(!serd_model_get(model, NULL, NULL, uri(world, 3), g));
  assert(!serd_model_get(model, uri(world, 1), uri(world, 99), NULL, g));

  assert(serd_node_equals(
    serd_model_get(model, uri(world, 1), uri(world, 2), NULL, g),
    uri(world, 3)));
  assert(serd_node_equals(
    serd_model_get(model, uri(world, 1), NULL, uri(world, 3), g),
    uri(world, 2)));
  assert(serd_node_equals(
    serd_model_get(model, NULL, uri(world, 2), uri(world, 3), g),
    uri(world, 1)));
  if (g) {
    assert(serd_node_equals(
      serd_model_get(model, uri(world, 1), uri(world, 2), uri(world, 3), NULL),
      g));
  }

  for (unsigned i = 0; i < NUM_PATTERNS; ++i) {
    QueryTest test = patterns[i];
    Quad      pat  = {test.query[0], test.query[1], test.query[2], g};

    SerdCursor* range = serd_model_find(model, pat[0], pat[1], pat[2], pat[3]);
    int         num_results = 0;
    for (; !serd_cursor_is_end(range); serd_cursor_advance(range)) {
      ++num_results;

      const SerdStatement* first = serd_cursor_get(range);
      assert(first);
      assert(serd_statement_matches(first, pat[0], pat[1], pat[2], pat[3]));
    }

    serd_cursor_free(range);

    assert(num_results == test.expected_num_results);
  }

  // Query blank node subject

  const SerdNode* ablank = serd_nodes_blank(nodes, serd_string("ablank"));

  Quad        pat         = {ablank, 0, 0};
  int         num_results = 0;
  SerdCursor* range = serd_model_find(model, pat[0], pat[1], pat[2], pat[3]);
  for (; !serd_cursor_is_end(range); serd_cursor_advance(range)) {
    ++num_results;
    const SerdStatement* statement = serd_cursor_get(range);
    assert(serd_statement_matches(statement, pat[0], pat[1], pat[2], pat[3]));
  }
  serd_cursor_free(range);

  assert(num_results == 2);

  // Test nested queries
  const SerdNode* last_subject = 0;
  range                        = serd_model_find(model, NULL, NULL, NULL, NULL);
  for (; !serd_cursor_is_end(range); serd_cursor_advance(range)) {
    const SerdStatement* statement = serd_cursor_get(range);
    const SerdNode*      subject   = serd_statement_subject(statement);
    if (subject == last_subject) {
      continue;
    }

    Quad              subpat = {subject, 0, 0};
    SerdCursor* const subrange =
      serd_model_find(model, subpat[0], subpat[1], subpat[2], subpat[3]);

    assert(subrange);

    const SerdStatement* substatement    = serd_cursor_get(subrange);
    uint64_t             num_sub_results = 0;
    assert(serd_statement_subject(substatement) == subject);
    for (; !serd_cursor_is_end(subrange); serd_cursor_advance(subrange)) {
      const SerdStatement* const front = serd_cursor_get(subrange);
      assert(front);

      assert(serd_statement_matches(
        front, subpat[0], subpat[1], subpat[2], subpat[3]));

      ++num_sub_results;
    }
    serd_cursor_free(subrange);
    assert(num_sub_results == N_OBJECTS_PER);

    uint64_t count = serd_model_count(model, subject, 0, 0, 0);
    assert(count == num_sub_results);

    last_subject = subject;
  }
  serd_cursor_free(range);

  serd_nodes_free(nodes);
  return 0;
}

static SerdStatus
expected_error(void* const               handle,
               const SerdLogLevel        level,
               const size_t              n_fields,
               const SerdLogField* const fields,
               const SerdStringView      message)
{
  (void)level;
  (void)n_fields;
  (void)fields;
  (void)handle;

  fprintf(stderr, "expected: %s\n", message.buf);
  return SERD_SUCCESS;
}

SERD_PURE_FUNC
static SerdStatus
ignore_only_index_error(void* const               handle,
                        const SerdLogLevel        level,
                        const size_t              n_fields,
                        const SerdLogField* const fields,
                        const SerdStringView      message)
{
  (void)handle;
  (void)level;
  (void)n_fields;
  (void)fields;

  const bool is_index_error = strstr(message.buf, "index");

  assert(is_index_error);

  return is_index_error ? SERD_SUCCESS : SERD_ERR_UNKNOWN;
}

static int
test_free_null(SerdWorld* world, const unsigned n_quads)
{
  (void)world;
  (void)n_quads;

  serd_model_free(NULL); // Shouldn't crash
  return 0;
}

static int
test_get_world(SerdWorld* world, const unsigned n_quads)
{
  (void)n_quads;

  SerdModel* model = serd_model_new(world, SERD_ORDER_SPO, 0U);
  assert(serd_model_world(model) == world);
  serd_model_free(model);
  return 0;
}

static int
test_get_default_order(SerdWorld* world, const unsigned n_quads)
{
  (void)n_quads;

  SerdModel* const model1 = serd_model_new(world, SERD_ORDER_SPO, 0U);
  SerdModel* const model2 = serd_model_new(world, SERD_ORDER_GPSO, 0U);

  assert(serd_model_default_order(model1) == SERD_ORDER_SPO);
  assert(serd_model_default_order(model2) == SERD_ORDER_GPSO);

  serd_model_free(model2);
  serd_model_free(model1);
  return 0;
}

static int
test_get_flags(SerdWorld* world, const unsigned n_quads)
{
  (void)n_quads;

  const SerdModelFlags flags = SERD_STORE_GRAPHS | SERD_STORE_CARETS;
  SerdModel*           model = serd_model_new(world, SERD_ORDER_SPO, flags);

  assert(serd_model_flags(model) & SERD_STORE_GRAPHS);
  assert(serd_model_flags(model) & SERD_STORE_CARETS);
  serd_model_free(model);
  return 0;
}

static int
test_all_begin(SerdWorld* world, const unsigned n_quads)
{
  (void)n_quads;

  SerdModel*  model = serd_model_new(world, SERD_ORDER_SPO, 0U);
  SerdCursor* begin = serd_model_begin(model);
  SerdCursor* first = serd_model_find(model, NULL, NULL, NULL, NULL);

  assert(serd_cursor_equals(begin, first));

  serd_cursor_free(first);
  serd_cursor_free(begin);
  serd_model_free(model);
  return 0;
}

static int
test_begin_ordered(SerdWorld* world, const unsigned n_quads)
{
  (void)n_quads;

  SerdModel* model = serd_model_new(world, SERD_ORDER_SPO, SERD_STORE_GRAPHS);

  assert(
    !serd_model_add(model, uri(world, 1), uri(world, 2), uri(world, 3), 0));

  SerdCursor* i = serd_model_begin_ordered(model, SERD_ORDER_SPO);
  assert(i);
  assert(!serd_cursor_is_end(i));
  serd_cursor_free(i);

  i = serd_model_begin_ordered(model, SERD_ORDER_POS);
  assert(serd_cursor_is_end(i));
  serd_cursor_free(i);

  serd_model_free(model);
  return 0;
}

static int
test_add_with_iterator(SerdWorld* world, const unsigned n_quads)
{
  (void)n_quads;

  SerdModel* model = serd_model_new(world, SERD_ORDER_SPO, 0U);

  serd_set_log_func(world, expected_error, NULL);
  assert(
    !serd_model_add(model, uri(world, 1), uri(world, 2), uri(world, 3), 0));

  // Add a statement with an active iterator
  SerdCursor* iter = serd_model_begin(model);
  assert(
    !serd_model_add(model, uri(world, 1), uri(world, 2), uri(world, 4), 0));

  // Check that iterator has been invalidated
  assert(!serd_cursor_get(iter));
  assert(serd_cursor_advance(iter) == SERD_ERR_BAD_CURSOR);

  serd_cursor_free(iter);
  serd_model_free(model);
  return 0;
}

static int
test_add_remove_nodes(SerdWorld* world, const unsigned n_quads)
{
  (void)n_quads;

  SerdModel* model = serd_model_new(world, SERD_ORDER_SPO, 0U);

  assert(serd_model_nodes(model));
  assert(serd_nodes_size(serd_model_nodes(model)) == 0);

  const SerdNode* const a = uri(world, 1);
  const SerdNode* const b = uri(world, 2);
  const SerdNode* const c = uri(world, 3);

  // Add 2 statements with 3 nodes
  assert(!serd_model_add(model, a, b, a, NULL));
  assert(!serd_model_add(model, c, b, c, NULL));
  assert(serd_model_size(model) == 2);
  assert(serd_nodes_size(serd_model_nodes(model)) == 3);

  // Remove one statement to leave 2 nodes
  SerdCursor* const begin = serd_model_begin(model);
  assert(!serd_model_erase(model, begin));
  assert(serd_model_size(model) == 1);
  assert(serd_nodes_size(serd_model_nodes(model)) == 2);
  serd_cursor_free(begin);

  // Clear the last statement to leave 0 nodes
  assert(!serd_model_clear(model));
  assert(serd_nodes_size(serd_model_nodes(model)) == 0);

  serd_model_free(model);
  return 0;
}

static int
test_add_index(SerdWorld* world, const unsigned n_quads)
{
  (void)n_quads;

  SerdModel* const      model = serd_model_new(world, SERD_ORDER_SPO, 0U);
  const SerdNode* const s     = uri(world, 0);
  const SerdNode* const p     = uri(world, 1);
  const SerdNode* const o1    = uri(world, 2);
  const SerdNode* const o2    = uri(world, 3);

  // Try to add an existing index
  assert(serd_model_add_index(model, SERD_ORDER_SPO) == SERD_FAILURE);

  // Add a couple of statements
  serd_model_add(model, s, p, o1, NULL);
  serd_model_add(model, s, p, o2, NULL);
  assert(serd_model_size(model) == 2);

  // Add a new index
  assert(!serd_model_add_index(model, SERD_ORDER_PSO));

  // Count statements via the new index
  size_t      count = 0U;
  SerdCursor* cur   = serd_model_find(model, NULL, p, NULL, NULL);
  while (!serd_cursor_is_end(cur)) {
    ++count;
    serd_cursor_advance(cur);
  }
  serd_cursor_free(cur);

  serd_model_free(model);
  assert(count == 2);
  return 0;
}

static int
test_remove_index(SerdWorld* world, const unsigned n_quads)
{
  (void)n_quads;

  SerdModel* const      model = serd_model_new(world, SERD_ORDER_SPO, 0U);
  const SerdNode* const s     = uri(world, 0);
  const SerdNode* const p     = uri(world, 1);
  const SerdNode* const o1    = uri(world, 2);
  const SerdNode* const o2    = uri(world, 3);

  // Try to remove default and non-existent indices
  assert(serd_model_drop_index(model, SERD_ORDER_SPO) == SERD_ERR_BAD_CALL);
  assert(serd_model_drop_index(model, SERD_ORDER_PSO) == SERD_FAILURE);

  // Add a couple of statements so that dropping an index isn't trivial
  serd_model_add(model, s, p, o1, NULL);
  serd_model_add(model, s, p, o2, NULL);
  assert(serd_model_size(model) == 2);

  assert(serd_model_add_index(model, SERD_ORDER_PSO) == SERD_SUCCESS);
  assert(serd_model_drop_index(model, SERD_ORDER_PSO) == SERD_SUCCESS);
  assert(serd_model_drop_index(model, SERD_ORDER_PSO) == SERD_FAILURE);
  assert(serd_model_size(model) == 2);
  serd_model_free(model);
  return 0;
}

static int
test_inserter(SerdWorld* world, const unsigned n_quads)
{
  (void)n_quads;

  SerdNodes* const nodes    = serd_nodes_new();
  SerdModel*       model    = serd_model_new(world, SERD_ORDER_SPO, 0U);
  SerdSink*        inserter = serd_inserter_new(model, NULL);

  const SerdNode* const s =
    serd_nodes_uri(nodes, serd_string("http://example.org/s"));

  const SerdNode* const p =
    serd_nodes_uri(nodes, serd_string("http://example.org/p"));

  const SerdNode* const rel = serd_nodes_uri(nodes, serd_string("rel"));

  serd_set_log_func(world, expected_error, NULL);

  assert(serd_sink_write(inserter, 0, s, p, rel, NULL) == SERD_ERR_BAD_ARG);

  serd_sink_free(inserter);
  serd_model_free(model);
  serd_nodes_free(nodes);
  return 0;
}

static int
test_erase_with_iterator(SerdWorld* world, const unsigned n_quads)
{
  (void)n_quads;

  SerdModel* model = serd_model_new(world, SERD_ORDER_SPO, 0U);

  serd_set_log_func(world, expected_error, NULL);
  assert(
    !serd_model_add(model, uri(world, 1), uri(world, 2), uri(world, 3), 0));
  assert(
    !serd_model_add(model, uri(world, 4), uri(world, 5), uri(world, 6), 0));

  // Erase a statement with an active iterator
  SerdCursor* iter1 = serd_model_begin(model);
  SerdCursor* iter2 = serd_model_begin(model);
  assert(!serd_model_erase(model, iter1));

  // Check that erased iterator points to the next statement
  const SerdStatement* const s1 = serd_cursor_get(iter1);
  assert(s1);
  assert(
    serd_statement_matches(s1, uri(world, 4), uri(world, 5), uri(world, 6), 0));

  // Check that other iterator has been invalidated
  assert(!serd_cursor_get(iter2));
  assert(serd_cursor_advance(iter2) == SERD_ERR_BAD_CURSOR);

  // Check that erasing the end iterator does nothing
  SerdCursor* const end = serd_cursor_copy(serd_model_end(model));
  assert(serd_model_erase(model, end) == SERD_FAILURE);

  serd_cursor_free(end);
  serd_cursor_free(iter2);
  serd_cursor_free(iter1);
  serd_model_free(model);
  return 0;
}

static int
test_add_erase(SerdWorld* world, const unsigned n_quads)
{
  (void)n_quads;

  SerdNodes* const nodes = serd_nodes_new();
  SerdModel* const model = serd_model_new(world, SERD_ORDER_SPO, 0U);

  // Add (s p "hello")
  const SerdNode* s     = uri(world, 1);
  const SerdNode* p     = uri(world, 2);
  const SerdNode* hello = serd_nodes_string(nodes, serd_string("hello"));

  assert(!serd_model_add(model, s, p, hello, NULL));
  assert(serd_model_ask(model, s, p, hello, NULL));

  // Add (s p "hi")
  const SerdNode* hi = serd_nodes_string(nodes, serd_string("hi"));
  assert(!serd_model_add(model, s, p, hi, NULL));
  assert(serd_model_ask(model, s, p, hi, NULL));

  // Erase (s p "hi")
  SerdCursor* iter = serd_model_find(model, s, p, hi, NULL);
  assert(!serd_model_erase(model, iter));
  assert(serd_model_size(model) == 1);
  serd_cursor_free(iter);

  // Check that erased statement can not be found
  SerdCursor* empty = serd_model_find(model, s, p, hi, NULL);
  assert(serd_cursor_is_end(empty));
  serd_cursor_free(empty);

  serd_model_free(model);
  serd_nodes_free(nodes);
  return 0;
}

static int
test_add_bad_statement(SerdWorld* world, const unsigned n_quads)
{
  (void)n_quads;

  SerdNodes* const nodes = serd_nodes_new();
  const SerdNode*  s     = serd_nodes_uri(nodes, serd_string("urn:s"));
  const SerdNode*  p     = serd_nodes_uri(nodes, serd_string("urn:p"));
  const SerdNode*  o     = serd_nodes_uri(nodes, serd_string("urn:o"));

  const SerdNode* f =
    serd_nodes_uri(nodes, serd_string("file:///tmp/file.ttl"));

  SerdCaret* caret = serd_caret_new(f, 16, 18);
  SerdModel* model = serd_model_new(world, SERD_ORDER_SPO, 0U);

  assert(!serd_model_add_with_caret(model, s, p, o, NULL, caret));

  SerdCursor* const    begin     = serd_model_begin(model);
  const SerdStatement* statement = serd_cursor_get(begin);
  assert(statement);

  assert(serd_node_equals(serd_statement_subject(statement), s));
  assert(serd_node_equals(serd_statement_predicate(statement), p));
  assert(serd_node_equals(serd_statement_object(statement), o));
  assert(!serd_statement_graph(statement));

  const SerdCaret* statement_caret = serd_statement_caret(statement);
  assert(statement_caret);
  assert(serd_node_equals(serd_caret_document(statement_caret), f));
  assert(serd_caret_line(statement_caret) == 16);
  assert(serd_caret_column(statement_caret) == 18);

  assert(!serd_model_erase(model, begin));

  serd_cursor_free(begin);
  serd_model_free(model);
  serd_caret_free(caret);
  serd_nodes_free(nodes);
  return 0;
}

static int
test_add_with_caret(SerdWorld* world, const unsigned n_quads)
{
  (void)n_quads;

  SerdNodes* const nodes = serd_nodes_new();
  const SerdNode*  lit   = serd_nodes_string(nodes, serd_string("string"));
  const SerdNode*  uri   = serd_nodes_uri(nodes, serd_string("urn:uri"));
  const SerdNode*  blank = serd_nodes_blank(nodes, serd_string("b1"));
  SerdModel*       model = serd_model_new(world, SERD_ORDER_SPO, 0U);

  assert(serd_model_add(model, lit, uri, uri, NULL));
  assert(serd_model_add(model, uri, blank, uri, NULL));
  assert(serd_model_add(model, uri, uri, uri, lit));

  serd_model_free(model);
  serd_nodes_free(nodes);
  return 0;
}

static int
test_erase_all(SerdWorld* world, const unsigned n_quads)
{
  SerdModel* model = serd_model_new(world, SERD_ORDER_SPO, 0U);
  serd_model_add_index(model, SERD_ORDER_OSP);
  generate(world, model, n_quads, NULL);

  SerdCursor* iter = serd_model_begin(model);
  while (!serd_cursor_equals(iter, serd_model_end(model))) {
    assert(!serd_model_erase(model, iter));
  }

  assert(serd_model_empty(model));

  serd_cursor_free(iter);
  serd_model_free(model);
  return 0;
}

static int
test_clear(SerdWorld* world, const unsigned n_quads)
{
  SerdModel* model = serd_model_new(world, SERD_ORDER_SPO, 0U);
  generate(world, model, n_quads, NULL);

  serd_model_clear(model);
  assert(serd_model_empty(model));

  serd_model_free(model);
  return 0;
}

static int
test_copy(SerdWorld* world, const unsigned n_quads)
{
  SerdModel* model = serd_model_new(world, SERD_ORDER_SPO, 0U);
  generate(world, model, n_quads, NULL);

  SerdModel* copy = serd_model_copy(model);
  assert(serd_model_equals(model, copy));

  serd_model_free(model);
  serd_model_free(copy);
  return 0;
}

static int
test_equals(SerdWorld* world, const unsigned n_quads)
{
  SerdModel* model = serd_model_new(world, SERD_ORDER_SPO, 0U);
  generate(world, model, n_quads, NULL);
  serd_model_add(
    model, uri(world, 0), uri(world, 1), uri(world, 2), uri(world, 3));

  assert(serd_model_equals(NULL, NULL));
  assert(!serd_model_equals(NULL, model));
  assert(!serd_model_equals(model, NULL));

  SerdModel* empty = serd_model_new(world, SERD_ORDER_SPO, 0U);
  assert(!serd_model_equals(model, empty));

  SerdModel* different = serd_model_new(world, SERD_ORDER_SPO, 0U);
  generate(world, different, n_quads, NULL);
  serd_model_add(
    different, uri(world, 1), uri(world, 1), uri(world, 2), uri(world, 3));

  assert(serd_model_size(model) == serd_model_size(different));
  assert(!serd_model_equals(model, different));

  serd_model_free(model);
  serd_model_free(empty);
  serd_model_free(different);
  return 0;
}

static int
test_find_past_end(SerdWorld* world, const unsigned n_quads)
{
  (void)n_quads;

  SerdModel*      model = serd_model_new(world, SERD_ORDER_SPO, 0U);
  const SerdNode* s     = uri(world, 1);
  const SerdNode* p     = uri(world, 2);
  const SerdNode* o     = uri(world, 3);
  assert(!serd_model_add(model, s, p, o, 0));
  assert(serd_model_ask(model, s, p, o, 0));

  const SerdNode* huge  = uri(world, 999);
  SerdCursor*     range = serd_model_find(model, huge, huge, huge, 0);
  assert(serd_cursor_is_end(range));

  serd_cursor_free(range);
  serd_model_free(model);
  return 0;
}

static int
test_find_unknown_node(SerdWorld* world, const unsigned n_quads)
{
  (void)n_quads;

  const SerdNode* const s = uri(world, 1);
  const SerdNode* const p = uri(world, 2);
  const SerdNode* const o = uri(world, 3);

  SerdModel* const model =
    serd_model_new(world, SERD_ORDER_SPO, SERD_STORE_GRAPHS);

  // Add one statement
  assert(!serd_model_add(model, s, p, o, NULL));
  assert(serd_model_ask(model, s, p, o, NULL));

  /* Test searching for statements that contain a non-existent node.  This is
     semantically equivalent to any other non-matching pattern, but can be
     implemented with a fast path that avoids searching a statement index
     entirely. */

  const SerdNode* const q = uri(world, 42);
  assert(!serd_model_ask(model, s, p, o, q));
  assert(!serd_model_ask(model, s, p, q, NULL));
  assert(!serd_model_ask(model, s, q, o, NULL));
  assert(!serd_model_ask(model, q, p, o, NULL));

  serd_model_free(model);
  return 0;
}

static int
test_find_graph(SerdWorld* world, const unsigned n_quads)
{
  (void)n_quads;

  const SerdNode* const s  = uri(world, 1);
  const SerdNode* const p  = uri(world, 2);
  const SerdNode* const o1 = uri(world, 3);
  const SerdNode* const o2 = uri(world, 4);
  const SerdNode* const g  = uri(world, 5);

  for (unsigned indexed = 0U; indexed < 2U; ++indexed) {
    SerdModel* const model =
      serd_model_new(world, SERD_ORDER_SPO, SERD_STORE_GRAPHS);

    if (indexed) {
      serd_model_add_index(model, SERD_ORDER_GSPO);
    }

    // Add one statement in a named graph and one in the default graph
    assert(!serd_model_add(model, s, p, o1, NULL));
    assert(!serd_model_add(model, s, p, o2, g));

    // Both statements can be found in the default graph
    assert(serd_model_ask(model, s, p, o1, NULL));
    assert(serd_model_ask(model, s, p, o2, NULL));

    // Only the one statement can be found in the named graph
    assert(!serd_model_ask(model, s, p, o1, g));
    assert(serd_model_ask(model, s, p, o2, g));

    serd_model_free(model);
  }

  return 0;
}

static int
test_range(SerdWorld* world, const unsigned n_quads)
{
  SerdModel* model = serd_model_new(world, SERD_ORDER_SPO, 0U);
  generate(world, model, n_quads, NULL);

  SerdCursor* range1 = serd_model_begin(model);
  SerdCursor* range2 = serd_model_begin(model);

  assert(!serd_cursor_is_end(range1));
  assert(serd_cursor_is_end(NULL));

  assert(serd_cursor_equals(NULL, NULL));
  assert(!serd_cursor_equals(range1, NULL));
  assert(!serd_cursor_equals(NULL, range1));
  assert(serd_cursor_equals(range1, range2));

  assert(!serd_cursor_advance(range2));
  assert(!serd_cursor_equals(range1, range2));

  serd_cursor_free(range2);
  serd_cursor_free(range1);
  serd_model_free(model);

  return 0;
}

static int
test_triple_index_read(SerdWorld* world, const unsigned n_quads)
{
  serd_set_log_func(world, ignore_only_index_error, NULL);

  for (unsigned i = 0; i < 6; ++i) {
    SerdModel* model = serd_model_new(world, (SerdStatementOrder)i, 0U);
    generate(world, model, n_quads, 0);
    assert(!test_read(world, model, 0, n_quads));
    serd_model_free(model);
  }

  return 0;
}

static int
test_quad_index_read(SerdWorld* world, const unsigned n_quads)
{
  serd_set_log_func(world, ignore_only_index_error, NULL);

  for (unsigned i = 0; i < 6; ++i) {
    SerdModel* model =
      serd_model_new(world, (SerdStatementOrder)i, SERD_STORE_GRAPHS);

    const SerdNode* graph = uri(world, 42);
    generate(world, model, n_quads, graph);
    assert(!test_read(world, model, graph, n_quads));
    serd_model_free(model);
  }

  return 0;
}

static int
test_remove_graph(SerdWorld* world, const unsigned n_quads)
{
  (void)n_quads;

  SerdModel* model = serd_model_new(world, SERD_ORDER_GSPO, SERD_STORE_GRAPHS);

  // Generate a couple of graphs
  const SerdNode* graph42 = uri(world, 42);
  const SerdNode* graph43 = uri(world, 43);
  generate(world, model, 1, graph42);
  generate(world, model, 1, graph43);

  // Remove one graph via range
  SerdCursor* range = serd_model_find(model, NULL, NULL, NULL, graph43);
  SerdStatus  st    = serd_model_erase_statements(model, range);
  assert(!st);
  serd_cursor_free(range);

  // Erase the first tuple (an element in the default graph)
  SerdCursor* iter = serd_model_begin(model);
  assert(!serd_model_erase(model, iter));
  serd_cursor_free(iter);

  // Ensure only the other graph is left
  Quad pat = {0, 0, 0, graph42};
  for (iter = serd_model_begin(model);
       !serd_cursor_equals(iter, serd_model_end(model));
       serd_cursor_advance(iter)) {
    const SerdStatement* const s = serd_cursor_get(iter);
    assert(s);
    assert(serd_statement_matches(s, pat[0], pat[1], pat[2], pat[3]));
  }
  serd_cursor_free(iter);

  serd_model_free(model);
  return 0;
}

static int
test_default_graph(SerdWorld* world, const unsigned n_quads)
{
  (void)n_quads;

  const SerdNode* s  = uri(world, 1);
  const SerdNode* p  = uri(world, 2);
  const SerdNode* o  = uri(world, 3);
  const SerdNode* g1 = uri(world, 101);
  const SerdNode* g2 = uri(world, 102);

  {
    // Make a model that does not store graphs
    SerdModel* model = serd_model_new(world, SERD_ORDER_SPO, 0U);

    // Insert a statement into a graph (which will be dropped)
    assert(!serd_model_add(model, s, p, o, g1));

    // Attempt to insert the same statement into another graph
    assert(serd_model_add(model, s, p, o, g2) == SERD_FAILURE);

    // Ensure that we only see the statement once
    assert(serd_model_count(model, s, p, o, NULL) == 1);

    serd_model_free(model);
  }

  {
    // Make a model that stores graphs
    SerdModel* model = serd_model_new(world, SERD_ORDER_SPO, SERD_STORE_GRAPHS);

    // Insert the same statement into two graphs
    assert(!serd_model_add(model, s, p, o, g1));
    assert(!serd_model_add(model, s, p, o, g2));

    // Ensure we see the statement twice
    assert(serd_model_count(model, s, p, o, NULL) == 2);

    serd_model_free(model);
  }

  return 0;
}

static int
test_write_flat_range(SerdWorld* world, const unsigned n_quads)
{
  (void)n_quads;

  SerdModel* model = serd_model_new(world, SERD_ORDER_SPO, SERD_STORE_GRAPHS);
  SerdNodes* nodes = serd_nodes_new();

  const SerdNode* s  = serd_nodes_uri(nodes, serd_string("urn:s"));
  const SerdNode* p  = serd_nodes_uri(nodes, serd_string("urn:p"));
  const SerdNode* b1 = serd_nodes_blank(nodes, serd_string("b1"));
  const SerdNode* b2 = serd_nodes_blank(nodes, serd_string("b2"));
  const SerdNode* o  = serd_nodes_uri(nodes, serd_string("urn:o"));

  serd_model_add(model, s, p, b1, NULL);
  serd_model_add(model, b1, p, o, NULL);
  serd_model_add(model, s, p, b2, NULL);
  serd_model_add(model, b2, p, o, NULL);

  SerdBuffer       buffer = {NULL, 0};
  SerdEnv*         env    = serd_env_new(serd_empty_string());
  SerdOutputStream out    = serd_open_output_buffer(&buffer);

  SerdWriter* writer = serd_writer_new(world, SERD_TURTLE, 0, env, &out, 1);
  assert(writer);

  SerdCursor* all = serd_model_begin(model);
  for (const SerdStatement* t = NULL; (t = serd_cursor_get(all));
       serd_cursor_advance(all)) {
    serd_sink_write_statement(serd_writer_sink(writer), 0U, t);
  }
  serd_cursor_free(all);

  serd_writer_finish(writer);
  serd_close_output(&out);

  const char* const        str      = (const char*)buffer.buf;
  static const char* const expected = "<urn:s>\n"
                                      "\t<urn:p> _:b1 ,\n"
                                      "\t\t_:b2 .\n"
                                      "\n"
                                      "_:b1\n"
                                      "\t<urn:p> <urn:o> .\n"
                                      "\n"
                                      "_:b2\n"
                                      "\t<urn:p> <urn:o> .\n";

  assert(str);
  assert(!strcmp(str, expected));

  serd_free(buffer.buf);
  serd_writer_free(writer);
  serd_model_free(model);
  serd_env_free(env);
  serd_nodes_free(nodes);
  return 0;
}

static int
test_write_bad_list(SerdWorld* world, const unsigned n_quads)
{
  (void)n_quads;

  SerdModel* model = serd_model_new(world, SERD_ORDER_SPO, SERD_STORE_GRAPHS);
  SerdNodes* nodes = serd_nodes_new();

  serd_model_add_index(model, SERD_ORDER_OPS);

  const SerdNode* s       = serd_nodes_uri(nodes, serd_string("urn:s"));
  const SerdNode* p       = serd_nodes_uri(nodes, serd_string("urn:p"));
  const SerdNode* list1   = serd_nodes_blank(nodes, serd_string("l1"));
  const SerdNode* list2   = serd_nodes_blank(nodes, serd_string("l2"));
  const SerdNode* nofirst = serd_nodes_blank(nodes, serd_string("nof"));
  const SerdNode* norest  = serd_nodes_blank(nodes, serd_string("nor"));
  const SerdNode* pfirst  = serd_nodes_uri(nodes, serd_string(RDF_FIRST));
  const SerdNode* prest   = serd_nodes_uri(nodes, serd_string(RDF_REST));
  const SerdNode* val1    = serd_nodes_string(nodes, serd_string("a"));
  const SerdNode* val2    = serd_nodes_string(nodes, serd_string("b"));

  // List where second node has no rdf:first
  serd_model_add(model, s, p, list1, NULL);
  serd_model_add(model, list1, pfirst, val1, NULL);
  serd_model_add(model, list1, prest, nofirst, NULL);

  // List where second node has no rdf:rest
  serd_model_add(model, s, p, list2, NULL);
  serd_model_add(model, list2, pfirst, val1, NULL);
  serd_model_add(model, list2, prest, norest, NULL);
  serd_model_add(model, norest, pfirst, val2, NULL);

  SerdBuffer       buffer = {NULL, 0};
  SerdEnv*         env    = serd_env_new(serd_empty_string());
  SerdOutputStream out    = serd_open_output_buffer(&buffer);

  SerdWriter* writer = serd_writer_new(world, SERD_TURTLE, 0, env, &out, 1);
  assert(writer);

  SerdCursor* all = serd_model_begin(model);
  serd_describe_range(all, serd_writer_sink(writer), 0);
  serd_cursor_free(all);

  serd_writer_finish(writer);
  serd_close_output(&out);

  const char* str      = (const char*)buffer.buf;
  const char* expected = "<urn:s>\n"
                         "	<urn:p> (\n"
                         "		\"a\"\n"
                         "	) , (\n"
                         "		\"a\"\n"
                         "		\"b\"\n"
                         "	) .\n";

  assert(str);
  assert(!strcmp(str, expected));

  free(buffer.buf);
  serd_writer_free(writer);
  serd_close_output(&out);
  serd_model_free(model);
  serd_env_free(env);
  serd_nodes_free(nodes);
  return 0;
}

static int
test_write_infinite_list(SerdWorld* world, const unsigned n_quads)
{
  (void)n_quads;

  SerdModel* model = serd_model_new(world, SERD_ORDER_SPO, SERD_STORE_GRAPHS);
  SerdNodes* nodes = serd_nodes_new();

  serd_model_add_index(model, SERD_ORDER_OPS);

  const SerdNode* s     = serd_nodes_uri(nodes, serd_string("urn:s"));
  const SerdNode* p     = serd_nodes_uri(nodes, serd_string("urn:p"));
  const SerdNode* list1 = serd_nodes_blank(nodes, serd_string("l1"));
  const SerdNode* list2 = serd_nodes_blank(nodes, serd_string("l2"));

  const SerdNode* pfirst = serd_nodes_uri(nodes, serd_string(RDF_FIRST));
  const SerdNode* prest  = serd_nodes_uri(nodes, serd_string(RDF_REST));
  const SerdNode* val1   = serd_nodes_string(nodes, serd_string("a"));
  const SerdNode* val2   = serd_nodes_string(nodes, serd_string("b"));

  // List with a cycle: list1 -> list2 -> list1 -> list2 ...
  serd_model_add(model, s, p, list1, NULL);
  serd_model_add(model, list1, pfirst, val1, NULL);
  serd_model_add(model, list1, prest, list2, NULL);
  serd_model_add(model, list2, pfirst, val2, NULL);
  serd_model_add(model, list2, prest, list1, NULL);

  SerdBuffer       buffer = {NULL, 0};
  SerdEnv*         env    = serd_env_new(serd_empty_string());
  SerdOutputStream out    = serd_open_output_buffer(&buffer);

  SerdWriter* writer = serd_writer_new(world, SERD_TURTLE, 0, env, &out, 1);
  assert(writer);

  serd_env_set_prefix(
    env,
    serd_string("rdf"),
    serd_string("http://www.w3.org/1999/02/22-rdf-syntax-ns#"));

  SerdCursor* all = serd_model_begin(model);
  serd_describe_range(all, serd_writer_sink(writer), 0);
  serd_cursor_free(all);

  serd_writer_finish(writer);
  serd_close_output(&out);
  const char* str      = (const char*)buffer.buf;
  const char* expected = "<urn:s>\n"
                         "	<urn:p> _:l1 .\n"
                         "\n"
                         "_:l1\n"
                         "	rdf:first \"a\" ;\n"
                         "	rdf:rest [\n"
                         "		rdf:first \"b\" ;\n"
                         "		rdf:rest _:l1\n"
                         "	] .\n";

  assert(str);
  assert(!strcmp(str, expected));

  free(buffer.buf);
  serd_writer_free(writer);
  serd_close_output(&out);
  serd_model_free(model);
  serd_env_free(env);
  serd_nodes_free(nodes);

  return 0;
}

typedef struct {
  size_t n_written;
  size_t max_successes;
} FailingWriteFuncState;

/// Write function that fails after a certain number of writes
static size_t
failing_write_func(const void* buf, size_t size, size_t nmemb, void* stream)
{
  (void)buf;
  (void)size;
  (void)nmemb;

  FailingWriteFuncState* state = (FailingWriteFuncState*)stream;

  return (++state->n_written > state->max_successes) ? 0 : nmemb;
}

static int
test_write_error_in_list_subject(SerdWorld* world, const unsigned n_quads)
{
  (void)n_quads;

  serd_set_log_func(world, expected_error, NULL);

  SerdModel* model = serd_model_new(world, SERD_ORDER_SPO, 0U);
  SerdNodes* nodes = serd_nodes_new();

  serd_model_add_index(model, SERD_ORDER_OPS);

  /* const SerdNode* s  = serd_nodes_uri(nodes, serd_string("urn:s")); */
  const SerdNode* p  = serd_nodes_uri(nodes, serd_string("urn:p"));
  const SerdNode* o  = serd_nodes_uri(nodes, serd_string("urn:o"));
  const SerdNode* l1 = serd_nodes_blank(nodes, serd_string("l1"));

  const SerdNode* one = serd_nodes_integer(nodes, 1, serd_empty_string());
  const SerdNode* l2  = serd_nodes_blank(nodes, serd_string("l2"));
  const SerdNode* two = serd_nodes_integer(nodes, 2, serd_empty_string());

  const SerdNode* rdf_first = serd_nodes_uri(nodes, serd_string(RDF_FIRST));
  const SerdNode* rdf_rest  = serd_nodes_uri(nodes, serd_string(RDF_REST));
  const SerdNode* rdf_nil   = serd_nodes_uri(nodes, serd_string(RDF_NIL));

  serd_model_add(model, l1, rdf_first, one, NULL);
  serd_model_add(model, l1, rdf_rest, l2, NULL);
  serd_model_add(model, l2, rdf_first, two, NULL);
  serd_model_add(model, l2, rdf_rest, rdf_nil, NULL);
  serd_model_add(model, l1, p, o, NULL);

  SerdEnv* env = serd_env_new(serd_empty_string());

  for (size_t max_successes = 0; max_successes < 18; ++max_successes) {
    FailingWriteFuncState state = {0, max_successes};
    SerdOutputStream      out =
      serd_open_output_stream(failing_write_func, NULL, &state);

    SerdWriter* writer = serd_writer_new(world, SERD_TURTLE, 0, env, &out, 1);

    const SerdSink* const sink = serd_writer_sink(writer);
    SerdCursor* const     all  = serd_model_begin(model);
    const SerdStatus      st   = serd_describe_range(all, sink, 0);
    serd_cursor_free(all);

    assert(st == SERD_ERR_BAD_WRITE);

    serd_writer_free(writer);
    serd_close_output(&out);
  }

  serd_env_free(env);
  serd_model_free(model);
  serd_nodes_free(nodes);
  return 0;
}

static int
test_write_error_in_list_object(SerdWorld* world, const unsigned n_quads)
{
  (void)n_quads;

  serd_set_log_func(world, expected_error, NULL);

  SerdModel* model = serd_model_new(world, SERD_ORDER_SPO, 0U);
  SerdNodes* nodes = serd_nodes_new();

  serd_model_add_index(model, SERD_ORDER_OPS);

  const SerdNode* s  = serd_nodes_uri(nodes, serd_string("urn:s"));
  const SerdNode* p  = serd_nodes_uri(nodes, serd_string("urn:p"));
  const SerdNode* l1 = serd_nodes_blank(nodes, serd_string("l1"));

  const SerdNode* one = serd_nodes_integer(nodes, 1, serd_empty_string());
  const SerdNode* l2  = serd_nodes_blank(nodes, serd_string("l2"));
  const SerdNode* two = serd_nodes_integer(nodes, 2, serd_empty_string());

  const SerdNode* rdf_first = serd_nodes_uri(nodes, serd_string(RDF_FIRST));
  const SerdNode* rdf_rest  = serd_nodes_uri(nodes, serd_string(RDF_REST));
  const SerdNode* rdf_nil   = serd_nodes_uri(nodes, serd_string(RDF_NIL));

  serd_model_add(model, s, p, l1, NULL);
  serd_model_add(model, l1, rdf_first, one, NULL);
  serd_model_add(model, l1, rdf_rest, l2, NULL);
  serd_model_add(model, l2, rdf_first, two, NULL);
  serd_model_add(model, l2, rdf_rest, rdf_nil, NULL);

  SerdEnv* env = serd_env_new(serd_empty_string());

  for (size_t max_successes = 0; max_successes < 21; ++max_successes) {
    FailingWriteFuncState state = {0, max_successes};
    SerdOutputStream      out =
      serd_open_output_stream(failing_write_func, NULL, &state);

    SerdWriter* writer = serd_writer_new(world, SERD_TURTLE, 0, env, &out, 1);

    const SerdSink* const sink = serd_writer_sink(writer);
    SerdCursor* const     all  = serd_model_begin(model);
    const SerdStatus      st   = serd_describe_range(all, sink, 0);
    serd_cursor_free(all);

    assert(st == SERD_ERR_BAD_WRITE);

    serd_writer_free(writer);
    serd_close_output(&out);
  }

  serd_env_free(env);
  serd_model_free(model);
  serd_nodes_free(nodes);
  return 0;
}

int
main(void)
{
  static const unsigned n_quads = 300;

  serd_model_free(NULL); // Shouldn't crash

  typedef int (*TestFunc)(SerdWorld*, unsigned);

  const TestFunc tests[] = {test_free_null,
                            test_get_world,
                            test_get_default_order,
                            test_get_flags,
                            test_all_begin,
                            test_begin_ordered,
                            test_add_with_iterator,
                            test_add_remove_nodes,
                            test_add_index,
                            test_remove_index,
                            test_inserter,
                            test_erase_with_iterator,
                            test_add_erase,
                            test_add_bad_statement,
                            test_add_with_caret,
                            test_erase_all,
                            test_clear,
                            test_copy,
                            test_equals,
                            test_find_past_end,
                            test_find_unknown_node,
                            test_find_graph,
                            test_range,
                            test_triple_index_read,
                            test_quad_index_read,
                            test_remove_graph,
                            test_default_graph,
                            test_write_flat_range,
                            test_write_bad_list,
                            test_write_infinite_list,
                            test_write_error_in_list_subject,
                            test_write_error_in_list_object,
                            NULL};

  SerdWorld* world = serd_world_new();
  int        ret   = 0;

  for (const TestFunc* t = tests; *t; ++t) {
    serd_set_log_func(world, NULL, NULL);
    ret += (*t)(world, n_quads);
  }

  serd_world_free(world);
  return ret;
}
