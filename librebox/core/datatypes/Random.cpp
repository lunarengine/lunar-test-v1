#include "Random.h"
#include <random>
#include <cmath>
#include <algorithm>
#include <limits>
#include <cstring>
#include <stdexcept>

using namespace lb;

// --------- PCG helpers ---------
static inline uint32_t pcg32_step(uint64_t& state, uint64_t inc){
    state = state * 6364136223846793005ULL + (inc | 1ULL);
    uint32_t xorshifted = (uint32_t)(((state >> 18u) ^ state) >> 27u);
    uint32_t rot = (uint32_t)(state >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31u));
}
uint32_t Random::pcg32(){ return pcg32_step(state_, inc_); }
uint64_t Random::pcg64(){ return (uint64_t)pcg32() << 32 | pcg32(); }

uint64_t Random::mix64(uint64_t x){
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

// --------- Constructors ---------
Random::Random(){
    std::random_device rd;
    uint64_t a = ((uint64_t)rd() << 32) ^ rd();
    uint64_t b = ((uint64_t)rd() << 32) ^ rd();

    state_ = 0;
    inc_   = (mix64(b) << 1u) | 1u;
    pcg32();
    state_ += mix64(a);
    pcg32();
}
Random::Random(uint64_t seed){
    uint64_t s0 = mix64(seed ^ 0xD2B74407B1CE6E93ULL);
    uint64_t s1 = mix64(seed + 0xCA5A826395121157ULL);
    state_ = 0;
    inc_   = (s1 << 1u) | 1u;
    pcg32();
    state_ += s0;
    pcg32();
}

// --------- Core API ---------
int64_t Random::NextInteger(int64_t min, int64_t max){
    if (min > max) throw std::runtime_error("NextInteger: min > max");
    // span = (uint64_t)(max - min) + 1 without __int128
    const uint64_t span = (uint64_t)max - (uint64_t)min + 1ULL;

    if (span == 0ULL) {
        // Full 64-bit range
        return (int64_t)pcg64();
    }

    const uint64_t limit = std::numeric_limits<uint64_t>::max() - (std::numeric_limits<uint64_t>::max() % span);
    uint64_t r;
    do { r = pcg64(); } while (r >= limit);
    const uint64_t val = (uint64_t)min + (r % span);
    return (int64_t)val;
}

double Random::NextNumber(){
    const uint64_t u = pcg64();
    const uint64_t t = (u >> 11) & ((1ULL << 53) - 1ULL);
    return (double)t / 9007199254740991.0; // 2^53 - 1
}
double Random::NextNumber(double min, double max){
    if (!(min <= max)) throw std::runtime_error("NextNumber: min > max");
    return min + (max - min) * NextNumber();
}

Vector3Game Random::NextUnitVector(){
    const double z = NextNumber(-1.0, 1.0);
    // Avoid M_PI on MSVC
    const double TAU = 6.28318530717958647692;
    const double t = NextNumber(0.0, TAU);
    const double r = std::sqrt(std::max(0.0, 1.0 - z*z));
    return Vector3Game{ (float)(r * std::cos(t)), (float)(r * std::sin(t)), (float)z };
}

void Random::Shuffle(lua_State* L, int tblIndex){
    if (tblIndex < 0) tblIndex = lua_gettop(L) + 1 + tblIndex;
    if (!lua_istable(L, tblIndex)) { luaL_error(L, "Shuffle expects a table"); return; }

    lua_Integer n = 0;
    lua_pushnil(L);
    while (lua_next(L, tblIndex) != 0) {
        if (lua_type(L, -2) == LUA_TNUMBER) {
            lua_Number k = lua_tonumber(L, -2);
            lua_Integer ik = (lua_Integer)k;
            if (k == (lua_Number)ik && ik > 0) if (ik > n) n = ik;
        }
        lua_pop(L, 1);
    }
    for (lua_Integer i = 1; i <= n; ++i){
        lua_rawgeti(L, tblIndex, (int)i);
        if (lua_isnil(L, -1)) { lua_pop(L, 1); luaL_error(L, "Shuffle requires contiguous array"); return; }
        lua_pop(L, 1);
    }
    for (lua_Integer i = n; i >= 2; --i){
        int64_t j = NextInteger(1, (int64_t)i);
        lua_rawgeti(L, tblIndex, (int)i);
        lua_rawgeti(L, tblIndex, (int)j);
        lua_rawseti(L, tblIndex, (int)i);
        lua_rawseti(L, tblIndex, (int)j);
    }
}

// --------- Lua bindings ---------
static int r_new(lua_State* L){
    int n = lua_gettop(L);
    if (n == 0) { lb::push(L, Random{}); return 1; }
    if (n == 1){
        uint64_t seed = (uint64_t)luaL_checkinteger(L,1);
        lb::push(L, Random{seed});
        return 1;
    }
    luaL_error(L, "Random.new expects () or (seed:int)");
    return 0;
}

static int r_nextint(lua_State* L){
    auto* r = const_cast<Random*>(lb::check<Random>(L,1));
    int64_t a = (int64_t)luaL_checkinteger(L,2);
    int64_t b = (int64_t)luaL_checkinteger(L,3);
    lua_pushinteger(L, r->NextInteger(a,b));
    return 1;
}

static int r_nextnum(lua_State* L){
    auto* r = const_cast<Random*>(lb::check<Random>(L,1));
    int argc = lua_gettop(L) - 1;
    if (argc == 0){
        lua_pushnumber(L, r->NextNumber());
        return 1;
    } else if (argc == 2){
        double a = luaL_checknumber(L,2);
        double b = luaL_checknumber(L,3);
        lua_pushnumber(L, r->NextNumber(a,b));
        return 1;
    }
    luaL_error(L, "NextNumber expects () or (min:number, max:number)");
    return 0;
}

static int r_nextunit(lua_State* L){
    auto* r = const_cast<Random*>(lb::check<Random>(L,1));
    lb::push(L, r->NextUnitVector());
    return 1;
}

static int r_shuffle(lua_State* L){
    auto* r = const_cast<Random*>(lb::check<Random>(L,1));
    r->Shuffle(L, 2);
    return 0;
}

static int r_tostring(lua_State* L){
    lua_pushliteral(L, "Random");
    return 1;
}

static const luaL_Reg R_METHODS[] = {
    {"NextInteger",    r_nextint},
    {"NextNumber",     r_nextnum},
    {"NextUnitVector", r_nextunit},
    {"Shuffle",        r_shuffle},
    {nullptr,nullptr}
};
static const luaL_Reg R_META[] = {
    {"__tostring", r_tostring},
    {nullptr,nullptr}
};

lua_CFunction Traits<Random>::Ctor() { return r_new; }
const luaL_Reg* Traits<Random>::Methods() { return R_METHODS; }
const luaL_Reg* Traits<Random>::MetaMethods() { return R_META; }
