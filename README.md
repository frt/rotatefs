[![Flattr this git repo](http://api.flattr.com/button/flattr-badge-large.png)](https://flattr.com/submit/auto?user_id=frteodoro&url=https://github.com/frt/rotatefs&title=rotatefs&tags=github,FUSE,filesystem&category=software)

# rotatefs
FUSE filesystem that removes the oldest file whenever there is no space left to do some operation.

# Compile with
                                                                                  
    gcc -Wall rotatefs.c `pkg-config fuse --cflags --libs` -lulockmgr -o rotatefs

You must have FUSE (version 2.9) installed to compile rotatefs.

# Usage

    rotatefs [FUSE and mount options] rootDir mountPoint
