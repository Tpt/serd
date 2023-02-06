// Copyright 2011-2022 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#include "world.h"

#include "node.h"
#include "serd_config.h"
#include "system.h"

#include "serd/string_view.h"

#if defined(USE_POSIX_FADVISE)
#  include <fcntl.h>
#endif

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FILE*
serd_world_fopen(SerdWorld* world, const char* path, const char* mode)
{
  FILE* fd = fopen(path, mode);
  if (!fd) {
    char message[1024] = {0};
    serd_system_strerror(errno, message, sizeof(message));

    serd_world_errorf(
      world, SERD_ERR_INTERNAL, "failed to open file %s (%s)\n", path, message);
    return NULL;
  }

#if USE_POSIX_FADVISE && USE_FILENO
  (void)posix_fadvise(fileno(fd), 0, 0, POSIX_FADV_SEQUENTIAL);
#endif

  return fd;
}

SerdStatus
serd_world_error(const SerdWorld* const world, const SerdError* const e)
{
  if (world->error_func) {
    world->error_func(world->error_handle, e);
  } else {
    if (e->filename) {
      fprintf(stderr, "error: %s:%u:%u: ", e->filename, e->line, e->col);
    } else {
      fprintf(stderr, "error: ");
    }
    vfprintf(stderr, e->fmt, *e->args);
  }
  return e->status;
}

SerdStatus
serd_world_errorf(const SerdWorld* const world,
                  const SerdStatus       st,
                  const char* const      fmt,
                  ...)
{
  va_list args; // NOLINT(cppcoreguidelines-init-variables)
  va_start(args, fmt);
  const SerdError e = {st, NULL, 0, 0, fmt, &args};
  serd_world_error(world, &e);
  va_end(args);
  return st;
}

SerdWorld*
serd_world_new(void)
{
  SerdWorld* world = (SerdWorld*)calloc(1, sizeof(SerdWorld));

  world->blank_node = serd_new_blank(serd_string("b00000000000"));

  return world;
}

void
serd_world_free(SerdWorld* const world)
{
  if (world) {
    serd_node_free(world->blank_node);
    free(world);
  }
}

const SerdNode*
serd_world_get_blank(SerdWorld* const world)
{
#define BLANK_CHARS 12

  char* buf = serd_node_buffer(world->blank_node);
  memset(buf, 0, BLANK_CHARS + 1);

  world->blank_node->length =
    (size_t)snprintf(buf, BLANK_CHARS + 1, "b%u", ++world->next_blank_id);

  return world->blank_node;

#undef BLANK_CHARS
}

void
serd_world_set_error_func(SerdWorld*    world,
                          SerdErrorFunc error_func,
                          void*         handle)
{
  world->error_func   = error_func;
  world->error_handle = handle;
}
