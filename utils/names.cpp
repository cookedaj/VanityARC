#include "names.h"

#include <chrono>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "memory/memory.h"

namespace names {
namespace {

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

//--- RVAs and constants, v1029 (2026-11-17). Change these after a patch. --------------------------------------------
constexpr u64 NAMES_RVA        = 0x105726C0; //name pool
constexpr u64 TEXT_TABLE_RVA   = 0x1043A2A4; //64 x u16 text keys
constexpr u64 ANCHOR_RVA       = 0x104F48B0;
constexpr u64 ANCHOR_XOR       = 0x25FAB87EA2567D68ULL;

constexpr u32 HP               = 0x01000193;
constexpr u64 FNV_PRIME        = 0x100000001B3ULL;
constexpr u64 FNV_ADD          = 0x2790CE36A33B0E0AULL;

constexpr u32 SLOT_HASH_ADD    = 0x213E69E4u;
constexpr u32 SLOT_HASH_FOLD   = 0x269E4u;
constexpr u64 SLOT_KEY_ADD     = 0x902552F3625ADD06ULL;
constexpr u64 SLOT_UNFOLD_A    = 0xA15809052DCA75EDULL;
constexpr u64 SLOT_UNFOLD_B    = 0xBDEF271E65265A09ULL;

constexpr u32 SHARD_HASH_ADD   = 0x6C48F44Au;
constexpr u64 CHUNK_HASH_SEED  = 0x60;
constexpr u64 CHUNK_BLOCK_BASE = 0x70;
constexpr u32 CHUNK_OFFSET_MASK = 0xFFFF00;
constexpr u64 BLOCK_KEY_ADD    = 0xDFD86F31B20DF944ULL;
constexpr u64 BLOCK_UNFOLD_A   = 0x1148A6092DCA75EDULL;
constexpr u64 BLOCK_UNFOLD_B   = 0xF2121ADC1DBC1135ULL;

constexpr int TEXT_TABLE_COUNT = 64;
constexpr u32 STRING_KEY_BIAS  = 0x2DCA;
constexpr u32 STRING_ODD_MUL   = 0xFFFFD236u;
constexpr u32 STRING_ODD_ADD   = 0x75EDu;
constexpr u32 STRING_STEP_MUL  = 0x830A364u;
constexpr u32 STRING_STEP_ADD  = 0xEAE8BFEBu;

constexpr u32 PLAYER_NAME_ADD  = 0x79EA3F14u;
//-------------------------------------------------------------------------------------------------------------------

uintptr_t g_base = 0x140000000ULL;

std::mutex g_mutex;
bool g_inited = false;
Status g_status = Status::TableZero;
double g_lastInitTry = -1e9;
u16 g_textTable[TEXT_TABLE_COUNT];
u64 g_slotXor = 0, g_blockKey = 0;
//name index -> text. An empty text means "could not read": that is only remembered for a few seconds so a
//name that was not loaded yet is tried again, and the cache cannot grow without limit on garbage indexes.
struct CacheEntry { std::string text; double t = 0; };
std::unordered_map<u32, CacheEntry> g_cache;
constexpr double RETRY_FAILED_AFTER = 5.0;
constexpr size_t CACHE_LIMIT = 50000;

double nowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

inline u32 rol32(u32 v, int n) { n &= 31; return n ? (v << n) | (v >> (32 - n)) : v; }
inline u64 rol64(u64 v, int n) { n &= 63; return n ? (v << n) | (v >> (64 - n)) : v; }

inline bool memValid(u64 p) { return p > 0x10000ULL && p < 0x7FFFFFFFFFFFULL; }

//Carry-less multiply, low 64 bits of the result (what pclmulqdq gives for the low half)
u64 clmulLo(u64 a, u64 b) {
    u64 r = 0;
    for (int i = 0; i < 64; i++)
        if ((b >> i) & 1ULL) r ^= a << i;
    return r;
}

inline u64 unfold(u64 lo, u64 hi, u64 a, u64 b) { return lo ^ clmulLo(a, clmulLo(b, lo) ^ hi); }

std::string readPoolName(u32 index);

bool init() {
    if (g_inited) return true;
    const double t0 = nowSec();
    if (t0 - g_lastInitTry < 1.0) return false; //not readable a moment ago (game still loading): try again in a second
    g_lastInitTry = t0;
    u16 table[TEXT_TABLE_COUNT];
    ReadMemoryBuffer(g_base + TEXT_TABLE_RVA, table, sizeof table);
    int nonZero = 0;
    for (int i = 0; i < TEXT_TABLE_COUNT; i++) nonZero += table[i] != 0;
    if (nonZero < 8) { g_status = Status::TableZero; return false; } //game not running yet, or the RVA is stale
    std::memcpy(g_textTable, table, sizeof table);
    u64 anchor = g_base + ANCHOR_RVA;
    u64 key = anchor * ((anchor ^ ANCHOR_XOR) | 1);
    g_slotXor = key + SLOT_KEY_ADD;
    g_blockKey = key + BLOCK_KEY_ADD;
    //name 0 is always "None" in UE5: anything else means the pool / anchor RVAs or the key table are stale
    if (readPoolName(0) != "None") { g_status = Status::PoolStale; return false; }
    g_status = Status::Ok;
    g_inited = true;
    return true;
}

//Which of the four 0x20-byte slots after the object header holds the name
u32 slotFor(u64 obj) {
    u64 seed = obj + 0x10;
    u32 lo = (u32)seed, hi = (u32)(seed >> 32);
    u32 h = HP * rol32(lo, 22) + SLOT_HASH_ADD;
    h = HP * rol32(h, 28) + hi + SLOT_HASH_ADD;
    h = HP * (h >> 10) + SLOT_HASH_ADD;
    h = HP * (h >> 4);
    return ((((h + SLOT_HASH_FOLD) >> 16) ^ h) & 3u) ^ 2u;
}

//The name field of an object: index in the high 32 bits, instance number in the low 32 bits
bool readNameField(u64 obj, u32& index, u32& number) {
    u64 raw[2];
    ReadMemoryBuffer((uintptr_t)(obj + 0x20 + 0x20ULL * slotFor(obj)), raw, sizeof raw);
    if (!raw[0] && !raw[1]) return false;
    u64 v = unfold(raw[0], raw[1], SLOT_UNFOLD_A, SLOT_UNFOLD_B) ^ g_slotXor;
    index = (u32)(v >> 32);
    number = (u32)v;
    return true;
}

void shardHash(u64 addr, u32& b1, u32& b2) {
    u32 lo = (u32)addr, hi = (u32)(addr >> 32);
    u32 h = HP * rol32(lo, 25) + SHARD_HASH_ADD;
    h = HP * rol32(h, 26) + hi + SHARD_HASH_ADD;
    h = HP * (h >> 7) + SHARD_HASH_ADD;
    h = HP * (h >> 6) + SHARD_HASH_ADD;
    u32 mixed = h ^ (h >> 16);
    b1 = mixed & 7u;
    b2 = (mixed + 1u) & 7u;
}

//Address of the stored text for a name index
u64 resolveEntry(u32 index) {
    u64 chunk = g_base + NAMES_RVA + (u64)((index >> 8) & CHUNK_OFFSET_MASK);
    u32 b1, b2;
    shardHash(chunk + CHUNK_HASH_SEED, b1, b2);
    u64 blk1[2], blk2[2];
    ReadMemoryBuffer((uintptr_t)(chunk + CHUNK_BLOCK_BASE + 32ULL * b1), blk1, sizeof blk1);
    ReadMemoryBuffer((uintptr_t)(chunk + CHUNK_BLOCK_BASE + 32ULL * b2), blk2, sizeof blk2);
    if (!blk1[0] && !blk1[1] && !blk2[0] && !blk2[1]) return 0;
    u64 s1 = unfold(blk1[0], blk1[1], BLOCK_UNFOLD_A, BLOCK_UNFOLD_B) ^ g_blockKey;
    u64 s2 = unfold(blk2[0], blk2[1], BLOCK_UNFOLD_A, BLOCK_UNFOLD_B) ^ g_blockKey;
    u64 mix = FNV_PRIME * rol64(s1, 40) + FNV_ADD;
    mix = FNV_PRIME * rol64(mix, 54) + FNV_ADD;
    return (mix ^ s2) + s1 + 2ULL * (u16)index;
}

inline u16 textKey(u32 key, int i) {
    return g_textTable[((i & 1) == 0 ? key : key * STRING_ODD_MUL + STRING_ODD_ADD) & 0x3Fu];
}
inline u32 stepKey(u32 key, int i) { return (i & 1) ? key * STRING_STEP_MUL + STRING_STEP_ADD : key; }

bool printable(const std::string& s) {
    if (s.empty()) return false;
    for (unsigned char c : s) if (c < 32 || c > 126) return false;
    return true;
}

std::string readPoolName(u32 index) {
    u64 entry = resolveEntry(index);
    if (!memValid(entry)) return "";
    u16 header = 0;
    ReadMemoryBuffer((uintptr_t)entry, &header, sizeof header);
    bool wide = (header & 1) != 0;
    int length = ((header << 1) & 0x380) | (header >> 9);
    if (wide) length >>= 1;
    if (length <= 0 || length > 512) return "";

    std::string out;
    u32 key = (u32)length + STRING_KEY_BIAS;
    if (wide) {
        std::vector<u16> raw((size_t)length);
        ReadMemoryBuffer((uintptr_t)(entry + 2), raw.data(), raw.size() * 2);
        for (int i = 0; i < length; i++) {
            u16 c = (u16)(raw[(size_t)i] ^ textKey(key, i));
            key = stepKey(key, i);
            if (c == 0) break;
            if (c < 32 || c > 126) return ""; //object names are ASCII: a bad byte means a wrong decode, retried later
            out.push_back((char)c);
        }
    } else {
        std::vector<u8> raw((size_t)length);
        ReadMemoryBuffer((uintptr_t)(entry + 2), raw.data(), raw.size());
        for (int i = 0; i < length; i++) {
            u8 c = (u8)(raw[(size_t)i] ^ (u8)(textKey(key, i) >> 3));
            key = stepKey(key, i);
            if (c == 0) break;
            if (c < 32 || c > 126) return "";
            out.push_back((char)c);
        }
    }
    return printable(out) ? out : "";
}

//Letters/digits are rotated in groups, then the whole printable range is rotated (same as the game's routine)
u8 rotRange(int c, int lo, int hi, int n) { return (c >= lo && c <= hi) ? (u8)((c - lo + n) % (hi - lo + 1) + lo) : (u8)c; }

const u8* subTable() {
    static u8 t[256];
    static bool built = false;
    if (!built) {
        for (int c = 0; c < 256; c++) {
            int x = rotRange(c, 33, 126, 47);
            x = rotRange(x, 48, 57, 5);
            x = rotRange(x, 97, 122, 13);
            x = rotRange(x, 65, 90, 13);
            t[c] = rotRange(x, 33, 126, 47);
        }
        built = true;
    }
    return t;
}

} // namespace

void setBase(uintptr_t base) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_base = base;
    g_inited = false;
    g_cache.clear();
    g_lastInitTry = -1e9;
}

bool ready() {
    std::lock_guard<std::mutex> lk(g_mutex);
    return init();
}

Status status() {
    std::lock_guard<std::mutex> lk(g_mutex);
    init();
    return g_status;
}

void clearCache() {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_cache.clear();
    g_inited = false; //a new process may use a different text key table: read it again on the next lookup
    g_lastInitTry = -1e9;
}

uint64_t clmul64(uint64_t a, uint64_t b) { return clmulLo(a, b); }
uint32_t pickNameSlot(uintptr_t actor) { return slotFor(actor); }

std::string playerName(uintptr_t fstringAddr) {
    if (!fstringAddr) return "";
    uintptr_t data = ReadMemory<uintptr_t>(fstringAddr);
    int count = ReadMemory<int>(fstringAddr + 8);
    if (!memValid(data) || count <= 1 || count > 64) return "";

    int len = count - 1;
    std::vector<u16> ch((size_t)len);
    ReadMemoryBuffer(data, ch.data(), ch.size() * 2);

    const u8* sub = subTable();
    u32 state = 0;
    std::string out;
    for (int i = 0; i < len && ch[(size_t)i] != 0; i++) {
        state = 16777619u * (state + rol32(16777619u * state + PLAYER_NAME_ADD, 16));
        u16 c = sub[(ch[(size_t)i] ^ (int)(state & 0x1Fu)) & 0xFF];
        out.push_back(c >= 32 && c <= 126 ? (char)c : '?'); //never let a wrong decode put control characters on screen
    }
    return out;
}

Decoded objectName(uintptr_t obj) {
    Decoded d;
    if (!memValid(obj)) return d;
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!init()) return d;
    u32 index, number;
    if (!readNameField(obj, index, number) || index == 0) return d;
    const double t = nowSec();
    auto it = g_cache.find(index);
    if (it == g_cache.end() || (it->second.text.empty() && t - it->second.t > RETRY_FAILED_AFTER)) {
        if (g_cache.size() >= CACHE_LIMIT) g_cache.clear();
        it = g_cache.insert_or_assign(index, CacheEntry{readPoolName(index), t}).first;
    }
    if (it->second.text.empty()) return d;
    d.ok = true;
    d.index = index;
    d.number = number;
    d.text = it->second.text;
    return d;
}

std::string name(uintptr_t obj) { return objectName(obj).text; }

std::string fullName(uintptr_t obj) {
    Decoded d = objectName(obj);
    if (!d.ok) return "";
    if (d.number == 0 || d.number > 0x100000u) return d.text;
    return d.text + "_" + std::to_string(d.number - 1);
}

} // namespace names
