#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

#define COOLDOWN_START_MS    100
#define COOLDOWN_BACKOFF     1.15
#define COOLDOWN_MAX_MS      30 * 1000
#define COOLDOWN_RESET_AFTER 600 /* seconds */
int do_cooldown = 1;
void cooldown() {
	int rc;
	struct timespec now;
	static struct timespec last;
	static unsigned int ms = 0;

	if (!do_cooldown)
		goto no_cooldown;

	if (ms == 0) { /* initialize */
		ms = COOLDOWN_START_MS;
		rc = clock_gettime(CLOCK_MONOTONIC, &last);
		if (rc == -1)
			goto failed;
	}

	rc = clock_gettime(CLOCK_MONOTONIC, &now);
	if (rc == -1)
		goto failed;

	if (now.tv_sec - last.tv_sec - 1 > COOLDOWN_RESET_AFTER)
		ms = COOLDOWN_START_MS;

	last.tv_sec  = now.tv_sec;
	last.tv_nsec = now.tv_nsec;
	usleep(ms * 1000);

	ms = ms * COOLDOWN_BACKOFF;
	if (ms > COOLDOWN_MAX_MS)
		ms = COOLDOWN_MAX_MS;
	return;

failed:
	fprintf(stderr, "failed to retrieve monotonic time: %s (error %d)\n", strerror(errno), errno);
no_cooldown:
	do_cooldown = 0;
	usleep(COOLDOWN_MAX_MS * 1000);
}

int main(int argc, char **argv) {
	int rc, i;
	char **cmd, **envp;

	envp = argv+1;
	cmd  = NULL;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--") == 0) {
			argv[i] = NULL;
			cmd = argv+i+1;
			break;
		}
	}
	if (!cmd || !*cmd) {
		fprintf(stderr, "USAGE: %s ENV=VALUE -- command --to --run --with /arg/u/ments\n", argv[0]);
		return 1;
	}

	fprintf(stderr, "setting up execution environment:\n");
	for (i = 0; envp[i]; i++) {
		fprintf(stderr, "  %s\n", envp[i]);
	}
	fprintf(stderr, "running command %s with arguments:\n", cmd[0]);
	for (i = 1; cmd[i]; i++) {
		fprintf(stderr, "  [%d]: '%s'\n", i-1, cmd[i]);
	}

	for (;;) {
		pid_t pid;
		int status;

		pid = fork();
		if (pid < 0) continue;

		if (pid == 0) {
			execve(cmd[0], cmd, envp);
			continue;
		}

		for (;;) {
			rc = waitpid(pid, &status, 0);
			if (rc == -1 && errno == EINTR) continue;
			if (rc == -1 && errno == ECHILD) break;
			break;
		}

		cooldown();
	}
	return 0;
}
