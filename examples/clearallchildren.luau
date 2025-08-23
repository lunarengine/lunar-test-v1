-- Demonstration of :ClearAllChildren() in lua
local function countParts()
	local count = 0
	for _, child in ipairs(workspace:GetDescendants()) do
		if child:IsA("Part") then
			count += 1
		end
	end
	return count
end

-- create a part and make it visible
Instance.new("Part").Parent = workspace

workspace:FindFirstChildOfClass("Part"):Clone() -- extra part lol

print("Number of parts:", countParts()) -- should print 2

print("Destroying all in 3 seconds..")
task.wait(3)
workspace:ClearAllChildren()

print("Number of parts:", countParts()) -- should print 0
