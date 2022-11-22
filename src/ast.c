// diagnostics reporting
#include "c0lib.h"
#include "compiler.h"
#include "abuf.h"


typedef enum reprflag {
  REPRFLAG_HEAD = 1 << 0, // is list head
  REPRFLAG_SHORT = 1 << 1,
} reprflag_t;


typedef struct {
  buf_t outbuf;
  err_t err;
  map_t seen;
} repr_t;


// node kind string table with compressed indices (compared to table of pointers.)
// We end up with something like this; one string with indices:
//   enum {
//     NK_NBAD, NK__NBAD = NK_NBAD + strlen("NBAD"),
//     NK_NCOMMENT, NK__NCOMMENT = NK_NCOMMENT + strlen("NCOMMENT"),
//     NK_NUNIT, NK__NUNIT = NK_NUNIT + strlen("NUNIT"),
//     NK_NFUN, NK__NFUN = NK_NFUN + strlen("NFUN"),
//     NK_NBLOCK, NK__NBLOCK = NK_NBLOCK + strlen("NBLOCK"),
//     NK_UNKNOWN, NK__UNKNOWN = NK_UNKNOWN + strlen("N?"),
//   };
//   static const struct { u8 offs[NODEKIND_COUNT]; char strs[]; } nodekind_strtab = {
//     { NK_NBAD, NK_NCOMMENT, NK_NUNIT, NK_NFUN, NK_NBLOCK },
//     { "NBAD\0NCOMMENT\0NUNIT\0NFUN\0NBLOCK\0N?" }
//   };
#define NK_UNKNOWN_STR "NODE?"
enum {
  #define _(NAME) NK_##NAME, NK__##NAME = NK_##NAME + strlen(#NAME),
  FOREACH_NODEKIND(_)
  FOREACH_NODEKIND_TYPE(_)
  #undef _
  NK_UNKNOWN, NK__UNKNOWN = NK_UNKNOWN + strlen(NK_UNKNOWN_STR),
};
static const struct {
  int  offs[NODEKIND_COUNT + 1]; // index into strs
  char strs[];
} nodekind_strtab = {
  { // get offset from enum
    #define _(NAME) NK_##NAME,
    FOREACH_NODEKIND(_)
    FOREACH_NODEKIND_TYPE(_)
    #undef _
    NK_UNKNOWN,
  }, {
    #define _(NAME) #NAME "\0"
    FOREACH_NODEKIND(_)
    FOREACH_NODEKIND_TYPE(_)
    #undef _
    NK_UNKNOWN_STR
  }
};


#define STRTAB_GET(strtab, kind) \
  &(strtab).strs[ (strtab).offs[ MIN((kind), countof(strtab.offs)-1) ] ]


#define CHAR(ch) ( \
  buf_push(&r->outbuf, (ch)) ?: seterr(r, ErrNoMem) )

#define PRINT(cstr) ( \
  buf_print(&r->outbuf, (cstr)) ?: seterr(r, ErrNoMem) )

#define PRINTF(fmt, args...) ( \
  buf_printf(&r->outbuf, (fmt), ##args) ?: seterr(r, ErrNoMem) )

#define FILL(byte, len) ( \
  buf_fill(&r->outbuf, (byte), (len)) ?: seterr(r, ErrNoMem) )

#define INDENT 2

#define REPR_BEGIN(opench, kindname) ({ \
  if ((fl & REPRFLAG_HEAD) == 0) { \
    CHAR('\n'), FILL(' ', indent); \
    indent += INDENT; \
  } \
  fl &= ~REPRFLAG_HEAD; \
  CHAR(opench); \
  PRINT(kindname); \
})

#define REPR_END(closech) \
  ( CHAR((closech)), indent -= 2 )


const char* nodekind_name(nodekind_t kind) {
  return STRTAB_GET(nodekind_strtab, kind);
}


static void seterr(repr_t* r, err_t err) {
  if (!r->err)
    r->err = err;
}


#define RPARAMS repr_t* r, usize indent, reprflag_t fl
#define RARGS       r, indent, fl
#define RARGSFL(fl) r, indent, fl

static void repr(RPARAMS, const node_t* n);
static void repr_type(RPARAMS, const type_t* t);


static bool seen(repr_t* r, const void* n) {
  if (nodekind_isbasictype(((const node_t*)n)->kind))
    return false;
  const void** vp = (const void**)map_assign_ptr(&r->seen, r->outbuf.ma, n);
  if (vp && !*vp) {
    *vp = n;
    return false;
  }
  if (!vp)
    seterr(r, ErrNoMem);
  CHAR('\'');
  return true;
}


static void repr_typedef(RPARAMS, const typedef_t* n) {
  CHAR(' ');
  PRINT(n->name);
  CHAR(' ');
  repr_type(RARGS, n->type);
}


static void repr_struct(RPARAMS, const structtype_t* n, bool isnew) {
  if (n->name)
    CHAR(' '), PRINT(n->name);
  if (isnew) for (u32 i = 0; i < n->fields.len; i++) {
    CHAR(' ');
    repr(RARGS, n->fields.v[i]);
  }
}


static void repr_fun(RPARAMS, const fun_t* n) {
  if (n->name) {
    CHAR(' '), PRINT(n->name);
  }
  {
    REPR_BEGIN('(', "params");
    for (u32 i = 0; i < n->params.len; i++) {
      if (i) CHAR(' ');
      repr(RARGS, n->params.v[i]);
    }
    REPR_END(')');
  }
  {
    REPR_BEGIN('(', "result");
    CHAR(' ');
    if (n->type->kind == TYPE_FUN) {
      repr_type(RARGS, ((funtype_t*)n->type)->result);
    } else {
      CHAR('?');
    }
    REPR_END(')');
  }
  if (n->body)
    CHAR(' '), repr(RARGS, (node_t*)n->body);
}


static void repr_funtype(RPARAMS, const funtype_t* n) {
  PRINT(" (");
  for (u32 i = 0; i < n->params.len; i++) {
    if (i) CHAR(' ');
    repr(RARGS, n->params.v[i]);
  }
  CHAR(')');
  repr_type(RARGS, n->result);
}


static void repr_call(RPARAMS, const call_t* n) {
  CHAR(' ');
  repr(RARGSFL(fl | REPRFLAG_SHORT), (const node_t*)n->recv);
  if (n->args.len == 0)
    return;
  CHAR(' ');
  for (usize i = 0; i < n->args.len; i++) {
    if (i) CHAR(' ');
    repr(RARGS, (const node_t*)n->args.v[i]);
  }
}


static void repr_nodearray(RPARAMS, const ptrarray_t* nodes) {
  for (usize i = 0; i < nodes->len; i++) {
    CHAR(' ');
    repr(RARGS, nodes->v[i]);
  }
}


static void repr_type(RPARAMS, const type_t* t) {
  REPR_BEGIN('<', nodekind_name(t->kind));
  bool isnew = !seen(r, t);
  switch (t->kind) {
  case TYPE_INT:
  case TYPE_I8:
  case TYPE_I16:
  case TYPE_I32:
  case TYPE_I64:
    PRINT(t->isunsigned ? " u" : " s");
    break;
  case TYPE_STRUCT:
    repr_struct(RARGS, (const structtype_t*)t, isnew);
    break;
  case TYPE_FUN:
    if (isnew) {
      repr_funtype(RARGS, (const funtype_t*)t);
    } else {
      CHAR('\'');
    }
    break;
  case TYPE_ARRAY:
  case TYPE_ENUM:
  case TYPE_PTR:
    dlog("TODO subtype %s", nodekind_name(t->kind));
    break;
  }
  REPR_END('>');
}


static void repr(RPARAMS, const node_t* n) {
  const char* kindname = STRTAB_GET(nodekind_strtab, n->kind);
  REPR_BEGIN('(', kindname);

  if (node_isexpr(n) || n->kind == NODE_FIELD) {
    expr_t* expr = (expr_t*)n;
    CHAR(' ');
    if (expr->type) {
      repr_type(RARGSFL(fl | REPRFLAG_HEAD), expr->type);
    } else {
      PRINT("<?>");
    }
  }

  if (seen(r, n))
    goto end;

  switch (n->kind) {

  case NODE_UNIT:    repr_nodearray(RARGS, &((unit_t*)n)->children); break;
  case STMT_TYPEDEF: repr_typedef(RARGS, (typedef_t*)n); break;
  case EXPR_FUN:     repr_fun(RARGS, (fun_t*)n); break;
  case EXPR_BLOCK:   repr_nodearray(RARGS, &((block_t*)n)->children); break;
  case EXPR_CALL:    repr_call(RARGS, (call_t*)n); break;

  case EXPR_BOOLLIT:
    CHAR(' '), PRINT(((const boollit_t*)n)->val ? "true" : "false");
    break;

  case EXPR_INTLIT: {
    u64 u = ((intlit_t*)n)->intval;
    CHAR(' ');
    if (!((intlit_t*)n)->type->isunsigned && (u & 0x1000000000000000)) {
      u &= ~0x1000000000000000;
      CHAR('-');
    }
    PRINT("0x");
    buf_print_u64(&r->outbuf, u, 16);
    break;
  }

  case EXPR_FLOATLIT:
    if (((const floatlit_t*)n)->type == type_f64) {
      PRINTF(" %f", ((const floatlit_t*)n)->f64val);
    } else {
      PRINTF(" %f", ((const floatlit_t*)n)->f32val);
    }
    break;

  case EXPR_MEMBER:
    CHAR(' '), PRINT(((const member_t*)n)->name);
    CHAR(' '), repr(RARGS, (const node_t*)((const member_t*)n)->recv);
    break;

  case EXPR_ID:
    CHAR(' '), PRINT(((idexpr_t*)n)->name);
    if (((idexpr_t*)n)->ref) {
      CHAR(' ');
      repr(RARGSFL(fl | REPRFLAG_HEAD), (const node_t*)((idexpr_t*)n)->ref);
    }
    break;

  case EXPR_PREFIXOP:
  case EXPR_POSTFIXOP: {
    unaryop_t* op = (unaryop_t*)n;
    CHAR(' '), PRINT(tok_repr(op->op));
    CHAR(' '), repr(RARGS, (node_t*)op->expr);
    break;
  }

  case EXPR_BINOP: {
    binop_t* op = (binop_t*)n;
    CHAR(' '), PRINT(tok_repr(op->op));
    CHAR(' '), repr(RARGS, (node_t*)op->left);
    CHAR(' '), repr(RARGS, (node_t*)op->right);
    break;
  }

  case NODE_FIELD:
  case EXPR_PARAM:
  case EXPR_LET:
  case EXPR_VAR:
    CHAR(' '); PRINT(((const local_t*)n)->name);
    if (((const local_t*)n)->init) {
      CHAR(' ');
      repr(RARGS, (const node_t*)((const local_t*)n)->init);
    }
    break;

  }

end:
  REPR_END(')');
}


err_t node_repr(buf_t* buf, const node_t* n) {
  repr_t r = {
    .outbuf = *buf,
  };
  if (!map_init(&r.seen, buf->ma, 64))
    return ErrNoMem;
  repr(&r, 0, REPRFLAG_HEAD, n);
  *buf = r.outbuf;
  map_dispose(&r.seen, buf->ma);
  return r.err;
}
