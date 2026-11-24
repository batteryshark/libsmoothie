#include <stdio.h>
#include <cstring>
#include <vector>
#include <filesystem>

#include "utils.h"
#include "mapmanager.h"
#include "mountmanager.h"

#include "libsmoothie.h"

#if _WIN32
#include <windows.h>
#define EXPORTED __declspec(dllexport)

#ifndef SE_BACKUP_PRIVILEGE
#define SE_BACKUP_PRIVILEGE (17L)
#endif

// Enable SE_BACKUP_PRIVILEGE so symlink walks can traverse restricted
// system directories (e.g. System Volume Information).
static ULONG enable_backup_privilege(){
    if(!ImpersonateSelf(SecurityImpersonation)){
        return GetLastError();
    }
    HANDLE token;
    if(!OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES, TRUE, &token)){
        return GetLastError();
    }
    TOKEN_PRIVILEGES privs;
    privs.PrivilegeCount = 1;
    privs.Privileges[0].Luid.LowPart = SE_BACKUP_PRIVILEGE;
    privs.Privileges[0].Luid.HighPart = 0;
    privs.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED | SE_PRIVILEGE_ENABLED_BY_DEFAULT;
    AdjustTokenPrivileges(token, FALSE, &privs, 0, NULL, NULL);
    CloseHandle(token);
    return GetLastError();
}
#else
#define EXPORTED __attribute__((visibility("default")))
#endif

extern "C"{
    EXPORTED int smoothie_create(const char* path_to_mapfile, const char* path_to_root, const char* path_to_persistence);
    EXPORTED int smoothie_destroy(const char* path_to_root);
    EXPORTED int smoothie_resolve(const char* path_to_root, const char* virtual_path, char* out_path);
}

// Process-global state for the active Smoothie instance. libsmoothie supports
// one active instance per process at a time; init_paths is called from every
// entrypoint to rebind these to the supplied root.
static std::filesystem::path mount_root;
static std::filesystem::path map_root;
static std::filesystem::path mounts_conf_path;
static std::filesystem::path persistence_conf_path;

void init_paths(const char* path_to_root){
    #if _WIN32
    enable_backup_privilege();
    #endif
    // Absolute so the \\?\ link paths and the link targets work from a relative root.
    std::error_code ec;
    std::filesystem::path root = std::filesystem::absolute(path_to_root, ec);
    mount_root = root / std::string("mnt");
    map_root = root / std::string("map");
    mounts_conf_path = root / std::string("mounts.conf");
    persistence_conf_path = root / std::string("persistence.conf");
}

// Roll back any partial mount + map state after a create failure.
void failure_cleanup(){
    rollback_mounts();
    std::filesystem::remove_all(map_root);
    std::filesystem::remove_all(mount_root);
}

// Resolve a content path: literal -> local content root -> $SGCROOT.
// Always absolute: a relative symlink target would resolve against the link's folder.
std::filesystem::path resolve_content_path(const std::filesystem::path& content_root, const std::string& image_filename){
    if(std::filesystem::exists(image_filename)){
        return std::filesystem::absolute(image_filename);
    }
    std::filesystem::path local_path = content_root;
    local_path.append(image_filename);
    if(std::filesystem::exists(local_path)){return std::filesystem::absolute(local_path);}

    if(getenv("SGCROOT")){
        std::filesystem::path global_path = std::string(getenv("SGCROOT"));
        global_path.append(image_filename);
        if(std::filesystem::exists(global_path)){
            return std::filesystem::absolute(global_path);
        }
    }
    return {};
}

// Destroy an existing smoothie instance, moving to persistence if a
// persistence root was set at create time.
int smoothie_destroy(const char* path_to_root){
    init_paths(path_to_root);

    // Read persistence root (empty if persistence.conf does not exist).
    std::filesystem::path persistence_root = read_file(persistence_conf_path.string());

    // Read mounts.conf and unmount each entry.
    remove_mounts_from_conf(mounts_conf_path.string());

    std::filesystem::remove_all(mount_root);

    // Remove map root - migrate real files to persistence if requested.
    cleanup_map(map_root, persistence_root);

    std::filesystem::remove(persistence_conf_path);
    std::filesystem::remove(mounts_conf_path);

    return 1;
}

// Given a map configuration, path to a root, and optional persistence root,
// create a layered composite filesystem.
int smoothie_create(const char* path_to_mapfile, const char* path_to_root, const char* path_to_persistence){
    init_paths(path_to_root);

    std::filesystem::create_directories(mount_root);
    std::filesystem::create_directories(map_root);

    // If a previous instance was not destroyed (or crashed), tear it down first.
    if(std::filesystem::exists(mounts_conf_path)){
        printf("Root Already Mounted - Unmounting First...\n");
        smoothie_destroy(path_to_root);
    }

    std::error_code ec;
    std::filesystem::path mapfile_path = std::filesystem::absolute(path_to_mapfile, ec);
    std::filesystem::path content_path = mapfile_path.parent_path();
    if(!std::filesystem::exists(mapfile_path)){
        printf("Mapfile does not Exist!\n");
        failure_cleanup();
        return 0;
    }

    std::vector<std::string> mapfile_lines = read_lines_from_file(mapfile_path.string());
    for(auto& line : mapfile_lines){
        if(line.empty() || line[0] == '#'){continue;}

        std::vector<std::string> params = split_string(line, ";");
        // Fallback: tab-delimited if no semicolons were found.
        if(params.size() == 1){params = split_string(line, "\t");}

        const std::string& command = params[0];

        if(command == "MAP"){
            if(params.size() != 3){
                printf("MAP requires 2 arguments (image;virtual_path): %s\n", line.c_str());
                continue;
            }
            std::filesystem::path full_image_path = resolve_content_path(content_path, params[1]);
            if(full_image_path.empty()){
                printf("Could not Resolve Image Path: %s\n", params[1].c_str());
                failure_cleanup();
                return 0;
            }
            std::filesystem::path mount_path = mount_root / std::filesystem::path(params[1]).stem();
            if(!add_mount(full_image_path, mount_path)){
                printf("Failed to Mount: %s\n", params[1].c_str());
                failure_cleanup();
                return 0;
            }
            map_path(map_root, mount_path, params[2]);
        }else if(command == "REMOVE"){
            if(params.size() != 2){
                printf("REMOVE requires 1 argument (virtual_path): %s\n", line.c_str());
                continue;
            }
            remove_node(map_root, params[1]);
        }else if(command == "LINK"){
            if(params.size() != 3){
                printf("LINK requires 2 arguments (source;virtual_path): %s\n", line.c_str());
                continue;
            }
            std::filesystem::path full_item_path = resolve_content_path(content_path, params[1]);
            if(full_item_path.empty()){
                printf("Could not Resolve Link Source: %s\n", params[1].c_str());
                continue;
            }
            link_node(map_root, full_item_path, params[2]);
        }else{
            printf("Unrecognized Command: %s\n", line.c_str());
        }
    }

    save_mounts_conf(mounts_conf_path.string());

    // If persistence is set: create it, overlay any existing contents into the
    // map root, and record the path so smoothie_destroy can migrate writes back.
    if(path_to_persistence && strlen(path_to_persistence)){
        std::filesystem::path persistence_root = std::filesystem::absolute(path_to_persistence);
        std::filesystem::create_directories(persistence_root);
        map_path(map_root, persistence_root, std::string(""));
        write_string_to_file(persistence_root.string(), persistence_conf_path.string());
    }

    return 1;
}

// Resolve a virtual path to its mapped real path on disk.
int smoothie_resolve(const char* path_to_root, const char* virtual_path, char* out_path){
    init_paths(path_to_root);
    std::filesystem::path mapped_path = resolve_map_path(map_root, std::string(virtual_path));
    if(mapped_path.empty()){return 0;}

    const std::string resolved = mapped_path.string();
    if(resolved.length() >= LIBSMOOTHIE_MAX_PATH){return 0;}
    strcpy(out_path, resolved.c_str());
    return 1;
}
