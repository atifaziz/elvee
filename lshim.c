#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <struct.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main(int argc, char** argv) {

  char path[PATH_MAX];
  char fname[PATH_MAX];
  // win32 = _fullpath; https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/fullpath-wfullpath?view=vs-2017
  if (!realpath(argv[0], path)) {
    fprintf(stderr, "Error in directory listing.\n");
    return 1;
  }

  char* pathsep = strrchr(path, '/');
  if (!pathsep)
    pathsep = strrchr(path, '\\');
  strcpy(fname, pathsep + 1);
  *(pathsep + 1) = 0;

  DIR *d; d = opendir(path);
  if (!d) {
    fprintf(stderr, "Error listing directory.\n");
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

  closedir(d);
  if (errno) {
    fprintf(stderr, "Error listing directory.\n");
    return 1;
  }

  sprintf(path, "%s%s/%s", path, lname, fname);

  pid_t pid = fork();
  if (pid < 0) {
    fprintf(stderr, "Error launching: %s\n", path);
    return 1;
  } else if (pid) { // fork parent
    int status;
    return wait(&status) >= 0 && WIFEXITED(status)
         ? WEXITSTATUS(status) : 1;
  } else { // fork child
    execv(path, argv);
    fprintf(stderr, "Failed to fork: %s\n", path);
    return 1;
  }

  return 0;
}
