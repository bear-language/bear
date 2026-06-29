//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef CLI_ARGS_H
#define CLI_ARGS_H

#include <stdbool.h>
#include <stdint.h>

#define CLI_ARGS_MAX_FILE_NAME_LENGTH 1024
#define CLI_ARGS_MAX_FLAG_LENGTH 32
#define CLI_ARGS_MAX_IMPORT_PATH_COUNT 512
#define CLI_ARGS_MAX_IMPORT_FILE_COUNT 512

#ifdef __cplusplus
extern "C" {
#endif

typedef enum cli_flag {
    CLI_FLAG_ERR_UNKNOWN,
    CLI_FLAG_HELP,
    CLI_FLAG_VERSION,
    CLI_FLAG_TOKEN_TABLE,
    CLI_FLAG_PRETTY_PRINT,
    CLI_FLAG_SILENT,
    CLI_FLAG_FILE_GRAPH,
    CLI_FLAG_COMPILE,
    CLI_FLAG_IMPORT_PATH,
    CLI_FLAG_IMPORT_FILE,
    CLI_FLAG_PARSE_ONLY,
    CLI_FLAG_OUTPUT,
    CLI_FLAG_COMPACT_DIAGS,
    CLI_FLAG_TERSE,
    CLI_FLAG_STRICT_SYNTAX,
    CLI_FLAG_ERR_DUPLICATE,
    CLI_FLAG_ERR_FILE_NAME_TOO_LONG,
    CLI_FLAG_ERR_TOO_MANY_INPUT_FILES,
    CLI_FLAG_ERR_NO_ARGUMENT_PROVIDED_TO_IMPORT_PATH,
    CLI_FLAG_ERR_NO_ARGUMENT_PROVIDED_TO_IMPORT_FILE,
    CLI_FLAG_ERR_NO_ARGUMENT_PROVIDED_TO_OUTPUT,
    CLI_FLAG__NUM,
} cli_flag_e;

typedef struct bearc_args {
    char* import_paths[CLI_ARGS_MAX_IMPORT_PATH_COUNT];
    char* import_files[CLI_ARGS_MAX_IMPORT_FILE_COUNT];
    bool flags[CLI_FLAG__NUM];
    char* input_file_name;
    char* output_file_name;
    uint16_t import_path_cnt;
    uint16_t import_file_cnt;
} bearc_args_t;

typedef struct {
    char name[CLI_ARGS_MAX_FLAG_LENGTH];
    cli_flag_e flag;
} cli_flag_long_mapping_t;

extern cli_flag_long_mapping_t cli_flag_long_map[];

bearc_args_t parse_cli_args(int argc, char** argv);

#ifdef __cplusplus
}
#endif

#endif // !CLI_ARGS_H
