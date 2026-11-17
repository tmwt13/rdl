#pragma once

#include "da.h"
#include "ht.h"
#include "parser.h"
#include "lexer.h"

#ifndef _RDL_H
#define _RDL_H

typedef struct Result Result; 

typedef int offset_t;
typedef enum {
   OpERROR,    // Abort program
   OpNULL,     // PUSH null
   OpNUM,      // PUSH num
   OpTXT,      // PUSH text
   OpIDENT,    // PUSH ident
   OpDUP,      // PUSH last element again
   OpCTXPOP,   // PUSH ctx and pop it
   OpCTX,      // PUSH ctx 
   OpPUSHIDX,  // PUSH value form idx
   OpPUSHREL,  // PUSH rel to bp
   OpPUSHA,
   OpGETVAR,   // PUSH var value
   OpGETLOCAL,   // PUSH var value

   OpPOP,      // POP last value
   OpCTXPUSH,  // POP and push to ctx
   OpPOPIDX,   // POP value to idx
   OpCONDJMP,  // POP and evtl. JMP
   OpPOPA,
   OpDOT,      // pop receiver, combine with the method/field name baked into this op (as.text) -> push fn, push receiver
   OpADD,
   OpSUB,
   OpMULT,
   OpDIV,
   OpEQ,
   OpLT,
   OpGT,
   OpELT,
   OpEGT,
   OpECHO,     // pop+echo top of stack, but only if something sits above the locals (as.idx = #slots owned by the compiler)
   OpSETVAR,   // pop name and register fn
   OpSETLOCAL,   // pop name and register fn

   OpDOLAR,    // POP idx PUSH value
   OpJMP,      // JMP to idx (pc)
   OpSWAP,     // swap the top two values
   OpNOT,

   OpFN,       // PUSH old bp + pc  (sets new call frame)
   OpCALL,     // 
   OpRET,      // pop to bp and push result
   OpCLOSURE,  // build a closure for the fn body at as.ptr; followed by one OpUPDESC per captured variable
} OPCODE;

typedef struct {
   OPCODE opcode;
   union {
      int idx;          // total idx of stack
      int offset;       // offset relative to bp
      offset_t ptr;     // Operation pointer
      double number;
      const char *text; // OpIDENT: identifier name; OpDOT: method/field name (stable temp-arena string)
   } as;
} Operation;

typedef struct{
   Operation *data;
   size_t count, capacity;
} OperationStack;

typedef struct Object Object;

typedef struct RdlVM RdlVM;
typedef void (*RdlCallback)(RdlVM *vm);

typedef struct Upvalue Upvalue;

#define FOBJ_USED (1u<<0)
#define FOBJ_FREE (1u<<1)

struct Object {
   uint8_t flags;
   enum {
      OBJ_INSTANCE, OBJ_METHOD, OBJ_TEXT, OBJ_LIST, OBJ_CLOSURE,
   } kind;
   char * class_name;

   union {
      struct {
         char * cc;
      } string;

      struct{
         void *		   data;
      } instance;

      struct{
         char *	method_name;
      } method;

      struct{
         Result *data;
         u8 count, capacity;
      } list;

      struct{
         offset_t start;      // first operation of the fn body
         RdlCallback native;  // C closure: called instead of running a body (state lives in upvalues)
         Upvalue **upvalues;
         int count;
      } closure;
   } as;

   Object *next;
};

typedef enum {
	RDL_ERROR = 0,
	RDL_RAW,     // fn ist ein vollständiger RdlCallback: fn(vm) wird 1:1 aufgerufen
	RDL_STACK,     
	RDL_V_V,
	RDL_VM_V,
	RDL_D_V,     // fn ist double(*)(double):          slot[0] = fn(slot[arg_base])
	RDL_D_D,     // fn ist double(*)(double):          slot[0] = fn(slot[arg_base])
	RDL_DD_D,    // fn ist double(*)(double,double):   slot[0] = fn(slot[arg_base], slot[arg_base+1])
	RDL_DDD_D,
	RDL_I_S,
	RDL_I_D,
} RdlNativeKind;

typedef struct {
   const char   *class_name;
   const char   *signature;   // "sqrt(0)", "add(2)", "name" ...
   RdlNativeKind kind;
   int           arg_base;    // 0 = Receiver ist der Operand (Methodenaufruf), 1 = erstes explizites Arg (freier Aufruf)
   union {
      void *fn;
      offset_t offset;
   } as;
} RdlNativeEntry;

struct Result {
	enum {
		RESULT_ERROR, RESULT_NUMBER, RESULT_NULL,
      RESULT_FUNCTION,
		RESULT_VOID,
		RESULT_INT,
      RESULT_CHAR,
      RESULT_OBJECT,
	} kind;
	union {
      int 		      error_code;
      RdlNativeEntry *entry;
      double 		   number;
      char           chr;
      int		      integer;
      Object*        object;
	};
};

// A captured variable (Lua: upvalue). While the owning frame is alive it is
// "open" and refers to a stack slot by *index* (vm->data is realloc'd, so
// never by pointer). When that frame returns it is "closed": the value is
// copied into the upvalue itself and all closures keep sharing it.
struct Upvalue {
   bool open;
   int slot;            // open: absolute index into vm->data
   Result closed;       // closed: the value lives here
   int refcount;        // number of closures holding this upvalue
   Upvalue *next_open;  // vm->open_upvalues list (sorted by slot, descending)
};


typedef Ht(char *, RdlNativeEntry) RdlBindings;

typedef struct VariableItem VariableItem;

struct VariableItem{
   char *name;
   int slot; // index into vm->data, NOT a raw pointer - vm->data is
             // realloc'd by push(), so a cached Result* would dangle
             // after the very next push that grows the array.
};

// Frame layout below bp: [pc, lb, ab, cl bp] saved by OpFN, then fn, ctx, args...
#define CALL_PROLOG_SIZE     5

#define MAX_OBJECTS     20
#define MAX_VARIABLES   20

struct RdlVM{
   // Stack / Slots
	Result *data;
	union {
		int count;
		int sp; // SlotPointer
	};
	int capacity;

   // function scoping:
   int bp;                 // base pointer
   int pc;                 // program counter
   int ab;                 // args base
   int lb;                 // locals base
   Result cl;              // running closure (NULL for plain fns), source of OpGETUP/OpSETUP
   Upvalue *open_upvalues; // upvalues
   
   // Registers
   Result register_a;
   //       Context
	struct{
		Result *data;
		int count, capacity;
	} context;

   // Function Bindings
	RdlBindings bindings;

   // Variables
   VariableItem variables[MAX_VARIABLES];
   int variables_count;

   // Garbage Collection
   Object *objects;
   int objects_count;

   Object *freelist;

	// Standard Functions
	void (*echo_fn)(Result);
	void (*print_fn)(Result);

	// Debug: JIT-Modus - statementweise kompilieren+ausfuehren statt den
	// kompletten OperationStack fuer die Quelle zu bauen und zu printen.
	bool debug_jit;
};

#endif // _AST_DEF

// API
void        rdl_init(RdlVM *vm);
void        rdl_reset(RdlVM *vm);
void        rdl_free(RdlVM *vm); // TODO
void        rdl_eval(RdlVM *vm);
void        rdl_compile(RdlVM *vm, StringView src);
void        rdl_register_method(RdlVM *vm, char *class_name, 
               char *signature, RdlNativeKind kind, int arg_base, void *fn);
void        rdl_bind_natives(RdlVM *vm);

Result      rdl_single_eval(RdlVM *vm, StringView *src);

void        rdl_push(RdlVM *vm, Result res);
void        rdl_push_string(RdlVM *vm, char *str);
void        rdl_push_double(RdlVM *vm, double value);
void        rdl_push_bool(RdlVM *vm, bool value);
void        rdl_push_null(RdlVM *vm);
void *      rdl_push_new_foreign(RdlVM *vm, char *class_name, int size);

Result      rdl_pop(RdlVM *vm);
char *rdl_pop_string(RdlVM *vm);
double      rdl_pop_double(RdlVM *vm);
bool        rdl_pop_bool(RdlVM *vm);
int         rdl_type(RdlVM *vm);
void *      rdl_pop_foreign(RdlVM *vm);

// --- Closures ---
// C closures (like lua_pushcclosure): `fn` gets `nupvalues` private, persistent
// slots. Initialise them from the creating native with rdl_slot_set_upvalue_*,
// read/write them inside `fn` with rdl_upvalue_*.
/*
 *    Push Closure:
 *       rdl_push_double(vm, double);     <- becomes upvalue
 *       rdl_push_double(vm, double);     <- becomes upvalue
 *       rdl_push_fn(vm, callback, 2); 
 */
void        rdl_push_fn(RdlVM *vm, RdlCallback fn, int nupvalues);

Result      rdl_upvalue_get(RdlVM *vm, int idx);
void        rdl_upvalue_set(RdlVM *vm, int idx, Result value);

double      rdl_upvalue_get_double(RdlVM *vm, int idx);
void *      rdl_upvalue_get_foreign(RdlVM *vm, int idx);
void        rdl_upvalue_set_double(RdlVM *vm, int idx, double value);
void        rdl_upvalue_set_null(RdlVM *vm, int idx);

// Helper
#define SLOT_GET(vm, slot) 	(*((vm)->data + ((vm)->bp) + (slot)))
#define ENTRY_KEY(class_name, signature) (stf_temp_sprintf("%s.%s", class_name, signature))
