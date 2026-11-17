#include "da.h"
#include "parser.h"

#ifndef _CUST_LEX
#define _CUST_LEX

typedef struct{
	StringView *src;
	Token next;
} Lexer;

#endif // _CUST_LEX
       
Lexer new_lexer(StringView *src);
Token eat(Lexer *L);
void activate_template_mode();

