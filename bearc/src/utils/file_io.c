//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "utils/file_io.h"
#include "cli/args.h"
#include "utils/ansi_codes.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// check that a file of a specified name exists
bool file_exists(const char* file_name) {
    FILE* file = fopen(file_name, "r");
    if (file) {
        fclose(file);
        return true;
    }
    return false;
}

// helper
int read_file_to_src_buffer(src_buffer_t* buffer, const char* file_name) {
    FILE* file = fopen(file_name, "rb");
    if (!file) {
        return -1;
    }

    // find file size
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }
    long src_len = ftell(file);
    if (src_len < 0) {
        fclose(file);
        return -1;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }
    // allocate buffer (+1 for null terminator in case treating as string)
    size_t size = (size_t)src_len + strlen(file_name) + 2;
    buffer->data = malloc(size);
    if (!buffer->data) {
        fclose(file);
        return -1;
    }
    // read file into buffer
    size_t read_size = fread(buffer->data, 1, (size_t)src_len, file);
    fclose(file);

    if (read_size != (size_t)src_len) {
        free(buffer->data);
        printf("e5\n");
        return -1;
    }
    strncpy(buffer->data + src_len + 1, file_name, strlen(file_name));
    buffer->file_name = buffer->data + src_len + 1;
    // ensure null term for both src and file name
    buffer->data[src_len] = '\0';
    buffer->data[size - 1] = '\0';
    buffer->size = size;
    buffer->src_len = src_len;
    return 0;
}

// returns an src_buffer_t by value, which will need to be destructed by src_buffer_destroy
src_buffer_t src_buffer_from_file_create(const char* file_name) {
    src_buffer_t buffer
        = {.file_name = file_name, .data = NULL, .size = 0, .src_len = 0, .owns_data = true};
    if (read_file_to_src_buffer(&buffer, file_name) < 0) {
        printf("%serror%s: could not read file: %s\n", ansi_bold_red(), ansi_reset(), file_name);
    }
    return buffer;
}

src_buffer_t src_buffer_from_file_createn(const char* file_name, size_t name_len) {
    // get in null-terminated form for proper fopen behavior
    char file_name_nt[CLI_ARGS_MAX_FILE_NAME_LENGTH];
    strncpy(file_name_nt, file_name, name_len);
    file_name_nt[name_len] = '\0';
    src_buffer_t buffer
        = {.file_name = file_name, .data = NULL, .size = 0, .src_len = 0, .owns_data = true};
    if (read_file_to_src_buffer(&buffer, file_name) < 0) {
        printf("%serror%s: could not read file: %s\n", ansi_bold_red(), ansi_reset(), file_name);
    }
    return buffer;
}
src_buffer_t src_buffer_from_string_literal(const char* literal_file_name,
                                            const char* literal_buffer) {
    src_buffer_t buffer = {.file_name = literal_file_name,
                           .data = (char*)literal_buffer,
                           .size = 0,
                           .src_len = strlen(literal_buffer),
                           .owns_data = false};
    return buffer;
}

// destructs an src_buffer_t that was created by src_buffer_from_file_create
void src_buffer_destroy(src_buffer_t* buffer) {
    if (buffer->owns_data) {
        free(buffer->data);
    }
}

// gets ptr to data
const char* src_buffer_get(src_buffer_t* buffer) { return buffer->data; }
