#include "js_internal.h"

#include "../log.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace yoyo::js {
namespace {

struct Timer {
    uint64_t due_ms = 0;
    uint64_t interval_ms = 0;
    bool repeating = false;
    JSValue callback = JS_UNDEFINED;
    JSContext* ctx = nullptr; /* owning plugin context */
    std::string code;
};

std::recursive_mutex& timer_mutex() {
    static std::recursive_mutex mutex;
    return mutex;
}

std::unordered_map<uint64_t, Timer>& timers() {
    static std::unordered_map<uint64_t, Timer> table;
    return table;
}

uint64_t& next_timer_id() {
    static uint64_t id = 1;
    return id;
}

uint64_t now_ms() {
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

/*
 * setTimeout/setInterval accept either a function or a code string, matching
 * LLSE.  The timers are drained from Level::tick on the game thread, so any
 * packet the callback sends is safe.
 */
JSValue timer_create(JSContext* ctx, int argc, JSValueConst* argv, bool repeating) {
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "%s(callback, msec) requires two arguments",
                                 repeating ? "setInterval" : "setTimeout");
    }
    const bool is_function = JS_IsFunction(ctx, argv[0]);
    std::string code;
    if (!is_function) {
        if (!JS_IsString(argv[0])) {
            return JS_ThrowTypeError(ctx, "callback must be a function or a code string");
        }
        const char* text = JS_ToCString(ctx, argv[0]);
        code = text ? text : "";
        if (text) {
            JS_FreeCString(ctx, text);
        }
    }
    int64_t delay = 0;
    if (JS_ToInt64(ctx, &delay, argv[1]) < 0) {
        return JS_EXCEPTION;
    }
    if (delay < 0) {
        delay = 0;
    }

    std::lock_guard lock(timer_mutex());
    const uint64_t id = next_timer_id()++;
    Timer timer;
    timer.due_ms = now_ms() + static_cast<uint64_t>(delay);
    timer.interval_ms = static_cast<uint64_t>(delay);
    timer.repeating = repeating;
    timer.code = std::move(code);
    timer.callback = is_function ? JS_DupValue(ctx, argv[0]) : JS_UNDEFINED;
    timer.ctx = ctx;
    timers()[id] = std::move(timer);
    return JS_NewInt64(ctx, static_cast<int64_t>(id));
}

JSValue timer_set_timeout(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return timer_create(ctx, argc, argv, false);
}

JSValue timer_set_interval(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return timer_create(ctx, argc, argv, true);
}

JSValue timer_clear(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "clearInterval(id) requires an id");
    }
    int64_t id = 0;
    if (JS_ToInt64(ctx, &id, argv[0]) < 0) {
        return JS_EXCEPTION;
    }
    bool removed = false;
    {
        std::lock_guard lock(timer_mutex());
        auto it = timers().find(static_cast<uint64_t>(id));
        if (it != timers().end()) {
            if (it->second.ctx != nullptr && JS_IsFunction(it->second.ctx, it->second.callback)) {
                JS_FreeValue(it->second.ctx, it->second.callback);
            }
            timers().erase(it);
            removed = true;
        }
    }
    return JS_NewBool(ctx, removed);
}

} // namespace

void register_timers(JSContext* ctx) {
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "setTimeout",
                      JS_NewCFunction(ctx, timer_set_timeout, "setTimeout", 2));
    JS_SetPropertyStr(ctx, global, "setInterval",
                      JS_NewCFunction(ctx, timer_set_interval, "setInterval", 2));
    JS_SetPropertyStr(ctx, global, "clearTimeout", JS_NewCFunction(ctx, timer_clear, "clearTimeout", 1));
    JS_SetPropertyStr(ctx, global, "clearInterval",
                      JS_NewCFunction(ctx, timer_clear, "clearInterval", 1));
    JS_FreeValue(ctx, global);
}

void run_timers() {
    if (g_context == nullptr) {
        return;
    }
    struct Fire {
        uint64_t id = 0;
        JSValue callback = JS_UNDEFINED;
        JSContext* ctx = nullptr;
        std::string code;
    };
    std::vector<Fire> due;
    std::vector<uint64_t> finished;
    const uint64_t now = now_ms();
    {
        std::lock_guard lock(timer_mutex());
        for (auto& [id, timer] : timers()) {
            if (timer.due_ms > now) {
                continue;
            }
            due.push_back({id, timer.ctx != nullptr ? JS_DupValue(timer.ctx, timer.callback) : JS_UNDEFINED,
                           timer.ctx, timer.code});
            if (timer.repeating) {
                timer.due_ms = now + std::max<uint64_t>(timer.interval_ms, 1);
            } else {
                finished.push_back(id);
            }
        }
        /* Erasing while iterating would invalidate the range, so one-shot
         * timers are removed after the walk. */
        for (const uint64_t id : finished) {
            auto it = timers().find(id);
            if (it != timers().end()) {
                if (it->second.ctx != nullptr && JS_IsFunction(it->second.ctx, it->second.callback)) {
                    JS_FreeValue(it->second.ctx, it->second.callback);
                }
                timers().erase(it);
            }
        }
    }
    for (auto& fire : due) {
        JSContext* ctx = fire.ctx != nullptr ? fire.ctx : g_context;
        if (ctx == nullptr) {
            continue;
        }
        if (JS_IsFunction(ctx, fire.callback)) {
            
            JSValue result = JS_Call(ctx, fire.callback, JS_UNDEFINED, 0, nullptr);
            if (JS_IsException(result)) {
                log_exception(ctx);
            }
            JS_FreeValue(ctx, result);
        } else if (!fire.code.empty()) {
            JSValue result = JS_Eval(ctx, fire.code.data(), fire.code.size(), "<timer>",
                                     JS_EVAL_TYPE_GLOBAL);
            if (JS_IsException(result)) {
                log_exception(ctx);
            }
            JS_FreeValue(ctx, result);
        }
        JS_FreeValue(ctx, fire.callback);
    }
}

void clear_timers() {
    std::lock_guard lock(timer_mutex());
    for (auto& [id, timer] : timers()) {
        if (timer.ctx != nullptr && JS_IsFunction(timer.ctx, timer.callback)) {
            JS_FreeValue(timer.ctx, timer.callback);
        }
    }
    timers().clear();
}

} // namespace yoyo::js
