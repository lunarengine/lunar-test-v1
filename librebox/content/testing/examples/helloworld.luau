-- If this does not print "Hello World", something
-- is seriously wrong

print("Hello World")

-- Print some other stuff

print(script.Parent) -- should be nil

script.Parent = game.Workspace

print(script.Parent == game.Workspace) -- should be true
