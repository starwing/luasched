#define LUA_LIB
#include "lsched.h"
#include <lauxlib.h>


#include <assert.h>


#define LSC_MAIN_STATE 0x15CEA125
#define LSC_TASK_BOX   0x7A58B085


/* main state maintains */

static void get_taskbox(lua_State *L) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, (void*)LSC_TASK_BOX);
    if (!lua_isnil(L, -1)) return;
    lua_newtable(L); /* no weak? */
    lua_pushvalue(L, -1);
    lua_rawsetp(L, LUA_REGISTRYINDEX, (void*)LSC_TASK_BOX);
}

lsc_State *lsc_state(lua_State *L) {
    lsc_State *s;
    lua_rawgetp(L, LUA_REGISTRYINDEX, (void*)LSC_MAIN_STATE);
    s = (lsc_State*)lua_touserdata(L, -1);
    if (s != NULL) { 
        lua_pop(L, 1);
        return s;
    }
    s = (lsc_State*)lua_newuserdata(L, sizeof(lsc_State));
    s->main = NULL;
    s->ud = NULL;
    s->poll = NULL;
    lua_rawsetp(L, LUA_REGISTRYINDEX, (void*)LSC_MAIN_STATE);
    lsc_maintask(L); /* init main task */
    lsc_initsignal(&s->running);
    lsc_initsignal(&s->ready);
    lsc_initsignal(&s->error);
    return s;
}

lsc_Task *lsc_current(lua_State *L) {
    lsc_Task *t;
    get_taskbox(L);
    lua_pushthread(L);
    lua_rawget(L, -2);
    t = (lsc_Task*)lua_touserdata(L, -1);
    lua_pop(L, 2);
    return t;
}

lsc_Task *lsc_maintask(lua_State *L) {
    lua_State *mL;
    lsc_State *s = lsc_state(L);
    if (s->main) return s->main;
    lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
    mL = lua_tothread(L, -1);
    assert(mL != NULL);
    s->main = lsc_newtask(L, mL, 0);
    lua_pop(L, 2);
    /* main task is always treats as running */
    lsc_wait(s->main, &s->running, 0);
    return s->main;
}

void lsc_setpoll(lsc_State *s, lsc_Poll *poll, void *ud) {
    s->poll = poll;
    s->ud = ud;
}

int lsc_once(lsc_State *s, lua_State *from) {
    lsc_Task *t;
    lsc_Signal ready_again;
    lsc_initsignal(&ready_again);
    while ((t = lsc_next(&s->ready, NULL)) != NULL) {
        lsc_wakeup(t, from, -1);
        if (t->waitat == &s->ready)
            lsc_wait(t, &ready_again, 0);
    }
    s->ready = ready_again;
    if (s->poll != NULL)
        return s->poll(s, from, s->ud);
    return lsc_next(&s->ready, NULL) != NULL;
}

void lsc_loop(lsc_State *s, lua_State *from) {
    while (lsc_once(s, from))
        ;
}


/* object type maintains */

lsc_Task *lsc_newtask(lua_State *L, lua_State *coro, size_t extrasz) {
    lsc_Task *t = (lsc_Task*)lua_newuserdata(L, sizeof(lsc_Task) + extrasz);
    luaL_setmetatable(L, "sched.signal");
    t->S = lsc_state(L);
    t->L = coro;
    t->waitat = NULL;
    lsc_initsignal(&t->head);
    lsc_initsignal(&t->joined);
    return t;
}

static int copy_stack(lua_State *from, lua_State *to, int n) {
    int i;
    luaL_checkstack(from, n, "too many args");
    for (i = 0; i < n; ++i)
        lua_pushvalue(from, -n);
    lua_xmove(from, to, n);
    return n;
}

static void wakeup_joins(lua_State *L, lsc_Signal *s, int nrets) {
    lsc_Task *t;
    int stat = lua_status(L);
    if (nrets < 0)
        nrets = lua_gettop(L);
    while ((t = lsc_next(s, NULL)) != NULL) {
        switch (stat) {
        case LUA_YIELD:
            lua_pushnil(L);
            lua_pushstring(L, "task deleted");
            copy_stack(L, t->L, nrets);
            lsc_wakeup(t, L, nrets + 2);
            break;
        case LUA_OK:
            lua_pushboolean(L, 1);
            copy_stack(L, t->L, nrets);
            lsc_wakeup(t, L, nrets + 1);
            break;
        default:
            lua_pushnil(L);
            copy_stack(L, t->L, 1);
            lsc_wakeup(t, L, 2);
            break;
        }
    }
}

int lsc_deletetask(lsc_Task *t, int nrets) {
    lua_State *L;
    if (t->L == NULL || lsc_status(t) == lsc_Running)
        return 0;
    /* avoid task run */
    lsc_hold(t, 0);
    /* remove it from task box */
    get_taskbox(t->L);
    lua_pushthread(t->L);
    lua_pushnil(t->L);
    lua_rawset(t->L, -3);
    lua_pop(t->L, 1);
    /* cleanup binding coroutine and wake up joined tasks*/
    L = t->L;
    t->L = NULL;
    wakeup_joins(L, &t->joined, nrets);
    return 1;
}

lsc_Signal *lsc_newsignal(lua_State *L, size_t extrasz) {
    lsc_Signal *s =
        (lsc_Signal*)lua_newuserdata(L, sizeof(lsc_Signal) + extrasz);
    luaL_setmetatable(L, "sched.signal");
    lsc_initsignal(s);
    return s;
}

void lsc_initsignal(lsc_Signal *s) {
    s->prev = s->next = s;
}

void lsc_freesignal(lsc_Signal *s, lua_State *from) {
    lsc_Task *t = NULL;
    while ((t = lsc_next(s, NULL)) != NULL) {
        lua_pushnil(t->L);
        lua_pushstring(t->L, "signal deleted");
        lsc_wakeup(t, from, 2);
        if (t->waitat == s)
            lsc_error(t, "waiting on deleted signal");
    }
}

lsc_Task *lsc_checktask(lua_State *L, int idx) {
    return (lsc_Task*)luaL_checkudata(L, idx, "sched.task");
}

lsc_Task *lsc_testtask(lua_State *L, int idx) {
    return (lsc_Task*)luaL_testudata(L, idx, "sched.task");
}

lsc_Signal *lsc_checksignal(lua_State *L, int idx) {
    return (lsc_Signal*)luaL_checkudata(L, idx, "sched.signal");
}

lsc_Signal *checktask(lua_State *L, int idx) {
    return (lsc_Signal*)luaL_testudata(L, idx, "sched.signal");
}

int lsc_pushcontext(lua_State *L, lsc_Task *t) {
    if (t->L == NULL || lsc_status(t) == lsc_Running)
        return 0;
    return copy_stack(t->L, L, lua_gettop(t->L));
}


/* signal maintains */

static void append(lsc_Signal *head, lsc_Signal *to) {
    head->prev = to->prev;                                                    \
    head->prev->next = head;                                                      \
    head->next = to;                                                            \
    to->prev = head;                                                            \
}

static void merge(lsc_Signal *head, lsc_Signal *to) {
    to->prev->next = head->next;                                              \
    head->next->prev = to->prev;                                              \
    to->prev = head->prev;                                                    \
    to->prev->next = to;                                                      \
}

static void remove_self(lsc_Signal *head) {
    head->prev->next = head->next;
    head->next->prev = head->prev;
}

lsc_Task *lsc_next(lsc_Signal *s, lsc_Task *curr) {
    if (curr == NULL)
        return s->next == s ? NULL : (lsc_Task*)s->next;
    return curr->head.next == s ?
        NULL : (lsc_Task*)curr->head.next;
}


/* task maintains */

const char *lsc_collect(lua_State *L, lsc_State *s, lsc_Collect *f, void *ud) {
    lsc_Signal deleted;
    luaL_Buffer b;
    lsc_Task *t;
    luaL_buffinit(L, &b);
    lsc_initsignal(&deleted);
    while ((t = lsc_next(&s->error, NULL)) != NULL) {
        lua_pushfstring(L, "task(%p): ", t);
        luaL_addvalue(&b);
        if (f != NULL)
            luaL_addstring(&b, f(t, L, &deleted, ud));
        else {
            assert(lua_isstring(t->L, -1));
            lua_xmove(t->L, L, 1);
            luaL_addvalue(&b);
            lsc_wait(t, &deleted, 0);
        }
        luaL_addchar(&b, '\n');
    }
    luaL_pushresult(&b);
    return lua_tostring(L, -1);
}

lsc_Status lsc_status(lsc_Task *t) {
    if (t->waitat == NULL)
        return lsc_Hold;
    else if (t->waitat == &t->S->running)
        return lsc_Running;
    else if (t->waitat == &t->S->ready)
        return lsc_Ready;
    else if (t->waitat == &t->S->error)
        return lsc_Error;
    return lsc_Waiting;
}

int lsc_error(lsc_Task *t, const char *errmsg) {
    if (t->L == NULL) return 0;
    if (lsc_status(t) == lsc_Running)
        return luaL_error(t->L, errmsg);
    lua_pushstring(t->L, errmsg); /* context */
    return lsc_wait(t, &t->S->error, 1);
}

int lsc_wait(lsc_Task *t, lsc_Signal *s, int nctx) {
    int is_running = lsc_status(t) == lsc_Running;
    remove_self(&t->head);
    if (s == NULL)
        lsc_initsignal(&t->head);
    else
        append(&t->head, s);
    t->waitat = s;
    if (is_running)
        return lua_yield(t->L, nctx);
    return 0;
}

int lsc_ready(lsc_Task *t, int nctx) {
    return lsc_wait(t, &t->S->ready, nctx);
}

int lsc_hold(lsc_Task *t, int nctx) {
    return lsc_wait(t, NULL, nctx);
}

int lsc_join(lsc_Task *t, lsc_Task *jointo, int nctx) {
    return lsc_wait(t, &jointo->joined, nctx);
}

static void adjust_stack(lua_State *L, int top, int nargs) {
    /* may faster than lua_remove? */
    if (top > nargs) {
        int i, removed = top - nargs;
        for (i = 1; i <= removed; ++i) {
            lua_pushvalue(L, removed+i);
            lua_replace(L, i);
        }
        lua_settop(L, nargs);
    }
}

int lsc_wakeup(lsc_Task *t, lua_State *from, int nargs) {
    int res, top;
    if (t->L == NULL || lsc_status(t) == lsc_Running)
        return 0;
    if (t->waitat != NULL)
        remove_self(&t->head);
    append(&t->head, &t->S->running);
    t->waitat = &t->S->running;
    res = lua_status(t->L);
    top = lua_gettop(t->L);
    if (nargs < 0) {
        nargs = top;
        if (res == LUA_OK)
            --nargs; /* first run */
    }
    /* adjust stack to contain args only */
    if (res != LUA_YIELD)
        adjust_stack(t->L, nargs, top);
    res = lua_resume(t->L, from, nargs);
    if (res == LUA_OK || res != LUA_YIELD) {
        lsc_deletetask(t, -1);
        return res == LUA_OK;
    }
    return 1;
}

int lsc_emit(lsc_Signal *s, lua_State *from, int nargs) {
    int n = 0;
    lsc_Task *t = NULL;
    lsc_Signal wait_again;
    lsc_initsignal(&wait_again);
    while ((t = lsc_next(s, t)) != NULL) {
        assert(lsc_status(t) != lsc_Running);
        lsc_wakeup(t, from, nargs);
        if (t->waitat == s)
            lsc_wait(t, &wait_again, 0);
        ++n;
    }
    merge(&wait_again, s);
    return n;
}


/* task module interface */

static int Ltask_new(lua_State *L) {
    lua_State *coro;
    int top = lua_gettop(L);
    luaL_checktype(L, 1, LUA_TFUNCTION);
    coro = lua_newthread(L);
    lsc_hold(lsc_newtask(L, coro, 0), 0);
    lua_insert(L, 1);
    lua_xmove(L, coro, top);
    return 1;
}

static int Ltask_delete(lua_State *L) {
    lsc_Task *t = lsc_testtask(L, 1);
    if (t != NULL)
        lsc_deletetask(t, -1);
    return 0;
}

static lsc_Task *default_task(lua_State *L, int *parg) {
    int arg = 2;
    lsc_Task *t = lsc_testtask(L, 1);
    if (t == NULL) {
        arg = 1; 
        t = lsc_current(L);
    }
    if (t == NULL)
        luaL_error(L, "only callable on a task");
    if (parg) *parg = arg;
    return t;
}

static int Ltask_wait(lua_State *L) {
    int arg, top = lua_gettop(L);
    lsc_Task *t = default_task(L, &arg);
    lsc_Signal *s = lsc_checksignal(L, arg);
    lsc_wait(t, s, top - arg);
    lua_settop(L, 1);
    return 1;
}

static int Ltask_ready(lua_State *L) {
    int arg, top = lua_gettop(L);
    lsc_Task *t = default_task(L, &arg);
    lsc_ready(t, top - arg);
    lua_settop(L, 1);
    return 1;
}

static int Ltask_hold(lua_State *L) {
    int arg, top = lua_gettop(L);
    lsc_Task *t = default_task(L, &arg);
    lsc_hold(t, top - arg);
    lua_settop(L, 1);
    return 1;
}

static int Ltask_wakeup(lua_State *L) {
    int top = lua_gettop(L);
    lsc_Task *t = lsc_checktask(L, 1);
    lua_xmove(L, t->L, top-1);
    /* push result of task? */
    lua_pushboolean(L, lsc_wakeup(t, L, top-1));
    return 1;
}

static int Ltask_context(lua_State *L) {
    int arg, top = lua_gettop(L);
    lsc_Task *t = default_task(L, &arg);
    if (arg <= top) { /* set context */
        if (lsc_status(t) == lsc_Running)
            return 0;
        lua_settop(t->L, 0);
        lua_xmove(L, t->L, top - arg + 1);
        return 0;
    }
    return lsc_pushcontext(L, t);
}

static int Ltask_join(lua_State *L) {
    lsc_Task *t, *jointo;
    int top = lua_gettop(L);
    t = lsc_checktask(L, 1);
    if (top == 1) {
        jointo = t;
        t = lsc_current(L);
    }
    else
        jointo = lsc_checktask(L, 2);
    lua_pushboolean(L, lsc_join(t, jointo, -1));
    return 1;
}

static int Ltask_status(lua_State *L) {
    lsc_Task *t = default_task(L, NULL);
    const char *s = NULL;
    switch (lsc_status(t)) {
    case lsc_Error:    s = "error";   break;
    case lsc_Hold:     s = "hold";    break;
    case lsc_Ready:    s = "ready";   break;
    case lsc_Running:  s = "running"; break;
    case lsc_Waiting:  s = "waiting"; break;
    default:
        return 0;
    }
    lua_pushstring(L, s);
    return 1;
}

LUALIB_API int luaopen_sched_task(lua_State *L) {
    luaL_Reg libs[] = {
#define ENTRY(name) { #name, Ltask_##name }
        ENTRY(new),
        ENTRY(delete),
        ENTRY(wait),
        ENTRY(ready),
        ENTRY(hold),
        ENTRY(wakeup),
        ENTRY(context),
        ENTRY(join),
        ENTRY(status),
#undef  ENTRY
        { NULL, NULL }
    };
    if (luaL_newmetatable(L, "sched.task"))
        luaL_setfuncs(L, libs, 0);
    return 1;
}


/* signal module interface */

static int Lsignal_new(lua_State *L) {
    return 0;
}

static int Lsignal_delete(lua_State *L) {
    return 0;
}

static int Lsignal_emit(lua_State *L) {
    lsc_Task *t = NULL;
    lsc_Signal *s = lsc_checksignal(L, 1);
    int top = lua_gettop(L) - 1;
    while ((t = lsc_next(s, t)) != NULL)
        copy_stack(L, t->L, top);
    lua_pushboolean(L, lsc_emit(s, L, top));
    return 1;
}

static int Lsignal_ready(lua_State *L) {
    lsc_Task *t = NULL;
    lsc_Signal *s = lsc_checksignal(L, 1);
    int top = lua_gettop(L) - 1;
    while ((t = lsc_next(s, t)) != NULL) {
        copy_stack(L, t->L, top);
        lsc_ready(t, top);
    }
    return 1;
}

static int Lsignal_one(lua_State *L) {
    lsc_Task *t;
    lsc_Signal *s = lsc_checksignal(L, 1);
    int top = lua_gettop(L) - 1;
    if ((t = lsc_next(s, NULL)) != NULL) {
        copy_stack(L, t->L, top);
        lsc_wakeup(t, L, top);
    }
    return 1;
}

static int Lsignal_filter(lua_State *L) {
    return 0;
}

static int Lsignal_iter(lua_State *L) {
    return 0;
}

static int Lsignal_index(lua_State *L) {
    return 0;
}

LUALIB_API int luaopen_sched_signal(lua_State *L) {
    luaL_Reg libs[] = {
        { "__gc", Lsignal_delete },
#define ENTRY(name) { #name, Lsignal_##name }
        ENTRY(new),
        ENTRY(delete),
        ENTRY(emit),
        ENTRY(ready),
        ENTRY(one),
        ENTRY(filter),
        ENTRY(iter),
        ENTRY(index),
#undef  ENTRY
        { NULL, NULL }
    };
    if (luaL_newmetatable(L, "sched.signal"))
        luaL_setfuncs(L, libs, 0);
    return 1;
}


/* global module interface */

static int Lsetpoll(lua_State *L) {
    return 0;
}

static int Lonce(lua_State *L) {
    return 0;
}

static int Lloop(lua_State *L) {
    return 0;
}

static int Lerrors(lua_State *L) {
    return 0;
}

static int Lcollect(lua_State *L) {
    return 0;
}

LUALIB_API int luaopen_sched(lua_State *L) {
    luaL_Reg libs[] = {
#define ENTRY(name) { #name, L##name }
        ENTRY(setpoll),
        ENTRY(once),
        ENTRY(loop),
        ENTRY(errors),
        ENTRY(collect),
#undef  ENTRY
        { NULL, NULL }
    };
    luaL_newlib(L, libs);
    return 1;
}
/* cc: flags+='-s -O2 -mdll -DLUA_BUILD_AS_DLL'
 * cc: libs+='-llua52' output='sched.dll' */
