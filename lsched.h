#ifndef lsched_h
#define lsched_h


#include <lua.h>


#ifdef __cplusplus
# define LSC_NS_BEGIN extern "C" {
# define LSC_NS_END   }
#else
# define LSC_NS_BEGIN
# define LSC_NS_END
#endif

#if !defined(LSC_API) && defined(_WIN32)
# ifdef LSC_IMPLEMENTATION
#  define LSC_API __declspec(dllexport)
# else
#  define LSC_API __declspec(dllimport)
# endif
#endif

#ifndef LSC_API
# define LSC_API extern
#endif

#define LSCLUA_API LSC_API

LSC_NS_BEGIN

typedef struct lsc_State lsc_State;
typedef struct lsc_Task lsc_Task;
typedef struct lsc_Signal lsc_Signal;

/*
 * task status.
 *
 * task will get into several status:
 *   - lsc_Finished: just as dead for users, but you can retrieve
 *     context from tasks, can not run or wait anything or joined on
 *     it, use `lsc_deletetask` to trasfer to `lsc_Dead` status and
 *     complete free all resources.
 *   - lsc_Error: just like `lsc_Finished`, but means task has errors.
 *   - lsc_Dead: a dead tasks, can not do anything, the underlying
 *     resources all freed.
 *   - lsc_Hold: can not run anymore, unless you use `lsc_wait` or
 *     `lsc_wakeup` trasfer it to another status. use `lsc_hold` to
 *     trasfer to this status.
 *   - lsc_Ready: schedule to run at next 'tick', i.e. the next time
 *     `lsc_once` called. use `lsc_ready` to trasfer to this status.
 *   - lsc_Running: is running, use `lsc_wakeup` to trasfer to this
 *     status.
 *   - lsc_Waitting: waitting some signal, use `lsc_wait` to trasfer
 *     to this status.
 *
 * if a task is not running, using lsc_wait will turn it to
 * lsc_Waitting status, but if not, lsc_wait will abort current C
 * executing, i.e. it will call longjmp to break current running. be
 * careful about this.
 */
typedef enum lsc_Status {
    lsc_Finished = -3,
    lsc_Error    = -2,
    lsc_Dead     = -1,
    lsc_Running  =  0,
    lsc_Waitting =  1,
    lsc_Hold     =  2,
    lsc_Ready    =  3
} lsc_Status;


/*
 * poll function for once/loop
 * with global state, the lua_State from once/loop called (can be NULL) and a uservalue.
 * return non-zero if the loop will continue, or 0 break the loop.
 */
typedef int lsc_Poll(lsc_State *s, lua_State *from, void *ud);

/*
 * collect the error message of errored out tasks.
 * return 1 if a string is pushed to lua_State *from, used concated
 * with preivous error string, or 0 does nothing (and lsc_collect will
 * generate error message itself).
 * be ware keep the balance of the Lua stack!
 */
typedef int lsc_Collect(lsc_Task *t, lua_State *from, void *ud);


/* 
 * the lua sched module export functions
 */
LSCLUA_API int luaopen_sched(lua_State *L);
LSCLUA_API int luaopen_sched_signal(lua_State *L);
LSCLUA_API int luaopen_sched_task(lua_State *L);

/* 
 * install lua module to Lua, so you can `require()` it.
 * will install "sched", "sched.signal" and "sched.task" module.
 */
LSC_API void lsc_install(lua_State *L);


/* return the main state of sched module  */
LSC_API lsc_State *lsc_state(lua_State *L);

/* return the current task of lua state L,
 * or NULL if L is not a task.  */
LSC_API lsc_Task *lsc_current(lua_State *L);

/* return the task object against lua main thread. */
LSC_API lsc_Task *lsc_maintask(lua_State *L);

/* collect the error tasks, push a string onto lua stack, and return
 * the string pointer, return NULL if no error tasks */
LSC_API const char *lsc_collect(lua_State *L, lsc_State *s, lsc_Collect *clt, void *ud);

/* set poll function for once/loop */
LSC_API void lsc_setpoll(lsc_State *s, lsc_Poll *poll, void *ud);

/* run scheduler once.
 * return 1 if scheduler need run further,
 * return -1 if has tasks error out, 
 * or return 0 if scheduler needn't run. */
LSC_API int lsc_once(lsc_State *s, lua_State *from);

/* run a loop.
 * return 1 if not more task is running,
 * or return 0 if has tasks error out. */
LSC_API int lsc_loop(lsc_State *s, lua_State *from);


/* create a new lua signal object (a userdata).
 * extrasz is the extrasz, you can contain your data here. you can get
 * a pointer to that with `lsc_signalud`.  */
LSC_API lsc_Signal *lsc_newsignal(lua_State *L, size_t extrasz);

/* delete a lua signal object. will wakeup all tasks wait on them and
 * then invalid this signal (can not wait on it). */
LSC_API void lsc_deletesignal(lsc_Signal *s, lua_State *from);

/* get the extra object binding to signal object and vice versa */
#define lsc_signalpointer(s) (void*)((lsc_Signal*)(s) + 1)
#define lsc_signalfromptr(p)        ((lsc_Signal*)(p) - 1)

/* check a signal is valid (can be waited) */
LSC_API int lsc_signalvalid(lsc_Signal *s);

/* check/test whether a object at lua stack is a signal */
LSC_API lsc_Signal *lsc_checksignal(lua_State *L, int idx);
LSC_API lsc_Signal *lsc_testsignal(lua_State *L, int idx);

/* init a self-used, user alloced lsc_Signal for temporary works.
 * needn't to delete this signal, but you must make sure it's empty if
 * you don't use it anymore. anyway you can all `lsc_deletesignal` to
 * ensure this.
 */
LSC_API void lsc_initsignal(lsc_Signal *s);

/* return the next task wait on s, the previous task is curr, the
 * first task will returned if curr == NULL, or NULL if no tasks
 * waitting on the signal.
 */
LSC_API lsc_Task *lsc_next(lsc_Signal *s, lsc_Task *curr);

/* get the count of tasks that wait on signal s */
LSC_API size_t lsc_count(lsc_Signal *s);

/* get the task at idx for signal s, idx is 0-bases and can be
 * negative (reversed index).
 * note that this routine is O(n), use `lsc_next` if you want to
 * travrse tasks waitting on signal.  */
LSC_API lsc_Task *lsc_index(lsc_Signal *s, int idx);


/* 
 * create new task. extrasz is extra size to alloc from userdata. 
 * coro is a created coroutine (by lua_newthread), leave a new task
 * object to lua stack and return it's pointer.
 */
LSC_API lsc_Task *lsc_newtask(lua_State *L, lua_State *coro, size_t extrasz);

/*
 * delete the task. wake it up to tell the deletion (if it's waitting
 * something), and run all tasks joined on it, with nrets at t's
 * stack (access from t->L). after the task is deleted, you can not
 * run/joined this task.
 */
LSC_API int lsc_deletetask(lsc_Task *t, lua_State *from);

/* get the extra object binding to task object and vice versa */
#define lsc_taskpointer(t) (void*)((lsc_Task*)(t) + 1)
#define lsc_taskfromptr(p)        ((lsc_Task*)(p) - 1)

/* check/test whether a object at lua stack is a task */
LSC_API lsc_Task *lsc_checktask(lua_State *L, int idx);
LSC_API lsc_Task *lsc_testtask(lua_State *L, int idx);

/* push task t's lua object to lua stack */
LSC_API int lsc_pushtask(lua_State *L, lsc_Task *t);


/* push the context of task t onto lua stack, preivous context will be
 * clean up.
 *
 * NOTE that contexts at lua stack L will NOT poped, this allow you
 * set many tasks' context without copy contexts on L.
 *
 * if you needn't retain contexts on lua stack L, use `lua_xmove`
 * instead, it may faster than this routine, beware the special case
 * described below:
 *
 * if task t has never waked up before (i.e. it have a 'fresh'
 * coroutine underlying), there is some special rules: if the bottom
 * of stack (index 1) is a integer, then it's the retained index
 * numbers of stack (include itself). otherwise, the count of retained
 * index is 1, usually the function will be called itself. if you call
 * `lsc_setcontext` on this kind of task, the retained indexs will not
 * touched.
 *
 * does nothing if t is running, dead, finished or error out (i.e.
 * status > 0).
 */
LSC_API int lsc_setcontext(lua_State *L, lsc_Task *t, int nargs);

/* get context of task t, or return values if t finished, or error
 * message if t error out */
LSC_API int lsc_getcontext(lua_State *L, lsc_Task *t);

/* return the status of a task */
LSC_API lsc_Status lsc_status(lsc_Task *t);

/* set a task t as error status, the error string given as errmsg.
 * called joined tasks af any, see `lsc_wakeup`.
 * does nothing if t is running, dead, finished or already error out
 * (i.e. status <= 0)
 */
LSC_API int lsc_error(lsc_Task *t, const char *errmsg);

/* wait to a signal s.
 *
 * if t is running, it will be yield (doesn't return any more), so in
 * this case you'd better use `return lsc_wait(...);` idiom, just like
 * `lua_yield`.
 * nctx is the number of context data on t's stack. contexts can set
 * by `lsc_setcontext`.
 * it will return immediately if t is already waitting for something.
 * so it's a way to change task's waiting.
 *
 * if s == NULL, this is equal `lsc_hold`.
 */
LSC_API int lsc_wait(lsc_Task *t, lsc_Signal *s, int nctx);

/* ready to run at next tick. i.e. the next run of `lsc_once`.
 * does nothing if task is running */
LSC_API int lsc_ready(lsc_Task *t, int nctx);

/* never run again before status changed.
 * does nothing if task is running */
LSC_API int lsc_hold(lsc_Task *t, int nctx);

/* wait to another task finished or errored.
 * does nothing if:
 *   - t is running;
 *   - jointo is running, dead, finished or errored out.
 *     (i.e. status <= 0)
 * */
LSC_API int lsc_join(lsc_Task *t, lsc_Task *jointo, int nctx);

/* wake up a waitting task t, or does nothing if t is running.
 *
 * if task t returned normally, the task itself will deleted,  but you
 * can still use `lsc_getcontext` to retrieve return values from
 * task. call `lsc_deletetask` later will really free resource of
 * tasks.
 * if task t errored out, it will chained to error signal, you can
 * call `lsc_collect` to get the result of errors, or just delete it
 * simply. all these way will clean up resources.
 *
 * if task if finised or error out, joined tasks will be waked up with
 * `true`, plus return values from task (if task is returnd), or with
 * `nil`, plus a error value. if task is waitting something (i.e. the
 * underlying coroutine is yield), joined tasks will be waked up with
 * `nil`, plus a message "task deleted", and any values from task's
 * context.
 * 
 * this function will return 0 for error, or 1 otherwise.
 */
LSC_API int lsc_wakeup(lsc_Task *t, lua_State *from, int nargs);

/* emit a signal, wake up all tasks wait on it.
 * 
 * if nargs < 0, all tasks context will be the return values of waked
 * up tasks. if from != NULL and nargs > 0, nargs values at from will
 * replaced as tasks context and returned. after that, these values
 * will poped from state.
 *
 * if a task wait to s after wakeup, it will not wakeup again. you
 * should call emit again to wakeup it. i.e. every tasks on signal s
 * only wakeup once.
 *
 * return the count of waked up tasks.
 */
LSC_API int lsc_emit(lsc_Signal *s, lua_State *from, int nargs);


/* all fields in structure are READ-ONLY */

struct lsc_Signal {
    struct lsc_Signal *prev;
    struct lsc_Signal *next;
};

struct lsc_Task {
    lsc_Signal head;
    lsc_Signal joined;
    lsc_State *S;
    lua_State *L;
    lsc_Signal *waitat;
};

struct lsc_State {
    lsc_Signal running;
    lsc_Signal ready;
    lsc_Signal error;
    lsc_Task *main;
    void *ud;
    lsc_Poll *poll;
};


LSC_NS_END

#endif /* lsched_h */


#ifdef LSC_IMPLEMENTATION

LSC_NS_BEGIN


#include <lauxlib.h>
#include <assert.h>
#include <string.h>


#define LSC_MAIN_STATE 0x15CEA125
#define LSC_TASK_BOX   0x7A58B085

#if LUA_VERSION_NUM >= 503
# define lua53_rawgetp lua_rawgetp
# define lua53_rawgeti lua_rawgeti
#else
static int lua53_rawgetp(lua_State *L, int idx, const void *p)
{ lua_rawgetp(L, idx, p); return lua_type(L, -1); }
static int lua53_rawgeti(lua_State *L, int idx, lua_Integer i)
{ lua_rawgeti(L, idx, i); return lua_type(L, -1); }
#endif


static void get_taskbox(lua_State *L) {
    if (lua_rawgetp(L, LUA_REGISTRYINDEX, (void*)LSC_TASK_BOX) == LUA_TTABLE)
        return;
    lua_pop(L, 1);
    lua_newtable(L); /* no weak? */
    lua_pushvalue(L, -1);
    lua_rawsetp(L, LUA_REGISTRYINDEX, (void*)LSC_TASK_BOX);
}

LSC_API lsc_State *lsc_state(lua_State *L) {
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

static void queue_replace(lsc_Signal *head, lsc_Signal *to) {
    if (to->prev) {
        head->prev = to->prev;
        head->prev->next = head;
        head->next = to->next;
        head->next->prev = head;
        to->prev = to->next = NULL; /* invalid queue */
    }
    else 
        head->prev = head->next = head;
}

static int queue_task(lsc_Task *t, lsc_Signal *s) {
    queue_append(&t->head, t->waitat = s);
    return 1;
}

LSC_API lsc_Signal *lsc_newsignal(lua_State *L, size_t extrasz) {
    lsc_Signal *s =
        (lsc_Signal*)lua_newuserdata(L, sizeof(lsc_Signal) + extrasz);
    luaL_setmetatable(L, "sched.signal");
    lsc_initsignal(s);
    return s;
}

LSC_API void lsc_deletesignal(lsc_Signal *s, lua_State *from) {
    lsc_Signal removed;
    lsc_Task *t = NULL;
    /* replace and invalid signal */
    queue_replace(&removed, s);
    while ((t = lsc_next(&removed, NULL)) != NULL) {
        lua_pushnil(t->L);
        lua_pushstring(t->L, "signal deleted");
        lsc_wakeup(t, from, 2);
        assert(t->waitat != &removed);
    }
}

LSC_API void lsc_initsignal(lsc_Signal *s) {
    s->prev = s->next = s;
}

LSC_API int lsc_signalvalid(lsc_Signal *s) {
    return s->prev != NULL;
}

LSC_API lsc_Task *lsc_next(lsc_Signal *s, lsc_Task *curr) {
    if (curr == NULL)
        return s->prev == s ? NULL : (lsc_Task*)s->next;
    return curr->head.next == s ?
        NULL : (lsc_Task*)curr->head.next;
}

LSC_API size_t lsc_count(lsc_Signal *s) {
    lsc_Task *t = NULL;
    size_t count = 0;
    while ((t = lsc_next(s, t)) != NULL)
        ++count;
    return count;
}

LSC_API lsc_Task *lsc_index(lsc_Signal *s, int idx) {
    if (!lsc_signalvalid(s)) /* handle deleted signal */
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

LSC_API lsc_Task *lsc_newtask(lua_State *L, lua_State *coro, size_t extrasz) {
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
    lsc_Signal joined;
    int nrets = 2, stat = lua_status(t->L);
    if (from == NULL)
        from = t->L;
    /* replace and invalid joined queue */
    t->waitat = NULL;
    queue_removeself(&t->head);
    lsc_initsignal(&t->head);
    if (!lsc_signalvalid(&t->joined))
        return;
    queue_replace(&joined, &t->joined);
    /* calc return values */
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
        if (t->L != from)
            lua_xmove(t->L, from, 1);
        break;
    }
    /* setup return values */
    lsc_emit(&joined, from, nrets);
    assert(joined.prev == &joined);
}

LSC_API int lsc_deletetask(lsc_Task *t, lua_State *from) {
    lsc_Status s = lsc_status(t);
    if (s == lsc_Dead || s == lsc_Running)
        return 0;
    /* invalid task and wake up joined tasks */
    wakeup_joins(t, from);
    /* remove it from task box */
    unregister_task(t);
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

LSC_API int lsc_setcontext(lua_State *L, lsc_Task *t, int nargs) {
    lsc_Status s = lsc_status(t);
    if (s <= 0) return 0;
    if (lua_status(t->L) == LUA_OK) { /* initial task? */
        int n = lua_tointeger(t->L, 1);
        lua_settop(t->L, n == 0 ? 1 : n);
    } else
        lua_settop(t->L, 0);
    return copy_stack(L, t->L, nargs);
}

LSC_API int lsc_getcontext(lua_State *L, lsc_Task *t) {
    lsc_Status s = lsc_status(t);
    if (s == lsc_Dead || s == lsc_Running)
        return 0;
    if (s == lsc_Error) {
        assert(lua_isstring(t->L, -1));
        lua_pushvalue(t->L, -1);
        lua_xmove(t->L, L, 1);
        return 1;
    }
    if (s > 0 && lua_status(t->L) == LUA_OK) { /* initial task? */
        int n = lua_tointeger(t->L, 1);
        return copy_stack(t->L, L,
                lua_gettop(t->L) - (n == 0 ? 1 : n));
    }
    return copy_stack(t->L, L, lua_gettop(t->L));
}

LSC_API lsc_Status lsc_status(lsc_Task *t) {
    if (t->L == NULL)
        return lsc_Dead;
    else if (t->waitat == &t->S->running)
        return lsc_Running;
    else if (t->waitat == &t->S->ready)
        return lsc_Ready;
    else if (t->waitat == &t->S->error)
        return lsc_Error;
    else if (!lsc_signalvalid(&t->joined))
        return lsc_Finished;
    else if (t->waitat == NULL)
        return lsc_Hold;
    return lsc_Waitting;
}

LSC_API int lsc_error(lsc_Task *t, const char *errmsg) {
    lsc_Status s = lsc_status(t);
    if (s == lsc_Dead || s == lsc_Finished) return 0;
    if (s == lsc_Running) return luaL_error(t->L, errmsg);
    lua_settop(t->L, 0);
    lua_pushstring(t->L, errmsg); /* context */
    return queue_task(t, &t->S->error);
}

LSC_API int lsc_wait(lsc_Task *t, lsc_Signal *s, int nctx) {
    lsc_Status stat = lsc_status(t);
    t->waitat = s;
    if (s == NULL) {
        queue_removeself(&t->head);
        lsc_initsignal(&t->head);
    }
    else
        queue_append(&t->head, t->waitat);
    if (stat == lsc_Running)
        return lua_yield(t->L, nctx);
    return 0;
}

LSC_API int lsc_ready(lsc_Task *t, int nctx) {
    if (lsc_status(t) == lsc_Running)
        return 0;
    return queue_task(t, &t->S->ready);
}

LSC_API int lsc_hold(lsc_Task *t, int nctx) {
    if (lsc_status(t) == lsc_Running)
        return 0;
    queue_removeself(&t->head);
    lsc_initsignal(&t->head);
    t->waitat = NULL;
    return 1;
}

LSC_API int lsc_join(lsc_Task *t, lsc_Task *jointo, int nctx) {
    lsc_Status st = lsc_status(t);
    lsc_Status sj = lsc_status(jointo);
    if (st == lsc_Running || sj <= 0) return 0;
    return queue_task(t, &jointo->joined);
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

LSC_API int lsc_wakeup(lsc_Task *t, lua_State *from, int nargs) {
    lsc_Status s = lsc_status(t);
    int res, top;
    if (s <= 0) return 0;
    queue_task(t, &t->S->running);
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
        /* invaliad task and call joined tasks */
        wakeup_joins(t, from);
        if (res != LUA_OK) {
            /* setup error message as context */
            const char *errmsg = lua_tostring(t->L, -1);
            assert(errmsg != NULL);
            lua_settop(t->L, 0);
            lua_pushstring(t->L, errmsg);
            queue_task(t, &t->S->error);
            return 0;
        }
    }
    /* finished or wait something? */
    assert(lsc_status(t) != lsc_Running);
    return 1;
}

LSC_API int lsc_emit(lsc_Signal *s, lua_State *from, int nargs) {
    int n = 0;
    lsc_Task *t;
    lsc_Signal wait_again;
    lsc_initsignal(&wait_again);
    while ((t = lsc_next(s, NULL)) != NULL) {
        assert(lsc_status(t) != lsc_Running);
        if (from && nargs > 0)
            lsc_setcontext(from, t, nargs);
        lsc_wakeup(t, from, nargs);
        if (t->waitat == s)
            queue_append(&t->head, &wait_again);
        ++n;
    }
    queue_replace(s, &wait_again);
    if (from && nargs > 0)
        lua_pop(from, nargs);
    return n;
}


/* main state maintains */

LSC_API lsc_Task *lsc_current(lua_State *L) {
    lsc_Task *t;
    get_taskbox(L);
    lua_pushthread(L);
    lua_rawget(L, -2);
    t = (lsc_Task*)lua_touserdata(L, -1);
    lua_pop(L, 2);
    return t;
}

LSC_API lsc_Task *lsc_maintask(lua_State *L) {
    lua_State *mL;
    lsc_State *s = lsc_state(L);
    if (s->main) return s->main;
    lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
    mL = lua_tothread(L, -1);
    assert(mL != NULL);
    s->main = lsc_newtask(L, mL, 0);
    lua_pop(L, 2);
    /* main task is always treats as running */
    queue_task(s->main, &s->running);
    return s->main;
}

LSC_API const char *lsc_collect(lua_State *L, lsc_State *s, lsc_Collect *f, void *ud) {
    lsc_Signal curr_error;
    luaL_Buffer b;
    lsc_Task *t;
    luaL_buffinit(L, &b);
    queue_replace(&curr_error, &s->error);
    lsc_initsignal(&s->error);
    while ((t = lsc_next(&curr_error, NULL)) != NULL) {
        lua_pushfstring(L, "task(%p): ", t);
        luaL_addvalue(&b);
        if (f != NULL && f(t, L, ud))
            luaL_addvalue(&b);
        else {
            assert(lua_isstring(t->L, -1));
            lua_xmove(t->L, L, 1);
            luaL_addvalue(&b);
            lsc_deletetask(t, L);
        }
        if (t->L != NULL)
            queue_append(&t->head, &s->error);
        luaL_addchar(&b, '\n');
    }
    luaL_pushresult(&b);
    return lua_tostring(L, -1);
}

LSC_API void lsc_setpoll(lsc_State *s, lsc_Poll *poll, void *ud) {
    s->poll = poll;
    s->ud = ud;
}

LSC_API int lsc_once(lsc_State *s, lua_State *from) {
    int res = 0;
    lsc_Signal curr_ready;
    queue_replace(&curr_ready, &s->ready);
    lsc_initsignal(&s->ready);
    lsc_emit(&curr_ready, from, -1);
    assert(curr_ready.prev == &curr_ready);
    if (s->poll != NULL)
        res = !s->poll(s, from, s->ud);
    if (s->error.prev != &s->error) /* has errors? */
        return -1;
    return res || lsc_next(&s->ready, NULL) != NULL;
}

LSC_API int lsc_loop(lsc_State *s, lua_State *from) {
    int res;
    while ((res = lsc_once(s, from)) > 0)
        ;
    return res == 0;
}


/* lua type maintains */

LSC_API lsc_Task *lsc_checktask(lua_State *L, int idx) {
    lsc_Task *t = (lsc_Task*)luaL_checkudata(L, idx, "sched.task");
    if (t->L == NULL)
        luaL_argerror(L, idx, "got deleted task");
    return t;
}

LSC_API lsc_Task *lsc_testtask(lua_State *L, int idx) {
    return (lsc_Task*)luaL_testudata(L, idx, "sched.task");
}

LSC_API int lsc_pushtask(lua_State *L, lsc_Task *t) {
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

LSC_API lsc_Signal *lsc_checksignal(lua_State *L, int idx) {
    lsc_Signal *s = (lsc_Signal*)luaL_checkudata(L, idx, "sched.signal");
    if (!lsc_signalvalid(s))
        luaL_argerror(L, idx, "got deleted signal");
    return s;
}

LSC_API lsc_Signal *lsc_testsignal(lua_State *L, int idx) {
    return (lsc_Signal*)luaL_testudata(L, idx, "sched.signal");
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
    else if (!lsc_signalvalid(s))
        lua_pushfstring(L, "sched.signal(deleted): %p", s);
    else
        lua_pushfstring(L, "sched.signal: %p", s);
    return 1;
}

static int Lsignal_emit(lua_State *L) {
    lsc_Signal *s = lsc_checksignal(L, 1);
    int top = lua_gettop(L) - 1;
    lua_pushboolean(L, lsc_emit(s, L, top == 0 ? -1 : top));
    return 1;
}

static int Lsignal_ready(lua_State *L) {
    lsc_Task *t;
    lsc_Signal *s = lsc_checksignal(L, 1);
    int top = lua_gettop(L) - 1;
    while ((t = lsc_next(s, NULL)) != NULL) {
        lsc_setcontext(L, t, top);
        lsc_ready(t, top);
        assert(t->waitat != s);
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
        lua_pushvalue(L, -1);
        lsc_pushtask(L, t);
        n = lsc_getcontext(L, t) + 1;
        lua_call(L, n, 0);
        t = next;
    }
    lua_settop(L, 1);
    return 1;
}

static int Lsignal_next(lua_State *L) {
    lsc_Signal *s = lsc_checksignal(L, 1);
    lsc_Task *t = lua_isnoneornil(L, 2) ? NULL :
        lsc_checktask(L, 2);
    return lsc_pushtask(L, lsc_next(s, t));
}

static int Lsignal_count(lua_State *L) {
    lsc_Signal *s = lsc_checksignal(L, 1);
    lua_pushinteger(L, lsc_count(s));
    return 1;
}

static int Lsignal_index(lua_State *L) {
    lsc_Signal *s = lsc_checksignal(L, 1);
    lua_Integer idx = luaL_optinteger(L, 2, 1);
    if (idx > 0) --idx;
    return lsc_pushtask(L, lsc_index(s, (int)idx));
}

LSCLUA_API int luaopen_sched_signal(lua_State *L) {
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
        ENTRY(next),
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
    int res, top = lua_gettop(L) - 1;
    lsc_Task *t = lsc_checktask(L, 1);
    lsc_Status s;
    if (top != 0) { /* replace context? */
        if (lua_status(t->L) == LUA_OK) /* first run? */
            lua_settop(t->L, 1); /* clear original context */
        lua_xmove(L, t->L, top);
    }
    lsc_wakeup(t, L, top == 0 ? -1 : top);
    s = lsc_status(t);
    assert(s != lsc_Running && s != lsc_Dead);
    lua_pushboolean(L, s != lsc_Error);
    res = lsc_getcontext(L, t) + 1;
    if (s == lsc_Finished)
        lsc_deletetask(t, L);
    return res;
}

static int Ltask_context(lua_State *L) {
    int top = lua_gettop(L) - 1;
    lsc_Task *t = lsc_checktask(L, 1);
    if (lsc_status(t) == lsc_Running)
        return 0;
    if (top > 0) { /* set context */
        lua_settop(t->L, 0);
        lua_xmove(L, t->L, top);
        lua_settop(L, 1);
        return 1;
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

LSCLUA_API int luaopen_sched_task(lua_State *L) {
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
        if (lua53_rawgeti(L, LUA_REGISTRYINDEX, ctx->ref) == LUA_TFUNCTION)
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
    int res = lsc_once(s, L);
    if (res >= 0)
        lua_pushboolean(L, res);
    else
        lua_pushnil(L);
    return 1;
}

static int Lloop(lua_State *L) {
    lua_pushboolean(L, lsc_loop(lsc_state(L), L));
    return 1;
}

static int Lerrors(lua_State *L) {
    if (lua_gettop(L) == 0) {
        lua_pushcfunction(L, Lerrors);
        return 1;
    }
    else {
        lsc_State *s = lsc_state(L);
        lsc_Task *t = lua_isnoneornil(L, -1) ? NULL :
            lsc_checktask(L, -1);
        return lsc_pushtask(L, lsc_next(&s->error, t));
    }
}

static int aux_collect(lsc_Task *t, lua_State *from, void *ud) {
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
    if (s->error.prev == &s->error)
        return 0; /* no errors */
    if (lua_isnoneornil(L, 1)) {
        lsc_collect(L, s, NULL, NULL);
        return 1;
    }
    lsc_collect(L, s, aux_collect, NULL);
    return 1;
}

LSCLUA_API int luaopen_sched(lua_State *L) {
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

LSC_API void lsc_install(lua_State *L) {
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


LSC_NS_END

#endif /* LSC_IMPLEMENTATION */

/* cc: flags+='-s -O2 -mdll -xc -DLSC_IMPLEMENTATION'
 * cc: libs+='-llua53' output='sched.dll'
 * cc: run='lua.exe test.lua' */
