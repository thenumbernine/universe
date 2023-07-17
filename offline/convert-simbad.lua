#!/usr/bin/env lua
require 'ext'

local convertToMpc = {
	mpc = 1,
	kpc = .001,
	pc = .000001,
	km = 1 / (3261563.79673 * 9460730472580.8),
}

local filename = 'datasets/simbad/results.lua'

--[[
this uses mesDiameter, which has something like 3340 unique entries
another option is to use basic.galdim_majaxis and basic.galdim_minaxis, but this is only set for about 553 entries (all but 3 also have basic.galdim_angle)
--]]
local function addDiameters(entries)
	local querySimbad = require 'query-simbad'
	-- now query diameters..
	local diamsForOIDs = {}
	local lastOIDMax = 0
print('querying diameters...')
	while true do
		local query = "select oidref,diameter,unit,error from mesDiameter where oidref > "..lastOIDMax.." order by oidref asc"
		local results = querySimbad(query)
		if #results.data == 0 then break end
		for _,row in ipairs(results.data) do
			local oid = row[1]
			if oid then
				lastOIDMax = math.max(lastOIDMax, oid)
				local unit = row[3]
				local diam = row[2]
				if not unit then
					io.stderr:write('failed to find units for oid '..oidref..'\n')
				elseif not diam then
					io.stderr:write('failed to find diam for oid '..oidref..'\n')
				else
					unit = unit:trim():lower()
					if not (unit == 'km' or unit == 'mas') then
						io.stderr:write('unknown diameter units: '..unit)
					else
						if not diamsForOIDs[oid] then diamsForOIDs[oid] = table() end
						diamsForOIDs[oid]:insert{diam=diam,unit=unit,err=err}
					end
				end
			end
		end
	end

print('sorting and applying...')
	for oid,diamsForOID in pairs(diamsForOIDs) do
		local _, entry = entries:find(nil, function(entry) return entry.oid == oid end)
		if entry then
					
			diamsForOID = diamsForOID:sort(function(a,b)
				if a.units ~= b.units then
					if a.units == 'km' and b.units == 'mas' then return true end
					if a.units == 'mas' and b.units == 'km' then return false end
					error("shouldn't get here")
				else
					if a.err and b.err then
						return a.diam < b.diam
					elseif a.err and not b.err then
						return true
					elseif not a.err and b.err then
						return false
					else
						return a.diam > b.diam
					end
				end
			end)
			local best = diamsForOID[1]

			if best.unit == 'km' then
				entry.diam = best.diam * convertToMpc.km
			elseif best.unit == 'mas' then -- micro-arc-seconds
				if entry.dist then
					entry.diam = entry.dist * best.diam * 4.8481368e-9	-- radians per mas
				end
			else
				error("shouldn't get here")
			end
		end
	end
print('done with diameters')
end

local function getOnlineData()
	local querySimbad = require 'query-simbad'
	local distsForOIDs = {}
	local lastOIDMax = 0
	while true do
		local results = querySimbad(
			"select "
			.."mesDistance.oidref,mesDistance.dist,mesDistance.unit,mesDistance.minus_err,mesDistance.plus_err,"
			.."basic.main_id,basic.ra,basic.dec,"
			.."otypedef.otype_shortname "
			.."from mesDistance "
			.."inner join basic on mesDistance.oidref=basic.oid "
			.."inner join otypedef on basic.otype=otypedef.otype "
			.."where oidref > "..lastOIDMax.." "
			.."and mesDistance.dist is not null "	-- there are 2 null distance entries.  out of a few hundred thousand.  hmm.
			.."order by oidref asc")
		if #results.data == 0 then break end
		for _,row in ipairs(results.data) do
			local oid = row[1]
			if oid then	
				lastOIDMax = math.max(lastOIDMax, oid)
				-- convert to common units
				local unit = row[3]
				if not unit then
					io.stderr:write('failed to find units for oid '..oidref..'\n')
				else
					unit = unit:trim():lower()
					local s = convertToMpc[unit]
					if not s then error("failed to convert units "..tostring(unit)) end
					local dist = assert(row[2], "better have distance") * s
					local dist_err_min = row[4] and row[4] * s
					local dist_err_max = row[5] and row[5] * s
					local dist_err = dist_err_min and dist_err_max and .5 * (math.abs(dist_err_min) + math.abs(dist_err_max))
					-- just copy these in all distance entries -- so I only have to query once
					local id = row[6]
					local ra = tonumber(row[7])
					local dec = tonumber(row[8])
					local otype = row[9]

					if ra and dec then
						if not distsForOIDs[oid] then distsForOIDs[oid] = table() end
						distsForOIDs[oid]:insert{dist=dist, dist_err=dist_err, oid=oid, id=id, otype=otype, ra=ra, dec=dec}
					end
				end
			end
		end
	end

	local entries = table()
	for oid,distsForOID in pairs(distsForOIDs) do
		-- prefer entries with error over those without, pick the smallest error
		distsForOID = distsForOID:sort(function(a,b)
			if a.dist_err and b.dist_err then
				return a.dist_err < b.dist_err
			elseif a.dist_err and not b.dist_err then
				return true
			elseif not a.dist_err and b.dist_err then
				return false
			else
				return a.dist > b.dist
			end
		end)
		local entry = distsForOID[1]
		entry.dist_err = nil	-- don't need that anymore
		entries:insert(entry)

		local rad_ra = entry.ra * math.pi / 180
		local rad_dec = entry.dec * math.pi / 180
		local cos_rad_dec = math.cos(rad_dec)
		entry.vtx = {
			entry.dist * math.cos(rad_ra) * cos_rad_dec,
			entry.dist * math.sin(rad_ra) * cos_rad_dec,
			entry.dist * math.sin(rad_dec),
		}
	end

	addDiameters(entries)
	
	path'datasets/simbad':mkdir(true)
	-- save with one record on one line ... because lua can't handle the whole thing at once
	path(filename):write(
		'{\n'
		.. entries:map(function(entry)
			return tolua(entry, {indent=false})..',\n'
		end):concat() .. '}\n'
	)
	return entries
end

if not path(filename):exists() then
	-- writing in Lua was able to work without running out of memory, but reading in Luajit doesn't ...
	getOnlineData()
end

-- parse this one line at a time because lua can't handle the whole thing at once
local function iterateOfflineData()
	for l in io.lines(filename) do
		if l == '{' or l == '}' or l == '' then
		else
			local entry = assert(fromlua(l:sub(1,-2)))
			coroutine.yield(entry)
		end
	end
end

--local json = require 'dkjson'

local oTypes = fromlua(path'../otypedescs.lua':read())
local galaxyOTypes = table.map(
	oTypes, function(v,k,t)
		v = v:lower()
		if v:find('galaxy',1,true) or v:find('galaxies',1,true) then
			return true,k
		end
	end)

local function filter(entry)
	return 
	-- filter out points inside of our galaxy?
	-- andromeda is .77 mpc ... milky way is .030660137 mpc
	entry.dist > .1
	-- filter by otype
	and galaxyOTypes[entry.otype]
end

path'datasets/simbad/points':mkdir()

if not path'datasets/simbad/points/points.f32':exists() then
	local ffi = require 'ffi'
	require 'ffi.c.stdio'
	print'writing out point file...'
	local pointFile = assert(ffi.C.fopen('datasets/simbad/points/points.f32', 'wb'))
	local numWritten = 0
	local vtx = ffi.new('float[3]')
	local sizeofvtx = ffi.sizeof('float[3]')
	-- write out point files
	for entry in coroutine.wrap(iterateOfflineData) do
		if filter(entry) then 
			vtx[0], vtx[1], vtx[2] = table.unpack(entry.vtx)
			ffi.C.fwrite(vtx, sizeofvtx, 1, pointFile)
			numWritten = numWritten + 1
		end
	end
	ffi.C.fclose(pointFile)
	print('wrote '..numWritten..' universe points')
end

local cols = table{'id','otype'}
local colmaxs
if not path'datasets/simbad/catalog.specs':exists() then
	print'writing out catalog spec file...'
	-- write out catalog data and spec files
	colmaxs = cols:map(function(col)
		return 0, col
	end)
	for entry in coroutine.wrap(iterateOfflineData) do
		if filter(entry) then 
			-- determine col value max length while we are here
			for _,col in ipairs(cols) do
				if entry[col] then
					colmaxs[col] = math.max(colmaxs[col], #tostring(entry[col]) or 0)
				end
			end
		end
	end
	path'datasets/simbad/catalog.specs':write(tolua(colmaxs))
else
	colmaxs = assert(fromlua(path'datasets/simbad/catalog.specs':read()), "failed to load colmaxs")
end

if not path'datasets/simbad/catalog.dat':exists() then
	local ffi = require 'ffi'
	require 'ffi.c.stdio'
	local catalogFile = assert(ffi.C.fopen('datasets/simbad/catalog.dat', 'wb'))
	local tmplen = colmaxs:sup()+1
	local tmp = ffi.new('char[?]', tmplen)
	for entry in coroutine.wrap(iterateOfflineData) do
		if filter(entry) then
			for _,col in ipairs(cols) do
				ffi.fill(tmp, tmplen)
				if entry[col] then ffi.copy(tmp, tostring(entry[col])) end
				ffi.C.fwrite(tmp, colmaxs[col], 1, catalogFile)
			end
		end
	end
	ffi.C.fclose(catalogFile)
end

if not path'datasets/simbad/catalog.lua':exists() then
	print'writing out catalog file in lua format...'
	--[[
	local json = require 'dkjson'
	path'datasets/simbad/catalog.json':write(json.encode(setmetatable(entries:map(function(entry)
		local result = {}
		for _,col in ipairs(cols) do
			result[col] = entry[col]
		end
		return result
	end),nil),{indent=true}))
	--]]

	-- TODO convert this separately, because luajit and luarocks aren't being friendly
	local f = assert(path'datasets/simbad/catalog.lua':open'w')
	f:write'{\n'
	for entry in coroutine.wrap(iterateOfflineData) do
		if filter(entry) then
			local result = {}
			for _,col in ipairs(cols) do
				result[col] = entry[col]
			end
			f:write('\t'..tolua(result, {indent=false})..',\n')
		end
	end
	f:write'}\n'
	f:close()
end

if not path'datasets/simbad/catalog.json':exists() then
	print'writing out catalog file in json format...'
	local json = require 'dkjson'
	path'datasets/simbad/catalog.json':write(json.encode(fromlua(path'datasets/simbad/catalog.lua':read())))
end

print'done!'
