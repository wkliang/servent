/*
 * $Id: json.h,v 1.6 2006/01/26 02:16:28 mclark Exp $
 *
 * Copyright (c) 2004, 2005 Metaparadigm Pte. Ltd.
 * Michael Clark <michael@metaparadigm.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See COPYING for details.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#ifndef _json_h_
#define _json_h_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * $Id: bits.h,v 1.10 2006/01/30 23:07:57 mclark Exp $
 */

#ifndef json_min
#define json_min(a,b) ((a) < (b) ? (a) : (b))
#endif

#ifndef json_max
#define json_max(a,b) ((a) > (b) ? (a) : (b))
#endif

#define hexdigit(x) (((x) <= '9') ? (x) - '0' : ((x) & 7) + 9)
#define error_ptr(error) ((void*)error)
#define is_error(ptr) ((unsigned long)ptr > (unsigned long)-4000L)

/*
 * $Id: debug.h,v 1.5 2006/01/30 23:07:57 mclark Exp $
 */
extern void my_abort(const char* file, const int line, const char *msg, ...);
extern void my_debug(const char* file, const int line, const char *msg, ...);
extern void my_error(const char* file, const int line, const char *msg, ...);
extern void my_mylog(const char* file, const int line, const char *msg, ...);

#ifndef MY_DEBUGGING
#	define MY_DEBUGGING	0
#endif

#define MY_ABORT(x, ...) my_abort(__FILE__, __LINE__, x, ##__VA_ARGS__)
#define MY_ERROR(x, ...) my_error(__FILE__, __LINE__, x, ##__VA_ARGS__)
#define MY_DEBUG(x, ...) if (MY_DEBUGGING) my_debug(__FILE__, __LINE__, x, ##__VA_ARGS__)
#define MY_MYLOG(x, ...) my_mylog(__FILE__, __LINE__, x, ##__VA_ARGS__)

/*
 * $Id: linkhash.h,v 1.6 2006/01/30 23:07:57 mclark Exp $
 */

/**
 * golden prime used in hash functions
 */
#define LH_PRIME 0x9e370001UL
/**
 * sentinel pointer value for empty slots
 */
#define LH_EMPTY (void*)-1
/**
 * sentinel pointer value for freed slots
 */
#define LH_FREED (void*)-2

/**
 * An entry in the hash table
 */
struct lh_entry {
	void *k;		/* The key. */
	const void *v;		/* The value. */
	struct lh_entry *next;	/* The next entry */
	struct lh_entry *prev;	/* The previous entry. */
};

/**
 * callback function prototypes
 */
typedef void (lh_entry_free_fn) (struct lh_entry *e);
/**
 * callback function prototypes
 */
typedef unsigned long (lh_hash_fn) (const void *k);
/**
 * callback function prototypes
 */
typedef int (lh_equal_fn) (const void *k1, const void *k2);

/**
 * The hash table structure.
 */
struct lh_table {
	int size;	/* Size of our hash. */
	int count;	/* Numbers of entries. */
	int collisions;	/* Number of collisions. */
	int resizes;	/* Number of resizes. */
	int lookups;	/* Number of lookups. */
	int inserts;	/* Number of inserts.  */
	int deletes;	/* Number of deletes.  */

	const char *name;	/* Name of the hash table.  */
	struct lh_entry *head;	/* The first entry.  */
	struct lh_entry *tail;	/* The last entry.  */
	struct lh_entry *table;

	/**
	 * A pointer onto the function responsible for freeing an entry.
	 */
	lh_entry_free_fn *free_fn;
	lh_hash_fn *hash_fn;
	lh_equal_fn *equal_fn;
};

/**
 * Pre-defined hash and equality functions
 */
extern unsigned long lh_ptr_hash(const void *k);
extern int lh_ptr_equal(const void *k1, const void *k2);

extern unsigned long lh_char_hash(const void *k);
extern int lh_char_equal(const void *k1, const void *k2);

/**
 * Convenience list iterator.
 */
#define lh_foreach(table, entry) \
for(entry = table->head; entry; entry = entry->next)

/**
 * lh_foreach_safe allows calling of deletion routine while iterating.
 */
#define lh_foreach_safe(table, entry, tmp) \
for(entry = table->head; entry && ((tmp = entry->next) || 1); entry = tmp)

/**
 * Create a new linkhash table.
 * @param size initial table size. The table is automatically resized
 * although this incurs a performance penalty.
 * @param name the table name.
 * @param free_fn callback function used to free memory for entries
 * when lh_table_free or lh_table_delete is called.
 * If NULL is provided, then memory for keys and values
 * must be freed by the caller.
 * @param hash_fn  function used to hash keys. 2 standard ones are defined:
 * lh_ptr_hash and lh_char_hash for hashing pointer values
 * and C strings respectively.
 * @param equal_fn comparison function to compare keys. 2 standard ones defined:
 * lh_ptr_hash and lh_char_hash for comparing pointer values
 * and C strings respectively.
 * @return a pointer onto the linkhash table.
 */
extern struct lh_table* lh_table_new(int size, const char *name,
				     lh_entry_free_fn *free_fn,
				     lh_hash_fn *hash_fn,
				     lh_equal_fn *equal_fn);

/**
 * Convenience macros to create a new linkhash table with char/ptr keys.
 */
#define lh_kchar_table_new(s,n,f)	lh_table_new((s), (n), (f), lh_char_hash, lh_char_equal)
#define lh_kptr_table_new(s,n,f)	lh_table_new((s), (n), (f), lh_ptr_hash, lh_ptr_equal)

/**
 * Free a linkhash table.
 * If a callback free function is provided then it is called for all
 * entries in the table.
 * @param t table to free.
 */
extern void lh_table_free(struct lh_table *t);

/**
 * Insert a record into the table.
 * @param t the table to insert into.
 * @param k a pointer to the key to insert.
 * @param v a pointer to the value to insert.
 */
extern int lh_table_insert(struct lh_table *t, void *k, const void *v);

/**
 * Lookup a record into the table.
 * @param t the table to lookup
 * @param k a pointer to the key to lookup
 * @return a pointer to the record structure of the value or NULL if it does not exist.
 */
extern struct lh_entry* lh_table_lookup_entry(struct lh_table *t, const void *k);

/**
 * Lookup a record into the table
 * @param t the table to lookup
 * @param k a pointer to the key to lookup
 * @return a pointer to the found value or NULL if it does not exist.
 */
extern const void* lh_table_lookup(struct lh_table *t, const void *k);

/**
 * Delete a record from the table.
 * If a callback free function is provided then it is called for the
 * for the item being deleted.
 * @param t the table to delete from.
 * @param e a pointer to the entry to delete.
 * @return 0 if the item was deleted.
 * @return -1 if it was not found.
 */
extern int lh_table_delete_entry(struct lh_table *t, struct lh_entry *e);

/**
 * Delete a record from the table.
 * If a callback free function is provided then it is called for the
 * for the item being deleted.
 * @param t the table to delete from.
 * @param k a pointer to the key to delete.
 * @return 0 if the item was deleted.
 * @return -1 if it was not found.
 */
extern int lh_table_delete(struct lh_table *t, const void *k);

void lh_table_resize(struct lh_table *t, int new_size);

/*
 * $Id: arraylist.h,v 1.4 2006/01/26 02:16:28 mclark Exp $
 */

#define ARRAY_LIST_DEFAULT_SIZE 32

typedef void (array_list_free_fn) (void *data);

struct array_list {
	void **array;
	int length;
	int size;
	array_list_free_fn *free_fn;
};

extern struct array_list* array_list_new(array_list_free_fn *free_fn);

extern void array_list_free(struct array_list *al);

extern void* array_list_get_idx(struct array_list *al, int i);

extern int array_list_put_idx(struct array_list *al, int i, void *data);

extern int array_list_add(struct array_list *al, void *data);

extern int array_list_length(struct array_list *al);

/*
 * $Id: printbuf.h,v 1.4 2006/01/26 02:16:28 mclark Exp $
 */
struct printbuf {
	char *buf;
	int bpos;
	int size;
};

extern struct printbuf* printbuf_new(void);

/* As an optimization, printbuf_memappend is defined as a macro that
 * handles copying data if the buffer is large enough; otherwise it
 * invokes printbuf_memappend_real() which performs the heavy lifting
 * of realloc()ing the buffer and copying data.
 */
extern int printbuf_memappend(struct printbuf *p, const char *buf, int size);

#define printbuf_buf(p)		((p)->buf)
#define printbuf_bpos(p)	((p)->bpos)

#define printbuf_memappend_fast(p, bufptr, bufsize)          \
do {                                                         \
  if ((p->size - p->bpos) > bufsize) {                       \
    memcpy(p->buf + p->bpos, (bufptr), bufsize);             \
    p->bpos += bufsize;                                      \
    p->buf[p->bpos]= '\0';                                   \
  } else {  printbuf_memappend(p, (bufptr), bufsize); }      \
} while (0)

extern int sprintbuf(struct printbuf *p, const char *msg, ...);

extern void printbuf_reset(struct printbuf *p);

extern void printbuf_free(struct printbuf *p);

/*
 * $Id: json_object.h,v 1.12 2006/01/30 23:07:57 mclark Exp $
 */

#define JSON_OBJECT_DEF_HASH_ENTRIES 16

#undef FALSE
#define FALSE ((boolean)0)

#undef TRUE
#define TRUE ((boolean)1)

extern const char *json_number_chars;
extern const char *json_hex_chars;

/* forward structure definitions */

typedef int boolean;
typedef struct printbuf printbuf;
typedef struct lh_table lh_table;
typedef struct array_list array_list;
typedef struct json_object json_object;
typedef struct json_object_iter json_object_iter;
typedef struct json_tokener json_tokener;

/* supported object types */

typedef enum json_type {
	json_type_null,
	json_type_boolean,
	json_type_double,
	json_type_int,
	json_type_object,
	json_type_array,
	json_type_string
} json_type;

/* reference counting functions */

/**
 * Increment the reference count of json_object
 * @param obj the json_object instance
 */
extern json_object* jsobj_IncrRef(json_object *obj);

/**
 * Decrement the reference count of json_object and free if it reaches zero
 * @param obj the json_object instance
 */
extern void jsobj_DecrRef(struct json_object *obj);

/**
 * Check if the json_object is of a given type
 * @param obj the json_object instance
 * @param type one of:
     json_type_boolean,
     json_type_double,
     json_type_int,
     json_type_object,
     json_type_array,
     json_type_string,
 */
extern int json_object_is_type(struct json_object *obj, enum json_type type);

/**
 * Get the type of the json_object
 * @param obj the json_object instance
 * @returns type being one of: json_type
 */
extern enum json_type json_object_get_type(struct json_object *obj);

/** Stringify object to json format
 * @param obj the json_object instance
 * @returns a string in JSON format
 */
extern const char* json_object_to_string(struct json_object *obj);

/* object type methods */

/** Create a new empty object
 * @returns a json_object of type json_type_object
 */
extern struct json_object* json_object_new_object(void);

/** Get the hashtable of a json_object of type json_type_object
 * @param obj the json_object instance
 * @returns a linkhash
 */
extern struct lh_table* json_object_get_object(struct json_object *obj);

/** Add an object field to a json_object of type json_type_object
 *
 * The reference count will *not* be incremented. This is to make adding
 * fields to objects in code more compact. If you want to retain a reference
 * to an added object you must wrap the passed object with json_object_get
 *
 * @param obj the json_object instance
 * @param key the object field name (a private copy will be duplicated)
 * @param val a json_object or NULL member to associate with the given field
 */
extern void json_object_object_add(struct json_object* obj, const char *key,
				   struct json_object *val);

/** Get the json_object associate with a given object field
 * @param obj the json_object instance
 * @param key the object field name
 * @returns the json_object associated with the given field name
 */
extern struct json_object* json_object_object_get(struct json_object* obj,
						  const char *key);

/** Delete the given json_object field
 *
 * The reference count will be decremented for the deleted object
 *
 * @param obj the json_object instance
 * @param key the object field name
 */
extern void json_object_object_del(struct json_object* obj, const char *key);

/** Iterate through all keys and values of an object
 * @param obj the json_object instance
 * @param key the local name for the char* key variable defined in the body
 * @param val the local name for the json_object* object variable defined in the body
 */
#if defined(__GNUC__) && !defined(__STRICT_ANSI__)

# define json_object_object_foreach(obj,key,val) \
 char *key; struct json_object *val; \
 for(struct lh_entry *entry = json_object_get_object(obj)->head; ({ if(entry) { key = (char*)entry->k; val = (struct json_object*)entry->v; } ; entry; }); entry = entry->next )

#else /* ANSI C or MSC */

# define json_object_object_foreach(obj,key,val) \
 char *key; struct json_object *val; struct lh_entry *entry; \
 for(entry = json_object_get_object(obj)->head; (entry ? (key = (char*)entry->k, val = (struct json_object*)entry->v, entry) : 0); entry = entry->next)

#endif /* defined(__GNUC__) && !defined(__STRICT_ANSI__) */

/** Iterate through all keys and values of an object (ANSI C Safe)
 * @param obj the json_object instance
 * @param iter the object iterator
 */
#define json_object_object_foreachC(obj,iter) \
 for(iter.entry = json_object_get_object(obj)->head; (iter.entry ? (iter.key = (char*)iter.entry->k, iter.val = (struct json_object*)iter.entry->v, iter.entry) : 0); iter.entry = iter.entry->next)

/* Array type methods */

/** Create a new empty json_object of type json_type_array
 * @returns a json_object of type json_type_array
 */
extern struct json_object* json_object_new_array(void);

/** Get the arraylist of a json_object of type json_type_array
 * @param obj the json_object instance
 * @returns an arraylist
 */
extern struct array_list* json_object_get_array(struct json_object *obj);

/** Get the length of a json_object of type json_type_array
 * @param obj the json_object instance
 * @returns an int
 */
extern int json_object_array_length(struct json_object *obj);

/** Add an element to the end of a json_object of type json_type_array
 *
 * The reference count will *not* be incremented. This is to make adding
 * fields to objects in code more compact. If you want to retain a reference
 * to an added object you must wrap the passed object with json_object_get
 *
 * @param obj the json_object instance
 * @param val the json_object to be added
 */
extern int json_object_array_add(struct json_object *obj,
				 struct json_object *val);

/** Insert or replace an element at a specified index in an array (a json_object of type json_type_array)
 *
 * The reference count will *not* be incremented. This is to make adding
 * fields to objects in code more compact. If you want to retain a reference
 * to an added object you must wrap the passed object with json_object_get
 *
 * The reference count of a replaced object will be decremented.
 *
 * The array size will be automatically be expanded to the size of the
 * index if the index is larger than the current size.
 *
 * @param obj the json_object instance
 * @param idx the index to insert the element at
 * @param val the json_object to be added
 */
extern int json_object_array_put_idx(struct json_object *obj, int idx,
				     struct json_object *val);

/** Get the element at specificed index of the array (a json_object of type json_type_array)
 * @param obj the json_object instance
 * @param idx the index to get the element at
 * @returns the json_object at the specified index (or NULL)
 */
extern struct json_object* json_object_array_get_idx(struct json_object *obj,
						     int idx);

/* boolean type methods */

/** Create a new empty json_object of type json_type_boolean
 * @param b a boolean TRUE or FALSE (0 or 1)
 * @returns a json_object of type json_type_boolean
 */
extern struct json_object* json_object_new_boolean(boolean b);

/** Get the boolean value of a json_object
 *
 * The type is coerced to a boolean if the passed object is not a boolean.
 * integer and double objects will return FALSE if there value is zero
 * or TRUE otherwise. If the passed object is a string it will return
 * TRUE if it has a non zero length. If any other object type is passed
 * TRUE will be returned if the object is not NULL.
 *
 * @param obj the json_object instance
 * @returns a boolean
 */
extern boolean json_object_get_boolean(struct json_object *obj);

/* int type methods */

/** Create a new empty json_object of type json_type_int
 * @param i the integer
 * @returns a json_object of type json_type_int
 */
extern struct json_object* json_object_new_int(int i);

/** Get the int value of a json_object
 *
 * The type is coerced to a int if the passed object is not a int.
 * double objects will return their integer conversion. Strings will be
 * parsed as an integer. If no conversion exists then 0 is returned.
 *
 * @param obj the json_object instance
 * @returns an int
 */
extern int json_object_get_int(struct json_object *obj);

/* double type methods */

/** Create a new empty json_object of type json_type_double
 * @param d the double
 * @returns a json_object of type json_type_double
 */
extern struct json_object* json_object_new_double(double d);

/** Get the double value of a json_object
 *
 * The type is coerced to a double if the passed object is not a double.
 * integer objects will return their dboule conversion. Strings will be
 * parsed as a double. If no conversion exists then 0.0 is returned.
 *
 * @param obj the json_object instance
 * @returns an double
 */
extern double json_object_get_double(struct json_object *obj);

/* string type methods */

/** Create a new empty json_object of type json_type_string
 *
 * A copy of the string is made and the memory is managed by the json_object
 *
 * @param s the string
 * @returns a json_object of type json_type_string
 */
extern struct json_object* json_object_new_string(const char *s);

extern struct json_object* json_object_new_string_len(const char *s, int len);

/** Get the string value of a json_object
 *
 * If the passed object is not of type json_type_string then the JSON
 * representation of the object is returned.
 *
 * The returned string memory is managed by the json_object and will
 * be freed when the reference count of the json_object drops to zero.
 *
 * @param obj the json_object instance
 * @returns a string
 */
extern const char* json_object_get_string(struct json_object *obj);

/*
 * wkliang:20110522
 */
void jsobj_dump(json_object* obj, const char* name, int tab);
const char* jsobj_get_string(json_object *jso, const char* key, const char* val);
void jsobj_url_decode(json_object* jso, const char* cp);

/*
 * $Id: json_tokener.h,v 1.10 2006/07/25 03:24:50 mclark Exp $
 */

enum json_tokener_error {
	json_tokener_success,
	json_tokener_continue,
	json_tokener_error_depth,
	json_tokener_error_parse_eof,
	json_tokener_error_parse_unexpected,
	json_tokener_error_parse_null,
	json_tokener_error_parse_boolean,
	json_tokener_error_parse_number,
	json_tokener_error_parse_array,
	json_tokener_error_parse_object_key_name,
	json_tokener_error_parse_object_key_sep,
	json_tokener_error_parse_object_value_sep,
	json_tokener_error_parse_string,
	json_tokener_error_parse_comment
};

enum json_tokener_state {
	json_tokener_state_eatws,
	json_tokener_state_start,
	json_tokener_state_finish,
	json_tokener_state_null,
	json_tokener_state_comment_start,
	json_tokener_state_comment,
	json_tokener_state_comment_eol,
	json_tokener_state_comment_end,
	json_tokener_state_string,
	json_tokener_state_string_escape,
	json_tokener_state_escape_unicode,
	json_tokener_state_boolean,
	json_tokener_state_number,
	json_tokener_state_array,
	json_tokener_state_array_add,
	json_tokener_state_array_sep,
	json_tokener_state_object_field_start,
	json_tokener_state_object_field,
	json_tokener_state_object_field_end,
	json_tokener_state_object_value,
	json_tokener_state_object_value_add,
	json_tokener_state_object_sep
};

struct json_tokener_srec {
	enum json_tokener_state state, saved_state;
	struct json_object *obj;
	struct json_object *current;
	char *obj_field_name;
};

#define JSON_TOKENER_MAX_DEPTH 32

struct json_tokener {
	char *str;
	struct printbuf *pb;
	int depth, is_double, st_pos, char_offset;
	ptrdiff_t err;
	unsigned int ucs_char;
	char quote_char;
	struct json_tokener_srec stack[JSON_TOKENER_MAX_DEPTH];
};

extern const char* json_tokener_errors[];

extern struct json_tokener* json_tokener_new(void);
extern void json_tokener_free(struct json_tokener *tok);
extern void json_tokener_reset(struct json_tokener *tok);
extern struct json_object* json_tokener_parse(const char *str);
extern struct json_object* json_tokener_parse_ex(struct json_tokener *tok, const char *str, int len);

#ifdef __cplusplus
}
#endif

#endif
