//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "bearc.h"
#include "cli/args.h"
#include "cli/import_path.h"
#include "cli/versioning.h"
#include "compiler/compile.h"
#include "compiler/token.h"
#include "utils/ansi_codes.h"
#include <stdio.h>
#include <string.h>

// private cli error struct
#define BR_CLI_DO_FN_ERROR_MSG_LEN 64
typedef struct {
    int error_code;
    char error_message[BR_CLI_DO_FN_ERROR_MSG_LEN];
} cli_error_status;

// private cli functions
void cli_help(void);
void cli_version(void);
cli_error_status cli_compile(const bearc_args_t* args);
cli_error_status cli_build(const bearc_args_t* args);
void cli_no_args(void);
void cli_announce_unknown_flag(void);
void warn_duplicate_flag(void);
void warn_too_many_input_files(void);
void warn_no_arg_for_import_path(void);
void warn_no_arg_for_output(void);
// checks if the args are otherwise empty besides the specified flag
bool cli_args_otherwise_empty(bearc_args_t* args, cli_flag_e flag);

void do_cli_announce_incompatible_flags(void);

int bearc_compile_from_args(int argc, char** argv) {
    ansi_init();
    // parse and do preliminary validatation on args, convert to max 1 flag, 1 filename
    bearc_args_t args = parse_cli_args(argc, argv);

    // error that dispatched cli functions can return, default to inoffensive values

    bool err = false;

    if (args.flags[CLI_FLAG_ERR_UNKNOWN]) {
        cli_announce_unknown_flag();
        err = true;
    }
    if (args.flags[CLI_FLAG_ERR_DUPLICATE]) {
        warn_duplicate_flag();
        err = true;
    }
    if (args.flags[CLI_FLAG_ERR_TOO_MANY_INPUT_FILES]) {
        warn_too_many_input_files();
        err = true;
    }

    if (args.flags[CLI_FLAG_ERR_NO_ARGUMENT_PROVIDED_TO_IMPORT_PATH]) {
        warn_no_arg_for_import_path();
        err = true;
    }

    if (args.flags[CLI_FLAG_ERR_NO_ARGUMENT_PROVIDED_TO_OUTPUT]) {
        warn_no_arg_for_output();
        err = true;
    }

    // no compilation options
    if (args.flags[CLI_FLAG_HELP]) {
        if (!cli_args_otherwise_empty(&args, CLI_FLAG_HELP)) {
            do_cli_announce_incompatible_flags();
            err = true;
        }
        if (!err) {
            cli_help();
        }
        return 0;
    }
    if (args.flags[CLI_FLAG_VERSION]) {
        if (!cli_args_otherwise_empty(&args, CLI_FLAG_VERSION)) {
            do_cli_announce_incompatible_flags();
            err = true;
        }
        if (!err) {
            cli_version();
        }
        return 0;
    }

    if (args.flags[CLI_FLAG_ERR_FILE_NAME_TOO_LONG]) {

        printf("%s(bearc)%s file name exessively long\n", ansi_bold(), ansi_reset());
        err = true;
    }

    if (!args.input_file_name && cli_args_otherwise_empty(&args, CLI_FLAG_ERR_UNKNOWN)) {
        cli_no_args();
        return 0;
    }

    // basic input file validation
    if (!args.input_file_name) {
        printf("%s(bearc)%s no root file provided\n", ansi_bold_reset(), ansi_reset());

        err = true;
    }

    if (args.input_file_name != NULL
        && !file_exists_on_import_path(args.input_file_name, ".", &args)) {
        printf("%s(bearc)%s error: %sfile does not exist: %s'%s'\n%s", ansi_bold_reset(),
               ansi_bold_red(), ansi_reset(), ansi_bold(), args.input_file_name, ansi_reset());
        err = true;
    }

    if (err) {
        return -1;
    }
    cli_error_status error_status = {0, ""};
    // compilation
    if (strlen(args.input_file_name)) {
        error_status = cli_compile(&args);
    }
    if (error_status.error_code < 0) {
        return error_status.error_code;
    }
    return 0;
}

void cli_help(void) {
    const char* usage_title = "usage:\n";
    const char* usage = "        bearc";
    const char* lbrack = " [";
    const char* usage_options = "options";
    const char* rbrack = "]\n";
    const char* options_title = "options:\n";
    const char* usage_args = " <file_name>";
    const char* usage_args_flags = "flags";
    const char* options = "        [--version | -v]  display current bearc version\n"
                          "        [--help | -h]     display this help message\n";
    const char* flags_title = "flags:\n";
    const char* flags
        = "        [--silent]             suppress all compiler diagnostics\n"
          "        [--token-table]        print a list of lexed tokens\n"
          "        [--pretty-print]       print a syntax tree diagram\n"
          "        [--file-graph]         list all compiled files with their dependencies\n"
          "        [--parse-only]         stop compilation after parsing\n"
          "        [--compact-diags]      print diagnostics that are vertically compact\n"
          "        [--terse | -t]         only print error and warning diagnostics\n"
          "        [--strict-syntax | -s] stop compilation after parsing when syntax errors are "
          "present\n";
    const char* flags_w_args_title = "flags with arguments:\n";
    const char* flags_w_args
        = "        [--import-path | -I] <import_dirs...>  supply import paths\n"
          "        [--compile | -c]     <root_file>       compile from a root file\n"
          "        [--output | -o]      <output_file>     specify an output file\n"

        ;
    const char* options_color = ansi_bold_yellow();
    const char* flags_color = ansi_bold_cyan();
    printf("%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n", ansi_bold_cyan(),
           usage_title, ansi_reset(), ansi_bold_green(), usage, ansi_bold_reset(), lbrack,
           options_color, usage_options, ansi_bold_reset(), rbrack, ansi_bold_green(), usage,
           ansi_bold_reset(), usage_args, lbrack, flags_color, usage_args_flags, ansi_bold_reset(),
           rbrack, options_color, options_title, ansi_reset(), options, flags_color, flags_title,
           ansi_reset(), flags, flags_color, flags_w_args_title, ansi_reset(), flags_w_args);
}

void cli_version(void) { printf("bearc " BEARC_VERSION_STR "\n"); }

cli_error_status cli_compile(const bearc_args_t* args) {
    cli_error_status error_status = {0, ""};
    error_status.error_code = compile_file(args);
    token_maps_free(); // after all operations involving token lookups are done
    return error_status;
}

void cli_no_args(void) {
    printf("run '%sbearc%s --help%s' to see available operations.\n\n", ansi_bold_green(),
           ansi_bold_reset(), ansi_reset());

    printf("Bear Compiler Copyright (C) 2025-2026 Zachary Mahan \n"
           "This program comes with ABSOLUTELY NO WARRANTY.\n"
           "This is free software, and you are welcome to redistribute it under certain "
           "conditions.\n"
           "For more information, visit: "
           "https://www.gnu.org/licenses/gpl-3.0.en.html#license-text\n\n");

    cli_version();

    puts("");
}
void cli_announce_unknown_flag(void) {
    printf("%s(bearc)%s unknown flag(s)\n", ansi_bold(), ansi_reset());
}

void warn_duplicate_flag(void) {
    printf("%s(bearc)%s duplicate flag(s)\n", ansi_bold(), ansi_reset());
}

void warn_too_many_input_files(void) {
    printf("%s(bearc)%s too many input file(s) provided\n", ansi_bold(), ansi_reset());
}

void do_cli_announce_incompatible_flags(void) {
    printf("%s(bearc)%s incompatible flags\n", ansi_bold(), ansi_reset());
}

bool cli_args_otherwise_empty(bearc_args_t* args, cli_flag_e flag) {
    if (args->input_file_name != NULL) {
        return false;
    }
    for (size_t i = 0; i < CLI_FLAG__NUM; i++) {
        if (args->flags[i] == true && i != flag) {
            return false;
        }
    }
    return true;
}

void warn_no_arg_for_import_path(void) {
    printf("%s(bearc)%s no arguments provided for import-path\n", ansi_bold(), ansi_reset());
}

void warn_no_arg_for_output(void) {
    printf("%s(bearc)%s no argument provided for output\n", ansi_bold(), ansi_reset());
}
