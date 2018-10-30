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

#define log(format, ...) \
    fprintf(stderr, "%s(%d):" format "\n", __FILE__, __LINE__, __VA_ARGS__)

int verbose = 0;

#define vlog(format, ...) \
    if (verbose) { log(format, __VA_ARGS__); }

#undef min
#define min(a, b) ((a) < (b) ? (a) : (b))

int main(int argc, char **argv)
{
    // Enable verbose logging to STDERR if an environment variable named
    // `LSHIM_VERBOSE` or `lshim_verbose` is defined and its value is
    // anything but 0.

    char *verbose_env;
    if (!(verbose_env = getenv("LSHIM_VERBOSE"))) {
        if (!(verbose_env = getenv("lshim_verbose"))) {
            verbose_env = "0";
        }
    }
    verbose = strcmp("0", verbose_env) ? 1 : 0;

    // Get the absolute path of this program.

    char path[PATH_MAX];
    char fname[NAME_MAX];
    if (!realpath(argv[0], path)) {
        print_op_error("realpath");
        return 1;
    }

    // Split program directory path and file name.

    char *pathsep = strrchr(path, PATH_SEPARATOR_CHAR);
    if (strlen(pathsep + 1) >= DIM(fname)) {
        printf_app_error("File name is too long: %s", pathsep + 1);
        return 1;
    }
    strcpy(fname, pathsep + 1);
    *pathsep = 0;

    // Blow away the file extension, if any.

    char *ext = strrchr(fname, '.');
    if (ext) {
        *ext = 0;
    }

    vlog("path: %s", path);
    vlog("fname: %s", fname);

    // Has this program been renamed? If so then it will only look for that
    // program's versions in sub-directories. Otherwise, the first argument is
    // a template string that must have the token `\?\` (Windows) or `/?/`
    // (*nix). The `?` in the token is then replace with the name of the
    // latest version directory. The versions are scanned in the path to the
    // left of the token. The path resulting from the replacement will be the
    // path of the spawned program.

    char *p = fname;
    int is_lshim =  'l' == tolower(*p++)
                 && 's' == tolower(*p++)
                 && 'h' == tolower(*p++)
                 && 'i' == tolower(*p++)
                 && 'm' == tolower(*p++);

    char *template = argv[1];
    if (is_lshim) {
        vlog("template: %s", template);
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

    // Scan the directory for sub-directories whose name conforms to the
    // following pattern:
    //
    //     "v" MAJOR [ "." MINOR [ "." PATCH ] ] [ "-" SUFFIX ]
    //
    // where MAJOR, MINOR and PATCH must be (when present) non-negative decimal
    // integers. The SUFFIX is any string of characters and compared verbatim.

    vlog("opendir: %s", path);
    DIR *d; d = opendir(path);
    if (!d) {
        print_op_error("opendir");
        return 1;
    }

    char lname[fldsiz(dirent, d_name) / sizeof(char)] = { 0 };
    char lsuffix[DIM(lname)] = { 0 };
    unsigned int lmajor = 0, lminor = 0, lpatch = 0;
    struct dirent *dir;

    while ((errno = 0, dir = readdir(d)) != NULL) {

        // Consider only directories that start with "v".

        int ignore = dir->d_type != DT_DIR || dir->d_name[0] != 'v';
        vlog("dir[%s]: (%x) %s", ignore ? "x" : " ", dir->d_type, dir->d_name);
        if (ignore)
            continue;

        // Parse out tokens from the directory name.

        int major = 0, minor = 0, patch = 0;
        char suffix[DIM(lsuffix)] = { 0 };
        int tokens;
        if ((tokens = sscanf(dir->d_name, "v%u.%u.%u%s", &major, &minor, &patch, suffix)) < 3) {
            if ((tokens = sscanf(dir->d_name, "v%u.%u%s", &major, &minor, suffix)) < 2) {
                tokens = sscanf(dir->d_name, "v%u%s", &major, suffix);
            }
        }

        if (!tokens) // no tokens then loop around
            continue;

        // Suffix must begin with a hyphen (-).

        int invalid_suffix = *suffix && *suffix != '-';
        vlog("tokens(%d): %u.%u.%u%s%s", tokens, major, minor, patch, suffix, invalid_suffix ? " (invalid suffix)" : "");
        if (invalid_suffix)
            continue;

        // Does this entry sort higher than the last we know? Then...

        int upgrade
            =   major > lmajor
            || (major == lmajor && minor > lminor)
            || (major == lmajor && minor == lminor && patch > lpatch)
            || (major == lmajor && minor == lminor && patch == lpatch
                && *lsuffix && *suffix
                && strncmp(suffix, lsuffix, min(strlen(suffix), strlen(lsuffix))) > 0);

        vlog("upgrade: %u.%u.%u%s > %u.%u.%u%s ? %s", major, minor, patch, suffix, lmajor, lminor, lpatch, lsuffix, upgrade ? "yes" : "no");

        if (upgrade) {
            lmajor = major; // ... upgrade!
            lminor = minor;
            lpatch = patch;
            strcpy(lsuffix, suffix);
            strcpy(lname, dir->d_name);
        }
    }

    if (errno) {
        print_op_error("readdir");
        closedir(d);
        return 1;
    }

    closedir(d);

    if (!*lname) {
        fprintf(stderr, "No version found to run!\n");
        return 1;
    }

    // Build up the path to the program to spawn.

    char spawn_path[PATH_MAX];
    if (snprintf(spawn_path, DIM(spawn_path), "%s%s%s%s%s", path, PATH_SEPARATOR, lname, PATH_SEPARATOR, fname) >= DIM(spawn_path)) {
        print_app_error("Final path is too long!");
        return 1;
    }

    // If the first argument was a template remove it before passing on the
    // rest of arguments to the program to spawn.

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
        patched_argv[--argc] = 0;
        argv = patched_argv;
    }

    if (verbose) {
        for (int i = 0; i < argc; i++) {
            vlog("argv[%d] = \"%s\"", i, argv[i] ? argv[i] : "(null)");
        }
    }

    // Shazam!

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
