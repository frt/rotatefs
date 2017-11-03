#define _XOPEN_SOURCE 600

#include <stdio.h>
#include <string.h>
#include <ftw.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

char oldest_path[PATH_MAX];
time_t oldest_mtime;

int files_traversed = 0;

int save_older (const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
    if (typeflag != FTW_F) return 0;

    if (files_traversed == 0 || sb->st_mtime < oldest_mtime) {
        strcpy(oldest_path, fpath);
        oldest_mtime = sb->st_mtime;
        files_traversed++;
    }

    return 0;
}

int main(int argc, char* argv[])
{
    if (nftw(argv[1], save_older, FOPEN_MAX, FTW_MOUNT | FTW_PHYS) != 0) 
        perror("error ocurred: ");

    printf("oldest file: %s\n", oldest_path);
}
