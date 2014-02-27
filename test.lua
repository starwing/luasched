local sched = require "sched"
local task = require "sched.task"
local signal = require "sched.signal"

for k,v in pairs(sched) do print(k,v) end
print(("="):rep(60))
for k,v in pairs(task) do print(k,v) end
print(("="):rep(60))
for k,v in pairs(signal) do print(k,v) end
print(("="):rep(60))
