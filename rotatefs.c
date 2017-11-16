/** @file
 *
 * Compile with:
 *
 *     gcc -Wall rotatefs.c `pkg-config fuse --cflags --libs` -lulockmgr -o rotatefs
 *
 */

#define _FILE_OFFSET_BITS 64
#define FUSE_USE_VERSION 29

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE
#define _XOPEN_SOURCE 600

#include <fuse.h>

#ifdef HAVE_LIBULOCKMGR
#include <ulockmgr.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif
#include <sys/file.h> /* flock(2) */

#include <ftw.h>
#include <limits.h>
#include <sys/types.h>

struct rfs_state {
    char *rootdir;
    char oldest_path[PATH_MAX];
    int files_traversed;
    time_t oldest_mtime;
};
#define RFS_DATA ((struct rfs_state *) fuse_get_context()->private_data)

int save_older (const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{

    if (typeflag != FTW_F) return 0;

    if (RFS_DATA->files_traversed == 0 || sb->st_mtime < RFS_DATA->oldest_mtime) {
        strcpy(RFS_DATA->oldest_path, fpath);
        RFS_DATA->oldest_mtime = sb->st_mtime;
        RFS_DATA->files_traversed++;
    }

    return 0;
}

int delete_oldest()
{
    int res;

    if (nftw(RFS_DATA->rootdir, save_older, FOPEN_MAX, FTW_MOUNT | FTW_PHYS) != 0) {
        perror("error ocurred: ");
        return -errno;
    }

    res = unlink(RFS_DATA->oldest_path);
    if (res == -1)
        return -errno;

    /* after the file deleted, will need to search for the oldest file again. */
    RFS_DATA->files_traversed = 0;

    return 0;
}

size_t device_size()
{
    int res;
    size_t fsize;
    struct statvfs *stbuf = malloc(sizeof(struct statvfs));

    res = statvfs(RFS_DATA->oldest_path, stbuf);
    fsize = stbuf->f_bsize * stbuf->f_blocks;
    free(stbuf);
    if (res == -1) {
        return -errno;
    }

    return fsize;
}

//  All the paths I see are relative to the root of the mounted
//  filesystem.  In order to get to the underlying filesystem, I need to
//  have the mountpoint.  I'll save it away early on in main(), and then
//  whenever I need a path for something I'll call this to construct
//  it.
static void fullpath(char fpath[PATH_MAX], const char *path)
{
    strcpy(fpath, RFS_DATA->rootdir);
    strncat(fpath, path, PATH_MAX); // ridiculously long paths will
				    // break here
}

static void *xmp_init(struct fuse_conn_info *conn)
{
	(void) conn;

	return RFS_DATA;
}

static int xmp_getattr(const char *path, struct stat *stbuf)
{
	int res;
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	res = lstat(fpath, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_fgetattr(const char *path, struct stat *stbuf,
			struct fuse_file_info *fi)
{
	int res;

	(void) path;

	res = fstat(fi->fh, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_access(const char *path, int mask)
{
	int res;
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	res = access(fpath, mask);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_readlink(const char *path, char *buf, size_t size)
{
	int res;
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	res = readlink(fpath, buf, size - 1);
	if (res == -1)
		return -errno;

	buf[res] = '\0';
	return 0;
}

struct xmp_dirp {
	DIR *dp;
	struct dirent *entry;
	off_t offset;
};

static int xmp_opendir(const char *path, struct fuse_file_info *fi)
{
	int res;
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	struct xmp_dirp *d = malloc(sizeof(struct xmp_dirp));
	if (d == NULL)
		return -ENOMEM;

	d->dp = opendir(fpath);
	if (d->dp == NULL) {
		res = -errno;
		free(d);
		return res;
	}
	d->offset = 0;
	d->entry = NULL;

	fi->fh = (unsigned long) d;
	return 0;
}

static inline struct xmp_dirp *get_dirp(struct fuse_file_info *fi)
{
	return (struct xmp_dirp *) (uintptr_t) fi->fh;
}

static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
	struct xmp_dirp *d = get_dirp(fi);

	(void) path;
	if (offset != d->offset) {
		seekdir(d->dp, offset);
		d->entry = NULL;
		d->offset = offset;
	}
	while (1) {
		struct stat st;
		off_t nextoff;

		if (!d->entry) {
			d->entry = readdir(d->dp);
			if (!d->entry)
				break;
		}

		memset(&st, 0, sizeof(st));
		st.st_ino = d->entry->d_ino;
		st.st_mode = d->entry->d_type << 12;
		nextoff = telldir(d->dp);
		if (filler(buf, d->entry->d_name, &st, nextoff))
			break;

		d->entry = NULL;
		d->offset = nextoff;
	}

	return 0;
}

static int xmp_releasedir(const char *path, struct fuse_file_info *fi)
{
	struct xmp_dirp *d = get_dirp(fi);
	(void) path;
	closedir(d->dp);
	free(d);
	return 0;
}

static int xmp_mknod(const char *path, mode_t mode, dev_t rdev)
{
	int res;
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	if (S_ISFIFO(mode))
		res = mkfifo(fpath, mode);
	else
		res = mknod(fpath, mode, rdev);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_mkdir(const char *path, mode_t mode)
{
	int res;
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	res = mkdir(fpath, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_unlink(const char *path)
{
	int res;
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	res = unlink(fpath);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_rmdir(const char *path)
{
	int res;
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	res = rmdir(fpath);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_symlink(const char *from, const char *to)
{
	int res;
        char ffrom[PATH_MAX];
        char fto[PATH_MAX];

        fullpath(ffrom, from);
        fullpath(fto, to);
	res = symlink(ffrom, fto);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_rename(const char *from, const char *to)
{
	int res;
        char ffrom[PATH_MAX];
        char fto[PATH_MAX];

        fullpath(ffrom, from);
        fullpath(fto, to);
	res = rename(ffrom, fto);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_link(const char *from, const char *to)
{
	int res;
        char ffrom[PATH_MAX];
        char fto[PATH_MAX];

        fullpath(ffrom, from);
        fullpath(fto, to);
	res = link(ffrom, fto);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_chmod(const char *path, mode_t mode)
{
	int res;
        char fpath[PATH_MAX];

        fullpath(fpath, path);
        res = chmod(fpath, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_chown(const char *path, uid_t uid, gid_t gid)
{
	int res;
        char fpath[PATH_MAX];

        fullpath(fpath, path);
        res = lchown(fpath, uid, gid);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_truncate(const char *path, off_t size)
{
	int res;
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	res = truncate(fpath, size);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_ftruncate(const char *path, off_t size,
			 struct fuse_file_info *fi)
{
	int res;

	(void) path;

	res = ftruncate(fi->fh, size);
	if (res == -1)
		return -errno;

	return 0;
}

#ifdef HAVE_UTIMENSAT
static int xmp_utimens(const char *path, const struct timespec ts[2],
		       struct fuse_file_info *fi)
{
	int res;
        char fpath[PATH_MAX];

	/* don't use utime/utimes since they follow symlinks */
	if (fi)
            res = futimens(fi->fh, ts);
        else {
            fullpath(fpath, path);
            res = utimensat(0, path, ts, AT_SYMLINK_NOFOLLOW);
        }
	if (res == -1)
            return -errno;

	return 0;
}
#endif

static int xmp_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	int fd;
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	fd = open(fpath, fi->flags, mode);
	if (fd == -1)
		return -errno;

	fi->fh = fd;
	return 0;
}

static int xmp_open(const char *path, struct fuse_file_info *fi)
{
	int fd;
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	fd = open(fpath, fi->flags);
	if (fd == -1)
		return -errno;

	fi->fh = fd;
	return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
	int res;

	(void) path;
	res = pread(fi->fh, buf, size, offset);
	if (res == -1)
		res = -errno;

	return res;
}

static int xmp_read_buf(const char *path, struct fuse_bufvec **bufp,
			size_t size, off_t offset, struct fuse_file_info *fi)
{
	struct fuse_bufvec *src;

	(void) path;

	src = malloc(sizeof(struct fuse_bufvec));
	if (src == NULL)
		return -ENOMEM;

	*src = FUSE_BUFVEC_INIT(size);

	src->buf[0].flags = FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK;
	src->buf[0].fd = fi->fh;
	src->buf[0].pos = offset;

	*bufp = src;

	return 0;
}

static int xmp_write(const char *path, const char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi)
{
	int res;

	(void) path;

        for (res = pwrite(fi->fh, buf, size, offset); res == -1 && errno == ENOSPC; res = pwrite(fi->fh, buf, size, offset)) {
            fprintf(stderr, "device_size: %ld; size: %ld\n", device_size(), size);
            if (device_size() < size || delete_oldest() != 0) {
                break;
            }
        }
	
	if (res == -1)
		res = -errno;

	return res;
}

static int xmp_write_buf(const char *path, struct fuse_bufvec *buf,
		     off_t offset, struct fuse_file_info *fi)
{
	struct fuse_bufvec dst = FUSE_BUFVEC_INIT(fuse_buf_size(buf));
        int res;

	(void) path;

	dst.buf[0].flags = FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK;
	dst.buf[0].fd = fi->fh;
	dst.buf[0].pos = offset;

        for (res = fuse_buf_copy(&dst, buf, FUSE_BUF_SPLICE_NONBLOCK); res == -ENOSPC; res = fuse_buf_copy(&dst, buf, FUSE_BUF_SPLICE_NONBLOCK)) {
            fprintf(stderr, "device_size: %ld; fuse_buf_size(buf): %ld\n", device_size(), fuse_buf_size(buf));
            if (device_size() < fuse_buf_size(buf) || delete_oldest() != 0) {
                break;
            }
        }

	return res;
}

static int xmp_statfs(const char *path, struct statvfs *stbuf)
{
	int res;
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	res = statvfs(fpath, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_flush(const char *path, struct fuse_file_info *fi)
{
	int res;

	(void) path;
	/* This is called from every close on an open file, so call the
	   close on the underlying filesystem.	But since flush may be
	   called multiple times for an open file, this must not really
	   close the file.  This is important if used on a network
	   filesystem like NFS which flush the data/metadata on close() */
	res = close(dup(fi->fh));
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_release(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	close(fi->fh);

	return 0;
}

static int xmp_fsync(const char *path, int isdatasync,
		     struct fuse_file_info *fi)
{
	int res;
	(void) path;

#ifndef HAVE_FDATASYNC
	(void) isdatasync;
#else
	if (isdatasync)
		res = fdatasync(fi->fh);
	else
#endif
		res = fsync(fi->fh);
	if (res == -1)
		return -errno;

	return 0;
}

#ifdef HAVE_POSIX_FALLOCATE
static int xmp_fallocate(const char *path, int mode,
			off_t offset, off_t length, struct fuse_file_info *fi)
{
	(void) path;

	if (mode)
		return -EOPNOTSUPP;

	return -posix_fallocate(fi->fh, offset, length);
}
#endif

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int xmp_setxattr(const char *path, const char *name, const char *value,
			size_t size, int flags)
{
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	int res = lsetxattr(fpath, name, value, size, flags);
	if (res == -1)
		return -errno;
	return 0;
}

static int xmp_getxattr(const char *path, const char *name, char *value,
			size_t size)
{
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	int res = lgetxattr(fpath, name, value, size);
	if (res == -1)
		return -errno;
	return res;
}

static int xmp_listxattr(const char *path, char *list, size_t size)
{
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	int res = llistxattr(fpath, list, size);
	if (res == -1)
		return -errno;
	return res;
}

static int xmp_removexattr(const char *path, const char *name)
{
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	int res = lremovexattr(fpath, name);
	if (res == -1)
		return -errno;
	return 0;
}
#endif /* HAVE_SETXATTR */

#ifdef HAVE_LIBULOCKMGR
static int xmp_lock(const char *path, struct fuse_file_info *fi, int cmd,
		    struct flock *lock)
{
	(void) path;

	return ulockmgr_op(fi->fh, cmd, lock, &fi->lock_owner,
			   sizeof(fi->lock_owner));
}
#endif

static int xmp_flock(const char *path, struct fuse_file_info *fi, int op)
{
	int res;
	(void) path;

	res = flock(fi->fh, op);
	if (res == -1)
		return -errno;

	return 0;
}

static struct fuse_operations xmp_oper = {
	.init           = xmp_init,
	.getattr	= xmp_getattr,
	.fgetattr	= xmp_fgetattr,
	.access		= xmp_access,
	.readlink	= xmp_readlink,
	.opendir	= xmp_opendir,
	.readdir	= xmp_readdir,
	.releasedir	= xmp_releasedir,
	.mknod		= xmp_mknod,
	.mkdir		= xmp_mkdir,
	.symlink	= xmp_symlink,
	.unlink		= xmp_unlink,
	.rmdir		= xmp_rmdir,
	.rename		= xmp_rename,
	.link		= xmp_link,
	.chmod		= xmp_chmod,
	.chown		= xmp_chown,
	.truncate	= xmp_truncate,
	.ftruncate	= xmp_ftruncate,
#ifdef HAVE_UTIMENSAT
	.utimens	= xmp_utimens,
#endif
	.create		= xmp_create,
	.open		= xmp_open,
	.read		= xmp_read,
	.read_buf	= xmp_read_buf,
	.write		= xmp_write,
	.write_buf	= xmp_write_buf,
	.statfs		= xmp_statfs,
	.flush		= xmp_flush,
	.release	= xmp_release,
	.fsync		= xmp_fsync,
#ifdef HAVE_POSIX_FALLOCATE
	.fallocate	= xmp_fallocate,
#endif
#ifdef HAVE_SETXATTR
	.setxattr	= xmp_setxattr,
	.getxattr	= xmp_getxattr,
	.listxattr	= xmp_listxattr,
	.removexattr	= xmp_removexattr,
#endif
#ifdef HAVE_LIBULOCKMGR
	.lock		= xmp_lock,
#endif
	.flock		= xmp_flock,
};

void rfs_usage()
{
    fprintf(stderr, "usage:  rotatefs [FUSE and mount options] rootDir mountPoint\n");
    abort();
}

int main(int argc, char *argv[])
{
    int fuse_stat;
    struct rfs_state *rfs_data;

    umask(0);

    // See which version of fuse we're running
    fprintf(stderr, "Fuse library version %d.%d\n", FUSE_MAJOR_VERSION, FUSE_MINOR_VERSION);

    // Perform some sanity checking on the command line:  make sure
    // there are enough arguments, and that neither of the last two
    // start with a hyphen (this will break if you actually have a
    // rootpoint or mountpoint whose name starts with a hyphen, but so
    // will a zillion other programs)
    if ((argc < 3) || (argv[argc-2][0] == '-') || (argv[argc-1][0] == '-'))
        rfs_usage();

    rfs_data = malloc(sizeof(struct rfs_state));
    if (rfs_data == NULL) {
	perror("main calloc");
	abort();
    }
    // Pull the rootdir out of the argument list and save it in my
    // internal data
    rfs_data->rootdir = realpath(argv[argc-2], NULL);
    argv[argc-2] = argv[argc-1];
    argv[argc-1] = NULL;
    argc--;
    fprintf(stderr, "rootdir: %s\n", rfs_data->rootdir);

    rfs_data->files_traversed = 0;

    // turn over control to fuse
    fuse_stat = fuse_main(argc, argv, &xmp_oper, rfs_data);
    
    return fuse_stat;
}
