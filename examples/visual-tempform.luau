-- Temperature wave form
-- Shown in repo
-- Demonstration of Color3, Position, advanced functions

local RunService = game:GetService("RunService")

-- Config
local GRID_SIZE = 20
local BLOCK_SIZE = 2
local Y_OFFSET = 3
local SCROLL_SPEED = 0.5

-- Base part
local base = Instance.new("Part")
base.Size = Vector3.new(BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE)
base.Parent = workspace

-- Build grid
local blocks = {}
for x = 1, GRID_SIZE do
	blocks[x] = {}
	for z = 1, GRID_SIZE do
		local p = base:Clone()
		p.CFrame = CFrame.new(
			(x - (GRID_SIZE + 1)/2) * BLOCK_SIZE,
			Y_OFFSET,
			(z - (GRID_SIZE + 1)/2) * BLOCK_SIZE
		)
		p.Parent = workspace
		blocks[x][z] = p
	end
end

-- Lerp helpers
local function lerp(a, b, t) return a + (b - a) * t end
local function lerpColor(c1, c2, t)
	return Color3.new(
		lerp(c1.R, c2.R, t),
		lerp(c1.G, c2.G, t),
		lerp(c1.B, c2.B, t)
	)
end

-- Cyclic thermal palette (red → orange → yellow → pale → white → back to red)
local stops = {
	Color3.fromRGB(180, 0,   0),   -- red
	Color3.fromRGB(255, 70,  0),   -- orange
	Color3.fromRGB(255, 180, 0),   -- yellow
	Color3.fromRGB(255, 230, 120), -- pale
	Color3.fromRGB(255, 255, 255), -- white
    Color3.fromRGB(255, 230, 120), -- pale
    Color3.fromRGB(255, 180, 0),   -- yellow
    Color3.fromRGB(255, 70,  0),   -- orange
}

local function palette(u) -- u in R, loops seamlessly
	u = u % 1
	local scaled = u * #stops
	local i = math.floor(scaled)
	local f = scaled - i
	local c1 = stops[(i % #stops) + 1]
	local c2 = stops[((i + 1) % #stops) + 1]
	return lerpColor(c1, c2, f)
end

-- Continuous diagonal field with constant stripe size
local function field(x, z, t)
	-- world coords in studs
	local wx = (x - (GRID_SIZE + 1)/2) * BLOCK_SIZE
	local wz = (z - (GRID_SIZE + 1)/2) * BLOCK_SIZE
	-- choose wavelength in studs; larger = wider stripes
	local wavelength = 20
	return (wx + wz) / wavelength + t * SCROLL_SPEED
end

-- Animate
local t = 0
RunService.RenderStepped:Connect(function(dt)
	t = t + dt
	for x = 1, GRID_SIZE do
		for z = 1, GRID_SIZE do
			local c = palette(field(x, z, t))
			local p = blocks[x][z]
			p.Color = c
			-- scale green to height, add baseline
			local y = Y_OFFSET + c.G * 4.5
			p.CFrame = CFrame.new(
				(x - (GRID_SIZE + 1)/2) * BLOCK_SIZE,
				y,
				(z - (GRID_SIZE + 1)/2) * BLOCK_SIZE
			)
		end
	end
end)

