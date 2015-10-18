-- the database: http://simbad.u-strasbg.fr/simbad/tap/tapsearch.html
return function(query)
	local table = require 'ext.table'
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
