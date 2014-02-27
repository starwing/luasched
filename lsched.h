#ifndef lsched_h
#define lsched_h


#include <lua.h>


#define LSC_API LUALIB_API


typedef struct lsc_State lsc_State;
typedef struct lsc_Task lsc_Task;
typedef struct lsc_Signal lsc_Signal;

typedef enum lsc_Status {
    lsc_Error,
    lsc_Hold,
    lsc_Ready,
    lsc_Running,
    lsc_Waiting,
    lsc_StatusNum
} lsc_Status;


/* 
 * poll function for once/loop
 * with global state, the lua_State from once/loop called (can be NULL) and a uservalue.
 * return non-zero if the loop will continue, or 0 break the loop.
 */
typedef int lsc_Poll(lsc_State *s, lua_State *from, void *ud);

/*
 * collect the error tasks.
 * deleted is the queue that should deleted later, if you want delete
 * task t, wait it to deleted, DO NOT call lsc_deletetask!.  return 1
 * if a string is pushed to lua_State *from, used concated with
 * preivous error string, or 0 does nothing (and lsc_collect will
 * generate error message itself).
 * be ware keep the balance of the Lua stack!
 */
typedef int lsc_Collect(lsc_Task *t, lua_State *from, lsc_Signal *deleted, void *ud);


/* 
 * the lua sched module export functions
 */
LUALIB_API int luaopen_sched(lua_State *L);
LUALIB_API int luaopen_sched_signal(lua_State *L);
LUALIB_API int luaopen_sched_task(lua_State *L);

/* 
 * install lua module to Lua, so you can require() it.
 * will install "sched", "sched.signal" and "sched.task" module.
 */
LSC_API void lsc_install(lua_State *L);


/* return the main state of sched module.  */
LSC_API lsc_State *lsc_state(lua_State *L);

/* return the current task of lua state L, or NULL if L is not a task. */
LSC_API lsc_Task *lsc_current(lua_State *L);

/* return the task object against lua main thread. */
LSC_API lsc_Task *lsc_maintask(lua_State *L);

/* collect the error tasks, push a string onto lua stack, and return
 * the string pointer, return NULL if no error tasks */
LSC_API const char *lsc_collect(lua_State *L, lsc_State *s, lsc_Collect *clt, void *ud);

/* set poll function for once/loop */
LSC_API void lsc_setpoll(lsc_State *s, lsc_Poll *poll, void *ud);

/* run scheduler once, return non-zero if scheduler need run further,
 * or 0 if scheduler needn't run. */
LSC_API int  lsc_once(lsc_State *s, lua_State *from);

/* run a loop, return if not more task is running. */
LSC_API void lsc_loop(lsc_State *s, lua_State *from);


/* 
 * create a new lua signal object (a userdata).
 * extrasz is the extrasz, you can contain your data here. you can get
 * a pointer to that with lsc_signalud().
 */
LSC_API lsc_Signal *lsc_newsignal(lua_State *L, size_t extrasz);

/* delete a lua signal object. will wakeup all tasks wait on them and
 * then invalid this signal (can not wait on it). */
LSC_API void lsc_deletesignal(lsc_Signal *s, lua_State *from);

/* init a self-used, user alloced lsc_Signal for temporary works.
 * you DO NOT need to delete this signal, but you must make sure it's
 * empty if you don't use it anymore.
 */
LSC_API void lsc_initsignal(lsc_Signal *s);

/* 
 * get the extra object binding to signal object.
 * if you pass 0 to extrasz when you create the signal object, NULL
 * will returned.
 */
LSC_API void *lsc_signalpointer(lsc_Signal *s);

/* return the next task wait on s, the previous task is curr, the
 * first task will returned if curr == NULL, or NULL if no tasks
 * waitting on the signal.
 */
LSC_API lsc_Task *lsc_next(lsc_Signal *s, lsc_Task *curr);

/* check/test whether a object at lua stack is a signal */
LSC_API lsc_Signal *lsc_checksignal(lua_State *L, int idx);
LSC_API lsc_Signal *lsc_testsignal(lua_State *L, int idx);


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
 *
 * joined tasks will be waked up with `true`, plus return values from
 * task (if task is returnd), or with `nil`, plus a error value.
 * if task is waitting something (i.e. the underlying coroutine is
 * yield), joined tasks will be waked up with `nil`, plus a message
 * "task deleted", and any values from task's context.
 */
LSC_API int lsc_deletetask(lsc_Task *t, lua_State *from);

/* check/test whether a object at lua stack is a task */
LSC_API lsc_Task *lsc_checktask(lua_State *L, int idx);
LSC_API lsc_Task *lsc_testtask(lua_State *L, int idx);

/* push task t's lua object to lua stack, */
LSC_API int lsc_pushtask(lua_State *L, lsc_Task *t);


/* push the context of task t onto lua stack */
LSC_API int lsc_setcontext(lua_State *L, lsc_Task *t, int nargs);

/* get context of task t */
LSC_API int lsc_getcontext(lua_State *L, lsc_Task *t);

/* return the status of a task */
LSC_API lsc_Status lsc_status(lsc_Task *t);

/* set a task t as error status, the error string given as errmsg */
LSC_API int lsc_error(lsc_Task *t, const char *errmsg);

/* wait to a signal s.
 * if t is running, it will be yield (doesn't return any more), so in
 * this case you'd better use `return lsc_wait(...);` idiom, just like
 * `lua_yield`.
 * nctx is the number of context data on t's stack. contexts can set
 * by lsc_setcontext().
 * it will return immediately if t is already waitting for something.
 * so it's a way to change task's waiting.
 *
 * if s == NULL, this is equal `lsc_hold()`.
 * */
LSC_API int lsc_wait(lsc_Task *t, lsc_Signal *s, int nctx);

/* ready to run at next tick. i.e. the next run of lsc_once() */
LSC_API int lsc_ready(lsc_Task *t, int nctx);

/* never run again before status changed */
LSC_API int lsc_hold(lsc_Task *t, int nctx);

/* wait to another task finished or errored */
LSC_API int lsc_join(lsc_Task *t, lsc_Task *jointo, int nctx);

/* wake up a waitting task t, or does nothing if t is running.
 *
 * if task t is finished (returned from lua or has error out), the
 * task itself will deleted, i.e. can not run anymore, and any tasks
 * joined on it will be waked up.
 */
LSC_API int lsc_wakeup(lsc_Task *t, lua_State *from, int nargs);

/* emit a signal, wake up all tasks wait on it */
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
