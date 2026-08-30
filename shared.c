/* shared.c: global vars + some callback functions
 *
 * Copyright (C) 2006-2014 cgit Development Team <cgit@lists.zx2c4.com>
 *
 * Licensed under GNU General Public License v2
 *   (see COPYING for full license text)
 */

#include <limits.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#define USE_THE_REPOSITORY_VARIABLE

#include "cgit.h"

struct cgit_repolist cgit_repolist;
struct cgit_context ctx;

int chk_zero(int result, char *msg)
{
	if (result != 0)
		die_errno("%s", msg);
	return result;
}

int chk_positive(int result, char *msg)
{
	if (result <= 0)
		die_errno("%s", msg);
	return result;
}

int chk_non_negative(int result, char *msg)
{
	if (result < 0)
		die_errno("%s", msg);
	return result;
}

char *cgit_default_repo_desc = "[no description]";
struct cgit_repo *cgit_add_repo(const char *url)
{
	struct cgit_repo *ret;

	if (++cgit_repolist.count > cgit_repolist.length) {
		if (cgit_repolist.length == 0)
			cgit_repolist.length = 8;
		else
			cgit_repolist.length *= 2;
		cgit_repolist.repos = xrealloc(cgit_repolist.repos,
					       cgit_repolist.length *
					       sizeof(struct cgit_repo));
	}

	ret = &cgit_repolist.repos[cgit_repolist.count-1];
	memset(ret, 0, sizeof(struct cgit_repo));
	ret->url = trim_end(url, '/');
	*strchrnul(ret->url, '\n') = '\0';
	ret->name = ret->url;
	ret->path = NULL;
	ret->desc = cgit_default_repo_desc;
	ret->extra_head_content = NULL;
	ret->owner = NULL;
	ret->homepage = NULL;
	ret->section = ctx.cfg.section;
	ret->snapshots = ctx.cfg.snapshots;
	ret->enable_blame = ctx.cfg.enable_blame;
	ret->enable_commit_graph = ctx.cfg.enable_commit_graph;
	ret->enable_follow_links = ctx.cfg.enable_follow_links;
	ret->enable_log_filecount = ctx.cfg.enable_log_filecount;
	ret->enable_log_linecount = ctx.cfg.enable_log_linecount;
	ret->enable_remote_branches = ctx.cfg.enable_remote_branches;
	ret->enable_subject_links = ctx.cfg.enable_subject_links;
	ret->enable_html_serving = ctx.cfg.enable_html_serving;
	ret->max_stats = ctx.cfg.max_stats;
	ret->branch_sort = ctx.cfg.branch_sort;
	ret->commit_sort = ctx.cfg.commit_sort;
	ret->module_link = ctx.cfg.module_link;
	ret->readme = ctx.cfg.readme;
	ret->mtime = -1;
	ret->about_filter = ctx.cfg.about_filter;
	ret->commit_filter = ctx.cfg.commit_filter;
	ret->source_filter = ctx.cfg.source_filter;
	ret->email_filter = ctx.cfg.email_filter;
	ret->owner_filter = ctx.cfg.owner_filter;
	ret->clone_url = ctx.cfg.clone_url;
	ret->submodules.strdup_strings = 1;
	ret->hide = ret->ignore = 0;
	return ret;
}

struct cgit_repo *cgit_get_repoinfo(const char *url)
{
	int i;
	struct cgit_repo *repo;

	for (i = 0; i < cgit_repolist.count; i++) {
		repo = &cgit_repolist.repos[i];
		if (repo->ignore)
			continue;
		if (!strcmp(repo->url, url))
			return repo;
	}
	return NULL;
}

void cgit_free_commitinfo(struct commitinfo *info)
{
	free(info->author);
	free(info->author_email);
	free(info->committer);
	free(info->committer_email);
	free(info->subject);
	free(info->msg);
	free(info->msg_encoding);
	free(info);
}

char *trim_end(const char *str, char c)
{
	int len;

	if (str == NULL)
		return NULL;
	len = strlen(str);
	while (len > 0 && str[len - 1] == c)
		len--;
	if (len == 0)
		return NULL;
	return xstrndup(str, len);
}

char *ensure_end(const char *str, char c)
{
	size_t len = strlen(str);
	char *result;

	if (len && str[len - 1] == c)
		return xstrndup(str, len);

	result = xmalloc(len + 2);
	memcpy(result, str, len);
	result[len] = '/';
	result[len + 1] = '\0';
	return result;
}

void strbuf_ensure_end(struct strbuf *sb, char c)
{
	if (!sb->len || sb->buf[sb->len - 1] != c)
		strbuf_addch(sb, c);
}

void cgit_add_ref(struct reflist *list, struct refinfo *ref)
{
	size_t size;

	if (list->count >= list->alloc) {
		list->alloc += (list->alloc ? list->alloc : 4);
		size = list->alloc * sizeof(struct refinfo *);
		list->refs = xrealloc(list->refs, size);
	}
	list->refs[list->count++] = ref;
}

static struct refinfo *cgit_mk_refinfo(const char *refname, const struct object_id *oid)
{
	struct refinfo *ref;

	ref = xmalloc(sizeof (struct refinfo));
	ref->refname = xstrdup(refname);
	ref->object = parse_object(the_repository, oid);
	switch (ref->object->type) {
	case OBJ_TAG:
		ref->tag = cgit_parse_tag((struct tag *)ref->object);
		break;
	case OBJ_COMMIT:
		ref->commit = cgit_parse_commit((struct commit *)ref->object);
		break;
	}
	return ref;
}

void cgit_free_taginfo(struct taginfo *tag)
{
	if (tag->tagger)
		free(tag->tagger);
	if (tag->tagger_email)
		free(tag->tagger_email);
	if (tag->msg)
		free(tag->msg);
	free(tag);
}

static void cgit_free_refinfo(struct refinfo *ref)
{
	if (ref->refname)
		free((char *)ref->refname);
	switch (ref->object->type) {
	case OBJ_TAG:
		cgit_free_taginfo(ref->tag);
		break;
	case OBJ_COMMIT:
		cgit_free_commitinfo(ref->commit);
		break;
	}
	free(ref);
}

void cgit_free_reflist_inner(struct reflist *list)
{
	int i;

	for (i = 0; i < list->count; i++) {
		cgit_free_refinfo(list->refs[i]);
	}
	free(list->refs);
}

int cgit_refs_cb(const struct reference *ref, void *cb_data)
{
	struct reflist *list = (struct reflist *)cb_data;
	struct refinfo *info = cgit_mk_refinfo(ref->name, ref->oid);

	if (info)
		cgit_add_ref(list, info);
	return 0;
}

void cgit_diff_tree_cb(struct diff_queue_struct *q,
		       struct diff_options *options, void *data)
{
	int i;

	for (i = 0; i < q->nr; i++) {
		if (q->queue[i]->status == 'U')
			continue;
		((filepair_fn)data)(q->queue[i]);
	}
}

static int load_mmfile(mmfile_t *file, const struct object_id *oid)
{
	enum object_type type;

	if (is_null_oid(oid)) {
		file->ptr = (char *)"";
		file->size = 0;
	} else {
		file->ptr = odb_read_object(the_repository->objects, oid, &type,
		                           (unsigned long *)&file->size);
	}
	return 1;
}

/*
 * Receive diff-buffers from xdiff and concatenate them as
 * needed across multiple callbacks.
 *
 * This is basically a copy of xdiff-interface.c/xdiff_outf(),
 * ripped from git and modified to use globals instead of
 * a special callback-struct.
 */
static char *diffbuf = NULL;
static int buflen = 0;

static int filediff_cb(void *priv, mmbuffer_t *mb, int nbuf)
{
	int i;

	for (i = 0; i < nbuf; i++) {
		if (mb[i].ptr[mb[i].size-1] != '\n') {
			/* Incomplete line */
			diffbuf = xrealloc(diffbuf, buflen + mb[i].size);
			memcpy(diffbuf + buflen, mb[i].ptr, mb[i].size);
			buflen += mb[i].size;
			continue;
		}

		/* we have a complete line */
		if (!diffbuf) {
			((linediff_fn)priv)(mb[i].ptr, mb[i].size);
			continue;
		}
		diffbuf = xrealloc(diffbuf, buflen + mb[i].size);
		memcpy(diffbuf + buflen, mb[i].ptr, mb[i].size);
		((linediff_fn)priv)(diffbuf, buflen + mb[i].size);
		free(diffbuf);
		diffbuf = NULL;
		buflen = 0;
	}
	if (diffbuf) {
		((linediff_fn)priv)(diffbuf, buflen);
		free(diffbuf);
		diffbuf = NULL;
		buflen = 0;
	}
	return 0;
}

int cgit_diff_files(const struct object_id *old_oid,
		    const struct object_id *new_oid, unsigned long *old_size,
		    unsigned long *new_size, int *binary, int context,
		    int ignorews, linediff_fn fn)
{
	mmfile_t file1, file2;
	xpparam_t diff_params;
	xdemitconf_t emit_params;
	xdemitcb_t emit_cb;

	if (!load_mmfile(&file1, old_oid) || !load_mmfile(&file2, new_oid))
		return 1;

	*old_size = file1.size;
	*new_size = file2.size;

	if ((file1.ptr && buffer_is_binary(file1.ptr, file1.size)) ||
	    (file2.ptr && buffer_is_binary(file2.ptr, file2.size))) {
		*binary = 1;
		if (file1.size)
			free(file1.ptr);
		if (file2.size)
			free(file2.ptr);
		return 0;
	}

	memset(&diff_params, 0, sizeof(diff_params));
	memset(&emit_params, 0, sizeof(emit_params));
	memset(&emit_cb, 0, sizeof(emit_cb));
	diff_params.flags = XDF_NEED_MINIMAL;
	if (ignorews)
		diff_params.flags |= XDF_IGNORE_WHITESPACE;
	emit_params.ctxlen = context > 0 ? context : 3;
	emit_params.flags = XDL_EMIT_FUNCNAMES;
	emit_cb.out_line = filediff_cb;
	emit_cb.priv = fn;
	xdl_diff(&file1, &file2, &diff_params, &emit_params, &emit_cb);
	if (file1.size)
		free(file1.ptr);
	if (file2.size)
		free(file2.ptr);
	return 0;
}

void cgit_diff_tree(const struct object_id *old_oid,
		    const struct object_id *new_oid,
		    filepair_fn fn, const char *prefix, int ignorews)
{
	struct diff_options opt;
	struct pathspec_item *item;

	repo_diff_setup(the_repository, &opt);
	opt.output_format = DIFF_FORMAT_CALLBACK;
	opt.detect_rename = 1;
	opt.rename_limit = ctx.cfg.renamelimit;
	opt.flags.recursive = 1;
	if (ignorews)
		DIFF_XDL_SET(&opt, IGNORE_WHITESPACE);
	opt.format_callback = cgit_diff_tree_cb;
	opt.format_callback_data = fn;
	if (prefix) {
		item = xcalloc(1, sizeof(*item));
		item->match = xstrdup(prefix);
		item->len = strlen(prefix);
		opt.pathspec.nr = 1;
		opt.pathspec.items = item;
	}
	diff_setup_done(&opt);

	if (old_oid && !is_null_oid(old_oid))
		diff_tree_oid(old_oid, new_oid, "", &opt);
	else
		diff_root_tree_oid(new_oid, "", &opt);
	diffcore_std(&opt);
	diff_flush(&opt);
}

void cgit_diff_commit(struct commit *commit, filepair_fn fn, const char *prefix)
{
	const struct object_id *old_oid = NULL;

	if (commit->parents)
		old_oid = &commit->parents->item->object.oid;
	cgit_diff_tree(old_oid, &commit->object.oid, fn, prefix,
		       ctx.qry.ignorews);
}

int cgit_parse_snapshots_mask(const char *str)
{
	struct string_list tokens = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	const struct cgit_snapshot_format *f;
	int rv = 0;

	/* favor legacy setting */
	if (atoi(str))
		return 1;

	if (strcmp(str, "all") == 0)
		return INT_MAX;

	string_list_split(&tokens, str, " ", -1);
	string_list_remove_empty_items(&tokens, 0);

	for_each_string_list_item(item, &tokens) {
		for (f = cgit_snapshot_formats; f->suffix; f++) {
			if (!strcmp(item->string, f->suffix) ||
			    !strcmp(item->string, f->suffix + 1)) {
				rv |= cgit_snapshot_format_bit(f);
				break;
			}
		}
	}

	string_list_clear(&tokens, 0);
	return rv;
}

typedef struct {
	char * name;
	char * value;
} cgit_env_var;

void cgit_prepare_repo_env(struct cgit_repo * repo)
{
	cgit_env_var env_vars[] = {
		{ .name = "CGIT_REPO_URL", .value = repo->url },
		{ .name = "CGIT_REPO_NAME", .value = repo->name },
		{ .name = "CGIT_REPO_PATH", .value = repo->path },
		{ .name = "CGIT_REPO_OWNER", .value = repo->owner },
		{ .name = "CGIT_REPO_DEFBRANCH", .value = repo->defbranch },
		{ .name = "CGIT_REPO_SECTION", .value = repo->section },
		{ .name = "CGIT_REPO_CLONE_URL", .value = repo->clone_url }
	};
	int env_var_count = ARRAY_SIZE(env_vars);
	cgit_env_var *p, *q;
	static char *warn = "cgit warning: failed to set env: %s=%s\n";

	p = env_vars;
	q = p + env_var_count;
	for (; p < q; p++)
		if (p->value && setenv(p->name, p->value, 1))
			fprintf(stderr, warn, p->name, p->value);
}

/* Read the content of the specified file into a newly allocated buffer,
 * zeroterminate the buffer, truncate at a new line, and return 0 on success,
 * errno otherwise.
 */
int read_first_line(const char *path, char **buf, size_t *size)
{
	int fd, e;
	struct stat st;

	fd = open(path, O_RDONLY);
	if (fd == -1)
		return errno;
	if (fstat(fd, &st)) {
		e = errno;
		close(fd);
		return e;
	}
	if (!S_ISREG(st.st_mode)) {
		close(fd);
		return EISDIR;
	}
	*buf = xmalloc(st.st_size + 1);
	*size = read_in_full(fd, *buf, st.st_size);
	e = errno;
	(*buf)[*size] = '\0';
	*strchrnul(*buf, '\n') = '\0';
	close(fd);
	return (*size == st.st_size ? 0 : e);
}

char *strdup_first_line(const char *txt)
{
	char *t = xstrdup(txt);
	*strchrnul(t, '\n') = '\0';
	return t;
}

static int is_token_char(char c)
{
	return isalnum(c) || c == '_';
}

/* Replace name with getenv(name), return pointer to zero-terminating char
 */
static char *expand_macro(char *name, int maxlength)
{
	char *value;
	size_t len;

	len = 0;
	value = getenv(name);
	if (value) {
		len = strlen(value) + 1;
		if (len > maxlength)
			len = maxlength;
		strlcpy(name, value, len);
		--len;
	}
	return name + len;
}

#define EXPBUFSIZE (1024 * 8)

/* Replace all tokens prefixed by '$' in the specified text with the
 * value of the named environment variable.
 * NB: the return value is a static buffer, i.e. it must be strdup'd
 * by the caller.
 */
char *expand_macros(const char *txt)
{
	static char result[EXPBUFSIZE];
	char *p, *start;
	int len;

	p = result;
	start = NULL;
	while (p < result + EXPBUFSIZE - 1 && txt && *txt) {
		*p = *txt;
		if (start) {
			if (!is_token_char(*txt)) {
				if (p - start > 0) {
					*p = '\0';
					len = result + EXPBUFSIZE - start - 1;
					p = expand_macro(start, len) - 1;
				}
				start = NULL;
				txt--;
			}
			p++;
			txt++;
			continue;
		}
		if (*txt == '$') {
			start = p;
			txt++;
			continue;
		}
		p++;
		txt++;
	}
	*p = '\0';
	if (start && p - start > 0) {
		len = result + EXPBUFSIZE - start - 1;
		p = expand_macro(start, len);
		*p = '\0';
	}
	return result;
}

char *get_mimetype_for_filename(const char *filename)
{
	const char *ext;
	char *mimetype, line[1024];
	struct string_list list = STRING_LIST_INIT_NODUP;
	int i;
	FILE *file;
	struct string_list_item *mime;

	if (!filename)
		return NULL;

	ext = strrchr(filename, '.');
	if (!ext)
		return NULL;
	++ext;
	if (!ext[0])
		return NULL;
	mime = string_list_lookup(&ctx.cfg.mimetypes, ext);
	if (mime)
		return xstrdup(mime->util);

	if (!ctx.cfg.mimetype_file)
		return NULL;
	file = fopen(ctx.cfg.mimetype_file, "r");
	if (!file)
		return NULL;
	while (fgets(line, sizeof(line), file)) {
		if (!line[0] || line[0] == '#')
			continue;
		string_list_split_in_place(&list, line, " \t\r\n", -1);
		string_list_remove_empty_items(&list, 0);
		mimetype = list.items[0].string;
		for (i = 1; i < list.nr; i++) {
			if (!strcasecmp(ext, list.items[i].string)) {
				fclose(file);
				return xstrdup(mimetype);
			}
		}
		string_list_clear(&list, 0);
	}
	fclose(file);
	return NULL;
}

#define NS_PER_SEC 1000000000
#define NS_PER_MS 1000000
#define MS_PER_SEC 1000
#define LONG_SEC_MAX_FOR_MS (LONG_MAX / MS_PER_SEC)

/**
 * Normalize tv_nsec with its absolute range [0..1'000'000'000[ or [0..1'000'000'000)
 * and having same sign as tv_sec.
 *
 * Used after arithmetic operations.
 *
 * @returns passed timespec instance
 */
struct timespec *cgit_ts_normalize(struct timespec *ts) {
    if ( 0 != ts->tv_nsec ) {
        if ( labs(ts->tv_nsec) >= NS_PER_SEC ) {
            const int64_t c = ts->tv_nsec / NS_PER_SEC;
            ts->tv_nsec -= c * NS_PER_SEC;
            ts->tv_sec += c;
        }
        if ( ts->tv_nsec < 0 && ts->tv_sec >= 1 ) {
            ts->tv_nsec += NS_PER_SEC;
            ts->tv_sec -= 1;
        } else if ( ts->tv_nsec > 0 && ts->tv_sec <= -1 ) {
            ts->tv_nsec -= NS_PER_SEC;
            ts->tv_sec += 1;
        }
    }
	return ts;
}

long cgit_ts_to_ms(const struct timespec *ts)
{
	if ( ts->tv_sec < 0 || ts->tv_nsec < 0 ) {
	    return 0;
	}
	return ts->tv_sec < LONG_SEC_MAX_FOR_MS ? ts->tv_sec * MS_PER_SEC + ts->tv_nsec / NS_PER_MS : LONG_MAX;
}

struct timespec *cgit_ts_add(struct timespec *tsr, const struct timespec *ts1, const struct timespec *ts2)
{
	tsr->tv_nsec = ts1->tv_nsec + ts2->tv_nsec;  // we allow the 'overflow' over 1'000'000'000, fitting into type and normalize() later
	tsr->tv_sec  = ts1->tv_sec  + ts2->tv_sec;
	return cgit_ts_normalize(tsr);
}

struct timespec *cgit_ts_sub(struct timespec *tsr, const struct timespec *ts1, const struct timespec *ts2)
{
	tsr->tv_nsec = ts1->tv_nsec - ts2->tv_nsec;  // we allow the 'overflow' over 1'000'000'000, fitting into type and normalize() later
	tsr->tv_sec  = ts1->tv_sec  - ts2->tv_sec;
	return cgit_ts_normalize(tsr);
}

long cgit_ts_ms_sub(const struct timespec *ts1, const struct timespec *ts2)
{
	struct timespec tsr;
	return cgit_ts_to_ms( cgit_ts_sub(&tsr, ts1, ts2) );
}

/**
 * Returns an integer indicating the result of the comparison, as follows:
 * • 0, if the lhs and rhs are equal;
 * • a negative value if lhs is less than rhs;
 * • a positive value if lhs is greater than rhs.
 */
int cgit_ts_cmp(const struct timespec* lhs, const struct timespec* rhs) {
    if ( lhs->tv_sec == rhs->tv_sec ) {
        return lhs->tv_nsec == rhs->tv_nsec ? 0 : (lhs->tv_nsec < rhs->tv_nsec ? -1 : 1);
    }
    return lhs->tv_sec < rhs->tv_sec ? -1 : 1;
}

/**
 * Gets the current monotonic clock, good enough to measure time delta but not wallclock.
 * @param ts timespec storage
 * @return the passed timespec storage
 */
struct timespec *cgit_ts_current(struct timespec *ts)
{
	clock_gettime(CLOCK_MONOTONIC, ts);
	return ts;
}

long cgit_ts_ms_sub_current(const struct timespec *ts)
{
	struct timespec tNow, tDiff;
	return cgit_ts_to_ms( cgit_ts_sub(&tDiff, cgit_ts_current(&tNow), ts) );
}



#define MY_MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MY_MAX(X, Y) (((X) > (Y)) ? (X) : (Y))

/**
 * @param to_max maximum time for write transfer until timeout in milliseconds
 */
ssize_t cgit_write_to(int fd, const void *buf, size_t count, off_t *total_out,
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
			if (fd == STDOUT_FILENO) {
				cgit_sentnotify_term(written);
			}
			if (!count)
				return total;
		}
	} while (1);
}

/* Print a message to stderr, similar to `Apache2` error log */
__attribute__((format (printf,1,2)))
ssize_t cgit_log(const char *format, ...)
{
	char buffer[400];
	char *end = buffer + sizeof(buffer);
	char *out = buffer;
	*(end - 1) = 0;
	struct tm tNowLocal;
	time_t tNow = time(NULL);
	struct tm *tres = localtime_r(&tNow, &tNowLocal);
	// Apache2: [Fri Aug 28 02:41:07.739456 2026] [cgid:error] [pid 2940541:tid 2940569] [client 1.1.1.1:2222] MESSAGE
	// cgit:    [Fri Aug 28 02:58:17 2026] [cgit] [pid 1143667] [client 1.1.1.1:2222] MESSAGE
	if (tres == &tNowLocal) {
		out += strftime(out, 30 + 1, "[%a %b %d %H:%M:%S %Y] ", tres);    // '[Day Mon dd hh:mm:ss YYYY] '
	}
	pid_t pid = getpid();
	out += snprintf(out, end - out, "[cgit] [pid %7d] [client %s:%d] ", // '  38588'
		pid, ctx.env.remote_addr, ctx.env.remote_port);
	va_list args;
	va_start(args, format);
	vsnprintf(out, end - out, format, args);
	va_end(args);
	return fputs(buffer, stderr);
}

/** str[size] including EOS. */
size_t to_decstr(char *dest, size_t dest_sz, const long val, const char separator) {
	if (!dest_sz) {
		return 0;
	}
	unsigned long v = (unsigned long)val;
	const uint32_t sign_len = val >= 0 ? 0 : 1;
	// reverse output (relieves us from needing log10 for digits)
	const char* const d_end = dest + dest_sz - 1; // EOS reserved
	const char* const d_end_num = d_end - sign_len;
	char* e = dest;
	*e = 0;
	uint32_t digit_cnt = 0;
	while (e < d_end_num && (v!=0 || e==dest)) {
		if (separator && 0 < digit_cnt && 0 == digit_cnt % 3) {
			*(++e) = separator;
		}
		if (e < d_end_num) {
			*(++e) = '0' + (v % 10);
			v /= 10;
			++digit_cnt;
		}
	}
	if (sign_len && e < d_end) {
		*(--e) = '-';
	}
	size_t len = e - dest; // w/o EOS
	// reverse
	char * s = dest;
	char t;
	while (e>s) {
		t = *s;
		*s = *e;
		*e = t;
		++s;
		--e;
	}
	return len;
}

char *cgit_appendl(char *dest, size_t *dest_sz, const long val, const char separator)
{
	char src[100];
	size_t len = MY_MIN(*dest_sz - 1, to_decstr(src, 100, val, separator));
	if (!len) {
		return dest;
	}
	mempcpy(dest, src, len);
	*dest_sz -= len;
	if (*dest_sz>0) {
		dest[len]=0;
	}
	return dest + len;
}

char *cgit_appends(char *dest, size_t *dest_sz, const char *src)
{
	size_t len = strnlen(src, *dest_sz-1);
	if (!len) {
		return dest;
	}
	mempcpy(dest, src, len);
	*dest_sz -= len;
	if (*dest_sz>0) {
		dest[len]=0;
	}
	return dest + len;
}

char *cgit_append_log(char *dest, size_t *dest_sz, const char *start_timestr, long elapsedMS, const char *src)
{
	// [Fri Aug 28 02:58:17 2026 + 1'000ms] [cgit] [pid 1143667] [client 1.1.1.1:2222] MESSAGE
	dest = cgit_appends(dest, dest_sz, "[");
	dest = cgit_appends(dest, dest_sz, start_timestr);
	dest = cgit_appends(dest, dest_sz, " + ");
	dest = cgit_appendl(dest, dest_sz, elapsedMS, '\'');
	dest = cgit_appends(dest, dest_sz, "ms] [cgit] [pid ");
	dest = cgit_appendl(dest, dest_sz, (long)getpid(), 0);
	dest = cgit_appends(dest, dest_sz, "] [client ");
	dest = cgit_appends(dest, dest_sz, ctx.env.remote_addr);
	dest = cgit_appends(dest, dest_sz, ":");
	dest = cgit_appendl(dest, dest_sz, ctx.env.remote_port, 0);
	dest = cgit_appends(dest, dest_sz, "] ");
	return cgit_appends(dest, dest_sz, src);
}

typedef void (*sighandler_t)(int);

enum { max_proc = 16 };

struct term_ctx_t {
	int stdout_fd;
	int stderr_fd;
	struct timespec start;
	char start_timestr[32];
	const char* query;
	sighandler_t sh_alrm_orig;
	volatile pid_t child_proc[max_proc];
	volatile size_t child_count;
	char mark[80];
	volatile size_t mark_len;
	volatile size_t sent_to_client; /* known bytes sent to client */
	volatile int    sent_to_client_mask; /* don't accumulate bytes sent to client */
	volatile int initialized;	/* TODO: make it thread-safe if desired */
};
static struct term_ctx_t term_ctx = {0};

/* fork() wrapper, registering child pid to shutdown handler */
pid_t cgit_fork() {
	const pid_t p = fork();
	if (p>0 && term_ctx.child_count<max_proc) {
		term_ctx.child_proc[term_ctx.child_count++] = p;
	}
	return p;
}

static void cgit_kill_child_proc() {
	size_t n = term_ctx.child_count;
	term_ctx.child_count = 0;
	for(size_t i = n; i-- > 0; ) {
		kill(term_ctx.child_proc[i], SIGTERM);
	}
}

static void cgit_alarm_sighandler(int sig)
{
	if (sig != SIGALRM || !term_ctx.initialized) {
		return;
	}
	/* Restore stdout, stderr. */
	if (term_ctx.stdout_fd >= 0) {
		dup2(term_ctx.stdout_fd, STDOUT_FILENO);
		term_ctx.stdout_fd = -1;
	}
	if (term_ctx.stderr_fd >= 0) {
		dup2(term_ctx.stderr_fd, STDERR_FILENO);
		term_ctx.stderr_fd = -1;
	}
	{
		static struct timespec now;
		cgit_ts_current(&now);
		const long dt_ms = cgit_ts_ms_sub(&now, &term_ctx.start);
		size_t error_page_bytes = 0;
		size_t error_page_bytes_sent = 0;
		int error_page_errno = 0;
		long dt_sent = 0;

		if (0 == term_ctx.sent_to_client) {
			static const char error_page[] =
			    "Status: 429 Too Many Requests\n"
			    "Content-type: text/html; charset=UTF-8\n"
			    "Retry-After: 42\n"
			    "\n"
			    "<!DOCTYPE html>\n"
			    "<html lang='en'><head><title>429 - cgit error</title>"
			    "<meta name='generator' content='cgit '/><meta name='robots' content='index, nofollow'/></head>\n"
			    "<body><p>cgit is currently being overrun by bots. Please try again later.</p></body></html>\n";
			error_page_bytes = sizeof(error_page);
			const ssize_t wres = write(STDOUT_FILENO, error_page, error_page_bytes);
			if (wres >= 0) {
				error_page_bytes_sent = wres;
			} else {
				error_page_errno = errno;
			}
			dt_sent = cgit_ts_ms_sub_current(&now);
		}
		char dest_mem[512];
		size_t dest_sz = sizeof(dest_mem);
		char *dest = dest_mem;
		dest = cgit_append_log(dest, &dest_sz, term_ctx.start_timestr, dt_ms, "SIGALRM @ '");
		if (term_ctx.mark_len) {
			dest = cgit_appends(dest, &dest_sz, term_ctx.mark);
		} else {
			dest = cgit_appends(dest, &dest_sz, "undef");
		}
		dest = cgit_appends(dest, &dest_sz, "', ");
		if (0 == term_ctx.sent_to_client) {
			dest = cgit_appends(dest, &dest_sz, "error-page[");
			if (error_page_errno) {
				dest = cgit_appends(dest, &dest_sz, "error ");
				dest = cgit_appendl(dest, &dest_sz, (long)error_page_errno, 0);
				dest = cgit_appends(dest, &dest_sz, " while sending ");
			} else {
				dest = cgit_appendl(dest, &dest_sz, (long)error_page_bytes_sent, '\'');
				dest = cgit_appends(dest, &dest_sz, "B/");
			}
			dest = cgit_appendl(dest, &dest_sz, (long)error_page_bytes, '\'');
			dest = cgit_appends(dest, &dest_sz, "B in ");
			dest = cgit_appendl(dest, &dest_sz, (long)dt_sent, '\'');
			dest = cgit_appends(dest, &dest_sz, "ms] sent, childs ");
		} else {
			dest = cgit_appendl(dest, &dest_sz, (long)term_ctx.sent_to_client, '\'');
			dest = cgit_appends(dest, &dest_sz, "B sent, childs ");
		}
		dest = cgit_appendl(dest, &dest_sz, (long)term_ctx.child_count, '\'');
		dest = cgit_appends(dest, &dest_sz, ", query ");
		dest = cgit_appends(dest, &dest_sz, term_ctx.query);
		dest = cgit_appends(dest, &dest_sz, "\n");
		write(STDERR_FILENO, dest_mem, dest-dest_mem);
	}
	if (term_ctx.sh_alrm_orig == SIG_IGN) {
		cgit_kill_child_proc();
		_exit(EXIT_FAILURE);
	} else if (term_ctx.sh_alrm_orig == SIG_DFL) {
		signal(sig, SIG_DFL);
		cgit_kill_child_proc();
		raise(sig);
		// should never be reached
		_exit(EXIT_FAILURE);
	} else {
		// custom handler
		cgit_kill_child_proc();
		term_ctx.sh_alrm_orig(sig);
	}
}

void cgit_mark_term(const char *mark) {
	const size_t dlen = strnlen(mark, sizeof(term_ctx.mark)-1);
	term_ctx.mark_len = 0;
	memset(term_ctx.mark, 0, sizeof(term_ctx.mark));
	mempcpy(term_ctx.mark, mark, dlen);
	term_ctx.mark_len = dlen;
}
__attribute__((format (printf,1,2)))
void cgit_mark_termf(const char *format, ...) {
	va_list args;
	va_start(args, format);
	term_ctx.mark_len = 0;
	memset(term_ctx.mark, 0, sizeof(term_ctx.mark));
	term_ctx.mark_len = vsnprintf(term_ctx.mark, sizeof(term_ctx.mark), format, args);
	va_end(args);
}

void cgit_sentnotify_term(size_t bytes) {
	if (!term_ctx.sent_to_client_mask) {
		term_ctx.sent_to_client = term_ctx.sent_to_client + bytes;
	}
}

void cgit_sentmask_term(int mask) {
	term_ctx.sent_to_client_mask = mask;
}

void cgit_sentreset_term() {
	term_ctx.sent_to_client = 0;
}

/* Essential for cgit instances w/o receiving signals from httpd, e.g. Apache2 + suEXEC. */
void cgit_install_alarm(int timeout, const char* query) {
	if (timeout <= 0 || term_ctx.initialized) {
		return;
	}
	term_ctx.stdout_fd = dup(STDOUT_FILENO);
	term_ctx.stderr_fd = dup(STDERR_FILENO);
	cgit_ts_current(&term_ctx.start);
	{
		struct tm tNowLocal;
		time_t tNow = time(NULL);
		struct tm *tres = localtime_r(&tNow, &tNowLocal);
		if (tres == &tNowLocal) {
			strftime(term_ctx.start_timestr, sizeof(term_ctx.start_timestr), "%a %b %d %H:%M:%S %Y", tres);    // 'Day Mon dd hh:mm:ss YYYY'
		} else {
			term_ctx.start_timestr[0]=0;
		}
	}
	term_ctx.query = query;
	memset(term_ctx.mark, 0, sizeof(term_ctx.mark));

	struct sigaction new_act = {0};
	struct sigaction old_act = {0};
	new_act.sa_handler = &cgit_alarm_sighandler;
	if (sigaction(SIGALRM, &new_act, &old_act) == -1)
	{
		cgit_log("Unable to install SIGALRM handler\n");
		exit(-1);
	}
	term_ctx.sh_alrm_orig = old_act.sa_handler;
	term_ctx.initialized = 1;
	alarm(timeout);
}
