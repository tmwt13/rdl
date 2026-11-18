/* RdlVM 
 * 
 *
 *
 * Function Calls
 *    Push Fn_Name      -> [ name ]
 *    GETVAR            -> [ {fn} ]
 *    FN                -> [ {fn}, ab, pc, bp ]
 *    PUSHREL -4        -> [ {fn}, ab, pc, bp, {FN} ]
 *    Push arg1         -> [ {fn}, ab, pc, bp, {FN}, arg1 ]
 *    Push arg2         -> [ {fn}, ab, pc, bp, {FN}, arg1, arg2 ]
 *    CALL              -> [ {fn}, ab, pc, bp, {FN}, arg1, arg2 ]
 *       - save state
 *       A) C-Function
 *          call entry
 *          - restore state
 *       B) native
 *          shift pc
 *
 *    ...               -> [ {fn}, ab, pc, bp, {FN}, arg1, arg2, a, b, c, d ]
 *    RET               -> [ d ]
 *
 *
 */


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
Result      buildfunction(RdlVM *vm);
Result      buildstring(RdlVM *vm, const char *cc);

// Evaluating
bool        operation_eval(RdlVM *vm, Operation op);

/* Evalaute an OperationStack until returned in top level
 */
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

bool stack_eval(RdlVM *vm, const OperationStack stack){
   while(operation_eval(vm, stack.data[vm->pc]));
   return true;
}

/* ------------------------- *
 *  Function-Calls
 * ------------------------- */ 
/* Save:
 *    -4: ab
 *    -3: lb
 *    -2: pc
 *    -1: bp
 */
#define AB_OFFSET -4
#define LB_OFFSET -3
#define PC_OFFSET -2
#define BP_OFFSET -1

#define STORE(vm, _val) push(vm, ((Result){.kind = RESULT_INT, .integer = _val}))
void save_state(RdlVM *vm){
   STORE(vm, vm->ab);
   STORE(vm, vm->lb);
   STORE(vm, vm->pc + 1);
   STORE(vm, vm->bp);
   // update ArgBasePtr
   vm->ab = vm->sp;
   // bp gets filled back at OpCALL
}

#define RESTORE(vm, _val) ((_val) = pop(vm).integer)
void restore_state(RdlVM *vm){
   // reset sp
   vm->sp = vm->bp;
   RESTORE(vm, vm->bp);
   RESTORE(vm, vm->pc);
   RESTORE(vm, vm->lb);
   RESTORE(vm, vm->ab);
}

Result buildfunction(RdlVM *vm){
   Object *obj = new_object(vm);
   obj->kind = OBJ_FUNCTION;
   obj->class_name = "Function";
   return (Result){.kind = RESULT_OBJECT, .object = obj};
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
         // Fill back BP
         rhs = SLOT_GET(vm, vm->ab-1);
         rhs.integer = vm->bp;
         vm->bp = vm->ab;
         vm->lb = vm->sp;

         // Get {fn}
         lhs = SLOT_GET(vm, 0); 
         RdlNativeEntry *entry = NULL;
         if(lhs.kind != RESULT_OBJECT || lhs.object->kind != OBJ_FUNCTION)
            ERROR("can not call non fn obj");

         // A) C-Function or B) native RDL function
         entry = lhs.object->as.function.entry;
         // A
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

         // B) Function lives on stack 

         vm->pc = (offset_t)entry->as.offset;
         //    load stack data if available
         if(lhs.object->as.function.data){
            memcpy(vm->data+vm->sp, lhs.object->as.function.data, lhs.object.as.function.dcount);
            vm->sp += lhs.object.as.function.dcount;

            // pop pc
            rhs = pop(vm);
            if(rhs.kind != RESULT_INTEGER) ERROR("expected pc at end of stack data");
            vm->pc = rhs.integer;
            // TODO: rewind locals
         }

         //    push NULL as safety
         temp = (Result){.kind = RESULT_NULL};
         push(vm, temp);

         vm->cl = (Result){.kind = RESULT_NULL};

         return true;
      }
      case OpYIELD:
      {
         if(vm->bp == 0) {
            INFO("bp = 0");
            return false;
         }

         // Save result
         if(vm->sp - vm->bp == 0) {
            lhs = (Result){.kind = RESULT_NULL};
         } else {
            lhs = pop(vm);
         }

         // unwind locals
         unwind_locals(vm);
         
         // Store Stack 
         //    push pc
         rhs = (Result){.kind = RESULT_INTEGER, .integer = vm->pc+1};
         push(vm, rhs);
         //    save stack state
         Result fn = vm->data[vm->bp];
         if(fn.kind != RESULT_OBJECT || fn.object->kind != OBJECT_CLOSURE)
            ERROR("cannot yield non local function");
         int local_stack_size = vm->sp - vm->bp;
         fn.object->as.function.data = calloc(local_stack_size, sizeof(Result));
         fn.object->as.function.dcount = local_stack_size;
         memcpy(fn.object->as.function.data, vm->data+vm->bp, local_stack_size);
         
         // restore state
         restore_state(vm);
         // push result
         push(vm, lhs);
         return true;
      } break;
      case OpSETVAR:
      {
         lhs = pop(vm);
         if(lhs.kind != RESULT_NUMBER) ERROR("expected fn start idx");
         set_variable(vm, op.as.text, lhs);
      } break;
      case OpGETVAR:
      {
         if(!get_variable(vm, op.as.text)) return false;
      } break;
      case OpRET:
         if(vm->bp == 0) {
            INFO("bp = 0");
            return false;
         }
         
         // Save result
         if(vm->sp - vm->lb == 0) {
            lhs = (Result){.kind = RESULT_NULL};
         } else {
            lhs = pop(vm);
         }

         unwind_locals(vm);
         restore_state(vm);

         push(vm, lhs);
         return true;
      case OpDOLAR:
         lhs = pop(vm);

         if(lhs.kind != RESULT_NUMBER) ERROR("expected Number for dolar");
         int idx = (int)lhs.number;

         if(idx > vm->sp - vm->bp) 
            ERROR("to few args (argc = %d < %d)", vm->sp-vm->bp-2, idx);

         push(vm, vm->data[vm->ab+idx+1]); // fn, args...
         break;
      case OpECHO:
         if(vm->sp <= vm->lb + op.as.idx) break; // statement left no value
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
      {
         lhs = buildmethod(vm, "Global", op.as.text);
         push(vm, lhs);
      } break;
      case OpTXT:
         lhs = buildstring(vm, op.as.text);
         push(vm, lhs);
         break;
      case OpCTXPUSH:
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
         rhs = pop(vm);
         lhs = pop(vm);
         push(vm, rhs);
         push(vm, lhs);
         break;
      case OpDOT:
         RdlNativeEntry *entry = {0};

         lhs = pop(vm); // receiver

         entry = _get_entry(
               vm,
               result_type(lhs),
               op.as.text);

         if(entry == NULL) ERROR("could not find method");

         rhs = buildfunction(vm);
         rhs.object->as.function.entry = *entry;
         push(vm, rhs); // fn
         push(vm, lhs); // receiver, preserved so the call can read it as arg0
         break;
      case OpCTX:
         push(vm, stf_da_last(&vm->context));
         break;
      case OpJMP:       
         vm->pc = op.as.ptr;
         return true;
      case OpCONDJMP:   
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
         lhs = (Result){.kind = RESULT_NULL};
         push(vm, lhs);
         break;
      case OpPUSHIDX:
         if(op.as.idx >= vm->sp)
            ERROR("tried to read outside Stack %d > %d", op.as.idx, vm->sp-1);
         lhs = vm->data[op.as.idx];
         push(vm, lhs);
         break;
      case OpPUSHREL:
         if(vm->sp + op.as.offset >= vm->sp || vm->sp + op.as.offset < 0)
            ERROR("tried to read outside Stack %d > %d", vm->sp + op.as.offset, vm->sp-1);
         lhs = vm->data[vm->sp + op.as.offset];
         push(vm, lhs);
         break;
      case OpGETLOCAL:
      {
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
         lhs = buildfunction(vm);
         lhs.object->as.function.entry.kind = RDL_STACK;
         lhs.object->as.function.entry.as.offset = op.as.ptr;
         lhs.object->as.function.count = 0;
         push(vm, lhs);
      } break;
      case OpDUP:
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
// --------- HERE
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
         for(int i = 0; i < obj->as.function.count; ++i)
            release_upvalue(obj->as.function.upvalues[i]);
         free(obj->as.function.upvalues);
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
      for(int i = 0; i < obj->as.function.count; ++i){
         if(!obj->as.function.upvalues[i]->open) gc_mark(obj->as.function.upvalues[i]->closed);
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

void reset_variables(RdlVM *vm, int from){
   VariableItem *item;

   if(vm->variables_count == 0) return;
   while(vm->variables_count > 0 && vm->variables[vm->variables_count].slot >= idx){
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

/* ------------------------- *
 *  Upvalues
 * ------------------------- */ 
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
   Object *function = vm->cl.object;
   if(idx < 0 || idx >= function->as.function.count) ERROR("bad upvalue %d", idx);
   Upvalue *uv = function->as.function.upvalues[idx];
   return uv->open ? &vm->data[uv->slot] : &uv->closed;
}

bool get_variable(RdlVM *vm, const char *name)
{
   RdlNativeEntry *entry = {0};

   entry = _get_entry(
         vm,
         "Global",
         op.as.text);

   if(entry == NULL) ERROR("could not find function %s", op.as.text);
   lhs = (Result){.kind = RESULT_FUNCTION, .entry = entry};
   push(vm, lhs);
}
