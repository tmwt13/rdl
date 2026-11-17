#define HT_PUBDEF static
#include "rdl.h"
#include "parser.h"
#include "lexer.h"

#include "da.h"
#define HT_IMPLEMENTATION
#include "ht.h"

typedef struct {
	int lb, rb;
} Bp; // Binding-Power

bool        is_op(Token t);
int         prefix_bp(Token token);
int         postfix_bp(Token token);
Bp          infix_bp(Token token);

offset_t    operation(Operation op,OperationStack *stack) ;
void        process(Token token, OperationStack *stack);
bool        expr_bp(RdlVM *vm, Lexer *L, int min_bp, OperationStack *stack);
bool        compile(RdlVM *vm, Lexer *L, OperationStack *stack);
bool        compile_args(RdlVM *vm, Lexer *L, OperationStack *stack);
// compile calls
void        compile_call(RdlVM *vm, Lexer *L, OperationStack *stack);
void        compile_dot_call(RdlVM *vm, Lexer *L, OperationStack *stack, Token name);
bool        compile_set(RdlVM *vm, Lexer *L, OperationStack *stack);
void        compile_reset(void);
bool        compile_with(RdlVM *vm, Lexer *L, OperationStack *stack);
bool        compile_loop(RdlVM *vm, Lexer *L, OperationStack *stack);
bool        compile_if(RdlVM *vm, Lexer *L, OperationStack *stack);
// compile fn (bodys)
bool        compile_fn(RdlVM *vm, Lexer *L, OperationStack *stack);
bool        compile_fn_global(RdlVM *vm, Lexer *L, OperationStack *stack);

const char  *token_cc(TOKEN_KIND kind);
void        stack_print(const OperationStack stack);


// Variables
//    Variables are stored in a linked list and are checkt and resolved at compile time
//    TODO: search for variable name will be linear form bottom most to top most scope
//    TODO: I want that function become some kind of variables too
//    TODO: some way for closures to capture parent variables

struct variable {
   const char *name;
   int slot;
};

struct FnState {
   struct FnState    *parent;
   struct {
      struct variable data[10];
      int count;
   } locals;
   int stack_offset;     // keeping track of where the var is on the stack
};

static struct FnState top_fs;
static struct FnState *fs = &top_fs;

void compile_reset(void){
   top_fs = (struct FnState){0};
   fs = &top_fs;
}

/*
 *       fn foo
 *          set a 1
 *          set bar fn
 *             a
 *             set a a+1
 *          end 
 *          set a 0
 *          return bar
 *       end
 *
 */
static void declare_local(const char *name)
{
   struct variable new = {0};
   new.name = strdup(name);
   new.slot = fs->stack_offset++ - 1;
   fs->locals.data[fs->locals.count++] = new;
}

static int find_local(const char *name)
{
   for(int i=0; i<fs->locals.count; ++i)
   {
      if(strcmp(fs->locals.data[i].name, name) == 0) return fs->locals.data[i].slot; 
   }
   return -1;
}

// frame slots below the fn call that OpFN opens (see Calling convention)
#define PRE1 (-(CALL_PROLOG_SIZE + 1))   // state data + 1
#define PRE2 (-(CALL_PROLOG_SIZE + 2))
   
/* ------------------------- *
 *  Implementation
 * ------------------------- */ 
bool is_op(Token t) {
	return (t.kind >= TOKEN_OP_BEGIN && t.kind <= TOKEN_OP_END);
}

int prefix_bp(Token token){
	switch(token.kind){
		case TOKEN_NOT: return 20;
		case TOKEN_DOLAR: return 35; // must bind tighter than call "(" (20): $1(x) == ($1)(x) and "." (30)
		case TOKEN_PDOT:
		case TOKEN_DOT: return 35;
		case TOKEN_ADD: return 21;
		case TOKEN_SUB: return 21;
		case TOKEN_KEYWORD: return 2;
      // else:
		default:
         return 0;
	}
}

int postfix_bp(Token token){
	switch(token.kind){
		case TOKEN_PERCENT: return 20;  
		default:
         return 0;
	}
}

Bp infix_bp(Token token){
	switch(token.kind){
		case TOKEN_AND:
			return (Bp){5,5};
		case TOKEN_OR:
			return (Bp){4,4};
		case TOKEN_GE:
		case TOKEN_LE:
		case TOKEN_EQ:
		case TOKEN_GT:
		case TOKEN_LT:
			return (Bp){6,6};
		case TOKEN_SUB:
		case TOKEN_ADD:
			return (Bp){10,10};
		case TOKEN_DIV:
		case TOKEN_MULT:
			return (Bp){11,11};
		case TOKEN_DOT:
			return (Bp){30,30};
		case TOKEN_DDOT:
			return (Bp){1,1};
		case TOKEN_POPEN:
			return (Bp){20,0};
		case TOKEN_PIPE: // > POPEN && < DOT
			return (Bp){25,0};
		case TOKEN_COMMA:
			return (Bp){2,2};
		case TOKEN_QMARK:
			return (Bp){3,3};
		case TOKEN_WHERE:
			return (Bp){2,2};
		default:
			ERROR("%s is not infix", token_cc(token.kind));
			return (Bp){0};
	}  
}

int net_operation_impact(Operation op)
{
   int impacts[39] = {
      1, //OpERROR,    // Abort program
      1, //OpNULL,     // PUSH null
      1, //OpNUM,      // PUSH num
      1, //OpTXT,      // PUSH text
      1, //OpIDENT,    // PUSH ident
      1, //OpDUP,      // PUSH last element again
      1, //OpCTXPOP,   // PUSH ctx and pop it
      1, //OpCTX,      // PUSH ctx 
      1, //OpPUSHIDX,  // PUSH value form idx
      1, //OpPUSHREL,  // PUSH rel to bp
      1, //OpPUSHA,
      1, //OpGETVAR,   // PUSH var value
      1, //OpGETLOCAL,   // PUSH var value

      -1, //OpPOP,      // POP last value
      -1, //OpCTXPUSH,  // POP and push to ctx
      -1, //OpPOPIDX,   // POP value to idx
      -1, //OpCONDJMP,  // POP and evtl. JMP
      -1, //OpPOPA,
      -1, //OpDOT,      // pop receiver, combine with the method/field name baked into this op (as.text) -> push fn, push receiver
      -1, //OpADD,
      -1, //OpSUB,
      -1, //OpMULT,
      -1, //OpDIV,
      -1, //OpEQ,
      -1, //OpLT,
      -1, //OpGT,
      -1, //OpELT,
      -1, //OpEGT,
      -1, //OpECHO,     // pop+echo top of stack, but only if something sits above the locals (as.idx = #slots owned by the compiler)
      -1, //OpSETVAR,   // pop name and register fn
      -1, //OpSETLOCAL,   // pop name and register fn
       
      0, //OpDOLAR,    // POP idx PUSH value
      0, //OpJMP,      // JMP to idx (pc)
      0, //OpSWAP,     // swap the top two values
      0, //OpNOT,
      
      // special cases:
      CALL_PROLOG_SIZE, //OpFN,       // PUSH old bp + pc  (sets new call frame)
      0, //OpCALL,     // 
      0, //OpRET,      // pop to bp and push result
      0, //OpCLOSURE,  // build a closure for the fn body at as.ptr; followed by one OpUPDESC per captured variable
   };
   
   return impacts[op.opcode];
}

offset_t operation(Operation op, OperationStack *stack){
   if(stack->count >= stack->capacity) ERROR("OperationStack to small");
   stack->data[stack->count] = op;
   fs->stack_offset += net_operation_impact(op);
   return stack->count++;
}

#define OP_PUSH(stck, ...) operation((Operation){__VA_ARGS__}, stck)

void process(Token token, OperationStack *stack) {
   Operation op = {0};
	switch (token.kind) {
		case TOKEN_NUMBER:
         OP_PUSH(stack, .opcode = OpNUM, .as = {.number = token.number}); break;
      case TOKEN_TEXT:
         OP_PUSH(stack, .opcode = OpTXT, .as = {.text = stf_temp_svtocc(token.sv)}); break;
      case TOKEN_IDENT:
         OP_PUSH(stack, .opcode = OpIDENT, .as = {.text = stf_temp_svtocc(token.sv)}); break;
      case TOKEN_PDOT: break;
      case TOKEN_DOT: break;
      case TOKEN_DOLAR:
         OP_PUSH(stack, .opcode = OpDOLAR); break;
      case TOKEN_ADD:
         OP_PUSH(stack, .opcode = OpADD); break;
      case TOKEN_EQ:
         OP_PUSH(stack, .opcode = OpEQ); break;
      case TOKEN_LT:
         OP_PUSH(stack, .opcode = OpLT); break;
      case TOKEN_GT:
         OP_PUSH(stack, .opcode = OpGT); break;
      case TOKEN_LE:
         OP_PUSH(stack, .opcode = OpELT); break;
      case TOKEN_GE:
         OP_PUSH(stack, .opcode = OpEGT); break;
      case TOKEN_DDOT:
         TODO("process ddot");
      case TOKEN_SUB:
         OP_PUSH(stack, .opcode = OpSUB); break;
      case TOKEN_MULT:
         OP_PUSH(stack, .opcode = OpMULT); break;
      case TOKEN_DIV:
         OP_PUSH(stack, .opcode = OpDIV); break;
		case TOKEN_PERCENT:
         TODO("process percent");
		case TOKEN_KEYWORD:
			TODO("KEYWORD");
		case TOKEN_NOT:
         OP_PUSH(stack, .opcode = OpNOT); break;
		case TOKEN_POPEN: return;
		case TOKEN_COMMA:
         // Trennt Argumente; die beiden Operanden liegen als eigene
         // Werte schon auf dem Stack - nichts zu tun.
         break;
		case TOKEN_NLINE:
			UNREACHABLE("ERROR");
		case TOKEN_EOF:
			UNREACHABLE("ERROR");
		case TOKEN_QMARK:
         TODO("process qmark");
		case TOKEN_WHERE:
         TODO("process where");
		case TOKEN_AND:
         TODO("process TOKEN_AND");
		case TOKEN_OR:
         TODO("process TOKEN_OR");
		case TOKEN_END:
			UNREACHABLE("ERROR");

		default:
			TODO("TOKEN IMPL %d", token.kind);
	}
}


bool compile(RdlVM *vm, Lexer *L, OperationStack *stack){
   while(L->next.kind == TOKEN_NLINE) eat(L);

   if(L->next.kind == TOKEN_KEYWORD){
      // set is a statement: the value stays on the stack as the new local
      if(L->next.keyword == KEYWORD_SET) return compile_set(vm, L, stack);
      // fn name ... end as a statement defines a global function
      if(L->next.keyword == KEYWORD_FN && fs == &top_fs) return compile_fn_global(vm, L, stack);
   }

   bool res = expr_bp(vm, L, 0, stack);
   OP_PUSH(stack, .opcode = OpECHO);
   return res;
}

// set name expr
//    sets variables (local -> parent scope -> global)
/*
 *
 *    Variable Table:
 *       global.     a  = 1
 *       foo.        b  = 2
 *       bar.        c  = 3
 */
bool compile_set(RdlVM *vm, Lexer *L, OperationStack *stack){
   // eat set keyword
   eat(L);
   // expecting var name
   if(L->next.kind != TOKEN_IDENT) ERROR("expected a name after 'set'");

   StringView name = eat(L).sv;
   // compile variable value
   expr_bp(vm, L, 0, stack);

   //    1: try get slot of variable
   int slot; 
   if((slot = find_local(stf_temp_svtocc(name))) >= 0) {
   // if found 
      // Push Set value Operation
      OP_PUSH(stack, .opcode = OpSETLOCAL, .as.offset = slot); //TODO: POP to and PUSH from IDX OPCODES
      return true;
   }
   //    TODO: variables outside the scope
   //    2: new local: the value just computed *is* its slot
   declare_local(stf_temp_svtocc(name));

   return true;
}

/* calls:
 *    Normal call:
 *    stack on call [fn_obj, fn_obj, ctx, args... ]
 *                           ^
 *                           bp
 *
 *    Dot call:
 *    stack on call [fn_obj, receiver, fn_obj, receiver, args... ]
 *                           ^
 *                           bp
 */
void compile_call(RdlVM *vm, Lexer *L, OperationStack *stack){
   OP_PUSH(stack, .opcode = OpFN);                          // push old argb and store new one
   OP_PUSH(stack, .opcode = OpPUSHREL, .as.offset = -(CALL_PROLOG_SIZE + 1));    // slot0 = fn
   OP_PUSH(stack, .opcode = OpCTX);                         // slot1 = ambient this

   compile_args(vm, L, stack);                              // slot2.. = args

   OP_PUSH(stack, .opcode = OpCALL);                     // set bp and jmp
   OP_PUSH(stack, .opcode = OpSWAP);                     // swap result and fn
   OP_PUSH(stack, .opcode = OpPOP);                      // drop the leftover fn
}

void compile_dot_call(RdlVM *vm, Lexer *L, OperationStack *stack, Token name){
   OP_PUSH(stack, .opcode = OpDOT, .as.text = stf_temp_svtocc(name.sv));   // -> [fn, receiver]
   OP_PUSH(stack, .opcode = OpFN);
   OP_PUSH(stack, .opcode = OpPUSHREL, .as.offset = -(CALL_PROLOG_SIZE + 2));                   // slot0 = fn
   OP_PUSH(stack, .opcode = OpPUSHREL, .as.offset = -(CALL_PROLOG_SIZE + 2));                   // slot1 = receiver

   compile_args(vm, L, stack);                                             // slot2.. = args
                                                                           
   OP_PUSH(stack, .opcode = OpCALL);
   OP_PUSH(stack, .opcode = OpSWAP);
   OP_PUSH(stack, .opcode = OpPOP);                                        // drop the leftover receiver
   OP_PUSH(stack, .opcode = OpSWAP);
   OP_PUSH(stack, .opcode = OpPOP);                                        // drop the leftover fn
}

bool expr_bp(RdlVM *vm, Lexer *L, int min_bp, OperationStack *stack) {
   if(L->next.kind == TOKEN_EOF){
      OP_PUSH(stack, .opcode = OpRET);       
		return false;
	}

   if(L->next.kind == TOKEN_PCLOSE) return true;

	if(L->next.kind == TOKEN_NLINE){
		eat(L);
      expr_bp(vm, L, min_bp, stack);
		return true;
	}

   Token token;
	int rp;
	if ((rp = prefix_bp(L->next))){
		if(L->next.kind == TOKEN_KEYWORD){
			switch(L->next.keyword){
         case KEYWORD_THIS:
            eat(L);
            OP_PUSH(stack, .opcode = OpCTX);
            break;
			case KEYWORD_IF:     compile_if(vm, L, stack);     break;
			case KEYWORD_LOOP:   compile_loop(vm, L, stack);  break;
			case KEYWORD_WITH:   compile_with(vm, L, stack);   break;
			case KEYWORD_FN:     compile_fn(vm,L,stack);       break;
			case KEYWORD_RETURN:
				eat(L);
				if(L->next.kind == TOKEN_NLINE || L->next.kind == TOKEN_END || L->next.kind == TOKEN_EOF)
					OP_PUSH(stack, .opcode = OpNULL);
				else
					expr_bp(vm, L, 0, stack);
				OP_PUSH(stack, .opcode = OpRET);
				break;
			case KEYWORD_NULL:
				eat(L);
				OP_PUSH(stack, .opcode = OpNULL);
				break;
			case KEYWORD_FALSE:
				eat(L);
				OP_PUSH(stack, .opcode = OpNUM, .as.number = 0);
				break;
			case KEYWORD_SET:
				ERROR("'set' is a statement, it cannot be used inside an expression");
			default:
				TODO("keyword %d", L->next.keyword);
			}
		} else {
         if(L->next.kind == TOKEN_PDOT || L->next.kind == TOKEN_DOT){
					eat(L); // .
               // implicit receiver: the ambient `this`
               OP_PUSH(stack, .opcode = OpCTX);
               if(L->next.kind != TOKEN_IDENT) ERROR("expected Ident as Method, got %d", L->next.kind);
               compile_dot_call(vm, L, stack, eat(L));
         } else if(L->next.kind == TOKEN_SUB){
               eat(L);
               OP_PUSH(stack, .opcode = OpNUM, .as.number = 0);
               expr_bp(vm, L, rp, stack);
               OP_PUSH(stack, .opcode = OpSUB);
         } else if(L->next.kind == TOKEN_ADD){
               eat(L);
               expr_bp(vm, L, rp, stack);
         } else {
            token = eat(L);
            expr_bp(vm, L, rp, stack);
            process(token, stack);
         }
		}
	} else {
		switch (L->next.kind) {
			case TOKEN_POPEN:
            compile_args(vm, L, stack);
				break;

/*
 * add(1,2)    -> add 1 2 call      -> add 1 2           ;push add, push 1, push 2, call
 * x           -> x call            -> x                 ;push x, call
 * 1.add(2)    -> 1 add . 2 call    -> Num.add 1 2 call  ;push 1, push add, dot, push 2, call
 * Acc.name    -> Acc name . call   -> Acc.name call     ;push Acc, push name, dot, call
 *
 * 
 * A.B(c)      -> [expr A], [push B], dot, call
 * 
 *
 * add expr call
 */
 /*
  *       
  *
  */
         case TOKEN_IDENT:
            {
               Token name = eat(L);
               const char *cc = stf_temp_svtocc(name.sv);
               int slot, up;

               // if local or parent variable
               if((slot = find_local(cc)) >= 0){
                  // plain value; a following "(" is handled as an infix call
                  OP_PUSH(stack, .opcode = OpGETLOCAL, .as.offset = slot);
               } 
               // has to be global / native variable
               else {
                  OP_PUSH(stack, .opcode = OpGETVAR, .as.text = cc);
                  compile_call(vm, L, stack);
               }
            }
            break;
			default:
				process(eat(L), stack);
		}
	}

	while (is_op(L->next)) {
		if(L->next.kind == TOKEN_NLINE) {
			eat(L);
			return true;
		}
      // Handle PostFix

		int lb;
		if ((lb = postfix_bp(L->next))) {
			if (lb < min_bp) {
				break;
			}
			process(eat(L), stack);
			continue;
		}

		Bp bp = infix_bp(L->next);
		if (bp.lb <= min_bp) {
			break;
		}
		switch(L->next.kind){
			case TOKEN_QMARK:
				{
               TODO("shorthand if");
				} break;
         case TOKEN_POPEN:
         {
            // whatever is already on the stack (one value) is the callable
            compile_call(vm, L, stack);
         } break;
			case TOKEN_DOT:
				{
					eat(L); // .
               if(L->next.kind != TOKEN_IDENT) ERROR("expected Ident as Method");
               compile_dot_call(vm, L, stack, eat(L));
				} break;
         case TOKEN_PIPE:
         {
            // a >> fn(b)   ->   fn ctx a b call
            int stack_saved = fs->stack_offset;
            eat(L); // >>
            if(L->next.kind != TOKEN_IDENT) ERROR("expected function name after '>>'");
            Token fn_name = eat(L);
            OP_PUSH(stack, .opcode = OpFN);                                            // +2
            OP_PUSH(stack, .opcode = OpIDENT, .as.text = stf_temp_svtocc(fn_name.sv)); // slot0 = fn
            OP_PUSH(stack, .opcode = OpCTX);                                           // slot1 = ambient this
            OP_PUSH(stack, .opcode = OpPUSHREL, .as.offset = -(CALL_PROLOG_SIZE + 4));  // slot2 = a (piped value)
            fs->stack_offset += 3+CALL_PROLOG_SIZE;
            compile_args(vm, L, stack);                                                // slot3.. = args
            OP_PUSH(stack, .opcode = OpCALL);
            OP_PUSH(stack, .opcode = OpSWAP);
            OP_PUSH(stack, .opcode = OpPOP);                                           // drop the leftover `a`
            fs->stack_offset = stack_saved;
         } break;
			default:
				{
					token = eat(L);
					expr_bp(vm, L, bp.rb, stack);
               process(token, stack);
				}
		}
		continue;
	}
   
   return true;
}

const char *token_cc(TOKEN_KIND kind) {
	switch (kind) {
		case TOKEN_ERROR:
			return "TOKEN_ERROR";
		case TOKEN_QMARK:
			return "TOKEN_QMARK";
		case TOKEN_DDOT:
			return "TOKEN_DDOT";
		case TOKEN_EOF:
			return "TOKEN_EOF";
		case TOKEN_VOID:
			return "TOKEN_VOID";
		case TOKEN_NUMBER:
			return "TOKEN_NUMBER";
		case TOKEN_TEXT:
			return "TOKEN_TEXT";
		case TOKEN_IDENT:
			return "TOKEN_IDENT";
		case TOKEN_KEYWORD:
			return "TOKEN_KEYWORD";
		case TOKEN_OP_BEGIN:
			return "TOKEN_OP_BEGIN";
		case TOKEN_POPEN:
			return "TOKEN_POPEN";
		case TOKEN_PCLOSE:
			return "TOKEN_PCLOSE";
		case TOKEN_ASSIGN:
			return "TOKEN_ASSIGN";
		case TOKEN_COMMA:
			return "TOKEN_COMMA";
		case TOKEN_MOD:
			return "TOKEN_MOD";
		case TOKEN_GT:
			return "TOKEN_GT";
		case TOKEN_LT:
			return "TOKEN_LT";
		case TOKEN_ADD:
			return "TOKEN_ADD";
		case TOKEN_SUB:
			return "TOKEN_SUB";
		case TOKEN_MULT:
			return "TOKEN_MULT";
		case TOKEN_DIV:
			return "TOKEN_DIV";
		case TOKEN_NOT:
			return "TOKEN_NOT";
		case TOKEN_AND:
			return "TOKEN_AND";
		case TOKEN_OR:
			return "TOKEN_OR";
		case TOKEN_PERCENT:
			return "TOKEN_PERCENT";
		case TOKEN_DOT:
			return "TOKEN_DOT";
		case TOKEN_WITH:
			return "TOKEN_WITH";
		case TOKEN_END:
			return "TOKEN_END";
		case TOKEN_OP_END:
			return "TOKEN_OP_END";
		case TOKEN_NLINE:
			return "TOKEN_NLINE";
		default:
			TODO("token_cc");
	}
}

void print_op(Operation op){
   switch(op.opcode){
      case OpADD:       printf("[OpADD]\n"); break;
      case OpSUB:       printf("[OpSUB]\n"); break;
      case OpMULT:       printf("[OpMULT]\n"); break;
      case OpDIV:       printf("[OpDIV]\n"); break;
      case OpEQ:        printf("[OpEQ]\n"); break;
      case OpLT:        printf("[OpLT]\n"); break;
      case OpGT:        printf("[OpGT]\n"); break;
      case OpELT:       printf("[OpELT]\n"); break;
      case OpEGT:       printf("[OpEGT]\n"); break;
      case OpNUM:       printf("[push: %f]\n", op.as.number); break;
      case OpIDENT:     printf("[push: {%s}]\n", op.as.text); break;
      case OpTXT:       printf("[push: \"%s\"]\n", op.as.text); break;
      case OpCTXPUSH:   printf("[ctx push]\n"); break;
      case OpCTXPOP:    printf("[ctx pop]\n"); break;
      case OpCTX:       printf("[push: CTX]\n"); break;
      case OpFN:        printf("[OpFN] ;; FN_CALL BEGIN\n"); break;
      case OpCALL:      printf("[OpCALL]\n"); break;
      case OpJMP:       printf("[OpJMP: %d]\n", op.as.ptr); break;
      case OpCONDJMP:   printf("[OpCONDJMP: %d]\n", op.as.ptr); break;
      case OpNULL:      printf("[OpNULL]\n"); break;
      case OpPUSHIDX:   printf("[OpPUSHIDX %d]\n", op.as.idx); break;
      case OpPUSHREL:   printf("[OpPUSHREL sp + %d]\n", op.as.offset); break;
      case OpDUP:       printf("[OpDUP]\n"); break;
      case OpECHO:      printf("[OpECHO]\n"); break;
      case OpDOT:       printf("[OpDOT .%s]\n", op.as.text); break;
      case OpNOT:       printf("[OpNOT]\n"); break;
      case OpPUSHA:     printf("[OpPUSHA]\n"); break;
      case OpPOPA:      printf("[OpPOPA]\n"); break;
      case OpSWAP:      printf("[OpSWAP]\n"); break;
      case OpPOP:       printf("[OpPOP]\n"); break;
      case OpRET:       printf("[OpRET]\n"); break;
      case OpERROR:     printf("[OpERROR]\n"); break;
      case OpCLOSURE:   printf("[OpCLOSURE -> %d]\n", op.as.ptr); break;
      case OpDOLAR:     printf("[OpDOLAR]\n"); break;
      case OpGETVAR:     printf("[OpGETVAR %s]\n", op.as.text); break;
      case OpSETVAR:     printf("[OpSETVAR %s]\n", op.as.text); break;
      case OpGETLOCAL:     printf("[OpGETLOCAL lb + %d]\n", op.as.offset); break;
      case OpSETLOCAL:     printf("[OpSETLOCAL lb + %d]\n", op.as.offset); break;

      default:
                        TODO("implement opcode %d", op.opcode);
   }
}

void stack_print(const OperationStack stack){
   OperationStack cpy = stack;
   printf("Stack: (%zu); \n", stack.count);
   Operation op = {0};
   printf("===== INSTRUCTION-STACK =====\n");
   for(int i=0; i<stack.count; ++i){
      op = stack.data[i];
      printf("%d: ", i);
      print_op(op);
   }
}

// end of a fn body: drop its locals so OpRET finds the default NULL
static void compile_fn_end(OperationStack *stack){
   // Return from fuction body
   OP_PUSH(stack, .opcode = OpRET);
   //for SAFETY: executing out of function operations
   OP_PUSH(stack, .opcode = OpERROR, .as.text = "not allowed to read");
}

// fn name ... end 
//    a named global function,
//    compiled out of line and registered by name
bool compile_fn_global(RdlVM *vm, Lexer *L, OperationStack *stack){
   offset_t jump_idx, fn_start;
   Token token;
   struct FnState state = {0};

   // check if to level
   if(fs != &top_fs) ERROR("Global fn has to be defined at top level");

   // eat fn keyword
   eat(L);
   // expect function name
   if(L->next.kind != TOKEN_IDENT) ERROR("expected FN name");
   // eat fn name
   token = eat(L);

   // set new function State (= State called from)
   state.parent = &top_fs;
   fs = &state;

   // Jump over function body
   jump_idx = OP_PUSH(stack, .opcode = OpJMP);

   // store Start of fuction body
   fn_start = stack->count;
   // compile function body
   while(L->next.kind != TOKEN_EOF && L->next.kind != TOKEN_END) compile(vm, L, stack);
   // function must end with END keyword
   if(L->next.kind != TOKEN_END) ERROR("expected end");
   // eat end keyword
   eat(L);

   // function closing opertations (unwind, ret, err)
   compile_fn_end(stack);

   // restore function state of parent
   fs = &top_fs;

   // fill in jump over with body len
   stack->data[jump_idx].as.ptr = stack->count;

   // register function by name
   OP_PUSH(stack, .opcode = OpNUM, .as.number = fn_start);
   OP_PUSH(stack, .opcode = OpSETVAR, .as.text = stf_temp_svtocc(token.sv));

   return true;
}

bool compile_fn(RdlVM *vm, Lexer *L, OperationStack *stack){
   struct FnState state = {0};
   offset_t skip, start;

   // eat fn keyword
   eat(L);
   // name is only a label -> drop it
   if(L->next.kind == TOKEN_IDENT) eat(L); 

   // save parent state
   state.parent = fs;
   // set current state
   fs = &state;

   // jump over fn body
   skip = OP_PUSH(stack, .opcode = OpJMP);
   start = stack->count;

   // parse fn body
   while(L->next.kind != TOKEN_EOF && L->next.kind != TOKEN_END) compile(vm, L, stack);
   if(L->next.kind != TOKEN_END) ERROR("expected end");
   eat(L);

   // end operations (unwind, ret, error)
   compile_fn_end(stack);
   // fill back jmp loaction
   stack->data[skip].as.ptr = stack->count;

   // restore parent state
   fs = state.parent;

   OP_PUSH(stack, .opcode = OpCLOSURE, .as.ptr = start);
   // for(int i = 0; i < state.nups; ++i){
   //    OP_PUSH(stack, .opcode = OpUPDESC, .as.idx = state.ups[i]);
   // }

   return true;
}

bool compile_with(RdlVM *vm, Lexer *L, OperationStack *stack){
   eat(L);
   // push obj
   expr_bp(vm, L, 0,stack);               // +1
   // ctx obj
   OP_PUSH(stack, .opcode = OpCTXPUSH);   // -1
   // expr
   while(L->next.kind != TOKEN_EOF && L->next.kind != TOKEN_END) compile(vm, L,stack);
   if(L->next.kind != TOKEN_END) ERROR("end of range expected");
   eat(L);
   // ctx clear
   OP_PUSH(stack, .opcode = OpCTXPOP);    // +0

   return true;
}



/*
 * loop <iterator> ... end
 */
bool compile_loop(RdlVM *vm, Lexer *L, OperationStack *stack){
   offset_t stmt, start;
   // eat loop
   eat(L);
   // eval target -> iterator, kept as the ambient `this` for the whole loop
   expr_bp(vm, L, 0,stack);                                          // +1
   OP_PUSH(stack, .opcode = OpCTXPUSH);                              // -1

   // result = iterator()
   start = OP_PUSH(stack, .opcode = OpFN);
   OP_PUSH(stack, .opcode = OpCTX);                                  // +1 slot0 = the iterator as fn
   OP_PUSH(stack, .opcode = OpCALL);                                 // -1+1 

   // CONDJMP eats the copy; on exit the original result is still there
   OP_PUSH(stack, .opcode = OpDUP);                                  // +1
   stmt = OP_PUSH(stack, .opcode = OpCONDJMP);                       // -1

   // result becomes the ambient `this` for the loop body
   OP_PUSH(stack, .opcode = OpCTXPUSH);                              // -1

   while(L->next.kind != TOKEN_EOF && L->next.kind != TOKEN_END) {
      compile(vm, L,stack);                                          // 0
   }
   if(L->next.kind != TOKEN_END) ERROR("end of loop expected");
   // eat end
   eat(L);

   OP_PUSH(stack, .opcode = OpCTXPOP);                               // 0 drop value
   OP_PUSH(stack, .opcode = OpJMP, .as.ptr = start);
   stack->data[stmt].as.ptr = stack->count;

   OP_PUSH(stack, .opcode = OpPOP);    // drop the final false/null  // -1
   OP_PUSH(stack, .opcode = OpCTXPOP); // drop iterator             

   return true;
}

bool compile_if(RdlVM *vm, Lexer *L, OperationStack *stack){
   // eat if
   eat(L);   
   // compile expr
   expr_bp(vm, L, 0,stack); // +1
   
   // jump to end
   offset_t cond_offset = OP_PUSH(stack, .opcode = OpCONDJMP); // -1
   while(L->next.kind != TOKEN_END && L->next.kind != TOKEN_EOF) compile(vm, L, stack);
   if(L->next.kind != TOKEN_END)
      ERROR("expected END at end of if");
   eat(L);
   stack->data[cond_offset].as.ptr = stack->count;

   return true;
}

bool compile_args(RdlVM *vm, Lexer *L, OperationStack *stack){
   if(L->next.kind == TOKEN_POPEN) {
      eat(L); // (

      // expr_bp's own comma handling (bp {2,2}, a no-op separator) walks
      // the whole comma-separated list in one call, pushing one value per arg.
      if(L->next.kind != TOKEN_PCLOSE){
         expr_bp(vm, L, 0, stack);
         fs->stack_offset += 1;
      }
      if (L->next.kind != TOKEN_PCLOSE) ERROR("expected closing () got %s", token_cc(L->next.kind));
      eat(L); // )
   }

   return true;
}
