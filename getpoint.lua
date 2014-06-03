module(..., package.seeall)

local json = require 'dkjson'
require 'ext'
require 'wsapi.request'


function run(env)
	local headers = { ["Content-type"] = "text/javascript" }
	local req = wsapi.request.new(env)
	local pointIndex = req.GET and tonumber(req.GET.point)

	-- TODO different catalogs based on different sets 
	local catalogSpecs = assert(loadstring('return ' .. io.readfile('2mrs-catalog.specs')))()

	local rowsize = 0
	for _,col in pairs(catalogSpecs) do
		rowsize = rowsize + col[2]
	end
	
	local function text()
		if not pointIndex or pointIndex < 0 then
			coroutine.yield('got bad point '..tostring(pointIndex))
		else
			local results = {}
			-- TODO use the fits file, or whatever other constant-time access method.  the rows of the text file look equal, but there's a few outliers
			local f = assert(io.open('2mrs-catalog.dat', 'rb'))
			f:seek('set', pointIndex * rowsize)
			for _,col in ipairs(catalogSpecs) do
				local key, size = unpack(col)
				size = tonumber(size)
				local value = f:read(size):match('[^%z]*')	-- trim \0's 
				if not value then
					error('failed to read for point '..tostring(pointIndex))
				end
				results[key] = value
			end
			f:close()
			
			coroutine.yield(json.encode(results))
		end
	end
	
	return 200, headers, coroutine.wrap(text)
end
