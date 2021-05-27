# libsmoothie
A composition utility for Linux/Windows that takes a list of instructions and creates a virtual layered shadow filesystem with optional persistence. It effectively lets you take a collection of files and build your own VFS.

## How to Use
1. Define your Layers: These can be content such as ISOs, VHDs, VHDXs, ZIPs, or other root directories.
2. Create a config file to tell smoothie how the layered filesystems should be composited. Config files are semicolon-delimited and support the following commands at present:
- MAP: Map an image into our virtual space, it will be mounted if not already. 
Example: 
```
# This will make the contents of base_windows.zip be located at SROOT\C\Windows
MAP;base_windows.zip;C:\Windows 
```
- REMOVE: Remove a virtual path from our space. This is useful if mounting images where changes involve deleting files.
Example:
```
REMOVE;C:\Games\MyGame\somethingtodelete.dll
```

- LINK: Link an individual file to a virtual target path in our map. Works much like a raw mount except without mounting. Use this to preload configurations quickly.
Example:
```
LINK;somefile_example.txt;C:\path\somefile.txt
```

For more details, reference "sample.map" in the example directory.

3. Place your images into a directory to reference. Optionally, a global "content directory" can be set by setting the envar "SGCROOT". This is useful for looking up images that may be used by multiple instances.

4. Run smoothie to create the layered instance like so:
```
int smoothie_create(const char* path_to_mapfile, const char* path_to_root, const char* path_to_persistence)
```

If changes are to be saved, be sure to include a persistence path.


5. When finished, call destroy:
```
int smoothie_destroy(const char* path_to_root)
```

If a persistence directory (optional) at the end was included, upon destruction, any changed files will be pushed to that persistence root to be reloaded on the next 'create' where that directory is given.


