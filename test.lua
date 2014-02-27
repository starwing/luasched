local sched = require "sched"
local task = require "sched.task"
local signal = require "sched.signal"

print "\nsched:"
for k,v in pairs(sched) do print(k,v) end
print "\ntask:"
for k,v in pairs(task) do print(k,v) end
print "\nsignal:"
for k,v in pairs(signal) do print(k,v) end
print "\n=========="

local t = task.new(function()end)
assert(tostring(t):match "^sched.task: %x+$")
assert(t:wakeup())
assert(tostring(t):match "^sched.task%(deleted%): %x+$")

