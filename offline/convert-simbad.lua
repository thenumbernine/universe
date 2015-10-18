#!/usr/bin/env lua -lluarocks.require
require 'ext'

-- the database: http://simbad.u-strasbg.fr/simbad/tap/tapsearch.html
local function querySimbad(query)
	local socket = require 'socket'
	local http = require 'socket.http'
	local json = require 'dkjson'
	
	local url = socket.url.build{
		scheme = 'http',
		host = 'simbad.u-strasbg.fr',
		path = '/simbad/sim-tap/sync',
		-- why a table of tables?  because lang must come second...
		query = table{
			{request = 'doQuery'},
			{lang = 'adql'},
			{format = 'json'},	-- votable, json, csv, tsv, text
			-- also don't forget to 
			-- example:
			--{query = ('SELECT TOP 10 MAIN_ID,RA,DEC FROM BASIC WHERE rvz_redshift > 3.3')},
			-- works, to get the id of Andromeda (though the result ID "M 31" doesn't match the queried ID "MESSIER_031"):
			-- NOTICE: use single-quotes for strings within the query
			--{query = "select oidref,id from ident where id='MESSIER_031'"},
			--{query = "select oidref,id from ident where id='M  31'"},	-- this is the ID it gives back
			--{query = "select oidref,id from ident where oidref=1575544},	-- this is the ID it gives back
			--{query = "select oidref,id from ident where id='00424433+4116074'"}, -- 2MRS ID doesn't work for ident.id ...
			--{query = "select oidref,id from ident where id='1991RC3.9.C...0000d'"}, -- how about vsrc? nope.
			--{query = "select oidref,dist,unit,minus_err,plus_err from mesDistance where oidref=1575544"},	-- this is the ID it gives back
			{query = query},
		}:map(function(l)
			local k=next(l)
			local v = socket.url.escape(l[k])
			return k..'='..v
		end):concat('&'),
	}
	--print(url)
	local result = {socket.http.request(url)}
	--print(table.unpack(result))
	if result[2] ~= 200 then
		error("failed to request url: "..url.."\ngot response:\n"..result[1])
	end
	local data = json.decode(result[1])
	--print(tolua(data,{indent=true}))
	return data
end

local convertToMpc = {
	mpc = 1,
	kpc = .001,
	pc = .000001,
	km = 1 / (3261563.79673 * 9460730472580.8),
}

--[[
this uses mesDiameter, which has something like 3340 unique entries
another option is to use basic.galdim_majaxis and basic.galdim_minaxis, but this is only set for about 553 entries (all but 3 also have basic.galdim_angle)
--]]
local function addDiameters(entries)
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
	
	return entries
end

local function getOfflineData()
	local filename = 'datasets/simbad/results.lua'
	if not io.fileexists(filename) then return end
	local entries = table()
	for l in io.lines(filename) do
		if l == '{' or l == '}' or l == '' then
		else
			entries:insert(load('return '..l:sub(1,-2))())
		end
	end
	return entries
end

local entries = getOfflineData() or getOnlineData()

file['datasets/simbad/results.lua'] = '{\n'
	.. entries:map(function(entry) return tolua(entry)..',\n' end):concat() .. '}\n'

local ffi = require 'ffi'
require 'ffi.C.stdio'

entries = entries:filter(function(entry)
	return entry.dist > .1	-- andromeda is .77 mpc ... milky way is .030660137 mpc
end)

-- write out point files
local vtx = ffi.new('float[3]')
local sizeofvtx = ffi.sizeof('float[3]')
local pointFile = ffi.C.fopen('datasets/simbad/points/points.f32', 'wb')
local numWritten = 0
for _,entry in ipairs(entries) do
	vtx[0], vtx[1], vtx[2] = table.unpack(entry.vtx)
	ffi.C.fwrite(vtx, sizeofvtx, 1, pointFile)
	numWritten = numWritten + 1
end
ffi.C.fclose(pointFile)
print('wrote '..numWritten..' universe points')

-- write out catalog data and spec files
local cols = table{'id','otype'}
local colmaxs = cols:map(function(col)
	return entries:map(function(entry)
		return entry[col] and #tostring(entry[col]) or 0
	end):sup(), col
end)
file['datasets/simbad/catalog.specs'] = cols:map(function(col)
	return col..'='..colmaxs[col]
end):concat'\n'
local catalogFile = ffi.C.fopen('datasets/simbad/catalog.dat', 'wb')
local tmplen = colmaxs:sup()+1
local tmp = ffi.new('char[?]', tmplen)
for _,entry in ipairs(entries) do
	for _,col in ipairs(cols) do
		ffi.fill(tmp, tmplen)
		if entry[col] then ffi.copy(tmp, tostring(entry[col])) end
		ffi.C.fwrite(tmp, colmaxs[col], 1, catalogFile)
	end
end
ffi.C.fclose(catalogFile)

