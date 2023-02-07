// Copyright 2011-2020 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#include "serd/env.h"

#include "env.h"
#include "node.h"

#include "serd/node.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  SerdNode* name;
  SerdNode* uri;
} SerdPrefix;

struct SerdEnvImpl {
  SerdPrefix* prefixes;
  size_t      n_prefixes;
  SerdNode*   base_uri_node;
  SerdURIView base_uri;
};

SerdEnv*
serd_env_new(const SerdStringView base_uri)
{
  SerdEnv* env = (SerdEnv*)calloc(1, sizeof(struct SerdEnvImpl));
  if (env && base_uri.len) {
    if (serd_env_set_base_uri(env, base_uri)) {
      free(env);
      return NULL;
    }
  }

  return env;
}

SerdEnv*
serd_env_copy(const SerdEnv* const env)
{
  if (!env) {
    return NULL;
  }

  SerdEnv* copy = (SerdEnv*)calloc(1, sizeof(struct SerdEnvImpl));
  if (copy) {
    copy->n_prefixes = env->n_prefixes;
    copy->prefixes = (SerdPrefix*)malloc(copy->n_prefixes * sizeof(SerdPrefix));
    for (size_t i = 0; i < copy->n_prefixes; ++i) {
      copy->prefixes[i].name = serd_node_copy(env->prefixes[i].name);
      copy->prefixes[i].uri  = serd_node_copy(env->prefixes[i].uri);
    }

    const SerdNode* const base = serd_env_base_uri(env);
    if (base) {
      serd_env_set_base_uri(copy, serd_node_string_view(base));
    }
  }

  return copy;
}

void
serd_env_free(SerdEnv* const env)
{
  if (!env) {
    return;
  }

  for (size_t i = 0; i < env->n_prefixes; ++i) {
    serd_node_free(env->prefixes[i].name);
    serd_node_free(env->prefixes[i].uri);
  }
  free(env->prefixes);
  serd_node_free(env->base_uri_node);
  free(env);
}

bool
serd_env_equals(const SerdEnv* const a, const SerdEnv* const b)
{
  if (!a || !b) {
    return !a == !b;
  }

  if (a->n_prefixes != b->n_prefixes ||
      !serd_node_equals(a->base_uri_node, b->base_uri_node)) {
    return false;
  }

  for (size_t i = 0; i < a->n_prefixes; ++i) {
    if (!serd_node_equals(a->prefixes[i].name, b->prefixes[i].name) ||
        !serd_node_equals(a->prefixes[i].uri, b->prefixes[i].uri)) {
      return false;
    }
  }

  return true;
}

SerdURIView
serd_env_base_uri_view(const SerdEnv* const env)
{
  return env->base_uri;
}

const SerdNode*
serd_env_base_uri(const SerdEnv* const env)
{
  return env ? env->base_uri_node : NULL;
}

SerdStatus
serd_env_set_base_uri(SerdEnv* const env, const SerdStringView uri)
{
  if (!uri.len) {
    serd_node_free(env->base_uri_node);
    env->base_uri_node = NULL;
    env->base_uri      = SERD_URI_NULL;
    return SERD_SUCCESS;
  }

  SerdNode* const old_base_uri = env->base_uri_node;

  // Resolve the new base against the current base in case it is relative
  const SerdURIView new_base_uri =
    serd_resolve_uri(serd_parse_uri(uri.buf), env->base_uri);

  // Replace the current base URI
  env->base_uri_node = serd_new_parsed_uri(new_base_uri);
  env->base_uri      = serd_node_uri_view(env->base_uri_node);

  serd_node_free(old_base_uri);
  return SERD_SUCCESS;
}

SERD_PURE_FUNC
static SerdPrefix*
serd_env_find(const SerdEnv* const env,
              const char* const    name,
              const size_t         name_len)
{
  for (size_t i = 0; i < env->n_prefixes; ++i) {
    const SerdNode* const prefix_name = env->prefixes[i].name;
    if (prefix_name->length == name_len) {
      if (!memcmp(serd_node_string(prefix_name), name, name_len)) {
        return &env->prefixes[i];
      }
    }
  }
  return NULL;
}

static void
serd_env_add(SerdEnv* const       env,
             const SerdStringView name,
             const SerdStringView uri)
{
  SerdPrefix* const prefix = serd_env_find(env, name.buf, name.len);
  if (prefix) {
    if (!!strcmp(serd_node_string(prefix->uri), uri.buf)) {
      serd_node_free(prefix->uri);
      prefix->uri = serd_new_uri(uri);
    }
  } else {
    env->prefixes = (SerdPrefix*)realloc(
      env->prefixes, (++env->n_prefixes) * sizeof(SerdPrefix));
    env->prefixes[env->n_prefixes - 1].name = serd_new_string(name);
    env->prefixes[env->n_prefixes - 1].uri  = serd_new_uri(uri);
  }
}

SerdStatus
serd_env_set_prefix(SerdEnv* const       env,
                    const SerdStringView name,
                    const SerdStringView uri)
{
  if (serd_uri_string_has_scheme(uri.buf)) {
    // Set prefix to absolute URI
    serd_env_add(env, name, uri);
    return SERD_SUCCESS;
  }

  if (!env->base_uri_node) {
    return SERD_ERR_BAD_ARG;
  }

  // Resolve relative URI and create a new node and URI for it
  SerdNode* const abs_uri = serd_new_resolved_uri(uri, env->base_uri);

  // Set prefix to resolved (absolute) URI
  serd_env_add(env, name, serd_node_string_view(abs_uri));
  serd_node_free(abs_uri);
  return SERD_SUCCESS;
}

SerdStatus
serd_env_qualify(const SerdEnv* const  env,
                 const SerdStringView  uri,
                 SerdStringView* const prefix,
                 SerdStringView* const suffix)
{
  for (size_t i = 0; i < env->n_prefixes; ++i) {
    const SerdNode* const prefix_uri     = env->prefixes[i].uri;
    const size_t          prefix_uri_len = serd_node_length(prefix_uri);
    if (uri.buf && uri.len >= prefix_uri_len) {
      const char* prefix_str = serd_node_string(prefix_uri);
      const char* uri_str    = uri.buf;

      if (!strncmp(uri_str, prefix_str, prefix_uri_len)) {
        *prefix     = serd_node_string_view(env->prefixes[i].name);
        suffix->buf = uri_str + prefix_uri_len;
        suffix->len = uri.len - prefix_uri_len;
        return SERD_SUCCESS;
      }
    }
  }

  return SERD_FAILURE;
}

SerdStatus
serd_env_expand_in_place(const SerdEnv* const  env,
                         const SerdStringView  curie,
                         SerdStringView* const uri_prefix,
                         SerdStringView* const uri_suffix)
{
  const char* const str = curie.buf;
  const char* const colon =
    str ? (const char*)memchr(str, ':', curie.len + 1) : NULL;
  if (!colon) {
    return SERD_ERR_BAD_ARG;
  }

  const size_t            name_len = (size_t)(colon - str);
  const SerdPrefix* const prefix   = serd_env_find(env, str, name_len);
  if (prefix) {
    uri_prefix->buf = serd_node_string(prefix->uri);
    uri_prefix->len = prefix->uri ? prefix->uri->length : 0;
    uri_suffix->buf = colon + 1;
    uri_suffix->len = curie.len - name_len - 1;
    return SERD_SUCCESS;
  }
  return SERD_ERR_BAD_CURIE;
}

static SerdNode*
expand_uri(const SerdEnv* env, const SerdNode* node)
{
  assert(serd_node_type(node) == SERD_URI);

  return serd_new_resolved_uri(serd_node_string_view(node), env->base_uri);
}

SerdNode*
serd_env_expand_curie(const SerdEnv* const env, const SerdStringView curie)
{
  if (!env) {
    return NULL;
  }

  SerdStringView prefix;
  SerdStringView suffix;
  if (serd_env_expand_in_place(env, curie, &prefix, &suffix)) {
    return NULL;
  }

  const size_t len = prefix.len + suffix.len;
  SerdNode*    ret = serd_node_malloc(len, 0, SERD_URI);
  char*        buf = serd_node_buffer(ret);

  snprintf(buf, len + 1, "%s%s", prefix.buf, suffix.buf);
  ret->length = len;
  return ret;
}

SerdNode*
serd_env_expand_node(const SerdEnv* const env, const SerdNode* const node)
{
  if (!env || !node) {
    return NULL;
  }

  switch (node->type) {
  case SERD_LITERAL:
    break;
  case SERD_URI:
    return expand_uri(env, node);
  case SERD_BLANK:
  case SERD_VARIABLE:
    break;
  }

  return NULL;
}

void
serd_env_write_prefixes(const SerdEnv* const env, const SerdSink* const sink)
{
  for (size_t i = 0; i < env->n_prefixes; ++i) {
    serd_sink_write_prefix(sink, env->prefixes[i].name, env->prefixes[i].uri);
  }
}
