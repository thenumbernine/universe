local arg = {...}

require 'ext'


local function exec(...)
	print('#', ...)
	os.execute(...)
end

local function getsetname(i)
	if i == 1 then return '2mrs' end
	return '2mrs-c'..i
end

local opt = arg[1]
local opts = {
	make = function()
		m = tonumber(arg[2])
		assert(m and m > 0, 'expected number of iterations')
		n = tonumber(arg[3])
		assert(n and n > 0, 'expected number of iterations')

		for i=m,n do
			local setname = getsetname(i)
			local prevsetname = getsetname(i-1)
			if i == 1 then
				if not path('datasets/'..setname..'/points/points.f32'):exists() then
					exec('convert-2mrs')	-- makes datasets/2mrs/points/*.f32 
				end
			else
				if not path('datasets/'..setname..'/points/points.f32'):exists() then
					path('datasets/'..setname):mkdir()
					path('datasets/'..setname..'/points'):mkdir()
					path('datasets/'..setname..'/stats'):mkdir()
					exec('copy datasets/'..prevsetname..'/points/* datasets/'..setname..'/points/')
					exec('flatten-clusters --set '..setname)
				end
			end
			exec('getstats --set '..setname..' --all')	--makes datasets/<setname>/stats/*.f32
			exec('gettotalstats --set '..setname)	--makes datasets/<setname>/stats/total.f32
			exec('mark-clusters --set '..setname)	--makes datasets/<setname>/points/*.clusters,*.links
		end
	end,
	show = function()
		m = tonumber(arg[2])
		assert(m and m > 0, 'expected number of iterations')
		n = tonumber(arg[3])
		assert(n and n > 0, 'expected number of iterations')
		
		local setnames = table()
		for i=m,n do
			setnames:insert(getsetname(i))	
		end
		exec('show '..setnames:map(function(name) 
			return '--set '..name..' --all'
		end):concat(' '))
	end,
}
assert(opts[opt], 'failed to find option')()
