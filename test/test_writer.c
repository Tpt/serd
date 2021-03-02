// Copyright 2011-2021 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#undef NDEBUG

#include "serd/buffer.h"
#include "serd/env.h"
#include "serd/memory.h"
#include "serd/node.h"
#include "serd/sink.h"
#include "serd/status.h"
#include "serd/string_view.h"
#include "serd/syntax.h"
#include "serd/writer.h"

#include <assert.h>
#include <string.h>

static void
test_write_bad_prefix(void)
{
  SerdEnv*    env    = serd_env_new(serd_empty_string());
  SerdBuffer  buffer = {NULL, 0};
  SerdWriter* writer =
    serd_writer_new(SERD_TURTLE, 0U, env, serd_buffer_sink, &buffer);

  assert(writer);

  SerdNode* name = serd_new_string(serd_string("eg"));
  SerdNode* uri  = serd_new_uri(serd_string("rel"));

  assert(serd_sink_write_prefix(serd_writer_sink(writer), name, uri) ==
         SERD_ERR_BAD_ARG);

  char* const out = serd_buffer_sink_finish(&buffer);

  assert(!strcmp(out, ""));
  serd_free(out);

  serd_node_free(uri);
  serd_node_free(name);
  serd_writer_free(writer);
  serd_env_free(env);
}

static void
test_write_long_literal(void)
{
  SerdEnv*    env    = serd_env_new(serd_empty_string());
  SerdBuffer  buffer = {NULL, 0};
  SerdWriter* writer =
    serd_writer_new(SERD_TURTLE, 0U, env, serd_buffer_sink, &buffer);

  assert(writer);

  SerdNode* s = serd_new_uri(serd_string("http://example.org/s"));
  SerdNode* p = serd_new_uri(serd_string("http://example.org/p"));
  SerdNode* o = serd_new_literal(serd_string("hello \"\"\"world\"\"\"!"),
                                 serd_empty_string(),
                                 serd_empty_string());

  assert(!serd_sink_write(serd_writer_sink(writer), 0, s, p, o, NULL));

  serd_node_free(o);
  serd_node_free(p);
  serd_node_free(s);
  serd_writer_free(writer);
  serd_env_free(env);

  char* out = serd_buffer_sink_finish(&buffer);

  static const char* const expected =
    "<http://example.org/s>\n"
    "\t<http://example.org/p> \"\"\"hello \"\"\\\"world\"\"\\\"!\"\"\" .\n\n";

  assert(!strcmp((char*)out, expected));
  serd_free(out);
}

int
main(void)
{
  test_write_bad_prefix();
  test_write_long_literal();

  return 0;
}
