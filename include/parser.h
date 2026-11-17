#pragma once
#include "da.h"

typedef enum {
  TOKEN_ERROR = 0,
  TOKEN_EOF,
  TOKEN_END,
  TOKEN_WITH,
  TOKEN_VOID,
  TOKEN_NUMBER,
  TOKEN_TEXT,
  TOKEN_IDENT,
  TOKEN_KEYWORD,
  TOKEN_AT,

  TOKEN_PCLOSE,
  TOKEN_SEMI,
  TOKEN_PDOT,
  TOKEN_DOLAR,

  TOKEN_OP_BEGIN,
  TOKEN_POPEN,
  TOKEN_PIPE,
  TOKEN_DDOT,
  TOKEN_NLINE,
  TOKEN_WHERE,

  TOKEN_ASSIGN,
  TOKEN_COMMA,

  TOKEN_QMARK,

  TOKEN_MOD,
  TOKEN_GT,
  TOKEN_LT,
  TOKEN_EQ,
  TOKEN_LE,
  TOKEN_GE,
  TOKEN_ADD,
  TOKEN_SUB,
  TOKEN_MULT,
  TOKEN_DIV,

  TOKEN_NOT,
  TOKEN_AND,
  TOKEN_OR,
  TOKEN_PERCENT,
  
  TOKEN_DOT,

  TOKEN_OP_END,

} TOKEN_KIND;

typedef enum {
	KEYWORD_END, 
   KEYWORD_THIS, 
   KEYWORD_VAR, 
   KEYWORD_FN, 
   KEYWORD_IF, 
   KEYWORD_WITH, 
   KEYWORD_WHERE, 
   KEYWORD_LOOP, 
   KEYWORD_NOT,
   KEYWORD_SET, 
   KEYWORD_RETURN, 
   KEYWORD_NULL, 
   KEYWORD_FALSE,
} KEYWORD;

typedef struct Token Token;
struct Token{
  TOKEN_KIND kind;
  union {
    double number;
    StringView sv;
    KEYWORD keyword;
  };
  StringView location;
};

bool token_is_operation(Token t);
Token parse_next_token(StringView *source);
Token peak(StringView *src);
void init_keywords();
int get_lc();
