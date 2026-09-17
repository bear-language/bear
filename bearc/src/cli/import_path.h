//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef CLI_IMPORT_PATH_H
#define CLI_IMPORT_PATH_H

#include "cli/args.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

bool file_exists_on_import_path(const char* file_name, const char* importer_dir,
                                const bearc_args_t* args);

#ifdef __cplusplus
} // extern "c"
#endif

// cpp only api
#ifdef __cplusplus
#include <filesystem>
#include <optional>

std::optional<std::filesystem::path>
resolve_on_import_path(const std::filesystem::path& file_name,
                       const std::filesystem::path& importer_dir, const bearc_args_t* args);
#endif

#endif
