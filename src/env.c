// Copyright 2011-2020 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#include "serd/env.h"

#include "env.h"
#include "memory.h"
#include "node.h"
#include "world.h"

#include "serd/node.h"
#include "serd/nodes.h"
#include "zix/allocator.h"
#include "zix/filesystem.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>

typedef struct {
  const SerdNode* name;
  const SerdNode* uri;
} SerdPrefix;

struct SerdEnvImpl {
  SerdWorld*      world;
  SerdNodes*      nodes;
  SerdPrefix*     prefixes;
  size_t          n_prefixes;
  const SerdNode* base_uri_node;
  SerdURIView     base_uri;
};

SerdEnv*
serd_env_new(SerdWorld* const world, const ZixStringView base_uri)
{
  assert(world);

  SerdEnv* env = (SerdEnv*)serd_wcalloc(world, 1, sizeof(struct SerdEnvImpl));

  if (env) {
    env->world = world;
    if (!(env->nodes = serd_nodes_new(world->allocator))) {
      serd_wfree(world, env);
      return NULL;
    }

    if (base_uri.length) {
      if (serd_env_set_base_uri(env, base_uri)) {
        serd_nodes_free(env->nodes);
        serd_wfree(world, env);
        return NULL;
      }
    }
  }

  return env;
}

SerdEnv*
serd_env_copy(ZixAllocator* const allocator, const SerdEnv* const env)
{
  if (!env) {
    return NULL;
  }

  SerdEnv* copy =
    (SerdEnv*)zix_calloc(allocator, 1, sizeof(struct SerdEnvImpl));

  if (copy) {
    copy->world      = env->world;
    copy->n_prefixes = env->n_prefixes;

    if (!(copy->nodes = serd_nodes_new(allocator))) {
      serd_wfree(env->world, copy);
      return NULL;
    }

    if (!(copy->prefixes = (SerdPrefix*)zix_malloc(
            allocator, copy->n_prefixes * sizeof(SerdPrefix)))) {
      serd_nodes_free(copy->nodes);
      serd_wfree(env->world, copy);
      return NULL;
    }

    for (size_t i = 0; i < copy->n_prefixes; ++i) {
      copy->prefixes[i].name =
        serd_nodes_intern(copy->nodes, env->prefixes[i].name);

      copy->prefixes[i].uri =
        serd_nodes_intern(copy->nodes, env->prefixes[i].uri);
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
  if (env) {
    serd_wfree(env->world, env->prefixes);
    serd_nodes_free(env->nodes);
    serd_wfree(env->world, env);
  }
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

SerdWorld*
serd_env_world(const SerdEnv* const env)
{
  return env->world;
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
serd_env_set_base_uri(SerdEnv* const env, const ZixStringView uri)
{
  assert(env);

  if (!uri.length) {
    serd_nodes_deref(env->nodes, env->base_uri_node);
    env->base_uri_node = NULL;
    env->base_uri      = SERD_URI_NULL;
    return SERD_SUCCESS;
  }

  const SerdNode* const old_base_uri = env->base_uri_node;

  // Resolve the new base against the current base in case it is relative
  const SerdURIView new_base_uri =
    serd_resolve_uri(serd_parse_uri(uri.data), env->base_uri);

  // Replace the current base URI
  if ((env->base_uri_node =
         serd_nodes_get(env->nodes, serd_a_parsed_uri(new_base_uri)))) {
    env->base_uri = serd_node_uri_view(env->base_uri_node);
  } else {
    return SERD_BAD_ALLOC;
  }

  serd_nodes_deref(env->nodes, old_base_uri);
  return SERD_SUCCESS;
}

SerdStatus
serd_env_set_base_path(SerdEnv* const env, const ZixStringView path)
{
  assert(env);

  if (!path.data || !path.length) {
    return serd_env_set_base_uri(env, zix_empty_string());
  }

  char* const real_path = zix_canonical_path(NULL, path.data);
  if (!real_path) {
    return SERD_BAD_ARG;
  }

  const size_t real_path_len = strlen(real_path);
  SerdNode*    base_node     = NULL;
  const char   path_last     = path.data[path.length - 1];
  if (path_last == '/' || path_last == '\\') {
    char* const base_path =
      (char*)serd_wcalloc(env->world, real_path_len + 2, 1);

    memcpy(base_path, real_path, real_path_len + 1);
    base_path[real_path_len] = path_last;

    base_node = serd_node_new(
      NULL, serd_a_file_uri(zix_string(base_path), zix_empty_string()));

    serd_wfree(env->world, base_path);
  } else {
    base_node = serd_node_new(
      NULL, serd_a_file_uri(zix_string(real_path), zix_empty_string()));
  }

  serd_env_set_base_uri(env, serd_node_string_view(base_node));
  serd_node_free(NULL, base_node);
  zix_free(NULL, real_path);

  return SERD_SUCCESS;
}

ZIX_PURE_FUNC
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

static SerdStatus
serd_env_add(SerdEnv* const        env,
             const ZixStringView   name,
             const SerdNode* const uri)
{
  SerdPrefix* const prefix = serd_env_find(env, name.data, name.length);
  if (prefix) {
    if (!!strcmp(serd_node_string(prefix->uri), serd_node_string(uri))) {
      serd_nodes_deref(env->nodes, prefix->uri);
      prefix->uri = uri;
    }
  } else {
    const SerdNode* const name_node =
      serd_nodes_get(env->nodes, serd_a_string_view(name));

    if (!name_node) {
      return SERD_BAD_ALLOC;
    }

    SerdPrefix* const new_prefixes = (SerdPrefix*)serd_wrealloc(
      env->world, env->prefixes, (env->n_prefixes + 1) * sizeof(SerdPrefix));

    if (!new_prefixes) {
      return SERD_BAD_ALLOC;
    }

    new_prefixes[env->n_prefixes].name = name_node;
    new_prefixes[env->n_prefixes].uri  = uri;
    env->prefixes                      = new_prefixes;
    ++env->n_prefixes;
  }

  return SERD_SUCCESS;
}

SerdStatus
serd_env_set_prefix(SerdEnv* const      env,
                    const ZixStringView name,
                    const ZixStringView uri)
{
  assert(env);

  if (serd_uri_string_has_scheme(uri.data)) {
    // Set prefix to absolute URI
    const SerdNode* const abs_uri = serd_nodes_get(env->nodes, serd_a_uri(uri));
    if (!abs_uri) {
      return SERD_BAD_ALLOC;
    }

    return serd_env_add(env, name, abs_uri);
  }

  if (!env->base_uri_node) {
    return SERD_BAD_ARG;
  }

  // Resolve potentially relative URI reference to an absolute URI
  const SerdURIView uri_view     = serd_parse_uri(uri.data);
  const SerdURIView abs_uri_view = serd_resolve_uri(uri_view, env->base_uri);
  assert(abs_uri_view.scheme.length);

  // Serialise absolute URI to a new node
  const SerdNode* const abs_uri =
    serd_nodes_get(env->nodes, serd_a_parsed_uri(abs_uri_view));

  if (!abs_uri) {
    return SERD_BAD_ALLOC;
  }

  assert(serd_uri_string_has_scheme(serd_node_string(abs_uri)));

  // Set prefix to resolved absolute URI
  return serd_env_add(env, name, abs_uri);
}

SerdStatus
serd_env_unset_prefix(SerdEnv* const env, const ZixStringView name)
{
  // Find matching prefix
  SerdPrefix* const prefix = serd_env_find(env, name.data, name.length);
  if (!prefix) {
    return SERD_SUCCESS;
  }

  // Drop prefix's URI node and clear if it's the only one
  serd_nodes_deref(env->nodes, prefix->uri);
  if (env->n_prefixes == 1U) {
    serd_wfree(env->world, env->prefixes);
    env->prefixes   = NULL;
    env->n_prefixes = 0U;
    return SERD_SUCCESS;
  }

  // Replace the prefix entry with the last one (if applicable)
  const size_t index          = (size_t)(prefix - env->prefixes);
  const size_t new_n_prefixes = env->n_prefixes - 1U;
  if (index < new_n_prefixes) {
    env->prefixes[index] = env->prefixes[new_n_prefixes];
  }

  // Shrink prefixes array
  SerdPrefix* const new_prefixes = (SerdPrefix*)serd_wrealloc(
    env->world, env->prefixes, new_n_prefixes * sizeof(SerdPrefix));

  if (new_prefixes) {
    env->prefixes   = new_prefixes;
    env->n_prefixes = new_n_prefixes;
  }

  return new_prefixes ? SERD_SUCCESS : SERD_BAD_ALLOC;
}

SerdStatus
serd_env_qualify(const SerdEnv* const env,
                 const ZixStringView  uri,
                 ZixStringView* const prefix,
                 ZixStringView* const suffix)
{
  for (size_t i = 0; i < env->n_prefixes; ++i) {
    const SerdNode* const prefix_uri     = env->prefixes[i].uri;
    const size_t          prefix_uri_len = serd_node_length(prefix_uri);
    if (uri.data && uri.length >= prefix_uri_len) {
      const char* prefix_str = serd_node_string(prefix_uri);
      const char* uri_str    = uri.data;

      if (!strncmp(uri_str, prefix_str, prefix_uri_len)) {
        *prefix        = serd_node_string_view(env->prefixes[i].name);
        suffix->data   = uri_str + prefix_uri_len;
        suffix->length = uri.length - prefix_uri_len;
        return SERD_SUCCESS;
      }
    }
  }

  return SERD_FAILURE;
}

SerdStatus
serd_env_expand_in_place(const SerdEnv* const env,
                         const ZixStringView  curie,
                         ZixStringView* const uri_prefix,
                         ZixStringView* const uri_suffix)
{
  const char* const str = curie.data;
  const char* const colon =
    str ? (const char*)memchr(str, ':', curie.length + 1) : NULL;
  if (!colon) {
    return SERD_BAD_ARG;
  }

  const size_t            name_len = (size_t)(colon - str);
  const SerdPrefix* const prefix   = serd_env_find(env, str, name_len);
  if (!prefix || !prefix->uri) {
    return SERD_BAD_CURIE;
  }

  uri_prefix->data   = prefix->uri ? serd_node_string(prefix->uri) : "";
  uri_prefix->length = prefix->uri ? prefix->uri->length : 0;
  uri_suffix->data   = colon + 1;
  uri_suffix->length = curie.length - name_len - 1;
  return SERD_SUCCESS;
}

SerdNode*
serd_env_expand_curie(const SerdEnv* const env, const ZixStringView curie)
{
  if (!env) {
    return NULL;
  }

  ZixStringView prefix = zix_empty_string();
  ZixStringView suffix = zix_empty_string();
  SerdStatus    st     = serd_env_expand_in_place(env, curie, &prefix, &suffix);
  if (st || !prefix.data || !suffix.data) {
    return NULL;
  }

  const size_t len = prefix.length + suffix.length;
  SerdNode*    ret =
    serd_node_malloc(env->world->allocator, sizeof(SerdNode) + len + 1);

  if (ret) {
    ret->length = len;
    ret->flags  = 0U;
    ret->type   = SERD_URI;

    char* const string = serd_node_buffer(ret);
    assert(prefix.data);
    memcpy(string, prefix.data, prefix.length);
    memcpy(string + prefix.length, suffix.data, suffix.length);
  }

  return ret;
}

SerdNode*
serd_env_expand_node(const SerdEnv* const env, const SerdNode* const node)
{
  if (!env || !node || node->type != SERD_URI) {
    return NULL;
  }

  const SerdURIView uri     = serd_node_uri_view(node);
  const SerdURIView abs_uri = serd_resolve_uri(uri, env->base_uri);
  if (!abs_uri.scheme.length) {
    return NULL;
  }

  const SerdWriteResult r =
    serd_node_construct(0U, NULL, serd_a_parsed_uri(abs_uri));

  SerdNode* const expanded = serd_node_try_malloc(env->world->allocator, r);
  if (expanded) {
    serd_node_construct(r.count, expanded, serd_a_parsed_uri(abs_uri));
  }

  return expanded;
}

SerdStatus
serd_env_write_prefixes(const SerdEnv* const env, const SerdSink* const sink)
{
  assert(env);
  assert(sink);

  SerdStatus st = SERD_SUCCESS;

  for (size_t i = 0; !st && i < env->n_prefixes; ++i) {
    const SerdPrefix* const prefix = &env->prefixes[i];

    st = serd_sink_write_prefix(sink, prefix->name, prefix->uri);
  }

  return st;
}
