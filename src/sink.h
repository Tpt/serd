// Copyright 2011-2020 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#ifndef SERD_SRC_SINK_H
#define SERD_SRC_SINK_H

#include "serd/event.h"
#include "serd/sink.h"
#include "serd/world.h"

/**
   An interface that receives a stream of RDF data.
*/
struct SerdSinkImpl {
  const SerdWorld* world;
  void*            handle;
  SerdFreeFunc     free_handle;
  SerdEventFunc    on_event;
};

#endif // SERD_SRC_SINK_H
