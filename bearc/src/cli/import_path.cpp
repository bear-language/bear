//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#include "import_path.h"
#include <filesystem>
#include <optional>
#include <system_error>
extern "C" {

bool file_exists_on_import_path(const char* file_name, const char* importer_dir,
                                const bearc_args_t* args) {
    if (!file_name || !importer_dir) {
        return false;
    }

    std::filesystem::path fn(file_name);
    std::filesystem::path importer;

    if (importer_dir) {
        importer = std::filesystem::path(importer_dir);
    }

    return resolve_on_import_path(fn, importer, args).has_value();
}

} // extern "C"

std::optional<std::filesystem::path>
resolve_on_import_path(const std::filesystem::path& file_name,
                       const std::filesystem::path& importer_dir, const bearc_args_t* args) {
    namespace fs = std::filesystem;
    std::error_code err;

    auto try_resolve = [&](const fs::path& candidate) -> std::optional<fs::path> {
        err.clear();

        if (!fs::is_regular_file(candidate, err) || err) {
            return std::nullopt;
        }

        fs::path canonical = fs::weakly_canonical(candidate, err);
        if (err) {
            return std::nullopt;
        }

        return canonical;
    };

    // check absolute path directly
    if (file_name.is_absolute()) {
        return try_resolve(file_name);
    }

    // check importer directly
    if (!importer_dir.empty()) {
        if (auto r = try_resolve(importer_dir / file_name)) {
            return r;
        }
    }

    // search imported path with first match win
    for (size_t i = 0; i < args->import_path_cnt; ++i) {
        fs::path base(args->import_paths[i]);

        if (auto r = try_resolve(base / file_name)) {
            return r;
        }
    }

    return std::nullopt;
}
