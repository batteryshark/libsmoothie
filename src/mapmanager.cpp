// Symlink-Based Mapper for the composite layers.
#if _WIN32
#include <windows.h>
#endif

#include <cstdio>
#include <string>
#include <vector>
#include <filesystem>
#include "utils.h"

#include "mapmanager.h"


// Platform-Dependent Functionality
// Windows doesn't play well with creating a symlink or detecting one via stdlib for whatever reason...
bool path_is_symlink(const std::filesystem::path& in_path){
    #ifdef _WIN32
        DWORD attrs = GetFileAttributesA(in_path.string().c_str());
        return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT);
    #else
        return std::filesystem::is_symlink(in_path);
    #endif
}

// Symlinks on windows can be up to 32768 characters in length provided they are prepended with '\\\\?\\'
// SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE (0x2, Windows 10 Creators Update+) lets
// non-admin users create symlinks when Developer Mode is enabled. Windows before 10 1703
// rejects this flag (ERROR_INVALID_PARAMETER), so links fail there.
// File links only: cleanup_map can't tell a directory link from a real folder on
// MinGW and would empty the linked source directory on destroy.
void path_create_symlink(const std::filesystem::path& src_path, const std::filesystem::path& dest_path){
    #if _WIN32
        #ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
        #define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
        #endif
        DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
        std::wstring frp = L"\\\\?\\" + dest_path.wstring();
        if(!CreateSymbolicLinkW(frp.c_str(), src_path.wstring().c_str(), flags)){
            printf("CreateSymbolicLinkW Failed (%lu): %s\n", GetLastError(), dest_path.string().c_str());
        }
    #else
        std::filesystem::create_symlink(src_path,dest_path);
    #endif    
}

// Separates a path from a given root and attaches a new root to it.
// e.g. C:\\test\\app\\banana\\example.txt, C:\\test\\app, C:\\wat -> C:\\wat\\banana\\example.txt
std::filesystem::path rebase_path(const std::filesystem::path& in_path, const std::filesystem::path& old_root, const std::filesystem::path& new_root){
    std::filesystem::path rebased_path = new_root / std::filesystem::relative(in_path,old_root);
    return rebased_path;
}

// Creates a 'mapped' representation of the absolute path by conversion to a stem prepended to a given root.
// e.g. C:\\app\\hello.exe, C:\\map -> C:\\map\\C\\app\\hello.exe
std::filesystem::path resolve_map_path(const std::filesystem::path& map_root, const std::string& virtual_path){
    // If there's no virtual path given, we assume the map root.
    if(virtual_path.empty()){
        return map_root;
    }

    // Remove any : from the path (Windows Paths)
    std::string node = str_replace(virtual_path, std::string(":"), std::string(""));

    // Appending a path with a node that starts with a directory separator can screw it up.
    // It results in the whole path being reset with the node representing root (which we don't want)
    // Move the node ahead one character if this is the case.
    if ((node.rfind("\\", 0) == 0) || (node.rfind("/", 0) == 0)){
        node = node.substr(1,node.size());
    }    

    return map_root / node;
}

// -- API FUNCTIONS --

// Given a virtual path, resolve to its mapped path and remove if it exists.
void remove_node(const std::filesystem::path& map_root, const std::string& target_path){
    std::filesystem::path mapped_path = resolve_map_path(map_root,target_path);
    if(std::filesystem::exists(mapped_path)){std::filesystem::remove(mapped_path);}
}

// Given a real source file path and virtual path, resolve to its mapped path and create a symbolic link.
// Like map_path, an existing entry at that path is replaced.
void link_node(const std::filesystem::path& map_root, const std::filesystem::path& src_path, const std::string& target_path){
    if(std::filesystem::is_directory(src_path)){
        printf("LINK only takes files: %s\n", src_path.string().c_str());
        return;
    }
    std::filesystem::path mapped_path = resolve_map_path(map_root,target_path);
    if(std::filesystem::exists(mapped_path) || path_is_symlink(mapped_path)){
        std::filesystem::remove(mapped_path);
    }
    path_create_symlink(src_path,mapped_path);
}

// Given a root of files and directories, map these items over a given root.
void map_path(const std::filesystem::path& map_root, const std::filesystem::path& src_root, const std::string& virtual_root){
    std::filesystem::path mapped_base = resolve_map_path(map_root,virtual_root);
    
    // Create the Resolved Root if it doesn't exist
    std::filesystem::create_directories(mapped_base);
    
    for (std::filesystem::recursive_directory_iterator i(src_root,std::filesystem::directory_options::skip_permission_denied), end; i != end; ++i){
        std::filesystem::path rebased_path = rebase_path(i->path(),src_root,mapped_base);
        if (is_directory(i->path())){
            std::filesystem::create_directories(rebased_path);
        }else{
            if(std::filesystem::exists(rebased_path) || path_is_symlink(rebased_path)){
                std::filesystem::remove(rebased_path);
            }
            std::filesystem::create_directories(i->path().parent_path());
            path_create_symlink(i->path(),rebased_path);            
        }
    }    
}

// Given a map root, remove all empty directories and symlinks.
// If a persistence path was specified, move any real files to 
// a cloned path in the persistence root.
void cleanup_map(const std::filesystem::path& map_root,const std::filesystem::path& persistence_root){
    int persist = std::filesystem::exists(persistence_root);

    // Collect file entries before modifying the tree. Mutating the directory
    // during recursive_directory_iterator is implementation-defined.
    std::vector<std::filesystem::path> file_entries;
    for (std::filesystem::recursive_directory_iterator i(map_root,std::filesystem::directory_options::skip_permission_denied), end; i != end; ++i){
        if (!std::filesystem::is_directory(i->path())){
            file_entries.push_back(i->path());
        }
    }

    // Persist real files (or remove them if no persistence root), and remove symlinks.
    for (auto& entry : file_entries){
        if(!persist || path_is_symlink(entry)){
            std::filesystem::remove(entry);
            continue;
        }

        std::filesystem::path pfp = rebase_path(entry,map_root,persistence_root);
        std::filesystem::create_directories(pfp.parent_path());
        if(std::filesystem::exists(pfp)){
            std::filesystem::remove(pfp);
        }
        std::filesystem::rename(entry,pfp);
    }

    // Remove all leftover directories. Collect first for the same reason.
    std::vector<std::filesystem::path> dir_entries;
    for (std::filesystem::recursive_directory_iterator i(map_root,std::filesystem::directory_options::skip_permission_denied), end; i != end; ++i){
        if (is_directory(i->path())){
            dir_entries.push_back(i->path());
        }
    }
    for (auto& dir : dir_entries){
        std::filesystem::remove_all(dir);
    }

    std::filesystem::remove_all(map_root);
}