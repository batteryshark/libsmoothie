// This is a bullshit testing thing using guestmount.
// TODO: Actually make this not crap.
#include <stdio.h>
#include <string>
#include <filesystem>

#include "mount_utils.h"

int mount_image(const char* path_to_image, const char* path_to_mountpoint){
    std::string cmd = "sudo guestmount -a \""; 
    cmd.append(path_to_image);
    cmd.append("\"  -m /dev/sda2 -r \"");
    cmd.append(path_to_mountpoint);
    cmd.append("\"  -o allow_other");
    system(cmd.c_str());
    return 1;
}

int unmount_image(const char* path_to_image, const char* path_to_mountpoint){
    std::string cmd = "sudo guestunmount \"";
    cmd.append(path_to_mountpoint);
    cmd.append("\"");
    system(cmd.c_str());
    return 1;
}