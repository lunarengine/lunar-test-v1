
-- Create a part and put it into the workspace
-- Note: Physics is currently not implemented, will be later

-- use Instance.new()

local part = Instance.new("Part")

-- anchor it for safety
part.Anchored = true

-- bring it to the workspace
part.Parent = workspace

-- wait
task.wait(3)

-- bye bye!
print("Destroying part..")
part:Destroy()

print(part.Anchored) -- should be nil or not work
