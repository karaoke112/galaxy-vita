// The Mii library's files after platform/src/sdk/rfl_port.cpp has turned them into this host's layout, read back through
// RFL's own structures (decomp/libs/RVLFaceLib, little-endian bit-fields as on the Vita): a Mii database (RFL_DB.dat) and a
// face resource (RFL_Res.dat), both written here big-endian, field by field, as the Wii has them.
#include <RVLFaceLib.h>
#include "RFLi_HiddenDatabase.h"
#include "RFLi_Texture.h"
#include "RFLi_Types.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" void port_rfl_resource_to_native(void* buffer, uint32_t size);
extern "C" void port_rfl_database_to_native(void* buf, uint32_t offset, uint32_t length);
extern "C" void port_log(const char*, ...) {}

static int g_fail = 0;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                       \
            ++g_fail;                                                                                                  \
        }                                                                                                              \
    } while (0)

// RFLiDatabase (RFLi_Database.h, which needs the SDK's OS headers): the same members.
struct Database {
    u32 identifier;
    RFLiCharData rawData[RFL_DB_CHAR_MAX];
    u32 isolation : 1;
    u32 padding1 : 31;
    u8 specialInvite[13];
    u8 nwc24Month;
    u8 mwc24Day;
    u8 padding2;
    RFLiHiddenDB hidden;
};
static_assert(sizeof(RFLiCharData) == 0x4A, "RFLiCharData");
static_assert(offsetof(Database, specialInvite) == 0x1CF0, "Database flags");
static_assert(offsetof(Database, hidden) == 0x1D00, "Database hidden header");
static_assert(sizeof(Database) == 0x1F1E0, "RFLiDatabase");

// ---- writing the Wii's layout ----------------------------------------------

struct Bits {
    uint8_t* p;
    unsigned bit = 0;
    void put(unsigned width, unsigned value) {
        for (unsigned i = 0; i < width; i++, bit++) {
            const unsigned v = (value >> (width - 1 - i)) & 1;
            if (v) p[bit / 8] |= (uint8_t)(0x80 >> (bit % 8));
            else p[bit / 8] &= (uint8_t)~(0x80 >> (bit % 8));
        }
    }
};

static void put16(uint8_t* p, unsigned v) { p[0] = (uint8_t)(v >> 8), p[1] = (uint8_t)v; }
static void put32(uint8_t* p, uint32_t v) { put16(p, v >> 16), put16(p + 2, v & 0xFFFF); }

struct Mii {
    unsigned sex, month, day, favColor, favorite, height, build;
    uint32_t miiId, systemId;
    unsigned faceType, faceColor, faceTex, localonly, type;
    unsigned hairType, hairColor, hairFlip;
    unsigned browType, browRotate, browColor, browScale, browY, browX;
    unsigned eyeType, eyeRotate, eyeY, eyeColor, eyeScale, eyeX;
    unsigned noseType, noseScale, noseY;
    unsigned mouthType, mouthColor, mouthScale, mouthY;
    unsigned glassType, glassColor, glassScale, glassY;
    unsigned mustacheType, beardType, beardColor, beardScale, beardY;
    unsigned moleType, moleScale, moleY, moleX;
    uint16_t name[10], creator[10];
};

static unsigned rnd(unsigned bits) { return (unsigned)std::rand() & ((1u << bits) - 1); }

static Mii randomMii() {
    Mii m{};
    m.sex = rnd(1), m.month = rnd(4), m.day = rnd(5), m.favColor = rnd(4), m.favorite = rnd(1);
    m.height = rnd(7), m.build = rnd(7);
    m.miiId = 0x80000000u | ((uint32_t)std::rand() & 0x1FFFFFFF), m.systemId = (uint32_t)std::rand() * 2654435761u;
    m.faceType = rnd(3), m.faceColor = rnd(3), m.faceTex = rnd(4), m.localonly = rnd(1), m.type = rnd(2);
    m.hairType = rnd(7), m.hairColor = rnd(3), m.hairFlip = rnd(1);
    m.browType = rnd(5), m.browRotate = rnd(5), m.browColor = rnd(3), m.browScale = rnd(4), m.browY = rnd(5), m.browX = rnd(4);
    m.eyeType = rnd(6), m.eyeRotate = rnd(5), m.eyeY = rnd(5), m.eyeColor = rnd(3), m.eyeScale = rnd(4), m.eyeX = rnd(4);
    m.noseType = rnd(4), m.noseScale = rnd(4), m.noseY = rnd(5);
    m.mouthType = rnd(5), m.mouthColor = rnd(2), m.mouthScale = rnd(4), m.mouthY = rnd(5);
    m.glassType = rnd(4), m.glassColor = rnd(3), m.glassScale = rnd(4), m.glassY = rnd(5);
    m.mustacheType = rnd(2), m.beardType = rnd(2), m.beardColor = rnd(3), m.beardScale = rnd(4), m.beardY = rnd(5);
    m.moleType = rnd(1), m.moleScale = rnd(4), m.moleY = rnd(5), m.moleX = rnd(5);
    for (int i = 0; i < 10; i++) m.name[i] = (uint16_t)(0x3041 + rnd(8)), m.creator[i] = (uint16_t)(0x41 + rnd(5));
    return m;
}

// RFLCharData as the Wii stores it (big-endian, each bit-field from the top bit).
static void writeMii(uint8_t* c, const Mii& m) {
    std::memset(c, 0, 0x4A);
    Bits b{c};
    b.put(1, 0), b.put(1, m.sex), b.put(4, m.month), b.put(5, m.day), b.put(4, m.favColor), b.put(1, m.favorite);
    for (int i = 0; i < 10; i++) put16(c + 2 + 2 * i, m.name[i]);
    c[0x16] = (uint8_t)m.height, c[0x17] = (uint8_t)m.build;
    put32(c + 0x18, m.miiId), put32(c + 0x1C, m.systemId);
    b.bit = 0x20 * 8;
    b.put(3, m.faceType), b.put(3, m.faceColor), b.put(4, m.faceTex), b.put(3, 0), b.put(1, m.localonly), b.put(2, m.type);
    b.put(7, m.hairType), b.put(3, m.hairColor), b.put(1, m.hairFlip), b.put(5, 0);
    b.put(5, m.browType), b.put(5, m.browRotate), b.put(6, 0);
    b.put(3, m.browColor), b.put(4, m.browScale), b.put(5, m.browY), b.put(4, m.browX);
    b.put(6, m.eyeType), b.put(5, m.eyeRotate), b.put(5, m.eyeY);
    b.put(3, m.eyeColor), b.put(4, m.eyeScale), b.put(4, m.eyeX), b.put(5, 0);
    b.put(4, m.noseType), b.put(4, m.noseScale), b.put(5, m.noseY), b.put(3, 0);
    b.put(5, m.mouthType), b.put(2, m.mouthColor), b.put(4, m.mouthScale), b.put(5, m.mouthY);
    b.put(4, m.glassType), b.put(3, m.glassColor), b.put(4, m.glassScale), b.put(5, m.glassY);
    b.put(2, m.mustacheType), b.put(2, m.beardType), b.put(3, m.beardColor), b.put(4, m.beardScale), b.put(5, m.beardY);
    b.put(1, m.moleType), b.put(4, m.moleScale), b.put(5, m.moleY), b.put(5, m.moleX), b.put(1, 0);
    for (int i = 0; i < 10; i++) put16(c + 0x36 + 2 * i, m.creator[i]);
}

static void checkMii(const RFLiCharData& d, const Mii& m) {
    CHECK(d.sex == m.sex && d.birthMonth == m.month && d.birthDay == m.day && d.favoriteColor == m.favColor && d.favorite == m.favorite);
    for (int i = 0; i < 10; i++) CHECK((uint16_t)d.name[i] == m.name[i] && (uint16_t)d.creatorName[i] == m.creator[i]);
    CHECK(d.height == m.height && d.build == m.build);
    // RFL reads the create ID as two u32 (RFLiIsSpecialID: the top bit of the first)
    uint32_t id[2];
    std::memcpy(id, d.createID.data, 8);
    CHECK(id[0] == m.miiId && id[1] == m.systemId);
    CHECK(d.faceType == m.faceType && d.faceColor == m.faceColor && d.faceTex == m.faceTex && d.localonly == m.localonly && d.type == m.type);
    CHECK(d.hairType == m.hairType && d.hairColor == m.hairColor && d.hairFlip == m.hairFlip);
    CHECK(d.eyebrowType == m.browType && d.eyebrowRotate == m.browRotate && d.eyebrowColor == m.browColor && d.eyebrowScale == m.browScale &&
          d.eyebrowY == m.browY && d.eyebrowX == m.browX);
    CHECK(d.eyeType == m.eyeType && d.eyeRotate == m.eyeRotate && d.eyeY == m.eyeY && d.eyeColor == m.eyeColor && d.eyeScale == m.eyeScale &&
          d.eyeX == m.eyeX);
    CHECK(d.noseType == m.noseType && d.noseScale == m.noseScale && d.noseY == m.noseY);
    CHECK(d.mouthType == m.mouthType && d.mouthColor == m.mouthColor && d.mouthScale == m.mouthScale && d.mouthY == m.mouthY);
    CHECK(d.glassType == m.glassType && d.glassColor == m.glassColor && d.glassScale == m.glassScale && d.glassY == m.glassY);
    CHECK(d.mustacheType == m.mustacheType && d.beardType == m.beardType && d.beardColor == m.beardColor && d.beardScale == m.beardScale &&
          d.beardY == m.beardY);
    CHECK(d.moleType == m.moleType && d.moleScale == m.moleScale && d.moleY == m.moleY && d.moleX == m.moleX);
}

// RFLiCalculateCRC
static uint16_t crc(const uint8_t* p, size_t n) {
    uint16_t c = 0;
    for (size_t i = 0; i < n; i++) {
        uint8_t d = p[i];
        for (int j = 0; j < 8; j++, d <<= 1) {
            c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
            if (d & 0x80) c ^= 1;
        }
    }
    return c;
}

static void testDatabase() {
    std::vector<uint8_t> file(0x1F1E0 + 10000 * 0x40, 0);
    uint8_t* f = file.data();
    std::memcpy(f, "RNOD", 4);
    Mii miis[100];
    for (int i = 0; i < 100; i++) {
        miis[i] = randomMii();
        if (i % 7 != 3) writeMii(f + 4 + i * 0x4A, miis[i]);  // some slots empty
    }
    f[0x1CEC] = 0x80;
    std::memcpy(f + 0x1D00, "RNHD", 4);
    put16(f + 0x1D04, 0xFFFF), put16(f + 0x1D06, 0xFFFF);
    for (int i = 0; i < 10000; i++) {
        uint8_t* e = f + 0x1D08 + i * 12;
        put16(e + 8, 0x7FFF), put16(e + 10, 0x7FFF);
    }
    put16(f + 0x1F1DE, crc(f, 0x1F1E0));
    CHECK(crc(f, 0x1F1E0) == 0);

    std::vector<uint8_t> read(f, f + 0x1F1E0);
    port_rfl_database_to_native(read.data(), 0, (uint32_t)read.size());
    const Database* db = reinterpret_cast< const Database* >(read.data());
    CHECK(db->identifier == 'RNOD');
    CHECK(db->isolation == 1);
    for (int i = 0; i < 100; i++) {
        if (i % 7 == 3) CHECK(db->rawData[i].name[0] == 0);
        else checkMii(db->rawData[i], miis[i]);
    }
    CHECK(db->hidden.identifier == 'RNHD' && db->hidden.head == -1 && db->hidden.tail == -1);
    CHECK(db->hidden.data[0].next == -1 && db->hidden.data[0].prev == -1 && db->hidden.data[9999].next == -1);
    // RFL checks the CRC over what it holds
    CHECK(crc(read.data(), read.size()) == 0);

    // a damaged file reads as a valid database with no Miis (not one RFL calls broken)
    std::vector<uint8_t> bad(f, f + 0x1F1E0);
    bad[0x100] ^= 0x40;
    port_rfl_database_to_native(bad.data(), 0, (uint32_t)bad.size());
    const Database* empty = reinterpret_cast< const Database* >(bad.data());
    CHECK(empty->identifier == 'RNOD' && empty->hidden.identifier == 'RNHD' && empty->hidden.head == -1);
    for (int i = 0; i < 100; i++) CHECK(empty->rawData[i].name[0] == 0);
    CHECK(crc(bad.data(), bad.size()) == 0);

    // hidden Miis, read whole records after the header
    uint8_t hidden[2 * 0x40] = {}, full[0x4A];
    Mii h = randomMii();
    writeMii(full, h);
    std::memcpy(hidden + 0x40, full, 0x40);  // a hidden record has no creator name
    port_rfl_database_to_native(hidden, 0x1F1E0 + 5 * 0x40, sizeof(hidden));
    const RFLiHiddenCharData* hd = reinterpret_cast< const RFLiHiddenCharData* >(hidden + 0x40);
    CHECK(hd->sex == h.sex && hd->favoriteColor == h.favColor && hd->hairType == h.hairType && hd->eyeType == h.eyeType &&
          hd->moleX == h.moleX && hd->beardY == h.beardY && (uint16_t)hd->name[3] == h.name[3]);
}

// ---- RFL_Res.dat ------------------------------------------------------------

static void testResource() {
    // archive kinds in RFLiArcID order: 0 texture, 1 shape, 2 shape without texture coordinates, 3 faceline
    static const int kinds[18] = {2, 0, 0, 3, 0, 2, 1, 0, 2, 1, 0, 0, 0, 2, 1, 0, 1, 0};
    std::vector<uint8_t> res(0x40000, 0);
    uint8_t* r = res.data();
    put16(r, 0), put16(r + 2, 7);
    uint32_t at = 4 + 18 * 4 + 12;
    struct Shape {
        uint32_t file;
        int kind;
    } shapes[18];
    uint32_t textures[18];
    for (int a = 0; a < 18; a++) {
        put32(r + 4 + 4 * a, at);
        put16(r + at, 1), put16(r + at + 2, 0x200);  // one file
        uint8_t* table = r + at + 4;
        uint8_t* data = table + 8;
        uint32_t size;
        if (kinds[a] == 0) {
            // RFLiTexture: format 5 (RGB5A3), 64 x 32, image at 0x20, then texels
            data[0] = 5;
            put16(data + 2, 64), put16(data + 4, 32), put16(data + 0x0A, 0), put32(data + 0x0C, 0), put16(data + 0x1A, 0xFFF0);
            put32(data + 0x1C, 0x20);
            for (int i = 0; i < 16; i++) data[0x20 + i] = (uint8_t)(0xA0 + i);
            size = 0x20 + 64 * 32 * 2;
            textures[a] = (uint32_t)(data - r);
        } else {
            uint8_t* p = data;
            put32(p, 0x73686170), p += 4;
            if (kinds[a] == 3) {
                for (int i = 0; i < 9; i++) {
                    float v = 1.5f * (float)(i + 1);
                    uint32_t u;
                    std::memcpy(&u, &v, 4);
                    put32(p, u), p += 4;
                }
            }
            put16(p, 2), p += 2;  // positions
            for (int i = 0; i < 6; i++) put16(p, (unsigned)(int16_t)(-300 + 100 * i)), p += 2;
            put16(p, 1), p += 2;  // normals
            for (int i = 0; i < 3; i++) put16(p, (unsigned)(int16_t)(1000 * i - 1000)), p += 2;
            if (kinds[a] != 2) {
                put16(p, 2), p += 2;  // texture coordinates
                for (int i = 0; i < 4; i++) put16(p, 0x1234 + i), p += 2;
            }
            const uint8_t dl[] = {1, 3, 0x90, 0, 0, 1, 0, 0, 1, 0};
            std::memcpy(p, dl, sizeof(dl)), p += sizeof(dl);
            size = (uint32_t)(p - data);
            shapes[a] = {(uint32_t)(data - r), kinds[a]};
        }
        put32(table, 0), put32(table + 4, size);
        at = (uint32_t)(data - r) + ((size + 31) & ~31u);
    }
    const uint32_t total = at;
    port_rfl_resource_to_native(r, total);
    port_rfl_resource_to_native(r, total);  // the game keeps the archive between scenes: once only

    // read as RFL_NANDLoader.c does (parseOnmemoryRes_, getCachedFile_)
    CHECK(*(u16*)(r + 2) == 7);
    for (int a = 0; a < 18; a++) {
        const u32 offset = *(u32*)(r + 4 * (a + 1));
        const u16 numFiles = *(u16*)(r + offset);
        CHECK(numFiles == 1 && *(u16*)(r + offset + 2) == 0x200);
        const u32 arc = offset + 4;
        const u32 self = ((u32*)(r + arc))[0], next = ((u32*)(r + arc))[1];
        const u8* file = r + arc + self + numFiles * 4 + 4;
        CHECK(next > self);
        if (kinds[a] == 0) {
            CHECK(file == r + textures[a]);
            const RFLiTexture* t = reinterpret_cast< const RFLiTexture* >(file);
            CHECK(t->format == 5 && t->width == 64 && t->height == 32 && t->imageOfs == 0x20 && t->lodBias == -16);
            CHECK(file[0x20] == 0xA0 && file[0x21] == 0xA1);  // texels stay as the GX layer reads them
        } else {
            CHECK(file == r + shapes[a].file);
            const u8* p = file + 4;
            if (kinds[a] == 3) {
                const float* v = reinterpret_cast< const float* >(p);
                CHECK(v[0] == 1.5f && v[8] == 13.5f);
                p += 36;
            }
            CHECK(*(const u16*)p == 2);
            const s16* pos = reinterpret_cast< const s16* >(p + 2);
            CHECK(pos[0] == -300 && pos[5] == 200);
            p += 2 + 12;
            CHECK(*(const u16*)p == 1);
            const s16* nrm = reinterpret_cast< const s16* >(p + 2);
            CHECK(nrm[0] == -1000 && nrm[2] == 1000);
            p += 2 + 6;
            if (kinds[a] != 2) {
                CHECK(*(const u16*)p == 2 && reinterpret_cast< const u16* >(p + 2)[3] == 0x1237);
                p += 2 + 8;
            }
            CHECK(p[0] == 1 && p[1] == 3 && p[2] == 0x90);  // the primitives stay bytes
        }
    }
}

int main() {
    std::srand(12345);
    testDatabase();
    testResource();
    if (g_fail) {
        std::fprintf(stderr, "rfl_port_layout: %d failed\n", g_fail);
        return 1;
    }
    std::printf("rfl_port_layout: all passed\n");
    return 0;
}
