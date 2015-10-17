local json = require 'dkjson'
require 'ext'
local wsapi_request = require 'wsapi.request'

return {
	run = function(env)
		local headers = { ["Content-type"] = "text/javascript" }
		local req = wsapi_request.new(env)
		local ident = req.GET and req.GET.ident
		
		local catalogSpecs, rowsize = require 'catalog_specs' 'simbad'	

		local function text()
			local indexes = table()
			--pointID is 0-based
			if ident then
				ident = ident:lower()
				ident = ident:gsub('%s+', ' ')
				-- TODO use the fits file, or whatever other constant-time access method.  the rows of the text file look equal, but there's a few outliers
				local f = assert(io.open('simbad-catalog.dat', 'rb'))
				local lineno = 0
				repeat
					local l = f:read(rowsize)
					if not l or #l < rowsize then break end
					l = l:gsub('%s+', ' ')
					local loc = l:lower():find(ident)
					if loc then	-- poor way to column test
						indexes:insert(lineno)
					end
					lineno = lineno + 1
				until false
				f:close()
				coroutine.yield(json.encode({indexes=indexes, ident=ident}, {indent=true}))
			else
				coroutine.yield(json.encode({ident=ident, error='failed to interpret ident parameter'}, {indent=true}))
			end
		end
		
		return 200, headers, coroutine.wrap(text)
	end,
}

