// Copyright 2011-2020 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#undef NDEBUG

#include "serd/memory.h"
#include "serd/node.h"
#include "serd/status.h"
#include "serd/string_view.h"
#include "serd/uri.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NS_XSD "http://www.w3.org/2001/XMLSchema#"
#define NS_RDF "http://www.w3.org/1999/02/22-rdf-syntax-ns#"

#if defined(__clang__)

#  define SERD_DISABLE_CONVERSION_WARNINGS                     \
    _Pragma("clang diagnostic push")                           \
    _Pragma("clang diagnostic ignored \"-Wconversion\"")       \
    _Pragma("clang diagnostic ignored \"-Wdouble-promotion\"") \
    _Pragma("clang diagnostic ignored \"-Wc11-extensions\"")

#  define SERD_RESTORE_WARNINGS _Pragma("clang diagnostic pop")

#elif defined(__GNUC__) && __GNUC__ >= 8

#  define SERD_DISABLE_CONVERSION_WARNINGS                   \
    _Pragma("GCC diagnostic push")                           \
    _Pragma("GCC diagnostic ignored \"-Wconversion\"")       \
    _Pragma("GCC diagnostic ignored \"-Wfloat-conversion\"") \
    _Pragma("GCC diagnostic ignored \"-Wdouble-promotion\"")

#  define SERD_RESTORE_WARNINGS _Pragma("GCC diagnostic pop")

#else

#  define SERD_DISABLE_CONVERSION_WARNINGS
#  define SERD_RESTORE_WARNINGS

#endif

static void
test_boolean(void)
{
  SerdNode* const true_node = serd_new_boolean(true);
  assert(!strcmp(serd_node_string(true_node), "true"));
  assert(serd_get_boolean(true_node));

  const SerdNode* const true_datatype = serd_node_datatype(true_node);
  assert(true_datatype);
  assert(!strcmp(serd_node_string(true_datatype), NS_XSD "boolean"));
  serd_node_free(true_node);

  SerdNode* const false_node = serd_new_boolean(false);
  assert(!strcmp(serd_node_string(false_node), "false"));
  assert(!serd_get_boolean(false_node));

  const SerdNode* const false_datatype = serd_node_datatype(false_node);
  assert(false_datatype);
  assert(!strcmp(serd_node_string(false_datatype), NS_XSD "boolean"));
  serd_node_free(false_node);
}

static void
check_get_boolean(const char* string,
                  const char* datatype_uri,
                  const bool  expected)
{
  SerdNode* const node = serd_new_literal(
    serd_string(string), SERD_HAS_DATATYPE, serd_string(datatype_uri));

  assert(node);
  assert(serd_get_boolean(node) == expected);

  serd_node_free(node);
}

static void
test_get_boolean(void)
{
  check_get_boolean("false", NS_XSD "boolean", false);
  check_get_boolean("true", NS_XSD "boolean", true);
  check_get_boolean("0", NS_XSD "boolean", false);
  check_get_boolean("1", NS_XSD "boolean", true);
  check_get_boolean("0", NS_XSD "integer", false);
  check_get_boolean("1", NS_XSD "integer", true);
  check_get_boolean("0.0", NS_XSD "double", false);
  check_get_boolean("1.0", NS_XSD "double", true);
  check_get_boolean("unknown", NS_XSD "string", false);
  check_get_boolean("!invalid", NS_XSD "long", false);
}

static void
test_decimal(void)
{
  const double test_values[] = {
    0.0, 9.0, 10.0, .01, 2.05, -16.00001, 5.000000005, 0.0000000001};

  static const char* const test_strings[] = {"0.0",
                                             "9.0",
                                             "10.0",
                                             "0.01",
                                             "2.05",
                                             "-16.00001",
                                             "5.000000005",
                                             "0.0000000001"};

  for (size_t i = 0; i < sizeof(test_values) / sizeof(double); ++i) {
    SerdNode*   node     = serd_new_decimal(test_values[i]);
    const char* node_str = serd_node_string(node);
    assert(!strcmp(node_str, test_strings[i]));

    const size_t len = strlen(node_str);
    assert(serd_node_length(node) == len);

    const SerdNode* const datatype = serd_node_datatype(node);
    assert(datatype);
    assert(!strcmp(serd_node_string(datatype), NS_XSD "decimal"));

    const double value = serd_get_double(node);
    assert(!memcmp(&value, &test_values[i], sizeof(value)));
    serd_node_free(node);
  }
}

static void
test_double(void)
{
  const double test_values[]  = {0.0, -0.0, 1.2, -2.3, 4567890};
  const char*  test_strings[] = {
     "0.0E0", "-0.0E0", "1.2E0", "-2.3E0", "4.56789E6"};

  for (size_t i = 0; i < sizeof(test_values) / sizeof(double); ++i) {
    SerdNode*   node     = serd_new_double(test_values[i]);
    const char* node_str = serd_node_string(node);
    assert(!strcmp(node_str, test_strings[i]));

    const size_t len = strlen(node_str);
    assert(serd_node_length(node) == len);

    const SerdNode* const datatype = serd_node_datatype(node);
    assert(datatype);
    assert(!strcmp(serd_node_string(datatype), NS_XSD "double"));

    const double value = serd_get_double(node);
    assert(!memcmp(&value, &test_values[i], sizeof(value)));
    serd_node_free(node);
  }
}

static void
check_get_double(const char*  string,
                 const char*  datatype_uri,
                 const double expected)
{
  SerdNode* const node = serd_new_literal(
    serd_string(string), SERD_HAS_DATATYPE, serd_string(datatype_uri));

  assert(node);

  const double value = serd_get_double(node);
  assert(!memcmp(&value, &expected, sizeof(value)));

  serd_node_free(node);
}

static void
test_get_double(void)
{
  check_get_double("1.2", NS_XSD "double", 1.2);
  check_get_double("-.5", NS_XSD "float", -0.5);
  check_get_double("-67", NS_XSD "long", -67.0);
  check_get_double("8.9", NS_XSD "decimal", 8.9);
  check_get_double("false", NS_XSD "boolean", 0.0);
  check_get_double("true", NS_XSD "boolean", 1.0);

  static const uint8_t blob[] = {1U, 2U, 3U, 4U};

  SERD_DISABLE_CONVERSION_WARNINGS

  SerdNode* const nan = serd_new_string(serd_string("unknown"));
  assert(isnan(serd_get_double(nan)));
  serd_node_free(nan);

  SerdNode* const invalid = serd_new_literal(
    serd_string("!invalid"), SERD_HAS_DATATYPE, serd_string(NS_XSD "long"));

  assert(isnan(serd_get_double(invalid)));
  serd_node_free(invalid);

  SerdNode* const base64 =
    serd_new_base64(blob, sizeof(blob), serd_empty_string());

  assert(isnan(serd_get_double(base64)));
  serd_node_free(base64);

  SERD_RESTORE_WARNINGS
}

static void
test_float(void)
{
  const float test_values[]  = {0.0f, -0.0f, 1.5f, -2.5f, 4567890.0f};
  const char* test_strings[] = {
    "0.0E0", "-0.0E0", "1.5E0", "-2.5E0", "4.56789E6"};

  for (size_t i = 0; i < sizeof(test_values) / sizeof(float); ++i) {
    SerdNode*   node     = serd_new_float(test_values[i]);
    const char* node_str = serd_node_string(node);
    assert(!strcmp(node_str, test_strings[i]));

    const size_t len = strlen(node_str);
    assert(serd_node_length(node) == len);

    const SerdNode* const datatype = serd_node_datatype(node);
    assert(datatype);
    assert(!strcmp(serd_node_string(datatype), NS_XSD "float"));

    const float value = serd_get_float(node);
    assert(!memcmp(&value, &test_values[i], sizeof(value)));
    serd_node_free(node);
  }
}

static void
check_get_float(const char* string,
                const char* datatype_uri,
                const float expected)
{
  SerdNode* const node = serd_new_literal(
    serd_string(string), SERD_HAS_DATATYPE, serd_string(datatype_uri));

  assert(node);

  const float value = serd_get_float(node);
  assert(!memcmp(&value, &expected, sizeof(value)));

  serd_node_free(node);
}

static void
test_get_float(void)
{
  check_get_float("1.2", NS_XSD "float", 1.2f);
  check_get_float("-.5", NS_XSD "float", -0.5f);
  check_get_float("-67", NS_XSD "long", -67.0f);
  check_get_float("1.5", NS_XSD "decimal", 1.5f);
  check_get_float("false", NS_XSD "boolean", 0.0f);
  check_get_float("true", NS_XSD "boolean", 1.0f);

  SERD_DISABLE_CONVERSION_WARNINGS

  SerdNode* const nan = serd_new_string(serd_string("unknown"));
  assert(isnan(serd_get_float(nan)));
  serd_node_free(nan);

  SerdNode* const invalid = serd_new_literal(
    serd_string("!invalid"), SERD_HAS_DATATYPE, serd_string(NS_XSD "long"));

  assert(isnan(serd_get_double(invalid)));

  SERD_RESTORE_WARNINGS

  serd_node_free(invalid);
}

static void
test_integer(void)
{
  assert(!serd_new_integer(42, serd_string("notauri")));

  const int64_t test_values[]  = {0, -0, -23, 23, -12340, 1000, -1000};
  const char*   test_strings[] = {
      "0", "0", "-23", "23", "-12340", "1000", "-1000"};

  for (size_t i = 0; i < sizeof(test_values) / sizeof(double); ++i) {
    SerdNode*   node = serd_new_integer(test_values[i], serd_empty_string());
    const char* node_str = serd_node_string(node);
    assert(!strcmp(node_str, test_strings[i]));
    const size_t len = strlen(node_str);
    assert(serd_node_length(node) == len);

    const SerdNode* const datatype = serd_node_datatype(node);
    assert(datatype);
    assert(!strcmp(serd_node_string(datatype), NS_XSD "integer"));

    assert(serd_get_integer(node) == test_values[i]);
    serd_node_free(node);
  }
}

static void
check_get_integer(const char*   string,
                  const char*   datatype_uri,
                  const int64_t expected)
{
  SerdNode* const node = serd_new_literal(
    serd_string(string), SERD_HAS_DATATYPE, serd_string(datatype_uri));

  assert(node);
  assert(serd_get_integer(node) == expected);

  serd_node_free(node);
}

static void
test_get_integer(void)
{
  check_get_integer("12", NS_XSD "long", 12);
  check_get_integer("-34", NS_XSD "long", -34);
  check_get_integer("56", NS_XSD "integer", 56);
  check_get_integer("false", NS_XSD "boolean", 0);
  check_get_integer("true", NS_XSD "boolean", 1);
  check_get_integer("78.0", NS_XSD "decimal", 78);
  check_get_integer("unknown", NS_XSD "string", 0);
  check_get_integer("!invalid", NS_XSD "long", 0);
}

static void
test_base64(void)
{
  assert(!serd_new_base64(&SERD_URI_NULL, 1, serd_string("notauri")));
  assert(!serd_new_base64(&SERD_URI_NULL, 0, serd_empty_string()));

  // Test valid base64 blobs with a range of sizes
  for (size_t size = 1; size < 256; ++size) {
    uint8_t* const data = (uint8_t*)malloc(size);
    for (size_t i = 0; i < size; ++i) {
      data[i] = (uint8_t)((size + i) % 256);
    }

    SerdNode*    blob     = serd_new_base64(data, size, serd_empty_string());
    const char*  blob_str = serd_node_string(blob);
    const size_t max_size = serd_get_base64_size(blob);
    uint8_t*     out      = (uint8_t*)calloc(1, max_size);

    const SerdWriteResult r = serd_get_base64(blob, max_size, out);
    assert(r.status == SERD_SUCCESS);
    assert(r.count == size);
    assert(r.count <= max_size);
    assert(serd_node_length(blob) == strlen(blob_str));

    for (size_t i = 0; i < size; ++i) {
      assert(out[i] == data[i]);
    }

    const SerdNode* const datatype = serd_node_datatype(blob);
    assert(datatype);
    assert(!strcmp(serd_node_string(datatype), NS_XSD "base64Binary"));

    serd_node_free(blob);
    serd_free(out);
    free(data);
  }
}

static void
check_get_base64(const char* string,
                 const char* datatype_uri,
                 const char* expected)
{
  SerdNode* const node = serd_new_literal(
    serd_string(string), SERD_HAS_DATATYPE, serd_string(datatype_uri));

  assert(node);

  const size_t max_size = serd_get_base64_size(node);
  char* const  decoded  = (char*)calloc(1, max_size + 1);

  const SerdWriteResult r = serd_get_base64(node, max_size, decoded);
  assert(!r.status);
  assert(r.count <= max_size);

  assert(!strcmp(decoded, expected));
  assert(strlen(decoded) <= max_size);

  free(decoded);
  serd_node_free(node);
}

static void
test_get_base64(void)
{
  check_get_base64("Zm9vYmFy", NS_XSD "base64Binary", "foobar");
  check_get_base64("Zm9vYg==", NS_XSD "base64Binary", "foob");
  check_get_base64(" \f\n\r\t\vZm9v \f\n\r\t\v", NS_XSD "base64Binary", "foo");

  SerdNode* const node = serd_new_literal(
    serd_string("Zm9v"), SERD_HAS_DATATYPE, serd_string(NS_XSD "base64Binary"));

  char                  small[2] = {0};
  const SerdWriteResult r        = serd_get_base64(node, sizeof(small), small);

  assert(r.status == SERD_ERR_OVERFLOW);
  serd_node_free(node);
}

static void
test_node_equals(void)
{
  static const uint8_t replacement_char_str[] = {0xEF, 0xBF, 0xBD, 0};

  static const SerdStringView replacement_char = {
    (const char*)replacement_char_str, 3};

  SerdNode* lhs = serd_new_string(replacement_char);
  SerdNode* rhs = serd_new_string(serd_string("123"));

  assert(serd_node_equals(lhs, lhs));
  assert(!serd_node_equals(lhs, rhs));

  assert(!serd_node_copy(NULL));

  serd_node_free(lhs);
  serd_node_free(rhs);
}

static void
test_node_from_syntax(void)
{
  SerdNode* const hello = serd_new_string(serd_string("hello\""));
  assert(serd_node_length(hello) == 6);
  assert(!serd_node_flags(hello));
  assert(!strncmp(serd_node_string(hello), "hello\"", 6));
  serd_node_free(hello);
}

static void
test_node_from_substring(void)
{
  SerdNode* const a_b = serd_new_string(serd_substring("a\"bc", 3));
  assert(serd_node_length(a_b) == 3);
  assert(!serd_node_flags(a_b));
  assert(strlen(serd_node_string(a_b)) == 3);
  assert(!strncmp(serd_node_string(a_b), "a\"b", 3));
  serd_node_free(a_b);
}

static void
check_copy_equals(const SerdNode* const node)
{
  SerdNode* const copy = serd_node_copy(node);

  assert(serd_node_equals(node, copy));

  serd_node_free(copy);
}

static void
test_new(void)
{
  assert(!serd_node_new(SERD_URI,
                        serd_string("http://example.org/"),
                        SERD_HAS_DATATYPE,
                        serd_string("http://example.org/uris/cant/have/me")));

  assert(!serd_node_new(SERD_URI,
                        serd_string("http://example.org/"),
                        SERD_HAS_LANGUAGE,
                        serd_string("in-valid")));

  assert(!serd_node_new(SERD_BLANK,
                        serd_string("b0"),
                        SERD_HAS_DATATYPE,
                        serd_string("http://example.org/uris/cant/have/me")));

  assert(!serd_node_new(
    SERD_BLANK, serd_string("b0"), SERD_HAS_LANGUAGE, serd_string("in-valid")));
}

static void
test_literal(void)
{
  const SerdStringView hello_str = serd_string("hello");
  const SerdStringView empty_str = serd_empty_string();

  assert(!serd_new_literal(
    hello_str, SERD_HAS_DATATYPE | SERD_HAS_LANGUAGE, serd_string("whatever")));

  assert(!serd_new_literal(hello_str, SERD_HAS_DATATYPE, empty_str));
  assert(!serd_new_literal(hello_str, SERD_HAS_LANGUAGE, empty_str));

  assert(!serd_new_literal(hello_str, SERD_HAS_DATATYPE, serd_string("Type")));
  assert(!serd_new_literal(hello_str, SERD_HAS_DATATYPE, serd_string("de")));

  assert(!serd_new_literal(hello_str, SERD_HAS_LANGUAGE, serd_string("3n")));
  assert(!serd_new_literal(hello_str, SERD_HAS_LANGUAGE, serd_string("d3")));
  assert(!serd_new_literal(hello_str, SERD_HAS_LANGUAGE, serd_string("d3")));
  assert(!serd_new_literal(hello_str, SERD_HAS_LANGUAGE, serd_string("en-!")));

  SerdNode* hello2 = serd_new_string(serd_string("hello\""));

  assert(serd_node_length(hello2) == 6 &&
         !strcmp(serd_node_string(hello2), "hello\""));

  check_copy_equals(hello2);

  assert(!serd_new_literal(
    serd_string("plain"), SERD_HAS_DATATYPE, serd_string(NS_RDF "langString")));

  serd_node_free(hello2);

  const char* lang_lit_str = "\"Hello\"@en-ca";
  SerdNode*   sliced_lang_lit =
    serd_new_literal(serd_substring(lang_lit_str + 1, 5),
                     SERD_HAS_LANGUAGE,
                     serd_substring(lang_lit_str + 8, 5));

  assert(!strcmp(serd_node_string(sliced_lang_lit), "Hello"));

  const SerdNode* const lang = serd_node_language(sliced_lang_lit);
  assert(lang);
  assert(!strcmp(serd_node_string(lang), "en-ca"));
  check_copy_equals(sliced_lang_lit);
  serd_node_free(sliced_lang_lit);

  const char* type_lit_str = "\"Hallo\"^^<http://example.org/Greeting>";
  SerdNode*   sliced_type_lit =
    serd_new_literal(serd_substring(type_lit_str + 1, 5),
                     SERD_HAS_DATATYPE,
                     serd_substring(type_lit_str + 10, 27));

  assert(!strcmp(serd_node_string(sliced_type_lit), "Hallo"));

  const SerdNode* const datatype = serd_node_datatype(sliced_type_lit);
  assert(datatype);
  assert(!strcmp(serd_node_string(datatype), "http://example.org/Greeting"));
  serd_node_free(sliced_type_lit);
}

static void
test_blank(void)
{
  SerdNode* blank = serd_new_token(SERD_BLANK, serd_string("b0"));
  assert(serd_node_length(blank) == 2);
  assert(serd_node_flags(blank) == 0);
  assert(!strcmp(serd_node_string(blank), "b0"));
  serd_node_free(blank);
}

static void
test_compare(void)
{
  SerdNode* xsd_short = serd_new_token(
    SERD_URI, serd_string("http://www.w3.org/2001/XMLSchema#short"));

  SerdNode* angst = serd_new_string(serd_string("angst"));

  SerdNode* angst_de = serd_new_literal(
    serd_string("angst"), SERD_HAS_LANGUAGE, serd_string("de"));

  SerdNode* angst_en = serd_new_literal(
    serd_string("angst"), SERD_HAS_LANGUAGE, serd_string("en"));

  SerdNode* hallo = serd_new_literal(
    serd_string("Hallo"), SERD_HAS_LANGUAGE, serd_string("de"));

  SerdNode* hello         = serd_new_string(serd_string("Hello"));
  SerdNode* universe      = serd_new_string(serd_string("Universe"));
  SerdNode* integer       = serd_new_integer(4, serd_empty_string());
  SerdNode* short_integer = serd_new_integer(4, serd_string(NS_XSD "short"));
  SerdNode* blank         = serd_new_token(SERD_BLANK, serd_string("b1"));

  SerdNode* uri = serd_new_uri(serd_string("http://example.org/"));

  SerdNode* aardvark =
    serd_new_literal(serd_string("alex"),
                     SERD_HAS_DATATYPE,
                     serd_string("http://example.org/Aardvark"));

  SerdNode* badger = serd_new_literal(serd_string("bobby"),
                                      SERD_HAS_DATATYPE,
                                      serd_string("http://example.org/Badger"));

  // Types are ordered according to their SerdNodeType (more or less arbitrary)
  assert(serd_node_compare(hello, uri) < 0);
  assert(serd_node_compare(uri, blank) < 0);

  // If the types are the same, then strings are compared
  assert(serd_node_compare(hello, universe) < 0);

  // If literal strings are the same, languages or datatypes are compared
  assert(serd_node_compare(angst, angst_de) < 0);
  assert(serd_node_compare(angst_de, angst_en) < 0);
  assert(serd_node_compare(integer, short_integer) < 0);
  assert(serd_node_compare(aardvark, badger) < 0);

  serd_node_free(uri);
  serd_node_free(blank);
  serd_node_free(short_integer);
  serd_node_free(integer);
  serd_node_free(badger);
  serd_node_free(aardvark);
  serd_node_free(universe);
  serd_node_free(hello);
  serd_node_free(hallo);
  serd_node_free(angst_en);
  serd_node_free(angst_de);
  serd_node_free(angst);
  serd_node_free(xsd_short);
}

int
main(void)
{
  test_boolean();
  test_get_boolean();
  test_decimal();
  test_double();
  test_get_double();
  test_float();
  test_get_float();
  test_integer();
  test_get_integer();
  test_base64();
  test_get_base64();
  test_node_equals();
  test_node_from_syntax();
  test_node_from_substring();
  test_new();
  test_literal();
  test_blank();
  test_compare();

  printf("Success\n");
  return 0;
}
