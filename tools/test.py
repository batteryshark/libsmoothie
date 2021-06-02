import os
import sys
import platform
import ctypes
import tempfile
from pathlib import Path

if platform.system() == "Windows":
    LIB_PATH = "bin/libsmoothie.dll"
elif platform.system() == "Linux":
    LIB_PATH = "bin/libsmoothie.so"
elif platform.system() == "Darwin":
    LIB_PATH = "bin/libsmoothie.dylib"



if __name__ == "__main__":
    try:
        lib = ctypes.CDLL(LIB_PATH)
    except Exception as e:
        print(f"Failed to load library: {e}")
        sys.exit(-1)

    # Bindings
    # int smoothie_resolve(const char* path_to_root, const char* virtual_path, char* out_path)
    smoothie_resolve = lib.smoothie_resolve
    smoothie_resolve.restype = ctypes.c_int32
    smoothie_resolve.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p]

    # int smoothie_destroy(const char* path_to_root);
    smoothie_destroy = lib.smoothie_destroy
    smoothie_destroy.restype = ctypes.c_int32
    smoothie_destroy.argtypes = [ctypes.c_char_p]

    # int smoothie_create(const char* path_to_mapfile, const char* path_to_root, const char* path_to_persistence);
    smoothie_create = lib.smoothie_create
    smoothie_create.restype = ctypes.c_int32
    smoothie_create.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p]


    mnt_root = tempfile.TemporaryDirectory(prefix="lstest_tmp_")
    save_root = tempfile.TemporaryDirectory(prefix="lstest_save_")
    

    path_to_root = ctypes.create_string_buffer(mnt_root.name.encode('utf-8'))
    path_to_save = ctypes.create_string_buffer(save_root.name.encode('utf-8'))
    
    path_to_mapfile = ctypes.create_string_buffer(str(Path("./tools/test.map").resolve()).encode('utf-8'))


    if(not smoothie_create(path_to_mapfile,path_to_root, path_to_save)):
        print("smoothie_create Failed!")
        mnt_root.cleanup()
        save_root.cleanup()
        sys.exit(-1)        

    print("smoothie_create: OK!")
    out_path = ctypes.create_string_buffer(1024)    
    virtual_path = ctypes.create_string_buffer("C:\\teststuff\\test.txt".encode('utf-8'))

    if(not smoothie_resolve(path_to_root, virtual_path, out_path)):
        print("smoothie_resolve Failed!")
    else:
        out_path = out_path[:].decode('utf-8').rstrip('\x00')
        out_path = Path(out_path)
        if not out_path.exists():
            print(f"smoothie_resolve: {out_path}: Does not Exist")
        else:
            print(f"smoothie_resolve: {out_path}: Exists")

    print(f"smoothie_destroy result: {smoothie_destroy(path_to_root)}")

    mnt_root.cleanup()
    save_root.cleanup()