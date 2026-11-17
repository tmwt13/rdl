
#define HT_PUBDEF static
#include "rdl.h"
#include <math.h>
#include "da.h"
#define HT_IMPLEMENTATION
#include "ht.h"

// bindings:
void g_range(RdlVM *vm);

void foreign_add(RdlVM *vm);

// ------
// IMPLEMENTATION
// ------

extern void result_print(Result res);

double math_add(double a, double b){ return a+b;}

void g_newline(RdlVM *vm) { 
	Result res = {.kind = RESULT_CHAR, .chr = '\n'};
	vm->echo_fn(res); 
}

void g_tab(RdlVM *vm) { 
   double a = 1;
   if(vm->sp - vm->bp > 1) {
	   a = rdl_pop_double(vm);
   }
	Result res = {.kind = RESULT_CHAR, .chr = '\t'};
	for(int i=0; i < (int)a; ++i){
		vm->echo_fn(res); 
	}
	rdl_push_null(vm);
}

double number_is_between(double num, double a, double b){
	return num >= a && num <= b;
}

void number_fmt(RdlVM *vm){
	const char *fmt = rdl_pop_string(vm);   // 1
	double num = rdl_pop_double(vm);        // 0

	rdl_push_string(vm, stf_temp_sprintf(fmt, (int)num));
}

void foreign_add(RdlVM *vm) {
	double b = rdl_pop_double(vm); // 1
	double a = rdl_pop_double(vm); // 0
	rdl_push_double(vm, a + b);
}
void foreign_math_add(RdlVM *vm) {
	double b = rdl_pop_double(vm);
	double a = rdl_pop_double(vm);
	rdl_push_double(vm, a + b);
}

// range(n): returns an iterator closure over 1..n, driven by `loop`.
// Its state is two upvalues: 0 = current, 1 = max.
enum { RANGE_CUR, RANGE_MAX, RANGE_UPVALUES };

static void range_step(RdlVM *vm){
	double cur = rdl_upvalue_get_double(vm, RANGE_CUR) + 1;
	if(cur > rdl_upvalue_get_double(vm, RANGE_MAX)){
		rdl_push_null(vm);
		return;
	}
	rdl_upvalue_set_double(vm, RANGE_CUR, cur);
	rdl_push_double(vm, cur);
}

void g_range(RdlVM *vm){
	double max = rdl_pop_double(vm);
	rdl_push_double(vm, 0);              // CUR = 0
	rdl_push_double(vm, max);            // MAX = 1
	rdl_push_fn(vm, range_step, 2); // #2 upvalues
}

void g_log(RdlVM *vm){
   if(vm->sp - vm->bp<2) INFO("LOG: EMPTY MSG");    
   Result res = rdl_pop(vm);
   INFO("LOG:"); result_print(res); printf("\n----\n");
}

static const RdlNativeEntry g_natives[] = {
	// Handgeschriebene Callbacks bleiben RDL_RAW, unverändert:
	{"Global", "range", RDL_RAW, 0, g_range},
	{"Number", "range", RDL_RAW, -1, g_range},
	// Stdlib direkt aus libm, ohne eigene C-Funktion:
	{"Number",  "sqrt",  RDL_D_D,  0, sqrt},   // 2.sqrt() == 2.sqrt
	{"math",     "sqrt",  RDL_D_D,  1, sqrt},   // math.sqrt(3)
	{"Number",  "floor", RDL_D_D,  0, floor},
	{"math",     "floor", RDL_D_D,  1, floor},
	{"Number",  "pow",   RDL_DD_D, 0, pow},    // 2.pow(2)
	{"math",     "pow",   RDL_DD_D, 1, pow},    // std.pow(2,3)
	{"Number", "add", RDL_RAW, 0, foreign_add},
	{"Number", "between", RDL_DDD_D, 0, number_is_between},
	{"Number", "fmt", RDL_RAW, 0, number_fmt},
	{"math", "add", RDL_DD_D, 1, math_add},
	{"Global", "add", RDL_DD_D, 1, math_add}, // freistehend: add(1,2)
	{"Global", "nl", RDL_VM_V, 0, g_newline},
	{"Global", "tab", RDL_RAW, 0, g_tab},
	{"Global", "LOG", RDL_RAW, 0, g_log},

	{0},
};

void rdl_bind_natives(RdlVM *vm){
	int count = sizeof(g_natives)/sizeof(g_natives[0]) - 1; // -1: sentinel {0} am Ende
	for(int i = 0; i<count; ++i){
		RdlNativeEntry entry = g_natives[i];
		*ht_put(&vm->bindings, ENTRY_KEY(entry.class_name, entry.signature)) = entry;
	}

	RdlNativeEntry *entry = ht_find(&vm->bindings,ENTRY_KEY("math","add")); 
	if(!entry) ERROR("could not init");
}
