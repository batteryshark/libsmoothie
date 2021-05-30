#pragma once

extern "C"{
    int smoothie_create(const char* path_to_mapfile, const char* path_to_root, const char* path_to_persistence);
    int smoothie_destroy(const char* path_to_root);
    int smoothie_resolve(const char* path_to_root, const char* virtual_path, char* out_path);
}