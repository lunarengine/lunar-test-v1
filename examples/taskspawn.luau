-- task.spawn demonstration

-- Thread 1: prints every 1 second
task.spawn(function()
	while true do
		print("hi (1s)")
		task.wait(1) -- wait 1 second
	end
end)

-- Thread 2: prints every 0.25 seconds
task.spawn(function()
	while true do
		print("hi (0.25s)")
		task.wait(0.25) -- wait 0.25 seconds
	end
end)
