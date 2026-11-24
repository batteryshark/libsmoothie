#include <stdio.h>
#include <string>
#include <vector>
#include <filesystem>

#include "utils.h"
#include "mount_utils.h"

#include "mountmanager.h"

enum class MountType {
    Unknown,
    Image,  // ISO / VHD / VHDX -- mounted via virtdisk
    Raw,    // directory -- no mount needed
    Zip     // extracted via miniz_cpp
};

struct MountEntry {
    std::string image_path;
    std::string mount_point;
};

static std::vector<MountEntry> mount_entries;

// Map an image extension (or a directory) to its mount strategy.
static MountType detect_mount_type(const std::string& path_to_image){
    std::string ext = lowercase(get_path_extension(path_to_image));
    if(ext == "iso" || ext == "vhd" || ext == "vhdx"){return MountType::Image;}
    if(ext == "zip"){return MountType::Zip;}
    if(std::filesystem::is_directory(path_to_image)){return MountType::Raw;}
    return MountType::Unknown;
}


// -- API --

// Mount image_path under mount_path according to its detected type and
// record it for later rollback / save_mounts_conf.
// For Raw (directory) inputs, mount_path is rebound to image_path so the
// caller maps directly off the source tree.
int add_mount(std::filesystem::path& image_path, std::filesystem::path& mount_path){
    MountType mount_type = detect_mount_type(image_path.string());
    if(mount_type == MountType::Unknown){return 0;}

    if(mount_type != MountType::Raw){
        std::filesystem::create_directories(mount_path);
    }

    switch(mount_type){
        case MountType::Image:
            if(!mount_image(image_path.string().c_str(), mount_path.string().c_str())){return 0;}
            break;
        case MountType::Raw:
            mount_path = image_path;
            break;
        case MountType::Zip:
            if(!extract_zip_file(image_path.string(), mount_path.string())){return 0;}
            break;
        case MountType::Unknown:
            return 0;
    }

    mount_entries.push_back({image_path.string(), mount_path.string()});
    return 1;
}

static int remove_mount(std::string& path_to_image, std::string& path_to_mountpoint){
    MountType mount_type = detect_mount_type(path_to_image);
    if(mount_type == MountType::Unknown){return 0;}

    switch(mount_type){
        case MountType::Image:
            if(!unmount_image(path_to_image.c_str(), path_to_mountpoint.c_str())){return 0;}
            std::filesystem::remove_all(path_to_mountpoint);
            break;
        case MountType::Raw:
            // Host directory must not be touched.
            break;
        case MountType::Zip:
            std::filesystem::remove_all(path_to_mountpoint);
            break;
        case MountType::Unknown:
            return 0;
    }
    return 1;
}

// Unmount every entry recorded in this process so far. Used on create failure.
void rollback_mounts(){
    for(auto& entry : mount_entries){
        remove_mount(entry.image_path, entry.mount_point);
    }
    mount_entries.clear();
}

// Write each mounted image and its mountpoint to a config file for
// crash-safe unmount on the next smoothie_destroy.
void save_mounts_conf(const std::string& output_path){
    std::vector<std::string> conf_lines;
    for(auto& entry : mount_entries){
        conf_lines.push_back(entry.image_path + ";" + entry.mount_point);
    }
    write_lines_to_file(conf_lines, output_path);
    // mounts.conf is now the record; the next create in this process starts clean.
    mount_entries.clear();
}

// Read every entry in the mounts config and unmount it.
// Tolerant of empty or malformed lines.
void remove_mounts_from_conf(const std::string& conf_path){
    std::vector<std::string> lines = read_lines_from_file(conf_path);
    for(auto& line : lines){
        if(line.empty()){continue;}
        std::vector<std::string> params = split_string(line, ";");
        if(params.size() < 2){continue;}
        if(!remove_mount(params[0], params[1])){
            printf("Warning: Could not Unmount: %s\n", params[0].c_str());
        }
    }
}
