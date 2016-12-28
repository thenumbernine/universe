#!/usr/bin/env lua -lluarocks.require
require 'ext'

local querySimbad = require 'query-simbad'

--print(tolua(querySimbad("select oidref,id from ident where id = 'NGC_0253'"),{indent=true})) os.exit()		-- "NGC   253"

io.stderr:write('getting cat_ids...\n')
local entries = table()
local indexForColumnNames = {}
for l in io.lines'datasets/2mrs/source/2mrs_v240/catalog/2mrs_1175_done.dat' do
	if l:sub(1,1) == '#' then
		if l:sub(1,3) == '#ID' then
			local ws = l:sub(2):split'%s+'
			for i,w in ipairs(ws) do
				indexForColumnNames[w:lower()] = i
			end
		end
	else
		local ws = l:split'%s+'
		local entry = {}
		for key,index in pairs(indexForColumnNames) do
			entry[key] = ws[index]
		end
		entries:insert(entry)
	end
end
io.stderr:write('found '..#entries..' ids\n')

local batchSize = 200
--entries = entries:sub(1,batchSize) -- truncate to 1 batch

local altNames = {
	MESSIER_051a = 'NGC_5194',
	MESSIER_051b = 'NGC_5195',
	VV_820_NED02 = 'MCG+00-43-003',
	IC_4791 = '2MASX J18490117+1919518',
	NGC_1671 = 'IC  395',
	NGC_0006 = 'NGC_0020',
	NGC_1040 = 'NGC_1053',
}

io.stderr:write('getting oids...\n')
local oids = table()
for i=1,#entries,batchSize do
	local startIndex = i
	local endIndex = math.min(#entries, startIndex+batchSize-1)
	local catIDBatch = entries:sub(startIndex, endIndex):map(function(entry) return entry.cat_id end)
io.stderr:write('querying ids from '..startIndex..' to '..endIndex..'\n')
--io.stderr:write('querying batch: '..catIDBatch:concat(', ')..'\n')
	local fixedCatIDBatch = catIDBatch:map(function(id)
		id = altNames[id] or id
	
		id = id:match('(.*)_NED%d%d$') or id	-- remove _NED## suffix
		id = id:match('(.*)_NOTES%d%d$') or id	-- remove _NOTES## suffix

		local zeros, mid = id:match('^MESSIER_(0*)(%d%d-)$')
		if mid then return 'M '..(' '):rep(#zeros)..mid end
		
		local zeros, ngcid = id:match('^NGC_(0*)(%d[0-9,A-Z]-)$')
		if ngcid then return'NGC  '..(' '):rep(#zeros)..ngcid end
		
		local nid = id:match('^N(%d+)$')
		if nid then return 'NGC  '..nid end

		local zeros, icid = id:match('^IC_(0*)(%d[0-9,A-Z]-)$')
		if icid then return 'IC '..(' '):rep(#zeros)..icid end
		
		local zeros, ugcid = id:match('^UGC_(0*)(%d%d-)$')
		if ugcid then return 'UGC '..(' '):rep(#zeros)..ugcid end
		
		local ugcaid = id:match('^UGCA_(%d+)$')
		if ugcaid then return 'UGCA '..ugcaid end

		local apgid = id:match('^ARP_(%d+)$')
		if apgid then return 'APG '..apgid end

		local zeros, esoid, gid = id:match('^ESO_(0*)(%d+)-[_I][_G][_?](%d+)$')
		if esoid and gid then return 'ESO '..(' '):rep(#zeros)..esoid..'-'..tonumber(gid) end

		local _2masxid = id:match('^2MASX_(.*)$')
		if _2masxid then return '2MASX '.._2masxid end

		local mcgid = id:match('^MCG_(.*)$')
		if mcgid then return 'MCG'..mcgid end

		local _2mfgcid = id:match('^2MFGC_(%d+)$')
		if _2mfgcid  then return '2MFGC '.._2mfgcid end

		local zeros, mrkid = id:match('^MRK_(0*)(%d+)$')
		if mrkid then return 'Mrk '..(' '):rep(#zeros)..mrkid end

		return id
	end)
--io.stderr:write('fixed ids: '..fixedCatIDBatch:concat(', ')..'\n')
	local results = querySimbad("select id,oidref from IDENT where id in ("
		..fixedCatIDBatch:map(function(id)  return "'"..id.."'" end):concat','..")")
	-- i hate sql.  how do you get it to go through a list of strings and return an associated list of ids? 'in' just leaves out entries it doesnt find. then the results arent in order.
	-- create a mapping from ids to rows
	local oidForID = {}
	for j,row in ipairs(results.data) do
		oidForID[row[1]] = row[2]
	end
	-- make sure we got all our entries
	for j=startIndex,endIndex do
		local id = fixedCatIDBatch[j-startIndex+1]
		if not oidForID[id] then
			-- don't bother report on these ...
			if not id:match('^g'..('%d'):rep(7)..'%-'..('%d'):rep(6)..'$')
			and not id:match('^'..('%d'):rep(8)..'[+-]'..('%d'):rep(7)..'$')
			and not id:match('^A'..('%d'):rep(6)..'%.%d[+-]'..('%d'):rep(6)..'$')
			and not id:match('^A'..('%d'):rep(4)..'[+-]'..('%d'):rep(4)..'$')
			and not id:match('^WKK_%d%d%d%d$')
			and not id:match('^CGCG_%d%d%d%-%d%d%d$')
			and not id:match('^I%d%d%d%d$')
			then
				io.stderr:write("failed to find oid for id "..catIDBatch[j]..' => '..fixedCatIDBatch[j]..'\n')
			end
		else
			entries[j].oid = oidForID[id]
		end
	end
end
io.stderr:write('done getting oids\n')

-- now we've got our entries and our oids
-- now to query lat, lon, and distances
io.stderr:write('getting ra/dec...\n')
for i=1,#entries,batchSize do
	local startIndex = i
	local endIndex = math.min(#entries, startIndex+batchSize-1)
	local oidBatch = entries:sub(startIndex, endIndex):map(function(entry, _, t) return entry.oid, #t+1 end)
io.stderr:write('querying ids from '..startIndex..' to '..endIndex..'\n')

	local query = "select oid,ra,dec from BASIC where oid in ("
		..oidBatch:filter(function(oid) return type(oid) == 'number' end):concat', '..")"
	local results = querySimbad(query)
	local raDecForOIDs = {}
	for j,row in ipairs(results.data) do
		local oid, ra, dec = table.unpack(row)
		raDecForOIDs[oid] = {ra=ra, dec=dec}
	end

	for j=startIndex,endIndex do
		local entry = entries[j]
		local oid = entry.oid
		if oid then
			if raDecForOIDs[oid] then
				entry.ra = raDecForOIDs[oid].ra
				entry.dec = raDecForOIDs[oid].dec
			end
		end
	end
end
io.stderr:write('done getting ra/decs\n')

local inMpc = {
	mpc = 1,
	kpc = .001,
	pc = .000001,
}

io.stderr:write('getting distances...\n')
local dists = table()
for i=1,#entries,batchSize do
	local startIndex = i
	local endIndex = math.min(#entries, startIndex+batchSize-1)
	local oidBatch = entries:sub(startIndex, endIndex):map(function(entry,_,t) return entry.oid,#t+1 end)
io.stderr:write('querying ids from '..startIndex..' to '..endIndex..'\n')

	local results = querySimbad("select oidref,dist,unit,minus_err,plus_err from mesDistance where oidref in ("
		..oidBatch:filter(function(oid) return type(oid) == 'number' end):concat', '..")")
	local distsForOIDs = {}
	for j,row in ipairs(results.data) do
		local oid = row[1]
		if not distsForOIDs[oid] then distsForOIDs[oid] = table() end
		
		-- convert to common units
		local s = inMpc[row[3]:trim():lower()] or error("failed to convert units "..tostring(row[3]))
		local dist = assert(row[2], "better have distance") * s
		local min = row[4] and row[4] * s
		local max = row[5] and row[5] * s
		local err = min and max and .5 * (math.abs(min) + math.abs(max))
		distsForOIDs[oid]:insert{dist=dist, err=err, min=min, max=max}
	end
	for j=startIndex,endIndex do
		local entry = entries[j]
		local oid = entry.oid
		if oid then
			local distsForOID = distsForOIDs[oid]
			if distsForOID then
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
				entry.dist = distsForOID[1].dist
			end	
		end
	end
end
io.stderr:write('done getting distances\n')

for i,entry in ipairs(entries) do
	local catID = entry.cat_id
	local oid = entry.oid
	local ra = entry.ra
	local dec = entry.dec
	local dist = entry.dist
	local vtx
	if ra and dec and dist and type(dist) == 'number' then
		local rad_ra = ra * math.pi / 180
		local rad_dec = dec * math.pi / 180
		local cos_rad_dec = math.cos(rad_dec)
		vtx = {
			dist * math.cos(rad_ra) * cos_rad_dec,
			dist * math.sin(rad_ra) * cos_rad_dec,
			dist * math.sin(rad_dec),
		}
		print(tolua{oid=oid, catID=catID, dist=dist, ra=ra, dec=dec, vtx=vtx}..',')
	end
end
