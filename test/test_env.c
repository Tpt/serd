// Copyright 2011-2020 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#undef NDEBUG

#include "serd/env.h"
#include "serd/node.h"
#include "serd/status.h"
#include "serd/string_view.h"

#include <assert.h>
#include <string.h>

static SerdStatus
count_prefixes(void* handle, const SerdNode* name, const SerdNode* uri)
{
  (void)name;
  (void)uri;

  ++*(int*)handle;
  return SERD_SUCCESS;
}

static void
test_env(void)
{
  SerdNode* u   = serd_new_string(SERD_URI, "http://example.org/foo");
  SerdNode* b   = serd_new_string(SERD_CURIE, "invalid");
  SerdNode* c   = serd_new_string(SERD_CURIE, "eg.2:b");
  SerdNode* s   = serd_new_string(SERD_LITERAL, "hello");
  SerdEnv*  env = serd_env_new(NULL);
  serd_env_set_prefix_from_strings(env, "eg.2", "http://example.org/");

  const SerdNode* prefix_node = NULL;
  SerdStringView  prefix      = serd_empty_string();
  SerdStringView  suffix      = serd_empty_string();

  assert(!serd_env_qualify(NULL, u, &prefix_node, &suffix));

  assert(serd_env_expand(env, NULL, &prefix, &suffix) == SERD_ERR_BAD_CURIE);

  assert(!serd_env_expand_node(NULL, u));
  assert(!serd_env_expand_node(env, b));
  assert(!serd_env_expand_node(env, s));

  assert(!serd_env_set_base_uri(env, NULL));

  SerdNode* xu = serd_env_expand_node(env, u);
  assert(!strcmp(serd_node_string(xu), "http://example.org/foo"));
  serd_node_free(xu);

  SerdNode* badpre  = serd_new_string(SERD_CURIE, "hm:what");
  SerdNode* xbadpre = serd_env_expand_node(env, badpre);
  assert(!xbadpre);

  SerdNode* xc = serd_env_expand_node(env, c);
  assert(!strcmp(serd_node_string(xc), "http://example.org/b"));
  serd_node_free(xc);

  SerdNode* lit = serd_new_string(SERD_LITERAL, "hello");
  assert(serd_env_set_prefix(env, b, lit));

  SerdNode* blank = serd_new_string(SERD_BLANK, "b1");
  assert(!serd_env_expand_node(env, blank));
  serd_node_free(blank);

  int n_prefixes = 0;
  serd_env_set_prefix_from_strings(env, "eg.2", "http://example.org/");
  serd_env_foreach(env, count_prefixes, &n_prefixes);
  assert(n_prefixes == 1);

  SerdNode* shorter_uri = serd_new_string(SERD_URI, "urn:foo");
  assert(!serd_env_qualify(env, shorter_uri, &prefix_node, &suffix));

  assert(!serd_env_set_base_uri(env, u));
  assert(serd_node_equals(serd_env_base_uri(env, NULL), u));
  assert(!serd_env_set_base_uri(env, NULL));
  assert(!serd_env_base_uri(env, NULL));

  serd_node_free(shorter_uri);
  serd_node_free(lit);
  serd_node_free(badpre);
  serd_node_free(s);
  serd_node_free(c);
  serd_node_free(b);
  serd_node_free(u);

  serd_env_free(env);
}

int
main(void)
{
  test_env();
  return 0;
}
