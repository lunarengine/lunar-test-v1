-- Advanced Luau scripting
-- CFrame, math, rotation, RunService, RenderStepped

--[[
Wireframe Sphere Demo (Lat + Lon)
Creates a rotating wireframe sphere with latitude and longitude rings.
]]

local RunService = game:GetService("RunService")

-- CONFIG
local radius = 15
local segments = 16
local thickness = 0.3
local sphereCenter = Vector3.new(0, 20, 0)
local color = Color3.fromRGB(255, 255, 0)
local transparency = 0

-- Part template
local function createPart()
	local p = Instance.new("Part")
	p.Anchored = true
	p.CanCollide = false
	p.Size = Vector3.new(thickness, thickness, 1)
	p.Color = color
	p.Transparency = transparency
	p.Parent = workspace
	return p
end

-- Storage
local parts = {}
local rel = {}
local centerCF = CFrame.new(sphereCenter)

-- Helper to connect two points with a part and cache local-space transform
local function connectPoints(p1, p2)
	local dir = p2 - p1
	if dir.Magnitude <= 1e-6 then return end
	local mid = (p1 + p2) / 2
	local part = createPart()
	part.Size = Vector3.new(thickness, thickness, dir.Magnitude)
	part.CFrame = CFrame.new(mid, p2)
	table.insert(parts, part)
	table.insert(rel, centerCF:ToObjectSpace(part.CFrame))
end

-- Longitude (vertical great circles)
for i = 0, segments - 1 do
	local phi = 2 * math.pi * i / segments
	for j = 0, segments - 1 do
		local theta1 = math.pi * j / segments
		local theta2 = math.pi * (j + 1) / segments

		local p1 = Vector3.new(
			radius * math.sin(theta1) * math.cos(phi),
			radius * math.cos(theta1),
			radius * math.sin(theta1) * math.sin(phi)
		) + sphereCenter

		local p2 = Vector3.new(
			radius * math.sin(theta2) * math.cos(phi),
			radius * math.cos(theta2),
			radius * math.sin(theta2) * math.sin(phi)
		) + sphereCenter

		connectPoints(p1, p2)
	end
end

-- Latitude (horizontal rings)
for j = 1, segments - 1 do
	local theta = math.pi * j / segments
	for i = 0, segments - 1 do
		local phi1 = 2 * math.pi * i / segments
		local phi2 = 2 * math.pi * (i + 1) / segments

		local p1 = Vector3.new(
			radius * math.sin(theta) * math.cos(phi1),
			radius * math.cos(theta),
			radius * math.sin(theta) * math.sin(phi1)
		) + sphereCenter

		local p2 = Vector3.new(
			radius * math.sin(theta) * math.cos(phi2),
			radius * math.cos(theta),
			radius * math.sin(theta) * math.sin(phi2)
		) + sphereCenter

		connectPoints(p1, p2)
	end
end

-- Rigid rotation about center
local angle = 0
RunService.RenderStepped:Connect(function(dt)
	angle += dt * math.rad(20)
	local spin = centerCF * CFrame.Angles(angle * 0.5, angle, 0)
	for i = 1, #parts do
		parts[i].CFrame = spin * rel[i]
	end
end)
