// Copyright 2019-2020 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#undef NDEBUG

#include "serd/serd.h"
#include "zix/allocator.h"
#include "zix/string_view.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define NS_RDF "http://www.w3.org/1999/02/22-rdf-syntax-ns#"

static void
check_output(SerdWriter* writer, SerdBuffer* buffer, const char* expected)
{
  serd_writer_finish(writer);
  serd_buffer_close(buffer);

  const char* output = (const char*)buffer->buf;

  fprintf(stderr, "%s", output);
  assert(!strcmp(output, expected));

  buffer->len = 0;
}

static int
test(void)
{
  SerdWorld* world  = serd_world_new(NULL);
  SerdBuffer buffer = {NULL, NULL, 0};
  SerdEnv*   env    = serd_env_new(world, zix_empty_string());
  SerdNodes* nodes  = serd_nodes_new(serd_world_allocator(world));

  const SerdNode* b1 = serd_nodes_get(nodes, serd_a_blank(zix_string("b1")));
  const SerdNode* l1 = serd_nodes_get(nodes, serd_a_blank(zix_string("l1")));
  const SerdNode* l2 = serd_nodes_get(nodes, serd_a_blank(zix_string("l2")));
  const SerdNode* s1 = serd_nodes_get(nodes, serd_a_string("s1"));
  const SerdNode* s2 = serd_nodes_get(nodes, serd_a_string("s2"));

  const SerdNode* rdf_first =
    serd_nodes_get(nodes, serd_a_uri_string(NS_RDF "first"));

  const SerdNode* rdf_value =
    serd_nodes_get(nodes, serd_a_uri_string(NS_RDF "value"));

  const SerdNode* rdf_rest =
    serd_nodes_get(nodes, serd_a_uri_string(NS_RDF "rest"));

  const SerdNode* rdf_nil =
    serd_nodes_get(nodes, serd_a_uri_string(NS_RDF "nil"));

  serd_env_set_prefix(env, zix_string("rdf"), zix_string(NS_RDF));

  SerdOutputStream  output = serd_open_output_buffer(&buffer);
  SerdWriter* const writer =
    serd_writer_new(world, SERD_TURTLE, 0, env, &output, 1);

  const SerdSink* sink = serd_writer_sink(writer);

  // Simple lone list
  serd_sink_write(sink, SERD_TERSE_S | SERD_LIST_S, l1, rdf_first, s1, NULL);
  serd_sink_write(sink, 0, l1, rdf_rest, l2, NULL);
  serd_sink_write(sink, 0, l2, rdf_first, s2, NULL);
  serd_sink_write(sink, 0, l2, rdf_rest, rdf_nil, NULL);
  check_output(writer, &buffer, "( \"s1\" \"s2\" ) .\n");

  // Nested terse lists
  serd_sink_write(sink,
                  SERD_TERSE_S | SERD_LIST_S | SERD_TERSE_O | SERD_LIST_O,
                  l1,
                  rdf_first,
                  l2,
                  NULL);
  serd_sink_write(sink, 0, l2, rdf_first, s1, NULL);
  serd_sink_write(sink, 0, l1, rdf_rest, rdf_nil, NULL);
  serd_sink_write(sink, 0, l2, rdf_rest, rdf_nil, NULL);
  check_output(writer, &buffer, "( ( \"s1\" ) ) .\n");

  // List as object
  serd_sink_write(
    sink, SERD_EMPTY_S | SERD_LIST_O | SERD_TERSE_O, b1, rdf_value, l1, NULL);
  serd_sink_write(sink, 0, l1, rdf_first, s1, NULL);
  serd_sink_write(sink, 0, l1, rdf_rest, l2, NULL);
  serd_sink_write(sink, 0, l2, rdf_first, s2, NULL);
  serd_sink_write(sink, 0, l2, rdf_rest, rdf_nil, NULL);
  check_output(writer, &buffer, "[]\n\trdf:value ( \"s1\" \"s2\" ) .\n");

  serd_writer_free(writer);
  serd_close_output(&output);
  zix_free(NULL, buffer.buf);
  serd_nodes_free(nodes);
  serd_env_free(env);
  serd_world_free(world);

  return 0;
}

int
main(void)
{
  return test();
}
