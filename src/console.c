// Copyright 2011-2021 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#include "console.h"

#include "serd/serd.h"
#include "zix/allocator.h"
#include "zix/filesystem.h"

#ifdef _WIN32
#  ifdef _MSC_VER
#    define WIN32_LEAN_AND_MEAN 1
#  endif
#  include <fcntl.h>
#  include <io.h>
#endif

void
serd_set_stream_utf8_mode(FILE* const stream)
{
#ifdef _WIN32
  _setmode(_fileno(stream), _O_BINARY);
#else
  (void)stream;
#endif
}

int
serd_print_version(const char* const program)
{
  printf("%s %d.%d.%d <http://drobilla.net/software/serd>\n",
         program,
         SERD_MAJOR_VERSION,
         SERD_MINOR_VERSION,
         SERD_MICRO_VERSION);

  printf("Copyright 2011-2023 David Robillard <d@drobilla.net>.\n"
         "License: <http://www.opensource.org/licenses/isc>\n"
         "This is free software; you are free to change and redistribute it.\n"
         "There is NO WARRANTY, to the extent permitted by law.\n");

  return 0;
}

SerdStatus
serd_set_base_uri_from_path(SerdEnv* const env, const char* const path)
{
  char* const input_path = zix_canonical_path(NULL, path);
  if (!input_path) {
    return SERD_ERR_BAD_ARG;
  }

  SerdNode* const file_uri =
    serd_new_file_uri(serd_string(input_path), serd_empty_string());

  serd_env_set_base_uri(env, serd_node_string_view(file_uri));
  serd_node_free(file_uri);
  zix_free(NULL, input_path);

  return SERD_SUCCESS;
}
