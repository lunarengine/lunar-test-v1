print("Start")

-- Schedule a function to run after 2 seconds
task.delay(2, function()
	print("B")
end)

-- Schedule another after 0.5 seconds
task.delay(0.5, function()
	print("A")
end)

print("Order: A, B")
