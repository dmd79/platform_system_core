/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <private/fs_config.h>

// This file is used to define the properties of the filesystem
// images generated by build tools (mkbootfs and mkyaffs2image) and
// by the device side of adb.

#define LOG_TAG "fs_config"

#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <string>

#include <android-base/strings.h>
#include <log/log.h>
#include <private/android_filesystem_config.h>
#include <utils/Compat.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

using android::base::EndsWith;
using android::base::StartsWith;

// My kingdom for <endian.h>
static inline uint16_t get2LE(const uint8_t* src) {
    return src[0] | (src[1] << 8);
}

static inline uint64_t get8LE(const uint8_t* src) {
    uint32_t low, high;

    low = src[0] | (src[1] << 8) | (src[2] << 16) | (src[3] << 24);
    high = src[4] | (src[5] << 8) | (src[6] << 16) | (src[7] << 24);
    return ((uint64_t)high << 32) | (uint64_t)low;
}

#define ALIGN(x, alignment) (((x) + ((alignment)-1)) & ~((alignment)-1))

// Rules for directories.
// These rules are applied based on "first match", so they
// should start with the most specific path and work their
// way up to the root.

static const struct fs_path_config android_dirs[] = {
        // clang-format off
    { 00770, AID_SYSTEM,       AID_CACHE,        0, "cache" },
    { 00555, AID_ROOT,         AID_ROOT,         0, "config" },
    { 00771, AID_SYSTEM,       AID_SYSTEM,       0, "data/app" },
    { 00771, AID_SYSTEM,       AID_SYSTEM,       0, "data/app-private" },
    { 00771, AID_SYSTEM,       AID_SYSTEM,       0, "data/app-ephemeral" },
    { 00771, AID_ROOT,         AID_ROOT,         0, "data/dalvik-cache" },
    { 00771, AID_SYSTEM,       AID_SYSTEM,       0, "data/data" },
    { 00771, AID_SHELL,        AID_SHELL,        0, "data/local/tmp" },
    { 00771, AID_SHELL,        AID_SHELL,        0, "data/local" },
    { 00770, AID_DHCP,         AID_DHCP,         0, "data/misc/dhcp" },
    { 00771, AID_SHARED_RELRO, AID_SHARED_RELRO, 0, "data/misc/shared_relro" },
    { 01771, AID_SYSTEM,       AID_MISC,         0, "data/misc" },
    { 00775, AID_MEDIA_RW,     AID_MEDIA_RW,     0, "data/media/Music" },
    { 00775, AID_MEDIA_RW,     AID_MEDIA_RW,     0, "data/media" },
    { 00750, AID_ROOT,         AID_SHELL,        0, "data/nativetest" },
    { 00750, AID_ROOT,         AID_SHELL,        0, "data/nativetest64" },
    { 00775, AID_ROOT,         AID_ROOT,         0, "data/preloads" },
    { 00771, AID_SYSTEM,       AID_SYSTEM,       0, "data" },
    { 00755, AID_ROOT,         AID_SYSTEM,       0, "mnt" },
    { 00751, AID_ROOT,         AID_SHELL,        0, "product/bin" },
    { 00750, AID_ROOT,         AID_SHELL,        0, "sbin" },
    { 00777, AID_ROOT,         AID_ROOT,         0, "sdcard" },
    { 00751, AID_ROOT,         AID_SDCARD_R,     0, "storage" },
    { 00751, AID_ROOT,         AID_SHELL,        0, "system/bin" },
    { 00755, AID_ROOT,         AID_SHELL,        0, "system/etc" },
    { 00755, AID_ROOT,         AID_ROOT,         0, "system/etc/ppp" },
    { 00755, AID_ROOT,         AID_SHELL,        0, "system/vendor" },
    { 00751, AID_ROOT,         AID_SHELL,        0, "system/xbin" },
    { 00755, AID_ROOT,         AID_SHELL,        0, "system/apex/*/bin" },
    { 00751, AID_ROOT,         AID_SHELL,        0, "vendor/bin" },
    { 00755, AID_ROOT,         AID_SHELL,        0, "vendor" },
    { 00755, AID_ROOT,         AID_ROOT,         0, 0 },
        // clang-format on
};
#ifndef __ANDROID_VNDK__
auto __for_testing_only__android_dirs = android_dirs;
#endif

// Rules for files.
// These rules are applied based on "first match", so they
// should start with the most specific path and work their
// way up to the root. Prefixes ending in * denotes wildcard
// and will allow partial matches.
static const char sys_conf_dir[] = "/system/etc/fs_config_dirs";
static const char sys_conf_file[] = "/system/etc/fs_config_files";
// No restrictions are placed on the vendor and oem file-system config files,
// although the developer is advised to restrict the scope to the /vendor or
// oem/ file-system since the intent is to provide support for customized
// portions of a separate vendor.img or oem.img.  Has to remain open so that
// customization can also land on /system/vendor, /system/oem, /system/odm,
// /system/product or /system/product_services.
//
// We expect build-time checking or filtering when constructing the associated
// fs_config_* files (see build/tools/fs_config/fs_config_generate.c)
static const char ven_conf_dir[] = "/vendor/etc/fs_config_dirs";
static const char ven_conf_file[] = "/vendor/etc/fs_config_files";
static const char oem_conf_dir[] = "/oem/etc/fs_config_dirs";
static const char oem_conf_file[] = "/oem/etc/fs_config_files";
static const char odm_conf_dir[] = "/odm/etc/fs_config_dirs";
static const char odm_conf_file[] = "/odm/etc/fs_config_files";
static const char product_conf_dir[] = "/product/etc/fs_config_dirs";
static const char product_conf_file[] = "/product/etc/fs_config_files";
static const char product_services_conf_dir[] = "/product_services/etc/fs_config_dirs";
static const char product_services_conf_file[] = "/product_services/etc/fs_config_files";
static const char* conf[][2] = {
        {sys_conf_file, sys_conf_dir},
        {ven_conf_file, ven_conf_dir},
        {oem_conf_file, oem_conf_dir},
        {odm_conf_file, odm_conf_dir},
        {product_conf_file, product_conf_dir},
        {product_services_conf_file, product_services_conf_dir},
};

// Do not use android_files to grant Linux capabilities.  Use ambient capabilities in their
// associated init.rc file instead.  See https://source.android.com/devices/tech/config/ambient.

// Do not place any new vendor/, data/vendor/, etc entries in android_files.
// Vendor entries should be done via a vendor or device specific config.fs.
// See https://source.android.com/devices/tech/config/filesystem#using-file-system-capabilities
static const struct fs_path_config android_files[] = {
        // clang-format off
    { 00644, AID_SYSTEM,    AID_SYSTEM,    0, "data/app/*" },
    { 00644, AID_SYSTEM,    AID_SYSTEM,    0, "data/app-ephemeral/*" },
    { 00644, AID_SYSTEM,    AID_SYSTEM,    0, "data/app-private/*" },
    { 00644, AID_APP,       AID_APP,       0, "data/data/*" },
    { 00644, AID_MEDIA_RW,  AID_MEDIA_RW,  0, "data/media/*" },
    { 00640, AID_ROOT,      AID_SHELL,     0, "data/nativetest/tests.txt" },
    { 00640, AID_ROOT,      AID_SHELL,     0, "data/nativetest64/tests.txt" },
    { 00750, AID_ROOT,      AID_SHELL,     0, "data/nativetest/*" },
    { 00750, AID_ROOT,      AID_SHELL,     0, "data/nativetest64/*" },
    { 00600, AID_ROOT,      AID_ROOT,      0, "default.prop" }, // legacy
    { 00600, AID_ROOT,      AID_ROOT,      0, "system/etc/prop.default" },
    { 00600, AID_ROOT,      AID_ROOT,      0, "odm/build.prop" }, // legacy; only for P release
    { 00600, AID_ROOT,      AID_ROOT,      0, "odm/default.prop" }, // legacy; only for P release
    { 00600, AID_ROOT,      AID_ROOT,      0, "odm/etc/build.prop" },
    { 00444, AID_ROOT,      AID_ROOT,      0, odm_conf_dir + 1 },
    { 00444, AID_ROOT,      AID_ROOT,      0, odm_conf_file + 1 },
    { 00444, AID_ROOT,      AID_ROOT,      0, oem_conf_dir + 1 },
    { 00444, AID_ROOT,      AID_ROOT,      0, oem_conf_file + 1 },
    { 00600, AID_ROOT,      AID_ROOT,      0, "product/build.prop" },
    { 00444, AID_ROOT,      AID_ROOT,      0, product_conf_dir + 1 },
    { 00444, AID_ROOT,      AID_ROOT,      0, product_conf_file + 1 },
    { 00600, AID_ROOT,      AID_ROOT,      0, "product_services/build.prop" },
    { 00444, AID_ROOT,      AID_ROOT,      0, product_services_conf_dir + 1 },
    { 00444, AID_ROOT,      AID_ROOT,      0, product_services_conf_file + 1 },
    { 00750, AID_ROOT,      AID_SHELL,     0, "sbin/fs_mgr" },
    { 00755, AID_ROOT,      AID_SHELL,     0, "system/bin/crash_dump32" },
    { 00755, AID_ROOT,      AID_SHELL,     0, "system/bin/crash_dump64" },
    { 00755, AID_ROOT,      AID_SHELL,     0, "system/bin/debuggerd" },
    { 00750, AID_ROOT,      AID_ROOT,      0, "system/bin/install-recovery.sh" },
    { 00550, AID_LOGD,      AID_LOGD,      0, "system/bin/logd" },
    { 00700, AID_ROOT,      AID_ROOT,      0, "system/bin/secilc" },
    { 00750, AID_ROOT,      AID_ROOT,      0, "system/bin/uncrypt" },
    { 00600, AID_ROOT,      AID_ROOT,      0, "system/build.prop" },
    { 00444, AID_ROOT,      AID_ROOT,      0, sys_conf_dir + 1 },
    { 00444, AID_ROOT,      AID_ROOT,      0, sys_conf_file + 1 },
    { 00440, AID_ROOT,      AID_SHELL,     0, "system/etc/init.goldfish.rc" },
    { 00550, AID_ROOT,      AID_SHELL,     0, "system/etc/init.goldfish.sh" },
    { 00550, AID_ROOT,      AID_SHELL,     0, "system/etc/init.ril" },
    { 00555, AID_ROOT,      AID_ROOT,      0, "system/etc/ppp/*" },
    { 00555, AID_ROOT,      AID_ROOT,      0, "system/etc/rc.*" },
    { 00440, AID_ROOT,      AID_ROOT,      0, "system/etc/recovery.img" },
    { 00600, AID_ROOT,      AID_ROOT,      0, "vendor/build.prop" },
    { 00600, AID_ROOT,      AID_ROOT,      0, "vendor/default.prop" },
    { 00444, AID_ROOT,      AID_ROOT,      0, ven_conf_dir + 1 },
    { 00444, AID_ROOT,      AID_ROOT,      0, ven_conf_file + 1 },

    // the following two files are INTENTIONALLY set-uid, but they
    // are NOT included on user builds.
    { 06755, AID_ROOT,      AID_ROOT,      0, "system/xbin/procmem" },
    { 04750, AID_ROOT,      AID_SHELL,     0, "system/xbin/su" },

    // the following files have enhanced capabilities and ARE included
    // in user builds.
    { 00700, AID_SYSTEM,    AID_SHELL,     CAP_MASK_LONG(CAP_BLOCK_SUSPEND),
                                              "system/bin/inputflinger" },
    { 00750, AID_ROOT,      AID_SHELL,     CAP_MASK_LONG(CAP_SETUID) |
                                           CAP_MASK_LONG(CAP_SETGID),
                                              "system/bin/run-as" },
    { 00750, AID_ROOT,      AID_SHELL,     CAP_MASK_LONG(CAP_SETUID) |
                                           CAP_MASK_LONG(CAP_SETGID),
                                              "system/bin/simpleperf_app_runner" },

    // Support FIFO scheduling mode in SurfaceFlinger.
    { 00755, AID_SYSTEM,    AID_GRAPHICS,  CAP_MASK_LONG(CAP_SYS_NICE),
                                              "system/bin/surfaceflinger" },
    // generic defaults
    { 00755, AID_ROOT,      AID_ROOT,      0, "bin/*" },
    { 00640, AID_ROOT,      AID_SHELL,     0, "fstab.*" },
    { 00750, AID_ROOT,      AID_SHELL,     0, "init*" },
    { 00755, AID_ROOT,      AID_SHELL,     0, "product/bin/*" },
    { 00750, AID_ROOT,      AID_SHELL,     0, "sbin/*" },
    { 00755, AID_ROOT,      AID_SHELL,     0, "system/bin/*" },
    { 00755, AID_ROOT,      AID_SHELL,     0, "system/etc/init.d/*" },
    { 00755, AID_ROOT,      AID_SHELL,     0, "system/xbin/*" },
    { 00755, AID_ROOT,      AID_SHELL,     0, "system/apex/*/bin/*" },
    { 00755, AID_ROOT,      AID_SHELL,     0, "vendor/bin/*" },
    { 00755, AID_ROOT,      AID_SHELL,     0, "vendor/xbin/*" },
    { 00644, AID_ROOT,      AID_ROOT,      0, 0 },
        // clang-format on
};
#ifndef __ANDROID_VNDK__
auto __for_testing_only__android_files = android_files;
#endif

static size_t strip(const char* path, size_t len, const char suffix[]) {
    if (len < strlen(suffix)) return len;
    if (strncmp(path + len - strlen(suffix), suffix, strlen(suffix))) return len;
    return len - strlen(suffix);
}

static int fs_config_open(int dir, int which, const char* target_out_path) {
    int fd = -1;

    if (target_out_path && *target_out_path) {
        // target_out_path is the path to the directory holding content of
        // system partition but as we cannot guarantee it ends with '/system'
        // or with or without a trailing slash, need to strip them carefully.
        char* name = NULL;
        size_t len = strlen(target_out_path);
        len = strip(target_out_path, len, "/");
        len = strip(target_out_path, len, "/system");
        if (asprintf(&name, "%.*s%s", (int)len, target_out_path, conf[which][dir]) != -1) {
            fd = TEMP_FAILURE_RETRY(open(name, O_RDONLY | O_BINARY));
            free(name);
        }
    }
    if (fd < 0) {
        fd = TEMP_FAILURE_RETRY(open(conf[which][dir], O_RDONLY | O_BINARY));
    }
    return fd;
}

// if path is "odm/<stuff>", "oem/<stuff>", "product/<stuff>",
// "product_services/<stuff>" or "vendor/<stuff>"
static bool is_partition(const std::string& path) {
    static const char* partitions[] = {"odm/", "oem/", "product/", "product_services/", "vendor/"};
    for (size_t i = 0; i < (sizeof(partitions) / sizeof(partitions[0])); ++i) {
        if (StartsWith(path, partitions[i])) return true;
    }
    return false;
}

// alias prefixes of "<partition>/<stuff>" to "system/<partition>/<stuff>" or
// "system/<partition>/<stuff>" to "<partition>/<stuff>"
static bool fs_config_cmp(bool dir, const char* prefix, size_t len, const char* path, size_t plen) {
    std::string pattern(prefix, len);
    std::string input(path, plen);

    // Massage pattern and input so that they can be used by fnmatch where
    // directories have to end with /.
    if (dir) {
        if (!EndsWith(input, "/")) {
            input.append("/");
        }

        if (!EndsWith(pattern, "/*")) {
            if (EndsWith(pattern, "/")) {
                pattern.append("*");
            } else {
                pattern.append("/*");
            }
        }
    }

    // no FNM_PATHNAME is set in order to match a/b/c/d with a/*
    // FNM_ESCAPE is set in order to prevent using \\? and \\* and maintenance issues.
    const int fnm_flags = FNM_NOESCAPE;
    if (fnmatch(pattern.c_str(), input.c_str(), fnm_flags) == 0) return true;

    static constexpr const char* kSystem = "system/";
    if (StartsWith(input, kSystem)) {
        input.erase(0, strlen(kSystem));
    } else if (input.size() <= strlen(kSystem)) {
        return false;
    } else if (StartsWith(pattern, kSystem)) {
        pattern.erase(0, strlen(kSystem));
    } else {
        return false;
    }

    if (!is_partition(pattern)) return false;
    if (!is_partition(input)) return false;
    return fnmatch(pattern.c_str(), input.c_str(), fnm_flags) == 0;
}
#ifndef __ANDROID_VNDK__
auto __for_testing_only__fs_config_cmp = fs_config_cmp;
#endif

void fs_config(const char* path, int dir, const char* target_out_path, unsigned* uid, unsigned* gid,
               unsigned* mode, uint64_t* capabilities) {
    const struct fs_path_config* pc;
    size_t which, plen;

    if (path[0] == '/') {
        path++;
    }

    plen = strlen(path);

    for (which = 0; which < (sizeof(conf) / sizeof(conf[0])); ++which) {
        struct fs_path_config_from_file header;

        int fd = fs_config_open(dir, which, target_out_path);
        if (fd < 0) continue;

        while (TEMP_FAILURE_RETRY(read(fd, &header, sizeof(header))) == sizeof(header)) {
            char* prefix;
            uint16_t host_len = get2LE((const uint8_t*)&header.len);
            ssize_t len, remainder = host_len - sizeof(header);
            if (remainder <= 0) {
                ALOGE("%s len is corrupted", conf[which][dir]);
                break;
            }
            prefix = static_cast<char*>(calloc(1, remainder));
            if (!prefix) {
                ALOGE("%s out of memory", conf[which][dir]);
                break;
            }
            if (TEMP_FAILURE_RETRY(read(fd, prefix, remainder)) != remainder) {
                free(prefix);
                ALOGE("%s prefix is truncated", conf[which][dir]);
                break;
            }
            len = strnlen(prefix, remainder);
            if (len >= remainder) {  // missing a terminating null
                free(prefix);
                ALOGE("%s is corrupted", conf[which][dir]);
                break;
            }
            if (fs_config_cmp(dir, prefix, len, path, plen)) {
                free(prefix);
                close(fd);
                *uid = get2LE((const uint8_t*)&(header.uid));
                *gid = get2LE((const uint8_t*)&(header.gid));
                *mode = (*mode & (~07777)) | get2LE((const uint8_t*)&(header.mode));
                *capabilities = get8LE((const uint8_t*)&(header.capabilities));
                return;
            }
            free(prefix);
        }
        close(fd);
    }

    for (pc = dir ? android_dirs : android_files; pc->prefix; pc++) {
        if (fs_config_cmp(dir, pc->prefix, strlen(pc->prefix), path, plen)) {
            break;
        }
    }
    *uid = pc->uid;
    *gid = pc->gid;
    *mode = (*mode & (~07777)) | pc->mode;
    *capabilities = pc->capabilities;
}

ssize_t fs_config_generate(char* buffer, size_t length, const struct fs_path_config* pc) {
    struct fs_path_config_from_file* p = (struct fs_path_config_from_file*)buffer;
    size_t len = ALIGN(sizeof(*p) + strlen(pc->prefix) + 1, sizeof(uint64_t));

    if ((length < len) || (len > UINT16_MAX)) {
        return -ENOSPC;
    }
    memset(p, 0, len);
    uint16_t host_len = len;
    p->len = get2LE((const uint8_t*)&host_len);
    p->mode = get2LE((const uint8_t*)&(pc->mode));
    p->uid = get2LE((const uint8_t*)&(pc->uid));
    p->gid = get2LE((const uint8_t*)&(pc->gid));
    p->capabilities = get8LE((const uint8_t*)&(pc->capabilities));
    strcpy(p->prefix, pc->prefix);
    return len;
}
