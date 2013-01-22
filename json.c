/*
 * json-c-0.9
 *
 * Copyright (c) 2004, 2005 Metaparadigm Pte. Ltd.
 * Michael Clark <michael@metaparadigm.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See COPYING for details.

 */

#define MY_DEBUGGING	0
#include "json.h"

#include <stdarg.h>
#include <limits.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>

/* Define to 1 if you have the `strndup' function. */
#define HAVE_STRNDUP 1

/* Define to 1 if you have the `vasprintf' function. */
#define HAVE_VASPRINTF 1

/* Define to 1 if you have the `vsnprintf' function. */
#define HAVE_VSNPRINTF 1

/*
 * $Id: json_object_private.h,v 1.4 2006/01/26 02:16:28 mclark Exp $
 */

typedef void (json_object_delete_fn)(struct json_object *o);
typedef int (json_object_to_string_fn)(struct json_object *o,
					    struct printbuf *pb);

struct json_object
{
  enum json_type o_type;
  json_object_delete_fn *_delete;
  json_object_to_string_fn *_to_string;
  int _ref_count;
  struct printbuf *_pb;
  union data {
    boolean c_boolean;
    double c_double;
    int c_int;
    struct lh_table *c_object;
    struct array_list *c_array;
    char *c_string;
  } o;
};

/* CAW: added for ANSI C iteration correctness */
struct json_object_iter
{
	char *key;
	struct json_object *val;
	struct lh_entry *entry;
};

/*
 * $Id: arraylist.c,v 1.4 2006/01/26 02:16:28 mclark Exp $
 */

struct array_list* array_list_new(array_list_free_fn *free_fn)
{
	struct array_list *arr;

	arr = (struct array_list*)calloc(1, sizeof(struct array_list));
	if(!arr) return NULL;
	arr->size = ARRAY_LIST_DEFAULT_SIZE;
	arr->length = 0;
	arr->free_fn = free_fn;
	if(!(arr->array = (void**)calloc(sizeof(void*), arr->size))) {
		free(arr);
		return NULL;
	}
	return arr;
}

void array_list_free(struct array_list *arr)
{
	int i;
	for(i = 0; i < arr->length; i++)
		if(arr->array[i]) arr->free_fn(arr->array[i]);
		free(arr->array);
	free(arr);
}

void* array_list_get_idx(struct array_list *arr, int i)
{
	if(i >= arr->length) return NULL;
	return arr->array[i];
}

static int array_list_expand_internal(struct array_list *arr, int max)
{
  void *t;
  int new_size;

  if(max < arr->size) return 0;
  new_size = json_max(arr->size << 1, max);
  if(!(t = realloc(arr->array, new_size*sizeof(void*)))) return -1;
  arr->array = (void**)t;
  (void)memset(arr->array + arr->size, 0, (new_size-arr->size)*sizeof(void*));
  arr->size = new_size;
  return 0;
}

int array_list_put_idx(struct array_list *arr, int idx, void *data)
{
  if(array_list_expand_internal(arr, idx)) return -1;
  if(arr->array[idx]) arr->free_fn(arr->array[idx]);
  arr->array[idx] = data;
  if(arr->length <= idx) arr->length = idx + 1;
  return 0;
}

int array_list_add(struct array_list *arr, void *data)
{
	return array_list_put_idx(arr, arr->length, data);
}

int array_list_length(struct array_list *arr)
{
	return arr->length;
}

/*
 * $Id: json_object.c,v 1.17 2006/07/25 03:24:50 mclark Exp $
 */
#if !HAVE_STRNDUP
/* CAW: compliant version of strndup() */
char* strndup(const char* str, size_t n)
{
	if(str) {
		size_t len = strlen(str);
		size_t nn = json_min(len,n);
		char* s = (char*)malloc(sizeof(char) * (nn + 1));
		if(s) {
			memcpy(s, str, nn);
			s[nn] = '\0';
		}
		return s;
	}
	return NULL;
}
#endif

const char *json_number_chars = "0123456789.+-eE";
const char *json_hex_chars = "0123456789abcdef";

static void json_object_generic_delete(struct json_object* jso);
static struct json_object* json_object_new(enum json_type o_type);

/* ref count debugging */
#define REFCOUNT_DEBUG 1
#ifdef REFCOUNT_DEBUG
static const char* json_type_name[] = {
	"null",
	"boolean",
	"double",
	"int",
	"object",
	"array",
	"string",
};

static struct lh_table *json_object_table = NULL;

static void json_object_init(void) __attribute__ ((constructor));
static void json_object_init(void) {
	MY_DEBUG("json_object_init: creating object table\n");
	json_object_table = lh_kptr_table_new(128, "json_object_table", NULL);
}

static void json_object_fini(void) __attribute__ ((destructor));
static void json_object_fini(void) {
	struct lh_entry *ent;
	if (json_object_table->count) {
		MY_DEBUG("json_object_fini: %d referenced objects at exit\n",
			json_object_table->count);
		lh_foreach(json_object_table, ent) {
			struct json_object* obj = (struct json_object*)ent->v;
			MY_DEBUG("\t%s:%p\n", json_type_name[obj->o_type], obj);
		}
	}
	MY_DEBUG("json_object_fini: freeing object table\n");
	lh_table_free(json_object_table);
}
#endif /* REFCOUNT_DEBUG */

/* string escaping */

static int json_escape_str(struct printbuf *pb, char *str)
{
  int pos = 0, start_offset = 0;
  unsigned char c;
	if (!str)	// wkliang:20110522
		return -1;
  do {
    c = str[pos];
    switch(c) {
    case '\0':
      break;
    case '\b':
    case '\n':
    case '\r':
    case '\t':
    case '"':
    case '\\':
    case '/':
      if(pos - start_offset > 0)
	printbuf_memappend(pb, str + start_offset, pos - start_offset);
      if(c == '\b') printbuf_memappend(pb, "\\b", 2);
      else if(c == '\n') printbuf_memappend(pb, "\\n", 2);
      else if(c == '\r') printbuf_memappend(pb, "\\r", 2);
      else if(c == '\t') printbuf_memappend(pb, "\\t", 2);
      else if(c == '"') printbuf_memappend(pb, "\\\"", 2);
      else if(c == '\\') printbuf_memappend(pb, "\\\\", 2);
      else if(c == '/') printbuf_memappend(pb, "\\/", 2);
      start_offset = ++pos;
      break;
    default:
      if(c < ' ') {
	if(pos - start_offset > 0)
	  printbuf_memappend(pb, str + start_offset, pos - start_offset);
	sprintbuf(pb, "\\u00%c%c",
		  json_hex_chars[c >> 4],
		  json_hex_chars[c & 0xf]);
	start_offset = ++pos;
      } else pos++;
    }
  } while(c);
  if(pos - start_offset > 0)
    printbuf_memappend(pb, str + start_offset, pos - start_offset);
  return 0;
}

/* reference counting */

json_object* jsobj_IncrRef(json_object *jso)
{
	if (jso) {
		jso->_ref_count++;
	}
	return jso;
}

void jsobj_DecrRef(json_object *jso)
{
	if (jso) {
		jso->_ref_count--;
		if(!jso->_ref_count) jso->_delete(jso);
	}
}

/* generic object construction and destruction parts */

static void json_object_generic_delete(struct json_object* jso)
{
#ifdef REFCOUNT_DEBUG
	MY_DEBUG("json_object_delete_%s: %p %p\n",
		json_type_name[jso->o_type], json_object_table, jso);
	lh_table_delete(json_object_table, jso);
#endif /* REFCOUNT_DEBUG */
	printbuf_free(jso->_pb);
	free(jso);
}

static struct json_object* json_object_new(enum json_type o_type)
{
	struct json_object *jso = (struct json_object*)calloc(sizeof(struct json_object), 1);

	if(!jso) return NULL;
	jso->o_type = o_type;
	jso->_ref_count = 1;
	jso->_delete = &json_object_generic_delete;
#ifdef REFCOUNT_DEBUG
	lh_table_insert(json_object_table, jso, jso);
	MY_DEBUG("json_object_new_%s: %p %p\n",
		json_type_name[jso->o_type], json_object_table, jso);
#endif /* REFCOUNT_DEBUG */
	return jso;
}

/* type checking functions */

int json_object_is_type(struct json_object *jso, enum json_type type)
{
	return (jso->o_type == type);
}

enum json_type json_object_get_type(struct json_object *jso)
{
	return jso->o_type;
}

/* json_object_to_string */

const char* json_object_to_string(struct json_object *jso)
{
	if (!jso) return "null";
	if (!jso->_pb) {
		if (!(jso->_pb = printbuf_new())) return NULL;
	} else {
		printbuf_reset(jso->_pb);
	}
	if (jso->_to_string(jso, jso->_pb) < 0) return NULL;
	return jso->_pb->buf;
}

/* json_object_object */

static int json_object_object_to_string(struct json_object* jso,
					     struct printbuf *pb)
{
	int i=0;
	struct json_object_iter iter;
	sprintbuf(pb, "{");

	/* CAW: scope operator to make ANSI correctness */
	/* CAW: switched to json_object_object_foreachC which uses an iterator struct */
	json_object_object_foreachC(jso, iter) {
		if(i) sprintbuf(pb, ",");
		sprintbuf(pb, " \"");
		json_escape_str(pb, iter.key);
		sprintbuf(pb, "\":");
		if (iter.val == NULL)
			sprintbuf(pb, "null");
		else
			iter.val->_to_string(iter.val, pb);
		i++;
	}
	return sprintbuf(pb, "}");
}

static void json_object_lh_entry_free(struct lh_entry *ent)
{
	free(ent->k);
	jsobj_DecrRef((struct json_object*)ent->v);
}

static void json_object_object_delete(struct json_object* jso)
{
	lh_table_free(jso->o.c_object);
	json_object_generic_delete(jso);
}

struct json_object* json_object_new_object(void)
{
	struct json_object *jso = json_object_new(json_type_object);

	if(!jso) return NULL;
	jso->_delete = &json_object_object_delete;
	jso->_to_string = &json_object_object_to_string;
	jso->o.c_object = lh_kchar_table_new(JSON_OBJECT_DEF_HASH_ENTRIES,
					NULL, &json_object_lh_entry_free);
	return jso;
}

struct lh_table* json_object_get_object(struct json_object *jso)
{
	if(!jso) return NULL;
	switch(jso->o_type) {
	case json_type_object:
		return jso->o.c_object;
	default:
		return NULL;
  }
}

void json_object_object_add(struct json_object* jso, const char *key,
			    struct json_object *val)
{
	lh_table_delete(jso->o.c_object, key);
	lh_table_insert(jso->o.c_object, strdup(key), val);
}

struct json_object* json_object_object_get(struct json_object* jso, const char *key)
{
	return (struct json_object*) lh_table_lookup(jso->o.c_object, key);
}

void json_object_object_del(struct json_object* jso, const char *key)
{
	lh_table_delete(jso->o.c_object, key);
}


/* json_object_boolean */

static int json_object_boolean_to_string(struct json_object* jso,
					      struct printbuf *pb)
{
	if(jso->o.c_boolean)
		return sprintbuf(pb, "true");
	else
		return sprintbuf(pb, "false");
}

struct json_object* json_object_new_boolean(boolean b)
{
	struct json_object *jso = json_object_new(json_type_boolean);
	if(!jso) return NULL;
	jso->_to_string = &json_object_boolean_to_string;
	jso->o.c_boolean = b;
	return jso;
}

boolean json_object_get_boolean(struct json_object *jso)
{
	if(!jso) return FALSE;
	switch(jso->o_type) {
	case json_type_boolean:
		return jso->o.c_boolean;
	case json_type_int:
		return (jso->o.c_int != 0);
	case json_type_double:
		return (jso->o.c_double != 0);
	case json_type_string:
		return (strlen(jso->o.c_string) != 0);
	default:
		return FALSE;
	}
}

/* json_object_int */

static int json_object_int_to_string(struct json_object* jso, struct printbuf *pb)
{
	return sprintbuf(pb, "%d", jso->o.c_int);
}

struct json_object* json_object_new_int(int i)
{
	struct json_object *jso = json_object_new(json_type_int);
	if(!jso) return NULL;
	jso->_to_string = &json_object_int_to_string;
	jso->o.c_int = i;
	return jso;
}

int json_object_get_int(struct json_object *jso)
{
	int cint;

	if(!jso) return 0;
	switch(jso->o_type) {
		case json_type_int:	return jso->o.c_int;
		case json_type_double:	return (int)jso->o.c_double;
		case json_type_boolean:	return jso->o.c_boolean;
		case json_type_string:
			if(sscanf(jso->o.c_string, "%d", &cint) == 1)
				return cint;
		default:
			return 0;
	}
}

/* json_object_double */

static int json_object_double_to_string(struct json_object* jso,
					     struct printbuf *pb)
{
	return sprintbuf(pb, "%lf", jso->o.c_double);
}

struct json_object* json_object_new_double(double d)
{
  struct json_object *jso = json_object_new(json_type_double);
  if(!jso) return NULL;
  jso->_to_string = &json_object_double_to_string;
  jso->o.c_double = d;
  return jso;
}

double json_object_get_double(struct json_object *jso)
{
  double cdouble;

  if(!jso) return 0.0;
  switch(jso->o_type) {
  case json_type_double:
    return jso->o.c_double;
  case json_type_int:
    return jso->o.c_int;
  case json_type_boolean:
    return jso->o.c_boolean;
  case json_type_string:
    if(sscanf(jso->o.c_string, "%lf", &cdouble) == 1) return cdouble;
  default:
    return 0.0;
  }
}

/* json_object_string */

static int json_object_string_to_string(struct json_object* jso, struct printbuf *pb)
{
	sprintbuf(pb, "\"");
	// if (jso->o.c_string)	// wkliang:20110522
		json_escape_str(pb, jso->o.c_string);
	sprintbuf(pb, "\"");
	return 0;
}

static void json_object_string_delete(struct json_object* jso)
{
	free(jso->o.c_string);
	json_object_generic_delete(jso);
}

struct json_object* json_object_new_string(const char *s)
{
	struct json_object *jso = json_object_new(json_type_string);
	if(!jso) return NULL;
	jso->_delete = &json_object_string_delete;
	jso->_to_string = &json_object_string_to_string;
	if (s)	// wkliang:20110522
		jso->o.c_string = strdup(s);
	return jso;
}

struct json_object* json_object_new_string_len(const char *s, int len)
{
	struct json_object *jso = json_object_new(json_type_string);
	if(!jso) return NULL;
	jso->_delete = &json_object_string_delete;
	jso->_to_string = &json_object_string_to_string;
	jso->o.c_string = strndup(s, len);
	return jso;
}

const char* json_object_get_string(struct json_object *jso)
{
	if(!jso) return NULL;
	switch(jso->o_type) {
		case json_type_string:
			return jso->o.c_string;
		default:
			return json_object_to_string(jso);
	}
}

/* json_object_array */

static int json_object_array_to_string(struct json_object* jso,
					    struct printbuf *pb)
{
	int i;
	sprintbuf(pb, "[");
	for(i=0; i < json_object_array_length(jso); i++) {
		struct json_object *val;
		if(i) { sprintbuf(pb, ", "); }
		else { sprintbuf(pb, " "); }

		val = json_object_array_get_idx(jso, i);
		if(val == NULL) { sprintbuf(pb, "null"); }
		else { val->_to_string(val, pb); }
	}
	return sprintbuf(pb, " ]");
}

static void json_object_array_entry_free(void *data)
{
	jsobj_DecrRef((struct json_object*)data);
}

static void json_object_array_delete(struct json_object* jso)
{
	array_list_free(jso->o.c_array);
	json_object_generic_delete(jso);
}

struct json_object* json_object_new_array(void)
{
	struct json_object *jso = json_object_new(json_type_array);
	if(!jso) return NULL;
	jso->_delete = &json_object_array_delete;
	jso->_to_string = &json_object_array_to_string;
	jso->o.c_array = array_list_new(&json_object_array_entry_free);
	return jso;
}

struct array_list* json_object_get_array(struct json_object *jso)
{
  if(!jso) return NULL;
  switch(jso->o_type) {
  case json_type_array:
    return jso->o.c_array;
  default:
    return NULL;
  }
}

int json_object_array_length(struct json_object *jso)
{
	return array_list_length(jso->o.c_array);
}

int json_object_array_add(struct json_object *jso,struct json_object *val)
{
	return array_list_add(jso->o.c_array, val);
}

int json_object_array_put_idx(struct json_object *jso, int idx,
			      struct json_object *val)
{
	return array_list_put_idx(jso->o.c_array, idx, val);
}

struct json_object* json_object_array_get_idx(struct json_object *jso,
					      int idx)
{
	return (struct json_object*)array_list_get_idx(jso->o.c_array, idx);
}

/*
 * wkliang:20110522
 */

void jsobj_dump( json_object* obj, const char* name, int tab )
{
	int i;
	char buf[64];

	if( obj ) {
	switch( json_object_get_type(obj) ) {
		case json_type_array :
			MY_DEBUG( "%*s%s:\n", tab, " ", name );
			for( i = 0; i < json_object_array_length(obj); i++ ) {
				sprintf( buf, "%s[%d]", name, i );
				jsobj_dump( json_object_array_get_idx(obj, i), buf, tab + 4 );
			
			}
			return;
		case json_type_object :
			MY_DEBUG( "%*s%s:\n", tab, " ", name );
			{
				json_object_object_foreach(obj, key, val) {
					jsobj_dump( val, key, tab + 4 );
  				}
			}
			return;
		default :
			break;
	}
	}
	MY_DEBUG( "%*s%s: %s.\n", tab, " ", name, obj?json_object_to_string(obj):"nil");
	return;
}

const char* jsobj_get_string(json_object *jso, const char* key, const char* val)
{
	// check jso is an json_object_object
	json_object* vjso = (json_object*)lh_table_lookup(jso->o.c_object, key);
	if (vjso == NULL)
		return val;
	return json_object_get_string(vjso);
}

void jsobj_url_decode(json_object* jso, const char* cp)
{
	// check jso is an json_object_object
	const char *ep, *vs, *ve;
	char *key, *val;
	size_t klen, vlen;
	struct json_object *vjso;

	/* buf is "key1=val1&key2=val2...". Find key first */
	while (cp != NULL && *cp != '0') {
		if (!isalnum(*cp)) {
			cp++;	// skip leading non alpha numeric char
			continue;
		}
		if ((ep = strchr(cp, '=')) == NULL)
			break;	// not found
		if ((klen = ep - cp) == 0)
			break;	// empty key
		vs = ep + 1;	// value start
		for (ve = vs; *ve && *ve != '&'; ve++)
			;
		vlen = ve - vs;

		key = calloc(1, klen+1);
		val = calloc(1, vlen+1);
 		vjso = json_object_new(json_type_string);
		if (!key || !val || !vjso) {
			MY_DEBUG("%s: malloc error!\n", __func__);
			break;
		}
		// struct json_object* json_object_new_string(const char *s)
		vjso->_delete = &json_object_string_delete;
		vjso->_to_string = &json_object_string_to_string;
		vjso->o.c_string = val;

		memcpy(key, cp, klen);
		key[klen] = '\0';

		// int _shttpd_url_decode(const char *src, int src_len, char *dst, int dst_len)
		{
			#define	HEXTOI(x)  (isdigit(x) ? x - '0' : x - 'W')
			const char *src = vs;
			char *dst = val;
			int src_len = vlen, dst_len = vlen+1, i, j, a, b;

			for (i = j = 0; i < src_len && j < dst_len - 1; i++, j++)
			switch (src[i]) {
			case '+': // wkliang:20110604 missing +
				dst[j] = ' ';
				break;
			case '%':
				if (isxdigit(((unsigned char *) src)[i + 1]) &&
				    isxdigit(((unsigned char *) src)[i + 2])) {
					a = tolower(((unsigned char *)src)[i + 1]);
					b = tolower(((unsigned char *)src)[i + 2]);
					dst[j] = (HEXTOI(a) << 4) | HEXTOI(b);
					i += 2;
				} else {
					dst[j] = '%';
				}
				break;
			default:
				dst[j] = src[i];
				break;
			}
			dst[j] = '\0';	/* Null-terminate the destination */
		}
		MY_DEBUG("key=%s, val=%s.\n", key, val);

		// json_object_object_add()
		lh_table_delete(jso->o.c_object, key);
		lh_table_insert(jso->o.c_object, key, vjso);

		cp = ve;
	}
	return ;
}

/*
 * $Id: json_tokener.c,v 1.20 2006/07/25 03:24:50 mclark Exp $
 */

static const char* json_null_str = "null";
static const char* json_true_str = "true";
static const char* json_false_str = "false";

const char* json_tokener_errors[] = {
	"success",
	"continue",
	"nesting to deep",
	"unexpected end of data",
	"unexpected character",
	"null expected",
	"boolean expected",
	"number expected",
	"array value separator ',' expected",
	"quoted object property name expected",
	"object property name separator ':' expected",
	"object value separator ',' expected",
	"invalid string sequence",
	"expected comment",
};

struct json_tokener* json_tokener_new(void)
{
  struct json_tokener *tok;

  tok = (struct json_tokener*)calloc(1, sizeof(struct json_tokener));
  if (!tok) return NULL;
  tok->pb = printbuf_new();
  json_tokener_reset(tok);
  return tok;
}

void json_tokener_free(struct json_tokener *tok)
{
  json_tokener_reset(tok);
  if(tok) printbuf_free(tok->pb);
  free(tok);
}

static void json_tokener_reset_level(struct json_tokener *tok, int depth)
{
  tok->stack[depth].state = json_tokener_state_eatws;
  tok->stack[depth].saved_state = json_tokener_state_start;
  jsobj_DecrRef(tok->stack[depth].current);
  tok->stack[depth].current = NULL;
  free(tok->stack[depth].obj_field_name);
  tok->stack[depth].obj_field_name = NULL;
}

void json_tokener_reset(struct json_tokener *tok)
{
  int i;
  if (!tok)
    return;

  for(i = tok->depth; i >= 0; i--)
    json_tokener_reset_level(tok, i);
  tok->depth = 0;
  tok->err = json_tokener_success;
}

struct json_object* json_tokener_parse(const char *str)
{
  struct json_tokener* tok;
  struct json_object* obj;

  tok = json_tokener_new();
  obj = json_tokener_parse_ex(tok, str, -1);
  if(tok->err != json_tokener_success)
    obj = (struct json_object*)error_ptr(-tok->err);
  json_tokener_free(tok);
  return obj;
}

#define state  tok->stack[tok->depth].state
#define saved_state  tok->stack[tok->depth].saved_state
#define current tok->stack[tok->depth].current
#define obj_field_name tok->stack[tok->depth].obj_field_name

/* Optimization:
 * json_tokener_parse_ex() consumed a lot of CPU in its main loop,
 * iterating character-by character.  A large performance boost is
 * achieved by using tighter loops to locally handle units such as
 * comments and strings.  Loops that handle an entire token within 
 * their scope also gather entire strings and pass them to 
 * printbuf_memappend() in a single call, rather than calling
 * printbuf_memappend() one char at a time.
 *
 * POP_CHAR() and ADVANCE_CHAR() macros are used for code that is
 * common to both the main loop and the tighter loops.
 */

/* POP_CHAR(dest, tok) macro:
 *   Not really a pop()...peeks at the current char and stores it in dest.
 *   Returns 1 on success, sets tok->err and returns 0 if no more chars.
 *   Implicit inputs:  str, len vars
 */
#define POP_CHAR(dest, tok)                                                  \
  (((tok)->char_offset == len) ?                                          \
   (((tok)->depth == 0 && state == json_tokener_state_eatws && saved_state == json_tokener_state_finish) ? \
    (((tok)->err = json_tokener_success), 0)                              \
    :                                                                   \
    (((tok)->err = json_tokener_continue), 0)                             \
    ) :                                                                 \
   (((dest) = *str), 1)                                                 \
   )
 
/* ADVANCE_CHAR() macro:
 *   Incrementes str & tok->char_offset.
 *   For convenience of existing conditionals, returns the old value of c (0 on eof)
 *   Implicit inputs:  c var
 */
#define ADVANCE_CHAR(str, tok) \
  ( ++(str), ((tok)->char_offset)++, c)

/* End optimization macro defs */

struct json_object* json_tokener_parse_ex(struct json_tokener *tok,
					  const char *str, int len)
{
  struct json_object *obj = NULL;
  char c = '\1';

  tok->char_offset = 0;
  tok->err = json_tokener_success;

  while (POP_CHAR(c, tok)) {

  redo_char:
    switch(state) {

    case json_tokener_state_eatws:
      /* Advance until we change state */
      while (isspace(c)) {
	if ((!ADVANCE_CHAR(str, tok)) || (!POP_CHAR(c, tok)))
	  goto out;
      }
      if(c == '/') {
	printbuf_reset(tok->pb);
	printbuf_memappend_fast(tok->pb, &c, 1);
	state = json_tokener_state_comment_start;
      } else {
	state = saved_state;
	goto redo_char;
      }
      break;

    case json_tokener_state_start:
      switch(c) {
      case '{':
	state = json_tokener_state_eatws;
	saved_state = json_tokener_state_object_field_start;
	current = json_object_new_object();
	break;
      case '[':
	state = json_tokener_state_eatws;
	saved_state = json_tokener_state_array;
	current = json_object_new_array();
	break;
      case 'N':
      case 'n':
	state = json_tokener_state_null;
	printbuf_reset(tok->pb);
	tok->st_pos = 0;
	goto redo_char;
      case '"':
      case '\'':
	state = json_tokener_state_string;
	printbuf_reset(tok->pb);
	tok->quote_char = c;
	break;
      case 'T':
      case 't':
      case 'F':
      case 'f':
	state = json_tokener_state_boolean;
	printbuf_reset(tok->pb);
	tok->st_pos = 0;
	goto redo_char;
      case '0' ... '9':  /* #if defined(__GNUC__) */
      case '-':
	state = json_tokener_state_number;
	printbuf_reset(tok->pb);
	tok->is_double = 0;
	goto redo_char;
      default:
	tok->err = json_tokener_error_parse_unexpected;
	goto out;
      }
      break;

    case json_tokener_state_finish:
      if(tok->depth == 0) goto out;
      obj = jsobj_IncrRef(current);
      json_tokener_reset_level(tok, tok->depth);
      tok->depth--;
      goto redo_char;

    case json_tokener_state_null:
      printbuf_memappend_fast(tok->pb, &c, 1);
      if(strncasecmp(json_null_str, tok->pb->buf,
		     json_min(tok->st_pos+1, strlen(json_null_str))) == 0) {
	if(tok->st_pos == strlen(json_null_str)) {
	  current = NULL;
	  saved_state = json_tokener_state_finish;
	  state = json_tokener_state_eatws;
	  goto redo_char;
	}
      } else {
	tok->err = json_tokener_error_parse_null;
	goto out;
      }
      tok->st_pos++;
      break;

    case json_tokener_state_comment_start:
      if(c == '*') {
	state = json_tokener_state_comment;
      } else if(c == '/') {
	state = json_tokener_state_comment_eol;
      } else {
	tok->err = json_tokener_error_parse_comment;
	goto out;
      }
      printbuf_memappend_fast(tok->pb, &c, 1);
      break;

    case json_tokener_state_comment:
              {
          /* Advance until we change state */
          const char *case_start = str;
          while(c != '*') {
            if (!ADVANCE_CHAR(str, tok) || !POP_CHAR(c, tok)) {
              printbuf_memappend_fast(tok->pb, case_start, str-case_start);
              goto out;
            } 
          }
          printbuf_memappend_fast(tok->pb, case_start, 1+str-case_start);
          state = json_tokener_state_comment_end;
        }
            break;

    case json_tokener_state_comment_eol:
      {
	/* Advance until we change state */
	const char *case_start = str;
	while(c != '\n') {
	  if (!ADVANCE_CHAR(str, tok) || !POP_CHAR(c, tok)) {
	    printbuf_memappend_fast(tok->pb, case_start, str-case_start);
	    goto out;
	  }
	}
	printbuf_memappend_fast(tok->pb, case_start, str-case_start);
	MY_DEBUG("json_tokener_comment: %s\n", tok->pb->buf);
	state = json_tokener_state_eatws;
      }
      break;

    case json_tokener_state_comment_end:
      printbuf_memappend_fast(tok->pb, &c, 1);
      if(c == '/') {
	MY_DEBUG("json_tokener_comment: %s\n", tok->pb->buf);
	state = json_tokener_state_eatws;
      } else {
	state = json_tokener_state_comment;
      }
      break;

    case json_tokener_state_string:
      {
	/* Advance until we change state */
	const char *case_start = str;
	while(1) {
	  if(c == tok->quote_char) {
	    printbuf_memappend_fast(tok->pb, case_start, str-case_start);
	    current = json_object_new_string(tok->pb->buf);
	    saved_state = json_tokener_state_finish;
	    state = json_tokener_state_eatws;
	    break;
	  } else if(c == '\\') {
	    printbuf_memappend_fast(tok->pb, case_start, str-case_start);
	    saved_state = json_tokener_state_string;
	    state = json_tokener_state_string_escape;
	    break;
	  }
	  if (!ADVANCE_CHAR(str, tok) || !POP_CHAR(c, tok)) {
	    printbuf_memappend_fast(tok->pb, case_start, str-case_start);
	    goto out;
	  }
	}
      }
      break;

    case json_tokener_state_string_escape:
      switch(c) {
      case '"':
      case '\\':
      case '/':
	printbuf_memappend_fast(tok->pb, &c, 1);
	state = saved_state;
	break;
      case 'b':
      case 'n':
      case 'r':
      case 't':
	if(c == 'b') printbuf_memappend_fast(tok->pb, "\b", 1);
	else if(c == 'n') printbuf_memappend_fast(tok->pb, "\n", 1);
	else if(c == 'r') printbuf_memappend_fast(tok->pb, "\r", 1);
	else if(c == 't') printbuf_memappend_fast(tok->pb, "\t", 1);
	state = saved_state;
	break;
      case 'u':
	tok->ucs_char = 0;
	tok->st_pos = 0;
	state = json_tokener_state_escape_unicode;
	break;
      default:
	tok->err = json_tokener_error_parse_string;
	goto out;
      }
      break;

    case json_tokener_state_escape_unicode:
            /* Note that the following code is inefficient for handling large
       * chunks of extended chars, calling printbuf_memappend() once
       * for each multi-byte character of input.
       * This is a good area for future optimization.
       */
	{
	  /* Advance until we change state */
	  while(1) {
	    if(strchr(json_hex_chars, c)) {
	      tok->ucs_char += ((unsigned int)hexdigit(c) << ((3-tok->st_pos++)*4));
	      if(tok->st_pos == 4) {
		unsigned char utf_out[3];
		if (tok->ucs_char < 0x80) {
		  utf_out[0] = tok->ucs_char;
		  printbuf_memappend_fast(tok->pb, (char*)utf_out, 1);
		} else if (tok->ucs_char < 0x800) {
		  utf_out[0] = 0xc0 | (tok->ucs_char >> 6);
		  utf_out[1] = 0x80 | (tok->ucs_char & 0x3f);
		  printbuf_memappend_fast(tok->pb, (char*)utf_out, 2);
		} else {
		  utf_out[0] = 0xe0 | (tok->ucs_char >> 12);
		  utf_out[1] = 0x80 | ((tok->ucs_char >> 6) & 0x3f);
		  utf_out[2] = 0x80 | (tok->ucs_char & 0x3f);
		  printbuf_memappend_fast(tok->pb, (char*)utf_out, 3);
		}
		state = saved_state;
		break;
	      }
	    } else {
	      tok->err = json_tokener_error_parse_string;
	      goto out;
	      	  }
	  if (!ADVANCE_CHAR(str, tok) || !POP_CHAR(c, tok))
	    goto out;
	}
      }
      break;

    case json_tokener_state_boolean:
      printbuf_memappend_fast(tok->pb, &c, 1);
      if(strncasecmp(json_true_str, tok->pb->buf,
		     json_min(tok->st_pos+1, strlen(json_true_str))) == 0) {
	if(tok->st_pos == strlen(json_true_str)) {
	  current = json_object_new_boolean(1);
	  saved_state = json_tokener_state_finish;
	  state = json_tokener_state_eatws;
	  goto redo_char;
	}
      } else if(strncasecmp(json_false_str, tok->pb->buf,
			    json_min(tok->st_pos+1, strlen(json_false_str))) == 0) {
	if(tok->st_pos == strlen(json_false_str)) {
	  current = json_object_new_boolean(0);
	  saved_state = json_tokener_state_finish;
	  state = json_tokener_state_eatws;
	  goto redo_char;
	}
      } else {
	tok->err = json_tokener_error_parse_boolean;
	goto out;
      }
      tok->st_pos++;
      break;

    case json_tokener_state_number:
      {
	/* Advance until we change state */
	const char *case_start = str;
	int case_len=0;
	while(c && strchr(json_number_chars, c)) {
	  ++case_len;
	  if(c == '.' || c == 'e') tok->is_double = 1;
	  if (!ADVANCE_CHAR(str, tok) || !POP_CHAR(c, tok)) {
	    printbuf_memappend_fast(tok->pb, case_start, case_len);
	    goto out;
	  }
	}
        if (case_len>0)
          printbuf_memappend_fast(tok->pb, case_start, case_len);
      }
      {
        int numi;
        double numd;
        if(!tok->is_double && sscanf(tok->pb->buf, "%d", &numi) == 1) {
          current = json_object_new_int(numi);
        } else if(tok->is_double && sscanf(tok->pb->buf, "%lf", &numd) == 1) {
          current = json_object_new_double(numd);
        } else {
          tok->err = json_tokener_error_parse_number;
          goto out;
        }
        saved_state = json_tokener_state_finish;
        state = json_tokener_state_eatws;
        goto redo_char;
      }
      break;

    case json_tokener_state_array:
      if(c == ']') {
	saved_state = json_tokener_state_finish;
	state = json_tokener_state_eatws;
      } else {
	if(tok->depth >= JSON_TOKENER_MAX_DEPTH-1) {
	  tok->err = json_tokener_error_depth;
	  goto out;
	}
	state = json_tokener_state_array_add;
	tok->depth++;
	json_tokener_reset_level(tok, tok->depth);
	goto redo_char;
      }
      break;

    case json_tokener_state_array_add:
      json_object_array_add(current, obj);
      saved_state = json_tokener_state_array_sep;
      state = json_tokener_state_eatws;
      goto redo_char;

    case json_tokener_state_array_sep:
      if(c == ']') {
	saved_state = json_tokener_state_finish;
	state = json_tokener_state_eatws;
      } else if(c == ',') {
	saved_state = json_tokener_state_array;
	state = json_tokener_state_eatws;
      } else {
	tok->err = json_tokener_error_parse_array;
	goto out;
      }
      break;

    case json_tokener_state_object_field_start:
      if(c == '}') {
	saved_state = json_tokener_state_finish;
	state = json_tokener_state_eatws;
      } else if (c == '"' || c == '\'') {
	tok->quote_char = c;
	printbuf_reset(tok->pb);
	state = json_tokener_state_object_field;
      } else {
	tok->err = json_tokener_error_parse_object_key_name;
	goto out;
      }
      break;

    case json_tokener_state_object_field:
      {
	/* Advance until we change state */
	const char *case_start = str;
	while(1) {
	  if(c == tok->quote_char) {
	    printbuf_memappend_fast(tok->pb, case_start, str-case_start);
	    obj_field_name = strdup(tok->pb->buf);
	    saved_state = json_tokener_state_object_field_end;
	    state = json_tokener_state_eatws;
	    break;
	  } else if(c == '\\') {
	    printbuf_memappend_fast(tok->pb, case_start, str-case_start);
	    saved_state = json_tokener_state_object_field;
	    state = json_tokener_state_string_escape;
	    break;
	  }
	  if (!ADVANCE_CHAR(str, tok) || !POP_CHAR(c, tok)) {
	    printbuf_memappend_fast(tok->pb, case_start, str-case_start);
	    goto out;
	  }
	}
      }
      break;

    case json_tokener_state_object_field_end:
      if(c == ':') {
	saved_state = json_tokener_state_object_value;
	state = json_tokener_state_eatws;
      } else {
	tok->err = json_tokener_error_parse_object_key_sep;
	goto out;
      }
      break;

    case json_tokener_state_object_value:
      if(tok->depth >= JSON_TOKENER_MAX_DEPTH-1) {
	tok->err = json_tokener_error_depth;
	goto out;
      }
      state = json_tokener_state_object_value_add;
      tok->depth++;
      json_tokener_reset_level(tok, tok->depth);
      goto redo_char;

    case json_tokener_state_object_value_add:
      json_object_object_add(current, obj_field_name, obj);
      free(obj_field_name);
      obj_field_name = NULL;
      saved_state = json_tokener_state_object_sep;
      state = json_tokener_state_eatws;
      goto redo_char;

    case json_tokener_state_object_sep:
      if(c == '}') {
	saved_state = json_tokener_state_finish;
	state = json_tokener_state_eatws;
      } else if(c == ',') {
	saved_state = json_tokener_state_object_field_start;
	state = json_tokener_state_eatws;
      } else {
	tok->err = json_tokener_error_parse_object_value_sep;
	goto out;
      }
      break;

    }
    if (!ADVANCE_CHAR(str, tok))
      goto out;
  } /* while(POP_CHAR) */

 out:
  if (!c) { /* We hit an eof char (0) */
    if(state != json_tokener_state_finish &&
       saved_state != json_tokener_state_finish)
      tok->err = json_tokener_error_parse_eof;
  }

  if(tok->err == json_tokener_success) return jsobj_IncrRef(current);
  MY_DEBUG("json_tokener_parse_ex: error %s at offset %d\n",
	   json_tokener_errors[tok->err], tok->char_offset);
  return NULL;
}

/*
 * $Id: linkhash.c,v 1.4 2006/01/26 02:16:28 mclark Exp $
 */

unsigned long lh_ptr_hash(const void *k)
{
	/* CAW: refactored to be 64bit nice */
	return (unsigned long)((((ptrdiff_t)k * LH_PRIME) >> 4) & ULONG_MAX);
}

int lh_ptr_equal(const void *k1, const void *k2)
{
	return (k1 == k2);
}

unsigned long lh_char_hash(const void *k)
{
	unsigned int h = 0;
	const char* data = (const char*)k;
 
	while( *data!=0 ) h = h*129 + (unsigned int)(*data++) + LH_PRIME;

	return h;
}

int lh_char_equal(const void *k1, const void *k2)
{
	return (strcmp((const char*)k1, (const char*)k2) == 0);
}

struct lh_table* lh_table_new(int size, const char *name,
			      lh_entry_free_fn *free_fn,
			      lh_hash_fn *hash_fn,
			      lh_equal_fn *equal_fn)
{
	int i;
	struct lh_table *t;

	t = (struct lh_table*)calloc(1, sizeof(struct lh_table));
	if(!t) MY_ABORT("lh_table_new: calloc failed\n");
	t->count = 0;
	t->size = size;
	t->name = name;
	t->table = (struct lh_entry*)calloc(size, sizeof(struct lh_entry));
	if(!t->table) MY_ABORT("lh_table_new: calloc failed\n");
	t->free_fn = free_fn;
	t->hash_fn = hash_fn;
	t->equal_fn = equal_fn;
	for(i = 0; i < size; i++) t->table[i].k = LH_EMPTY;
	return t;
}

void lh_table_resize(struct lh_table *t, int new_size)
{
	struct lh_table *new_t;
	struct lh_entry *ent;

	new_t = lh_table_new(new_size, t->name, NULL, t->hash_fn, t->equal_fn);
	ent = t->head;
	while(ent) {
		lh_table_insert(new_t, ent->k, ent->v);
		ent = ent->next;
	}
	free(t->table);
	t->table = new_t->table;
	t->size = new_size;
	t->head = new_t->head;
	t->tail = new_t->tail;
	t->resizes++;
	free(new_t);
}

void lh_table_free(struct lh_table *t)
{
	struct lh_entry *c;
	for(c = t->head; c != NULL; c = c->next) {
		if(t->free_fn) {
			t->free_fn(c);
		}
	}
	free(t->table);
	free(t);
}

int lh_table_insert(struct lh_table *t, void *k, const void *v)
{
	unsigned long h, n;

	t->inserts++;
	if(t->count > t->size * 0.66) lh_table_resize(t, t->size * 2);

	h = t->hash_fn(k);
	MY_DEBUG("%s %p, %lu, %d.\n", __func__, t, h, t->size);
	n = h % t->size;
	MY_DEBUG("%s %lu, %lu, %d\n", __func__, n, h, t->size);

	while( 1 ) {
		if(t->table[n].k == LH_EMPTY || t->table[n].k == LH_FREED) break;
		t->collisions++;
		if(++n == t->size) n = 0;
	}

	t->table[n].k = k;
	t->table[n].v = v;
	t->count++;

	if(t->head == NULL) {
		t->head = t->tail = &t->table[n];
		t->table[n].next = t->table[n].prev = NULL;
	} else {
		t->tail->next = &t->table[n];
		t->table[n].prev = t->tail;
		t->table[n].next = NULL;
		t->tail = &t->table[n];
	}

	return 0;
}

struct lh_entry* lh_table_lookup_entry(struct lh_table *t, const void *k)
{
	unsigned long h = t->hash_fn(k);
	unsigned long n = h % t->size;

	t->lookups++;
	while( 1 ) {
		if(t->table[n].k == LH_EMPTY) return NULL;
		if(t->table[n].k != LH_FREED &&
		   t->equal_fn(t->table[n].k, k)) return &t->table[n];
		if(++n == t->size) n = 0;
	}
	return NULL;
}

const void* lh_table_lookup(struct lh_table *t, const void *k)
{
	struct lh_entry *e = lh_table_lookup_entry(t, k);
	if(e) return e->v;
	return NULL;
}

int lh_table_delete_entry(struct lh_table *t, struct lh_entry *e)
{
	ptrdiff_t n = (ptrdiff_t)(e - t->table); /* CAW: fixed to be 64bit nice, still need the crazy negative case... */

	/* CAW: this is bad, really bad, maybe stack goes other direction on this machine... */
	if(n < 0) { return -2; }

	if(t->table[n].k == LH_EMPTY || t->table[n].k == LH_FREED) return -1;
	t->count--;
	if(t->free_fn) t->free_fn(e);
	t->table[n].v = NULL;
	t->table[n].k = LH_FREED;
	if(t->tail == &t->table[n] && t->head == &t->table[n]) {
		t->head = t->tail = NULL;
	} else if (t->head == &t->table[n]) {
		t->head->next->prev = NULL;
		t->head = t->head->next;
	} else if (t->tail == &t->table[n]) {
		t->tail->prev->next = NULL;
		t->tail = t->tail->prev;
	} else {
		t->table[n].prev->next = t->table[n].next;
		t->table[n].next->prev = t->table[n].prev;
	}
	t->table[n].next = t->table[n].prev = NULL;
	return 0;
}

int lh_table_delete(struct lh_table *t, const void *k)
{
	struct lh_entry *e = lh_table_lookup_entry(t, k);
	if(!e) return -1;
	return lh_table_delete_entry(t, e);
}

/*
 * $Id: printbuf.c,v 1.5 2006/01/26 02:16:28 mclark Exp $
 */

void printbuf_reset(struct printbuf *p)
{
	p->buf[0] = '\0';
	p->bpos = 0;
}

void printbuf_free(struct printbuf *p)
{
	if (p) {
		free(p->buf);
		free(p);
	}
}

struct printbuf* printbuf_new(void)
{
	struct printbuf *p;

	p = (struct printbuf*)calloc(1, sizeof(struct printbuf));
	if(!p) return NULL;
	p->size = 32;
	p->bpos = 0;
	if(!(p->buf = (char*)malloc(p->size))) {
		free(p);
		return NULL;
	}
	return p;
}

int printbuf_memappend(struct printbuf *p, const char *buf, int size)
{
	char *t;
	if(p->size - p->bpos <= size) {
		int new_size = json_max(p->size * 2, p->bpos + size + 8);
#if 0
		MY_DEBUG("printbuf_memappend: realloc "
			"bpos=%d wrsize=%d old_size=%d new_size=%d\n",
			p->bpos, size, p->size, new_size);
#endif
		if(!(t = (char*)realloc(p->buf, new_size))) return -1;
		p->size = new_size;
		p->buf = t;
	}
	memcpy(p->buf + p->bpos, buf, size);
	p->bpos += size;
	p->buf[p->bpos]= '\0';
	return size;
}

#if !HAVE_VSNPRINTF && defined(WIN32)
# define vsnprintf _vsnprintf
#elif !HAVE_VSNPRINTF /* !HAVE_VSNPRINTF */
# error Need vsnprintf!
#endif /* !HAVE_VSNPRINTF && defined(WIN32) */

#if !HAVE_VASPRINTF
/* CAW: compliant version of vasprintf */
static int vasprintf(char **buf, const char *fmt, va_list ap)
{
#ifndef WIN32
	static char _T_emptybuffer = '\0';
#endif /* !defined(WIN32) */
	int chars;
	char *b;

	if(!buf) { return -1; }

#ifdef WIN32
	chars = _vscprintf(fmt, ap)+1;
#else /* !defined(WIN32) */
	/* CAW: RAWR! We have to hope to god here that vsnprintf doesn't overwrite
	   our buffer like on some 64bit sun systems.... but hey, its time to move on */
	chars = vsnprintf(&_T_emptybuffer, 0, fmt, ap)+1;
	if(chars < 0) { chars *= -1; } /* CAW: old glibc versions have this problem */
#endif /* defined(WIN32) */

	b = (char*)malloc(sizeof(char)*chars);
	if(!b) { return -1; }

	if((chars = vsprintf(b, fmt, ap)) < 0) {
		free(b);
	} else {
		*buf = b;
	}

	return chars;
}
#endif /* !HAVE_VASPRINTF */

int sprintbuf(struct printbuf *p, const char *msg, ...)
{
  va_list ap;
  char *t;
  int size;
  char buf[128];

  /* user stack buffer first */
  va_start(ap, msg);
  size = vsnprintf(buf, 128, msg, ap);
  va_end(ap);
  /* if string is greater than stack buffer, then use dynamic string
     with vasprintf.  Note: some implementation of vsnprintf return -1
     if output is truncated whereas some return the number of bytes that
     would have been written - this code handles both cases. */
  if(size == -1 || size > 127) {
    va_start(ap, msg);
    if((size = vasprintf(&t, msg, ap)) == -1) { va_end(ap); return -1; }
    va_end(ap);
    printbuf_memappend(p, t, size);
    free(t);
    return size;
  } else {
    printbuf_memappend(p, buf, size);
    return size;
  }
}

/*
 * $Id: debug.c,v 1.5 2006/01/26 02:16:28 mclark Exp $
 */

void my_abort(const char* file, const int line, const char *msg, ...)
{
	va_list ap;

	printf( "%s(%d) ", file, line );
	va_start(ap, msg);
	vprintf(msg, ap);
	va_end(ap);
	exit(1);
}

void my_debug(const char* file, const int line, const char *msg, ...)
{
	va_list ap;

	// fprintf(stderr, "%p:%d\t", json_object_table, json_object_table->size);
	fprintf(stderr, "%s(%d)\t", file, line );
	va_start(ap, msg);
	vprintf(msg, ap);
	va_end(ap);
}

void my_error(const char* file, const int line, const char *msg, ...)
{
	va_list ap;

	fprintf(stderr, "%s(%d) ", file, line );
	va_start(ap, msg);
	vprintf(msg, ap);
	va_end(ap);
}

void my_mylog(const char* file, const int line, const char *msg,...)
{
	time_t  curr ;
	FILE*   fp;
	struct  tm* tm_p;
	va_list ap;
	char    buf[ BUFSIZ ] ;

	curr = time( NULL );
	tm_p = localtime( &curr );

	sprintf( buf, "my.log" ) ;
	if( ( fp = fopen( buf, "a" ) ) == NULL )
		return;
	sprintf( buf, "%04d%02d%02d-%02d:%02d:%02d %s(%d) ", tm_p->tm_year+1900, tm_p->tm_mon+1, tm_p->tm_mday ,
		tm_p->tm_hour, tm_p->tm_min, tm_p->tm_sec, file , line );

	fputs( buf, fp );

	va_start(ap, msg);
	vfprintf(fp , msg, ap);
	va_end(ap);
	fclose( fp );
	return ;
}

