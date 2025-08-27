// ================== bootstrap/LuaScheduler.cpp ==================
#include "bootstrap/LuaScheduler.h"
#include "bootstrap/instances/BaseScript.h"
#include "core/logging/Logging.h"

#include <cstdlib>
#include <limits>
#include <algorithm>
#include <cmath>

// Raylib time
#include <raylib.h>

// Luau
#include "lua.h"
#include "lualib.h"
#include "luacode.h"
#include <limits>

LuaScheduler::LuaScheduler()
    : sleepingByTime(TimeCmp{&state})
    , sleepingTasks(TaskTimeCmp{&tasks})
{
    LOGI("LuaScheduler: Initializing...");
    L_main = luaL_newstate();
    if (!L_main) {
        LOGE("LuaScheduler: luaL_newstate failed");
        return;
    }
    luaL_openlibs(L_main);

    lua_gc(L_main, LUA_GCSETGOAL,     200);
    lua_gc(L_main, LUA_GCSETSTEPMUL,  200);
    lua_gc(L_main, LUA_GCSETSTEPSIZE, 128);
}

LuaScheduler::~LuaScheduler() {
    LOGI("LuaScheduler: Shutting down...");
    while (!sleepingByTime.empty()) sleepingByTime.pop();
    while (!sleepingTasks.empty()) sleepingTasks.pop();
    ready.clear();
    nextFrameQ.clear();
    readyTasks.clear();
    nextFrameTasks.clear();
    // Unref any remaining task threads
    if (L_main) {
        for (auto& kv : tasks) {
            if (kv.second.registryRef != LUA_NOREF) {
                lua_unref(L_main, kv.second.registryRef);
            }
            kv.second.registryRef = LUA_NOREF;
        }
    }
    tasks.clear();
    state.clear();
    if (L_main) {
        lua_close(L_main);
        L_main = nullptr;
    }
}

// ===== bootstrap/LuaScheduler.cpp =====
// ScriptState: add
int pendingArgc = 0;
bool hasPending = false;

// GetScriptThread
lua_State* LuaScheduler::GetScriptThread(BaseScript* s) {
    auto it = state.find(s); return it==state.end() ? nullptr : it->second.co;
}

void LuaScheduler::SetWaitEvent(BaseScript* s){
    auto it = state.find(s); if (it==state.end()) return;
    auto& st = it->second;
    st.status    = Status::Waiting;
    st.nextFrame = false;
    st.wakeTime  = std::numeric_limits<double>::infinity(); // parked
}

void LuaScheduler::SetTaskWaitEvent(lua_State* co){
    auto it = tasks.find(co); if (it==tasks.end()) return;
    auto& st = it->second;
    st.status    = Status::Waiting;
    st.nextFrame = false;
    st.wakeTime  = std::numeric_limits<double>::infinity();
}

void LuaScheduler::ResumeScriptNextFrame(BaseScript* s, int argc){
    auto it = state.find(s); if (it==state.end()) return;
    auto& st = it->second;
    st.status     = Status::Running;
    st.nextFrame  = true;
    st.pendingArgc = argc;
    st.hasPending = true;
    nextFrameQ.push_back(st.co ? std::static_pointer_cast<BaseScript>(s->shared_from_this()) : nullptr);
}

void LuaScheduler::WakeTaskNextFrame(lua_State* co, int argc){
    auto it = tasks.find(co); if (it == tasks.end()) return;
    auto& st = it->second;
    st.status      = Status::Running;
    st.nextFrame   = true;
    st.pendingArgc = argc;
    st.hasPending  = true; 
    nextFrameTasks.push_back(co);
}

void LuaScheduler::AddScript(const std::shared_ptr<BaseScript>& script,
                             const std::string&                 name,
                             const std::string&                 source,
                             const GlobalBinder&                binder)
{
    if (!L_main || !script) return;

    lua_State* co = lua_newthread(L_main);
    if (!co) {
        LOGE("LuaScheduler: lua_newthread failed for '%s'", name.c_str());
        return;
    }

    lua_setthreaddata(co, script.get());
    luaL_sandboxthread(co);

    size_t bcSize = 0;
    lua_CompileOptions opts{};
    opts.optimizationLevel = 1;
    opts.debugLevel        = 1;

    static const char* kMutable[] = {
        "game", "workspace", "script", "shared", "plugin", nullptr
    };
    opts.mutableGlobals = kMutable;  // NULL-terminated

    char* bytecode = luau_compile(source.c_str(), source.size(), &opts, &bcSize);
    if (!bytecode || bcSize == 0) {
        LOGE("Luau Compile Error for '%s'", name.c_str());
        if (bytecode) free(bytecode);
        return;
    }

    const std::string chunkName = "@" + name;
    if (luau_load(co, chunkName.c_str(), bytecode, bcSize, 0) != 0) {
        LOGE("Luau Load Error for '%s': %s", name.c_str(), lua_tostring(co, -1));
        lua_pop(co, 1);
        free(bytecode);
        return;
    }
    free(bytecode);

    if (binder) binder(co, script.get());

    auto& st = state[script.get()];
    st.status       = Status::Running;
    st.co           = co;
    st.wakeTime     = 0.0;
    st.nextFrame    = false;
    st.lastResumeTime = GetTime();
    st.passDelta    = false;
    st.resumeDelta  = 0.0;
    st.firstResume  = true;

    ready.push_back(script);
}

void LuaScheduler::StopScript(BaseScript* s) {
    if (!s) return;

    // Drop state so the coroutine will never be resumed again.
    auto it = state.find(s);
    if (it != state.end()) state.erase(it);

    // Purge from ready/nextFrame queues.
    auto pred = [s](const std::shared_ptr<BaseScript>& sp){ return sp.get() == s; };
    ready.erase(std::remove_if(ready.begin(), ready.end(), pred), ready.end());
    nextFrameQ.erase(std::remove_if(nextFrameQ.begin(), nextFrameQ.end(), pred), nextFrameQ.end());
}

void LuaScheduler::SetWaitAbs(BaseScript* s, double wakeTimeAbs) {
    auto it = state.find(s);
    if (it == state.end()) return;
    auto& st = it->second;
    st.status    = Status::Waiting;
    st.nextFrame = false;
    st.wakeTime  = wakeTimeAbs;
}

void LuaScheduler::SetWaitNextFrame(BaseScript* s) {
    auto it = state.find(s);
    if (it == state.end()) return;
    auto& st = it->second;
    st.status    = Status::Waiting;
    st.nextFrame = true;
}

// ======= Task API =======

void LuaScheduler::ScheduleTaskNextFrame(lua_State* co, int registryRef, int initialArgc) {
    if (!L_main || !co) return;
    auto& st = tasks[co];
    st.status       = Status::Waiting;
    st.co           = co;
    st.registryRef  = registryRef;
    st.nextFrame    = true;
    st.wakeTime     = 0.0;
    st.lastResumeTime = GetTime();
    st.passDelta    = false;
    st.resumeDelta  = 0.0;
    st.firstResume  = true;
    st.pendingArgc  = initialArgc;
    nextFrameTasks.push_back(co);
}

void LuaScheduler::ScheduleTaskAt(lua_State* co, int registryRef, double wakeTimeAbs, int initialArgc) {
    if (!L_main || !co) return;
    auto& st = tasks[co];
    st.status       = Status::Waiting;
    st.co           = co;
    st.registryRef  = registryRef;
    st.nextFrame    = false;
    st.wakeTime     = wakeTimeAbs;
    st.lastResumeTime = GetTime();
    st.passDelta    = false;
    st.resumeDelta  = 0.0;
    st.firstResume  = true;
    st.pendingArgc  = initialArgc;
    sleepingTasks.push(co);
}

void LuaScheduler::SetTaskWaitAbs(lua_State* co, double wakeTimeAbs) {
    auto it = tasks.find(co);
    if (it == tasks.end()) return;
    auto& st = it->second;
    st.status    = Status::Waiting;
    st.nextFrame = false;
    st.wakeTime  = wakeTimeAbs;
}

void LuaScheduler::SetTaskWaitNextFrame(lua_State* co) {
    auto it = tasks.find(co);
    if (it == tasks.end()) return;
    auto& st = it->second;
    st.status    = Status::Waiting;
    st.nextFrame = true;
}

// ======= Step =======
void LuaScheduler::Step(double now, double /*dt*/) {
    if (!L_main) return;
    frameIndex++;

    lua_gc(L_main, LUA_GCSTEP, 200);

    // Wake timed script sleepers
    while (!sleepingByTime.empty()) {
        auto s = sleepingByTime.top();
        auto it = state.find(s.get());
        if (it == state.end()) { sleepingByTime.pop(); continue; }
        auto& st = it->second;

        if (st.status != Status::Waiting || st.nextFrame) {
            sleepingByTime.pop();
            ready.push_back(s);
            continue;
        }
        if (st.wakeTime > now) break;

        sleepingByTime.pop();
        st.status      = Status::Running;
        st.nextFrame   = false;
        st.passDelta   = true;
        st.resumeDelta = now - st.lastResumeTime;
        ready.push_back(std::move(s));
    }

    // Wake timed TASK sleepers
    while (!sleepingTasks.empty()) {
        lua_State* co = sleepingTasks.top();
        auto it = tasks.find(co);
        if (it == tasks.end()) { sleepingTasks.pop(); continue; }
        auto& st = it->second;

        if (st.status != Status::Waiting || st.nextFrame) {
            sleepingTasks.pop();
            readyTasks.push_back(co);
            continue;
        }
        if (st.wakeTime > now) break;

        sleepingTasks.pop();
        st.status      = Status::Running;
        st.nextFrame   = false;
        st.passDelta   = true;
        st.resumeDelta = now - st.lastResumeTime;
        readyTasks.push_back(co);
    }

    // Move next-frame scripts
    if (!nextFrameQ.empty()) {
        for (auto& s : nextFrameQ) {
            auto it = state.find(s.get());
            if (it == state.end()) continue;
            auto& st = it->second;
            st.status      = Status::Running;
            st.nextFrame   = false;
            st.passDelta   = true;
            st.resumeDelta = now - st.lastResumeTime;
            ready.push_back(std::move(s));
        }
        nextFrameQ.clear();
    }

    // Move next-frame tasks
    if (!nextFrameTasks.empty()) {
        for (auto co : nextFrameTasks) {
            auto it = tasks.find(co);
            if (it == tasks.end()) continue;
            auto& st = it->second;
            st.status      = Status::Running;
            st.nextFrame   = false;
            st.resumeDelta = now - st.lastResumeTime;
            readyTasks.push_back(co);
        }
        nextFrameTasks.clear();
    }

    const double deadline = (maxTimeBudgetSeconds > 0.0)
                          ? (now + maxTimeBudgetSeconds)
                          : std::numeric_limits<double>::infinity();

    int resumes = 0;
    double t = now;

    // Resume script coroutines
    while (!ready.empty()) {
        if (resumes >= maxResumesPerFrame) break;
        if (t >= deadline) break;

        std::shared_ptr<BaseScript> s = ready.front();
        ready.pop_front();

        auto it = state.find(s.get());
        if (it == state.end()) continue;
        auto& st = it->second;

        if (st.status != Status::Running) {
            nextFrameQ.push_back(s);
            continue;
        }

        int nargs = 0;
        if (st.hasPending) {
            nargs = st.pendingArgc;
            st.pendingArgc = 0;
            st.hasPending  = false;
            st.passDelta   = false;
        } else if (st.passDelta && !st.firstResume) {
            lua_pushnumber(st.co, st.resumeDelta);
            nargs = 1;
            st.passDelta = false;
        }

        resumes++;
        const int r = lua_resume(st.co, nullptr, nargs);
        st.firstResume = false;
        st.lastResumeTime = now;

        if (r == LUA_OK) {
            st.status = Status::Done;
        } else if (r == LUA_YIELD) {
            if (st.status == Status::Waiting) {
                if (st.nextFrame) nextFrameQ.push_back(s);
                else if (!std::isinf(st.wakeTime)) sleepingByTime.push(s); // only timed waits
                // else parked on event: do not enqueue
            } else {
                nextFrameQ.push_back(s);
            }
        } else {
            LOGE("Luau Runtime Error: %s", lua_tostring(st.co, -1));
            lua_pop(st.co, 1);
            st.status = Status::Error;
        }

        if ((resumes & 7) == 0) t = GetTime();
    }

    // Resume TASK coroutines
    while (!readyTasks.empty()) {
        if (resumes >= maxResumesPerFrame) break;
        if (t >= deadline) break;

        lua_State* co = readyTasks.front();
        readyTasks.pop_front();

        auto it = tasks.find(co);
        if (it == tasks.end()) continue;
        auto& st = it->second;

        if (st.status != Status::Running) {
            nextFrameTasks.push_back(co);
            continue;
        }

        int nargs = 0;
        if (st.hasPending) {
            nargs = st.pendingArgc;
            st.pendingArgc = 0;
            st.hasPending  = false;
            st.passDelta   = false;
        } else if (st.firstResume) {
            nargs = st.pendingArgc;
            st.pendingArgc = 0;
        } else if (st.passDelta) {
            lua_pushnumber(st.co, st.resumeDelta);
            nargs = 1;
            st.passDelta = false;
        }

        resumes++;
        const int r = lua_resume(st.co, nullptr, nargs);
        st.firstResume = false;
        st.lastResumeTime = now;

        if (r == LUA_OK) {
            // If this task carried a registry ref it is ephemeral and must be unref'd.
            if (st.registryRef != LUA_NOREF) {
                lua_unref(L_main, st.registryRef);
                st.registryRef = LUA_NOREF;
            } else {
                // Reusable per-listener coroutine: clear any values left on its stack.
                lua_settop(st.co, 0);
            }
            tasks.erase(it);
        } else if (r == LUA_YIELD) {
            if (st.status == Status::Waiting) {
                if (st.nextFrame) nextFrameTasks.push_back(co);
                else if (!std::isinf(st.wakeTime)) sleepingTasks.push(co); // only timed waits
                // else parked on event: do not enqueue
            } else {
                nextFrameTasks.push_back(co);
            }
        } else {
            LOGE("Luau Runtime Error (task): %s", lua_tostring(st.co, -1));
            lua_pop(st.co, 1);
            if (st.registryRef != LUA_NOREF) {
                lua_unref(L_main, st.registryRef);
                st.registryRef = LUA_NOREF;
            }
            tasks.erase(it);
        }

        if ((resumes & 7) == 0) t = GetTime();
    }
}
