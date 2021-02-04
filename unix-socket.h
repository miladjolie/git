#ifndef UNIX_SOCKET_H
#define UNIX_SOCKET_H

struct unix_stream_listen_opts {
	int listen_backlog_size;
	unsigned int force_unlink_before_bind:1;
	unsigned int disallow_chdir:1;
};

#define UNIX_STREAM_LISTEN_OPTS_INIT \
{ \
	.listen_backlog_size = 5, \
	.force_unlink_before_bind = 1, \
	.disallow_chdir = 0, \
}

int unix_stream_connect(const char *path);
int unix_stream_listen(const char *path,
		       const struct unix_stream_listen_opts *opts);

#endif /* UNIX_SOCKET_H */
