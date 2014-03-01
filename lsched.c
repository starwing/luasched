#define LUA_LIB
#include "lsched.h"
#include <lauxlib.h>


#include <assert.h>
#include <string.h>


#define LSC_MAIN_STATE 0x15CEA125
#define LSC_TASK_BOX   0x7A58B085


static void get_taskbox(lua_State *L) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, (void*)LSC_TASK_BOX);
    if (lua_istable(L, -1)) return;
    lua_pop(L, 1);
    lua_newtable(L); /* no weak? */
    lua_pushvalue(L, -1);
    lua_rawsetp(L, LUA_REGISTRYINDEX, (void*)LSC_TASK_BOX);
}

lsc_State *lsc_state(lua_State *L) {
    lsc_State *s;
    lua_rawgetp(L, LUA_REGISTRYINDEX, (void*)LSC_MAIN_STATE);
    s = (lsc_State*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    if (s != NULL) return s;
    s = (lsc_State*)lua_newuserdata(L, sizeof(lsc_State));
    s->main = NULL;
    s->ud = NULL;
    s->poll = NULL;
    lsc_initsignal(&s->running);
    lsc_initsignal(&s->ready);
    lsc_initsignal(&s->error);
    lua_rawsetp(L, LUA_REGISTRYINDEX, (void*)LSC_MAIN_STATE);
    lsc_maintask(L); /* init main task */
    return s;
}


/* signal maintains */

static void queue_removeself(lsc_Signal *head) {
    if (head->prev && head->prev != head) {
        head->prev->next = head->next;
        head->next->prev = head->prev;
    }
}

static void queue_append(lsc_Signal *head, lsc_Signal *to) {
    queue_removeself(head);
    if (to->prev) {
        head->prev = to->prev;
        head->prev->next = head;
        head->next = to;
        to->prev = head;
    }
}

static void queue_merge(lsc_Signal *head, lsc_Signal *to) {
    if (to->prev) {
        to->prev->next = head->next;
        head->next->prev = to->prev;
        to->prev = head->prev;
        to->prev->next = to;
    }
}

lsc_Signal *lsc_newsignal(lua_State *L, size_t extrasz) {
    lsc_Signal *s =
        (lsc_Signal*)lua_newuserdata(L, sizeof(lsc_Signal) + extrasz);
    luaL_setmetatable(L, "sched.signal");
    lsc_initsignal(s);
    return s;
}

void lsc_deletesignal(lsc_Signal *s, lua_State *from) {
    lsc_Signal removed;
    lsc_Task *t = NULL;
    lsc_initsignal(&removed);
    queue_append(&removed, s);
    queue_removeself(s);
    /* invalid signal */
    s->prev = s->next = NULL;
    while ((t = lsc_next(&removed, NULL)) != NULL) {
        lua_pushnil(t->L);
        lua_pushstring(t->L, "signal deleted");
        lsc_wakeup(t, from, 2);
        assert(t->waitat != &removed);
    }
}

void *lsc_signalpointer(lsc_Signal *s) {
    return (void*)(s + 1);
}

void lsc_initsignal(lsc_Signal *s) {
    s->prev = s->next = s;
}

lsc_Task *lsc_next(lsc_Signal *s, lsc_Task *curr) {
    if (curr == NULL)
        return s->next == s ? NULL : (lsc_Task*)s->next;
    return curr->head.next == s ?
        NULL : (lsc_Task*)curr->head.next;
}

size_t lsc_count(lsc_Signal *s) {
    lsc_Task *t = NULL;
    size_t count = 0;
    while ((t = lsc_next(s, t)) != NULL)
        ++count;
    return count;
}

lsc_Task *lsc_index(lsc_Signal *s, int idx) {
    if (s->prev == NULL) /* handle deleted signal */
        return NULL;
    if (idx >= 0) {
        lsc_Task *t = NULL;
        while (idx >= 0 && (t = lsc_next(s, t)) != NULL)
            --idx;
        return t;
    }
    else {
        lsc_Signal *head = s;
        while (idx < 0 && head->prev != s) {
            head = head->prev;
            ++idx;
        }
        if (idx < 0)
            return NULL;
        return (lsc_Task*)head;
    }
}

/* task maintains */

static void register_task(lua_State *L, lsc_Task *t) {
    /* stack: task object */
    get_taskbox(L);
    lua_pushthread(t->L);
    if (t->L != L)
        lua_xmove(t->L, L, 1); /* push thread */
    lua_pushvalue(L, -3); /* task object */
    lua_rawset(L, -3);
    lua_pop(L, 1);
}

static void unregister_task(lsc_Task *t) {
    get_taskbox(t->L);
    lua_pushthread(t->L);
    lua_pushnil(t->L);
    lua_rawset(t->L, -3);
    lua_pop(t->L, 1);
}

lsc_Task *lsc_newtask(lua_State *L, lua_State *coro, size_t extrasz) {
    lsc_Task *t = (lsc_Task*)lua_newuserdata(L, sizeof(lsc_Task) + extrasz);
    luaL_setmetatable(L, "sched.task");
    t->S = lsc_state(L);
    t->L = coro;
    t->waitat = NULL;
    lsc_initsignal(&t->head);
    lsc_initsignal(&t->joined);
    register_task(L, t);
    return t;
}

static void wakeup_joins(lsc_Task *t, lua_State *from) {
    int nrets = 2;
    int stat = lua_status(t->L);
    lsc_Signal *s = &t->joined;
    /* mark as zombie */
    t->waitat = NULL;
    t->head.prev = t->head.next = NULL;
    /* wakeup joined tasks */
    if (from == NULL)
        from = t->L;
    while ((t = lsc_next(s, NULL)) != NULL) {
        switch (stat) {
        case LUA_YIELD:
            lua_pushnil(from);
            lua_pushstring(from, "task deleted");
            nrets += lsc_getcontext(from, t);
            break;
        case LUA_OK:
            lua_pushboolean(from, 1);
            nrets += lsc_getcontext(from, t) - 1;
            break;
        default:
            lua_pushnil(from);
            lua_pushvalue(t->L, 1);
            lua_xmove(t->L, from, 1);
            break;
        }
        lsc_wakeup(t, from, nrets);
    }
}


int lsc_deletetask(lsc_Task *t, lua_State *from) {
    lsc_Status s = lsc_status(t);
    if (s == lsc_Dead || s == lsc_Running)
        return 0;
    /* avoid task run */
    lsc_hold(t, 0);
    /* remove it from task box */
    unregister_task(t);
    /* wake up joined tasks */
    wakeup_joins(t, from);
    /* mark task as dead */
    t->L = NULL;
    return 1;
}

static int copy_stack(lua_State *from, lua_State *to, int n) {
    int i;
    luaL_checkstack(from, n, "too many args");
    for (i = 0; i < n; ++i)
        lua_pushvalue(from, -n);
    if (from != to)
        lua_xmove(from, to, n);
    return n;
}

int lsc_setcontext(lua_State *L, lsc_Task *t, int nargs) {
    lsc_Status s = lsc_status(t);
    if (s == lsc_Dead || s == lsc_Running)
        return 0;
    lua_settop(t->L, 0);
    return copy_stack(L, t->L, nargs);
}

int lsc_getcontext(lua_State *L, lsc_Task *t) {
    lsc_Status s = lsc_status(t);
    if (s == lsc_Dead || s == lsc_Running)
        return 0;
    if (s == lsc_Finished) {
        assert(lua_isstring(t->L, -1));
        lua_pushvalue(t->L, -1);
        lua_xmove(t->L, L, 1);
        return 1;
    }
    return copy_stack(t->L, L, lua_gettop(t->L));
}

lsc_Status lsc_status(lsc_Task *t) {
    if (t->L == NULL)
        return lsc_Dead;
    else if (t->head.prev == NULL)
        return lsc_Finished;
    else if (t->waitat == &t->S->running)
        return lsc_Running;
    else if (t->waitat == &t->S->ready)
        return lsc_Ready;
    else if (t->waitat == &t->S->error)
        return lsc_Error;
    else if (t->waitat == NULL)
        return lsc_Hold;
    return lsc_Waitting;
}

int lsc_error(lsc_Task *t, const char *errmsg) {
    lsc_Status s = lsc_status(t);
    if (s == lsc_Dead || s == lsc_Finished) return 0;
    if (s == lsc_Running) return luaL_error(t->L, errmsg);
    lua_settop(t->L, 0);
    lua_pushstring(t->L, errmsg); /* context */
    queue_append(&t->head, &t->S->error);
    return 1;
}

int lsc_wait(lsc_Task *t, lsc_Signal *s, int nctx) {
    lsc_Status stat = lsc_status(t);
    if (s == NULL) {
        queue_removeself(&t->head);
        lsc_initsignal(&t->head);
    }
    else
        queue_append(&t->head, s);
    t->waitat = s;
    if (stat == lsc_Running)
        return lua_yield(t->L, nctx);
    return 0;
}

int lsc_ready(lsc_Task *t, int nctx) {
    if (lsc_status(t) == lsc_Running)
        return 0;
    queue_append(&t->head, &t->S->ready);
    return 1;
}

int lsc_hold(lsc_Task *t, int nctx) {
    if (lsc_status(t) == lsc_Running)
        return 0;
    queue_removeself(&t->head);
    lsc_initsignal(&t->head);
    return 1;
}

int lsc_join(lsc_Task *t, lsc_Task *jointo, int nctx) {
    lsc_Status st = lsc_status(t);
    lsc_Status sj = lsc_status(jointo);
    if (st == lsc_Running ||
            sj == lsc_Error ||
            sj == lsc_Finished ||
            sj == lsc_Dead)
        return 0;
    queue_append(&t->head, &jointo->joined);
    return 1;
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
    lsc_Status s = lsc_status(t);
    int res, top;
    if (s == lsc_Dead || s == lsc_Running)
        return 0;
    queue_append(&t->head, &t->S->running);
    t->waitat = &t->S->running;
    res = lua_status(t->L);
    top = lua_gettop(t->L);
    if (nargs < 0) { /* nargs defaults all stack values */
        nargs = top;
        if (res == LUA_OK)
            --nargs; /* first run */
    }
    /* adjust stack to contain args only */
    if (res != LUA_OK && res != LUA_YIELD)
        adjust_stack(t->L, nargs, top);
    res = lua_resume(t->L, from, nargs);
    if (res == LUA_OK || res != LUA_YIELD) {
        /* call joined tasks */
        wakeup_joins(t, from);
        if (res != LUA_OK) {
            /* setup error message as context */
            const char *errmsg = lua_tostring(t->L, -1);
            assert(errmsg != NULL);
            lua_settop(t->L, 0);
            lua_pushstring(t->L, errmsg);
            /* append to error list if error out */
            queue_append(&t->head, &t->S->error);
            return 0;
        }
    }
    /* finished or wait something? */
    assert(lsc_status(t) != lsc_Running);
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
            queue_append(&t->head, &wait_again);
        ++n;
    }
    queue_merge(&wait_again, s);
    return n;
}


/* main state maintains */

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
    queue_append(&s->main->head, &s->running);
    return s->main;
}

const char *lsc_collect(lua_State *L, lsc_State *s, lsc_Collect *f, void *ud) {
    lsc_Signal deleted;
    luaL_Buffer b;
    lsc_Task *t;
    luaL_buffinit(L, &b);
    lsc_initsignal(&deleted);
    while ((t = lsc_next(&s->error, NULL)) != NULL) {
        lua_pushfstring(L, "task(%p): ", t);
        luaL_addvalue(&b);
        if (f != NULL && f(t, L, &deleted, ud))
            luaL_addvalue(&b);
        else {
            assert(lua_isstring(t->L, -1));
            lua_xmove(t->L, L, 1);
            luaL_addvalue(&b);
            queue_append(&t->head, &deleted);
        }
        luaL_addchar(&b, '\n');
    }
    luaL_pushresult(&b);
    while ((t = lsc_next(&deleted, NULL)) != NULL)
        lsc_deletetask(t, L);
    return lua_tostring(L, -1);
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
            queue_append(&t->head, &ready_again);
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


/* lua type maintains */

lsc_Task *lsc_checktask(lua_State *L, int idx) {
    lsc_Task *t = (lsc_Task*)luaL_checkudata(L, idx, "sched.task");
    if (t->L == NULL)
        luaL_argerror(L, idx, "deleted task got");
    return t;
}

lsc_Task *lsc_testtask(lua_State *L, int idx) {
    return (lsc_Task*)luaL_testudata(L, idx, "sched.task");
}

int lsc_pushtask(lua_State *L, lsc_Task *t) {
    if (t == NULL)
        return 0;
    get_taskbox(L);
    lua_pushthread(t->L);
    lua_xmove(t->L, L, 1);
    lua_rawget(L, -2);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 2);
        return 0;
    }
    lua_remove(L, -2);
    return 1;
}

lsc_Signal *lsc_checksignal(lua_State *L, int idx) {
    lsc_Signal *s = (lsc_Signal*)luaL_checkudata(L, idx, "sched.signal");
    if (s->prev == NULL)
        luaL_argerror(L, idx, "deleted signal got");
    return s;
}

lsc_Signal *lsc_testsignal(lua_State *L, int idx) {
    return (lsc_Signal*)luaL_testudata(L, idx, "sched.signal");
}


/* task module interface */

static int Ltask_new(lua_State *L) {
    lua_State *coro;
    int top = lua_gettop(L);
    luaL_checktype(L, 1, LUA_TFUNCTION);
    coro = lua_newthread(L);
    lsc_ready(lsc_newtask(L, coro, 0), 0);
    lua_insert(L, 1);
    lua_pop(L, 1); /* remove coroutine */
    lua_xmove(L, coro, top);
    assert(lua_gettop(L) == 1);
    return 1;
}

static int Ltask_delete(lua_State *L) {
    lsc_Task *t = lsc_testtask(L, 1);
    if (t != NULL)
        lsc_deletetask(t, L);
    return 0;
}

static int Ltask_tostring(lua_State *L) {
    lsc_Task *t = lsc_testtask(L, 1);
    if (t == NULL)
        luaL_tolstring(L, -1, NULL);
    else {
        const char *s;
        switch (lsc_status(t)) {
        case lsc_Finished: s = "finish"; break;
        case lsc_Error:    s = "error";  break;
        case lsc_Dead:     s = "dead";   break;
        default:           s = NULL;     break;
        }
        if (s != NULL)
            lua_pushfstring(L, "sched.task(%s): %p", s, t);
        else
            lua_pushfstring(L, "sched.task: %p", t);
    }
    return 1;
}

static lsc_Task *default_task(lua_State *L, int *parg) {
    int arg = 2;
    lsc_Task *t = lsc_testtask(L, 1);
    if (t == NULL) {
        arg = 1; 
        t = lsc_current(L);
    }
    if (t == NULL)
        luaL_error(L, "current coroutine is not a task");
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
    int top = lua_gettop(L) - 1;
    lsc_Task *t = lsc_checktask(L, 1);
    if (top != 0) { /* replace context? */
        if (lua_status(t->L) == LUA_OK) /* first run? */
            lua_settop(t->L, 1); /* clear original context */
        lua_xmove(L, t->L, top);
    }
    /* push result of task */
    lua_pushboolean(L, lsc_wakeup(t, L, top == 0 ? -1 : top));
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
    return lsc_getcontext(L, t);
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
    case lsc_Finished: s = "finish";   break;
    case lsc_Error:    s = "error";    break;
    case lsc_Dead:     s = "dead";     break;
    case lsc_Running:  s = "running";  break;
    case lsc_Waitting: s = "waitting"; break;
    case lsc_Hold:     s = "hold";     break;
    case lsc_Ready:    s = "ready";    break;
    }
    lua_pushstring(L, s);
    return 1;
}

LUALIB_API int luaopen_sched_task(lua_State *L) {
    luaL_Reg libs[] = {
        { "__gc", Ltask_delete },
        { "__tostring", Ltask_tostring },
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
    if (luaL_newmetatable(L, "sched.task")) {
        luaL_setfuncs(L, libs, 0);
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
    }
    return 1;
}


/* signal module interface */

static int Lsignal_new(lua_State *L) {
    lsc_newsignal(L, 0);
    return 1;
}

static int Lsignal_delete(lua_State *L) {
    lsc_Signal *s = lsc_testsignal(L, 1);
    if (s != NULL) lsc_deletesignal(s, L);
    return 0;
}

static int Lsignal_tostring(lua_State *L) {
    lsc_Signal *s = lsc_testsignal(L, 1);
    if (s == NULL)
        luaL_tolstring(L, -1, NULL);
    else if (s->prev == NULL)
        lua_pushfstring(L, "sched.signal(deleted): %p", s);
    else
        lua_pushfstring(L, "sched.signal: %p", s);
    return 1;
}

static int Lsignal_emit(lua_State *L) {
    lsc_Task *t = NULL;
    lsc_Signal *s = lsc_checksignal(L, 1);
    int top = lua_gettop(L) - 1;
    while ((t = lsc_next(s, t)) != NULL)
        lsc_setcontext(L, t, top);
    lua_pushboolean(L, lsc_emit(s, L, top));
    return 1;
}

static int Lsignal_ready(lua_State *L) {
    lsc_Task *t = NULL;
    lsc_Signal *s = lsc_checksignal(L, 1);
    int top = lua_gettop(L) - 1;
    while ((t = lsc_next(s, t)) != NULL) {
        lsc_setcontext(L, t, top);
        lsc_ready(t, top);
    }
    return 1;
}

static int Lsignal_one(lua_State *L) {
    lsc_Task *t;
    lsc_Signal *s = lsc_checksignal(L, 1);
    int top = lua_gettop(L) - 1;
    if ((t = lsc_next(s, NULL)) != NULL) {
        lsc_setcontext(L, t, top);
        lsc_wakeup(t, L, top);
    }
    return 1;
}

static int Lsignal_filter(lua_State *L) {
    lsc_Signal *s = lsc_checksignal(L, 1);
    lsc_Task *t = lsc_next(s, NULL);
    lua_settop(L, 2);
    while (t != NULL) {
        int n;
        lsc_Task *next = lsc_next(s, t);
        lsc_pushtask(L, t);
        n = lsc_getcontext(L, t) + 1;
        lua_call(L, n, 0);
        t = next;
    }
    lua_settop(L, 1);
    return 1;
}

static int aux_iter(lua_State *L) {
    lsc_Signal *s = lua_touserdata(L, 1);
    lsc_Task *t = lua_isnoneornil(L, 2) ? NULL :
        lsc_testtask(L, 1);
    return lsc_pushtask(L, lsc_next(s, t));
}

static int Lsignal_iter(lua_State *L) {
    lsc_checksignal(L, 1);
    lua_pushcfunction(L, aux_iter);
    lua_pushvalue(L, 1);
    return 2;
}

static int Lsignal_count(lua_State *L) {
    lsc_Signal *s = lsc_checksignal(L, 1);
    lua_pushinteger(L, lsc_count(s));
    return 1;
}

static int Lsignal_index(lua_State *L) {
    lsc_Signal *s = lsc_checksignal(L, 1);
    int idx = luaL_optint(L, 2, 1);
    if (idx > 0) --idx;
    return lsc_pushtask(L, lsc_index(s, idx));
}

LUALIB_API int luaopen_sched_signal(lua_State *L) {
    luaL_Reg libs[] = {
        { "__gc", Lsignal_delete },
        { "__tostring", Lsignal_tostring },
#define ENTRY(name) { #name, Lsignal_##name }
        ENTRY(new),
        ENTRY(delete),
        ENTRY(emit),
        ENTRY(ready),
        ENTRY(one),
        ENTRY(filter),
        ENTRY(iter),
        ENTRY(count),
        ENTRY(index),
#undef  ENTRY
        { NULL, NULL }
    };
    if (luaL_newmetatable(L, "sched.signal")) {
        luaL_setfuncs(L, libs, 0);
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
    }
    return 1;
}


/* global module interface */

typedef struct poll_ctx {
    lua_State *L;
    int ref;
} poll_ctx;

static int aux_poll(lsc_State *s, lua_State *from, void *ud) {
    poll_ctx *ctx = (poll_ctx*)ud;
    lua_rawgeti(ctx->L, LUA_REGISTRYINDEX, ctx->ref);
    lua_call(ctx->L, 0, 0);
    return 0;
}

static int Lsetpoll(lua_State *L) {
    poll_ctx *ctx;
    int ret = 0;
    lsc_State *s = lsc_state(L);
    luaL_checktype(L, 1, LUA_TFUNCTION);
    if (s->poll == aux_poll) {
        ctx = (poll_ctx*)s->ud;
        lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->ref);
        if (lua_isfunction(L, -1))
            ret = 1;
        lua_pushvalue(L, 1);
        lua_rawseti(L, LUA_REGISTRYINDEX, ctx->ref);
        return ret;
    }
    ctx = (poll_ctx*)lua_newuserdata(L, sizeof(poll_ctx));
    ctx->L = L;
    lua_pushvalue(L, 1);
    ctx->ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lsc_setpoll(s, aux_poll, ctx);
    return 0;
}

static int Lonce(lua_State *L) {
    lsc_State *s = lsc_state(L);
    lua_pushboolean(L, lsc_once(s, L));
    return 1;
}

static int Lloop(lua_State *L) {
    lsc_loop(lsc_state(L), L);
    return 0;
}

static int Lerrors(lua_State *L) {
    lsc_State *s = lsc_state(L);
    lua_pushcfunction(L, aux_iter);
    lua_pushlightuserdata(L, &s->error);
    return 2;
}

static int aux_collect(lsc_Task *t, lua_State *from, lsc_Signal *deleted, void *ud) {
    /* XXX how to delete task here? */
    lua_pushvalue(from, 1);
    lsc_pushtask(from, t);
    lua_call(from, 1, 1);
    if (lua_isstring(from, -1))
        return 1;
    lua_pop(from, 1);
    return 0;
}

static int Lcollect(lua_State *L) {
    lsc_State *s = lsc_state(L);
    if (lua_isnoneornil(L, 1)) {
        lsc_collect(L, s, NULL, NULL);
        return 1;
    }
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lsc_collect(L, s, aux_collect, NULL);
    return 1;
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

void lsc_install(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "_PRELOAD");
  lua_pushstring(L, "sched");
  lua_pushcfunction(L, luaopen_sched);
  lua_rawset(L, -3);
  lua_pushstring(L, "sched.signal");
  lua_pushcfunction(L, luaopen_sched_signal);
  lua_rawset(L, -3);
  lua_pushstring(L, "sched.task");
  lua_pushcfunction(L, luaopen_sched_task);
  lua_rawset(L, -3);
  lua_pop(L, 1);
}

/* cc: flags+='-s -O2 -mdll -DLUA_BUILD_AS_DLL'
 * cc: libs+='-llua52' output='sched.dll'
 * cc: run='lua.exe test.lua' */
