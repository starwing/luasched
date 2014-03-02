#ifndef lsched_h
#define lsched_h


#include <lua.h>


#define LSC_API LUALIB_API


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
LUALIB_API int luaopen_sched(lua_State *L);
LUALIB_API int luaopen_sched_signal(lua_State *L);
LUALIB_API int luaopen_sched_task(lua_State *L);

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

/* get the extra object binding to signal object */
LSC_API void *lsc_signalpointer(lsc_Signal *s);

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

/* 
 * get the extra object binding to task object.
 * if you pass 0 to extrasz when you create the task object, NULL
 * will returned.
 */
LSC_API void *lsc_signalpointer(lsc_Signal *s);

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


#endif /* lsched_h */
