
-- Since the Librebox scheduler is single-threaded and coroutine based (just as Roblox is)
-- , it is susceptible to crashing when:
-- + You forget to input a wait() or a task.wait() for the scheduler to exit.

print("CRASHING APPLICATION IN 3 SECONDS!")
task.wait(3)

-- Librebox (also like Roblox)
-- uses optimizationLevel=1 for Luau scripts
-- Having an empty while loop may be removed
-- by the Luau bytecode compiler 
x = 0
while true do
    x += 1
end 

print(x)
