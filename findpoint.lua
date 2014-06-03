module(..., package.seeall)

local json = require 'dkjson'
require 'ext'
require 'wsapi.request'

function run(env)
	local headers = { ["Content-type"] = "text/javascript" }
	local req = wsapi.request.new(env)
	local ident = req.GET and req.GET.ident
	
	-- TODO different catalogs based on different sets 
	local catalogSpecs = assert(loadstring('return ' .. io.readfile('2mrs-catalog.specs')))()

	local rowsize = 0
	for _,col in pairs(catalogSpecs) do
		rowsize = rowsize + col[2]
	end

	local function text()
		local indexes = table()
		--pointID is 0-based
		if ident then
			--local identparts = ident:split('%s+'):map(function(s) return s:lower() end)
			local specialChars = {
				['^']=true,
				['$']=true,
				['(']=true,
				[')']=true,
				['%']=true,
				['.']=true,
				['[']=true,
				[']']=true,
				['*']=true,
				['+']=true,
				['-']=true,
				['?']=true,
			}
			ident = ident:gsub('.', function(c) 
				if specialChars[c] then return '%'..c end
				-- spaces are wildcards to work around the _-spacing in the catalog 
				-- while you're at it, skip over leading 0's of 2nd parts of titles
				if c == ' ' then return '.0*' end
				return c
			end)
			-- TODO use the fits file, or whatever other constant-time access method.  the rows of the text file look equal, but there's a few outliers
			local f = assert(io.open('2mrs-catalog.dat', 'rb'))
			local lineno = 0
			repeat
				local l = f:read(rowsize)
				if not l or #l < rowsize then break end
				local loc = l:lower():find(ident)
				if loc then	-- poor way to column test
					indexes:insert(lineno)
				end
				lineno = lineno + 1
			until false
			f:close()
			coroutine.yield(json.encode{indexes=indexes, ident=ident})
		else
			coroutine.yield(json.encode{ident=ident, error='failed to interpret ident parameter'})
		end
	end
	
	return 200, headers, coroutine.wrap(text)
end
