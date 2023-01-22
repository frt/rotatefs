# rotatefs
FUSE filesystem that removes the oldest file whenever there is no space left to do some operation.

## Compile with
                                                                                  
    gcc -Wall rotatefs.c `pkg-config fuse --cflags --libs` -lulockmgr -o rotatefs

You must have FUSE (version 2.9) installed to compile rotatefs.

## Usage

    rotatefs [FUSE and mount options] rootDir mountPoint
