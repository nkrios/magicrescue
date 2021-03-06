/*
 * Magic Rescue file extraction and post-processing
 * Copyright (C) 2004 Jonas Jensen <jbj@knef.dk>
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#include "config.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"
#include "array.h"
#include "recipe.h"
#include "magicrescue.h"

static int compose_name(
	char *name, size_t name_len, off_t offset, const char *extension)
{
    struct stat st;
    long i = 0;

    do {
	int snprintf_retval;
	if (name_mode == MODE_DEVICE) {
	    snprintf_retval = snprintf(name, name_len, "%s/%012llX-%ld.%s",
		    output_dir, (long long)offset, i++, extension);

	} else /*if (name_mode == MODE_FILES)*/ {
	    snprintf_retval = snprintf(name, name_len, "%s/%s-%ld.%s",
		    output_dir, progress.device_basename, i++, extension);
	}
	if (snprintf_retval < 0 || (size_t)snprintf_retval >= name_len) {
	    return 0;
	}
    } while (lstat(name, &st) == 0);

    return 1;
}

int run_shell(int fd, off_t offset, const char *command,
	const char *argument, int *stdout_pipe)
{
    pid_t pid;
    int status = -1;
    int pipes[2];

    if (stdout_pipe && pipe(pipes) != 0) {
	perror("pipe()");
	return -1;
    }
    if (lseek(fd, offset, SEEK_SET) == (off_t)-1) {
	perror("lseek()");
	return -1;
    }

    pid = fork();
    if (pid == 0) {

	dup2(fd, 0);
	if (stdout_pipe) {
	    dup2(pipes[1], 1);
	    close(pipes[0]);
	    close(pipes[1]);
	}
	else if (machine_output)
	    dup2(2, 1); /* send program's stdout to stderr */
	
	execl("/bin/sh", "/bin/sh", "-c", command, "sh", argument, NULL);
	perror("Executing /bin/sh");
	exit(1);

    } else if (stdout_pipe) {
	close(pipes[1]);
	*stdout_pipe = pipes[0];
	return 0;
    } else if (pid > 0) {
	wait(&status);
    }
    return status;
}

void rename_output(int fd, off_t offset, const char *command,
	char *origname)
{
    int outfd;
    char mvbuf[2*PATH_MAX]; /* it has to be semi-large */
    ssize_t got, has = 0;
    char *rename_pos = mvbuf;

    if (run_shell(fd, offset, command, origname, &outfd) != 0)
	return;

    while ((got = read(outfd, mvbuf + has, sizeof(mvbuf)-has - 1)) > 0) {
	has += got;
    }
    close(outfd);
    wait(NULL);

    mvbuf[has] = '\0';

    if (has > 7 &&
	    (strncmp(rename_pos, "RENAME ", 7) == 0 ||
	     (fprintf(stderr, "Warning: garbage on rename stdout\n"), 0) ||
	     ((rename_pos = strstr(mvbuf, "\nRENAME ")) && rename_pos++)||
	     (rename_pos = strstr(mvbuf, "RENAME ")))
       ) {
	char *nlpos;
	rename_pos += 7;
	if ((nlpos = strchr(rename_pos, '\n')))
	    *nlpos = '\0';

	if (strlen(rename_pos) < 128) {
	    char newname[PATH_MAX];
	    if (!compose_name(newname, sizeof newname, offset, rename_pos)) {
		fprintf(stderr, "Warning: Renamed file name too long\n");
		return;
	    }

	    rename(origname, newname);
	    strcpy(origname, newname);
	}
    }
}

/** Calls an external program to extract a file from fd */
off_t extract(int fd, struct recipe *r, off_t offset)
{
    char outfile[PATH_MAX];
    struct stat st;

    if (!compose_name(outfile, sizeof outfile, offset, r->extension)) {
	fprintf(stderr, "Output file name too long, skipping\n");
	return -1;
    }

    if (run_shell(fd, offset, r->command, outfile, NULL) == -1)
	return -1;

    if (stat(outfile, &st) == 0) {
	if (st.st_size < r->min_output_file) {
	    fprintf(stderr, "Output too small, removing\n");
	    unlink(outfile);

	} else {
	    if (r->rename) {
		rename_output(fd, offset, r->rename, outfile);
	    }

	    if (r->postextract) {
		run_shell(fd, offset, r->postextract, outfile, NULL);
	    }

	    if ((machine_output & OUT_IO) == OUT_IO)
		printf("o %s\n", outfile);
	    else if (machine_output & OUT_O)
		printf("%s\n", outfile);
	    else
		fprintf(stderr, "%s: %lld bytes\n", outfile, 
			(long long int)st.st_size);

	    return st.st_size;
	}

    } else {
	fprintf(stderr, "No output file\n");
    }

    return 0;
}

/* vim: ts=8 sw=4 noet tw=80
 */
