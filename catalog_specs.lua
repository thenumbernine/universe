return function(setname)
	local catalogSpecs = {}
	for line in io.lines(setname..'-catalog.specs') do
		if #line > 0 then
			local k,v = line:match'(.-)=(.*)'
			assert(k and v, "got a bad line in the catalog: "..tostring(line))
			table.insert(catalogSpecs, {k,v})
		end
	end
	
	local rowsize = 0
	for _,col in pairs(catalogSpecs) do
		rowsize = rowsize + col[2]
	end
	
	return catalogSpecs, rowsize
end
