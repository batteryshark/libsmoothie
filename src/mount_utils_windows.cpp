// Windows-Specific Disk Mounting
// TODO: Clean this up, make it not terrible 
#define _WIN32_WINNT 0x601
#define INITGUID
#include <windows.h>
#include <stdio.h>
#include <initguid.h>
#include <virtdisk.h>
#include <string>
#include <algorithm>

#ifndef VIRTUAL_STORAGE_TYPE_DEVICE_VHDX
#define VIRTUAL_STORAGE_TYPE_DEVICE_VHDX 3
#endif

#include "mount_utils.h"

DWORD detect_image_type(const char* path_to_image){
    std::string pti = path_to_image;
    std::string image_ext = pti.substr(pti.find_last_of(".") + 1);
    std::transform(image_ext.begin(), image_ext.end(), image_ext.begin(), ::towlower);
    
    if(!strcmp(image_ext.c_str(),"iso")){
        return VIRTUAL_STORAGE_TYPE_DEVICE_ISO;
    }else if(!strcmp(image_ext.c_str(),"vhd")){
        return VIRTUAL_STORAGE_TYPE_DEVICE_VHD;
    }else if(!strcmp(image_ext.c_str(),"vhdx")){
        return VIRTUAL_STORAGE_TYPE_DEVICE_VHDX;
    }
    return 0;
}

int unmount_virtual_disk(HANDLE hDisk, const char* path_to_mntpoint){
    std::string mount_path = path_to_mntpoint;
    if(strcmp(mount_path.substr(mount_path.length()-1,4).c_str(),"\\")){
        mount_path.append("\\");
    }

    if(!DeleteVolumeMountPointA(mount_path.c_str())){
        printf("Failed to Delete Volume Mountpoint\n"); 
        return 0;   
    }
    if(DetachVirtualDisk(hDisk,DETACH_VIRTUAL_DISK_FLAG_NONE,0)){
        printf("DetachVirtualDisk Fail!\n");
        return 0;
    }
    return 1;
}

int mount_virtual_disk(HANDLE hDisk, const char* path_to_mntpoint){
    std::string mount_path = path_to_mntpoint;
    if(strcmp(mount_path.substr(mount_path.length()-1,4).c_str(),"\\")){
        mount_path.append("\\");
    }
    ATTACH_VIRTUAL_DISK_PARAMETERS attachParameters;
    memset(&attachParameters,0x00,sizeof(attachParameters));
    attachParameters.Version = ATTACH_VIRTUAL_DISK_VERSION_1;
    DWORD attach_flags = ATTACH_VIRTUAL_DISK_FLAG_READ_ONLY | ATTACH_VIRTUAL_DISK_FLAG_NO_DRIVE_LETTER | ATTACH_VIRTUAL_DISK_FLAG_PERMANENT_LIFETIME;
    DWORD status = AttachVirtualDisk(hDisk,NULL,(ATTACH_VIRTUAL_DISK_FLAG)attach_flags,0,&attachParameters,0);
    if(status){
        printf("AttachVirtualDisk Failed!\n");
        return 0;
    }
    
    WCHAR vdpp[MAX_PATH] = {0x00};
    ULONG vdpp_size = sizeof(vdpp);
    GetVirtualDiskPhysicalPath(hDisk,&vdpp_size , vdpp);

    WCHAR* device_index_w = NULL;
    DWORD device_index = 0;
    DWORD device_index_type = 0;
    if(wcsstr(vdpp,L"\\\\.\\PhysicalDrive")){
        device_index_w = wcsstr(vdpp,L"\\\\.\\PhysicalDrive") + wcslen(L"\\\\.\\PhysicalDrive");
        device_index = _wtoi(device_index_w);
        device_index_type = FILE_DEVICE_DISK;
    }else if(wcsstr(vdpp,L"\\\\.\\CDROM")){
        device_index_w = wcsstr(vdpp,L"\\\\.\\CDROM") + wcslen(L"\\\\.\\CDROM");
        device_index = _wtoi(device_index_w);
        device_index_type = FILE_DEVICE_CD_ROM;
    }else{
        wprintf(L"[mount_virtual_disk] Failed: Could not Identify Disk Type: %s\n",vdpp);
    }

    char volume_path[MAX_PATH] = {0x00};
    HANDLE hVolume = FindFirstVolumeA(volume_path, sizeof(volume_path));
    if(volume_path[strlen(volume_path)-1] == '\\'){volume_path[strlen(volume_path)-1] = 0x00;}
    BOOL target_volume_found = FALSE;
    while(!target_volume_found){
        STORAGE_DEVICE_NUMBER sdn;
        memset(&sdn,0,sizeof(STORAGE_DEVICE_NUMBER));
        HANDLE cVol = CreateFileA(volume_path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        DWORD bytes_returned = 0;
        if(cVol){
            BOOL res = DeviceIoControl(cVol, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0,&sdn, sizeof(STORAGE_DEVICE_NUMBER),&bytes_returned, NULL);
            CloseHandle(cVol);
            if(res && sdn.DeviceNumber == device_index && sdn.DeviceType == device_index_type){
                target_volume_found = TRUE;
                break;
            }
        }
        if (!FindNextVolumeA(hVolume, volume_path, sizeof(volume_path)) && GetLastError() == ERROR_NO_MORE_FILES){
            FindVolumeClose(hVolume);
            break;
            }
        if(volume_path[strlen(volume_path)-1] == '\\'){volume_path[strlen(volume_path)-1] = 0x00;}
    }
    if(!target_volume_found){
        printf("Target Volume Could not be Found\n");
        return 0;
    }

    volume_path[strlen(volume_path)] = '\\';
    if(!SetVolumeMountPointA(mount_path.c_str(), volume_path)){
        printf("Failed to Set Volume Mountpoint\n");
        return 0;
    }
    return 1;
}

int close_virtual_disk(HANDLE hDev){
    return CloseHandle(hDev);
}

int open_virtual_disk(const char* path_to_image, PHANDLE hDev){
    DWORD device_type = detect_image_type(path_to_image);
    if(!device_type){return 0;}

    wchar_t* wpath_to_image = (wchar_t*)calloc(1,32768);
    MultiByteToWideChar(CP_ACP,0,path_to_image,-1,wpath_to_image,32768);

    OPEN_VIRTUAL_DISK_PARAMETERS openParameters;
    memset(&openParameters, 0, sizeof(openParameters));
    openParameters.Version = OPEN_VIRTUAL_DISK_VERSION_1;

    VIRTUAL_STORAGE_TYPE storageType;
    storageType.DeviceId = device_type;
    storageType.VendorId = VIRTUAL_STORAGE_TYPE_VENDOR_MICROSOFT;
    DWORD access_mask = VIRTUAL_DISK_ACCESS_ATTACH_RO | VIRTUAL_DISK_ACCESS_GET_INFO;

    DWORD status = OpenVirtualDisk(&storageType,wpath_to_image,(VIRTUAL_DISK_ACCESS_MASK)access_mask,OPEN_VIRTUAL_DISK_FLAG_NONE,&openParameters,hDev);
    free(wpath_to_image);
    if(status){
        printf("OpenVirtualDisk Failed: %04X\n",status);
        return 0;
    }    
    return 1;    

}

int mount_image(const char* path_to_image, const char* path_to_mountpoint){
    void* hDisk;
    if(!open_virtual_disk(path_to_image,&hDisk)){return 0;}
    int result = mount_virtual_disk(hDisk,path_to_mountpoint);
    close_virtual_disk(hDisk);
    return result;
}

int unmount_image(const char* path_to_image, const char* path_to_mountpoint){
    void* hDisk;
    if(!open_virtual_disk(path_to_image,&hDisk)){return 0;}
    int result = unmount_virtual_disk(hDisk,path_to_mountpoint);
    close_virtual_disk(hDisk);
    return result;   
}