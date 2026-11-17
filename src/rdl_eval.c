#define HT_PUBDEF static

#include "rdl.h"
#include "da.h"
#define HT_IMPLEMENTATION
#include "ht.h"

#ifndef ALLOC
#define ALLOC malloc
#endif

// Printing
const char  *result_type(Result result);
void        result_print(Result res);

// Garbage Collection ----
Object *    new_object(RdlVM *vm);
bool        instance_cmp(const Object lhs, const Object rhs);
void        collectgarbage(RdlVM *vm);
void        free_object_payload(Object *obj);

Result      buildinstance(RdlVM *vm, const char *class_name, int size);
Result      buildmethod(RdlVM *vm, const char *class_name, const char *method_name);
Result      buildfunction(RdlVM *vm, const char *class_name, const char *method_name);
Result      buildstring(RdlVM *vm, const char *cc);
// Evaluating
bool        operation_eval(RdlVM *vm, Operation op);
bool        stack_eval(RdlVM *vm, const OperationStack stack);

void              _invoke_native(RdlVM *vm, const RdlNativeEntry *e);
RdlNativeEntry   *_get_entry(RdlVM *vm, const char *class_name, const char *signature);

void           register_variable(RdlVM *vm, const char *name);
void           retrive_variable(RdlVM *vm, const char *name);
void           reset_variables(RdlVM *vm, int idx);

extern void    register_fn(RdlVM *vm, const char *name, offset_t offset);

// Helper
#define push(vm, res)   stf_da_append((vm), (res))
#define pop(vm)         stf_da_pop(vm)



/* ------------------------- *
 *  Implementation
 * ------------------------- */ 
// the running operation stream; OpCLOSURE reads its OpUPDESC operands from it
static const Operation *g_ops;

bool stack_eval(RdlVM *vm, const OperationStack stack){
   g_ops = stack.data;
   while(operation_eval(vm, stack.data[vm->pc]));

   return true;
}

// ---- Upvalues (see Upvalue in rdl.h) ----
static Upvalue *capture_upvalue(RdlVM *vm, int slot){
   Upvalue *prev = NULL, *uv = vm->open_upvalues;
   while(uv && uv->slot > slot){
      prev = uv;
      uv = uv->next_open;
   }
   if(uv && uv->slot == slot) return uv; // already captured: share it

   Upvalue *created = calloc(1, sizeof(Upvalue));
   created->open = true;
   created->slot = slot;
   created->next_open = uv;
   if(prev) prev->next_open = created;
   else vm->open_upvalues = created;
   return created;
}

// the slots >= from are about to disappear: move their values into the upvalues
static void close_upvalues(RdlVM *vm, int from){
   Upvalue *uv;
   // for every upvalue after the slot until to be cleared
   while(vm->open_upvalues && vm->open_upvalues->slot >= from){
      // close upvalue:
      //    get uv (linked list)
      uv = vm->open_upvalues;
      vm->open_upvalues = uv->next_open;
      //    set closed
      uv->open = false;
      //    store state localy
      uv->closed = vm->data[uv->slot];
      //    detach
      uv->next_open = NULL;
      if(uv->refcount == 0) free(uv); // no closure left that could see it
   }
}

static void release_upvalue(Upvalue *uv){
   // an open upvalue stays alive until close_upvalues() unlinks it
   if(--uv->refcount <= 0 && !uv->open) free(uv);
}

extern Result *upvalue_ref(RdlVM *vm, int idx);
Result *upvalue_ref(RdlVM *vm, int idx){
   if(vm->cl.kind != RESULT_OBJECT || vm->cl.object->kind != OBJ_CLOSURE)
      ERROR("upvalue access outside of a closure");
   Object *closure = vm->cl.object;
   if(idx < 0 || idx >= closure->as.closure.count) ERROR("bad upvalue %d", idx);
   Upvalue *uv = closure->as.closure.upvalues[idx];
   return uv->open ? &vm->data[uv->slot] : &uv->closed;
}

Result buildclosure(RdlVM *vm, offset_t start, int up_count){
   Object *obj = new_object(vm);
   obj->kind = OBJ_CLOSURE;
   obj->class_name = "Closure";
   obj->as.closure.start = start;
   obj->as.closure.native = NULL;
   obj->as.closure.count = up_count;
   obj->as.closure.upvalues = up_count ? calloc(up_count, sizeof(Upvalue *)) : NULL;
   return (Result){.kind = RESULT_OBJECT, .object = obj};
}

#define STORE(vm, _val) push(vm, ((Result){.kind = RESULT_INT, .integer = _val}))
void save_state(RdlVM *vm){
   STORE(vm, vm->pc + 1);
   push(vm, vm->cl);
   STORE(vm, vm->lb);
   STORE(vm, vm->ab);
   STORE(vm, vm->bp);
   vm->ab = vm->sp;
}

#define RESTORE(vm, _val) ((_val) = pop(vm).integer)
void restore_state(RdlVM *vm){
   // reset sp
   vm->sp = vm->bp;
   RESTORE(vm, vm->bp);
   RESTORE(vm, vm->ab);
   RESTORE(vm, vm->lb);
   vm->cl = pop(vm);
   RESTORE(vm, vm->pc);
}

#define REQUIRES_ARGS(n)
#define PUSHES_VALUES(n)

#define BINOP(vm, _op) \
   REQUIRES_ARGS(2); \
   PUSHES_VALUES(1); \
   rhs = pop(vm); \
   lhs = pop(vm); \
   lhs = (Result){.kind = RESULT_NUMBER, .number = (lhs.number) _op (rhs.number)}; \
   push(vm,lhs)

bool operation_eval(RdlVM *vm, Operation op){
   Result lhs, rhs, temp;
   RdlNativeEntry *entry = {0};

	switch (op.opcode) {
      case OpERROR:
         ERROR("EVAL_ERROR at vm.pc = %d", vm->pc);
      case OpFN: {
         save_state(vm);
      } break;
      case OpCALL: {
         // CALL Stack:
         // -9: ab
         // -8: fn
         // -7: obj
         // -6: arg1     -> vm->bp - 2 - 1 - argc + 1
         // -4: arg2
         // -3: NULL
         // -2: PC
         // -1: BP
         // 0: ->SP
         REQUIRES_ARGS(1);
         PUSHES_VALUES(2);

         // get bp slot
         rhs = SLOT_GET(vm, vm->ab-1);
         rhs.integer = vm->bp;
         vm->bp = vm->ab;
         vm->lb = vm->sp;

         // get fn
         lhs = SLOT_GET(vm, 0); 

         if(lhs.kind == RESULT_OBJECT && lhs.object->kind == OBJ_CLOSURE
            && lhs.object->as.closure.native){
            // C closure: frame is [fn, ctx, args...] like a native. slot0 is
            // the result; the closure is the running one so rdl_upvalue_*
            // can reach its state.
            int bp = vm->bp;
            vm->bp += 1;
            vm->cl = lhs;
            SLOT_GET(vm, 0) = (Result){.kind = RESULT_NULL};
            lhs.object->as.closure.native(vm);
            temp = pop(vm);
            vm->bp = bp;
            restore_state(vm);
            push(vm, temp);
            return true;
         }

         if(lhs.kind == RESULT_OBJECT && lhs.object->kind == OBJ_CLOSURE){
            temp = (Result){.kind = RESULT_NULL}; // default return value
            push(vm, temp);
            vm->cl = lhs;
            vm->pc = lhs.object->as.closure.start;
            return true;
         }


         if(lhs.kind != RESULT_FUNCTION){
            // calling a plain value is a no-op: it just collapses to itself.
            // this is what lets a bare identifier double as "variable"
            // (OpIDENT already resolved it to a Result) without the
            // compiler ever needing to decide up front whether it's a
            // variable or a function.
            restore_state(vm);
            push(vm, lhs);
            return true;
         }

         entry = lhs.entry;

         if(entry->kind != RDL_STACK){
            _invoke_native(vm, entry);
            // save result;
            temp = pop(vm);
            // restore base;
            restore_state(vm);
            // push result;
            push(vm, temp);                

            return true;
         }

         // RDL_STACK: jump into the function body. Give OpRET a default
         // NULL to pop even if the body never explicitly pushes a result
         // (native calls don't need this: _invoke_native always pushes
         // its own result, and giving every call a phantom extra slot
         // broke natives like g_tab/g_log that infer arg count from
         // vm->sp - vm->bp).
         temp = (Result){.kind = RESULT_NULL};
         push(vm, temp);

         vm->cl = (Result){.kind = RESULT_NULL};
         vm->pc = (offset_t)entry->as.offset;

         return true;
      }
      case OpSETVAR:
      {
         lhs = pop(vm);
         if(lhs.kind != RESULT_NUMBER) ERROR("expected fn start idx");
         register_fn(vm, op.as.text, (int)lhs.number);
      } break;
      case OpGETVAR:
      {
         RdlNativeEntry *entry = {0};

         entry = _get_entry(
               vm,
               "Global",
               op.as.text);

         if(entry == NULL) ERROR("could not find function %s", op.as.text);
         lhs = (Result){.kind = RESULT_FUNCTION, .entry = entry};
         push(vm, lhs);
      } break;
      case OpRET:
         // CALL Stack:
         // 0: BP
         // 1: fn
         // 2: arg1     -> vm->bp - 2 - 1 - argc + 1
         // 3: arg2
         // 4: argc = 3
         // 5: PC
         // 6: BP
         // 7: ->SP
         if(vm->bp == 0) {
            INFO("bp = 0");
            return false;
         }
         REQUIRES_ARGS(1);
         PUSHES_VALUES(1);
         // save result
         if(vm->sp - vm->bp == 0) {
            lhs = (Result){.kind = RESULT_NULL};
         } else {
            lhs = pop(vm);
         }
         // locals of this frame die now: closures keep what they captured
         close_upvalues(vm, vm->bp);
         // restore state
         restore_state(vm);
         // push result
         push(vm, lhs);
         return true;
      case OpDOLAR:
         REQUIRES_ARGS(1);
         PUSHES_VALUES(1);
         lhs = pop(vm);

         if(lhs.kind != RESULT_NUMBER) ERROR("expected Number for dolar");
         int idx = (int)lhs.number;

         if(idx > vm->sp-vm->bp) 
            ERROR("to few args (argc = %d < %d)", vm->sp-vm->bp-2, idx);

         push(vm, vm->data[vm->bp+idx+1]); // fn, obj, args...
         break;
      case OpECHO:
         if(vm->sp <= vm->bp + op.as.idx) break; // statement left no value
         lhs = pop(vm);
         if(lhs.kind == RESULT_NULL || lhs.kind == RESULT_VOID) break;
         vm->echo_fn(lhs);
         break;
      case OpADD:    BINOP(vm, +); break;
      case OpSUB:    BINOP(vm, -); break;
      case OpMULT:   BINOP(vm, *); break;
      case OpDIV:    BINOP(vm, /); break;
      case OpEQ:              
         rhs = pop(vm);
         lhs = pop(vm);
         if(rhs.kind != lhs.kind)
            lhs = (Result){.kind = RESULT_NUMBER, .number = 0};
         else if(rhs.kind == RESULT_NUMBER)
            lhs = (Result){.kind = RESULT_NUMBER, .number = (lhs.number) == (rhs.number)};
         else if(rhs.kind == RESULT_NULL)
            lhs = (Result){.kind = RESULT_NUMBER, .number = lhs.kind == RESULT_NULL ? 1:0};
         push(vm,lhs);
         break;
      case OpLT:     BINOP(vm, <); break;
      case OpGT:     BINOP(vm, >); break;
      case OpELT:    BINOP(vm, <=); break;
      case OpEGT:    BINOP(vm, >=); break;
      case OpNOT:    
         lhs = pop(vm);
         if(lhs.kind != RESULT_NUMBER) ERROR("cannot ! on non-number");
         lhs.number = lhs.number == 0 ? 1: 0;
         push(vm, lhs);
         break;
      case OpNUM:
         PUSHES_VALUES(1);
         lhs = (Result){.kind = RESULT_NUMBER, .number = op.as.number};
         push(vm,lhs);
         break;
      case OpIDENT:
         // resolve a bare identifier: local variable (push its value
         // directly), else a deferred Global.<name> reference that the
         // surrounding call-convention will either invoke or collapse.
         PUSHES_VALUES(1);
         {
            bool found = false;
            for(int i = vm->variables_count-1; i>=0; --i){
               if(!strcmp(vm->variables[i].name, op.as.text)){
                  push(vm, vm->data[vm->variables[i].slot]);
                  found = true;
                  break;
               }
            }
            if(!found){
               lhs = buildmethod(vm, "Global", op.as.text);
               push(vm, lhs);
            }
         }
         break;
      case OpTXT:
         PUSHES_VALUES(1);
         lhs = buildstring(vm, op.as.text);
         push(vm, lhs);
         break;
      case OpCTXPUSH:
         PUSHES_VALUES(1);
         stf_da_append(&vm->context, pop(vm));
         break;
      case OpCTXPOP:
         stf_da_pop(&vm->context);
         break;
      case OpPOPA:
         vm->register_a = stf_da_pop(vm);
         break;
      case OpPOP:
         stf_da_pop(vm);
         break;
      case OpPUSHA:
          stf_da_append(vm,vm->register_a);
         break;
      case OpSWAP:
         REQUIRES_ARGS(2);
         PUSHES_VALUES(2);
         rhs = pop(vm);
         lhs = pop(vm);
         push(vm, rhs);
         push(vm, lhs);
         break;
      case OpDOT:
         // pop the receiver; the method/field name is baked into this
         // operation (op.as.text), not resolved via OpIDENT - "add" in
         // (5).add(2) must not be looked up as a global.
         // pop lhs 
         // get entry
         // push fn entry
         // push lhs
         REQUIRES_ARGS(1);
         PUSHES_VALUES(2);
         RdlNativeEntry *entry = {0};

         lhs = pop(vm); // receiver

         entry = _get_entry(
               vm,
               result_type(lhs),
               op.as.text);

         if(entry == NULL) ERROR("could not find method");

         rhs = (Result){.kind = RESULT_FUNCTION, .entry = entry};
         push(vm, rhs); // fn
         push(vm, lhs); // receiver, preserved so the call can read it as arg0
         break;
      case OpCTX:
         PUSHES_VALUES(1);
         push(vm, stf_da_last(&vm->context));
         break;
      case OpJMP:       
         vm->pc = op.as.ptr;
         return true;
      case OpCONDJMP:   
         REQUIRES_ARGS(1);
         lhs = pop(vm);
         switch(lhs.kind)
         {
            case RESULT_NUMBER: 
               if(lhs.number) break;
            case RESULT_NULL:
            case RESULT_ERROR:            
            case RESULT_VOID:
               vm->pc = op.as.ptr;
               return true;
            default:
         } 
         break;
      case OpNULL:      
         PUSHES_VALUES(1);
         lhs = (Result){.kind = RESULT_NULL};
         push(vm, lhs);
         break;
      case OpPUSHIDX:
         PUSHES_VALUES(1);
         if(op.as.idx >= vm->sp)
            ERROR("tried to read outside Stack %d > %d", op.as.idx, vm->sp-1);
         lhs = vm->data[op.as.idx];
         push(vm, lhs);
         break;
      case OpPUSHREL:
         PUSHES_VALUES(1);
         if(vm->sp + op.as.offset >= vm->sp || vm->sp + op.as.offset < 0)
            ERROR("tried to read outside Stack %d > %d", vm->sp + op.as.offset, vm->sp-1);
         lhs = vm->data[vm->sp + op.as.offset];
         push(vm, lhs);
         break;
      case OpGETLOCAL:
      {
         PUSHES_VALUES(1);
         if(vm->lb + op.as.offset >= vm->sp || vm->lb + op.as.offset < 0)
            ERROR("tried to read outside Stack %d > %d", vm->lb + op.as.offset, vm->sp-1);
         lhs = vm->data[vm->lb + op.as.offset];
         push(vm, lhs);
         break;
      } break;
      case OpSETLOCAL:
      {
         if(vm->lb + op.as.offset >= vm->sp || vm->lb + op.as.offset < 0)
            ERROR("tried to read outside Stack %d > %d", vm->lb + op.as.offset, vm->sp-1);
         vm->data[vm->lb + op.as.offset] = pop(vm);
         break;
      } break;
      case OpCLOSURE: {
         lhs = buildclosure(vm, op.as.ptr, 0);
         push(vm, lhs);
      } break;
      case OpDUP:
         REQUIRES_ARGS(1);
         PUSHES_VALUES(1);
         lhs = stf_da_last(vm);
         push(vm, lhs);
         break;
   default:
      TODO("implement opcode %d", op.opcode);
   }
   ++vm->pc;
   return true;
}

void result_print(Result res) {
	switch (res.kind) {
		case RESULT_VOID:
			return;
		case RESULT_NULL:
			printf("NULL");
			break;
		case RESULT_ERROR:
			printf("EvalError %d", res.error_code);
			break;
		case RESULT_NUMBER:
			printf("%f", res.number);
			break;
		case RESULT_INT:
			printf("%d", res.integer);
			break;
		case RESULT_OBJECT:
         if(res.object->kind == OBJ_METHOD) {
			   printf("{%s.%s}", res.object->class_name, res.object->as.method.method_name);
            break;
         } else if(res.object->kind == OBJ_TEXT){
            printf("\"%s\"", res.object->as.string.cc);
            break;
         }
			printf("{%s}", res.object->class_name);
			break;
		default:
			TODO("result print %d", res.kind);
	}
}

const char *result_type(Result result){
	switch(result.kind){
		case RESULT_NUMBER:
			return "Number";
		case RESULT_INT:
			return "Int";
		case RESULT_OBJECT:
			return result.object->class_name;
		default:
			UNREACHABLE("unknown type");
	}
}

static void rdl_echo_results(RdlVM *vm, int start_depth){
	for(int i = start_depth; i < vm->count; ++i){
		Result res = vm->data[i];
		if(vm->echo_fn && res.kind != RESULT_NULL) vm->echo_fn(res);
	}
	vm->count = start_depth;
}

RdlNativeEntry *_get_entry(RdlVM *vm, const char *class_name, const char *signature){
	const char *key = ENTRY_KEY(class_name, signature);
	RdlNativeEntry *entry = ht_find(&vm->bindings, key);
	if(entry){
		return entry;
	}
	return NULL;
}

void _invoke_native(RdlVM *vm, const RdlNativeEntry *e) {
   int bp;
   double a,b,c;
   Result res;
   void *data;

   bp = vm->bp;
   vm->bp += e->arg_base + 1; // ignore fn obj
   res = (Result){ .kind = RESULT_NULL};

	switch (e->kind) {
		case RDL_RAW:
			((RdlCallback)e->as.fn)(vm);         
         break;
		case RDL_STACK:
         UNREACHABLE("RDL_STACK");
		case RDL_V_V:
			((void(*)(void))e->as.fn)();
         break;
		case RDL_VM_V:
			((RdlCallback)e->as.fn)(vm);
         break;
		case RDL_D_V:
			a = rdl_pop_double(vm);
			((void(*)(double))e->as.fn)(a);
         break;
		case RDL_D_D:
			a = rdl_pop_double(vm);
			res = (Result){.kind = RESULT_NUMBER, .number = ((double(*)(double))e->as.fn)(a)};
		   break;
		case RDL_DD_D:
			b = rdl_pop_double(vm);
			a = rdl_pop_double(vm);
			res = (Result){.kind = RESULT_NUMBER, .number = ((double(*)(double, double))e->as.fn)(a,b)};
		   break;
		case RDL_DDD_D:
			c = rdl_pop_double(vm);
			b = rdl_pop_double(vm);
			a = rdl_pop_double(vm);
			res = (Result){.kind = RESULT_NUMBER, .number = ((double(*)(double,double,double))e->as.fn)(a, b,c)};
		   break;
		case RDL_I_S:
			data = rdl_pop_foreign(vm);
			res = buildstring(vm, ((const char*(*)(void*))e->as.fn)(data));
		   break;
		case RDL_I_D:
			data = rdl_pop_foreign(vm);
			res = (Result){.kind = RESULT_NUMBER, .number = ((double(*)(void*))e->as.fn)(data)};
		   break;
		default: TODO("switch");
	}
   
   if(e->kind != RDL_RAW) push(vm, res); // rdl_raw allready on top of stack
   vm->bp = bp;
}

/*
void _where_iterator_step(RdlVM *vm){
	WhereIterator *iterator = (WhereIterator*)rdl_slot_get_foreign(vm, 0);
	if(rdl_slot_get_type(vm, 1) == RESULT_NULL){
		const char *class_name = result_type(iterator->obj);
		iterator->iterator_step = *_get_entry(vm, class_name, "iteratorStep(1)");
		iterator->iterator_value = *_get_entry(vm, class_name, "iteratorValue(1)");
		iterator->result = SLOT_GET(vm, 1);
	}

	Result value, condition;
   value = (Result){.kind = RESULT_VOID};

   push(vm, value); 
   Result *value_slot = vm->data+vm->count-1;

	do{
      rdl_scope(vm){

         push(vm, iterator->obj);
         push(vm, iterator->result);
         _invoke_native(vm, &iterator->iterator_step);
         iterator->result = pop(vm);

         if(iterator->result.kind != RESULT_NULL) {
            rdl_scope(vm) {
               push(vm, iterator->obj);
               push(vm, iterator->result);
               _invoke_native(vm, &iterator->iterator_value);
               *value_slot = pop(vm);
            }

            stf_da_append(&vm->context, *value_slot);
            rdl_scope(vm){
               stack_eval(vm, iterator->condition);
               condition = pop(vm);
               if(condition.kind != RESULT_NUMBER && condition.kind != RESULT_NULL) 
                  ERROR("where expected number or null");	
            }
            stf_da_pop(&vm->context);		

         } else {
            *value_slot = iterator->result;
         }
      }
	} while(
		iterator->result.kind != RESULT_NULL && !(condition.number)
	);

   // value_slot allready on top
}
*/

/*
void _where_iterator_value(RdlVM *vm){
	result_print(SLOT_GET(vm, 0));
	result_print(SLOT_GET(vm, 1));
	SLOT_GET(vm, 0) = SLOT_GET(vm, 1);
}
*/

Result buildstring(RdlVM *vm, const char *cc){
   Object *obj;

   obj = new_object(vm);

   obj->kind = OBJ_TEXT;
   obj->class_name = "String";
   obj->as.string.cc = strdup(cc);

   return (Result){
      .kind = RESULT_OBJECT,
      .object = obj,
   };
}

Object *new_object(RdlVM *vm){
   Object *obj;

   if(vm->freelist == NULL){
      collectgarbage(vm);
      if(vm->freelist == NULL){
         ERROR("GC Out of Memory (all %d objects still reachable)", vm->objects_count);
      }
   }

   obj = vm->freelist;
   vm->freelist = obj->next;

   obj->flags = 0;

   return obj;
}

Result buildinstance(RdlVM *vm, const char *class_name, int size){
   Object *obj; 
   
   obj = new_object(vm); 
   obj->kind = OBJ_INSTANCE;
   obj->class_name = strdup(class_name);
   obj->as.instance.data = ALLOC(size);
   
   return (Result){
      .kind = RESULT_OBJECT,
      .object = obj,
   };
}

Result buildmethod(RdlVM *vm, const char *class_name, const char *method_name){
   Object *obj;

   obj = new_object(vm);
   obj->kind = OBJ_METHOD;
   obj->class_name = strdup(class_name);
   obj->as.method.method_name = strdup(method_name);

   return (Result){
      .kind = RESULT_OBJECT,
      .object = obj,
   };
}

bool instance_cmp(const Object lhs, const Object rhs){
   if(lhs.kind == OBJ_INSTANCE && rhs.kind == OBJ_INSTANCE){
     return !memcmp(lhs.as.instance.data, rhs.as.instance.data, sizeof rhs.as.instance.data); 
   }
   TODO("Instance CMP");
}

void free_object_payload(Object *obj){
   switch(obj->kind){
      case OBJ_TEXT:
         free((void*)obj->as.string.cc);
         break;
      case OBJ_INSTANCE:
         free((void*)obj->class_name);
         free(obj->as.instance.data);
         break;
      case OBJ_METHOD:
         free((void*)obj->class_name);
         free((void*)obj->as.method.method_name);
         break;
      case OBJ_LIST:
         stf_da_free(&obj->as.list);
         break;
      case OBJ_CLOSURE:
         for(int i = 0; i < obj->as.closure.count; ++i)
            release_upvalue(obj->as.closure.upvalues[i]);
         free(obj->as.closure.upvalues);
         break;
   }
}

static void gc_mark(Result r){
   if(r.kind != RESULT_OBJECT) return;
   Object *obj = r.object;
   if(obj->flags & FOBJ_USED) return; // schon markiert (verhindert Endlosschleifen bei Zyklen)
   obj->flags |= FOBJ_USED;

   if(obj->kind == OBJ_LIST){
      stf_da_foreach(Result, item, &(obj->as.list)){
         gc_mark(*item);
      }
   }

   if(obj->kind == OBJ_CLOSURE){
      // open upvalues point at stack slots (already roots); closed ones own their value
      for(int i = 0; i < obj->as.closure.count; ++i){
         if(!obj->as.closure.upvalues[i]->open) gc_mark(obj->as.closure.upvalues[i]->closed);
      }
   }
}

void collectgarbage(RdlVM *vm){
   for(int i=0;i<vm->objects_count;++i){
      vm->objects[i].flags &= ~FOBJ_USED;
   }

   // Slots
   for(int i=0;i<vm->count;++i){
      gc_mark(vm->data[i]);
   }

   // Context
   for(int i=0;i<vm->context.count;++i){
      gc_mark(vm->context.data[i]);
   }

   // Register
   gc_mark(vm->register_a);

   Object *obj;
   int count = 0;
   for(int i=0;i<vm->objects_count;++i){
      obj = vm->objects+i;
      if(obj->flags & FOBJ_USED){ continue; }
      if(obj->flags & FOBJ_FREE){ continue; }

      ++count;
      free_object_payload(obj);
      *obj = (Object){0};
      obj->flags |= FOBJ_FREE;
      obj->next = vm->freelist;
      vm->freelist = obj;
   }
}

void register_variable(RdlVM *vm, const char *name){

   for(int i=vm->variables_count-1; i>=0; --i){
      if(!strcmp(vm->variables[i].name, name)) {
         vm->data[vm->variables[i].slot] = pop(vm);
         return;
      }
   } 

   if(vm->variables_count == MAX_VARIABLES) ERROR("Variables out of Memory");
   int slot = vm->count-1;

   VariableItem *var_reg = vm->variables+vm->variables_count++;
   var_reg->name = strdup(name);
   var_reg->slot = slot;
}

void reset_variables(RdlVM *vm, int idx){
   VariableItem *item;

   if(vm->variables_count < idx || idx < 0) return;
   while(vm->variables_count > 0 && vm->variables_count > idx){
      --vm->variables_count;
      item = vm->variables+vm->variables_count;
      free(item->name);
   }

}

void retrive_variable(RdlVM *vm, const char *name){
   for(int i=vm->variables_count-1; i>=0; --i){
      if(!strcmp(vm->variables[i].name, name)) {
         push(vm, vm->data[vm->variables[i].slot]);
         return;
      }
   } 
   // ERROR("variable %s not found", name);
   Result obj = buildinstance(vm, name, 0);
   push(vm, obj);
}

