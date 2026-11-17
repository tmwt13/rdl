#define HT_PUBDEF static

#include "rdl.h"

#include <math.h>
#include "da.h"

#define HT_IMPLEMENTATION
#include "ht.h"

#define MAX_OPERATION_STACK 2000
static Operation _operations[MAX_OPERATION_STACK];
static offset_t operation_ptr = 1000;
static offset_t fn_operation_ptr = 0;
static OperationStack stack = {
   .data = _operations,
   .count = 0, .capacity = MAX_OPERATION_STACK,
};

Object _global_object = {
   .kind = OBJ_INSTANCE,
   .class_name = "Global",
   .as.instance.data = NULL,
};

Result global_object = {
   .kind = RESULT_OBJECT,
   .object = &_global_object,
};

extern void     compile_reset(void);
extern bool     compile(RdlVM *vm, Lexer *L, OperationStack *stack);
extern bool     stack_eval(RdlVM *vm, const OperationStack stack);
extern void     stack_print(const OperationStack stack);

extern Result   buildinstance(RdlVM *vm, const char *class_name, int size);
extern Result   buildmethod(RdlVM *vm, const char *class_name, const char *method_name);
extern Result   buildstring(RdlVM *vm, const char *cc);
extern Result   buildclosure(RdlVM *vm, offset_t start, int count);
extern Result   *upvalue_ref(RdlVM *vm, int idx);

//POP(vm)
#define POP(vm)         stf_da_pop((vm))
#define PUSH(vm, val)   stf_da_append((vm), (val))

// API Functions
void rdl_eval(RdlVM *vm){
	stack_eval(vm, stack);
}

void rdl_compile(RdlVM *vm, StringView src)
{
	compile_reset();
	Lexer lexer = new_lexer(&src);
	int start_depth = vm->count;
	while (compile(vm, &lexer, &stack)){}
   stack_print(stack);
}

void rdl_push(RdlVM *vm, Result res)
{
   PUSH(vm, res);
}

Result rdl_pop(RdlVM *vm)
{
   return POP(vm);
}

// A C closure owns `nupvalues` closed upvalues - the same mechanism as an rdl
// closure, only that nobody captured them from a stack frame: they are born
// closed and their values are the fn's persistent state.
void rdl_push_fn(RdlVM *vm, RdlCallback fn, int nupvalues){
	Result value = {0};
   Result closure = buildclosure(vm, -1, nupvalues);
	closure.object->as.closure.native = fn;
	//SLOT_GET(vm, slot) = closure; // rooted before anything else can collect

	for(int i = nupvalues-1; i >= 0; --i){
		Upvalue *uv = calloc(1, sizeof(Upvalue));
		uv->open = false;
		uv->refcount = 1;
		uv->closed = POP(vm); // pop last value as Upvalue
		closure.object->as.closure.upvalues[i] = uv;
	}

   PUSH(vm, closure);
}

void *rdl_upvalue_get_foreign(RdlVM *vm, int idx){
	Result value = *upvalue_ref(vm, idx);
	if(value.kind != RESULT_OBJECT || value.object->kind != OBJ_INSTANCE) ERROR("upvalue %d is not an instance", idx);
	return value.object->as.instance.data;
}

double rdl_upvalue_get_double(RdlVM *vm, int idx){
	Result value = *upvalue_ref(vm, idx);
	if(value.kind != RESULT_NUMBER) ERROR("upvalue %d is not a number", idx);
	return value.number;
}

void rdl_upvalue_set_double(RdlVM *vm, int idx, double value){
	*upvalue_ref(vm, idx) = (Result){.kind = RESULT_NUMBER, .number = value};
}

void rdl_upvalue_set_null(RdlVM *vm, int idx){
	*upvalue_ref(vm, idx) = (Result){.kind = RESULT_NULL};
}

void *rdl_pop_foreign(RdlVM *vm){
	Result value;
   
   value = POP(vm);
	if (value.kind != RESULT_OBJECT) ERROR("VALUE ERROR");
	return value.object->as.instance.data; // TODO: might get cleared if gc between retriving and working with data
}

void *rdl_push_new_foreign(RdlVM *vm, char *class_name, int size){
   Result result;

   result = buildinstance(vm, class_name, size);
	PUSH(vm, result);
	return result.object->as.instance.data;
}

void rdl_init(RdlVM *vm){
	init_keywords();

   // VM Init
	*vm = (RdlVM){0};

   vm->pc = 0;

	vm->bindings = (RdlBindings){
		.hasheq = ht_cstr_hasheq,
	};

	Result base_ctx = global_object;
	stf_da_append(&vm->context, base_ctx);

   // -- Init GC
	
   vm->objects_count = MAX_OBJECTS;
   vm->objects = malloc(sizeof(Object) * vm->objects_count);

   vm->freelist = NULL;
   for(int i=0;i<vm->objects_count;++i){
         vm->objects[i].next = vm->freelist;
         vm->freelist = vm->objects+i;
         vm->freelist->flags |= FOBJ_FREE;
   }

	// Bind Functions:
	rdl_bind_natives(vm);
}

void rdl_push_string(RdlVM *vm, char *str) {
	PUSH(vm,buildstring(vm, str));
}

char *rdl_pop_string(RdlVM *vm) {
	Result value;

   value = POP(vm);
	if (value.kind != RESULT_OBJECT && value.object->kind != OBJ_TEXT)
		ERROR("VALUE ERROR");
	return stf_temp_strdup(value.object->as.string.cc);
}

void rdl_push_double(RdlVM *vm, double value) {
	PUSH(vm, ((Result){
		.kind = RESULT_NUMBER,
		.number = value,
	}));
}

double rdl_pop_double(RdlVM *vm) {
   Result value;

   value = POP(vm);
	if (value.kind != RESULT_NUMBER)
		ERROR("VALUE ERROR, got %d", value.kind);
	return value.number;
}

int rdl_type(RdlVM *vm){
	return SLOT_GET(vm, vm->sp-1).kind;
}

void rdl_push_null(RdlVM *vm){
	PUSH(vm, ((Result){
      .kind = RESULT_NULL,
      .number = 0,
	}));
}

void register_fn(RdlVM *vm, const char *name, offset_t offset){
	RdlNativeEntry entry = {
		.class_name = "Global",
		.signature = name,
		.kind = RDL_STACK,
		.arg_base = 0,
		.as.offset = offset,
	};

	const char *key = ENTRY_KEY("Global", name);
	*ht_find_or_put(&vm->bindings, key) = entry;
}

void rdl_register_method(
	RdlVM *vm, 
	char *class_name, char *signature, 
	RdlNativeKind kind, int arg_base, void *fn)
{
	RdlNativeEntry entry = {
		.class_name = class_name,
		.signature = signature,
		.kind = kind,
		.arg_base = arg_base,
		.as.fn = fn
	};

	char *key = ENTRY_KEY(class_name, signature);
	*ht_find_or_put(&vm->bindings, key) = entry;
}

extern void free_object_payload(Object *obj);

// resets the current vm State
void rdl_reset(RdlVM *vm){
   //

   // clear UpValues
   Upvalue *upv, *nupv;
   nupv = vm->open_upvalues;
   vm->open_upvalues = NULL;
   while(nupv){
      upv = nupv;
      nupv = upv->next_open;
      free(upv);
   }
   // clear Variables 
   for(int i = vm->variables_count-1; i>=0; --i){
      free(vm->variables[i].name);
      vm->variables[i].name = NULL;
   }

   // clear Objects 
   Object *obj;
   int count = 0;
   for(int i=0;i<vm->objects_count;++i){
      obj = vm->objects+i;

      ++count;
      free_object_payload(obj);
      *obj = (Object){0};
      obj->flags |= FOBJ_FREE;
      obj->next = vm->freelist;
      vm->freelist = obj;
   }
   // clear context
   vm->context.count = 0;
   // clear Stack
   vm->bp = 0;
   vm->ab = 0;
   vm->sp = 0;
   vm->pc = 1000;

}

extern offset_t    operation(Operation op, OperationStack *stack);
extern bool        expr_bp(RdlVM *vm, Lexer *L, int min_bp, OperationStack *stack);

Result rdl_single_eval(RdlVM *vm, StringView *src){
   Lexer L = new_lexer(src);
   Operation ops[20] = {0};
   OperationStack stack = {
      .data = ops,
      .count = 0, .capacity = 20,
   };
   offset_t saved_sp, saved_bp;

   vm->pc = stack.count;
   saved_sp = vm->sp; saved_bp = vm->bp;

   INFO("Stack size = %zu", stack.count);
   expr_bp(vm, &L, 0, &stack);
   operation((Operation){.opcode = OpRET}, &stack);
   operation((Operation){.opcode = OpERROR}, &stack); // SAFETY
                                                      
   if(L.next.location.data != NULL)
      src->count += src->data - L.next.location.data;
   else src->count = 0;
   src->data = L.next.location.data;

   stack_print(stack);
   if(stack_eval(vm, stack)){}
   
   Result res = {.kind = RESULT_VOID};
   if(vm->sp)
      res = stf_da_pop(vm);

   vm->sp = saved_sp; vm->bp = saved_bp;
   return res;
}
