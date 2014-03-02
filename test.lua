local sched = require "sched"
local task = require "sched.task"
local signal = require "sched.signal"

local tests = {}
local order = {}

local function add_test(name, f)
   order[#order+1] = name
   tests[name] = f
end

add_test("info_test", function()
   print "\nsched:"
   for k,v in pairs(sched) do print(k,v) end
   print "\ntask:"
   for k,v in pairs(task) do print(k,v) end
   print "\nsignal:"
   for k,v in pairs(signal) do print(k,v) end
   print "\n=========="
end)

add_test("name_test", function()
   local t = task.new(function() return "ret" end)
   assert(tostring(t):match "^sched.task: %x+$")
   local res, ret = t:wakeup()
   assert(res and ret == "ret")
   assert(tostring(t):match "^sched.task%(dead%): %x+$")
   t:delete()
   assert(tostring(t):match "^sched.task%(dead%): %x+$")
end)

add_test("wait_test", function()
   local s = signal.new()
   assert(tostring(s):match "^sched.signal: %x+$")
   local t = task.new(function(...)
      assert(... == "arg")
      local ret = task.wait(s, "ctx")
      assert(ret == "wait")
      return "ret"
   end, "arg")
   assert(t:wakeup())
   assert(t:context() == "ctx")
   assert(s:index(1) == t)
   local jt = task.new(function(res, ...)
      assert(res == true)
      assert(... == "ret")
   end)
   jt:join(t)
   local res, ret = t:wakeup "wait"
   assert(res and ret == "ret")
end)

if arg[1] then
   if tests[arg[1]] then
      print(arg[1])
      tests[arg[1]]()
      print("OK")
   end
   return
end

for _,k in ipairs(order) do
   print(k.."...")
   tests[k]()
   print("OK")
end
