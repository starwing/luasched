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

add_test("loop_test", function()
   local counter = 0
   task.new(function()
      for i = 1, 10 do
         task.new(function()
            counter = counter + 1
         end)
      end
   end)
   assert(sched.once() == true)
   assert(sched.loop())
   assert(counter == 10)
   local t = task.new(function()
      error "err"
   end)
   assert(not sched.loop())
   assert(sched.errors(nil) == t)
   assert(sched.collect(function(t)
      return t:context()
   end):match "err")
   assert(t:status() == "error")
   assert(sched.collect(function(t1)
      local errmsg = t1:context()
      assert(t1 == t)
      t1:delete()
      return errmsg
   end):match "err")
   assert(t:status() == "dead")
end)

add_test("signal_test", function()
   local counter = 0
   local s = signal.new()
   local ts = {}
   for i = 1, 10 do
      ts[i] = task.new(function()
         counter = counter + 1
      end):wait(s)
   end
   assert(s:count() == 10)
   for i = 1, 10 do
      assert(s:index(i) == ts[i])
      assert(s:next(s:index(i)) == ts[i+1])
   end
   local i = 1
   s:filter(function(t)
      assert(t == ts[i])
      i = i + 1
   end)
   assert(s:one())
   assert(counter == 1)
   assert(s:emit())
   assert(counter == 10)
end)

add_test("task_test", function()
   local t
   t = task.new(function()
      assert(task.status() == "running")
      assert(t:status() == "running")
   end)
   assert(t:status() == "ready")
   assert(t:hold() == t)
   assert(t:status() == "hold")
   assert(t:wakeup())
   assert(t:status() == "dead") -- auto dead
   t:delete()
   assert(t:status() == "dead")
   local t = task.new(function()
      error "err"
   end)
   assert(not t:wakeup())
   assert(t:status() == "error")
   t:delete()
   assert(sched.errors(nil) == nil)
   local s = signal.new()
   local t = task.new(function()
      assert(task.wait(s, "foo") == "bar")
   end)
   assert(t:wakeup())
   assert(t:status() == "waitting")
   assert(t:context() == "foo")
   assert(t:context("bar") == t)
   assert(t:wakeup())
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
