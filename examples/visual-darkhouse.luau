-- "The Dark House"
-- Why is Librebox's default example called that?
-- Because I haven't implemented lighting within confined areas yet.
-- Haha.

-- written by me and gpt5 (i was too lazy)
-- Roblox house builder: only Parts + Position + Color + Size (no rotation)
-- Two floors, stairs, roof, porch, fence, simple furniture

-- we dont need that (we will add waitforchild asap)
local bp_destroyed = false

while (bp_destroyed == false) do
    local bp = game.Workspace:FindFirstChild("Baseplate")
    
    if bp then
        bp:Destroy()
        bp_destroyed = true
    end 
    task.wait()
end
-- services
local ws = game:GetService("Workspace")

-- template
local template = Instance.new("Part")
template.Anchored = true
template.Parent = nil

-- short hands
local function v3(x,y,z) return Vector3.new(x,y,z) end
local function rgb(r,g,b) return Color3.fromRGB(r,g,b) end

-- colors
local COL_GROUND   = rgb(95,160,90)
local COL_FOUND    = rgb(120,120,120)
local COL_WALL     = rgb(245,245,235)
local COL_TRIM     = rgb(220,220,220)
local COL_FLOOR    = rgb(200,180,150)
local COL_ROOF     = rgb(90,90,90)
local COL_GLASS    = rgb(170,200,255)
local COL_DARKWOOD = rgb(110,80,50)
local COL_LIGHT    = rgb(235,235,235)
local COL_STEEL    = rgb(160,160,170)
local COL_PATH     = rgb(140,130,115)

-- helpers
local function partFromCorner(corner, size, color, name, transparency)
    local scale = 1.01
    local newSize = size * scale
    local offset = (newSize - size) * 0.5
    local p = template:Clone()
    p.Size = newSize
    p.Position = corner + (size * 0.5) + offset
    p.Color = color or COL_WALL
    p.Name = name or "Part"
    p.Transparency = transparency or 0
    p.Parent = ws
    return p
end

local function uniqSorted(list)
    local seen, out = {}, {}
    for _,v in ipairs(list) do
        local k = math.floor(v*1000 + 0.5) -- quantize to avoid float dupes
        if not seen[k] then
            seen[k] = true
            table.insert(out, v)
        end
    end
    table.sort(out)
    return out
end

-- Build an axis-aligned wall that runs along X (length on X, thickness on Z)
-- corner: left-bottom-front corner, openings: { {x,y,w,h}, ... } in wall local frame
local function buildWallX(corner, length, height, thickness, openings, color)
    openings = openings or {}
    local xs = {0, length}
    local ys = {0, height}
    for _,o in ipairs(openings) do
        table.insert(xs, math.clamp(o.x, 0, length))
        table.insert(xs, math.clamp(o.x + o.w, 0, length))
        table.insert(ys, math.clamp(o.y, 0, height))
        table.insert(ys, math.clamp(o.y + o.h, 0, height))
    end
    xs = uniqSorted(xs)
    ys = uniqSorted(ys)
    for xi=1,#xs-1 do
        for yi=1,#ys-1 do
            local x0, x1 = xs[xi], xs[xi+1]
            local y0, y1 = ys[yi], ys[yi+1]
            local cx, cy = (x0+x1)/2, (y0+y1)/2
            local insideOpening = false
            for _,o in ipairs(openings) do
                if cx >= o.x and cx <= o.x + o.w and cy >= o.y and cy <= o.y + o.h then
                    insideOpening = true
                    break
                end
            end
            local dx, dy = x1 - x0, y1 - y0
            if dx > 0 and dy > 0 and not insideOpening then
                local segCorner = corner + v3(x0, y0, 0)
                partFromCorner(segCorner, v3(dx, dy, thickness), color or COL_WALL, "WallX")
            end
        end
    end
end

-- Build an axis-aligned wall that runs along Z (length on Z, thickness on X)
-- corner: left-bottom-front corner, openings: { {z,y,w,h}, ... } where w acts along Z
local function buildWallZ(corner, length, height, thickness, openings, color)
    openings = openings or {}
    local zs = {0, length}
    local ys = {0, height}
    for _,o in ipairs(openings) do
        table.insert(zs, math.clamp(o.z, 0, length))
        table.insert(zs, math.clamp(o.z + o.w, 0, length))
        table.insert(ys, math.clamp(o.y, 0, height))
        table.insert(ys, math.clamp(o.y + o.h, 0, height))
    end
    zs = uniqSorted(zs)
    ys = uniqSorted(ys)
    for zi=1,#zs-1 do
        for yi=1,#ys-1 do
            local z0, z1 = zs[zi], zs[zi+1]
            local y0, y1 = ys[yi], ys[yi+1]
            local cz, cy = (z0+z1)/2, (y0+y1)/2
            local insideOpening = false
            for _,o in ipairs(openings) do
                if cz >= o.z and cz <= o.z + o.w and cy >= o.y and cy <= o.y + o.h then
                    insideOpening = true
                    break
                end
            end
            local dz, dy = z1 - z0, y1 - y0
            if dz > 0 and dy > 0 and not insideOpening then
                local segCorner = corner + v3(0, y0, z0)
                partFromCorner(segCorner, v3(thickness, dy, dz), color or COL_WALL, "WallZ")
            end
        end
    end
end

-- Thin panes for windows on X-facing wall. Slight Z offset to avoid z-fighting.
local function addPanesX(corner, thickness, openings, inset, color)
    inset = inset or 0.2
    color = color or COL_GLASS
    for _,o in ipairs(openings or {}) do
        local paneCorner = corner + v3(o.x, o.y, inset)
        partFromCorner(paneCorner, v3(o.w, o.h, math.max(0.2, thickness - inset*2)), color, "PaneX", 0.35)
    end
end

-- Thin panes for Z-facing wall. Slight X offset to avoid z-fighting.
local function addPanesZ(corner, thickness, openings, inset, color)
    inset = inset or 0.2
    color = color or COL_GLASS
    for _,o in ipairs(openings or {}) do
        local paneCorner = corner + v3(inset, o.y, o.z)
        partFromCorner(paneCorner, v3(math.max(0.2, thickness - inset*2), o.h, o.w), color, "PaneZ", 0.35)
    end
end

-- simple repeated posts
local function postsAlongX(startCorner, count, spacing, size, color, name)
    for i=0,count-1 do
        partFromCorner(startCorner + v3(i*spacing, 0, 0), size, color, name or "PostX")
    end
end
local function postsAlongZ(startCorner, count, spacing, size, color, name)
    for i=0,count-1 do
        partFromCorner(startCorner + v3(0, 0, i*spacing), size, color, name or "PostZ")
    end
end

----------------------------------------------------------------
-- Site and global dims
----------------------------------------------------------------
local LOT = v3(180, 1, 180)
local LOT_CORNER = v3(-LOT.X/2, -0.1, -LOT.Z/2)

-- house footprint
local W, D = 80, 60          -- width X, depth Z
local WALL_T = 1              -- wall thickness
local H1, H2 = 16, 14         -- floor 1 and floor 2 wall heights
local SLAB = 1                -- floor/roof slab thickness

local houseCorner = v3(-W/2, SLAB, -D/2)   -- front-left-bottom for floor 1 slab top = y=SLAB

-- ground and foundation
partFromCorner(LOT_CORNER, LOT, COL_GROUND, "Lot")
partFromCorner(houseCorner + v3(-5, -SLAB, -5), v3(W+10, SLAB, D+10), COL_FOUND, "Foundation")

-- path to the front door
local pathW, pathL = 8, 35
local pathCorner = v3(-pathW/2, SLAB, -D/2 - 2 - pathL)
partFromCorner(pathCorner, v3(pathW, 0.2, pathL), COL_PATH, "FrontPath")

----------------------------------------------------------------
-- Floor 1: slab and exterior walls (door + windows)
----------------------------------------------------------------
-- floor 1 slab
partFromCorner(houseCorner, v3(W, SLAB, D), COL_FLOOR, "Floor1_Slab")

-- front wall (runs along X at front Z)
local doorW, doorH = 6, 9
local winW, winH = 10, 6
local frontOpenings = {
    {x = (W - doorW)/2, y = 0,  w = doorW, h = doorH},                 -- door
    {x = 8,             y = 5,  w = winW,  h = winH},                  -- left window
    {x = W - 8 - winW,  y = 5,  w = winW,  h = winH}                   -- right window
}
local frontCorner = houseCorner + v3(0, 0, 0)
buildWallX(frontCorner, W, H1, WALL_T, frontOpenings, COL_WALL)
addPanesX(frontCorner, WALL_T, {frontOpenings[2], frontOpenings[3]}, 0.15, COL_GLASS)
-- simple doorstep
partFromCorner(v3(-doorW/2, SLAB, -D/2 - 2), v3(doorW, 0.5, 2), COL_TRIM, "DoorStep")

-- back wall (runs along X at back Z)
local backOpenings = {
    {x = 12,            y = 5, w = winW, h = winH},
    {x = W - 12 - winW, y = 5, w = winW, h = winH}
}
local backCorner = houseCorner + v3(0, 0, D - WALL_T)
buildWallX(backCorner, W, H1, WALL_T, backOpenings, COL_WALL)
addPanesX(backCorner, WALL_T, backOpenings, 0.15, COL_GLASS)

-- left wall (runs along Z at left X)
local sideWinW = 8
local leftOpenings = {
    {z = 10,             y = 5, w = sideWinW, h = winH},
    {z = D - 10 - sideWinW, y = 5, w = sideWinW, h = winH}
}
local leftCorner = houseCorner + v3(0, 0, 0)
buildWallZ(leftCorner, D, H1, WALL_T, leftOpenings, COL_WALL)
addPanesZ(leftCorner, WALL_T, leftOpenings, 0.15, COL_GLASS)

-- right wall (runs along Z at right X)
local rightOpenings = {
    {z = 14,             y = 5, w = sideWinW, h = winH},
    {z = D - 14 - sideWinW, y = 5, w = sideWinW, h = winH}
}
local rightCorner = houseCorner + v3(W - WALL_T, 0, 0)
buildWallZ(rightCorner, D, H1, WALL_T, rightOpenings, COL_WALL)
addPanesZ(rightCorner, WALL_T, rightOpenings, 0.15, COL_GLASS)

-- interior partition (simple hallway wall with doorway)
local partX = -W/2 + 30
local partLen = D - 20
local partCorner = v3(partX, SLAB, -D/2 + 10)
local intDoorW = 5
local intDoor = { z = partLen/2 - intDoorW/2, y = 0, w = intDoorW, h = 8 }
buildWallZ(partCorner, partLen, 12, WALL_T, {intDoor}, COL_TRIM)

----------------------------------------------------------------
-- Stairs to floor 2 (axis-aligned steps)
----------------------------------------------------------------
local steps = H1            -- 16 steps, 1 stud rise each
local stepRise = 1
local stepRun  = 2.5
local stepWidth= 6
local stairCorner = v3(-W/2 + 6, SLAB, -D/2 + 14) -- bottom-left-front of first step
for i=0,steps-1 do
    local sc = stairCorner + v3(i*stepRun, i*stepRise, 0)
    partFromCorner(sc, v3(stepRun, stepRise, stepWidth), COL_DARKWOOD, "StairStep")
end
-- stair side rails
partFromCorner(stairCorner + v3(0, 0, -0.5), v3(steps*stepRun, 2, 0.5), COL_STEEL, "RailL")
partFromCorner(stairCorner + v3(0, 0, stepWidth), v3(steps*stepRun, 2, 0.5), COL_STEEL, "RailR")

----------------------------------------------------------------
-- Floor 2: slab and exterior walls with windows
----------------------------------------------------------------
local floor2Corner = houseCorner + v3(0, H1, 0)
-- extend floor2 slab by wall thickness
partFromCorner(floor2Corner - v3(0.25,0,WALL_T), v3(W + 0.125, SLAB, D + 2*WALL_T), COL_FLOOR, "Floor2_Slab")

-- second floor walls (repeat with higher window heights)
local f2WinY = 6
local f2FrontOpen = {
    {x = 10,            y = f2WinY, w = winW, h = winH},
    {x = W - 10 - winW, y = f2WinY, w = winW, h = winH}
}
local f2BackOpen = {
    {x = 14,            y = f2WinY, w = winW, h = winH},
    {x = W - 14 - winW, y = f2WinY, w = winW, h = winH}
}
local f2LeftOpen = {
    {z = 12,            y = f2WinY, w = sideWinW, h = winH},
    {z = D - 12 - sideWinW, y = f2WinY, w = sideWinW, h = winH}
}
local f2RightOpen = {
    {z = 18,            y = f2WinY, w = sideWinW, h = winH},
    {z = D - 18 - sideWinW, y = f2WinY, w = sideWinW, h = winH}
}

buildWallX(floor2Corner,               W, H2, WALL_T, f2FrontOpen, COL_WALL)
addPanesX(floor2Corner,                WALL_T, f2FrontOpen, 0.15, COL_GLASS)

buildWallX(floor2Corner + v3(0,0,D-WALL_T), W, H2, WALL_T, f2BackOpen,  COL_WALL)
addPanesX(floor2Corner + v3(0,0,D-WALL_T),  WALL_T, f2BackOpen, 0.15, COL_GLASS)

buildWallZ(floor2Corner,               D, H2, WALL_T, f2LeftOpen,  COL_WALL)
addPanesZ(floor2Corner,                WALL_T, f2LeftOpen, 0.15, COL_GLASS)

buildWallZ(floor2Corner + v3(W-WALL_T,0,0), D, H2, WALL_T, f2RightOpen, COL_WALL)
addPanesZ(floor2Corner + v3(W-WALL_T,0,0),  WALL_T, f2RightOpen, 0.15, COL_GLASS)

----------------------------------------------------------------
-- Roof: flat slab with low parapet + stepped crown
----------------------------------------------------------------
local roofY = H1 + H2 + SLAB
local roofCorner = houseCorner + v3(0, roofY, 0)
-- extend roof 3 studs left (X) and 2 studs front (Z)
partFromCorner(
    roofCorner + v3(-3, 0, -2),        -- shift starting corner left and forward
    v3(W + 3, SLAB, D + 2),            -- increase width and depth
    COL_ROOF,
    "Roof_Slab"
)

-- parapet
local parapetH = 3
buildWallX(roofCorner,               W, parapetH, WALL_T, nil, COL_TRIM)
buildWallX(roofCorner + v3(0,0,D-WALL_T), W, parapetH, WALL_T, nil, COL_TRIM)
buildWallZ(roofCorner,               D, parapetH, WALL_T, nil, COL_TRIM)
buildWallZ(roofCorner + v3(W-WALL_T,0,0), D, parapetH, WALL_T, nil, COL_TRIM)

-- small stepped crown on top
for i=0,3 do
    local inset = 2 + i*2
    local crownCorner = roofCorner + v3(inset, parapetH + i*0.5 + 0.5, inset)
    local crownSize   = v3(W - 2*inset, 0.5, D - 2*inset)
    partFromCorner(crownCorner, crownSize, COL_ROOF, "Roof_Crown")
end

----------------------------------------------------------------
-- Porch: slab, columns, awning
----------------------------------------------------------------
local porchDepth, porchWidth = 10, 24
local porchCorner = v3(-porchWidth/2, SLAB, -D/2 - porchDepth)
partFromCorner(porchCorner, v3(porchWidth, 0.6, porchDepth), COL_FLOOR, "Porch_Slab")

-- columns
local colW, colH = 1.2, 8
local colZ = -D/2 - 0.6
local colX1 = -porchWidth/2 + 2
local colX2 =  porchWidth/2 - 2
partFromCorner(v3(colX1, SLAB, colZ), v3(colW, colH, colW), COL_TRIM, "PorchCol1")
partFromCorner(v3(colX2, SLAB, colZ), v3(colW, colH, colW), COL_TRIM, "PorchCol2")
-- awning
local awnY = SLAB + colH
partFromCorner(v3(-porchWidth/2, awnY, -D/2 - porchDepth), v3(porchWidth, 0.6, porchDepth), COL_TRIM, "Porch_Awning")

----------------------------------------------------------------
-- Fence around yard
----------------------------------------------------------------
local fenceInset = 8
local fenceH, fenceT = 4, 0.6
local yardCorner = LOT_CORNER + v3(fenceInset, SLAB, fenceInset)
local yardW = LOT.X - 2*fenceInset
local yardD = LOT.Z - 2*fenceInset

-- posts every 6 studs
local spacing = 6
postsAlongX(yardCorner, math.floor(yardW/spacing)+1, spacing, v3(fenceT, fenceH, fenceT), COL_STEEL, "FencePost")
postsAlongX(yardCorner + v3(0,0,yardD - fenceT), math.floor(yardW/spacing)+1, spacing, v3(fenceT, fenceH, fenceT), COL_STEEL, "FencePost")
postsAlongZ(yardCorner, math.floor(yardD/spacing)+1, spacing, v3(fenceT, fenceH, fenceT), COL_STEEL, "FencePost")
postsAlongZ(yardCorner + v3(yardW - fenceT,0,0), math.floor(yardD/spacing)+1, spacing, v3(fenceT, fenceH, fenceT), COL_STEEL, "FencePost")

-- simple rails
partFromCorner(yardCorner + v3(0, fenceH-0.8, 0), v3(yardW, 0.3, fenceT), COL_STEEL, "RailN")
partFromCorner(yardCorner + v3(0, fenceH-0.8, yardD - fenceT), v3(yardW, 0.3, fenceT), COL_STEEL, "RailS")
partFromCorner(yardCorner + v3(0, fenceH-0.8, 0), v3(fenceT, 0.3, yardD), COL_STEEL, "RailW")
partFromCorner(yardCorner + v3(yardW - fenceT, fenceH-0.8, 0), v3(fenceT, 0.3, yardD), COL_STEEL, "RailE")

----------------------------------------------------------------
-- Simple furniture (axis-aligned)
----------------------------------------------------------------
-- living room couch
local couchCorner = v3(-W/2 + 6, SLAB, -D/2 + 20)
partFromCorner(couchCorner, v3(10, 1.2, 3), COL_DARKWOOD, "CouchBase")
partFromCorner(couchCorner + v3(0, 1.2, 0), v3(10, 2, 0.6), COL_LIGHT, "CouchSeat")
partFromCorner(couchCorner + v3(0, 3.2, 0), v3(10, 2, 0.6), COL_LIGHT, "CouchBack")
partFromCorner(couchCorner + v3(0, 1.2, 2.4), v3(10, 1.5, 0.6), COL_LIGHT, "CouchFront")

-- dining table
local tableCorner = v3(-3, SLAB, -D/2 + 28)
partFromCorner(tableCorner + v3(0, 2.5, 0), v3(8, 0.4, 4), COL_DARKWOOD, "TableTop")
partFromCorner(tableCorner, v3(0.4, 2.5, 0.4), COL_DARKWOOD, "Leg1")
partFromCorner(tableCorner + v3(7.6,0,0), v3(0.4, 2.5, 0.4), COL_DARKWOOD, "Leg2")
partFromCorner(tableCorner + v3(0,0,3.6), v3(0.4, 2.5, 0.4), COL_DARKWOOD, "Leg3")
partFromCorner(tableCorner + v3(7.6,0,3.6), v3(0.4, 2.5, 0.4), COL_DARKWOOD, "Leg4")

-- bed (floor 2)
local bedCorner = floor2Corner + v3(W - 18, 0, 6)
partFromCorner(bedCorner, v3(14, 1, 6), COL_DARKWOOD, "BedBase")
partFromCorner(bedCorner + v3(1, 1, 1), v3(12, 1.2, 4), rgb(220,220,255), "Mattress")
partFromCorner(bedCorner + v3(1, 2.2, 1), v3(12, 0.4, 4), COL_LIGHT, "Sheet")
partFromCorner(bedCorner + v3(2, 2.6, 1), v3(3, 0.8, 4), rgb(250,250,250), "Pillow")
partFromCorner(bedCorner + v3(0, 1, 0), v3(0.6, 4, 6), COL_DARKWOOD, "Headboard")

-- kitchen counter
local counterCorner = v3(-W/2 + 2, SLAB, -D/2 + 2)
partFromCorner(counterCorner, v3(20, 3, 2.5), COL_TRIM, "CounterBase")
partFromCorner(counterCorner + v3(0, 3, 0), v3(20, 0.4, 2.5), COL_LIGHT, "CounterTop")

----------------------------------------------------------------
-- Done
----------------------------------------------------------------
-- === Add-on: gardens, flowers, roses, and sky butterflies (axis-aligned Parts only) ===
-- Paste after your house script. No rotation. Only Part, Position, Color, Size.

local ws = game:GetService("Workspace")
local rs = game:GetService("RunService")
math.randomseed(os.clock())

-- tiny factory
local base = Instance.new("Part")
base.Anchored = true
base.Parent = nil

local function v3(x,y,z) return Vector3.new(x,y,z) end
local function rgb(r,g,b) return Color3.fromRGB(r,g,b) end
local function make(corner,size,color,name)
    local p = base:Clone()
    p.Size = size
    p.Position = corner + (size * 0.5)
    p.Color = color
    p.Name = name or "P"
    p.Parent = ws
    return p
end

-- infer lot and house bounds
local lot = ws:FindFirstChild("Lot")
local lotC, lotW, lotD = v3(0,0,0), 180, 180
if lot then
    lotC = lot.Position
    lotW, lotD = lot.Size.X, lot.Size.Z
end

local slab = ws:FindFirstChild("Floor1_Slab") or ws:FindFirstChild("Foundation")
local hC, hW, hD = v3(0,0,0), 80, 60
local hY = 1
if slab then
    hC = slab.Position
    hW, hD = slab.Size.X, slab.Size.Z
    hY = slab.Position.Y
end

-- colors
local C_SOIL = rgb(110,85,70)
local C_STEM = rgb(40,120,55)
local C_LEAF = rgb(60,150,70)
local C_ROSE = rgb(200,30,60)
local C_DAISY = rgb(240,230,120)
local C_BLUE = rgb(80,120,220)
local C_LAV = rgb(180,140,220)
local C_STONE = rgb(125,125,125)

-- flowers
local function plantStem(corner, h)
    return make(corner, v3(0.3, h, 0.3), C_STEM, "Stem")
end

local function petalCross(topCorner, s, col)
    -- +X, -X, +Z, -Z petals + center
    make(topCorner + v3( 0.35, 0, 0), v3(s, s*0.3, s), col, "Petal")
    make(topCorner + v3(-0.35, 0, 0), v3(s, s*0.3, s), col, "Petal")
    make(topCorner + v3(0, 0,  0.35), v3(s, s*0.3, s), col, "Petal")
    make(topCorner + v3(0, 0, -0.35), v3(s, s*0.3, s), col, "Petal")
    make(topCorner, v3(0.25, 0.25, 0.25), C_DAISY, "Pollen")
end

local function leafPair(midCorner, span)
    make(midCorner + v3(0,0,-span*0.5), v3(0.2,0.2, span), C_LEAF, "Leaf")
    make(midCorner + v3(0,0, span*0.5), v3(0.2,0.2, span), C_LEAF, "Leaf")
end

local function makeDaisy(rootCorner)
    local h = 1.6 + math.random()*0.8
    plantStem(rootCorner, h)
    leafPair(rootCorner + v3(0, h*0.45, 0), 0.8)
    petalCross(rootCorner + v3(0, h, 0), 0.35, (math.random()<0.5) and C_DAISY or C_BLUE)
end

local function makeRose(rootCorner)
    local h = 2.0 + math.random()*0.8
    plantStem(rootCorner, h)
    leafPair(rootCorner + v3(0, h*0.4, 0), 0.9)
    leafPair(rootCorner + v3(0, h*0.7, 0), 0.7)
    -- stacked rose head
    for i=0,3 do
        local s = 0.7 - i*0.12
        make(rootCorner + v3(-s*0.5, h + i*0.12, -s*0.5), v3(s, 0.12, s), C_ROSE, "RosePetal")
    end
end

local function flowerBed(corner, size, density, mix)
    make(corner, v3(size.X, 0.2, size.Z), C_SOIL, "Soil")
    local nx = math.max(1, math.floor(size.X * density))
    local nz = math.max(1, math.floor(size.Z * density))
    for ix=0,nx-1 do
        for iz=0,nz-1 do
            local jitterX = (math.random()-0.5)*0.6
            local jitterZ = (math.random()-0.5)*0.6
            local fx = corner.X + 0.3 + (ix+0.5)*(size.X-0.6)/nx + jitterX
            local fz = corner.Z + 0.3 + (iz+0.5)*(size.Z-0.6)/nz + jitterZ
            if mix == "roses" then
                if math.random()<0.75 then makeRose(v3(fx, corner.Y+0.2, fz)) else makeDaisy(v3(fx, corner.Y+0.2, fz)) end
            elseif mix == "wild" then
                local pick = math.random()
                local root = v3(fx, corner.Y+0.2, fz)
                if pick<0.34 then makeDaisy(root)
                elseif pick<0.67 then
                    petalCross(root + v3(0, 1.2, 0), 0.35, C_LAV)
                    plantStem(root, 1.2); leafPair(root + v3(0,0.6,0), 0.7)
                else makeRose(root) end
            else
                makeDaisy(v3(fx, corner.Y+0.2, fz))
            end
        end
    end
end

local function shortHedge(corner, length)
    local seg = 1.5
    local n = math.floor(length/seg)
    for i=0,n-1 do
        make(corner + v3(i*seg, 0, 0), v3(seg-0.2, 1.1, 1.0), C_LEAF, "Hedge")
    end
end

local function stonesRow(corner, length)
    local step = 2.2
    local n = math.floor(length/step)
    for i=0,n-1 do
        local w = 1.2 + math.random()*0.6
        local d = 1.0 + math.random()*0.6
        make(corner + v3(i*step, 0, 0), v3(w, 0.25, d), C_STONE, "Stone")
    end
end

-- place beds: front, along porch, and side
local frontZ = (hC.Z - (hD*0.5)) - 1.5
local bedW = hW - 8
flowerBed(v3(hC.X - bedW*0.5, hY, frontZ - 4), v3(bedW, 0, 4), 0.6, "roses")
shortHedge(v3(hC.X - bedW*0.5, hY+0.2, frontZ - 0.8), bedW)
stonesRow(v3(hC.X - bedW*0.5, hY+0.2, frontZ - 1.6), bedW)

local leftX = (hC.X - hW*0.5) - 1.5
flowerBed(v3(leftX - 3.5, hY, hC.Z - 18), v3(3.5, 0, 36), 0.55, "wild")

local rightX = (hC.X + hW*0.5) + 1.5
flowerBed(v3(rightX, hY, hC.Z - 24), v3(3.5, 0, 48), 0.5, "wild")

-- small pond with reeds
local pondCorner = v3(hC.X + hW*0.5 + 10, hY, hC.Z + hD*0.25)
make(pondCorner, v3(14, 0.3, 10), rgb(80,140,200), "Pond")
for i=0,7 do
    local px = pondCorner.X + 1 + i*1.5
    plantStem(v3(px, hY+0.3, pondCorner.Z + 1), 2.6)
end

-- path lights
for i=0,6 do
    local x = hC.X - 12 + i*4
    local z = frontZ - 6
    make(v3(x, hY, z), v3(0.4, 3.2, 0.4), C_STEM, "PathPost")
    make(v3(x-0.5, hY+3.2, z-0.5), v3(1.4, 0.4, 1.4), rgb(240,240,210), "Cap")
end

-- butterflies: small Parts flying high, far from house
local BUTTERFLIES = 40
local data = {}

local margin = 30
local halfW = hW*0.5
local halfD = hD*0.5

local function farCenterXY(radius)
    local lotMinX = lotC.X - lotW*0.5 + radius + 5
    local lotMaxX = lotC.X + lotW*0.5 - radius - 5
    local lotMinZ = lotC.Z - lotD*0.5 + radius + 5
    local lotMaxZ = lotC.Z + lotD*0.5 - radius - 5
    for _=1,200 do
        local cx = lotMinX + math.random()*(lotMaxX - lotMinX)
        local cz = lotMinZ + math.random()*(lotMaxZ - lotMinZ)
        local dx = math.abs(cx - hC.X) - (halfW + margin + radius)
        local dz = math.abs(cz - hC.Z) - (halfD + margin + radius)
        if dx > 0 or dz > 0 then
            return cx, cz
        end
    end
    -- fallback corners
    return lotMaxX, lotMaxZ
end

for i=1,BUTTERFLIES do
    local p = base:Clone()
    p.Size = v3(0.8, 0.2, 0.6)
    p.Color = (math.random()<0.5) and rgb(250,170,50) or rgb(200,90,220)
    p.Name = "Butterfly_"..i
    p.Parent = ws

    local radius = 10 + math.random()*18
    local cx, cz = farCenterXY(radius)
    local cy = hY + math.random()*20

    cx = cx - 40
    cz = cz - 130

    p.Position = v3(cx, cy, cz)

    data[p] = {
        c = v3(cx, cy, cz),
        r = radius,
        ax = 0.7 + math.random()*0.8,
        ay = 0.2 + math.random()*0.6,
        az = 0.7 + math.random()*0.8,
        w  = 0.12 + math.random()*0.18,
        t0 = os.clock() + math.random()*10
    }
end

task.spawn(function()
    while true do
        local now = os.clock()
        for p,d in pairs(data) do
            local t = now - d.t0
            local nx = math.cos(t*d.w)*d.r*d.ax
            local ny = math.sin(t*d.w*1.7)*2.5*d.ay + math.sin(t*d.w*0.25)*1.0
            local nz = math.sin(t*d.w*0.9)*d.r*d.az
            local pos = d.c + v3(nx, ny, nz)
            p.Position = pos
            -- wing "flap" by size pulse along X
            local pulse = 0.8 + 0.25*math.sin(t*12 + os.clock() %7)
            p.Size = v3(pulse, 0.2, 0.6)
        end
        rs.RenderStepped:Wait()
    end
end)

-- extra wildflower scatter in yard corners
local function scatterWildflowers(areaCorner, areaSize, count)
    for _=1,count do
        local x = areaCorner.X + math.random()*areaSize.X
        local z = areaCorner.Z + math.random()*areaSize.Z
        local y = areaCorner.Y + 0.2
        local pick = math.random()
        if pick < 0.5 then makeDaisy(v3(x,y,z))
        else
            petalCross(v3(x,y, z) + v3(0, 1.1, 0), 0.32, (math.random()<0.5) and C_LAV or C_BLUE)
            plantStem(v3(x,y,z), 1.1)
        end
    end
end

local yardPad = 10
scatterWildflowers(v3(lotC.X - lotW*0.5 + yardPad, hY, lotC.Z - lotD*0.5 + yardPad), v3(30,0,30), 50)
scatterWildflowers(v3(lotC.X + lotW*0.5 - yardPad - 30, hY, lotC.Z + lotD*0.5 - yardPad - 30), v3(30,0,30), 50)
