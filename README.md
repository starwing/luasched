lua-sched - A C coroutine scheduler
-----------------------------------

lua-sched is A C implemented Lua coroutine scheduler which implements
a Lumen[1] like interface. It's designed to used to implement a
coroutine based libuv binding.

[1]: https://github.com/xopxe/Lumen

lua-sched has two object: `signal` and `task`. Signal is a object that
task can wait on then, if signal is emit, all task wait on it will
wakeup and get the argument you pass to signal. You can iterate tasks
wait on this signal, i.e. the signal is just a queue that contains all
task that wait on them.

Task is a wrapped coroutine. You can create a task by pass a existing
coroutine or a function. Task will run at next 'tick'. A tick is a
single run of main loop, will describe below.

All task are in a status: `running`, `waiting`, `hold`, `ready` and `error`. New task
will at ready status, so if you do not want this task to run, just set
it to hold status. Only one task can at running state, other task may
wait on a signal or another task (`waiting`), or get ready to run at
next 'tick' (`ready`). If you don't want task to run anymore, you can
set it's status to hold, in this way task will not running, until you
set it's status to ready. Hold is just a 'default' signal that can
wait at, so hold is just a default waiting status. If a task has error,
it will at `error` status, it can be restart later.

There are some functions that you can operates tasks. These functions
are export to lua-sched module, if you call them directly (without a
task for it's first argument), they will operates the current task,
i.e. the task you call these functions.

All these functions accept any number of extra arguments as the
context. If task will waiting on any signal, these arguments will be
the context for task, e.g. why they are waiting on the singal? These
arguments can be used with a filter function, will describe below. The
arguments you passed to wakeup will become the return value of
functions that makes task waiting.

Functions on tasks:

- `delete()`
    delete a task, free resources and never run it again.
- `wait(signal, ...)`
    wait on a signal, cancel from the signal it waited before (if
    any). (TODO how to wait multi-signal?)
- `ready(...)`
    schedule task to run next 'tick', cancel from any singal it
    waited (if any).
- `hold(...)`
    task will cancel from signal it waited (if any), and doesn't
    run unless it's status changed.
- `wakeup(...)`
    run task immediately. Cancel from any signal it waited (if
    any).
- `context()`
    if task is waiting, get the contexts passed to wait functions,
    or nothing if task is running.
- `join(task)`
    wait another task -- run when that task finished. if that task
    has no errors, return true plus the return value of that task,
    or return nil and a error string from that task.
- `status()`
    return the status of task, 'error', 'running', 'waiting',
    'ready' or 'hold'. if status is 'error', a extra error string
    is returned.

Signal is a queue that hold any tasks wait on it. You can access any
task that wait on it. You can wake up them all. If you do so, any
context on them will be discard, they will accept arguments you pass
to emit as return values.

Functions on signals:

- `emit(...)`
    wakeup all tasks waiting on this signal, run them immediately.
- `ready(...)`
    makes tasks waking on this signal run at next 'tick'.
- `one(...)` (TODO a new name?)
    wakeup first task that waiting on this signal.
- `filter(f)`
    run function f to all tasks, with it's context, function f can
    decide how to deal with tasks, e.g. wakeup it, hold it, ready
    it, or leave it untouched.
- `iter()`
    return a iterator that iterates all tasks on this signals.
- `index(idx)`
    get task at index idx, with it's contexts.
- `delete()`
    delete a signal, wakeup all tasks with nil, "deleted". task
    can not wait on a deleted signal (will error out).


There are some global functions to used in lua-sched. Used to run a
tick, or start a loop, or any other things. Notice that the main state
of Lua is registered as a task as well. Wait it has different behaves.

There is a functions used to do the 'real' waiting work, named 'poll'.
If main state waiting on a signal or a task or hold, the poll function
will run until the main state is waked up. If main state is hold, the
poll function will run until the main state changed it's state.

A 'tick' time is before the poll function runs. All ready tasks will
wakeup and run just before poll function runs. If a task get ready
at this time, it will wakeup at the next 'tick' time. So it's safe to
ready a task at tick time.

All task may error out when it's running. If main state has error, it
just behave as before. Otherwise, it will collected to a "error list",
to get the error list, just call errors() functions to get all error
strings. A task may restart after error occurs, with arguments passed
to restart() function.


Functions of module:

- `poll(f)`
    set a poll functions, used to do the real waiting. poll
    functions return true if next run is needed, or lopp must
    return otherwise.
- `once()`
    run poll functions once.
- `loop()`
    start a event loop, unless poll functions return false.
- `errors()`
    return a iterators if to iterates all error task.
- `collect(['delete'|'restart'|f])`
    collect error string, if nothing or 'delete' is given, all
    error tasks will deleted, if restart is given, they will all
    restarted. if a function f is given, it will called on every
    task with task and error string as it's arguments. it can
    restart or delete tasks.

lua-sched will exports some C API to help used on other C module. They
are listed and documents at lua-sched.h, you can link your module with
sched.dll, or just static link with it, the Lua module in lua-sched is
hide unless you call `lsc_install` if you static link with it.

A optional timer module can be found as a example to use lua-sched
API, or you can use it as a real-life module. It support Windows/Unix
environment.

License
-------
Same as Lua, see COPYING.

Improvement
-----------
how to wait to multi signals?
