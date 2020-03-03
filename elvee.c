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

#define ascii_tolower(ch) \
    ((ch) >= 'A' && (ch) <= 'Z' ? (ch) | 0x20 : (ch))

#define log(format, ...) \
    fprintf(stderr, "%s(%d):" format "\n", __FILE__, __LINE__, __VA_ARGS__)

int verbose = 0;

#define vlog(format, ...) \
    if (verbose) { log(format, __VA_ARGS__); }

#undef min
#define min(a, b) ((a) < (b) ? (a) : (b))

#define PROGRAM_NAME "elvee"
#define PROGRAM_NAME_UPPER "ELVEE"
#define PROGRAM_VERSION "1.0"

char program_name[] = PROGRAM_NAME;

void help();
void license();
void timestamp();
int ascii_strcmpi(char *s1, char *s2);
char *argv_quote(char *arg);

int main(int argc, char **argv)
{
    // Enable verbose logging to STDERR if an environment variable named
    // `ELVEE_VERBOSE` or `elvee_verbose` is defined and its value is
    // anything but 0.

    char *verbose_env;
    if (!(verbose_env = getenv(PROGRAM_NAME_UPPER "_VERBOSE"))) {
        if (!(verbose_env = getenv(PROGRAM_NAME "_verbose"))) {
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

    int has_orig_name = 0 == ascii_strcmpi(fname, program_name);

    char *template = argv[1];
    if (has_orig_name) {
        if (!template) {
            print_app_error("Missing target template argument.");
            fprintf(stderr, "Run again with \"help\" (without quotes) as first argument for help.\n");
            return 1;
        }
        if (0 == strcmp(template, "help")) {
            help();
            return 0;
        }
        if (0 == strcmp(template, "license")) {
            license();
            return 0;
        }
        if (0 == strcmp(template, "timestamp")) {
            timestamp();
            return 0;
        }
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
                && *suffix && *lsuffix
                && strncmp(suffix, lsuffix, min(1 + strlen(suffix), 1 + strlen(lsuffix))) > 0);

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

    if (has_orig_name) {
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

    // Quote arguments if necessary and track those quoted.

    char **qargv = NULL;
    for (int i = 0; i < argc; i++) {
        char *qarg = argv_quote(argv[i]);
        if (qarg != argv[i]) {
            if (qargv == NULL) {
                qargv = calloc(argc, sizeof(char*));
            }
            qargv[i] = argv[i];
        }
        argv[i] = qarg;
    }

    intptr_t result = _spawnv(_P_WAIT, spawn_path, argv);

    // Free any quoted arguments, including their tracking.
    // Restore argv to its original state.

    if (qargv) {
        for (int i = 0; i < argc; i++) {
            if (qargv[i]) {
                free(argv[i]);
                argv[i] = qargv[i];
            }
        }
        free(qargv);
    }

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

int ascii_strcmpi(char *s1, char *s2)
{
    int cmp;
    for (cmp = 0; 0 == cmp && (*s1 || *s2); s1++, s2++) {
        cmp = ascii_tolower(*s1) - ascii_tolower(*s2);
    }
    return cmp;
}

void help()
{
    char *text[] = {
        PROGRAM_NAME" "PROGRAM_VERSION" - runs latest executable version",
        "Copyright (c) 2018 Atif Aziz. All rights reserved.",
        "Licensed under The MIT License: https://opensource.org/licenses/MIT",
        "",
        "This program searches for the latest version of an executable and",
        "runs it, passing along all arguments passed to it. To locate the",
        "latest version it searches sub-directories that conform to the",
        "following naming pattern:",
        "",
        "  \"v\" MAJOR [ \".\" MINOR [ \".\" PATCH ] ] [ \"-\" SUFFIX ]",
        "",
        "MAJOR, MINOR and PATCH must be non-negative decimal integers. The",
        "SUFFIX is any string of characters starting with a hypen. If",
        "present, it denotes a pre-release and which is compared ordinally to",
        "any other pre-release suffix in the same version. Only MAJOR is",
        "required. Below are examples of sub-directory names following this",
        "pattern and how they would compare to each other, with the right-most",
        "being considered the latest:",
        "",
        "  v1 < v2 < 3.0 < v3.0.1 < v3.1 < 3.1.1-beta < 3.1.1-rc < 3.1.1",
        "",
        "Note that folders named v2 and v2.0 and v2.0.0 will be considered to",
        "be the same and any one may be chosen arbitrarily if version 2",
        "represents the latest version so take care to avoid such ambiguities.",
        "",
        "Once the latest version directory has been identified, this program",
        "will run an identically named executable from that directory. It is",
        "therefore assumed that this program's file name will be renamed to",
        "bear the same name as the actual target program to run, with this",
        "program acting as a mere thunk/trampoline/selector.",
        "",
        "On a Windows system, the target executable can be a binary with an",
        "extension of \".com\" or \".exe\", or a batch script with an extension",
        " of \".bat\" or \".cmd\".",
        "",
        "If this program's filename is left exactly \""PROGRAM_NAME"\" then there is a",
        "second mode of operation where the first required argument specifies",
        "a template following the syntax (replace / with \\ on Windows):",
        "",
        "  SEARCH_PATH \"/?/\" SUB_PATH",
        "",
        "SEARCH_PATH is a relative or absolute directory path where the latest",
        "version sub-directory will be sought based on the earlier explanation",
        "and SUB_PATH is the path to the executale to run within the latest",
        "version sub-directory. The question mark effectively gets replaced",
        "with the latest version number at run-time. Suppose the following",
        "invocation:",
        "",
        "  "PROGRAM_NAME" /app/?/bin/foo bar baz",
        "",
        "Suppose further that the latest version directory under \"/app\" is",
        "called \"v4.2\". This program will then operate as if you intended",
        "to type the following on the command line:",
        "",
        "  /app/v4.2/bin/foo bar baz",
        "",
        "For dianostics, this program will display verbose output to STDERR",
        "if the environment variable "PROGRAM_NAME_UPPER"_VERBOSE is defined to be any value",
        "but zero (0).",
        "",
        "This program is distributed under the terms and conditions of",
        "The MIT License. Run the program with \"license\" (without quotes) as",
        "the first argument to display the full text of the license.",
        "",
        "This program was compiled on " __DATE__ " at " __TIME__ "."
    };

    for (int i = 0; i < DIM(text); i++) {
        puts(text[i]);
    }
}

void license()
{
    char *text[] = {
        "Copyright (C) 2018 Atif Aziz",
        "",
        "Permission is hereby granted, free of charge, to any person obtaining a",
        "copy of this software and associated documentation files (the \"Software\"),",
        "to deal in the Software without restriction, including without limitation",
        "the rights to use, copy, modify, merge, publish, distribute, sublicense,",
        "and/or sell copies of the Software, and to permit persons to whom the",
        "Software is furnished to do so, subject to the following conditions:",
        "",
        "The above copyright notice and this permission notice shall be included",
        "in all copies or substantial portions of the Software.",
        "",
        "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR",
        "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,",
        "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL",
        "THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER",
        "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING",
        "FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER",
        "DEALINGS IN THE SOFTWARE.",
    };

    for (int i = 0; i < DIM(text); i++) {
        puts(text[i]);
    }
}

void timestamp()
{
    char month_name[4];
    unsigned short year, month, day, hour, min, sec;
    char *month_names = "Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec";
    sscanf(__DATE__ " " __TIME__, "%s %hu %hu %hu:%hu:%hu", month_name, &day, &year, &hour, &min, &sec);
    month = (strstr(month_names, month_name) - month_names) / 4 + 1;
    printf("%04hu-%02hu-%02hu %02hu:%02hu:%02hu\n", year, month, day, hour, min, sec);
}

// Adapted from https://docs.microsoft.com/en-us/archive/blogs/twistylittlepassagesallalike/everyone-quotes-command-line-arguments-the-wrong-way
// See issue #1
// If quoting is needed then a newly allocated string is returned with "arg"
// content copied, quoted and escaped. If quoting is unnecessary then "arg" is
// returned as-is.

char *argv_quote(char *arg)
{
    int needs_quote = 0;
    int escape_count = 0;
    int arglen = 0;
    for (char *p = arg; *p != '\0'; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\v' || *p == '"') {
            needs_quote = 1;
        }
        if (*p == '\\' || *p == '"') {
            escape_count += 1;
        }
        arglen += 1;
    }

    if (arglen == 0 || !needs_quote) {
        return arg;
    }

    // One byte for each potentially added backslash.
    // Can overallocate, but that's very unlikely to matter.
    char *out = malloc(arglen + escape_count);
    char *outp = out;

    *outp++ = '"';

    for (char *p = arg; *p != '\0'; p++) {
        int slashc = 0;

        while (*p == '\\') {
            ++p;
            ++slashc;
        }

        if (*p == '\0') {
            // Escape all backslashes, but let the terminating
            // double quotation mark we add below be interpreted
            // as a metacharacter.
            for (int i = 0; i < slashc * 2; i++) {
                // There's a Linus quote about indenting this much
                *outp++ = '\\';
            }
            break;
        }
        else if (*p == '"') {
            // Escape all backslashes and the following
            // double quotation mark.
            for (int i = 0; i < slashc * 2 + 1; i++) {
                *outp++ = '\\';
            }
        }
        else {
            // Backslashes aren't special here.
            for (int i = 0; i < slashc; i++) {
                *outp++ = '\\';
            }
        }
        *outp++ = *p;
    }

    *outp++ = '"';
    *outp = '\0';
    return out;
}
