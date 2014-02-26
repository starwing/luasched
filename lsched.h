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

typedef int lsc_Poll(lsc_State *s, lua_State *from, void *ud);
typedef const char *lsc_Collect(lsc_Task *t, lua_State *from, lsc_Signal *deleted, void *ud);


LUALIB_API int luaopen_sched(lua_State *L);
LSC_API void lsc_install(lua_State *L);

LSC_API lsc_Task *lsc_newtask(lua_State *L, lua_State *coro, size_t extrasz);
LSC_API int lsc_deletetask(lsc_Task *t, int nrets);

LSC_API lsc_Signal *lsc_newsignal(lua_State *L, size_t extrasz);
LSC_API void lsc_initsignal(lsc_Signal *s);
LSC_API void lsc_freesignal(lsc_Signal *s, lua_State *from);

LSC_API lsc_State *lsc_state(lua_State *L);
LSC_API lsc_Task *lsc_current(lua_State *L);
LSC_API lsc_Task *lsc_maintask(lua_State *L);
LSC_API lsc_Task *lsc_checktask(lua_State *L, int idx);
LSC_API lsc_Task *lsc_testtask(lua_State *L, int idx);
LSC_API lsc_Signal *lsc_checksignal(lua_State *L, int idx);
LSC_API lsc_Signal *lsc_testsignal(lua_State *L, int idx);
LSC_API int lsc_pushcontext(lua_State *L, lsc_Task *t);
LSC_API const char *lsc_collect(lua_State *L, lsc_State *s, lsc_Collect *clt, void *ud);

LSC_API void lsc_setpoll(lsc_State *s, lsc_Poll *poll, void *ud);
LSC_API int  lsc_once(lsc_State *s, lua_State *from);
LSC_API void lsc_loop(lsc_State *s, lua_State *from);

LSC_API lsc_Status lsc_status(lsc_Task *t);
LSC_API int lsc_error(lsc_Task *t, const char *errmsg);
LSC_API int lsc_wait(lsc_Task *t, lsc_Signal *s, int nctx);
LSC_API int lsc_ready(lsc_Task *t, int nctx);
LSC_API int lsc_hold(lsc_Task *t, int nctx);
LSC_API int lsc_join(lsc_Task *t, lsc_Task *jointo, int nctx);
LSC_API int lsc_wakeup(lsc_Task *t, lua_State *from, int nargs);

LSC_API int lsc_emit(lsc_Signal *s, lua_State *from, int nargs);
LSC_API lsc_Task *lsc_next(lsc_Signal *s, lsc_Task *curr);


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
