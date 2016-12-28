#! /usr/bin/env lua
local querySimbad = require 'query-simbad'

local file = require 'ext.file'
local results = querySimbad("select otype_shortname,otype_longname from otypedef")
local t = {}
for _,row in ipairs(results.data) do
	t[row[1]] = row[2]
end
local json = require 'dkjson'
file['../otypedescs.js'] = 'var otypeDescs = '..json.encode(t,{indent=true})

