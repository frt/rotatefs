# rotatefs
FUSE filesystem that removes the oldest file whenever there is no space left to do some operation.

## Compile with
                                                                                  
    gcc -Wall rotatefs.c `pkg-config fuse --cflags --libs` -lulockmgr -o rotatefs

You must have FUSE (version 2.9) installed to compile rotatefs.

## Usage

    rotatefs [FUSE and mount options] rootDir mountPoint


[![Flattr this git repo](http://api.flattr.com/button/flattr-badge-large.png)](https://flattr.com/submit/auto?user_id=frteodoro&url=https://github.com/frt/rotatefs&title=rotatefs&tags=github,FUSE,filesystem&category=software)

<a href='https://ko-fi.com/M4M09AL6' target='_blank'><img height='36' style='border:0px;height:36px;' src='https://az743702.vo.msecnd.net/cdn/kofi5.png?v=0' border='0' alt='Buy Me a Coffee at ko-fi.com' /></a>

[![Buy Me a Coffee at ko-fi.com](https://az743702.vo.msecnd.net/cdn/kofi5.png?v=0)](https://ko-fi.com/M4M09AL6)

