/*
 * \brief  libc file operations
 * \author Christian Prochaska
 * \author Norman Feske
 * \author Emery Hemingway
 * \author Christian Helmuth
 * \date   2010-01-21
 */

/*
 * Copyright (C) 2010-2019 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/env.h>
#include <os/path.h>
#include <util/token.h>

/* compiler includes */
#include <stdarg.h>

extern "C" {
/* libc includes */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <libc_private.h>
#include <sys/cdefs.h>
}

/* libc-internal includes */
#include <internal/plugin_registry.h>
#include <internal/plugin.h>
#include <internal/file.h>
#include <internal/file_operations.h>
#include <internal/mem_alloc.h>
#include <internal/mmap_registry.h>
#include <internal/errno.h>
#include <internal/init.h>
#include <internal/cwd.h>

using namespace Libc;

#define __SYS_(ret_type, name, args, body) \
	extern "C" {\
	ret_type  __sys_##name args body \
	ret_type __libc_##name args __attribute__((alias("__sys_" #name))); \
	ret_type       _##name args __attribute__((alias("__sys_" #name))); \
	ret_type          name args __attribute__((alias("__sys_" #name))); \
	} \

Libc::Mmap_registry *Libc::mmap_registry()
{
	static Mmap_registry registry;
	return &registry;
}


static Cwd          *_cwd_ptr;
static unsigned int  _mmap_align_log2 { PAGE_SHIFT };

void Libc::init_file_operations(Cwd &cwd, File_descriptor_allocator &fd_alloc,
                                Config_accessor const &config_accessor)
{
	_fd_alloc_ptr = &fd_alloc;
	_cwd_ptr      = &cwd;

	config_accessor.with_config([&] (Xml_node const &config) {
		config.with_optional_sub_node("libc", [&] (Xml_node libc) {
			libc.with_optional_sub_node("mmap", [&] (Xml_node mmap) {
				_mmap_align_log2 = mmap.attribute_value("align_log2",
				                                        (unsigned int)PAGE_SHIFT);
			});
		});
	});
}


/***************
 ** Utilities **
 ***************/

/**
 * Current working directory
 */
static Absolute_path &cwd()
{
	struct Missing_call_of_init_file_operations : Exception { };
	if (!_cwd_ptr)
		throw Missing_call_of_init_file_operations();

	return _cwd_ptr->cwd();
}


using Path_element_token = Token<Vfs::Scanner_policy_path_element>;


/**
 * Resolve symbolic links in a given absolute path
 */
Symlink_resolve_result Libc::resolve_symlinks(char const *path, Absolute_path &resolved_path)
{
	char path_element[PATH_MAX];
	char symlink_target[PATH_MAX];

	Absolute_path current_iteration_working_path;
	Absolute_path next_iteration_working_path(path, cwd().base());

	enum { FOLLOW_LIMIT = 10 };
	int follow_count = 0;
	bool symlink_resolved_in_this_iteration;
	do {
		if (follow_count++ == FOLLOW_LIMIT) {
			errno = ELOOP;
			return Symlink_resolve_error();
		}

		current_iteration_working_path = next_iteration_working_path;

		next_iteration_working_path.import("");
		symlink_resolved_in_this_iteration = false;

		Path_element_token t(current_iteration_working_path.base());

		while (t) {
			if (t.type() != Path_element_token::IDENT) {
					t = t.next();
					continue;
			}

			t.string(path_element, sizeof(path_element));

			try {
				next_iteration_working_path.append_element(path_element);
			} catch (Path_base::Path_too_long) {
				errno = ENAMETOOLONG;
				return Symlink_resolve_error();
			}

			/*
			 * If a symlink has been resolved in this iteration, the remaining
			 * path elements get added and a new iteration starts.
			 */
			if (!symlink_resolved_in_this_iteration) {
				struct stat stat_buf;
				int res;
				FNAME_FUNC_WRAPPER_GENERIC(res = , stat, next_iteration_working_path.base(), &stat_buf);
				if (res == -1) {
					return Symlink_resolve_error();
				}
				if (S_ISLNK(stat_buf.st_mode)) {
					FNAME_FUNC_WRAPPER_GENERIC(res = , readlink,
					                           next_iteration_working_path.base(),
					                           symlink_target, sizeof(symlink_target) - 1);
					if (res < 1)
						return Symlink_resolve_error();

					/* zero terminate target */
					symlink_target[res] = 0;

					if (symlink_target[0] == '/')
						/* absolute target */
						next_iteration_working_path.import(symlink_target, cwd().base());
					else {
						/* relative target */
						next_iteration_working_path.strip_last_element();
						try {
							next_iteration_working_path.append_element(symlink_target);
						} catch (Path_base::Path_too_long) {
							errno = ENAMETOOLONG;
							return Symlink_resolve_error();
						}
					}
					symlink_resolved_in_this_iteration = true;
				}
			}

			t = t.next();
		}

	} while (symlink_resolved_in_this_iteration);

	resolved_path = next_iteration_working_path;
	resolved_path.remove_trailing('/');

	return Ok();
}


static Symlink_resolve_result resolve_symlinks_except_last_element(char const *path, Absolute_path &resolved_path)
{
	Absolute_path absolute_path_without_last_element(path, cwd().base());
	absolute_path_without_last_element.strip_last_element();

	if (resolve_symlinks(absolute_path_without_last_element.base(), resolved_path).failed())
		return Symlink_resolve_error();

	/* append last element to resolved path */
	Absolute_path absolute_path_last_element(path, cwd().base());
	absolute_path_last_element.keep_only_last_element();
	try {
		resolved_path.append_element(absolute_path_last_element.base());
	} catch (Path_base::Path_too_long) {
		errno = ENAMETOOLONG;
		return Symlink_resolve_error();
	}

	return Ok();
}


/********************
 ** Libc functions **
 ********************/

extern "C" int access(const char *path, int amode)
{
	if (!path)
		return Errno(EFAULT);

	if (path[0] == '\0')
		return Errno(ENOENT);

	Absolute_path resolved_path;

	if (resolve_symlinks(path, resolved_path).failed()) {
		errno = ENOENT;
		return -1;
	}

	FNAME_FUNC_WRAPPER(access, resolved_path.base(), amode);
}


extern "C" int chdir(const char *path)
{
	if (!path)
		return Errno(EFAULT);

	if (path[0] == '\0')
		return Errno(ENOENT);

	struct stat stat_buf;
	if ((stat(path, &stat_buf) == -1) ||
	    (!S_ISDIR(stat_buf.st_mode))) {
		errno = ENOTDIR;
		return -1;
	}
	cwd().import(path, cwd().base());
	return 0;
}


/**
 * Close is called incorrectly enough to justify a silent failure
 */
__SYS_(int, close, (int libc_fd),
{
	File_descriptor *fd = file_descriptor_allocator()->find_by_libc_fd(libc_fd);

	if (!fd)
		return Errno(EBADF);

	if (!fd->plugin || fd->plugin->close(fd) != 0)
		file_descriptor_allocator()->free(fd);

	return 0;
})


extern "C" int dup(int libc_fd)
{
	File_descriptor *ret_fd;
	FD_FUNC_WRAPPER_GENERIC(ret_fd =, 0, dup, libc_fd);
	return ret_fd ? ret_fd->libc_fd : INVALID_FD;
}


extern "C" int dup2(int libc_fd, int new_libc_fd)
{
	File_descriptor *fd = libc_fd_to_fd(libc_fd, "dup2");
	if (!fd || !fd->plugin) {
		errno = EBADF;
		return INVALID_FD;
	}

	if (libc_fd == new_libc_fd)
		return libc_fd;

	/*
	 * Check if 'new_libc_fd' is already in use. If so, close it before
	 * allocating it again.
	 */
	File_descriptor *new_fd = file_descriptor_allocator()->find_by_libc_fd(new_libc_fd);
	if (new_fd)
		close(new_libc_fd);

	new_fd = file_descriptor_allocator()->alloc(fd->plugin, 0, new_libc_fd);
	if (!new_fd) return Errno(EMFILE);

	/* new_fd->context must be assigned by the plugin implementing 'dup2' */
	return fd->plugin->dup2(fd, new_fd);
}


extern "C" __attribute__((alias("dup2")))
int _dup2(int libc_fd, int new_libc_fd);


extern "C" int fchdir(int libc_fd)
{
	File_descriptor *fd = libc_fd_to_fd(libc_fd, "fchdir");

	if (!fd) {
		errno = EBADF;
		return INVALID_FD;
	}

	return chdir(fd->fd_path);
}


__SYS_(int, fcntl, (int libc_fd, int cmd, ...),
{
	va_list ap;
	int res;
	va_start(ap, cmd);
	FD_FUNC_WRAPPER_GENERIC(res =, INVALID_FD, fcntl, libc_fd, cmd, va_arg(ap, long));
	va_end(ap);
	return res;
})


__SYS_(int, fstat, (int libc_fd, struct stat *buf),
{
	FD_FUNC_WRAPPER(fstat, libc_fd, buf);
})


__SYS_(int, fstatat, (int libc_fd, char const *path, struct stat *buf, int flags),
{
	if (!path)
		return Errno(EFAULT);

	if (path[0] == '\0')
		return Errno(ENOENT);

	if (*path == '/') {
		if (flags & AT_SYMLINK_NOFOLLOW)
			return lstat(path, buf);
		return stat(path, buf);
	}

	Absolute_path abs_path;

	if (libc_fd == AT_FDCWD) {
		abs_path = cwd();
		abs_path.append_element(path);
	} else {
		File_descriptor *fd = file_descriptor_allocator()->find_by_libc_fd(libc_fd);
		if (!fd) {
			errno = EBADF;
			return -1;
		}
		abs_path.import(path, fd->fd_path);
	}

	return (flags & AT_SYMLINK_NOFOLLOW)
		? lstat(abs_path.base(), buf)
		:  stat(abs_path.base(), buf);
})


__SYS_(int, fstatfs, (int libc_fd, struct statfs *buf), {
	FD_FUNC_WRAPPER(fstatfs, libc_fd, buf); })


__SYS_(int, fsync, (int libc_fd), {
	FD_FUNC_WRAPPER(fsync, libc_fd); })


__SYS_(int, fdatasync, (int libc_fd), {
	FD_FUNC_WRAPPER(fsync, libc_fd); })


__SYS_(int, ftruncate, (int libc_fd, ::off_t length), {
	FD_FUNC_WRAPPER(ftruncate, libc_fd, length); })


__SYS_(ssize_t, getdirentries, (int libc_fd, char *buf, ::size_t nbytes, ::off_t *basep), {
	FD_FUNC_WRAPPER(getdirentries, libc_fd, buf, nbytes, basep); })


__SYS_(int, ioctl, (int libc_fd, unsigned long request, char *argp), {
	FD_FUNC_WRAPPER(ioctl, libc_fd, request, argp); })


__SYS_(::off_t, lseek, (int libc_fd, ::off_t offset, int whence), {
	FD_FUNC_WRAPPER(lseek, libc_fd, offset, whence); })


extern "C" int lstat(const char *path, struct stat *buf)
{
	if (!path)
		return Errno(EFAULT);

	if (path[0] == '\0')
		return Errno(ENOENT);

	Absolute_path resolved_path;

	if (resolve_symlinks_except_last_element(path, resolved_path).failed())
		return -1;

	resolved_path.remove_trailing('/');
	FNAME_FUNC_WRAPPER(stat, resolved_path.base(), buf);
}


extern "C" int mkdir(const char *path, mode_t mode)
{
	if (!path)
		return Errno(EFAULT);

	if (path[0] == '\0')
		return Errno(ENOENT);

	Absolute_path resolved_path;

	if (resolve_symlinks_except_last_element(path, resolved_path).failed())
		return -1;

	resolved_path.remove_trailing('/');
	FNAME_FUNC_WRAPPER(mkdir, resolved_path.base(), mode);
}


__SYS_(void *, mmap, (void *addr, ::size_t length,
                      int prot, int flags,
                      int libc_fd, ::off_t offset),
{
	/* handle requests for anonymous memory */
	if ((flags & MAP_ANONYMOUS) || (flags & MAP_ANON)) {

		if (flags & MAP_FIXED) {
			Genode::error("mmap for fixed predefined address not supported yet");
			errno = EINVAL;
			return MAP_FAILED;
		}

		bool const executable = prot & PROT_EXEC;
		void *start = mem_alloc(executable)->alloc(length, _mmap_align_log2);
		if (!start) {
			errno = ENOMEM;
			return MAP_FAILED;
		}
		::memset(start, 0, align_addr(length, PAGE_SHIFT));
		mmap_registry()->insert(start, length, 0);
		return start;
	}

	/* lookup plugin responsible for file descriptor */
	File_descriptor *fd = libc_fd_to_fd(libc_fd, "mmap");
	if (!fd || !fd->plugin || !fd->plugin->supports_mmap()) {
		warning("mmap not supported for file descriptor ", libc_fd);
		errno = EBADF;
		return MAP_FAILED;
	}

	void *start = fd->plugin->mmap(addr, length, prot, flags, fd, offset);

	if (start != MAP_FAILED)
		mmap_registry()->insert(start, length, fd->plugin);

	return start;
})


extern "C" int munmap(void *start, ::size_t length)
{
	if (!mmap_registry()->registered(start)) {
		warning("munmap: could not lookup plugin for address ", start);
		errno = EINVAL;
		return -1;
	}

	/*
	 * Lookup plugin that was used for mmap
	 *
	 * If the pointer is NULL, 'start' refers to an anonymous mmap.
	 */
	Plugin *plugin = mmap_registry()->lookup_plugin_by_addr(start);

	/*
	 * Remove registry entry before unmapping to avoid double insertion error
	 * if another thread gets the same start address immediately after unmapping.
	 */
	mmap_registry()->remove(start);

	int ret = 0;
	if (plugin)
		ret = plugin->munmap(start, length);
	else {
		bool const executable = true;
		/* XXX another metadata handling required to track anonymous memory */
		mem_alloc(!executable)->free(start);
		mem_alloc(executable)->free(start);
	}

	return ret;
}


__SYS_(int, msync, (void *start, ::size_t len, int flags),
{
	if (!mmap_registry()->registered(start)) {
		warning("munmap: could not lookup plugin for address ", start);
		errno = EINVAL;
		return -1;
	}

	/*
	 * Lookup plugin that was used for mmap
	 *
	 * If the pointer is NULL, 'start' refers to an anonymous mmap.
	 */
	Plugin *plugin = mmap_registry()->lookup_plugin_by_addr(start);

	int ret = 0;
	if (plugin)
		ret = plugin->msync(start, len, flags);

	return ret;
})


__SYS_(int, open, (const char *pathname, int flags, ...),
{
	if (!pathname)
		return Errno(EFAULT);

	if (pathname[0] == '\0')
		return Errno(ENOENT);

	Absolute_path resolved_path;

	Plugin *plugin;
	File_descriptor *new_fdo;

	if (resolve_symlinks_except_last_element(pathname, resolved_path).failed())
		return -1;

	if (!(flags & O_NOFOLLOW)) {
		/* resolve last element */
		if (resolve_symlinks(resolved_path.base(), resolved_path).failed()) {
			if (errno == ENOENT) {
				if (!(flags & O_CREAT))
					return -1;
			} else
				return -1;
		}
	}

	plugin = plugin_registry()->get_plugin_for_open(resolved_path.base(), flags);

	if (!plugin) {
		error("no plugin found for open(\"", pathname, "\", ", flags, ")");
		return -1;
	}

	new_fdo = plugin->open(resolved_path.base(), flags);
	if (!new_fdo)
		return -1;
	new_fdo->path(resolved_path.base());

	if (flags & O_APPEND)
		lseek(new_fdo->libc_fd, 0, SEEK_END);

	return new_fdo->libc_fd;
})


__SYS_(int, openat, (int libc_fd, const char *path, int flags, ...),
{
	if (!path)
		return Errno(EFAULT);

	if (path[0] == '\0')
		return Errno(ENOENT);

	va_list ap;
	va_start(ap, flags);
	mode_t mode = va_arg(ap, unsigned);
	va_end(ap);


	if (*path == '/') {
		return open(path, flags, mode);
	}

	Absolute_path abs_path;

	if (libc_fd == AT_FDCWD) {
		abs_path = cwd();
		abs_path.append_element(path);
	} else {
		File_descriptor *fd = file_descriptor_allocator()->find_by_libc_fd(libc_fd);
		if (!fd) {
			errno = EBADF;
			return -1;
		}
		abs_path.import(path, fd->fd_path);
	}

	return open(abs_path.base(), flags, mode);
})


extern "C" int pipe(int pipefd[2]) {
	return pipe2(pipefd, 0); }


extern "C" int pipe2(int pipefd[2], int flags)
{
	Plugin *plugin;
	File_descriptor *pipefdo[2];

	plugin = plugin_registry()->get_plugin_for_pipe();

	if (!plugin) {
		error("no plugin found for pipe()");
		return -1;
	}

	if (plugin->pipe(pipefdo) == -1) {
		error("plugin()->pipe() failed");
		return -1;
	}

	if (flags & O_NONBLOCK) {
		int err = plugin->fcntl(pipefdo[0], F_SETFL, O_NONBLOCK)
		        | plugin->fcntl(pipefdo[1], F_SETFL, O_NONBLOCK);
		if (err != 0)
			warning("pipe plugin does not support O_NONBLOCK");
	}

	pipefd[0] = pipefdo[0]->libc_fd;
	pipefd[1] = pipefdo[1]->libc_fd;

	return 0;
}


__SYS_(ssize_t, read, (int libc_fd, void *buf, ::size_t count), {
	FD_FUNC_WRAPPER(read, libc_fd, buf, count); })


extern "C" ssize_t readlink(const char *path, char *buf, ::size_t bufsiz)
{
	if (!path)
		return Errno(EFAULT);

	if (path[0] == '\0')
		return Errno(ENOENT);

	Absolute_path resolved_path;

	if (resolve_symlinks_except_last_element(path, resolved_path).failed())
		return -1;

	FNAME_FUNC_WRAPPER(readlink, resolved_path.base(), buf, bufsiz);
}


extern "C" int rename(const char *oldpath, const char *newpath)
{
	if (!oldpath || !newpath)
		return Errno(EFAULT);

	if ((oldpath[0] == '\0') || (newpath[0] == '\0'))
		return Errno(ENOENT);

	Absolute_path resolved_oldpath, resolved_newpath;

	if (resolve_symlinks_except_last_element(oldpath, resolved_oldpath).failed())
		return -1;

	if (resolve_symlinks_except_last_element(newpath, resolved_newpath).failed())
		return -1;

	resolved_oldpath.remove_trailing('/');
	resolved_newpath.remove_trailing('/');

	FNAME_FUNC_WRAPPER(rename, resolved_oldpath.base(), resolved_newpath.base());
}


extern "C" int rmdir(const char *path)
{
	if (!path)
		return Errno(EFAULT);

	if (path[0] == '\0')
		return Errno(ENOENT);

	Absolute_path resolved_path;

	if (resolve_symlinks_except_last_element(path, resolved_path).failed())
		return -1;

	resolved_path.remove_trailing('/');

	struct stat stat_buf { };

	if (stat(resolved_path.base(), &stat_buf) == -1)
		return -1;

	if (!S_ISDIR(stat_buf.st_mode)) {
		errno = ENOTDIR;
		return -1;
	}

	FNAME_FUNC_WRAPPER(rmdir, resolved_path.base());
}


extern "C" int stat(const char *path, struct stat *buf)
{
	if (!path)
		return Errno(EFAULT);

	if (path[0] == '\0')
		return Errno(ENOENT);

	Absolute_path resolved_path;

	if (resolve_symlinks(path, resolved_path).failed())
		return -1;

	resolved_path.remove_trailing('/');
	FNAME_FUNC_WRAPPER(stat, resolved_path.base(), buf);
}


extern "C" int symlink(const char *oldpath, const char *newpath)
{
	if (!oldpath || !newpath)
		return Errno(EFAULT);

	if ((oldpath[0] == '\0') || (newpath[0] == '\0'))
		return Errno(ENOENT);

	Absolute_path resolved_path;

	if (resolve_symlinks_except_last_element(newpath, resolved_path).failed())
		return -1;

	FNAME_FUNC_WRAPPER(symlink, oldpath, resolved_path.base());
}


extern "C" int unlink(const char *path)
{
	if (!path)
		return Errno(EFAULT);

	if (path[0] == '\0')
		return Errno(ENOENT);

	Absolute_path resolved_path;

	if (resolve_symlinks_except_last_element(path, resolved_path).failed())
		return -1;

	FNAME_FUNC_WRAPPER(unlink, resolved_path.base());
}


__SYS_(ssize_t, write, (int libc_fd, const void *buf, ::size_t count),
{
	int flags = fcntl(libc_fd, F_GETFL);

	if ((flags != -1) && (flags & O_APPEND))
		lseek(libc_fd, 0, SEEK_END);

	FD_FUNC_WRAPPER(write, libc_fd, buf, count);
})


extern "C" int __getcwd(char *dst, ::size_t dst_size)
{
	copy_cstring(dst, cwd().base(), dst_size);
	return 0;
}
