# elvee

`elvee` is a small shim/thunk for running the latest version of any
executable, be that a shell script or a compiled/native binary. Each version
resides in its own directory and all invocations must then the shim instead
of the original.

It is written in C and designed to work on Windows, macOS and Linux.

It presents a simple solution to the problem of upgrading an executable,
especially one that may be a long-running process, while it is in use.
Existing versions that are in flight do not have to be stopped. Newer
invocations will start the latest deployed version.

It searches for the latest version of an executable and runs it, passing
along all arguments passed to it. To locate the latest version it searches
sub-directories that conform to the following naming pattern:

    "v" MAJOR [ "." MINOR [ "." PATCH ] ] [ "-" SUFFIX ]

`MAJOR`, `MINOR` and `PATCH` must be non-negative decimal integers. The
`SUFFIX` is any string of characters starting with a hypen. If present,
it denotes a pre-release and which is compared ordinally to any other
pre-release suffix in the same version. Only `MAJOR` is required. Below are
examples of sub-directory names following this pattern and how they would
compare to each other, with the right-most being considered the latest:

    v1 < v2 < 3.0 < v3.0.1 < v3.1 < 3.1.1-beta < 3.1.1-rc < 3.1.1

Note that folders named `v2` and `v2.0` and `v2.0.0` will be considered to
be the same and any one may be chosen arbitrarily if version 2 represents the
latest version so take care to avoid such ambiguities.

Once the latest version directory has been identified, the shim will run an
_identically_ named executable from that directory. It is therefore assumed
that this program's file name will be renamed to bear the same name as the
actual target program to run.

On a Windows system, the target executable can be a binary with an extension
of `.com` or `.exe`, or a batch script with an extension of `.bat` or `.cmd`.

If the shim's filename is left exactly `elvee` then there is a second mode
of operation where the first required argument specifies a template following
the syntax (replace `/` with `\` on Windows):

    SEARCH_PATH "/?/" SUB_PATH

`SEARCH_PATH` is a relative or absolute directory path where the latest
version sub-directory will be sought based on the earlier explanation and
`SUB_PATH` is the path to the executale to run within the latest version
sub-directory. The question mark effectively gets replaced with the latest
version number at run-time. Suppose the following invocation:

    elvee /app/?/bin/foo bar baz

Suppose further that the latest version directory under `/app` is called
`v4.2`. This program will then operate as if you intended to type the
following on the command line:

    /app/v4.2/bin/foo bar baz

For dianostics, this program will display verbose output to `STDERR` if the
environment variable `ELVEE_VERBOSE` is defined to be any value but
zero (`0`).

## Building

To build the application on Linux or macOS, run:

    clang -o elvee elvee.c

To build the application on Windows, run:

    cl /MD /O1 elvee.c
