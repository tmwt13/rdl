#define HT_PUBDEF static
#include "rdl.h"

#define STF_IMPLEMENTATION
#include "da.h"

#define HT_IMPLEMENTATION
#include "ht.h"

#include <stdio.h>
#include <string.h>

struct command {
   int level;
   const char *const name;
   const char *const signature;
   const char *const description;
   bool (*run)(struct command *self, const char *const prg_name, int *argc, const char **argv);
};


void command_print_usage(FILE *fd, struct command *cmd);
bool print_help(struct command *self, const char *const prg_name, int *argc, const char **argv);
bool set_out_path(struct command *self, const char *const prg_name, int *argc, const char **argv);
bool run_rdl_file(struct command *self, const char *const prg_name, int *argc, const char **argv);
bool run_next_command(const char *const prg_name, int *argc, const char **argv);

static struct command commands[] = {
   {
      .name = "help",
      .signature = " ",
      .description = "Print the help message",
      .run = print_help,
   },
   {
      .name = "run",
      .signature = "<FILE_PATH>",
      .description = "Run Rdl-File.",
      .run = run_rdl_file,
   },
   {
      .name = "-o",
      .level = 1,
      .signature = "<FILE_PATH>",
      .description = "Set output path for rdl.",
      .run = set_out_path,
   },
};
#define shift(_ac, _av) (assert((_ac)-- > 0 && "could not shift argc <= 0"), *(_av)++)
#define ARRAY_LEN(_a) (sizeof(_a)/sizeof(_a[0]))

const char *input_file = NULL;
const char *output_file = "./out.txt";

static FILE *g_out = NULL;

static void write_result(Result res) {
	switch (res.kind) {
		case RESULT_FUNCTION:
         TODO("RESULT_FUNCTION");
		case RESULT_VOID:
			return;
		case RESULT_NULL:
			fprintf(g_out, "NULL");
			break;
		case RESULT_ERROR:
			fprintf(g_out, "ERROR %d", res.error_code);
			break;
		case RESULT_NUMBER:
			fprintf(g_out, "%g", res.number);
			break;
		case RESULT_INT:
			fprintf(g_out, "%d", res.integer);
			break;
		case RESULT_CHAR:
			fprintf(g_out, "%c", res.chr);
			break;
		case RESULT_OBJECT:
			if (res.object->kind == OBJ_TEXT) {
				fprintf(g_out, "%s", res.object->as.string.cc);
			} else if (res.object->kind == OBJ_METHOD) {
				fprintf(g_out, "<%s.%s>", res.object->class_name, res.object->as.method.method_name);
			}
			break;
		default:
			fprintf(g_out, "<result kind=%d>", res.kind);
			break;
	}
}

void command_print_usage(FILE *fd, struct command *cmd)
{
   for(int i=0;i<cmd->level*2;++i) putc(' ', fd);
   fprintf(fd, "  %s %20s          %s\n", cmd->name, cmd->signature, cmd->description);
}

bool print_help(struct command *self, const char *const prg_name, int *argc, const char **argv)
{
   fprintf(stdout, "Usage:\n");
   for(int i=0; i<ARRAY_LEN(commands);++i){
      command_print_usage(stdout, commands + i);
   }
   return false;
}

bool set_out_path(struct command *self, const char *const prg_name, int *argc, const char **argv)
{
   if(!*argc){
      fprintf(stderr, "Expected file path after -o");
      command_print_usage(stderr, self);
      return false;
   }

   output_file = shift(argc, argv);
   return true;

}

bool run_rdl_file(struct command *self, const char *const prg_name, int *argc, const char **argv)
{
   if (*argc == 0) 
   {
      fprintf(stderr, "-f erwartet einen File Path\n");
      command_print_usage(stderr, self);
      return false;
   }

   input_file = shift(*argc, argv);

   while(*argc)
   {
      if(!run_next_command(prg_name, argc, argv))
         return false;
   }

	if (!input_file) 
   {
      fprintf(stderr, "No Rdl-File provided");
      command_print_usage(stderr, self);
      return false;
   }

	StringBuffer sb = stf_read_file_to_sb(input_file);
	StringView src  = stf_sv_from_sb(sb);

	g_out = fopen(output_file, "w+");
	if (!g_out) ERROR("could not open %s", output_file);

	RdlVM vm;
	rdl_init(&vm);
   //activate_template_mode();
	vm.echo_fn = write_result;
	vm.print_fn = write_result;

   rdl_compile(&vm, src);
	rdl_eval(&vm);

	fclose(g_out);
	printf("> %s -> %s\n", input_file, output_file);
}


bool run_next_command(const char *const prg_name, int *argc, const char **argv)
{
   if(*argc <= 0) return true;
   for(int i=0; i<ARRAY_LEN(commands);++i){
      if(strcmp(commands[i].name, *argv) == 0)
      {
         shift(*argc, argv);
         return commands[i].run(commands+i, prg_name, argc, argv);
      }
   }
   fprintf(stderr, "Command \"%s\" unknown.\n", *argv);
   return print_help(commands, prg_name, argc, argv);
}

int main(int argc, const char **argv) {
   const char *prg_name = shift(argc, argv);

   if(!run_next_command(prg_name, &argc, argv))
      return 1;

	return 0;
}
