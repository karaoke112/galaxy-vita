// NAND flash emulation: save data lives in files under the app's storage.
#include <stdint.h>
extern "C" void port_gpu_memory_written(void* addr, uint32_t size);
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include "port/heap_routing.h"
#include "port/port.h"
#include "revolution/nand.h"
#include "revolution/os.h"

// Slow host file-system calls run on a helper thread while this guest thread sleeps (platform/src/dvd/dvd.cpp): an fsync or a file create
// on the SD card can take hundreds of ms, and a guest thread that blocks in the host holds the CPU token and starves the audio interrupts.
extern "C" void port_host_job_wait(const char* what, void (*fn)(void*), void* arg);
template <class F>
static void hostJob(const char* what, F& f) {
    port_host_job_wait(what, [](void* p) { (*static_cast<F*>(p))(); }, &f);
}

static std::string sNandRoot;
static const char kHomeDir[] = "/title/00010000/524d4750/data";

// The Wii's Mii folder, where the Mii library (RFL, rfl_port.cpp) keeps its database. On the Vita it is the shared one
// Mii Maker Vita writes (ux0:data/mii/wii/RFL_DB.dat, the player's Miis as a Wii Mii database); elsewhere, under the
// saves. The game only reads it: writes there are refused, so a port never changes another app's Miis.
static const char kFaceLibDir[] = "/shared2/menu/FaceLib";
#if defined(__vita__)
static std::string sFaceLibRoot = "ux0:data/mii/wii";
#else
static std::string sFaceLibRoot;
#endif

extern "C" void port_nand_set_root(const char* root) { sNandRoot = root; }
extern "C" void port_nand_set_facelib_root(const char* root) { sFaceLibRoot = root ? root : ""; }
extern "C" void port_rfl_database_to_native(void* buf, uint32_t offset, uint32_t length);

static bool isFaceLib(const char* path) {
    return path && strncmp(path, kFaceLibDir, sizeof(kFaceLibDir) - 1) == 0 &&
           (path[sizeof(kFaceLibDir) - 1] == '/' || path[sizeof(kFaceLibDir) - 1] == '\0');
}

static bool isFaceLibDatabase(const char* path) {
    return isFaceLib(path) && strcmp(path + sizeof(kFaceLibDir) - 1, "/RFL_DB.dat") == 0;
}

static std::string hostPath(const char* path) {
    PortHostAllocScope scope;
    std::string p = path ? path : "";
    if (p.empty() || p[0] != '/') {
        p = std::string(kHomeDir) + "/" + p;
    }
    if (isFaceLib(p.c_str()) && !sFaceLibRoot.empty()) {
        return sFaceLibRoot + p.substr(sizeof(kFaceLibDir) - 1);
    }
    return sNandRoot + p;
}

static void makeDirs(const std::string& path) {
    PortHostAllocScope scope;
    for (size_t i = 1; i < path.size(); i++) {
        if (path[i] == '/') {
            std::string d = path.substr(0, i);
            mkdir(d.c_str(), 0770);
        }
    }
}

static s32 errnoToNand(int e) {
    switch (e) {
    case ENOENT:
        return NAND_RESULT_NOEXISTS;
    case EEXIST:
        return NAND_RESULT_EXISTS;
    case EACCES:
    case EPERM:
        return NAND_RESULT_ACCESS;
    case ENOSPC:
        return NAND_RESULT_MAXBLOCKS;
    case ENOTEMPTY:
        return NAND_RESULT_NOTEMPTY;
    default:
        return NAND_RESULT_UNKNOWN;
    }
}

extern "C" {

s32 NANDInit(void) { return NAND_RESULT_OK; }

s32 NANDGetHomeDir(char path[NAND_MAX_PATH]) {
    strcpy(path, kHomeDir);
    return NAND_RESULT_OK;
}

s32 NANDCreate(const char* path, u8 perm, u8 attr) {
    (void)perm;
    (void)attr;
    if (isFaceLib(path)) {
        return NAND_RESULT_ACCESS;
    }
    std::string hp = hostPath(path);
    s32 result = NAND_RESULT_OK;
    auto job = [&] {
        makeDirs(hp);
        int fd = open(hp.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0660);
        if (fd < 0) {
            result = errnoToNand(errno);
            return;
        }
        close(fd);
    };
    hostJob("NANDCreate", job);
    return result;
}

s32 NANDPrivateCreate(const char* path, u8 perm, u8 attr) { return NANDCreate(path, perm, attr); }

s32 NANDOpen(const char* path, NANDFileInfo* info, u8 mode) {
    if (isFaceLib(path) && mode != NAND_ACCESS_READ) {
        return NAND_RESULT_ACCESS;  // read-only (see sFaceLibRoot)
    }
    std::string hp = hostPath(path);
    int flags = O_CLOEXEC;
    switch (mode) {
    case NAND_ACCESS_READ:
        flags |= O_RDONLY;
        break;
    case NAND_ACCESS_WRITE:
        flags |= O_WRONLY;
        break;
    default:
        flags |= O_RDWR;
        break;
    }
    int fd = -1;
    int err = 0;
    auto job = [&] {
        fd = open(hp.c_str(), flags);
        err = errno;
    };
    hostJob("NANDOpen", job);
    if (fd < 0) {
        return errnoToNand(err);
    }
    memset(info, 0, sizeof(*info));
    info->fileDescriptor = fd;
    info->accType = mode;
    strncpy(info->origPath, path, NAND_MAX_PATH - 1);
    return NAND_RESULT_OK;
}

s32 NANDPrivateOpen(const char* path, NANDFileInfo* info, u8 mode) { return NANDOpen(path, info, mode); }

s32 NANDClose(NANDFileInfo* info) {
    if (info->fileDescriptor >= 0) {
        const int fd = info->fileDescriptor;
        const bool writable = info->accType != NAND_ACCESS_READ;
        info->fileDescriptor = -1;
        auto job = [&] {
            if (writable) {
                fsync(fd);
            }
            close(fd);
        };
        hostJob("NANDClose", job);
    }
    return NAND_RESULT_OK;
}

s32 NANDRead(NANDFileInfo* info, void* buf, u32 length) {
    ssize_t n = -1;
    int err = 0;
    const bool database = isFaceLibDatabase(info->origPath);
    off_t at = 0;
    auto job = [&] {
        if (database) at = lseek(info->fileDescriptor, 0, SEEK_CUR);
        n = read(info->fileDescriptor, buf, length);
        err = errno;
    };
    hostJob("NANDRead", job);
    if (database && n > 0 && at >= 0) port_rfl_database_to_native(buf, (uint32_t)at, (uint32_t)n);
    if (n > 0) port_gpu_memory_written(buf, (uint32_t)n);
    return n < 0 ? errnoToNand(err) : (s32)n;
}

s32 NANDWrite(NANDFileInfo* info, const void* buf, u32 length) {
    ssize_t n = -1;
    int err = 0;
    auto job = [&] {
        n = write(info->fileDescriptor, buf, length);
        err = errno;
    };
    hostJob("NANDWrite", job);
    return n < 0 ? errnoToNand(err) : (s32)n;
}

s32 NANDSeek(NANDFileInfo* info, s32 offset, s32 whence) {
    off_t r = lseek(info->fileDescriptor, offset, whence);
    return r < 0 ? errnoToNand(errno) : (s32)r;
}

s32 NANDGetLength(NANDFileInfo* info, u32* length) {
    struct stat st;
    if (fstat(info->fileDescriptor, &st) != 0) {
        return errnoToNand(errno);
    }
    *length = (u32)st.st_size;
    return NAND_RESULT_OK;
}

static int removeTree(const std::string& path) {
    struct stat st;
    if (lstat(path.c_str(), &st) != 0) {
        return -1;
    }
    if (S_ISDIR(st.st_mode)) {
        DIR* d = opendir(path.c_str());
        if (d) {
            while (struct dirent* e = readdir(d)) {
                if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
                    continue;
                }
                removeTree(path + "/" + e->d_name);
            }
            closedir(d);
        }
        return rmdir(path.c_str());
    }
    return unlink(path.c_str());
}

s32 NANDDelete(const char* path) {
    if (isFaceLib(path)) {
        return NAND_RESULT_ACCESS;
    }
    PortHostAllocScope scope;
    std::string hp = hostPath(path);
    s32 result = NAND_RESULT_OK;
    auto job = [&] {
        if (removeTree(hp) != 0) {
            result = errnoToNand(errno);
        }
    };
    hostJob("NANDDelete", job);
    return result;
}

s32 NANDMove(const char* path, const char* destDir) {
    PortHostAllocScope scope;
    std::string src = hostPath(path);
    std::string name = src.substr(src.rfind('/') + 1);
    std::string dst = hostPath(destDir) + "/" + name;
    s32 result = NAND_RESULT_OK;
    auto job = [&] {
        makeDirs(dst);
        if (rename(src.c_str(), dst.c_str()) != 0) {
            result = errnoToNand(errno);
        }
    };
    hostJob("NANDMove", job);
    return result;
}

s32 NANDCheck(u32 fsBlock, u32 inode, u32* answer) {
    (void)fsBlock;
    (void)inode;
    *answer = 0;  // enough space and inodes
    return NAND_RESULT_OK;
}

// ---------------------------------------------------------------------------
// Asynchronous calls (the Mii library's): done at once, their callbacks run a moment later from an alarm, as the Wii's
// arrive after the call returns. RFL depends on that order: a callback inside the call would see the request still being
// set up, and some paths would finish twice.
// ---------------------------------------------------------------------------

namespace {
struct NandPending {
    OSAlarm alarm;
    NANDCallback callback;
    NANDCommandBlock* block;
    s32 result;
    bool used;
};
NandPending sPending[16];

void nandAlarm(OSAlarm* alarm, OSContext*) {
    NandPending* p = (NandPending*)OSGetAlarmUserData(alarm);
    const NANDCallback cb = p->callback;
    NANDCommandBlock* const block = p->block;
    const s32 result = p->result;
    p->used = false;
    if (cb) cb(result, block);
}

s32 finish(s32 result, NANDCallback cb, NANDCommandBlock* block) {
    if (block) block->callback = (void*)cb;
    const BOOL enabled = OSDisableInterrupts();
    NandPending* p = nullptr;
    for (NandPending& q : sPending) {
        if (!q.used) {
            p = &q;
            break;
        }
    }
    if (!p) {
        OSRestoreInterrupts(enabled);
        return NAND_RESULT_BUSY;
    }
    p->used = true;
    p->callback = cb;
    p->block = block;
    p->result = result;
    OSCreateAlarm(&p->alarm);
    OSSetAlarmUserData(&p->alarm, p);
    OSSetAlarm(&p->alarm, OSMillisecondsToTicks(1), nandAlarm);
    OSRestoreInterrupts(enabled);
    return NAND_RESULT_OK;
}
}  // namespace

void NANDSetUserData(NANDCommandBlock* block, void* data) { block->userData = data; }
void* NANDGetUserData(const NANDCommandBlock* block) { return block->userData; }

s32 NANDOpenAsync(const char* path, NANDFileInfo* info, u8 mode, NANDCallback cb, NANDCommandBlock* block) {
    return finish(NANDOpen(path, info, mode), cb, block);
}
s32 NANDPrivateOpenAsync(const char* path, NANDFileInfo* info, const u8 mode, NANDCallback cb, NANDCommandBlock* block) {
    return finish(NANDOpen(path, info, mode), cb, block);
}
// "Safe" opens work on a copy on the Wii (committed by the close); a host file needs no copy.
s32 NANDPrivateSafeOpenAsync(const char* path, NANDFileInfo* info, const u8 mode, void* buf, const u32 length, NANDCallback cb,
                             NANDCommandBlock* block) {
    (void)buf;
    (void)length;
    return finish(NANDOpen(path, info, mode), cb, block);
}
s32 NANDSafeOpenAsync(const char* path, NANDFileInfo* info, const u8 mode, void* buf, const u32 length, NANDCallback cb,
                      NANDCommandBlock* block) {
    return NANDPrivateSafeOpenAsync(path, info, mode, buf, length, cb, block);
}
s32 NANDCloseAsync(NANDFileInfo* info, NANDCallback cb, NANDCommandBlock* block) {
    return finish(NANDClose(info), cb, block);
}
s32 NANDSafeCloseAsync(NANDFileInfo* info, NANDCallback cb, NANDCommandBlock* block) {
    return finish(NANDClose(info), cb, block);
}
s32 NANDReadAsync(NANDFileInfo* info, void* buf, u32 length, NANDCallback cb, NANDCommandBlock* block) {
    return finish(NANDRead(info, buf, length), cb, block);
}
s32 NANDWriteAsync(NANDFileInfo* info, const void* buf, u32 length, NANDCallback cb, NANDCommandBlock* block) {
    return finish(NANDWrite(info, buf, length), cb, block);
}
s32 NANDSeekAsync(NANDFileInfo* info, s32 offset, s32 whence, NANDCallback cb, NANDCommandBlock* block) {
    return finish(NANDSeek(info, offset, whence), cb, block);
}
s32 NANDGetLengthAsync(NANDFileInfo* info, u32* length, NANDCallback cb, NANDCommandBlock* block) {
    return finish(NANDGetLength(info, length), cb, block);
}
s32 NANDPrivateCreateAsync(const char* path, u8 perm, u8 attr, NANDCallback cb, NANDCommandBlock* block) {
    return finish(NANDCreate(path, perm, attr), cb, block);
}
s32 NANDPrivateDeleteAsync(const char* path, NANDCallback cb, NANDCommandBlock* block) {
    return finish(NANDDelete(path), cb, block);
}
s32 NANDPrivateCreateDirAsync(const char* path, u8 perm, u8 attr, NANDCallback cb, NANDCommandBlock* block) {
    (void)perm;
    (void)attr;
    if (isFaceLib(path) || strcmp(path, "/shared2/menu") == 0) {
        return finish(NAND_RESULT_EXISTS, cb, block);  // the Mii folder is there (and not the game's to make)
    }
    PortHostAllocScope scope;
    std::string hp = hostPath(path);
    s32 result = NAND_RESULT_OK;
    auto job = [&] {
        makeDirs(hp + "/");
        if (mkdir(hp.c_str(), 0770) != 0 && errno != EEXIST) result = errnoToNand(errno);
    };
    hostJob("NANDCreateDir", job);
    return finish(result, cb, block);
}

void NANDInitBanner(NANDBanner* banner, u32 flag, const u16* title, const u16* comment) {
    memset(banner, 0, sizeof(*banner));
    banner->signature = NAND_BANNER_SIGNATURE;
    banner->flag = flag;
    for (int i = 0; title && i < NAND_BANNER_COMMENT_SIZE && title[i]; i++) {
        banner->comment[0][i] = title[i];
    }
    for (int i = 0; comment && i < NAND_BANNER_COMMENT_SIZE && comment[i]; i++) {
        banner->comment[1][i] = comment[i];
    }
}

}  // extern "C"
