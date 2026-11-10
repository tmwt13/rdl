#include "include/parser.h"

#define HT_PUBDEF static
#define HT_IMPLEMENTATION
#include "../thirdparty/include/ht.h"

#include <ctype.h>

typedef Ht(const char *, KEYWORD) Keywords;

enum {
   TEMPLATE_NONE,
   TEMPLATE_TEXT,
   TEMPLATE_RDL,
} template_state = TEMPLATE_NONE;
static bool protected_dot = false;

static Keywords keywords = {
	.hasheq = ht_cstr_hasheq, 
};

void init_keywords(){
	*ht_put(&keywords, "var") = KEYWORD_VAR;
	*ht_put(&keywords, "this") = KEYWORD_THIS;
	*ht_put(&keywords, "if") = KEYWORD_IF;
	*ht_put(&keywords, "fn") = KEYWORD_FN;
	*ht_put(&keywords, "end") = KEYWORD_END;
	*ht_put(&keywords, "not") = KEYWORD_NOT;
	*ht_put(&keywords, "loop") = KEYWORD_LOOP;
	*ht_put(&keywords, "with") = KEYWORD_WITH;
	*ht_put(&keywords, "where") = KEYWORD_WHERE;
	*ht_put(&keywords, "set") = KEYWORD_SET;
	*ht_put(&keywords, "return") = KEYWORD_RETURN;
	*ht_put(&keywords, "null") = KEYWORD_NULL;
	*ht_put(&keywords, "false") = KEYWORD_FALSE;
}

static int line_count = 0;
int get_lc(){
   return line_count;
}

#define LSTART(token, source) (token).location.data = (source)->data
#define LEND(token, source) (token).location.count = (source)->data - (token).location.data

Token parse_next_token(StringView *source) {
   Token next_token = {0};

   if(template_state == TEMPLATE_TEXT){
      StringView text = *source;

      LSTART(next_token, source);
      while(source->count > 1 && (source->data[0] != '{' || source->data[1] != '{')) 
         stf_sv_shift(source);

      if(source->count > 1) {
         text.count = source->data - text.data;
         stf_sv_shift(source);
         stf_sv_shift(source);
      }
      template_state = TEMPLATE_RDL;

      //INFO("found TEXT : \n" SV_Fmt "\n left: \n" SV_Fmt "\n--\n", SV_Args(text), SV_Args(*source));

      next_token.kind = TOKEN_TEXT;
      next_token.sv = text;
      LEND(next_token, source);
      return next_token;
   } 

	if(source->count > 2 && source->data[0] == ';' && source->data[1] == ';'){
		while(source->count > 0 && source->data[0] != '\n')
			stf_sv_shift(source);
		return parse_next_token(source);
	}

   stf_sv_trim_left(source);


	if (source->count == 0) {
		return (Token){.kind = TOKEN_EOF};
	}

   if(template_state == TEMPLATE_RDL 
         && source->count > 1 
         && source->data[0] == '}' && source->data[1] == '}')
   {
      if(source->count < 2) 
         ERROR("expected closing '}}'");

      stf_sv_shift(source);
      stf_sv_shift(source);
      if (source->count && *source->data == 10) 
         stf_sv_shift(source);
      template_state = TEMPLATE_TEXT;
      return parse_next_token(source);
   }


   LSTART(next_token, source);
	switch (*(source->data)) 
   {
		case '\n':
         UNREACHABLE("should not happen");
         ++line_count;
         stf_sv_shift(source);
         LEND(next_token, source);
         next_token.kind = TOKEN_NLINE;
         return next_token;   // VOID
		case '_':
		  stf_sv_shift(source);
        LEND(next_token, source);
        next_token.kind = TOKEN_SUB;
			return next_token;
      case '$':
			stf_sv_shift(source);
         LEND(next_token, source);
         next_token.kind = TOKEN_DOLAR;
			return next_token;
      case '@':
			stf_sv_shift(source);
         LEND(next_token, source);
         next_token.kind = TOKEN_AT;
			return next_token;
      case '?':
		  stf_sv_shift(source);
        LEND(next_token, source);
        next_token.kind = TOKEN_QMARK;
			return next_token;
      case ':':
		  stf_sv_shift(source);
        LEND(next_token, source);
        next_token.kind = TOKEN_DDOT;
		  return next_token;
      case ';':
		  stf_sv_shift(source);
        LEND(next_token, source);
        next_token.kind = TOKEN_SEMI;
		  return next_token;
      case '+':
		  stf_sv_shift(source);
        LEND(next_token, source);
        next_token.kind = TOKEN_ADD;
			return next_token;
      case '!':
		  stf_sv_shift(source);
        LEND(next_token, source);
        next_token.kind = TOKEN_NOT;
			return next_token;
      case '%':
         stf_sv_shift(source);
         if(source->count > 0 && stf_da_first(source) == '%') {
            stf_sv_shift(source);
            LEND(next_token, source);
            next_token.kind = TOKEN_MOD;
         } else next_token.kind = TOKEN_PERCENT;   // ADD
         return next_token;
		case '-':
		  stf_sv_shift(source);
        LEND(next_token, source);
        next_token.kind = TOKEN_SUB;
			return next_token;
      case '.':
         if(*(source->data-1) < 33 || *(source->data-1) == '{')
            next_token.kind=TOKEN_PDOT;
         else 
            next_token.kind = TOKEN_DOT;
         stf_sv_shift(source);
         LEND(next_token, source);
         return next_token;
      case '<':
			stf_sv_shift(source);
			if(source->count > 0 && source->data[0] == '='){
				next_token.kind = TOKEN_LE;    // LT
			} else next_token.kind = TOKEN_LT;    // LT
         stf_sv_shift(source);
         LEND(next_token, source);
         return next_token;
		case '>':
			stf_sv_shift(source);
			if(source->count > 0 && source->data[0] == '>'){
				next_token.kind = TOKEN_PIPE;    // LT
			}
         else if(source->count > 0 && source->data[0] == '='){
				next_token.kind = TOKEN_GE;    // LT
			} else next_token.kind = TOKEN_GT;    // GT
         stf_sv_shift(source);
         LEND(next_token, source);
         return next_token;
		case '=':
			stf_sv_shift(source);
			if(source->count > 0 && stf_da_first(source) == '='){
            stf_sv_shift(source);
            LEND(next_token, source);
				next_token.kind = TOKEN_EQ;    // LT
            return next_token;
			}
			ERROR("=");
		case '*':
		  stf_sv_shift(source);
        LEND(next_token, source);
        next_token.kind = TOKEN_MULT;
			return next_token;
      case '&':
		  stf_sv_shift(source);
        LEND(next_token, source);
        next_token.kind = TOKEN_AND;
			return next_token;
      case '|':
		  stf_sv_shift(source);
        LEND(next_token, source);
        next_token.kind = TOKEN_OR;
			return next_token;
      case '/':
		  stf_sv_shift(source);
        LEND(next_token, source);
        next_token.kind = TOKEN_DIV;
			return next_token;
      case '(':
		  stf_sv_shift(source);
        LEND(next_token, source);
        next_token.kind = TOKEN_POPEN;
			return next_token;
      case ')':
		  stf_sv_shift(source);
        LEND(next_token, source);
        next_token.kind = TOKEN_PCLOSE;
			return next_token;
      case ',':
		  stf_sv_shift(source);
        LEND(next_token, source);
        next_token.kind = TOKEN_COMMA;
			return next_token;
      case '\'':
		{
			stf_sv_shift(source);
         next_token.kind = TOKEN_TEXT;
			next_token.sv.data = source->data;
			while(source->count > 0 && source->data[0] != '\''){
				if(source->data[0] == '\\' )
					stf_sv_shift(source);
				stf_sv_shift(source);
			}
			if(source->count == 0) ERROR("expected closing '");
			next_token.sv.count = source->data - next_token.sv.data;
			stf_sv_shift(source);
         LEND(next_token, source);
			return next_token;
		}
		case '"':
			stf_sv_shift(source);
         next_token.kind = TOKEN_TEXT;
			next_token.sv.data = source->data;
			while(source->count > 0 && source->data[0] != '"'){
				if(source->data[0] == '\\' )
					stf_sv_shift(source);
				stf_sv_shift(source);
			}
			if(source->count == 0) ERROR("expected closing \"");
			next_token.sv.count = source->data - next_token.sv.data;
			stf_sv_shift(source);
         LEND(next_token, source);
			return next_token;
		default:
			break;
	}

	if (isdigit(stf_da_first(source))) {
      LSTART(next_token, source);
		while (source->count > 0 && (isdigit(stf_da_first(source)) || (stf_da_first(source) == '.' && source->data[1] != '.')))
			stf_sv_shift(source);
      LEND(next_token, source);

      next_token.kind = TOKEN_NUMBER;
		next_token.number = atof(stf_temp_svtocc(next_token.location));
		return next_token;
	}

	if (isalpha(stf_da_first(source))) {
      LSTART(next_token, source);
		while (source->count > 0 && isalnum((unsigned char)stf_da_first(source)))
			stf_sv_shift(source);

      LEND(next_token, source);


		const char * dest = stf_temp_strdupn(next_token.location.data, next_token.location.count);

		KEYWORD *key;
		if((key = ht_find(&keywords, dest))){
         switch(*key){
            case KEYWORD_NOT:
               next_token.kind = TOKEN_NOT;
               break;
            case KEYWORD_END:
               next_token.kind = TOKEN_END;
               break;

            default:
               next_token.kind = TOKEN_KEYWORD;
               next_token.keyword = *key;
         }
         return next_token;
		}

      next_token.kind = TOKEN_IDENT;
		next_token.sv = next_token.location;
      return next_token;
	}

	UNREACHABLE("unknown next char: %d  \"" SV_Fmt "\"", *(source->data), SV_Args(*source));
}
