// The Wii's Mii library (RVLFaceLib, "RFL", built from the decompilation) on a little-endian host.
//
// RFL reads two big-endian files, which this turns into the layout its structures have on this host:
//
// - The face resource (RFL_Res.dat, in the disc's ObjectData/MiiFaceDatabase.arc), once, in memory, when RFLInitResAsync
//   receives it: the archive tables, the texture headers (their texels stay big-endian, as the GX layer reads them) and the
//   shapes' counts, vectors and vertex arrays (their index streams are bytes).
// - The Mii database (RFL_DB.dat, NAND /shared2/menu/FaceLib/, see nand.cpp), as it is read: the Miis' bit-fields are
//   rebuilt in this host's order (a bit-field's first member is its most significant bits on the Wii and its least significant
//   here), 16-bit text and the IDs' words swapped, and the header's CRC checked on the file's bytes, then made again over the
//   converted ones (RFL checks the CRC on what it holds).
#include <stdint.h>
#include <string.h>

#include "port/port.h"
#include <RVLFaceLib.h>
#include "RFLi_Types.h"

namespace {

inline uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
inline uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
inline void ne16(uint8_t* p, uint16_t v) { memcpy(p, &v, 2); }
inline void ne32(uint8_t* p, uint32_t v) { memcpy(p, &v, 4); }
inline void swap16(uint8_t* p) { ne16(p, be16(p)); }
inline void swap32(uint8_t* p) { ne32(p, be32(p)); }
inline void swap16n(uint8_t* p, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) swap16(p + 2 * i);
}

// ---------------------------------------------------------------------------
// RFL_Res.dat
// ---------------------------------------------------------------------------

// RFLiArcID order: what each archive holds.
enum Kind { kTexture, kShape, kShapeNoTxc, kFaceline };
const Kind kArchives[] = {
    kShapeNoTxc,  // Beard
    kTexture,     // Eye
    kTexture,     // Eyebrow
    kFaceline,    // Faceline
    kTexture,     // FaceTex
    kShapeNoTxc,  // ForeHead
    kShape,       // Glass
    kTexture,     // GlassTex
    kShapeNoTxc,  // Hair
    kShape,       // Mask
    kTexture,     // Mole
    kTexture,     // Mouth
    kTexture,     // Mustache
    kShapeNoTxc,  // Nose
    kShape,       // Nline
    kTexture,     // NlineTex
    kShape,       // Cap
    kTexture,     // CapTex
};
const uint32_t kNumArchives = sizeof(kArchives) / sizeof(kArchives[0]);

// RFLiTexture: u16 width, height, numColors; u32 paletteOfs, imageOfs; s16 lodBias.
void swapTexture(uint8_t* f, uint32_t size) {
    if (size < 0x20) return;
    swap16(f + 0x02);
    swap16(f + 0x04);
    swap16(f + 0x0A);
    swap32(f + 0x0C);
    swap16(f + 0x1A);
    swap32(f + 0x1C);
}

// The shape files RFLiInitShapeRes reads: a tag, (faceline: three Vecs), then counted s16 positions, normals and (not for
// the forehead, hair, beard and nose) texture coordinates, then the primitives as bytes.
void swapShape(uint8_t* f, uint32_t size, Kind kind) {
    uint8_t* p = f;
    uint8_t* const end = f + size;
    if (size < 4) return;
    swap32(p);
    p += 4;
    if (kind == kFaceline) {
        if (p + 36 > end) return;
        for (int i = 0; i < 9; i++) swap32(p + 4 * i);
        p += 36;
    }
    const struct {
        int components;
        bool present;
    } arrays[] = {{3, true}, {3, true}, {2, kind != kShapeNoTxc}};
    for (const auto& a : arrays) {
        if (!a.present) break;
        if (p + 2 > end) return;
        const uint16_t n = be16(p);
        ne16(p, n);
        p += 2;
        if (&a == &arrays[0] && n == 0) return;  // an empty shape: nothing follows that RFL reads
        const uint32_t values = (uint32_t)n * a.components;
        if (p + 2 * values > end) return;
        swap16n(p, values);
        p += 2 * values;
    }
}

}  // namespace

extern "C" void port_rfl_resource_to_native(void* buffer, uint32_t size) {
    uint8_t* const res = (uint8_t*)buffer;
    if (!res || size < 4 + 4 * kNumArchives) return;
    uint32_t first;
    memcpy(&first, res + 4, 4);
    if (first < size) return;  // already converted (the game keeps the archive between scenes)
    if (be32(res + 4) >= size) {
        port_log("rfl: RFL_Res.dat (%u bytes) is not a face resource this port reads", size);
        return;
    }
    uint32_t files = 0;
    for (uint32_t a = 0; a < kNumArchives; a++) {
        const uint32_t at = be32(res + 4 * (a + 1));
        if (at + 4 > size) return;
        const uint16_t count = be16(res + at);
        uint8_t* const table = res + at + 4;  // count + 1 offsets, from the end of the table
        if (at + 4 + 4 * (count + 1u) > size) return;
        uint8_t* const data = table + 4 * (count + 1u);
        for (uint32_t i = 0; i < count; i++) {
            const uint32_t from = be32(table + 4 * i), to = be32(table + 4 * (i + 1));
            if (to < from || (uint32_t)(data - res) + to > size) continue;
            uint8_t* f = data + from;
            if (kArchives[a] == kTexture) swapTexture(f, to - from);
            else swapShape(f, to - from, kArchives[a]);
            files++;
        }
        swap32(res + 4 * (a + 1));
        swap16(res + at);
        swap16(res + at + 2);
        for (uint32_t i = 0; i <= count; i++) swap32(table + 4 * i);
    }
    swap16(res);
    swap16(res + 2);
    port_log("rfl: face resource ready (%u files)", files);
}

// ---------------------------------------------------------------------------
// RFL_DB.dat
// ---------------------------------------------------------------------------

namespace {

const uint32_t kDbMiis = 0x04, kDbFlags = 0x1CEC, kDbHidden = 0x1D00, kDbHeaderSize = 0x1F1E0;
const uint32_t kCharSize = 0x4A, kHiddenCharSize = 0x40, kTableEntry = 0x0C, kTableCount = 10000;

// One big-endian 16-bit word of bit-fields (widths from the most significant) rebuilt with the first field lowest.
void fields16(uint8_t* p, const uint8_t* widths, int n) {
    const uint16_t v = be16(p);
    uint16_t out = 0;
    int from = 16, to = 0;
    for (int i = 0; i < n; i++) {
        from -= widths[i];
        out |= (uint16_t)(((v >> from) & ((1u << widths[i]) - 1)) << to);
        to += widths[i];
    }
    ne16(p, out);
}

void fields32(uint8_t* p, const uint8_t* widths, int n) {
    const uint32_t v = be32(p);
    uint32_t out = 0;
    int from = 32, to = 0;
    for (int i = 0; i < n; i++) {
        from -= widths[i];
        const uint32_t mask = widths[i] == 32 ? 0xFFFFFFFFu : ((1u << widths[i]) - 1);
        out |= ((v >> from) & mask) << to;
        to += widths[i];
    }
    ne32(p, out);
}

// RFLiCharData (hidden: RFLiHiddenCharData, its birthday a 9-bit padding and no creator name).
void charData(uint8_t* c, bool hidden) {
    static const uint8_t w00[] = {1, 1, 4, 5, 4, 1}, w00h[] = {1, 1, 9, 4, 1};
    static const uint8_t w20[] = {3, 3, 4, 3, 1, 2}, w22[] = {7, 3, 1, 5}, w24[] = {5, 5, 6}, w26[] = {3, 4, 5, 4};
    static const uint8_t w28[] = {6, 5, 5}, w2A[] = {3, 4, 4, 5}, w2C[] = {4, 4, 5, 3}, w2E[] = {5, 2, 4, 5};
    static const uint8_t w30[] = {4, 3, 4, 5}, w32[] = {2, 2, 3, 4, 5}, w34[] = {1, 4, 5, 5, 1};
#define F(off, w) fields16(c + (off), w, (int)sizeof(w))
    if (hidden) F(0x00, w00h);
    else F(0x00, w00);
    swap16n(c + 0x02, 10);  // name
    // 0x16 height, 0x17 build: bytes. 0x18 create ID: RFL reads it as two u32 (the flags in the first one's top bits).
    swap32(c + 0x18);
    swap32(c + 0x1C);
    F(0x20, w20);
    F(0x22, w22);
    F(0x24, w24);
    F(0x26, w26);
    F(0x28, w28);
    F(0x2A, w2A);
    F(0x2C, w2C);
    F(0x2E, w2E);
    F(0x30, w30);
    F(0x32, w32);
    F(0x34, w34);
#undef F
    if (!hidden) swap16n(c + 0x36, 10);  // creator name
}

uint16_t rflCrc(const uint8_t* p, uint32_t len) {
    uint16_t crc = 0;
    for (uint32_t i = 0; i < len; i++) {
        uint8_t data = p[i];
        for (int j = 0; j < 8; j++, data <<= 1) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
            if (data & 0x80) crc ^= 1;
        }
    }
    return crc;
}

// A database with no Miis, as RFL formats one (in file order; converted with the rest).
void emptyDatabase(uint8_t* db) {
    memset(db, 0, kDbHeaderSize);
    memcpy(db, "RNOD", 4);
    db[kDbFlags] = 0x80;
    memcpy(db + kDbHidden, "RNHD", 4);
    memset(db + kDbHidden + 4, 0xFF, 4);
    for (uint32_t i = 0; i < kTableCount; i++) {
        uint8_t* e = db + kDbHidden + 8 + i * kTableEntry;
        e[8] = 0x7F, e[9] = 0xFF, e[10] = 0x7F, e[11] = 0xFF;
    }
}

}  // namespace

// `buf` holds `length` bytes read from RFL_DB.dat at `offset`.
extern "C" void port_rfl_database_to_native(void* buf, uint32_t offset, uint32_t length) {
    uint8_t* const b = (uint8_t*)buf;
    if (!b) return;
    if (offset == 0 && length >= kDbHeaderSize) {
        if (memcmp(b, "RNOD", 4) != 0 || rflCrc(b, kDbHeaderSize) != 0) {
            // A file RFL would call broken (and the game then stops at an error screen): no Miis instead.
            port_log("rfl: RFL_DB.dat is not a valid Mii database; using none");
            emptyDatabase(b);
        }
        swap32(b);  // 'RNOD'
        for (uint32_t i = 0; i < 100; i++) charData(b + kDbMiis + i * kCharSize, false);
        static const uint8_t flags[] = {1, 31};
        fields32(b + kDbFlags, flags, 2);
        uint8_t* const h = b + kDbHidden;
        swap32(h);      // 'RNHD'
        swap16(h + 4);  // head
        swap16(h + 6);  // tail
        static const uint8_t links[] = {1, 15};
        for (uint32_t i = 0; i < kTableCount; i++) {
            uint8_t* e = h + 8 + i * kTableEntry;
            swap32(e);      // create ID, as above
            swap32(e + 4);
            fields16(e + 8, links, 2);   // sex : 1, next : 15
            fields16(e + 10, links, 2);  // padding : 1, prev : 15
        }
        // RFL's own check runs over what it holds now: make the CRC fit it (stored most significant byte first, so the
        // whole header checks to zero)
        b[kDbHeaderSize - 2] = b[kDbHeaderSize - 1] = 0;
        const uint16_t crc = rflCrc(b, kDbHeaderSize);
        b[kDbHeaderSize - 2] = (uint8_t)(crc >> 8);
        b[kDbHeaderSize - 1] = (uint8_t)crc;
        return;
    }
    // hidden Miis, read one or more whole records at a time
    if (offset >= kDbHeaderSize && (offset - kDbHeaderSize) % kHiddenCharSize == 0) {
        for (uint32_t at = 0; at + kHiddenCharSize <= length; at += kHiddenCharSize) charData(b + at, true);
    }
}

// ---------------------------------------------------------------------------
// Pieces of RFL the decompilation does not have yet, for paths the game does not take
// ---------------------------------------------------------------------------

extern "C" {

// Whether two Miis have the same face (the middle database's duplicate check): the parts, faceline to mole.
BOOL RFLiIsSameFaceCore(const RFLiCharInfo* lhs, const RFLiCharInfo* rhs) {
    return memcmp(lhs, rhs, offsetof(RFLiCharInfo, body)) == 0 ? TRUE : FALSE;
}

// Formatting the database (only for storing a Wii remote's Miis): the Mii folder is read-only here.
RFLErrcode RFLiFormatAsync(RFLiCallback) { return RFLErrcode_NotAvailable; }

// Miis kept in a Wii remote's memory: there are none.
s32 WPADReadFaceData(s32, void*, u16, u32, void*) { return -1; }  // WPAD_ERR_NO_CONTROLLER

}  // extern "C"
