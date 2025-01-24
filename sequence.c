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

#include "config.h"

static struct option long_options[] =
{
    {"zero", no_argument, NULL, '0'},
    {"print", no_argument, NULL, 'p'},
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
            "  %s [-0] [-p] [-v] [-h] directory [options]\n"
            "\n"
            "DESCRIPTION\n"
            "\n"
            "  The sequence command runs all the executables in a specified directory,\n"
            "  running each one in sequence ordered alphabetically.\n"
            "\n"
            "  Each executable is named sensibly so it is clear which executable is\n"
            "  responsible for output in logfiles.\n"
            "\n"
            "OPTIONS\n"
            "  -0, --zero  Terminate names with a zero instead of newline.\n"
            "\n"
            "  -p, --print  Print the name of executables rather than execute.\n"
            "\n"
            "  -h, --help  Display this help message.\n"
            "\n"
            "  -v, --version  Display the version number.\n"
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
            "EXAMPLES\n"
            "  In this basic example, we execute all commands in /etc/rc3.d, passing\n"
            "  the parameter 'start' to each command."
            "\n"
            "\t~$ sequence /etc/rc3.d -- start\n"
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

int main (int argc, char **argv)
{
    const char *name = argv[0];
    const char *dirname;
    int c, status = 0, zero = 0, print = 0, dfd;

    DIR *dh;
    struct dirent *de;

    size_t size = 16, count = 0, i, dirlen;

    const char **names = malloc(sizeof(const char *) * size);

    while ((c = getopt_long(argc, argv, "0phv", long_options, NULL)) != -1) {

        switch (c)
        {
        case '0':
            zero = 1;

            break;
        case 'p':
            print = 1;

            break;
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

    dirname = argv[optind];
    dirlen = strlen(dirname);

    dfd = open(dirname, O_RDONLY);
    if (dfd == -1) {
        fprintf(stderr, "%s: Could not open '%s': %s\n", name,
                dirname, strerror(errno));
        return 1;
    }

    if (fchdir(dfd) == -1) {
        fprintf(stderr, "%s: Could not chdir to '%s': %s\n", name,
                dirname, strerror(errno));
        return 1;
    }

    dh = fdopendir(dfd);
    if (!dh) {
        fprintf(stderr, "%s: Could not open directory '%s': %s\n", name,
                dirname, strerror(errno));
        return 1;
    }

    while ((de = readdir(dh))){

        /* ignore dot files */
        if (de->d_name[0] == '.') {
            continue;
        }

        switch (de->d_type) {
        case DT_REG: {

            if (size <= count) {
                size *= 2;
                names = realloc(names, size * sizeof(const char *));
            }

            names[count] = strdup(de->d_name);

            count++;

            break;
        }
        case DT_LNK: {

            struct stat buf;

            if (fstatat(dfd, de->d_name, &buf, 0)) {
                fprintf(stderr, "%s: Could not stat '%s/%s': %s\n", name,
                        dirname, de->d_name, strerror(errno));
                return 1;
            }

            if (!S_ISREG(buf.st_mode)) {
                continue;
            }

            if (size <= count) {
                size *= 2;
                names = realloc(names, size * sizeof(const char *));
            }

            names[count] = strdup(de->d_name);

            count++;

            break;
        }
        default:
            continue;
        }
    }

    closedir(dh);

    qsort(names, count, sizeof(const char *), sort_strcmp);

    for (i = 0; i < count; i++) {

        char *buf = malloc(dirlen + strlen(names[i]) + 2);

        sprintf(buf, "%s/%s", dirname, names[i]);

        argv[optind] = buf;

        if (print) {

            if (zero) {
                fprintf(stdout, "%s%c", buf, 0);
            }
            else {
                fprintf(stdout, "%s\n", buf);
            }

        }

        else {

            pid_t w, f;

            f = fork();

            /* error */
            if (f < 0) {
                fprintf(stderr, "%s: Could not fork: %s", name,
                        strerror(errno));

                return EXIT_FAILURE;
            }

            /* child */
            else if (f == 0) {


                execv(names[i], argv + optind);

                fprintf(stderr, "%s: Could not execute '%s': %s\n", name,
                        argv[optind], strerror(errno));

                return EXIT_FAILURE;
            }

            /* parent */
            else {

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
                else if (WIFEXITED(status)) {

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
