# libsmoothie

A small C++17 Windows DLL that builds a layered filesystem out of disk images,
ZIPs and plain directories, using nothing but mounts and symbolic links.

This is the native implementation of Smoothie from part two of my VX write-up,
[Masterpiece - Implementing a Layered Filesystem](https://batteryshark.github.io/2021/10/25/masterpiece-implementing-a-layered-filesystem-part-two.html).
[Smoothie.NET](https://github.com/batteryshark/Smoothie.NET) is the same idea in C#.

**Status: Windows only, x86-64 only, needs an elevated process. It builds with
MinGW-w64; this version has not been run on Windows yet. The Linux backend the
write-up mentions is gone. Details under [Status and limits](#status-and-limits).**

## Why this exists

VX packages a PC game the way a console would: read-only content images, a
description of where each one goes, and a separate place for anything the game
writes. To run one, something has to turn "this ZIP is `C:\`, this ISO is `D:\`,
and this one file replaces that one" into a directory tree a normal process can
open files from.

I didn't want a kernel driver for that, and I didn't want to keep a virtual
layout in the game's memory where no other process could see it. Windows can
already mount images and create symlinks, so Smoothie just uses those. The
result is a real folder on disk. Anything can read it, and you can look at it
in Explorer when something goes wrong.

## How it works

You give it a map file, a working root and (optionally) a persistence
directory. `smoothie_create` reads the map top to bottom. Each image gets
mounted under `mnt\`: ISO, VHD and VHDX are attached read-only through the
VirtDisk API with no drive letter and mounted onto a folder, a ZIP is extracted
with miniz_cpp, and a plain directory is used where it sits. Then every file in
that layer gets a symlink under `map\`, with the drive letter turned into a
folder (`C:\Windows\foo.dll` becomes `map\C\Windows\foo.dll`). Later lines win:
a file that already has a link gets relinked to the newer layer, and a `LINK`
replaces whatever is at its path. If you passed a persistence directory,
whatever is already in it goes on top last.

The working root ends up like this:

```text
<root>\
  map\
    C\              virtual C:\
    D\              virtual D:\
  mnt\
    example\        example.zip, extracted
    exiso\          exiso.iso, mounted here
    windows\        windows.vhdx, mounted here
  mounts.conf       what got mounted, one "image;mountpoint" per line
  persistence.conf  the persistence directory, if one was given
```

`smoothie_destroy` reads `mounts.conf`, unmounts everything, and walks `map\`.
Symlinks are deleted. Real files are whatever got written during the run, and
they are moved into the persistence directory (or deleted, if there isn't one).
The two `.conf` files live in the root so a run that crashed after
`smoothie_create` returned can still be cleaned up: the next `smoothie_create`
on the same root finds `mounts.conf` and tears the old instance down first.
`mounts.conf` is only written once the whole map is done, so a crash inside
`smoothie_create` leaves attached ISO/VHD images behind with no record. Detach
them in Disk Management before trying again.

libsmoothie does not intercept anything. It doesn't make a process believe that
`<root>\map\C` is `C:\`, and it doesn't replace a link with a real file when the
app wants to write. In VX that's the job of the redirection layer loaded into
the process ([pdx](https://github.com/batteryshark/pdx), part three of the
write-up). libsmoothie only builds the tree and sorts links from real files at
the end.

## Map format

One command per line, semicolon-delimited. A line with no semicolons is split
on tabs instead. A line that starts with `#` is a comment (whole lines only, a
trailing `#` is not stripped). Lines with the wrong number of arguments are
skipped with a warning.

```text
# MAP;image;virtual_path      lay an image, ZIP or directory over a virtual path
MAP;example.zip;C:\
MAP;exiso.iso;D:\
MAP;windows.vhdx;C:\Windows

# LINK;source_file;virtual_path      put one file at a virtual path, replacing what's there
LINK;somefile_example.txt;C:\app\somefile.txt

# REMOVE;virtual_path      drop one file mapped by earlier lines (a later MAP can bring it back)
REMOVE;C:\app\unwanted.dll
```

The write-up shows `MAP` and `LINK` and only mentions deleting; `REMOVE` is the
verb for that. `LINK` takes a file, never a directory: a directory source is
skipped with a warning, because teardown can't tell a directory link from a real
folder and would empty the linked one. Lay a directory over a path with `MAP`.

An image name is looked up as given, then next to the map file, then in the
directory named by the `SGCROOT` environment variable. The type comes from the
extension: `iso`, `vhd`, `vhdx`, `zip`, and anything else has to be a directory.

## API

```c
// out_path buffer must be at least this many bytes.
#define LIBSMOOTHIE_MAX_PATH 260

// Build the layered tree under path_to_root from a map file.
// path_to_persistence may be NULL or "" to throw writes away.
// Returns 1 on success, 0 on failure (partial mounts are rolled back).
int smoothie_create(const char* path_to_mapfile,
                    const char* path_to_root,
                    const char* path_to_persistence);

// Unmount, move real files to persistence if one was set, remove the tree.
int smoothie_destroy(const char* path_to_root);

// Translate a virtual path (C:\app\game.exe) to its path under the map root.
// Returns 0 if the result would not fit in LIBSMOOTHIE_MAX_PATH.
int smoothie_resolve(const char* path_to_root,
                     const char* virtual_path,
                     char* out_path);
```

`smoothie_resolve` is a path translation. It doesn't check that the file exists.
Relative paths are made absolute against the current directory before any link
is created.

`src/libsmoothie.h` wraps the declarations in a bare `extern "C"` block, so
include it from C++. From C or anything else, declare the three functions
yourself; they are plain C exports.

```cpp
#include "libsmoothie.h"

char real[LIBSMOOTHIE_MAX_PATH];

if (!smoothie_create("C:\\apps\\demo\\smoothie.map", "C:\\work\\demo", "C:\\saves\\demo"))
    return 1;

if (smoothie_resolve("C:\\work\\demo", "C:\\app\\game.exe", real))
    printf("%s\n", real);   // C:\work\demo\map\C\app\game.exe

// ... run the app against the map root ...

smoothie_destroy("C:\\work\\demo");
```

## Build

You need MinGW-w64 (`x86_64-w64-mingw32-g++`) and make. No Visual Studio and no
Windows SDK: MinGW-w64 ships the virtdisk import library.

```text
mingw32-make -f Makefile.win      on Windows
make -f Makefile.win              cross-building from Linux or macOS
```

The output is `bin/libsmoothie.dll`, with libstdc++ linked in statically.

## Try it

`tools/test.py` is a smoke test over ctypes. It maps `tools/test.zip` at
`C:\teststuff` in a temporary root, resolves `C:\teststuff\test.txt`, checks the
link is there and tears everything down. It prints PASS or FAIL and exits
non-zero on failure. Run it from the repository root in an elevated prompt,
with a 64-bit Python:

```text
python tools\test.py
```

`example/sample.map` is the map from the write-up. It shows the syntax; the
images it names are not in this repository.

## What's inside

| Path | What it does |
|---|---|
| `src/libsmoothie.cpp`, `.h` | The three exports, the map file parser, image lookup |
| `src/mapmanager.cpp` | The symlink tree: map, link, remove, resolve, teardown |
| `src/mountmanager.cpp` | Picks a mount strategy per layer, records mounts, rolls back on failure |
| `src/mount_utils_windows.cpp` | VirtDisk attach and detach, volume mount points |
| `src/utils.cpp` | String and file helpers, ZIP extraction |
| `src/zip_file.hpp` | Vendored miniz_cpp (see Credits) |
| `example/`, `tools/` | Sample map and the smoke test |

## Status and limits

- **Windows only, x86-64 only.** The target is Windows 10 and 11. ISO and VHDX
  attach need Windows 8 or later, and the symlink call passes the
  unprivileged-create flag that arrived in Windows 10 1703, so on anything
  older every link fails. The Makefile only builds 64-bit.
- **No Linux.** The write-up says Linux uses libguestfs. That backend shelled
  out to `guestmount` and never worked properly, so I removed it. A few
  `#else` branches for other platforms are still in the source; nothing builds
  them.
- **It needs elevation.** Attaching a virtual disk and setting a volume mount
  point are administrator operations. Symlinks need either elevation or
  Developer Mode. The library also turns on `SeBackupPrivilege` for the calling
  thread so it can walk protected folders inside a mounted image.
- **One instance per process.** The list of mounts and the active paths are
  process-global, so use one Smoothie root per process at a time. Several drive
  letters in one map are fine.
- **ZIP layers are extracted in full on every create.** Fine for small layers
  and patches. For big content use an ISO or a VHDX, which mount in place.
- **Mount folders are named after the image's file name without its extension**,
  so `data.zip` and `data.iso` in the same map collide.
- **Stick to ASCII paths.** Paths cross the API as narrow `char*` strings and
  the code mixes ANSI Win32 calls with `std::filesystem`.
- **`smoothie_resolve` output is capped at 260 bytes**, even though the links
  themselves are created with the `\\?\` prefix and can go deeper.
- **Errors are mostly a `printf` line plus a 0 return**, with no error codes.
  Not all of them, though: a `std::filesystem` failure (for example `REMOVE`
  on a folder that isn't empty) throws through the C export and takes the
  host process down. A `LINK` whose source can't be found is skipped with a
  warning; one whose target folder no `MAP` created prints the failed symlink
  call. Either way `smoothie_create` still returns 1. A failed symlink only
  prints a line: without elevation or Developer Mode, `smoothie_create` still
  returns 1, and `map\` has the folders but none of the files.

Two things that look odd and are on purpose: the ZIP reader is vendored as a
single header so the build has no dependencies to fetch, and state is kept in
two flat text files in the root instead of anywhere cleverer, because you can
read them when a run dies halfway.

## Where it fits in VX

- [vxlauncher](https://github.com/batteryshark/vxlauncher), the native launcher,
  loads `libsmoothie.dll` at runtime to build the tree for a `.vxapp` (the
  package carries a `smoothie.map`).
- [Smoothie.NET](https://github.com/batteryshark/Smoothie.NET) is the C# port,
  used by [VXTools.NET](https://github.com/batteryshark/VXTools.NET).
- [pdx](https://github.com/batteryshark/pdx) is what makes a process see the
  map root as its drives.
- Pointing the persistence directory at a synced folder is how the write-up
  gets cloud saves (part six).

## Credits

ZIP reading is [miniz_cpp](https://github.com/tfussell/miniz-cpp) by Thomas
Fussell (MIT), vendored as `src/zip_file.hpp`. It embeds miniz by Rich
Geldreich (public domain, unlicense). Both notices are kept in that file.
