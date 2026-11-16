// clap_bundle.h - the file to dlopen for a .clap path: on Linux the .clap is
// the shared library itself, on macOS a bundle folder holding
// Contents/MacOS/<Name>. Used by clap_latency_check and clap_remote_check.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char* clap_binary(const char* path, char* buf, size_t size)
{
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) return path;
    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;
    size_t len = strlen(base);
    if (len > 5 && !strcmp(base + len - 5, ".clap")) len -= 5;
    snprintf(buf, size, "%s/Contents/MacOS/%.*s", path, (int)len, base);
    return buf;
}
