#include <stdio.h>
#include <cstring>
#include <vector>
#include <filesystem>

#include "utils.h"
#include "mapmanager.h"
#include "mountmanager.h"

#include "libsmoothie.h"

#if _WIN32
#define EXPORTED __declspec(dllexport)
#else
#define EXPORTED __attribute__((visibility("default")))
#endif

extern "C"{
    EXPORTED int smoothie_create(const char* path_to_mapfile, const char* path_to_root, const char* path_to_persistence);
    EXPORTED int smoothie_destroy(const char* path_to_root);
}

static std::filesystem::path mount_root;
static std::filesystem::path map_root;
static std::filesystem::path mounts_conf_path;
static std::filesystem::path persistence_conf_path;

void init_paths(const char* path_to_root){
    std::filesystem::path root = path_to_root;
    mount_root = root / std::string("mnt");
    map_root = root / std::string("map");
    mounts_conf_path = root / std::string("mounts.conf");
    persistence_conf_path = root / std::string("persistence.conf");
}

// Rolling back in case something went wrong.
void failure_cleanup(std::filesystem::path& mount_root, std::filesystem::path& map_root){
    rollback_mounts();
    std::filesystem::remove_all(map_root);
    std::filesystem::remove_all(mount_root);
}

// Resolve a content path based on local or global root.
std::filesystem::path resolve_content_path(const std::filesystem::path& content_root ,const std::string& image_filename){

    std::filesystem::path local_path = content_root;
    local_path.append(image_filename);
    if(std::filesystem::exists(local_path)){return local_path;}

    if(getenv("SGCROOT")){
        std::filesystem::path global_path = std::string(getenv("SGCROOT"));
        global_path.append(image_filename);
        
        if(std::filesystem::exists(global_path)){
            return global_path;
        }
    }
    std::filesystem::path nopath = "";
    return nopath;
}

// Destroy an existing smoothie instance, moving to persistence if necessary.
int smoothie_destroy(const char* path_to_root){
    init_paths(path_to_root);
    
    // Read given Configuration Files
    std::filesystem::path persistence_root = read_file(persistence_conf_path.string());

    // Read the mounts that were mounted and unmount them
    remove_mounts_from_conf(mounts_conf_path.string());

    // Remove Mounts Directory
    std::filesystem::remove_all(mount_root);

    // Remove contents of map root - persist migrate if necessary.
    cleanup_map(map_root,persistence_root);
    
    // Remove Persistence Config
    std::filesystem::remove(persistence_conf_path);

    // Remove Mounts Config
    std::filesystem::remove(mounts_conf_path);

    return 1;
}

// Given a map configuration, path to a root, and optional persistence root, create a layered compositefs.
int smoothie_create(const char* path_to_mapfile, const char* path_to_root, const char* path_to_persistence){
    init_paths(path_to_root);

    // Create our mount and map roots if they don't exist already.
    std::filesystem::create_directories(mount_root);
    std::filesystem::create_directories(map_root);

    // If this smoothie is live, we need to kill it before proceeding.
    if(std::filesystem::exists(mounts_conf_path)){
        printf("Root Already Mounted - Unmounting First...\n");
        smoothie_destroy(path_to_root);    
    }

    // Read our Mapfile - Figure out what we have to do.
    std::filesystem::path mapfile_path = path_to_mapfile;
    std::filesystem::path content_path = mapfile_path.parent_path();
    if(!std::filesystem::exists(mapfile_path)){
        printf("Mapfile does not Exist!\n");
        failure_cleanup(mount_root,map_root);
        return 0;
    }

    std::vector<std::string> mapfile_lines  = read_lines_from_file(mapfile_path.string());
    for (auto & line : mapfile_lines) {
        std::vector<std::string> params = split_string(line,";");
        // Temporary compatibility fallback for tab-delimited maps.
        if(params.empty()){std::vector<std::string> params = split_string(line,"\t");}
        if(params.size() != 2 && params.size() != 3){continue;}

        // MAP;image_name.ext;virtual_path
        if(!params[0].compare("MAP")){
            // Resolve Image Path (from global or local content root)
            std::filesystem::path full_image_path = resolve_content_path(content_path, params[1]);
            if(full_image_path.empty()){
                printf("Could not Resolve Image Path: %s\n",params[1].c_str());
                return 0;
            }
            std::filesystem::path mount_path = mount_root / std::filesystem::path(params[1]).stem();
            if(!add_mount(params[1], full_image_path, mount_path)){
                printf("Failed to Mount: %s\n", params[1].c_str());
                failure_cleanup(mount_root,map_root);
            }
            // We now have to merge our changes onto the composite.
            map_path(map_root,mount_path,params[2]);
        }else if(!params[0].compare("REMOVE")){
            remove_node(map_root,params[1]);
        }else if(!params[0].compare("LINK")){
            std::filesystem::path full_item_path = resolve_content_path(content_path, params[1]);
            link_node(map_root,full_item_path,params[2]);
        }else{
            printf("Unrecognized Command: %s\n", line.c_str());
        }

    }

    // We're ready to write our mounts config file now.
    save_mounts_conf(mounts_conf_path.string());

    // If we specified a persistence root:
    // Create if it didn't exist, map what existed already, and write the path to our config.
    if(strlen(path_to_persistence)){
        std::filesystem::path persistence_root = path_to_persistence;
        std::filesystem::create_directories(persistence_root);
        map_path(map_root,persistence_root,std::string(""));
        write_string_to_file(persistence_root.string(),persistence_conf_path.string());
    }

    return 1;
}



