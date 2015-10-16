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

local function getOnlineData()
	local inMpc = {
		mpc = 1,
		kpc = .001,
		pc = .000001,
	}

	local distsForOIDs = {}
	local results = querySimbad("select mesDistance.oidref,mesDistance.dist,mesDistance.unit,mesDistance.minus_err,mesDistance.plus_err,BASIC.main_id,BASIC.ra,BASIC.dec from mesDistance inner join BASIC on mesDistance.oidref=BASIC.oid")
	for _,row in ipairs(results.data) do
		local oid = row[1]
		if oid then	
			-- convert to common units
			local unit = row[3]
			if not unit then
				io.stderr:write('failed to find units for oid '..oidref..'\n')
			else
				unit = unit:trim():lower()
				local s = inMpc[unit]
				if not s then error("failed to convert units "..tostring(unit)) end
				local dist = assert(row[2], "better have distance") * s
				local min = row[4] and row[4] * s
				local max = row[5] and row[5] * s
				local err = min and max and .5 * (math.abs(min) + math.abs(max))
				-- just copy these in all distance entries -- so I only have to query once
				local id = row[6]
				local ra = tonumber(row[7])
				local dec = tonumber(row[8])
			
				if ra and dec then
					if not distsForOIDs[oid] then distsForOIDs[oid] = table() end
					distsForOIDs[oid]:insert{dist=dist, err=err, min=min, max=max, id=id, ra=ra, dec=dec}
				end
			end
		end
	end

	local entries = table()
	for oid,distsForOID in pairs(distsForOIDs) do
		local entry = {oid=oid}
		entries:insert(entry)
		
		-- prefer entries with error over those without, pick the smallest error
		distsForOID = distsForOID:sort(function(a,b)
			if a.err and b.err then
				return a.err < b.err
			elseif a.err and not b.err then
				return true
			elseif not a.err and b.err then
				return false
			else
				return a.dist > b.dist
			end
		end)
		local best = distsForOID[1]
		entry.dist = best.dist
		entry.id = best.id
		entry.ra = best.ra
		entry.dec = best.dec
		
		local rad_ra = entry.ra * math.pi / 180
		local rad_dec = entry.dec * math.pi / 180
		local cos_rad_dec = math.cos(rad_dec)
		entry.vtx = {
			entry.dist * math.cos(rad_ra) * cos_rad_dec,
			entry.dist * math.sin(rad_ra) * cos_rad_dec,
			entry.dist * math.sin(rad_dec),
		}
	end

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

local dstfile = ffi.C.fopen('datasets/simbad/points/points.f32', 'wb')
local vtx = ffi.new('float[3]')
local sizeofvtx = ffi.sizeof('float[3]')
local numWritten = 0
for _,entry in ipairs(entries) do
	if entry.dist > .5 then	-- andromeda is .77 mpc ...
		vtx[0], vtx[1], vtx[2] = table.unpack(entry.vtx)
		ffi.C.fwrite(vtx, sizeofvtx, 1, dstfile)
		numWritten = numWritten + 1
	end
end
ffi.C.fclose(dstfile)
print('wrote '..numWritten..' points')
