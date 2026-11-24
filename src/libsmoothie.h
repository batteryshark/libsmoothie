#pragma once

// Required buffer size for smoothie_resolve out_path (in bytes, including null terminator).
#define LIBSMOOTHIE_MAX_PATH 260

extern "C"{
    // Build a layered shadow filesystem under path_to_root from a mapfile.
    // path_to_persistence may be NULL or "" for ephemeral mode.
    // Returns 1 on success, 0 on failure.
    int smoothie_create(const char* path_to_mapfile, const char* path_to_root, const char* path_to_persistence);

    // Unmount images, migrate writes to persistence (if set at create time), remove temp dirs.
    // Returns 1 on success.
    int smoothie_destroy(const char* path_to_root);

    // Resolve a virtual path to its mapped real path.
    // out_path must point to a buffer of at least LIBSMOOTHIE_MAX_PATH bytes.
    // Returns 0 only if the result would not fit in LIBSMOOTHIE_MAX_PATH; it does not check that the file exists.
    int smoothie_resolve(const char* path_to_root, const char* virtual_path, char* out_path);
}

// Threading and instances:
// libsmoothie keeps process-global state for the active instance.
// Use at most one Smoothie instance per process at a time. Multiple virtual
// drive letters within a single instance (MAP;...;C:\, MAP;...;D:\) are supported.
