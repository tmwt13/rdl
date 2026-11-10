#include "include/lexer.h"

extern enum {
   TEMPLATE_NONE,
   TEMPLATE_TEXT,
   TEMPLATE_RDL,
} template_state;

Lexer new_lexer(StringView *src) {

	return (Lexer){
		.src = src,
		.next = parse_next_token(src),
	};
}

void activate_template_mode(){
   template_state = TEMPLATE_TEXT;
}

Token eat(Lexer *L) {
	Token cur = L->next;
   L->next = parse_next_token(L->src);
	return cur;
}

