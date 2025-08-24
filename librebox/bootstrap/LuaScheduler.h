// ================== bootstrap/LuaScheduler.h ==================
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

// Luau
#include "lua.h"
#include "lualib.h"
#include "luacode.h"

struct BaseScript;  // opaque to the scheduler

class LuaScheduler {
public:
    using GlobalBinder = std::function<void(lua_State* co, BaseScript* self)>;

    enum class Status { New, Running, Waiting, Done, Error };

    LuaScheduler();
    ~LuaScheduler();

    lua_State* GetMainState() const { return L_main; }

    void AddScript(const std::shared_ptr<BaseScript>& script,
                   const std::string&                 name,
                   const std::string&                 source,
                   const GlobalBinder&                binder);

    void StopScript(BaseScript* s);
    void StopScript(const std::shared_ptr<BaseScript>& s) { StopScript(s.get()); }

private:
    void StopScriptImmediate(BaseScript* s);

public:

    void Step(double now, double dt = 0.0);

    // Script waits
    void SetWaitAbs(BaseScript* s, double wakeTimeAbs);
    void SetWaitNextFrame(BaseScript* s);
    void SetWaitEvent(BaseScript* s);

    // Task API (thread-based, not tied to BaseScript)
    void ScheduleTaskNextFrame(lua_State* co, int registryRef, int initialArgc);
    void ScheduleTaskAt(lua_State* co, int registryRef, double wakeTimeAbs, int initialArgc);
    void SetTaskWaitAbs(lua_State* co, double wakeTimeAbs);
    void SetTaskWaitNextFrame(lua_State* co);
    void SetTaskWaitEvent(lua_State* co);
    void WakeTaskNextFrame(lua_State* co, int argc);

    // resume script with 'argc' args already pushed on target coroutine
    void ResumeScriptNextFrame(BaseScript* s, int argc);

    // For RTScriptSignal::Wait() on scripts
    lua_State* GetScriptThread(BaseScript* s);

    int    maxResumesPerFrame   = 4096;
    double maxTimeBudgetSeconds = 0.010;

    uint64_t frameIndex = 0;

    LuaScheduler(const LuaScheduler&)            = delete;
    LuaScheduler& operator=(const LuaScheduler&) = delete;

    // Query whether a task coroutine is currently in the scheduler's active/tasks map.
    bool IsTaskActive(lua_State* co) const { return tasks.find(co) != tasks.end(); }

private:
    struct ScriptState {
        Status     status    = Status::New;
        lua_State* co        = nullptr;
        double     wakeTime  = 0.0;
        bool       nextFrame = false;
        // timing and resume
        double     lastResumeTime = 0.0;
        bool       passDelta      = false;
        double     resumeDelta    = 0.0;
        bool       firstResume    = true;
        // pending arguments
        bool       hasPending     = false;
        int        pendingArgc    = 0;
    };

    struct TaskState {
        Status     status      = Status::Running;
        lua_State* co          = nullptr;
        int        registryRef = LUA_NOREF; // keeps thread alive (ephemeral tasks)
        double     wakeTime    = 0.0;
        bool       nextFrame   = false;
        // timing and resume
        double     lastResumeTime = 0.0;
        bool       passDelta      = false;
        double     resumeDelta    = 0.0;
        bool       firstResume    = true;
        // pending arguments
        bool       hasPending     = false;
        int        pendingArgc    = 0;
    };

    lua_State* L_main = nullptr;

    // BaseScript coroutines
    std::unordered_map<BaseScript*, ScriptState> state;
    std::deque<std::shared_ptr<BaseScript>> ready;
    std::deque<std::shared_ptr<BaseScript>> nextFrameQ;
    
    // Deferred cleanup to avoid iterator invalidation during execution
    std::vector<BaseScript*> scriptsToStop;
    bool inStepExecution = false;

    struct TimeCmp {
        const std::unordered_map<BaseScript*, ScriptState>* st = nullptr;
        bool operator()(const std::shared_ptr<BaseScript>& a,
                        const std::shared_ptr<BaseScript>& b) const {
            const auto ia = st->find(a.get());
            const auto ib = st->find(b.get());
            const double ta = (ia == st->end()) ? 0.0 : ia->second.wakeTime;
            const double tb = (ib == st->end()) ? 0.0 : ib->second.wakeTime;
            return ta > tb; // min-heap
        }
    };

    std::priority_queue<
        std::shared_ptr<BaseScript>,
        std::vector<std::shared_ptr<BaseScript>>,
        TimeCmp
    > sleepingByTime;

    // Task coroutines (plain Luau threads)
    std::unordered_map<lua_State*, TaskState> tasks;
    std::deque<lua_State*> readyTasks;
    std::deque<lua_State*> nextFrameTasks;

    struct TaskTimeCmp {
        const std::unordered_map<lua_State*, TaskState>* st = nullptr;
        bool operator()(lua_State* a, lua_State* b) const {
            auto ia = st->find(a), ib = st->find(b);
            const double ta = (ia == st->end()) ? 0.0 : ia->second.wakeTime;
            const double tb = (ib == st->end()) ? 0.0 : ib->second.wakeTime;
            return ta > tb; // min-heap
        }
    };

    std::priority_queue<
        lua_State*,
        std::vector<lua_State*>,
        TaskTimeCmp
    > sleepingTasks;
};
