// Copyright 2011-2023 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#include "serd_config.h"
#include "system.h"

#include "serd/byte_source.h"
#include "serd/env.h"
#include "serd/error.h"
#include "serd/node.h"
#include "serd/reader.h"
#include "serd/sink.h"
#include "serd/status.h"
#include "serd/stream.h"
#include "serd/string_view.h"
#include "serd/syntax.h"
#include "serd/world.h"
#include "serd/writer.h"
#include "zix/allocator.h"
#include "zix/filesystem.h"

#ifdef _WIN32
#  ifdef _MSC_VER
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <fcntl.h>
#  include <io.h>
#endif

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SERDI_ERROR(msg) fprintf(stderr, "serdi: " msg)
#define SERDI_ERRORF(fmt, ...) fprintf(stderr, "serdi: " fmt, __VA_ARGS__)

static int
print_version(void)
{
  printf("serdi " SERD_VERSION " <http://drobilla.net/software/serd>\n");
  printf("Copyright 2011-2023 David Robillard <d@drobilla.net>.\n"
         "License ISC: <https://spdx.org/licenses/ISC>.\n"
         "This is free software; you are free to change and redistribute it."
         "\nThere is NO WARRANTY, to the extent permitted by law.\n");
  return 0;
}

static int
print_usage(const char* const name, const bool error)
{
  static const char* const description =
    "Read and write RDF syntax.\n"
    "Use - for INPUT to read from standard input.\n\n"
    "  -I BASE_URI  Input base URI.\n"
    "  -a           Write ASCII output if possible.\n"
    "  -b           Fast bulk output for large serialisations.\n"
    "  -c PREFIX    Chop PREFIX from matching blank node IDs.\n"
    "  -e           Eat input one character at a time.\n"
    "  -f           Keep full URIs in input (don't qualify).\n"
    "  -h           Display this help and exit.\n"
    "  -i SYNTAX    Input syntax: turtle/ntriples/trig/nquads.\n"
    "  -k BYTES     Parser stack size.\n"
    "  -l           Lax (non-strict) parsing.\n"
    "  -o SYNTAX    Output syntax: empty/turtle/ntriples/nquads.\n"
    "  -p PREFIX    Add PREFIX to blank node IDs.\n"
    "  -q           Suppress all output except data.\n"
    "  -r ROOT_URI  Keep relative URIs within ROOT_URI.\n"
    "  -s STRING    Parse STRING as input.\n"
    "  -t           Write terser output without newlines.\n"
    "  -v           Display version information and exit.\n";

  FILE* const os = error ? stderr : stdout;
  fprintf(os, "%s", error ? "\n" : "");
  fprintf(os, "Usage: %s [OPTION]... INPUT...\n", name);
  fprintf(os, "%s", description);
  return error ? 1 : 0;
}

static int
missing_arg(const char* const name, const char opt)
{
  SERDI_ERRORF("option requires an argument -- '%c'\n", opt);
  return print_usage(name, true);
}

static SerdStatus
quiet_error_func(void* const handle, const SerdError* const e)
{
  (void)handle;
  (void)e;
  return SERD_SUCCESS;
}

static SerdStatus
read_file(SerdWorld* const      world,
          SerdSyntax            syntax,
          const SerdReaderFlags flags,
          const SerdSink* const sink,
          const size_t          stack_size,
          const char* const     filename,
          const char* const     add_prefix,
          const bool            bulk_read)
{
  syntax = syntax ? syntax : serd_guess_syntax(filename);
  syntax = syntax ? syntax : SERD_TRIG;

  SerdByteSource* byte_source = NULL;
  if (!strcmp(filename, "-")) {
    SerdNode* name = serd_new_string(serd_string("stdin"));

    byte_source = serd_byte_source_new_function(
      serd_file_read_byte, (SerdStreamErrorFunc)ferror, NULL, stdin, name, 1);

    serd_node_free(name);
  } else {
    byte_source =
      serd_byte_source_new_filename(filename, bulk_read ? SERD_PAGE_SIZE : 1U);
  }

  if (!byte_source) {
    SERDI_ERRORF(
      "failed to open input file `%s' (%s)\n", filename, strerror(errno));

    return SERD_ERR_UNKNOWN;
  }

  SerdReader* reader = serd_reader_new(world, syntax, flags, sink, stack_size);

  serd_reader_add_blank_prefix(reader, add_prefix);

  SerdStatus st = serd_reader_start(reader, byte_source);

  st = st ? st : serd_reader_read_document(reader);

  serd_reader_free(reader);
  serd_byte_source_free(byte_source);

  return st;
}

int
main(int argc, char** argv)
{
  const char* const prog = argv[0];

  SerdNode*       base          = NULL;
  SerdSyntax      input_syntax  = SERD_SYNTAX_EMPTY;
  SerdSyntax      output_syntax = SERD_SYNTAX_EMPTY;
  SerdReaderFlags reader_flags  = 0;
  SerdWriterFlags writer_flags  = 0;
  bool            bulk_read     = true;
  bool            osyntax_set   = false;
  bool            quiet         = false;
  size_t          stack_size    = 4194304;
  const char*     input_string  = NULL;
  const char*     add_prefix    = "";
  const char*     chop_prefix   = NULL;
  const char*     root_uri      = NULL;
  int             a             = 1;
  for (; a < argc && argv[a][0] == '-'; ++a) {
    if (argv[a][1] == '\0') {
      break;
    }

    for (int o = 1; argv[a][o]; ++o) {
      const char opt = argv[a][o];

      if (opt == 'a') {
        writer_flags |= SERD_WRITE_ASCII;
      } else if (opt == 'b') {
        writer_flags |= SERD_WRITE_BULK;
      } else if (opt == 'e') {
        bulk_read = false;
      } else if (opt == 'f') {
        writer_flags |= (SERD_WRITE_UNQUALIFIED | SERD_WRITE_UNRESOLVED);
      } else if (opt == 'h') {
        return print_usage(prog, false);
      } else if (opt == 'l') {
        reader_flags |= SERD_READ_LAX;
        writer_flags |= SERD_WRITE_LAX;
      } else if (opt == 'q') {
        quiet = true;
      } else if (opt == 't') {
        writer_flags |= SERD_WRITE_TERSE;
      } else if (opt == 'v') {
        return print_version();
      } else if (argv[a][1] == 'I') {
        if (++a == argc) {
          return missing_arg(prog, 'I');
        }

        base = serd_new_uri(serd_string(argv[a]));
        break;
      } else if (opt == 'c') {
        if (argv[a][o + 1] || ++a == argc) {
          return missing_arg(prog, 'c');
        }

        chop_prefix = argv[a];
        break;
      } else if (opt == 'i') {
        if (argv[a][o + 1] || ++a == argc) {
          return missing_arg(prog, 'i');
        }

        if (!(input_syntax = serd_syntax_by_name(argv[a]))) {
          return print_usage(prog, true);
        }
        break;
      } else if (opt == 'k') {
        if (argv[a][o + 1] || ++a == argc) {
          return missing_arg(prog, 'k');
        }

        char*      endptr = NULL;
        const long size   = strtol(argv[a], &endptr, 10);
        if (size <= 0 || size == LONG_MAX || *endptr != '\0') {
          SERDI_ERRORF("invalid stack size '%s'\n", argv[a]);
          return 1;
        }
        stack_size = (size_t)size;
        break;
      } else if (opt == 'o') {
        osyntax_set = true;
        if (argv[a][o + 1] || ++a == argc) {
          return missing_arg(prog, 'o');
        }

        if (!strcmp(argv[a], "empty")) {
          output_syntax = SERD_SYNTAX_EMPTY;
        } else if (!(output_syntax = serd_syntax_by_name(argv[a]))) {
          return print_usage(argv[0], true);
        }
        break;
      } else if (opt == 'p') {
        if (argv[a][o + 1] || ++a == argc) {
          return missing_arg(prog, 'p');
        }

        add_prefix = argv[a];
        break;
      } else if (opt == 'r') {
        if (argv[a][o + 1] || ++a == argc) {
          return missing_arg(prog, 'r');
        }

        root_uri = argv[a];
        break;
      } else if (opt == 's') {
        if (argv[a][o + 1] || ++a == argc) {
          return missing_arg(prog, 's');
        }

        input_string = argv[a];
        break;
      } else {
        SERDI_ERRORF("invalid option -- '%s'\n", argv[a] + 1);
        return print_usage(prog, true);
      }
    }
  }

  if (a == argc && !input_string) {
    SERDI_ERROR("missing input\n");
    return print_usage(prog, true);
  }

#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif

  char* const* const inputs   = argv + a;
  const int          n_inputs = argc - a;

  bool input_has_graphs = serd_syntax_has_graphs(input_syntax);
  for (int i = a; i < argc; ++i) {
    if (serd_syntax_has_graphs(serd_guess_syntax(argv[i]))) {
      input_has_graphs = true;
      break;
    }
  }

  if (!output_syntax && !osyntax_set) {
    output_syntax = input_has_graphs ? SERD_NQUADS : SERD_NTRIPLES;
  }

  if (!base && n_inputs == 1 &&
      (output_syntax == SERD_NQUADS || output_syntax == SERD_NTRIPLES)) {
    // Choose base URI from the single input path
    char* const input_path = zix_canonical_path(NULL, inputs[0]);
    if (!input_path || !(base = serd_new_file_uri(serd_string(input_path),
                                                  serd_empty_string()))) {
      SERDI_ERRORF("unable to determine base URI from path %s\n", inputs[0]);
    }
    zix_free(NULL, input_path);
  }

  FILE* const      out_fd = stdout;
  SerdWorld* const world  = serd_world_new();
  SerdEnv* const   env =
    serd_env_new(base ? serd_node_string_view(base) : serd_empty_string());

  SerdWriter* const writer = serd_writer_new(
    world, output_syntax, writer_flags, env, (SerdWriteFunc)fwrite, out_fd);

  if (quiet) {
    serd_world_set_error_func(world, quiet_error_func, NULL);
  }

  if (root_uri) {
    SerdNode* const root = serd_new_uri(serd_string(root_uri));
    serd_writer_set_root_uri(writer, root);
    serd_node_free(root);
  }

  serd_writer_chop_blank_prefix(writer, chop_prefix);

  SerdStatus st         = SERD_SUCCESS;
  SerdNode*  input_name = NULL;
  if (input_string) {
    SerdByteSource* const byte_source =
      serd_byte_source_new_string(input_string, NULL);

    SerdReader* const reader =
      serd_reader_new(world,
                      input_syntax ? input_syntax : SERD_TRIG,
                      reader_flags,
                      serd_writer_sink(writer),
                      stack_size);

    serd_reader_add_blank_prefix(reader, add_prefix);

    if (!(st = serd_reader_start(reader, byte_source))) {
      st = serd_reader_read_document(reader);
    }

    serd_reader_free(reader);
    serd_byte_source_free(byte_source);
  }

  size_t prefix_len = 0;
  char*  prefix     = NULL;
  if (n_inputs > 1) {
    prefix_len = 8 + strlen(add_prefix);
    prefix     = (char*)calloc(1, prefix_len);
  }

  for (int i = 0; !st && i < n_inputs; ++i) {
    if (!base && !!strcmp(inputs[i], "-")) {
      char* const input_path = zix_canonical_path(NULL, inputs[i]);
      if (!input_path) {
        SERDI_ERRORF("failed to resolve path %s\n", inputs[i]);
        st = SERD_ERR_BAD_ARG;
        break;
      }

      SerdNode* const file_uri =
        serd_new_file_uri(serd_string(input_path), serd_empty_string());

      serd_env_set_base_uri(env, serd_node_string_view(file_uri));
      serd_node_free(file_uri);
      zix_free(NULL, input_path);
    }

    if (n_inputs > 1) {
      snprintf(prefix, prefix_len, "f%d%s", i, add_prefix);
    }

    if ((st = read_file(world,
                        input_syntax,
                        reader_flags,
                        serd_writer_sink(writer),
                        stack_size,
                        inputs[i],
                        n_inputs > 1 ? prefix : add_prefix,
                        bulk_read))) {
      break;
    }
  }
  free(prefix);

  serd_writer_free(writer);
  serd_node_free(input_name);
  serd_env_free(env);
  serd_node_free(base);
  serd_world_free(world);

  if (fclose(stdout)) {
    perror("serdi: write error");
    st = SERD_ERR_UNKNOWN;
  }

  return (st > SERD_FAILURE) ? 1 : 0;
}
