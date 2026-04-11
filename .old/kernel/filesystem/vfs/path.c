#include <ros.h>
#include "../../param.h"
#include "vfs.h"
#include "utils.h"
#include "log.h"
#include "../../hal/hal.h"
#include "memory.h"


int vfs_path_split(const char* path, char* buf) {
	int count = 0;
	int x = 0, y;
	for (;;) {
		if (path[x] == '\0') {
			break;
		}
		for (y = x; ((path[y] != '/') && (path[y] != '\0')); y++) {
		}
		if (y - x == 0) {
			// empty directory name
			x = y + 1;
			continue;
		}
		strncpy(buf + count * VFS_NAME_LENGTH_MAX, path + x, y - x);
		*(buf + count * VFS_NAME_LENGTH_MAX + y - x) = '\0';
		count++;
		if (path[y] == '\0') {
			// end of path string
			break;
		}
		x = y + 1;
	}
	return count;
}

int vfs_path_compare(int lhs_parts, const char* lhs_buf, int rhs_parts, const char* rhs_buf) {
	if (lhs_parts != rhs_parts) {
		return 0;
	}
	for (int i = 0; i < lhs_parts; i++) {
		if (strncmp(lhs_buf + i * VFS_NAME_LENGTH_MAX, rhs_buf + i * VFS_NAME_LENGTH_MAX, VFS_NAME_LENGTH_MAX) != 0) {
			return 0;
		}
	}
	return 1;
}

void vfs_path_tostring(struct VfsPath path, char* buf) {
	int next = 1;
	buf[0] = '/';
	for (int i = 0; i < path.parts; i++) {
		safestrcpy(buf + next, path.pathbuf + i * VFS_NAME_LENGTH_MAX, VFS_NAME_LENGTH_MAX);
		next += strlen(path.pathbuf + i * VFS_NAME_LENGTH_MAX);
		buf[next] = '/';
		next++;
	}
	buf[next] = '\0';
}

void vfs_get_absolute_path(struct VfsPath* path) {
	int write_parts;

	if (!path || !path->pathbuf || path->parts <= 0) {
		return;
	}

	/*
	 * Historical note:
	 * - this helper used to prepend a kernel-side cwd,
	 * - but the current task/process model no longer stores cwd in the kernel,
	 * - and user space now resolves most relative paths before making syscalls.
	 *
	 * That means the safe job here is:
	 * - treat the incoming relative path as rooted at '/', because the VFS split
	 *   format stores absolute and root-relative paths the same way after '/'
	 *   separators are removed, and
	 * - collapse '.' and '..' segments so backtracking behaves like a normal path.
	 */
	write_parts = 0;
	for (int read_parts = 0; read_parts < path->parts; read_parts++) {
		char* segment = path->pathbuf + read_parts * VFS_NAME_LENGTH_MAX;

		if (segment[0] == '\0' || (segment[0] == '.' && segment[1] == '\0')) {
			continue;
		}
		if (segment[0] == '.' && segment[1] == '.' && segment[2] == '\0') {
			if (write_parts > 0) {
				write_parts--;
			}
			continue;
		}

		if (write_parts != read_parts) {
			memmove(
				path->pathbuf + write_parts * VFS_NAME_LENGTH_MAX,
				segment,
				VFS_NAME_LENGTH_MAX);
		}
		write_parts++;
	}

	if (write_parts < path->parts) {
		memset(
			path->pathbuf + write_parts * VFS_NAME_LENGTH_MAX,
			0,
			(path->parts - write_parts) * VFS_NAME_LENGTH_MAX);
	}

	path->parts = write_parts;
}