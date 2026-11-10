#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <ctype.h>

#ifdef STF_NO_SHORT_TYPES_
#else
#ifndef STF_SHORT_TYPES_
	typedef uint8_t 	u8;
	typedef uint16_t 	u16;
	typedef uint32_t 	u32;
	typedef uint64_t 	u64;
#endif // STF_SHORT_TYPES_
#endif // STF_NO_SHORT_TYPES_

#ifndef STF_H_
#define STF_H_

#define ERROR(msg, ...) (printf("%s:%d \033[1;31mERROR:\033[0m " msg "\n",__FILE__,__LINE__, ##__VA_ARGS__), exit(200))
#define TODO(msg, ...) (printf("%s:%d \033[1;34mTODO\033[0m " msg "\n",__FILE__,__LINE__, ##__VA_ARGS__), exit(200))
#define UNREACHABLE(msg, ...) (printf("%s:%d \033[1;31mUNREACHABLE\033[0m: " msg "\n",__FILE__,__LINE__, ##__VA_ARGS__), exit(200))
#define INFO(msg, ...) printf("%s:%d \033[1;34mINFO\033[0m " msg "\n",__FILE__,__LINE__, ##__VA_ARGS__)

typedef int Error;

typedef struct{
  char *data;
  int count, capacity;
} StringBuffer;

typedef struct {
  const char *data;
  int count;
} StringView;

#define SV_Fmt "%.*s"
#define SV_Args(sv) (sv).count, (sv).data

#define SVLIT(lit) ((StringView){.count = sizeof(lit)-1, .data = (lit)})

StringView stf_sv_from_cc(const char *str);
StringView stf_sv_from_sb(const StringBuffer sb);

int         stf_sv_cmp(const StringView a, const StringView b);
int         stf_sv_cc_cmp(const StringView a, const char *b);

char stf_sv_shift(StringView *sv);
int         stf_sv_trim_left(StringView *sv);
int         stf_sv_trim_right(StringView *sv);
StringView  stf_sv_chop_left(StringView *sv, int count);
StringView  stf_sv_chop_right(StringView *sv, int count);
StringView  stf_sv_split(StringView *sv, char seperator);
StringView  stf_sv_split_after(StringView *sv, char seperator, int n);
int         stf_sv_first_where(StringView *sv, char seperator);

#define STF_TEMP_BUFFER_SIZE 1024*1024
static char stf_temp_buffer[STF_TEMP_BUFFER_SIZE];
static size_t stf_temp_count = 0;
void *stf_temp_alloc(int size);
void stf_temp_clear();
char *stf_temp_strdup(const char *str);
char *stf_temp_lower(const char *str);
char *stf_temp_upper(const char *str);
char *stf_temp_strdupn(const char *str, int len);
char *stf_temp_sprintf(const char *format, ...) __attribute__ ((format (printf, 1, 2)));
StringView stf_temp_svprintf(const char *format, ...) __attribute__ ((format (printf, 1, 2)));
const char *stf_temp_svtocc(const StringView sv);

// read file
StringBuffer stf_read_file_to_sb(const char *path);

#define stf_svdup(sv) strdup(stf_temp_svtocc(sv))

#define stf_da_free(da) ((da)->count=0, (da)->capacity=0, free((da)->data))
#define stf_da_reserve(da, n)                                                  \
  do {                                                                         \
    if ((da)->count + (n) <= (da)->capacity)                                   \
      break;                                                                   \
    while ((da)->count + (n) > (da)->capacity) {                               \
      if ((da)->capacity == 0)                                                 \
        (da)->capacity = 1;                                                    \
      (da)->capacity *= 2;                                                     \
    }                                                                          \
    (da)->data = realloc((da)->data, sizeof((da)->data[0]) * (da)->capacity);  \
    if ((da)->data == NULL)                                                    \
      exit(69);                                                                \
  } while (0)

#define stf_da_append(ptr, item)                                                   \
  do {                                                                         \
    stf_da_reserve(ptr, 1);                                                        \
    (ptr)->data[(ptr)->count++] = (item);                                        \
  } while (0)

#define stf_da_at(ptr, i) (ptr)->data[(assert((i) < (ptr)->count), (i))]

#define stf_da_memcpy(ptr, buf_ptr, n)                                           \
  do {                                                                         \
    DA_RESERVE(ptr, n);                                                        \
    memcpy((ptr)->data + (ptr)->count, (buf_ptr),                              \
           (n) * sizeof((ptr)->data[0]));                                      \
    (ptr)->count += (n);                                                       \
  } while (0)

#define stf_da_last(ptr) (ptr)->data[(assert((ptr)->count > 0), (ptr)->count-1)]
#define stf_da_lastp(ptr) ((ptr)->data+((assert((ptr)->count > 0), (ptr)->count-1)))
#define stf_da_first(ptr) (ptr)->data[(assert((ptr)->count > 0), 0)]

#define stf_da_pop(ptr) (ptr)->data[(assert((ptr)->count > 0), --(ptr)->count)]

#define stf_da_popn(ptr,n) (ptr)->count-=n;

#define stf_da_foreach(type, name, da)                                         \
  for (type *name = (da)->data; name < (da)->data + (da)->count; ++name)

#define stf_da_rforeach(type, name, da)                                        \
  for (type *name = (da)->data + (da)->count - 1; name >= (da)->data; --name)

#define stf_da_append_str(da, cptr, len)                                       \
  do {                                                                         \
    stf_da_reserve(da, len + 1);                                               \
    stf_da_memcpy(da, cptr, len);                                              \
    stf_da_append(da, 0);                                                      \
  } while (0)

static float stf_ctof(const char *text, int len);
double stf_svtod(StringView sv);
static int stf_svtoi(StringView sv);
Error stf_sb_from_char(StringBuffer *sb, char* text, int len);
StringView stf_sb_scanf(StringBuffer *sb);

#endif // STF_H_

#ifdef STF_IMPLEMENTATION

static float stf_ctof(const char* text,int len){
  char buf[20] = {0};
  memcpy(buf, text, len);
  return atof(buf);  
}

double stf_svtod(StringView sv)
{
   double res = 0;
   if(!sv.count) return res;
   else if(*sv.data == '-') {
      ++sv.data; --sv.count;
      return -stf_svtod(sv);
   } else if(*sv.data == '+') {
      ++sv.data; --sv.count;
      return stf_svtod(sv);
   } else {
      while(sv.count && *sv.data != '.')
      {
         if(!isdigit(*sv.data)) return res; 
         res = res * 10 + (*sv.data - '0');
         ++sv.data; --sv.count;
      }
      if(!(*sv.data == '.')) return res;
      ++sv.data; --sv.count;

      double factor = 1;
      while(sv.count)
      {
         if(!isdigit(*sv.data)) return res; 
         factor /= 10;
         res = res + factor * (*sv.data - '0');
         ++sv.data; --sv.count;
      }
   }

   return res;
}

static int stf_svtoi(StringView sv)
{
	char buf[20] = {0};
	memcpy(buf, stf_temp_svtocc(sv), sv.count);
	return atoi(buf);  
}

void *stf_temp_alloc(int size) {
  assert(size < STF_TEMP_BUFFER_SIZE && "STF_ERROR: temp_buffer to small\n"); 
  if (stf_temp_count + size > STF_TEMP_BUFFER_SIZE) {
	  stf_temp_count = 0;
  }
  int c = stf_temp_count;
  stf_temp_count += size;
  return (void*)(stf_temp_buffer + c);
}

// stolen from nob.h
char *stf_temp_vsprintf(const char *format, va_list ap) {
    va_list args;
    va_copy(args, ap);
    int n = vsnprintf(NULL, 0, format, args);
    va_end(args);

    assert(n >= 0);
    char *result = (char*)stf_temp_alloc(n + 1);
    assert(result != NULL && "Extend the size of the temporary allocator");
    // TODO: use proper arenas for the temporary allocator;
    va_copy(args, ap);
    vsnprintf(result, n + 1, format, args);
    va_end(args);

    return result;
}

// stolen from nob.h
char *stf_temp_sprintf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    char *result = stf_temp_vsprintf(format, args);
    va_end(args);
    return result;
}

StringView stf_temp_svprintf(const char *format, ...) {
  size_t n = stf_temp_count;

  va_list args;
  va_start(args, format);
  char *result = stf_temp_vsprintf(format, args);
  va_end(args);

  return (StringView){
    .data = result,
    .count = stf_temp_count - n - 1, // 0 not included
  };
}

#define STF_IS_WHITESPACE(c) ((c) < 33)

int stf_sv_trim_left(StringView *sv){
  int start = sv->count;
  while(sv->count && STF_IS_WHITESPACE(*(sv->data))) stf_sv_shift(sv);
  return sv->count - start;
}

int stf_sv_trim_right(StringView *sv){
  int start = sv->count;
  while(sv->count > 0 && STF_IS_WHITESPACE(stf_da_last(sv))){
    --sv->count;
  }
  return start - sv->count;
}

const char *stf_temp_svtocc(const StringView sv)
{
  char *dest = stf_temp_alloc(sv.count + 1);
  memcpy(dest, sv.data, sv.count);  
  dest[sv.count] = 0;
  return dest;
}

StringView stf_sv_from_cc(const char *str)
{
  int len = strlen(str); // without 0
  return (StringView){
    .data = str,
    .count = len,
  };
}

StringView stf_sv_from_sb(const StringBuffer sb){
  return (StringView){
    .count = sb.count,
    .data = sb.data,
  };
}

int stf_sv_cc_cmp(const StringView a, const char *b)
{
   return stf_sv_cmp(a, stf_sv_from_cc(b));
}

int stf_sv_cmp(const StringView a, const StringView b)
{
  if (a.count != b.count) return 1;
  return memcmp(a.data, b.data, a.count);
}

StringBuffer stf_read_file_to_sb(const char *path)
{
	FILE *f = fopen(path, "rb");
   if(f == NULL) {
      INFO("OPEN FAILED: %s | errno: %d (%s)\n", path, errno, strerror(errno));
      return (StringBuffer){0};
   }
	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	fseek(f, 0, SEEK_SET);
	
	StringBuffer sb = {
		.capacity = fsize,
		.count = fsize,
		.data = malloc(fsize),
	};
	fread(sb.data, fsize, 1, f);
	fclose(f);

	return sb;
}

char *stf_temp_strdup(const char *str)
{
	int len = strlen(str);
	return stf_temp_strdupn(str, len);
}

char *stf_temp_lower(const char *str)
{
	int len = strlen(str);
	char *data = stf_temp_alloc(len+1);
	for(int i=0;i<len;i++){
		if(str[i] >= 'A' && str[i] <= 'Z'){
			data[i] = str[i] + ('a' - 'A');
		} else data[i] = str[i];
	}
	data[len] = 0;
	return data;
}

char *stf_temp_upper(const char *str)
{
	int len = strlen(str);
	char *data = stf_temp_alloc(len+1);
	for(int i=0;i<len;i++){
		if(str[i] >= 'a' && str[i] <= 'z'){
			data[i] = str[i] + ('A' - 'a');
		} else data[i] = str[i];
	}
	data[len] = 0;
	return data;
}

char *stf_temp_strdupn(const char *str, int len)
{
	char *data = stf_temp_alloc(len+1);
	memcpy(data, str, len);
	data[len] = 0;
	return data;
}

StringView stf_sv_chop_left(StringView *sv, int count){
  if(sv->count >= count) {
    StringView left = {
      .data = sv->data,
      .count = count,
    };
    sv->data += count;
    sv->count -= count;
    return left;
  } else {
    StringView left = *sv;    
    sv->data += sv->count;
    sv->count = 0;
    return left;
  }
}

StringView stf_sv_chop_right(StringView *sv, int count){
  if(sv->count >= count) {
    StringView right = {
      .data = sv->data+sv->count-count,
      .count = count,
    };
    sv->count -= count;
    return right;
  } else {
    StringView right = *sv;    
    sv->count = 0;
    return right;
  }
}

inline char stf_sv_shift(StringView *sv){
  assert(sv->count > 0 && "can not shift");
  ++sv->data; --sv->count;
  return *(sv->data - 1);
}

StringView stf_sv_split(StringView *sv, char seperator){
  StringView left = *sv;
  while(sv->count > 0 && *sv->data != seperator) stf_sv_shift(sv);
  if(sv->count > 0) {
    stf_sv_shift(sv);
    left.count -= sv->count + 1;
  }
  return left;
}

StringView stf_sv_split_after(StringView *sv, char seperator, int n){
  StringView left = stf_sv_chop_left(sv, n);
  if(sv->count == 0) return left;

  while(sv->count > 0 && *sv->data != seperator) stf_sv_shift(sv);
  if(sv->count > 0) {
    stf_sv_shift(sv);
    left.count = sv->data - left.data - 1;
  }
  return left;
}

int stf_sv_first_where(StringView *sv, char character) {
  int i = 0;
  while(sv->count > i && sv->data[i] != character) ++i;
  if(i == sv->count) return -1; 
  return i;
}

#endif // STF_IMPLEMENTATION
