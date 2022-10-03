// Copyright 2011-2021 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#include "console.h"

#include "serd/log.h"
#include "serd/node.h"
#include "serd/stream.h"
#include "serd/string.h"
#include "serd/syntax.h"
#include "serd/uri.h"
#include "serd/version.h"

#ifdef _WIN32
#  ifdef _MSC_VER
#    define WIN32_LEAN_AND_MEAN 1
#  endif
#  include <fcntl.h>
#  include <io.h>
#endif

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

bool
serd_option_iter_is_end(const OptionIter iter)
{
  return iter.a >= iter.argc || iter.argv[iter.a][0] != '-' ||
         !iter.argv[iter.a][iter.f];
}

SerdStatus
serd_option_iter_advance(OptionIter* const iter)
{
  if (!iter->argv[iter->a][++iter->f]) {
    ++iter->a;
    iter->f = 1;
  }

  return SERD_SUCCESS;
}

SerdStatus
serd_tool_setup(SerdTool* const   tool,
                const char* const program,
                SerdCommonOptions options)
{
  // Open the output first, since if that fails we have nothing to do
  const char* const out_path = options.out_filename;
  if (!((tool->out = serd_open_tool_output(out_path)).stream)) {
    fprintf(stderr,
            "%s: failed to open output file (%s)\n",
            program,
            strerror(errno));
    return SERD_UNKNOWN_ERROR;
  }

  // We have something to write to, so build the writing environment
  if (!(tool->world = serd_world_new(NULL)) ||
      !(tool->env = serd_create_env(
          tool->world, program, options.base_uri, options.out_filename)) ||
      !(tool->writer = serd_writer_new(
          tool->world,
          serd_choose_syntax(
            tool->world, options.output, options.out_filename, SERD_NQUADS),
          options.output.flags,
          tool->env,
          &tool->out,
          options.block_size))) {
    fprintf(stderr, "%s: failed to set up writing environment\n", program);
    return SERD_UNKNOWN_ERROR;
  }

  return SERD_SUCCESS;
}

SerdStatus
serd_tool_cleanup(SerdTool tool)
{
  SerdStatus st = SERD_SUCCESS;
  if (tool.out.stream) {
    // Close the output stream explicitly to check if there were any errors
    if ((st = serd_close_output(&tool.out))) {
      perror("write error");
    }
  }

  serd_writer_free(tool.writer);
  serd_env_free(tool.env);
  serd_world_free(tool.world);
  return st;
}

void
serd_set_stream_utf8_mode(FILE* const stream)
{
#ifdef _WIN32
  _setmode(_fileno(stream), _O_BINARY);
#else
  (void)stream;
#endif
}

SerdStatus
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

  return SERD_FAILURE;
}

SerdStatus
serd_get_argument(OptionIter* const iter, const char** const argument)
{
  const char flag = iter->argv[iter->a][iter->f++];

  if (iter->argv[iter->a][iter->f] || (iter->a + 1) == iter->argc) {
    fprintf(
      stderr, "%s: option requires an argument -- %c\n", iter->argv[0], flag);
    return SERD_BAD_ARG;
  }

  *argument = iter->argv[++iter->a];
  ++iter->a;
  iter->f = 1;
  return SERD_SUCCESS;
}

SerdStatus
serd_get_size_argument(OptionIter* const iter, size_t* const argument)
{
  SerdStatus  st     = SERD_SUCCESS;
  const char* string = NULL;
  if ((st = serd_get_argument(iter, &string))) {
    return st;
  }

  char*      endptr = NULL;
  const long size   = strtol(string, &endptr, 10);
  if (size <= 0 || size == LONG_MAX || *endptr != '\0') {
    return SERD_BAD_ARG;
  }

  *argument = (size_t)size;
  return SERD_SUCCESS;
}

SerdStatus
serd_set_input_option(const SerdStringView   name,
                      SerdSyntax* const      syntax,
                      SerdReaderFlags* const flags)
{
  typedef struct {
    const char*    name;
    SerdReaderFlag flag;
  } InputOption;

  static const InputOption input_options[] = {
    {"lax", SERD_READ_LAX},
    {"variables", SERD_READ_VARIABLES},
    {"relative", SERD_READ_RELATIVE},
    {"global", SERD_READ_GLOBAL},
    {"generated", SERD_READ_GENERATED},
    {NULL, SERD_READ_LAX},
  };

  const SerdSyntax named_syntax = serd_syntax_by_name(name.buf);
  if (!serd_strncasecmp(name.buf, "empty", name.len) ||
      named_syntax != SERD_SYNTAX_EMPTY) {
    *syntax = named_syntax;
    return SERD_SUCCESS;
  }

  for (const InputOption* o = input_options; o->name; ++o) {
    if (!serd_strncasecmp(o->name, name.buf, name.len)) {
      *flags |= o->flag;
      return SERD_SUCCESS;
    }
  }

  return SERD_BAD_ARG;
}

SerdStatus
serd_parse_input_argument(OptionIter* const        iter,
                          SerdSyntaxOptions* const options)
{
  SerdStatus  st       = SERD_SUCCESS;
  const char* argument = NULL;

  if (!(st = serd_get_argument(iter, &argument))) {
    if ((st = serd_set_input_option(
           serd_string(argument), &options->syntax, &options->flags))) {
      fprintf(stderr, "%s: unknown option \"%s\"\n", iter->argv[0], argument);
    } else if (!strcmp(argument, "empty") || options->syntax) {
      options->overridden = true;
    }
  }

  return st;
}

SerdStatus
serd_set_output_option(const SerdStringView   name,
                       SerdSyntax* const      syntax,
                       SerdWriterFlags* const flags)
{
  typedef struct {
    const char*    name;
    SerdWriterFlag flag;
  } OutputOption;

  static const OutputOption output_options[] = {
    {"ascii", SERD_WRITE_ASCII},
    {"expanded", SERD_WRITE_EXPANDED},
    {"verbatim", SERD_WRITE_VERBATIM},
    {"terse", SERD_WRITE_TERSE},
    {"lax", SERD_WRITE_LAX},
    {"rdf_type", SERD_WRITE_RDF_TYPE},
    {"contextual", SERD_WRITE_CONTEXTUAL},
    {NULL, SERD_WRITE_ASCII},
  };

  const SerdSyntax named_syntax = serd_syntax_by_name(name.buf);
  if (!serd_strncasecmp(name.buf, "empty", name.len) ||
      named_syntax != SERD_SYNTAX_EMPTY) {
    *syntax = named_syntax;
    return SERD_SUCCESS;
  }

  for (const OutputOption* o = output_options; o->name; ++o) {
    if (!serd_strncasecmp(o->name, name.buf, name.len)) {
      *flags |= o->flag;
      return SERD_SUCCESS;
    }
  }

  return SERD_BAD_ARG;
}

SerdStatus
serd_parse_output_argument(OptionIter* const        iter,
                           SerdSyntaxOptions* const options)
{
  SerdStatus  st       = SERD_SUCCESS;
  const char* argument = NULL;

  if (!(st = serd_get_argument(iter, &argument))) {
    if ((st = serd_set_output_option(
           serd_string(argument), &options->syntax, &options->flags))) {
      fprintf(stderr, "%s: unknown option \"%s\"\n", iter->argv[0], argument);
    } else if (!strcmp(argument, "empty") || options->syntax) {
      options->overridden = true;
    }
  }

  return st;
}

SerdStatus
serd_parse_common_option(OptionIter* const iter, SerdCommonOptions* const opts)
{
  const char opt = iter->argv[iter->a][iter->f];
  switch (opt) {
  case 'B':
    return serd_get_argument(iter, &opts->base_uri);

  case 'I':
    return serd_parse_input_argument(iter, &opts->input);

  case 'O':
    return serd_parse_output_argument(iter, &opts->output);

  case 'b':
    return serd_get_size_argument(iter, &opts->block_size);

  case 'k':
    return serd_get_size_argument(iter, &opts->stack_size);

  case 'o':
    return serd_get_argument(iter, &opts->out_filename);

  default:
    break;
  }

  return SERD_FAILURE;
}

SerdEnv*
serd_create_env(SerdWorld* const  world,
                const char* const program,
                const char* const base_string,
                const char* const out_filename)
{
  if (serd_uri_string_has_scheme(base_string)) {
    return serd_env_new(world, serd_string(base_string));
  }

  const bool is_rebase = !strcmp(base_string, "rebase");
  if (is_rebase) {
    if (!out_filename) {
      fprintf(stderr, "%s: rebase requires an output filename\n", program);
      return NULL;
    }

    SerdEnv* const env = serd_env_new(world, serd_empty_string());
    serd_env_set_base_path(env, serd_string(out_filename));
    return env;
  }

  SerdEnv* const       env  = serd_env_new(world, serd_empty_string());
  const SerdStringView base = serd_string(base_string);
  const SerdStatus     st   = serd_env_set_base_path(env, base);
  if (st) {
    fprintf(stderr, "%s: invalid base URI \"%s\"\n", program, base_string);
    serd_env_free(env);
    return NULL;
  }

  return env;
}

SerdSyntax
serd_choose_syntax(SerdWorld* const        world,
                   const SerdSyntaxOptions options,
                   const char* const       filename,
                   const SerdSyntax        fallback)
{
  if (options.overridden || options.syntax != SERD_SYNTAX_EMPTY) {
    return options.syntax;
  }

  if (!filename || !strcmp(filename, "-")) {
    return fallback;
  }

  const SerdSyntax guessed = serd_guess_syntax(filename);
  if (guessed != SERD_SYNTAX_EMPTY) {
    return guessed;
  }

  serd_logf(world,
            SERD_LOG_LEVEL_WARNING,
            "unable to determine syntax of \"%s\", trying TriG",
            filename);

  return SERD_TRIG;
}

/// Wrapper for getc that is compatible with SerdReadFunc but faster than fread
static size_t
serd_file_read_byte(void* buf, size_t size, size_t nmemb, void* stream)
{
  (void)size;
  (void)nmemb;

  const int c = getc((FILE*)stream);
  if (c == EOF) {
    *((uint8_t*)buf) = 0;
    return 0;
  }

  *((uint8_t*)buf) = (uint8_t)c;
  return 1;
}

SerdInputStream
serd_open_tool_input(const char* const filename)
{
  if (!strcmp(filename, "-")) {
    const SerdInputStream in = serd_open_input_stream(
      serd_file_read_byte, (SerdErrorFunc)ferror, NULL, stdin);

    serd_set_stream_utf8_mode(stdin);
    return in;
  }

  return serd_open_input_file(filename);
}

SerdOutputStream
serd_open_tool_output(const char* const filename)
{
  if (!filename || !strcmp(filename, "-")) {
    serd_set_stream_utf8_mode(stdout);
    return serd_open_output_stream((SerdWriteFunc)fwrite,
                                   (SerdErrorFunc)ferror,
                                   (SerdCloseFunc)fclose,
                                   stdout);
  }

  return serd_open_output_file(filename);
}

SerdStatus
serd_read_source(SerdWorld* const        world,
                 const SerdCommonOptions opts,
                 SerdEnv* const          env,
                 const SerdSyntax        syntax,
                 SerdInputStream* const  in,
                 const char* const       name,
                 const SerdSink* const   sink)
{
  SerdReader* const reader = serd_reader_new(
    world, syntax, opts.input.flags, env, sink, opts.stack_size);

  SerdNode* const name_node = serd_node_new(NULL, serd_a_string(name));

  SerdStatus st = serd_reader_start(reader, in, name_node, opts.block_size);
  serd_node_free(NULL, name_node);
  if (!st) {
    st = serd_reader_read_document(reader);
  }

  serd_reader_free(reader);
  return st;
}

SerdStatus
serd_read_inputs(SerdWorld* const        world,
                 const SerdCommonOptions opts,
                 SerdEnv* const          env,
                 const intptr_t          n_inputs,
                 char* const* const      inputs,
                 const SerdSink* const   sink)
{
  SerdStatus st = SERD_SUCCESS;

  for (intptr_t i = 0; !st && i < n_inputs; ++i) {
    // Use the filename as the base URI if possible if user didn't override it
    const char* const in_path = inputs[i];
    if (!opts.base_uri[0] && !!strcmp(in_path, "-")) {
      serd_env_set_base_path(env, serd_string(in_path));
    }

    // Open the input stream
    SerdInputStream in = serd_open_tool_input(in_path);
    if (!in.stream) {
      return SERD_BAD_ARG;
    }

    // Read the entire file
    st = serd_read_source(
      world,
      opts,
      env,
      serd_choose_syntax(world, opts.input, in_path, SERD_TRIG),
      &in,
      !strcmp(in_path, "-") ? "stdin" : in_path,
      sink);

    serd_close_input(&in);
  }

  return st;
}
