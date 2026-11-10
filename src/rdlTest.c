// rdlTest - kleine CLI, um die OperationStack-basierte RDL-VM direkt gegen
// eine .rdl-Quelldatei zu fahren, ohne den Report/Ledger-Umweg über main.c.
//
//   rdlTest -f test.rdl -o out.txt
//
// Jede Top-Level-Expression in der Quelldatei wird kompiliert, ausgefuehrt
// und ihr Ergebnis (eine Zeile pro Expression) in die Output-Datei
// geschrieben - so laesst sich z.B. der Function-Call-Weg (OpIDENT -> FN_Obj
// -> OpBSET/OpCTX/OpCALL) an echtem Quelltext nachvollziehen.

#define HT_PUBDEF static
#include "ledger/include/ledger.h"
#include "rdl/include/rdl.h"

#define STF_IMPLEMENTATION
#include "include/da.h"

#define HT_IMPLEMENTATION
#include "thirdparty/include/ht.h"

#include <stdio.h>
#include <string.h>

typedef struct {
	const char **data;
	int count, capacity;
} Args;

static FILE *g_out = NULL;

static void write_result(Result res) {
	switch (res.kind) {
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

int main(int argc, const char **argv) {
	Args args = {
		.data = argv,
		.count = argc,
		.capacity = argc,
	};

	const char *input_file = NULL;
	const char *output_file = "./out.txt";
	bool jit = false;

	// erstes Arg ist der Programmname, ueberspringen
	++args.data;
	--args.count;

	while (args.count > 0) {
		if (!strcmp(stf_da_first(&args), "-f")) {
			++args.data;
			--args.count;
			if (args.count == 0) ERROR("-f erwartet einen Dateipfad");
			input_file = strdup(stf_da_first(&args));
			++args.data;
			--args.count;
			continue;
		}
		if (!strcmp(stf_da_first(&args), "-o")) {
			++args.data;
			--args.count;
			if (args.count == 0) ERROR("-o erwartet einen Dateipfad");
			output_file = strdup(stf_da_first(&args));
			++args.data;
			--args.count;
			continue;
		}
		if (!strcmp(stf_da_first(&args), "-j") || !strcmp(stf_da_first(&args), "--jit")) {
			++args.data;
			--args.count;
			jit = true;
			continue;
		}

		++args.data;
		--args.count;
	}

	if (!input_file) ERROR("usage: rdlTest -f <input.rdl> -o <output.txt> [-j|--jit]");

	StringBuffer sb = stf_read_file_to_sb(input_file);
	StringView src = stf_sv_from_sb(sb);

	g_out = fopen(output_file, "w+");
	if (!g_out) ERROR("could not open %s", output_file);

	RdlVM vm;
	rdl_init(&vm);
   //activate_template_mode();
	vm.echo_fn = write_result;
	vm.print_fn = write_result;
	vm.debug_jit = jit;

   rdl_compile(&vm, src);
	rdl_eval(&vm);

	fclose(g_out);
	printf("> %s -> %s\n", input_file, output_file);

	return 0;
}
