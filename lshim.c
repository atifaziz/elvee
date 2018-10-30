/* Copyright (C) 2018 Atif Aziz
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#if defined(_WIN64) || defined(_WIN32) && !defined(WINDOWS)
#define WINDOWS
#endif

#include <string.h>
#ifdef WINDOWS
#include "include/win/dirent.h"
#else
#include <dirent.h>
#endif
#include <errno.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#ifndef WINDOWS
#include <unistd.h>
#endif
#include <sys/types.h>
#ifndef WINDOWS
#include <sys/wait.h>
#endif

#ifdef WINDOWS

#include <windows.h>
#include <process.h>

#define realpath(rel, abs) _fullpath(abs, rel, sizeof(abs) / sizeof((abs)[0]))

#define PATH_SEPARATOR_CHAR '\\'
#define PATH_SEPARATOR      "\\"

#else // *nix

#define PATH_SEPARATOR_CHAR '/'
#define PATH_SEPARATOR      "/"

#endif

#include "include/struct.h"

#define DIM(x) (sizeof(x) / sizeof((x)[0]))

#define print_op_error(op) \
    fprintf(stderr, "Operation '%s' failed due to:\n%s\nat: %s:%d\n", (op), strerror(errno), __FILE__, __LINE__)

#define print_app_error(msg) \
    fprintf(stderr, "%s\nat: %s:%d\n", (msg), __FILE__, __LINE__)

#define printf_app_error(msg, ...) \
    fprintf(stderr, msg "\nat: %s:%d\n", __VA_ARGS__, __FILE__, __LINE__)

#define tolower(ch) ((ch) | 0x20)

int main(int argc, char **argv)
{
    char path[PATH_MAX];
    char fname[NAME_MAX];
    if (!realpath(argv[0], path)) {
        print_op_error("realpath");
        return 1;
    }

    char *pathsep = strrchr(path, PATH_SEPARATOR_CHAR);
    if (strlen(pathsep + 1) >= DIM(fname)) {
        printf_app_error("File name is too long: %s", pathsep + 1);
        return 1;
    }
    strcpy(fname, pathsep + 1);
    *pathsep = 0;

    char *ext = strrchr(fname, '.');
    if (ext) {
        *ext = 0;
    }

    char *p = fname;
    int is_lshim =  'l' == tolower(*p++)
                 && 's' == tolower(*p++)
                 && 'h' == tolower(*p++)
                 && 'i' == tolower(*p++)
                 && 'm' == tolower(*p++);

    char *template = argv[1];
    if (is_lshim) {
        char token[] = PATH_SEPARATOR "?" PATH_SEPARATOR;
        char *tt = strstr(template, token);
        if (!tt) {
            printf_app_error("Invalid template argument: %s", template);
            return 1;
        }
        strncpy(path, template, tt - template);
        path[tt - template] = 0;
        if (snprintf(fname, DIM(fname), "%s", tt + DIM(token) - 1) >= DIM(fname)) {
            print_app_error("Trailer path is too long!");
        }
    }

    DIR *d; d = opendir(path);
    if (!d) {
        print_op_error("opendir");
        return 1;
    }

    unsigned int lmajor = 0, lminor = 0, lpatch = 0;
    char lname[fldsiz(dirent, d_name) / sizeof(char)];
    struct dirent *dir;

    while ((errno = 0, dir = readdir(d)) != NULL) {
        if (dir->d_type != DT_DIR || dir->d_name[0] != 'v')
            continue;
        unsigned int major = 0, minor = 0, patch = 0;
        int tokens = sscanf(dir->d_name, "v%u.%u.%u", &major, &minor, &patch);
        if (!tokens)
            continue;
        if (major > lmajor) {
            lmajor = major;
            lminor = minor;
            lpatch = patch;
            strcpy(lname, dir->d_name);
        } else if (minor > lminor) {
            lminor = minor;
            lpatch = patch;
        } else if (patch > lpatch) {
            lpatch = patch;
        }
    }

    if (errno) {
        print_op_error("readdir");
        closedir(d);
        return 1;
    }

    closedir(d);

    char spawn_path[PATH_MAX];
    if (snprintf(spawn_path, DIM(spawn_path), "%s%s%s%s%s", path, PATH_SEPARATOR, lname, PATH_SEPARATOR, fname) >= DIM(spawn_path)) {
        print_app_error("Final path is too long!");
        return 1;
    }

    argv[0] = spawn_path;

    if (is_lshim) {
        char **patched_argv = alloca(argc * sizeof(patched_argv[0]));
        for (int si = 0, di = 0; si < argc; si++, di++) {
            if (1 == si) {
                di--;
            }
            else {
                patched_argv[di] = argv[si];
            }
        }
        patched_argv[argc - 1] = 0;
        argv = patched_argv;
    }

#ifdef WINDOWS

    intptr_t result = _spawnv(_P_WAIT, spawn_path, argv);
    if (result == -1) {
        printf_app_error("Error launching: %s\nReason: %s", spawn_path, strerror(errno));
        return 1;
    }

    return result;

#else // !WINDOWS

    pid_t pid = fork();
    if (pid < 0) {
        printf_app_error("Error launching: %s\nReason: %s", spawn_path, strerror(errno));
        return 1;
    } else if (pid) { // fork parent
        int status;
        return wait(&status) >= 0 && WIFEXITED(status)
             ? WEXITSTATUS(status)
             : 1;
    } else { // fork child
        execv(spawn_path, argv);
        printf_app_error("Failed to fork: %s\nReason: %s", spawn_path, strerror(errno));
        return 1;
    }

    return 0;

#endif // WINDOWS
}
