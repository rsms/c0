// SPDX-License-Identifier: Apache-2.0
#include "colib.h"
#include "compiler.h"

#if DEBUG
#include "abuf.h"
#endif


static void append(buf_t* buf, type_t* t);


// static nodekind_t tidc_nodekind(char c) {
//   switch (c) {
//   #define _(NAME) case TYPEID_PREFIX(NAME): return NAME;
//   FOREACH_NODEKIND_TYPE(_)
//   #undef _
//   }
//   return NODE_BAD;
// }


static void funtype(buf_t* buf, funtype_t* t) {
  buf_print_leb128_u32(buf, t->params.len);
  for (u32 i = 0; i < t->params.len; i++) {
    local_t* param = t->params.v[i];
    assert(param->kind == EXPR_PARAM);
    append(buf, assertnotnull(param->type));
  }
  append(buf, t->result);
}


static void structtype(buf_t* buf, structtype_t* t) {
  buf_print_leb128_u32(buf, t->fields.len);
  for (u32 i = 0; i < t->fields.len; i++) {
    local_t* field = t->fields.v[i];
    assert(field->kind == EXPR_FIELD);
    append(buf, assertnotnull(field->type));
  }
}


static void arraytype(buf_t* buf, arraytype_t* t) {
  buf_print_leb128_u32(buf, t->len);
  append(buf, t->elem);
}


static void aliastype(buf_t* buf, aliastype_t* t) {
  usize namelen = strlen(t->name);
  buf_print_leb128_u32(buf, (u32)namelen);
  buf_append(buf, t->name, namelen);
  // TODO: fully-qualified name
}


static void append(buf_t* buf, type_t* t) {
  if (type_isprim(t)) {
    buf_push(buf, (u8)t->tid[0]);
    return;
  }

  if (t->tid) {
    // assert(t->kind != TYPE_UNKNOWN);
    buf_print(buf, t->tid);
    return;
  }

  usize bufstart = buf->len;
  buf_push(buf, TYPEID_PREFIX(t->kind));

  switch (t->kind) {
    case TYPE_ARRAY:    arraytype(buf, (arraytype_t*)t); break;
    case TYPE_FUN:      funtype(buf, (funtype_t*)t); break;
    case TYPE_PTR:      append(buf, ((ptrtype_t*)t)->elem); break;
    case TYPE_REF:
    case TYPE_MUTREF:   append(buf, ((reftype_t*)t)->elem); break;
    case TYPE_SLICE:
    case TYPE_MUTSLICE: append(buf, ((slicetype_t*)t)->elem); break;
    case TYPE_OPTIONAL: append(buf, ((opttype_t*)t)->elem); break;
    case TYPE_STRUCT:   structtype(buf, (structtype_t*)t); break;
    case TYPE_ALIAS:    aliastype(buf, (aliastype_t*)t); break;
    default:            assertf(0, "unexpected %s", nodekind_name(t->kind));
  }

  // update t
  // note: may have been set in duplicate code branch
  if (!t->tid)
    t->tid = sym_intern(&buf->bytes[bufstart], buf->len - bufstart);
}


sym_t _typeid(type_t* t) {
  if (t->tid)
    return t->tid;

  char storage[64];
  buf_t buf = buf_makeext(memalloc_ctx(), storage, sizeof(storage));

  append(&buf, t);

  if UNLIKELY(!buf_nullterm(&buf))
    panic("out of memory");
  buf_dispose(&buf);

  assertnotnull(t->tid); // else append failed

  // #if DEBUG
  //   char tmp[128];
  //   abuf_t s = abuf_make(tmp, sizeof(tmp));
  //   abuf_repr(&s, t->tid, strlen(t->tid));
  //   abuf_terminate(&s);
  //   dlog("_typeid(%s) => \"%s\"", nodekind_name(t->kind), tmp);
  // #endif

  return t->tid;
}
