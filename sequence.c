/**
 *    Copyright (C) 2020 Graham Leggett <minfrin@sharp.fm>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define SYSLOG_NAMES 1
#include <syslog.h>

#include "config.h"

#define READ_FD 0
#define WRITE_FD 1

static struct option long_options[] =
{
    {"zero", no_argument, NULL, '0'},
    {"base", required_argument, NULL, 'b'},
    {"ignore", no_argument, NULL, 'i'},
    {"print", no_argument, NULL, 'p'},
    {"syslog", required_argument, NULL, 's'},
    {"help", no_argument, NULL, 'h'},
    {"version", no_argument, NULL, 'v'},
    {NULL, 0, NULL, 0}
};

static int help(const char *name, const char *msg, int code)
{
    const char *n;

    n = strrchr(name, '/');
    if (!n) {
        n = name;
    }
    else {
        n++;
    }

    fprintf(code ? stderr : stdout,
            "%s\n"
            "\n"
            "NAME\n"
            "  %s - Run all executables in a directory in sequence.\n"
            "\n"
            "SYNOPSIS\n"
            "  %s [-0] [-b dir] [-i] [-p] [-s facility.level] [-v] [-h] directory [options]\n"
            "\n"
            "DESCRIPTION\n"
            "\n"
            "  The sequence command runs all the executables in a specified directory,\n"
            "  running each one in sequence ordered alphabetically.\n"
            "\n"
            "  Each executable is named clearly in argv[0] or ${0}, and this\n"
            "  name is prefixed to stderr or syslog to be clear which executable is\n"
            "  responsible for output.\n"
            "\n"
            "  Sequence is an alternative to the run-parts command found in cron.\n"
            "\n"
            "OPTIONS\n"
            "  -0, --zero    Terminate names with a zero instead of newline.\n"
            "\n"
            "  -b, --base dir    Directory is relative to this base directory.\n"
            "\n"
            "  -i, --ignore  Ignore non executable files. See the note below.\n"
            "\n"
            "  -p, --print   Print the name of executables rather than execute.\n"
            "\n"
            "  -s, --syslog [facility.]level Send stderr to syslog at the given facility\n"
            "                                and level. Example: user.info\n"
            "\n"
            "  -h, --help    Display this help message.\n"
            "\n"
            "  -v, --version Display the version number.\n"
            "\n"
            "RETURN VALUE\n"
            "  The sequence tool returns the return code from the\n"
            "  first executable to fail.\n"
            "\n"
            "  If the executable was interrupted with a signal, the return\n"
            "  code is the signal number plus 128.\n"
            "\n"
            "  If the executable could not be executed, or if the options\n"
            "  are invalid, the status 1 is returned.\n"
            "\n"
            "NOTES\n"
            "  When non executable files are ignored with the -i option, sequence will\n"
            "  ignore the EACCESS result code when trying to execute the file and move\n"
            "  on to the next executable. When executables are listed with -p,\n"
            "  sequence will make a 'best effort' check as to whether it is allowed\n"
            "  to run an executable, ignoring any executables that are not regular files,\n"
            "  and ignoring any executables that do not pass an access check. Callers are\n"
            "  to take care using this information to ensure that race conditions and\n"
            "  additional restrictions like selinux do not negatively affect the outcome.\n"
            "\n"
            "EXAMPLES\n"
            "  In this basic example, we execute all commands in /etc/rc3.d, passing\n"
            "  the parameter 'start' to each command.\n"
            "\n"
            "\t~$ sequence /etc/rc3.d -- start\n"
            "\n"
            "  Here, we execute all commands in /etc/cron.d, passing stderr to syslog\n"
            "  with the level 'cron' and priority 'info'. 'cron.d/command' will be logged.\n"
            "\n"
            "\t~$ sequence -s cron.info -b /etc cron.d\n"
            "\n"
            "AUTHOR\n"
            "  Graham Leggett <minfrin@sharp.fm>\n"
            "", msg ? msg : "", n, n);
    return code;
}

static int version()
{
    printf(PACKAGE_STRING "\n");
    return 0;
}

static int
sort_strcmp(const void *p1, const void *p2)
{
    return strcmp(*(const char **) p1, *(const char **) p2);
}

static int syslog_decode(const char *name, const CODE *codetab)
{
    const CODE *c;

    for (c = codetab; c->c_name; c++) {
        if (!strcasecmp(name, c->c_name)) {
            return (c->c_val);
        }
    }

    return -1;
}

static char *syslog_details(const char *name, const CODE *codetab)
{
    const CODE *c;

    size_t len = 0;

    char *buf, *s;

    for (c = codetab; c->c_name; c++) {
        len += strlen(c->c_name);
        len++;
    }

    buf = s = malloc(len + 1);
    if (!buf) {
        return NULL;
    }

    for (c = codetab; c->c_name; c++) {
        if (c != codetab) {
            strncpy(s, ",", 1);
            s++;
        }
        size_t l = strlen(c->c_name);
        strncpy(s, c->c_name, l);
        s += l;
    }

    *s = 0;

    return buf;
}

int main (int argc, char **argv)
{
    const char *name = argv[0];
    const char *basename = NULL;
    const char *dirname;
    int c, status = 0, zero = 0, ignore = 0, print = 0, slog = 0, bfd, dfd;

    int facility = 0, level = 0;

    DIR *dh;
    struct dirent *de;

    size_t size = 16, count = 0, i, dirlen;

    const char **names = malloc(sizeof(const char *) * size);

    while ((c = getopt_long(argc, argv, "0b:ips:hv", long_options, NULL)) != -1) {

        switch (c)
        {
        case '0':
            zero = 1;

            break;
        case 'b':
            basename = optarg;

            break;
        case 'i':
            ignore = 1;

            break;
        case 'p':
            print = 1;

            break;
        case 's': {
            char *s;

            s = strchr(optarg, '.');
            if (s) {
                *s = '\0';

                facility = syslog_decode(optarg, facilitynames);
                if (facility < 0) {
                    char *facilities = syslog_details(name, facilitynames);
                    fprintf(stderr, "%s: Unknown facility '%s': %s\n",
                            name, optarg, facilities);
                    free(facilities);
                    return EXIT_FAILURE;
                }

                optarg = ++s;
            }
            else {
                facility = LOG_USER;
            }

            level = syslog_decode(optarg, prioritynames);
            if (level < 0) {
                char *priorities = syslog_details(name, prioritynames);
                fprintf(stderr, "%s: Unknown priority %s: %s\n",
                        name, optarg, priorities);
                free(priorities);
                return EXIT_FAILURE;
            }

            slog = 1;

            break;
        }
        case 'h':
            return help(name, NULL, 0);

        case 'v':
            return version();

        default:
            return help(name, NULL, EXIT_FAILURE);

        }

    }

    if (optind == argc) {
        fprintf(stderr, "%s: No directory specified.\n", name);
        return EXIT_FAILURE;
    }

    if (basename) {

        bfd = open(basename, O_RDONLY);
        if (bfd == -1) {
            fprintf(stderr, "%s: Could not open '%s': %s\n", name,
                    basename, strerror(errno));
            return EXIT_FAILURE;
        }

        if (fchdir(bfd) == -1) {
            fprintf(stderr, "%s: Could not chdir to '%s': %s\n", name,
                    basename, strerror(errno));
            return EXIT_FAILURE;
        }
    }

    dirname = argv[optind];
    dirlen = strlen(dirname);

    dfd = open(dirname, O_RDONLY);
    if (dfd == -1) {
        fprintf(stderr, "%s: Could not open '%s': %s\n", name,
                dirname, strerror(errno));
        return EXIT_FAILURE;
    }

    if (fchdir(dfd) == -1) {
        fprintf(stderr, "%s: Could not chdir to '%s': %s\n", name,
                dirname, strerror(errno));
        return EXIT_FAILURE;
    }

    dh = fdopendir(dfd);
    if (!dh) {
        fprintf(stderr, "%s: Could not open directory '%s': %s\n", name,
                dirname, strerror(errno));
        return EXIT_FAILURE;
    }

    while ((de = readdir(dh))){

        /* ignore dot files */
        if (de->d_name[0] == '.') {
            continue;
        }

        if (size <= count) {
            size *= 2;
            names = realloc(names, size * sizeof(const char *));
        }

        names[count] = strdup(de->d_name);

        count++;
    }

    closedir(dh);

    qsort(names, count, sizeof(const char *), sort_strcmp);

    for (i = 0; i < count; i++) {

        char *buf = malloc(dirlen + strlen(names[i]) + 2);

        sprintf(buf, "%s/%s", dirname, names[i]);

        argv[optind] = buf;

        if (print) {

            if (ignore) {

                struct stat buf;

                if (fstatat(dfd, names[i], &buf, 0)) {
                    continue;
                }

                if (!S_ISREG(buf.st_mode)) {
                    continue;
                }

                if (faccessat(dfd, names[i], X_OK, AT_EACCESS)) {
                    continue;
                }

            }

            if (zero) {
                fprintf(stdout, "%s%c", buf, 0);
            }
            else {
                fprintf(stdout, "%s\n", buf);
            }

        }

        else {

            int errpair[2];

            pid_t w, f;

            if (pipe(errpair)) {
                fprintf(stderr, "%s: Could not create pipe: %s", name,
                        strerror(errno));

                return EXIT_FAILURE;
            }

            /* Clear any inherited settings */
            signal(SIGCHLD, SIG_DFL);

            f = fork();

            /* error */
            if (f < 0) {
                fprintf(stderr, "%s: Could not fork: %s", name,
                        strerror(errno));

                return EXIT_FAILURE;
            }

            /* child */
            else if (f == 0) {

                dup2(errpair[WRITE_FD], STDERR_FILENO);
                close(errpair[READ_FD]);
                close(errpair[WRITE_FD]);

                execv(names[i], argv + optind);

                if (ignore && errno == EACCES) {
                    return EXIT_SUCCESS;
                }

                fprintf(stderr, "%s: Could not execute '%s': %s\n", name,
                        argv[optind], strerror(errno));

                return EXIT_FAILURE;
            }

            /* parent */
            else {

                /* read our child's stderr, redirect to syslog or prefix with script name */
                close(errpair[WRITE_FD]);

                if (slog) {
                    openlog(argv[optind], LOG_PID, facility);
                }

                while (1) {

                    char errbuf[1024];

                    int i, s = 0;

                    int n = read(errpair[READ_FD], errbuf, sizeof(errbuf));
                    if (n <= 0) {
                        break;
                    }

                    /* syslog/prefix all LF terminated strings */
                    for (i = 0; i < n; i++) {
                        if (errbuf[i] == '\n') {
                            if (slog) {
                                syslog(level, "%.*s\n", i - s, errbuf + s);
                            }
                            else {
                                fprintf(stderr, "%s: %.*s\n", argv[optind], i - s, errbuf + s);
                            }
                            s = i + 1;
                        }
                    }

                    /* syslog/prefix any overflow */
                    if (s < n) {
                        if (slog) {
                            syslog(level, "%.*s\n", i - s, errbuf + s);
                        }
                        else {
                            fprintf(stderr, "%s: %.*s\n", argv[optind], i - s, errbuf + s);
                        }
                    }

                }

                if (slog) {
                    closelog();
                }

                close(errpair[READ_FD]);

                /* wait for the child process to be done */
                do {
                    w = waitpid(f, &status, 0);
                    if (w == -1 && errno != EINTR) {
                        break;
                    }
                } while (w != f);

                /* waitpid failed, we give up */
                if (w == -1) {

                    fprintf(stderr, "%s: waitpid for '%s' failed: %s\n", name,
                            argv[optind], strerror(errno));

                    return EXIT_FAILURE;
                }

                /* process successful exit */
                else if (WIFEXITED(status) && (WEXITSTATUS(status) == EXIT_SUCCESS)) {

                    /* drop through */
                }

                /* process non success exit */
                else if (WIFEXITED(status)) {

                    fprintf(stderr, "%s: %s returned %d\n", name,
                            argv[optind], status);

                    return WEXITSTATUS(status);
                }

                /* process received a signal */
                else if (WIFSIGNALED(status)) {

                    fprintf(stderr, "%s: %s signaled %d\n", name,
                            argv[optind], status);

                    return WTERMSIG(status) + 128;
                }

                /* otherwise weirdness, just leave */
                else {

                    fprintf(stderr, "%s: %s failed with %d\n", name,
                            argv[optind], status);

                    return EX_OSERR;
                }

            }

        }

        free(buf);

        argv[optind] = NULL;
    }

    return EXIT_SUCCESS;
}
