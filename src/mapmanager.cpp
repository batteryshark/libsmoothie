// Symlink-Based Mapper for the composite layers.
#if _WIN32
#include <windows.h>
#endif

#include <string>
#include <filesystem>
#include "utils.h"

#include "mapmanager.h"


// Platform-Dependent Functionality
// Windows doesn't play well with creating a symlink or detecting one via stdlib for whatever reason...
bool path_is_symlink(const std::filesystem::path& in_path){
    #ifdef _WIN32
        return (bool)(GetFileAttributesA(in_path.string().c_str()) & FILE_ATTRIBUTE_REPARSE_POINT);
    #else
        return std::filesystem::is_symlink(in_path);
    #endif
}

// Symlinks on windows can be up to 32768 characters in length provided they are prepended with '\\\\?\\'
void path_create_symlink(const std::filesystem::path& src_path, const std::filesystem::path& dest_path){
    #if _WIN32
        std::wstring frp = L"\\\\?\\" + dest_path.wstring();             
        CreateSymbolicLinkW(frp.c_str(),src_path.wstring().c_str(),0);
    #else
        std::filesystem::create_symlink(src_path,dest_path);
    #endif    
}

// Separates a path from a given root and attaches a new root to it.
// e.g. C:\\test\\app\\banana\\example.txt, C:\\test\\app, C:\\wat -> C:\\wat\\banana\\example.txt
void rebase_path(const std::filesystem::path& in_path, const std::filesystem::path& old_root, const std::filesystem::path& new_root, std::filesystem::path& rebased_path){
    rebased_path = new_root / std::filesystem::relative(in_path,old_root);
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
void link_node(const std::filesystem::path& map_root, const std::filesystem::path& src_path, const std::string& target_path){
    std::filesystem::path mapped_path = resolve_map_path(map_root,target_path);
    path_create_symlink(src_path,mapped_path);    
}

// Given a root of files and directories, map these items over a given root.
void map_path(const std::filesystem::path& map_root, const std::filesystem::path& src_root, const std::string& virtual_root){
    std::filesystem::path mapped_base = resolve_map_path(map_root,virtual_root);
    
    // Create the Resolved Root if it doesn't exist
    std::filesystem::create_directories(mapped_base);
    
    for (std::filesystem::recursive_directory_iterator i(src_root), end; i != end; ++i){
        std::filesystem::path rebased_path;
        rebase_path(i->path(),src_root,mapped_base,rebased_path);
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
    // Remove the symlinks - if there are any files that were written, move those to persistence
    // or remove them.
    for (std::filesystem::recursive_directory_iterator i(map_root), end; i != end; ++i){
        if (!std::filesystem::is_directory(i->path())){                        
            if(!persist || path_is_symlink(i->path())){            
                std::filesystem::remove(i->path());
                continue;
            }

            std::filesystem::path pfp;
            rebase_path(i->path(),map_root,persistence_root,pfp);
            std::filesystem::create_directories(pfp.parent_path());
            if(std::filesystem::exists(pfp)){
                std::filesystem::remove(pfp);
            }            
            std::filesystem::rename(i->path(),pfp);
        }
    }

    // Remove all Leftover Directories
    for (std::filesystem::recursive_directory_iterator i(map_root), end; i != end; ++i){
        if (is_directory(i->path())){
            std::filesystem::remove_all(i->path());
        }
    }
      
    // Remove all that remains from map
    std::filesystem::remove_all(map_root);
}