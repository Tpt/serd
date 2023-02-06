// Copyright 2011-2023 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#include "serd_config.h"
#include "string_utils.h"
#include "system.h"

#include "serd/attributes.h"
#include "serd/env.h"
#include "serd/error.h"
#include "serd/node.h"
#include "serd/reader.h"
#include "serd/status.h"
#include "serd/stream.h"
#include "serd/string_view.h"
#include "serd/syntax.h"
#include "serd/writer.h"

#ifdef _WIN32
#  ifdef _MSC_VER
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <fcntl.h>
#  include <io.h>
#endif

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define SERDI_ERROR(msg) fprintf(stderr, "serdi: " msg)
#define SERDI_ERRORF(fmt, ...) fprintf(stderr, "serdi: " fmt, __VA_ARGS__)

typedef struct {
  SerdSyntax  syntax;
  const char* name;
  const char* extension;
} Syntax;

static const Syntax syntaxes[] = {{SERD_TURTLE, "turtle", ".ttl"},
                                  {SERD_NTRIPLES, "ntriples", ".nt"},
                                  {SERD_NQUADS, "nquads", ".nq"},
                                  {SERD_TRIG, "trig", ".trig"},
                                  {(SerdSyntax)0, NULL, NULL}};

static SerdSyntax
get_syntax(const char* const name)
{
  for (const Syntax* s = syntaxes; s->name; ++s) {
    if (!serd_strncasecmp(s->name, name, strlen(name))) {
      return s->syntax;
    }
  }

  SERDI_ERRORF("unknown syntax '%s'\n", name);
  return (SerdSyntax)0;
}

static SERD_PURE_FUNC
SerdSyntax
guess_syntax(const char* const filename)
{
  const char* ext = strrchr(filename, '.');
  if (ext) {
    for (const Syntax* s = syntaxes; s->name; ++s) {
      if (!serd_strncasecmp(s->extension, ext, strlen(ext))) {
        return s->syntax;
      }
    }
  }

  return (SerdSyntax)0;
}

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
    "  -l           Lax (non-strict) parsing.\n"
    "  -o SYNTAX    Output syntax: turtle/ntriples/nquads.\n"
    "  -p PREFIX    Add PREFIX to blank node IDs.\n"
    "  -q           Suppress all output except data.\n"
    "  -r ROOT_URI  Keep relative URIs within ROOT_URI.\n"
    "  -s INPUT     Parse INPUT as string (terminates options).\n"
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
quiet_error_sink(void* const handle, const SerdError* const e)
{
  (void)handle;
  (void)e;
  return SERD_SUCCESS;
}

static SerdWriterFlags
choose_style(const SerdSyntax input_syntax,
             const SerdSyntax output_syntax,
             const bool       ascii,
             const bool       bulk_write,
             const bool       full_uris)
{
  SerdWriterFlags writer_flags = 0U;
  if (output_syntax == SERD_NTRIPLES || ascii) {
    writer_flags |= SERD_WRITE_ASCII;
  } else if (output_syntax == SERD_TURTLE) {
    writer_flags |= SERD_WRITE_ABBREVIATED;
    if (!full_uris) {
      writer_flags |= SERD_WRITE_CURIED;
    }
  }

  if ((input_syntax == SERD_TURTLE || input_syntax == SERD_TRIG) ||
      (writer_flags & SERD_WRITE_CURIED)) {
    // Base URI may change and/or we're abbreviating URIs, so must resolve
    writer_flags |= SERD_WRITE_RESOLVED;
  }

  if (bulk_write) {
    writer_flags |= SERD_WRITE_BULK;
  }

  return writer_flags;
}

int
main(int argc, char** argv)
{
  const char* const prog = argv[0];

  SerdSyntax  input_syntax  = (SerdSyntax)0;
  SerdSyntax  output_syntax = (SerdSyntax)0;
  bool        from_string   = false;
  bool        from_stdin    = false;
  bool        ascii         = false;
  bool        bulk_read     = true;
  bool        bulk_write    = false;
  bool        full_uris     = false;
  bool        lax           = false;
  bool        quiet         = false;
  const char* add_prefix    = NULL;
  const char* chop_prefix   = NULL;
  const char* root_uri      = NULL;
  int         a             = 1;
  for (; a < argc && !from_string && argv[a][0] == '-'; ++a) {
    if (argv[a][1] == '\0') {
      from_stdin = true;
      break;
    }

    for (int o = 1; argv[a][o]; ++o) {
      const char opt = argv[a][o];

      if (opt == 'a') {
        ascii = true;
      } else if (opt == 'b') {
        bulk_write = true;
      } else if (opt == 'e') {
        bulk_read = false;
      } else if (opt == 'f') {
        full_uris = true;
      } else if (opt == 'h') {
        return print_usage(prog, false);
      } else if (opt == 'l') {
        lax = true;
      } else if (opt == 'q') {
        quiet = true;
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

        if (!(input_syntax = get_syntax(argv[a]))) {
          return print_usage(prog, true);
        }
        break;
      } else if (opt == 'o') {
        if (argv[a][o + 1] || ++a == argc) {
          return missing_arg(prog, 'o');
        }

        if (!(output_syntax = get_syntax(argv[a]))) {
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

  if (!input_syntax && !(input_syntax = guess_syntax(input))) {
    input_syntax = SERD_TRIG;
  }

  if (!output_syntax) {
    output_syntax =
      ((input_syntax == SERD_TURTLE || input_syntax == SERD_NTRIPLES)
         ? SERD_NTRIPLES
         : SERD_NQUADS);
  }

  const SerdWriterFlags writer_flags =
    choose_style(input_syntax, output_syntax, ascii, bulk_write, full_uris);

  SerdNode* base = NULL;
  if (a < argc) { // Base URI given on command line
    base = serd_new_uri(serd_string((const char*)argv[a]));
  } else if (!from_string && !from_stdin) { // Use input file URI
    base = serd_new_file_uri(serd_string(input), serd_empty_string());
  }

  FILE* const    out_fd = stdout;
  SerdEnv* const env =
    serd_env_new(base ? serd_node_string_view(base) : serd_empty_string());

  SerdWriter* writer = serd_writer_new(
    output_syntax, writer_flags, env, (SerdWriteFunc)fwrite, out_fd);

  SerdReader* const reader =
    serd_reader_new(input_syntax, serd_writer_sink(writer));

  serd_reader_set_strict(reader, !lax);
  if (quiet) {
    serd_reader_set_error_sink(reader, quiet_error_sink, NULL);
    serd_writer_set_error_sink(writer, quiet_error_sink, NULL);
  }

  if (root_uri) {
    SerdNode* const root = serd_new_uri(serd_string(root_uri));
    serd_writer_set_root_uri(writer, root);
    serd_node_free(root);
  }

  serd_writer_chop_blank_prefix(writer, chop_prefix);
  serd_reader_add_blank_prefix(reader, add_prefix);

  SerdStatus st = SERD_SUCCESS;
  if (from_string) {
    st = serd_reader_start_string(reader, input);
  } else if (from_stdin) {
    st = serd_reader_start_stream(reader,
                                  serd_file_read_byte,
                                  (SerdStreamErrorFunc)ferror,
                                  stdin,
                                  "(stdin)",
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
  serd_env_free(env);
  serd_node_free(base);

  if (fclose(stdout)) {
    perror("serdi: write error");
    st = SERD_ERR_UNKNOWN;
  }

  return (st > SERD_FAILURE) ? 1 : 0;
}
