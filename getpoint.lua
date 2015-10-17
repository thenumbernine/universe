local json = require 'dkjson'
require 'ext'
local wsapi_request = require 'wsapi.request'

return {
	run = function(env)
		local headers = { ["Content-type"] = "text/javascript" }
		local req = wsapi_request.new(env)
		local dataset = req.GET and assert(req.GET.set)
		local pointIndex = req.GET and assert(tonumber(req.GET.point))

		local catalogSpecs, rowsize = require 'catalog_specs'(dataset)
	
		local function text()
			if not pointIndex or pointIndex < 0 then
				coroutine.yield('got bad point '..tostring(pointIndex)..' from request '..tolua(req))
			else
				local results = {}
				-- TODO use the fits file, or whatever other constant-time access method.  the rows of the text file look equal, but there's a few outliers
				local f = assert(io.open(dataset..'-catalog.dat', 'rb'))
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
				
				coroutine.yield(json.encode(results, {indent=true}))
			end
		end
		
		return 200, headers, coroutine.wrap(text)
	end
}

