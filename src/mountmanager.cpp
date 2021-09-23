#include <stdio.h>
#include <string>
#include <vector>
#include <filesystem>

#include "utils.h"
#include "mount_utils.h"

#include "mountmanager.h"

#define MOUNT_UNK 0
#define MOUNT_IMAGE 1
#define MOUNT_RAW 2
#define MOUNT_ZIP 3

typedef struct _MOUNT_ENTRY{
    std::string full_image_path;
    std::string full_mount_point;
}MOUNT_ENTRY,*PMOUNT_ENTRY;

std::vector<PMOUNT_ENTRY> mount_entries;

// Use the extension to figure out what to do with the given image.
unsigned int detect_mount_type(std::string& path_to_image){
    std::string ext = get_path_extension(path_to_image);
    ext = lowercase(ext);
    if(!ext.compare("iso")){return MOUNT_IMAGE;}
    if(!ext.compare("vhd")){return MOUNT_IMAGE;}
    if(!ext.compare("vhdx")){return MOUNT_IMAGE;}
    if(!ext.compare("zip")){return MOUNT_ZIP;}
    if(std::filesystem::is_directory(path_to_image)){return MOUNT_RAW;}
    return MOUNT_UNK;
}


// -- API --
int add_mount(std::string& image_filename, std::filesystem::path& image_path, std::filesystem::path& mount_path){
    // Detect type of image, fail out if not.
    unsigned int mount_type = detect_mount_type(image_filename);
    if(!mount_type){return 0;}

    // Construct Mountpoint Path Only if not RAW
    if(mount_type != MOUNT_RAW){
    std::filesystem::create_directories(mount_path);
    }


    switch(mount_type){
        case MOUNT_IMAGE:
            if(!mount_image(image_path.string().c_str(),mount_path.string().c_str())){return 0;}
            break;
        case MOUNT_RAW:
            // We don't need a dedicated mount path if our image is RAW
            mount_path = image_path;
            break;
        case MOUNT_ZIP:
            if(!extract_zip_file(image_path.string(),mount_path.string())){return 0;}
            break;
    }
    PMOUNT_ENTRY cme = (PMOUNT_ENTRY)calloc(1,sizeof(MOUNT_ENTRY));
    cme->full_image_path = image_path.string();
    cme->full_mount_point = mount_path.string();
    // If we mounted it, we have to log it.
    mount_entries.push_back(cme);

    return 1;
}

int remove_mount(std::string& path_to_image,std::string& path_to_mountpoint){
    // Detect type of image, fail out if not.
    unsigned int mount_type = detect_mount_type(path_to_image);
    if(!mount_type){return 0;}    
    void* hDisk = NULL;
    switch(mount_type){
        case MOUNT_IMAGE:
            if(!unmount_image(path_to_image.c_str(),path_to_mountpoint.c_str())){return 0;}
            std::filesystem::remove_all(path_to_mountpoint);
            break;
        case MOUNT_RAW:
            // We're assuming that nothing will happen to our host raw mountpoint.            
            break;
        case MOUNT_ZIP:
            std::filesystem::remove_all(path_to_mountpoint);
            break;
    }    
    return 1;
}

// If there was an issue mounting, we will unmount what was mounted so far.
void rollback_mounts(){
    for (auto & entry : mount_entries) {
        remove_mount(entry->full_image_path,entry->full_mount_point);        
    }
}

// Write every image mounted along with their mountpoints to a config file for later.
void save_mounts_conf(const std::string& output_path){
    std::vector<std::string> conf_lines;
    for (auto & entry : mount_entries) {
        std::string nline = entry->full_image_path + ";" + entry->full_mount_point;
        conf_lines.push_back(nline);
    }
    write_lines_to_file(conf_lines,output_path);
}

// Read all entries in mount config and unmount them.
void remove_mounts_from_conf(const std::string& conf_path){
    std::vector<std::string> lines = read_lines_from_file(conf_path);
    for (auto & line : lines) {
        std::vector<std::string> params = split_string(line,";");
        if(!remove_mount(params[0],params[1])){
            printf("Warning: Could not Unmount: %s\n",params[0].c_str());
        }
    }        
}

 