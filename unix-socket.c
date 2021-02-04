#include "cache.h"
#include "unix-socket.h"

static int chdir_len(const char *orig, int len)
{
	char *path = xmemdupz(orig, len);
	int r = chdir(path);
	free(path);
	return r;
}

struct unix_sockaddr_context {
	char *orig_dir;
	unsigned int disallow_chdir:1;
};

#define UNIX_SOCKADDR_CONTEXT_INIT \
{ \
	.orig_dir=NULL, \
	.disallow_chdir=0, \
}

static void unix_sockaddr_cleanup(struct unix_sockaddr_context *ctx)
{
	if (!ctx->orig_dir)
		return;
	/*
	 * If we fail, we can't just return an error, since we have
	 * moved the cwd of the whole process, which could confuse calling
	 * code.  We are better off to just die.
	 */
	if (chdir(ctx->orig_dir) < 0)
		die("unable to restore original working directory");
	free(ctx->orig_dir);
}

static int unix_sockaddr_init(struct sockaddr_un *sa, const char *path,
			      struct unix_sockaddr_context *ctx)
{
	int size = strlen(path) + 1;

	if (ctx->disallow_chdir && size > sizeof(sa->sun_path)) {
		errno = ENAMETOOLONG;
		return -1;
	}

	if (size > sizeof(sa->sun_path)) {
		const char *slash = find_last_dir_sep(path);
		const char *dir;
		struct strbuf cwd = STRBUF_INIT;

		if (!slash) {
			errno = ENAMETOOLONG;
			return -1;
		}

		dir = path;
		path = slash + 1;
		size = strlen(path) + 1;
		if (size > sizeof(sa->sun_path)) {
			errno = ENAMETOOLONG;
			return -1;
		}
		if (strbuf_getcwd(&cwd))
			return -1;
		ctx->orig_dir = strbuf_detach(&cwd, NULL);
		if (chdir_len(dir, slash - dir) < 0)
			return -1;
	}

	memset(sa, 0, sizeof(*sa));
	sa->sun_family = AF_UNIX;
	memcpy(sa->sun_path, path, size);
	return 0;
}

int unix_stream_connect(const char *path)
{
	int fd = -1;
	int saved_errno;
	struct sockaddr_un sa;
	struct unix_sockaddr_context ctx = UNIX_SOCKADDR_CONTEXT_INIT;

	if (unix_sockaddr_init(&sa, path, &ctx) < 0)
		return -1;

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		goto fail;

	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0)
		goto fail;
	unix_sockaddr_cleanup(&ctx);
	return fd;

fail:
	saved_errno = errno;
	unix_sockaddr_cleanup(&ctx);
	if (fd != -1)
		close(fd);
	errno = saved_errno;
	return -1;
}

int unix_stream_listen(const char *path,
		       const struct unix_stream_listen_opts *opts)
{
	int fd = -1;
	int saved_errno;
	int bind_successful = 0;
	int backlog;
	struct sockaddr_un sa;
	struct unix_sockaddr_context ctx = UNIX_SOCKADDR_CONTEXT_INIT;

	ctx.disallow_chdir = opts->disallow_chdir;

	if (unix_sockaddr_init(&sa, path, &ctx) < 0)
		return -1;

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		goto fail;

	if (opts->force_unlink_before_bind)
		unlink(path);

	if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0)
		goto fail;
	bind_successful = 1;

	if (opts->listen_backlog_size > 0)
		backlog = opts->listen_backlog_size;
	else
		backlog = 5;
	if (listen(fd, backlog) < 0)
		goto fail;

	unix_sockaddr_cleanup(&ctx);
	return fd;

fail:
	saved_errno = errno;
	unix_sockaddr_cleanup(&ctx);
	if (fd != -1)
		close(fd);
	if (bind_successful)
		unlink(path);
	errno = saved_errno;
	return -1;
}
