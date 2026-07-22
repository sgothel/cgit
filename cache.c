/* cache.c: cache management
 *
 * Copyright (C) 2006-2014 cgit Development Team <cgit@lists.zx2c4.com>
 *
 * Licensed under GNU General Public License v2
 *   (see COPYING for full license text)
 *
 *
 * The cache is just a directory structure where each file is a cache slot,
 * and each filename is based on the hash of some key (e.g. the cgit url).
 * Each file contains the full key followed by the cached content for that
 * key.
 *
 */

#include "cgit.h"
#include "cache.h"
#include "html.h"
#include "ui-shared.h"
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#ifdef HAVE_LINUX_SENDFILE
#include <sys/sendfile.h>
#endif

#define CACHE_BUFSIZE (1024 * 4)

struct cache_slot {
	const char *key;
	size_t keylen;
	int ttl;
	cache_fill_fn fn;
	int cache_fd;
	int lock_fd;
	int stdout_fd;
	const char *cache_name;
	const char *lock_name;
	int match;
	struct stat cache_st;
	int bufsize;
	char buf[CACHE_BUFSIZE];
};

/* Open an existing cache slot and fill the cache buffer with
 * (part of) the content of the cache file. Return 0 on success
 * and errno otherwise.
 */
static int open_slot(struct cache_slot *slot)
{
	char *bufz;
	ssize_t bufkeylen = -1;

	slot->cache_fd = open(slot->cache_name, O_RDONLY);
	if (slot->cache_fd == -1)
		return errno;

	if (fstat(slot->cache_fd, &slot->cache_st))
		return errno;

	slot->bufsize = xread(slot->cache_fd, slot->buf, sizeof(slot->buf));
	if (slot->bufsize < 0)
		return errno;

	bufz = memchr(slot->buf, 0, slot->bufsize);
	if (bufz)
		bufkeylen = bufz - slot->buf;

	if (slot->key)
		slot->match = bufkeylen == slot->keylen &&
		    !memcmp(slot->key, slot->buf, bufkeylen + 1);

	return 0;
}

/* Close the active cache slot */
static int close_slot(struct cache_slot *slot)
{
	int err = 0;
	if (slot->cache_fd > 0) {
		if (close(slot->cache_fd))
			err = errno;
		slot->cache_fd = -1;
	}
	return err;
}

#define MY_MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MY_MAX(X, Y) (((X) > (Y)) ? (X) : (Y))

static int sendslot_to_idle(const struct timespec *tStart,
			    const struct timespec *tLastSend,
			    const struct timespec *tNow,
			    size_t off, size_t size, struct cache_slot *slot)
{
	const long td_total = cgit_ts_ms_sub(tNow, tStart);
	const long td_idle = cgit_ts_ms_sub(tNow, tLastSend);
	const long rate = off / MY_MAX(1, td_total/1000);
	cache_log("[cgit] send_slot timeout idle %ldms: sending cache "
			  "%s (%s) (%ld/%ld bytes) to client `%s` "
			  "within [total %ldms, idle %ldms, rate %ld Bps]\n",
			  td_idle, slot->cache_name, slot->key, off, size, ctx.env.remote_addr,
			  td_total, td_idle, rate);
	return ETIMEDOUT;
}

static int sendslot_to_minrate(const struct timespec *tStart,
			       const struct timespec *tNow,
			       size_t off, size_t size, struct cache_slot *slot)
{
	const long td_total = cgit_ts_ms_sub(tNow, tStart);
	const long rate = off / MY_MAX(1, td_total/1000);
	cache_log("[cgit] send_slot timeout rate-limit %d Bps: sending "
			  "cache %s (%s) (%ld/%ld bytes) to client `%s` "
			  "within [total %ldms, rate %ld Bps]\n",
			  ctx.cfg.client_io_min_rate, slot->cache_name, slot->key,
			  off, size, ctx.env.remote_addr,
			  td_total, rate);
	return ETIMEDOUT;
}

static int sendslot_ok(const struct timespec *tStart,
		       const struct timespec *tNow,
		       size_t size, struct cache_slot *slot)
{
	if (ctx.cfg.log_level >= LOG_LVL_DBG) {
		const long td_total = cgit_ts_ms_sub(tNow, tStart);
		const long rate = size / MY_MAX(1, td_total/1000);
		cache_log("[cgit] send_slot status: sent cache %s (%s) %ld bytes) to "
			  "client `%s` within [total %ldms, rate %ld Bps]\n",
			  slot->cache_name, slot->key,
			  size, ctx.env.remote_addr, td_total, rate);
	}
	return 0;
}

static int sendslot_ok2(const struct timespec *tStart, size_t size, struct cache_slot *slot)
{
	if (ctx.cfg.log_level >= LOG_LVL_DBG) {
		struct timespec tNow;
		return sendslot_ok(tStart, cgit_ts_current(&tNow), size, slot);
	}
	return 0;
}

static ssize_t write_in_full_to(int fd, const void *buf, size_t count, off_t *total_out,
				const struct timespec *tStart,
				struct timespec *tLastSend, long to_max)
{
	if (!count) {
		return 0;
	}
	const char *p = buf;
	ssize_t total = 0;
	struct timespec tNow = *tLastSend;

	do {
		if (cgit_ts_ms_sub(&tNow, tLastSend) >= (long)ctx.cfg.client_io_idle_timeout) {
			errno = ETIMEDOUT;
			return -2;
		}
		if (cgit_ts_ms_sub(&tNow, tStart) > to_max) {
			errno = ETIMEDOUT;
			return -3;
		}

		ssize_t written = write(fd, p, MY_MIN(count, MAX_IO_SIZE));
		cgit_ts_current(&tNow);
		if (written < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				struct pollfd pfd;
				pfd.fd = fd;
				pfd.events = POLLOUT;
				// no need to check for errors,
				// subsequent read/write will detect unrecoverable errors
				poll(&pfd, 1, -1);
				continue;
			}
			return -1;
		} else if (written > 0) {
			*total_out += written;
			count -= written;
			p += written;
			total += written;
			*tLastSend = tNow;
			if (!count)
				return total;
		}
	} while (1);
}

/* Print the content of the active cache slot (but skip the key). */
static int print_slot(struct cache_slot *slot)
{
	struct timespec tStart;
	cgit_ts_current(&tStart);
	struct timespec tLastSend = tStart;
	struct timespec tNow = tStart;

	off_t off = slot->keylen + 1;
	off_t size = slot->cache_st.st_size;

	if (!size) {
		return sendslot_ok(&tStart, &tNow, size, slot);
	}
	const long to_min_rate =
		MY_MAX(ctx.cfg.client_io_idle_timeout, (size / (long)ctx.cfg.client_io_min_rate) * 1000L);

#ifdef HAVE_LINUX_SENDFILE
	do {
		if (cgit_ts_ms_sub(&tNow, &tLastSend) >= (long)ctx.cfg.client_io_idle_timeout)
			return sendslot_to_idle(&tStart, &tLastSend, &tNow, off, size, slot);
		if (cgit_ts_ms_sub(&tNow, &tStart) > to_min_rate)
			return sendslot_to_minrate(&tStart, &tNow, off, size, slot);

		ssize_t count = sendfile(STDOUT_FILENO, slot->cache_fd, &off, size - off);
		cgit_ts_current(&tNow);
		if (count < 0) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			/* Fall back to read/write on EINVAL or ENOSYS */
			if (errno == EINVAL || errno == ENOSYS)
				break;
			return errno;
		} else if (count > 0) {
			tLastSend = tNow;
			if (off == size)
				return sendslot_ok(&tStart, &tNow, size, slot);
		}
	} while (1);
#endif

	if (lseek(slot->cache_fd, off, SEEK_SET) != off)
		return errno;

	do {
		ssize_t count = xread(slot->cache_fd, slot->buf, sizeof(slot->buf));
		if (count < 0)
			return errno;

		ssize_t res;
		if ((res = write_in_full_to(STDOUT_FILENO, slot->buf, count, &off,
					    &tStart, &tLastSend, to_min_rate)) < 0)
		{
			if (ETIMEDOUT == errno) {
				cgit_ts_current(&tNow);
				if (-2 == res)
					return sendslot_to_idle(&tStart, &tLastSend, &tNow, off, size, slot);
				else if (-3 == res)
					return sendslot_to_minrate(&tStart, &tNow, off, size, slot);
			}
			return errno;
		}
		if (off == size || !count /* should be redundant */) {
			return sendslot_ok2(&tStart, size, slot);
		}
	} while (1);
}

/* Check if the slot has expired */
static int is_expired(struct cache_slot *slot)
{
	if (slot->ttl < 0)
		return 0;
	else
		return slot->cache_st.st_mtime + slot->ttl * 60 < time(NULL);
}

/* Close an open lockfile */
static int close_lock(struct cache_slot *slot)
{
	int err = 0;
	if (slot->lock_fd > 0) {
		if (close(slot->lock_fd))
			err = errno;
		slot->lock_fd = -1;
	}
	return err;
}

enum lock_file_op_t { UNLINK_LOCK_FILE=0, REPLACE_OLD_SLOT=1 };

static int unlock_slot(struct cache_slot *slot, enum lock_file_op_t lock_file_op);

static const char *to_string(enum lock_file_op_t lock_file_op) {
	switch (lock_file_op) {
		case UNLINK_LOCK_FILE: return "unlink";
		case REPLACE_OLD_SLOT: return "replace";
		default: return "undef";
	}
}

/* Create a lockfile used to store the generated content for a cache
 * slot, and write the slot key + \0 into it.
 * Returns 0 on success and errno otherwise.
 */
static int lock_slot(struct cache_slot *slot, const struct timespec *tStart)
{
	struct flock lock = {
		.l_type = F_WRLCK,
		.l_whence = SEEK_SET,
		.l_start = 0,
		.l_len = 0,
	};
	size_t wait_count = 0;

	if (0 == strncmp("cgit_test_key_no_lock", slot->key, 21)) {
		cache_log("[cgit] Lock (%ldms): Test-Key: %s -> forced fail\n",
			  cgit_ts_ms_sub_current(tStart), slot->key);
		return ENOENT;
	}
	slot->lock_fd =
	    open(slot->lock_name, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (slot->lock_fd == -1) {
		int saved_errno = errno;
		cache_log("[cgit] Lock (%ldms): Unable to open/create lock slot %s (%s): %s (%d)\n",
			  cgit_ts_ms_sub_current(tStart), slot->lock_name,
			  slot->key, strerror(saved_errno), saved_errno);
		return saved_errno;
	}
	while (fcntl(slot->lock_fd, F_SETLK, &lock) < 0) {
		int saved_errno = errno;
		long tDiff = cgit_ts_ms_sub_current(tStart);
		if (EAGAIN != saved_errno ||
		    tDiff >= ctx.cfg.cache_lock_timeout) {
			close_lock(slot);
			cache_log("[cgit] Lock (%ldms): Unable to lock slot %s (%s): %s (%d)\n",
				  tDiff, slot->lock_name,
				  slot->key, strerror(saved_errno), saved_errno);
			return saved_errno;
		}
		++wait_count;
		usleep(100000); // 100ms sleep instead of sched_yield()
	}

	if (wait_count && ctx.cfg.log_level >= LOG_LVL_DBG) {
		cache_log("[cgit] Lock: Waited %ldms (%zu tries, cache_fd %d) to lock slot %s (%s)\n",
			  cgit_ts_ms_sub_current(tStart), wait_count, slot->cache_fd,
              slot->lock_name, slot->key);
	}
	if (slot->cache_fd <= 0) {
		int err = open_slot(slot);
		if (!err && slot->match) {
			if(!is_expired(slot)) {
				// concurrent process wrote the file
				if (ctx.cfg.log_level >= LOG_LVL_WARN) {
					cache_log("[cgit] Lock: Concurrent produced slot %s (%s)\n",
						  slot->lock_name, slot->key);
				}
				unlock_slot(slot, UNLINK_LOCK_FILE);
				close_lock(slot);
				return 0;
			}
			// overwrite expired file ...
			if (ctx.cfg.log_level >= LOG_LVL_WARN) {
				cache_log("[cgit] Lock: Dropping expired slot %s (%s)\n",
					  slot->lock_name, slot->key);
			}
		}
		close_slot(slot);
	}
	if (ftruncate(slot->lock_fd, 0) < 0) {
		int saved_errno = errno;
		cache_log("[cgit] Lock (%ldms): Unable to truncate locked slot %s (%s): %s (%d)\n",
			  cgit_ts_ms_sub_current(tStart), slot->lock_name,
			  slot->key, strerror(saved_errno), saved_errno);
		unlock_slot(slot, UNLINK_LOCK_FILE);
		close_lock(slot);
		return saved_errno;
	}
	if (xwrite(slot->lock_fd, slot->key, slot->keylen + 1) < 0) {
		int saved_errno = errno;
		cache_log("[cgit] Lock (%ldms): Unable to write to locked slot %s (%s): %s (%d)\n",
			  cgit_ts_ms_sub_current(tStart), slot->lock_name,
			  slot->key, strerror(saved_errno), saved_errno);
		unlock_slot(slot, UNLINK_LOCK_FILE);
		close_lock(slot);
		return saved_errno;
	}
	if (ctx.cfg.log_level >= LOG_LVL_DBG) {
		cache_log("[cgit] Lock (%ldms): Successful locked slot %s (%s)\n",
			  cgit_ts_ms_sub_current(tStart), slot->lock_name, slot->key);
	}
	return 0;
}

/* Release the current lockfile. If `replace_old_slot` is set the
 * lockfile replaces the old cache slot, otherwise the lockfile is
 * just deleted.
 * @param lock_file_op UNLINK_LOCK_FILE unlink or UNLINK_LOCK_FILE replace old-slot w/ lock-file
 */
static int unlock_slot(struct cache_slot *slot, enum lock_file_op_t lock_file_op)
{
	struct flock lock = {
	    .l_type = F_UNLCK,
	    .l_whence = SEEK_SET,
	    .l_start = 0,
	    .l_len = 0,
	};
	int err = 0;

	if (REPLACE_OLD_SLOT == lock_file_op) {
		if (rename(slot->lock_name, slot->cache_name)) {
			err = errno;
		}
	} else if (UNLINK_LOCK_FILE == lock_file_op) {
		if (unlink(slot->lock_name)) {
			err = errno;
		}
	}
	if (ctx.cfg.log_level < LOG_LVL_DBG && ENOENT == err) {
		err = 0; // suppress ENOENT messages
	}
	if (err) {
		cache_log("[cgit] Unlock: Failed to %s slot lock %s, cache %s, key %s: %s (%d)\n",
			  to_string(lock_file_op),
			  slot->lock_name, slot->cache_name, slot->key, strerror(err), err);
	}
	if (ENOENT == err) { // not an error
		err = 0;
	}
	/* Restore stdout and close the temporary FD. */
	if (slot->stdout_fd >= 0) {
		dup2(slot->stdout_fd, STDOUT_FILENO);
		close(slot->stdout_fd);
		slot->stdout_fd = -1;
	}
	if (slot->lock_fd > 0) {
		if (fcntl(slot->lock_fd, F_SETLK, &lock) < 0) {
			int saved_errno = errno;
			close(slot->lock_fd);
			slot->lock_fd = -1;
			cache_log("[cgit] Unlock: Unable to unlock slot %s (%s): %s (%d)\n",
			    slot->lock_name, slot->key, strerror(saved_errno), saved_errno);
			if (!err)
				err = saved_errno;
		}
	}
	if (!err) {
		if (ctx.cfg.log_level >= LOG_LVL_DBG) {
			cache_log("[cgit] Unlock: Successful unlocked slot %s (%s)\n",
				  slot->lock_name, slot->key);
		}
	}
	return err;
}

/* Generate the content for the current cache slot by redirecting
 * stdout to the lock-fd and invoking the callback function
 */
static int fill_slot(struct cache_slot *slot)
{
	/* Preserve stdout */
	slot->stdout_fd = dup(STDOUT_FILENO);
	if (slot->stdout_fd == -1)
		return errno;

	/* Redirect stdout to lockfile */
	if (dup2(slot->lock_fd, STDOUT_FILENO) == -1)
		return errno;

	/* Generate cache content */
	slot->fn();

	/* Make sure any buffered data is flushed to the file */
	if (fflush(stdout))
		return errno;

	/* update stat info */
	if (fstat(slot->lock_fd, &slot->cache_st))
		return errno;

	return 0;
}

/* Crude implementation of 32-bit FNV-1 hash algorithm,
 * see http://www.isthe.com/chongo/tech/comp/fnv/ for details
 * about the magic numbers.
 */
#define FNV_OFFSET 0x811c9dc5
#define FNV_PRIME  0x01000193

unsigned long hash_str(const char *str)
{
	unsigned long h = FNV_OFFSET;
	unsigned char *s = (unsigned char *)str;

	if (!s)
		return h;

	while (*s) {
		h *= FNV_PRIME;
		h ^= *s++;
	}
	return h;
}

static int process_slot(struct cache_slot *slot)
{
	int err;
	struct timespec tStart;

	err = open_slot(slot);
	if (!err && slot->match && !is_expired(slot)) {
		if ((err = print_slot(slot)) != 0 && err != ETIMEDOUT) {
			cache_log("[cgit] error printing cache %s (%s): %s (%d)\n",
				  slot->cache_name, slot->key, strerror(err), err);
		}
		close_slot(slot);
		return err;
	}
	close_slot(slot);

	/* If the cache slot does not exist (or its key doesn't match the
	 * current key), lets try to create a new cache slot for this
	 * request. If this fails (for whatever reason), lets just generate
	 * the content without caching it and fool the caller to believe
	 * everything worked out (but print a warning on stdout).
	 *
	 * If the cachefile has been created between
	 * above `open_slot` and within `lock_slot`, we'll just
	 * serve the new content from the new cachefile.
	 */
	cgit_ts_current(&tStart);
	if ((err = lock_slot(slot, &tStart)) != 0) {
		if (ctx.cfg.cache_lock_fail != 200) {
			cgit_print_error_page(ctx.cfg.cache_lock_fail,
			    "Cache: Could not lock new-slot within %ldms.",
			    cgit_ts_ms_sub_current(&tStart));
		} else {
			cgit_ts_current(&tStart);
			slot->fn();
			cache_log("[cgit] Uncached fill took %ldms %s (failed lock %s)\n",
				  cgit_ts_ms_sub_current(&tStart), slot->key, slot->lock_name);
		}
		return 0;
	}
	if (slot->cache_fd <= 0) {
		// first concurrent process lock
		cgit_ts_current(&tStart);
		if ((err = fill_slot(slot)) != 0) {
			struct timespec tNow1;
			long td = cgit_ts_ms_sub(cgit_ts_current(&tNow1), &tStart);
			cache_log("[cgit] Unable to fill slot in %ldms %s (%s): %d: %s (%d)\n",
			    td, slot->lock_name, slot->key,
				ctx.cfg.cache_lock_fail, strerror(err), err);
			unlock_slot(slot, UNLINK_LOCK_FILE);
			close_lock(slot);
			if (ctx.cfg.cache_lock_fail != 200) {
				cgit_print_error_page(ctx.cfg.cache_lock_fail,
				    "Cache: Could not fill slot within %ldms.", td);
			} else {
				slot->fn();
				cache_log("[cgit] Uncached fill took %ldms %s (failed locked fill %s)\n",
					cgit_ts_ms_sub_current(&tNow1), slot->key, slot->lock_name);
			}
			return 0;
		}
		// We've got a valid cache slot in the lock file, which
		// is about to replace the old cache slot. But if we
		// release the lockfile and then try to open the new cache
		// slot, we might get a race condition with a concurrent
		// writer for the same cache slot (with a different key).
		// Lets avoid such a race by just printing the content of
		// the lock file.
		slot->cache_fd = slot->lock_fd;
		unlock_slot(slot, REPLACE_OLD_SLOT);
	} // else concurrent process produced slot (opened)
	if ((err = print_slot(slot)) != 0 && err != ETIMEDOUT) {
		cache_log("[cgit] error printing cache %s (%s): %s (%d)\n",
			  slot->cache_name, slot->key,
			  strerror(err), err);
	}
	close_slot(slot);
	return err;
}

/* Print cached content to stdout, generate the content if necessary. */
int cache_process(int size, const char *path, const char *key, int ttl,
		  cache_fill_fn fn)
{
	unsigned long hash;
	int i;
	struct strbuf filename = STRBUF_INIT;
	struct strbuf lockname = STRBUF_INIT;
	struct cache_slot slot;
	int result;

	/* If the cache is disabled, just generate the content */
	if (size <= 0 || ttl == 0) {
		fn();
		return 0;
	}

	/* Verify input, calculate filenames */
	if (!path) {
		cache_log("[cgit] Cache path not specified, caching is disabled\n");
		fn();
		return 0;
	}
	if (!key)
		key = "";
	hash = hash_str(key) % size;
	strbuf_addstr(&filename, path);
	strbuf_ensure_end(&filename, '/');
	for (i = 0; i < 8; i++) {
		strbuf_addf(&filename, "%x", (unsigned char)(hash & 0xf));
		hash >>= 4;
	}
	strbuf_addbuf(&lockname, &filename);
	strbuf_addstr(&lockname, ".lock");
	slot.fn = fn;
	slot.ttl = ttl;
	slot.stdout_fd = -1;
	slot.cache_name = filename.buf;
	slot.lock_name = lockname.buf;
	slot.key = key;
	slot.keylen = strlen(key);
	result = process_slot(&slot);

	strbuf_release(&filename);
	strbuf_release(&lockname);
	return result;
}

/* Return a strftime formatted date/time
 * NB: the result from this function is to shared memory
 */
static char *sprintftime(const char *format, time_t time)
{
	static char buf[64];
	struct tm tm;

	if (!time)
		return NULL;
	gmtime_r(&time, &tm);
	strftime(buf, sizeof(buf)-1, format, &tm);
	return buf;
}

int cache_ls(const char *path)
{
	DIR *dir;
	struct dirent *ent;
	int err = 0;
	struct cache_slot slot = { NULL };
	struct strbuf fullname = STRBUF_INIT;
	size_t prefixlen;

	if (!path) {
		cache_log("[cgit] cache path not specified\n");
		return -1;
	}
	dir = opendir(path);
	if (!dir) {
		err = errno;
		cache_log("[cgit] unable to open path %s: %s (%d)\n",
			  path, strerror(err), err);
		return err;
	}
	strbuf_addstr(&fullname, path);
	strbuf_ensure_end(&fullname, '/');
	prefixlen = fullname.len;
	while ((ent = readdir(dir)) != NULL) {
		if (strlen(ent->d_name) != 8)
			continue;
		strbuf_setlen(&fullname, prefixlen);
		strbuf_addstr(&fullname, ent->d_name);
		slot.cache_name = fullname.buf;
		if ((err = open_slot(&slot)) != 0) {
			cache_log("[cgit] unable to open path %s: %s (%d)\n",
				  fullname.buf, strerror(err), err);
			continue;
		}
		htmlf("%s %s %10"PRIuMAX" %s\n",
		      fullname.buf,
		      sprintftime("%Y-%m-%d %H:%M:%S",
				  slot.cache_st.st_mtime),
		      (uintmax_t)slot.cache_st.st_size,
		      slot.buf);
		close_slot(&slot);
	}
	closedir(dir);
	strbuf_release(&fullname);
	return 0;
}

/* Print a message to stdout */
void cache_log(const char *format, ...)
{
	char buffer[400];
	char *end = buffer + sizeof(buffer);
	char *out = buffer;
	*(end - 1) = 0;
	struct tm tNowLocal;
	time_t tNow = time(NULL);
	struct tm *tres = localtime_r(&tNow, &tNowLocal);
	if (tres == &tNowLocal) {
		// 'YYYY-mm-dd hh:mm:ss '
		out += strftime(out, 20 + 1, "%Y-%m-%d %H:%M:%S ", tres);
	}
	pid_t pid = getpid();
	out += snprintf(out, end - out, "%7d ", pid); // '  38588'
	va_list args;
	va_start(args, format);
	vsnprintf(out, end - out, format, args);
	va_end(args);
	fputs(buffer, stderr);
}
