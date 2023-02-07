// Copyright 2019-2020 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#undef NDEBUG

#include "serd/serd.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define NS_EG "http://example.org/"

typedef struct {
  const SerdNode*      last_base;
  const SerdNode*      last_name;
  const SerdNode*      last_namespace;
  const SerdNode*      last_end;
  const SerdStatement* last_statement;
  SerdStatus           return_status;
} State;

static SerdStatus
on_base(void* handle, const SerdNode* uri)
{
  State* state = (State*)handle;

  state->last_base = uri;
  return state->return_status;
}

static SerdStatus
on_prefix(void* handle, const SerdNode* name, const SerdNode* uri)
{
  State* state = (State*)handle;

  state->last_name      = name;
  state->last_namespace = uri;
  return state->return_status;
}

static SerdStatus
on_statement(void*                      handle,
             SerdStatementFlags         flags,
             const SerdStatement* const statement)
{
  (void)flags;

  State* state = (State*)handle;

  state->last_statement = statement;

  return state->return_status;
}

static SerdStatus
on_end(void* handle, const SerdNode* node)
{
  State* state = (State*)handle;

  state->last_end = node;
  return state->return_status;
}

static SerdStatus
on_event(void* const handle, const SerdEvent* const event)
{
  switch (event->type) {
  case SERD_BASE:
    return on_base(handle, event->base.uri);
  case SERD_PREFIX:
    return on_prefix(handle, event->prefix.name, event->prefix.uri);
  case SERD_STATEMENT:
    return on_statement(
      handle, event->statement.flags, event->statement.statement);
  case SERD_END:
    return on_end(handle, event->end.node);
  }

  return SERD_BAD_ARG;
}

static void
test_callbacks(void)
{
  SerdWorld* const world = serd_world_new();
  SerdNodes* const nodes = serd_nodes_new();

  const SerdNode* base  = serd_nodes_uri(nodes, serd_string(NS_EG));
  const SerdNode* name  = serd_nodes_string(nodes, serd_string("eg"));
  const SerdNode* uri   = serd_nodes_uri(nodes, serd_string(NS_EG "uri"));
  const SerdNode* blank = serd_nodes_blank(nodes, serd_string("b1"));

  SerdEnv* env = serd_env_new(world, serd_node_string_view(base));

  SerdStatement* const statement =
    serd_statement_new(base, uri, blank, NULL, NULL);

  const SerdBaseEvent      base_event      = {SERD_BASE, uri};
  const SerdPrefixEvent    prefix_event    = {SERD_PREFIX, name, uri};
  const SerdStatementEvent statement_event = {SERD_STATEMENT, 0U, statement};
  const SerdEndEvent       end_event       = {SERD_END, blank};

  State state = {0, 0, 0, 0, 0, SERD_SUCCESS};

  // Call functions on a sink with no functions set

  SerdSink* null_sink = serd_sink_new(world, &state, NULL, NULL);

  assert(!serd_sink_write_base(null_sink, base));
  assert(!serd_sink_write_prefix(null_sink, name, uri));
  assert(!serd_sink_write_statement(null_sink, 0, statement));
  assert(!serd_sink_write(null_sink, 0, base, uri, blank, NULL));
  assert(!serd_sink_write_end(null_sink, blank));

  SerdEvent event = {SERD_BASE};

  event.base = base_event;
  assert(!serd_sink_write_event(null_sink, &event));
  event.prefix = prefix_event;
  assert(!serd_sink_write_event(null_sink, &event));
  event.statement = statement_event;
  assert(!serd_sink_write_event(null_sink, &event));
  event.end = end_event;
  assert(!serd_sink_write_event(null_sink, &event));

  serd_sink_free(null_sink);

  // Try again with a sink that has the event handler set

  SerdSink* sink = serd_sink_new(world, &state, on_event, NULL);

  assert(!serd_sink_write_base(sink, base));
  assert(serd_node_equals(state.last_base, base));

  assert(!serd_sink_write_prefix(sink, name, uri));
  assert(serd_node_equals(state.last_name, name));
  assert(serd_node_equals(state.last_namespace, uri));

  assert(!serd_sink_write_statement(sink, 0, statement));
  assert(serd_statement_equals(state.last_statement, statement));

  assert(!serd_sink_write_end(sink, blank));
  assert(serd_node_equals(state.last_end, blank));

  const SerdEvent junk = {(SerdEventType)42};
  assert(serd_sink_write_event(sink, &junk) == SERD_BAD_ARG);

  serd_sink_free(sink);

  serd_statement_free(statement);
  serd_env_free(env);
  serd_nodes_free(nodes);
  serd_world_free(world);
}

static void
test_free(void)
{
  // Free of null should (as always) not crash
  serd_sink_free(NULL);

  // Set up a sink with dynamically allocated data and a free function
  SerdWorld* world = serd_world_new();
  uintptr_t* data  = (uintptr_t*)calloc(1, sizeof(uintptr_t));
  SerdSink*  sink  = serd_sink_new(world, data, NULL, free);

  // Free the sink, which should free the data (rely on valgrind or sanitizers)
  serd_sink_free(sink);
  serd_world_free(world);
}

int
main(void)
{
  test_callbacks();
  test_free();
  return 0;
}
