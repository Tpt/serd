// Copyright 2011-2023 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#include "serd_config.h"
#include "system.h"

#include "serd/env.h"
#include "serd/error.h"
#include "serd/node.h"
#include "serd/reader.h"
#include "serd/status.h"
#include "serd/stream.h"
#include "serd/string_view.h"
#include "serd/syntax.h"
#include "serd/world.h"
#include "serd/writer.h"

#ifdef _WIN32
#  ifdef _MSC_VER
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <fcntl.h>
#  include <io.h>
#endif

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

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
    "  -a           Write ASCII output if possible.\n"
    "  -b           Fast bulk output for large serialisations.\n"
    "  -c PREFIX    Chop PREFIX from matching blank node IDs.\n"
    "  -e           Eat input one character at a time.\n"
    "  -f           Keep full URIs in input (don't qualify).\n"
    "  -h           Display this help and exit.\n"
    "  -i SYNTAX    Input syntax: turtle/ntriples/trig/nquads.\n"
    "  -k BYTES     Parser stack size.\n"
    "  -l           Lax (non-strict) parsing.\n"
    "  -o SYNTAX    Output syntax: turtle/ntriples/nquads.\n"
    "  -p PREFIX    Add PREFIX to blank node IDs.\n"
    "  -q           Suppress all output except data.\n"
    "  -r ROOT_URI  Keep relative URIs within ROOT_URI.\n"
    "  -s INPUT     Parse INPUT as string (terminates options).\n"
    "  -t           Write terser output without newlines.\n"
    "  -v           Display version information and exit.\n";

  FILE* const os = error ? stderr : stdout;
  fprintf(os, "%s", error ? "\n" : "");
  fprintf(os, "Usage: %s [OPTION]... INPUT [BASE_URI]\n", name);
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

int
main(int argc, char** argv)
{
  const char* const prog = argv[0];

  SerdSyntax      input_syntax  = (SerdSyntax)0;
  SerdSyntax      output_syntax = (SerdSyntax)0;
  SerdWriterFlags writer_flags  = 0;
  bool            from_string   = false;
  bool            from_stdin    = false;
  bool            bulk_read     = true;
  bool            lax           = false;
  bool            quiet         = false;
  size_t          stack_size    = 4194304;
  const char*     add_prefix    = NULL;
  const char*     chop_prefix   = NULL;
  const char*     root_uri      = NULL;
  int             a             = 1;
  for (; a < argc && !from_string && argv[a][0] == '-'; ++a) {
    if (argv[a][1] == '\0') {
      from_stdin = true;
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
        writer_flags |= SERD_WRITE_LAX;
        lax = true;
      } else if (opt == 'q') {
        quiet = true;
      } else if (opt == 't') {
        writer_flags |= SERD_WRITE_TERSE;
      } else if (opt == 'v') {
        return print_version();
      } else if (opt == 's') {
        from_string = true;
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
        if (argv[a][o + 1] || ++a == argc) {
          return missing_arg(prog, 'o');
        }

        if (!(output_syntax = serd_syntax_by_name(argv[a]))) {
          return print_usage(prog, true);
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
      } else {
        SERDI_ERRORF("invalid option -- '%s'\n", argv[a] + 1);
        return print_usage(prog, true);
      }
    }
  }

  if (a == argc) {
    SERDI_ERROR("missing input\n");
    return print_usage(prog, true);
  }

#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif

  const char* input = argv[a++];

  if (!input_syntax && !(input_syntax = serd_guess_syntax(input))) {
    input_syntax = SERD_TRIG;
  }

  const bool input_has_graphs = serd_syntax_has_graphs(input_syntax);
  if (!output_syntax) {
    output_syntax = input_has_graphs ? SERD_NQUADS : SERD_NTRIPLES;
  }

  SerdNode* base = NULL;
  if (a < argc) { // Base URI given on command line
    base = serd_new_uri(serd_string(argv[a]));
  } else if (!from_string && !from_stdin) { // Use input file URI
    base = serd_new_file_uri(serd_string(input), serd_empty_string());
  }

  FILE* const      out_fd = stdout;
  SerdWorld* const world  = serd_world_new();
  SerdEnv* const   env =
    serd_env_new(base ? serd_node_string_view(base) : serd_empty_string());

  SerdWriter* const writer = serd_writer_new(
    world, output_syntax, writer_flags, env, (SerdWriteFunc)fwrite, out_fd);

  SerdReader* const reader =
    serd_reader_new(world, input_syntax, serd_writer_sink(writer), stack_size);

  serd_reader_set_strict(reader, !lax);
  if (quiet) {
    serd_world_set_error_func(world, quiet_error_func, NULL);
  }

  if (root_uri) {
    SerdNode* const root = serd_new_uri(serd_string(root_uri));
    serd_writer_set_root_uri(writer, root);
    serd_node_free(root);
  }

  serd_writer_chop_blank_prefix(writer, chop_prefix);
  serd_reader_add_blank_prefix(reader, add_prefix);

  SerdStatus st         = SERD_SUCCESS;
  SerdNode*  input_name = NULL;
  if (from_string) {
    input_name = serd_new_string(serd_string("string"));
    st         = serd_reader_start_string(reader, input, input_name);
  } else if (from_stdin) {
    input_name = serd_new_string(serd_string("stdin"));
    st         = serd_reader_start_stream(reader,
                                  serd_file_read_byte,
                                  (SerdStreamErrorFunc)ferror,
                                  stdin,
                                  input_name,
                                  1);
  } else {
    st = serd_reader_start_file(reader, input, bulk_read);
  }

  if (!st) {
    st = serd_reader_read_document(reader);
  }

  serd_reader_finish(reader);
  serd_reader_free(reader);
  serd_writer_finish(writer);
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
