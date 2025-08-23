-- The rainbow part example shown on the first page
-- examples/part_example.lua
local part = Instance.new("Part") -- Create a part
part.Anchored = true -- compat
part.Color = Color3.new(1,0,0) -- Make the part red
part.Position = Vector3.new(0,2.5,0) -- Position it
part.Parent = workspace -- Put it into workspace

local rs = game:GetService("RunService")
local t = 0

rs.RenderStepped:Connect(function(dt)
	t += dt
	part.CFrame = CFrame.new(part.Position) * CFrame.Angles(0, t, 0) -- rotate in place with CFrame
	part.Color = Color3.fromHSV((t*0.2 % 1), 1, 1) -- set part color
end)
