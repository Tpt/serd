// Copyright 2011-2020 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#ifndef SERD_SRC_ENV_H
#define SERD_SRC_ENV_H

#include "serd/attributes.h"
#include "serd/env.h"
#include "serd/uri.h"

SERD_PURE_FUNC
SerdURIView
serd_env_base_uri_view(const SerdEnv* env);

#endif // SERD_SRC_ENV_H
