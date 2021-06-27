#!/usr/bin/env luajit
-- simple pointcloud visualizer
local class = require 'ext.class'
local table = require 'ext.table'
local range = require 'ext.range'
local string = require 'ext.string'
local math = require 'ext.math'
local file = require 'ext.file'
local fromlua = require 'ext.fromlua'
local ffi = require 'ffi'
local template = require 'template'
local Image = require 'image'
local gl = require 'gl'
local glreport = require 'gl.report'
local GLElementArrayBuffer = require 'gl.elementarraybuffer'
local GLProgram = require 'gl.program'
local GLFBO = require 'gl.fbo'
local GLTex2D = require 'gl.tex2d'
local GLHSVTex = require 'gl.hsvtex'
local GLArrayBuffer = require 'gl.arraybuffer'
local CLEnv = require 'cl.obj.env'
local vec3f = require 'vec-ffi.vec3f'
local vec3d = require 'vec-ffi.vec3d'
local quatd = require 'vec-ffi.quatd'
local vector = require 'ffi.cpp.vector'
local matrix_ffi = require 'matrix.ffi'
matrix_ffi.real = 'float'	-- default matrix_ffi type

--[[
set = directory to use if you don't specify individual files:

filename = filename for our point data
namefile = lua file mapping indexes to names of stars
consfile = lua file containing constellation info for star points

format = whether to use xyz or our 9-col format
lummin = set this to filter out for min value in solar luminosity
lumhicount = show only this many of the highest-luminosity stars
appmaghicount = show only this many of the highest apparent magnitude stars
rlowcount = show only this many of the stars with lowest r
rmax = filter out points further than this value
print = set this to print out all the remaining points
nolumnans = remove nan luminosity ... not needed, this was a mixup of abs mag and lum, and idk why abs mag had nans (maybe it too was being calculated from lums that were abs mags that had negative values?)
addsun = add our sun to the dataset.  I don't see it in there.
nounnamed = remove stars that don't have entries in the namefile
buildNbhds = build neighbhoods
buildVels
--]]
local cmdline = require 'ext.cmdline'(...)

--local set = cmdline.set or 'gaia'
local set = cmdline.set or 'hyg'

local filename = cmdline.filename or ('datasets/'..set..'/points/points-9col.f32')
local namefile = cmdline.namefile or ('datasets/'..set..'/namedStars.lua')
local constellationFile = cmdline.consfile or ('datasets/'..set..'/constellations.lua')

local format = cmdline.format or filename:match'%-(.*)%.f32$' or '3col'

-- lua table mapping from index to string
-- TODO now upon mouseover, determine star and show name by it
local names
if namefile then
	local namedata = file[namefile]
	if namedata then
		names = fromlua(namedata)
	end
end

-- this file holds info for the constellation field value of the pt_t
-- it also holds the 'indexes' but this is a bad idea for larger datasets
-- hmm, how about I don't store constellation per-point/ channels at all? 
-- and instead I just store regions of the sky?
local constellations
if constellationFile then
	local consdata = file[constellationFile]
	if consdata then
		constellations = fromlua(consdata)
		constellations = table.sort(constellations, function(a,b)
			return (a.name or ''):lower() < (b.name or ''):lower()
		end)
		for _,constellation in ipairs(constellations) do
			constellation.enabled = false
		end
--[[ shuffling is great for getting asosciated colors unique
-- but it will ruin the association with the 'constellation' channel in the pt_t buffer
-- so TODO instead, how about remapping via a table / texture ?		
-- though I am leaning away from the 'constellation' channel, since larger databases like Gaia do not have it		
		if constellations then
-- idk if it's just my list, but when i color hue by constellation, i get a rainbow
-- and i don't want a rainbow, i want discernible colors
-- so here i'm shuffling it
			local baseStars
			if constellations[1].name == nil then
				baseStars = table.remove(constellations, 1)
			end
			constellations = table.shuffle(constellations)
			-- keep the base stars at the first index
			if baseStars then
				constellations:insert(1, baseStars)
			end
		end
--]]		
		print('loaded '..#constellations..' constellations')
	end
end

local App = class(require 'glapp.orbit'(require 'imguiapp'))
local ig = require 'ffi.imgui'

App.title = 'pointcloud visualization tool'
App.viewDist = 5e-4

--[[
my gaia data: 
	float pos[3]
	float vel[3]
	float luminosity
	float temperature
	float radius
	TODO uint64_t source_id
my hyg data:
	float pos[3]
	float vel[3]
	float luminosity
	float temperature
	float constellationIndex
--]]
ffi.cdef[[
typedef struct {
	vec3f_t pos;
} pt_3col_t;

typedef struct {
	vec3f_t pos;		// in Pc
	vec3f_t vel;
	
	/*
	solar luminosity
	https://en.wikipedia.org/wiki/Solar_luminosity
	3.828e+26 watts
	total emitted over the whole surface 
	L_sun = 4 pi k I_sun A^2 
		for L_sum solar luminosity (watts)
		and I_sun solar irradiance (watts/meter^2)
	
	https://en.wikipedia.org/wiki/Luminosity	
	
	flux = luminosity / area
	area of sphere = 4 pi r^2
	area normal with observer = 2 pi r
	flux = luminosity / (2 pi r)
	
	absolute magnitude:
	MBol = -2.5 * log10( LStar / L0 ) ~ -2.5 * log10 LStar + 71.1974
	MBolStar - MBolSun = -2.5 * log10( LStar / LSun )
	absolute bolometric magnitude of sun: MBolSun = 4.74
	apparent bolometric magnitude of sun: mBolSun = -26.832
	(negative = brighter)

	*/
	float lum;

	/*
	temperature, in Kelvin
	*/
	float temp;

	//TODO
	//in HYG I'm actually outputting the constellation index here
	// which I'm rebuilding later from the 'constellations' file ...
	/// sooo ... dont' rebuild it? just make this 'constellation'
	//and as far as radius ... HYG has r1 and r2 for spheroid sizes
	//Gaia has 'radius' as well 
	//how many valid columns in both?
	// I should add on 'r1' and 'r2' .. or just 'radius' ... for both
	// and make this 11 cols
	//radius, probably in sun-radii
	float radius;
} pt_9col_t;
]]

assert(ffi.sizeof'pt_3col_t' == 3 * ffi.sizeof'float')
assert(ffi.sizeof'pt_9col_t' == 9 * ffi.sizeof'float')

local pt_t = ({
	['3col'] = 'pt_3col_t',
	['9col'] = 'pt_9col_t',
})[format] or error("couldn't deduce point type from format")

-- 2x
local gpuVelLineElemBuf
local cpuVelLineElemBuf 


ffi.cdef[[
typedef struct {
	vec3f_t pos;	//pos of each endpoint of the line
	float dist;		//distance between.  will be doubled up in pairs of vertexes for each line 
} nbhd_t;
]]

local cpuNbhdLineBuf
local gpuNbhdLineBuf

local env	-- I'm not using CL at the moment 
local drawIDShader
local accumStarPointShader
local accumStarLineShader
local renderAccumShader
local drawNbhdLineShader

local fbo
local fbotex

local tempMin = 3300
local tempMax = 8000
local tempTex

local LSun = 3.828e+26 	-- Watts
local L0 = 3.0128e+28	-- Watts
local LSunOverL0 = LSun / L0

-- set to true to put some rough connections between neighboring stars
-- which TODO looks like soup until I dist-atten it somehow
local buildNeighborhood = cmdline.buildNbhds
local buildVelocityBuffers = cmdline.buildVels

-- _G so that sliderFloatTable can use them
alphaValue = .5
lineAlphaValue = .5
pointSizeBias = 0
hsvRangeValue = .2
hdrScaleValue = .001
hdrGammaValue = 1
bloomLevelsValue = 0
showDensityValue = false
velScalarValue = 1
drawPoints = true
drawVelLines = true
normalizeVelValue = false
showPickScene = false
showInformation = true			-- show information on the current star being hovered over
drawGrid = true			-- draw a sphere with 30' segments around our orbiting star
gridRadius = 100
showNeighbors = false	-- associated with the initialization flag buildNeighborhood
showConstellations = false
sliceRMin = 0
sliceRMax = math.huge


--[[
picking is based on point size drawn
point size is based on apparent magnitude
but it's still depth-sorted
so weaker closer stars will block brighter further ones
a better way would be drawing the pick ID + app mag to a buffer
and then post-process flooding it outward to the appropriate size based on its app mag
that way brighter stars would overbear closer weaker neighbors
--]]
pickSizeBias = 4



local numPts	-- number of points
local gpuPointBuf, cpuPointBuf

function App:initGL(...)
	App.super.initGL(self, ...)

	gl.glDisable(gl.GL_DEPTH_TEST)

	self.view.znear = 1e-3
	self.view.zfar = 1e+6

--	self.view.angle = (quatd():fromAngleAxis(0, 0, 1, 90) * self.view.angle):normalize()

	local data = file[filename]
	numPts = #data / ffi.sizeof(pt_t)
print('loaded '..numPts..' stars...')
--numPts = math.min(numPts, 100000)
	
	local src = ffi.cast(pt_t..'*', ffi.cast('char*', data))
	
	--[[ I would just cast, but luajit doesn't seem to refcount it, so as soon as 'data' goes out of scope, 'cpuPointBuf' deallocates, and when I use it outside this function I get a crash ....
	cpuPointBuf = ffi.cast(pt_t..'*', s)
	--]]
	-- [[
	cpuPointBuf = ffi.new(pt_t..'[?]', numPts)
	for i=0,numPts-1 do
		cpuPointBuf[i] = ffi.new(pt_t, src[i])
	end
	--]]

	if cmdline.lummin 
	or cmdline.lumhicount
	or cmdline.appmaghicount 
	or cmdline.rlowcount
	or cmdline.rmax 
	or cmdline.nolumnans 
	or cmdline.addsun
	or (cmdline.nounnamed and names)
	then
		local pts = table()
		for i=0,numPts-1 do
			-- keep track of the original index, for remapping the name file
			pts:insert{obj=ffi.new(pt_t, cpuPointBuf[i]), index=i}
		end

		-- do this first, while names <-> objs, before moving any objs
		if cmdline.nounnamed then
			assert(names, "you can't filter out unnamed stars if you don't have a name file")
			for i=numPts-1,0,-1 do
				if not names[i] then
					pts:remove(i+1)
				end
			end
			print('nounnamed filtered down to '..#pts)
		end

		if cmdline.nolumnans then
			pts = pts:filter(function(pt)
				return math.isfinite(pt.obj.lum)
			end)
			print('nolumnans filtered down to '..#pts)
		end

		if cmdline.addsun then
			local sun = ffi.new(pt_t)
			sun.pos:set(0,0,0)
			-- vel is in Pc/year?  double check plz.
			-- Gaia velocity is in solar reference frame, so the sun's vel will be zero
			sun.vel:set(0,0,0)
			sun.lum = 1
			sun.temp = 5772	-- well, the B-V is 0.63.  maybe I should just be storing that? 
			sun.radius = 1	-- in solar radii
			
			pts:insert{obj=sun, index=numPts}
			numPts = numPts + 1	-- use a unique index.  don't matter about modifying numPts, we will recalculate numPts soon
		end

		if cmdline.lummin then
			pts = pts:filter(function(pt)
				return pt.obj.lum >= cmdline.lummin
			end)
			print('lummin filtered down to '..#pts)
		end
		if cmdline.lumhicount then
			pts = pts:sort(function(a,b)
				return a.obj.lum > b.obj.lum
			end):sub(1, cmdline.lumhicount)
			print('lumhicount filtered down to '..#pts)
		end
		if cmdline.appmaghicount then
			pts = pts:sort(function(a,b)
				return  a.obj.lum / a.obj.pos:lenSq()
						> b.obj.lum / b.obj.pos:lenSq()
			end):sub(1, cmdline.appmaghicount)
			print('appmaghicount filtered down to '..#pts)
		end
		if cmdline.rlowcount then
			pts = pts:sort(function(a,b)
				return  a.obj.pos:lenSq() < b.obj.pos:lenSq()
			end):sub(1, cmdline.rlowcount)
			print('rlowcount filtered down to '..#pts)
		end
		if cmdline.rmax then
			pts = pts:filter(function(pt)
				return pt.obj.pos:length() <= cmdline.rmax
			end)
			print('rmin filtered down to '..#pts)
		end

		-- now remap the constellations and names 
		if names then
			local newnames = names and {} or nil
			for i,pt in ipairs(pts) do
				-- pts will be 0-based
				if names then
					newnames[i-1] = names[pt.index]
				end
			end
			if names then
				names = newnames
			end
		end
		if constellations then
			if constellations then
				for _,c in ipairs(constellations) do
					c.indexset = table.mapi(c.indexes, function(index)
						return true, index
					end):setmetatable(nil)
					c.indexes = {}
				end
			end
			for i,pt in ipairs(pts) do
				if constellations then
					for _,c in ipairs(constellations) do
						if c.indexset[pt.index] then
							table.insert(c.indexes, i-1)
						end
					end
				end
			end
			if constellations then
				for _,c in ipairs(constellations) do
					c.indexset = nil
				end
			end
		end

		if cmdline.print then
			for _,pt in ipairs(pts) do
				local r = pt.obj:length()
				print('r='..r..' lum='..pt.obj.lum)
			end
		end

		numPts = #pts
		cpuPointBuf = ffi.new(pt_t..'[?]', numPts)
		for i=0,numPts-1 do
			cpuPointBuf[i] = pts[i+1].obj
		end
changed = true
	end

	env = CLEnv{precision='float', size=numPts, useGLSharing=false}
	local real3code = template([[
<?
for _,t in ipairs{'float', 'double'} do
?>
typedef union {
	<?=t?> s[3];
	struct { <?=t?> s0, s1, s2; };
	struct { <?=t?> x, y, z; };
} _<?=t?>3;
typedef union {
	<?=t?> s[4];
	struct { <?=t?> s0, s1, s2, s3; };
	struct { <?=t?> x, y, z, w; };
} _<?=t?>4;
<?
end
?>
typedef _<?=env.real?>3 real3;
typedef _<?=env.real?>4 real4;
]], {
		env = env,
	})

	ffi.cdef(real3code)
	env.code = env.code .. real3code
	
	-- get range
	local s = require 'stat.set'('x','y','z','r', 'v', 'lum', 'temp')
	-- TODO better way of setting these flags ... in ctor maybe?
	for _,s in ipairs(s) do
		s.onlyFinite = true
	end
	--[[
	in Mpc:
	r min = 3.2871054551763e-06
	r max = 2121.7879974726 Pc
	r avg = 0.0037223396472864
	r stddev = 0.95579087454634
	so 3 sigma is ... 3
	
	SagA* = 8178 Pc from us, so not in the HYG or Gaia data

	Proxima Centauri is 1.3 Pc from us 

	lum stddev is 163, so 3 stddev is ~ 500
	--]]
	for i=0,numPts-1 do
		local pt = cpuPointBuf[i]
		local pos = pt.pos
		local x,y,z = pos:unpack()
		local r = pos:length()
		local v = pt.vel:length()
		local lum = pt.lum
		local temp = pt.temp
		s:accum(x, y, z, r, v, lum, temp)
	end
	print("data range (Pc):")
	print(s)

	gpuPointBuf = GLArrayBuffer{
		size = numPts * ffi.sizeof(pt_t),
		data = cpuPointBuf,
	}

	local pt_t_attrs = {
		pos = {
			buffer = gpuPointBuf,
			size = 3,
			type = gl.GL_FLOAT,
			stride = ffi.sizeof(pt_t),
			offset = ffi.offsetof(pt_t, 'pos'),
		},
		vel = pt_t == 'pt_9col_t' and {
			buffer = gpuPointBuf,
			size = 3,
			type = gl.GL_FLOAT,
			stride = ffi.sizeof(pt_t),
			offset = ffi.offsetof(pt_t, 'vel'),
		} or nil,
		lum = pt_t == 'pt_9col_t' and {
			buffer = gpuPointBuf,
			size = 1,
			type = gl.GL_FLOAT,
			stride = ffi.sizeof(pt_t),
			offset = ffi.offsetof(pt_t, 'lum'),
		} or nil,
		temp = pt_t == 'pt_9col_t' and {
			buffer = gpuPointBuf,
			size = 1,
			type = gl.GL_FLOAT,
			stride = ffi.sizeof(pt_t),
			offset = ffi.offsetof(pt_t, 'temp'),
		} or nil,
		radius = pt_t == 'pt_9col_t' and {
			buffer = gpuPointBuf,
			size = 1,
			type = gl.GL_FLOAT,
			stride = ffi.sizeof(pt_t),
			offset = ffi.offsetof(pt_t, 'radius'),
		} or nil,
	}


	if buildVelocityBuffers then
		cpuVelLineElemBuf = vector'int'
		for i=0,numPts-1 do
			cpuVelLineElemBuf:push_back(i)
			cpuVelLineElemBuf:push_back(i)
		end
		
		gpuVelLineElemBuf = GLElementArrayBuffer{
			size = ffi.sizeof(cpuVelLineElemBuf.type) * #cpuVelLineElemBuf,
			data = cpuVelLineElemBuf.v,
		}
	end

	local nbhd_t_attrs
	if buildNeighborhood then
		-- maybe i can quick sort and throw in some random compare and that'll do some kind of rough estimate 
print('looking for max')
		cpuNbhdLineBuf = vector'nbhd_t'
		-- heuristic with bins
		-- r stddev is 190, so 3 is 570
		local threeSigma = 570
		local min = -570
		local max = 570
		local nodeCount = 1
		local root = {
			min = vec3d(min,min,min),
			max = vec3d(max,max,max),
			pts = table(),
		}
		root.mid = (root.min + root.max) * .5
		local nodemax = 10
		local function addToTree(node, i)
			local pos = cpuPointBuf[i].pos	
			
			-- have we divided?  pay it forward.
			if node.children then
				local childIndex = bit.bor(
					(pos.x > node.mid.x) and 1 or 0,
					(pos.y > node.mid.y) and 2 or 0,
					(pos.z > node.mid.z) and 4 or 0)
				return addToTree(node.children[childIndex], i)
			end
		
			-- not divided yet?  push into leaf until it gets too big, then divide.
			node.pts:insert(i)
			if #node.pts >= nodemax then
				-- make children
				node.children = {}
				for childIndex=0,7 do
					local xL = bit.band(childIndex,1) == 0
					local yL = bit.band(childIndex,2) == 0
					local zL = bit.band(childIndex,4) == 0
					local child = {
						min = vec3d(
							xL and node.min.x or node.mid.x,
							yL and node.min.y or node.mid.y,
							zL and node.min.z or node.mid.z
						),
						max = vec3d(
							xL and node.mid.x or node.max.x,
							yL and node.mid.y or node.max.y,
							zL and node.mid.z or node.max.z
						),
						pts = table()
					}
					nodeCount = nodeCount + 1
					child.mid = (child.min + child.max) * .5
					child.parent = node
					node.children[childIndex] = child
				end
				-- split the nodes up into the children
				for _,i in ipairs(node.pts) do
					local pos = cpuPointBuf[i].pos	
					local childIndex = bit.bor(
						(pos.x > node.mid.x) and 1 or 0,
						(pos.y > node.mid.y) and 2 or 0,
						(pos.z > node.mid.z) and 4 or 0)
					addToTree(node.children[childIndex], i)
				end
				node.pts = nil
			end
		end
print'pushing into bins'	
		for i=0,numPts-1 do
			addToTree(root, i)
		end
print('created '..nodeCount..' nodes')
print'searching bins'	
		local ai = 1
		local lastTime = os.time()	
		local function searchTree(node)
			if node.children then
				assert(not node.pts)
				for childIndex=0,7 do
					searchTree(node.children[childIndex])
				end
			else
				assert(node.pts)

				-- TODO for each point in the leaf
				-- search all neighbors and all parents, and all parents' neighbors
				-- which means everything except siblings-of-siblings

				local pts = node.pts
				local n = #pts
				for i=1,n-1 do
					local pi = cpuPointBuf[pts[i]]

--[=[
					local bestj = i+1
					local pj = cpuPointBuf[pts[bestj]]
					local bestdistsq = (pi.pos - pj.pos):lenSq()
					for j=i+2,n do
						pj = cpuPointBuf[pts[j]]
						distsq = (pi.pos - pj.pos):lenSq()
						if distsq < bestdistsq then
							bestdistsq = distsq
							bestj = j
						end
					end
					pj = cpuPointBuf[pts[bestj]]
					
					local dist = (pi.pos - pj.pos):length()
					local na = ffi.new'nbhd_t'
					na.pos:set(pi.pos:unpack())
					na.dist = dist
					cpuNbhdLineBuf:push_back(na)
					local nb = ffi.new'nbhd_t'
					nb.pos:set(pj.pos:unpack())
					nb.dist = dist
					cpuNbhdLineBuf:push_back(nb)
--]=]
-- [=[
					for j=i+2,n do
						local pj = cpuPointBuf[pts[j]]
						local dist = (pi.pos - pj.pos):length()
						local na = ffi.new'nbhd_t'
						na.pos:set(pi.pos:unpack())
						na.dist = dist
						cpuNbhdLineBuf:push_back(na)
						local nb = ffi.new'nbhd_t'
						nb.pos:set(pj.pos:unpack())
						nb.dist = dist
						cpuNbhdLineBuf:push_back(nb)					
					end
--]=]
					
					-- now compare up the tree
					-- but there are no points in non-leaf nodes
					-- so we have to find all leafs that are neighboring this node
					-- so ... i guess that means traverse the whole tree
					-- and look at whose bounding points are contained by our bounding points
					local n = node.parent

					--[[
					local thisTime = os.time()
					if lastTime ~= thisTime then
						lastTime = thisTime
						print((100 * (ai / nkbins)) .. '% done')
					end
					--]]
				end
			end
		end
		searchTree(root)
print'done'

		gpuNbhdLineBuf = GLArrayBuffer{
			size = ffi.sizeof(cpuNbhdLineBuf.type) * #cpuNbhdLineBuf,
			data = cpuNbhdLineBuf.v,
		}
	
		nbhd_t_attrs = {
			pos = {
				buffer = gpuNbhdLineBuf,
				size = 3,
				type = gl.GL_FLOAT,
				stride = ffi.sizeof'nbhd_t',
				offset = ffi.offsetof('nbhd_t', 'pos'),
			},
			dist = {
				buffer = gpuNbhdLineBuf,
				size = 1,
				type = gl.GL_FLOAT,
				stride = ffi.sizeof'nbhd_t',
				offset = ffi.offsetof('nbhd_t', 'dist'),
			},
		}
	end

	--refreshPoints()

	hsvtex = GLHSVTex(1024, nil, true)


	-- black body color table from http://www.vendian.org/mncharity/dir3/blackbody/UnstableURLs/bbr_color_D58.html
	-- HYG data temp range is 1500 to 21700 
	tempMin = 1000
	tempMax = 40000
	local rgbs = table()
	for l in io.lines'bbr_color_D58.txt' do
		if l ~= '' and l:sub(1,1) ~= '#' then
			local cmf = l:sub(11,15)
			if cmf == '10deg' then
				local temp = tonumber(string.trim(l:sub(2,6)))
				-- TODO instead of tempMin/tempMax determining the texture bounds, have the texture determine the tempMin/tempMax bounds
				-- and maybe remap it - logarithmically - or based on abs mag (log of lum)
				if temp >= tempMin and temp <= tempMax then
					local r = tonumber(l:sub(82,83), 16)
					local g = tonumber(l:sub(84,85), 16)
					local b = tonumber(l:sub(86,87), 16)
					rgbs:insert{r,g,b}
				end
			end
		end
	end
	local tempImg = Image(#rgbs, 1, 3, 'unsigned char')
	for i=0,#rgbs-1 do
		for j=0,2 do
			tempImg.buffer[j+3*i] = rgbs[i+1][j+1]
		end
	end
	tempTex = GLTex2D{
		width = tempImg.width,
		height = 1,
		internalFormat = gl.GL_RGB,
		format = gl.GL_RGB,
		type = gl.GL_UNSIGNED_BYTE,
		magFilter = gl.GL_LINEAR,
		minFilter = gl.GL_NEAREST,
		-- causing a gl error:
		--wrap = {gl.GL_CLAMP_TO_EDGE, gl.GL_CLAMP_TO_EDGE},
		image = tempImg,
	}
	glreport'here'


local calcPointSize = template([[
	//how to calculate this in fragment space ...
	// coordinates are in Pc
	float distInPcSq = dot(vmv.xyz, vmv.xyz);
	
	//log(distInPc^2) = 2 log(distInPc)
	//so log10(distInPc) = .5 log10(distInPc^2)
	//so log10(distInPc) = .5 / log(10) * log(distInPc^2)
	float log10DistInPc = <?= .5 / math.log(10) ?> * log(distInPcSq);

	//MBolStar - MBolSun = -2.5 * log10( LStar / LSun)
	float LStarOverLSun = lum;
	float LStarOverL0 = <?=LSunOverL0?> * LStarOverLSun;
	float absoluteMagnitude = <?= -2.5 / math.log(10)?> * log(LStarOverL0);	// abs magn

	/*
	apparent magnitude:
	M = absolute magnitude
	m = apparent magnitude
	d = distance in parsecs
	m = M - 5 + 5 * log10(d)
	*/
	float apparentMagnitude = absoluteMagnitude - 5. + 5. * log10DistInPc;
	
	/*
	ok now on to the point size ...
	and magnitude ...
	hmm ... 
	HDR will have to factor into here somehow ...
	*/

	gl_PointSize = 6.5 - apparentMagnitude + pointSizeBias;
]], {
		LSunOverL0 = LSunOverL0,
	})



	-- since the point renderer varies its gl_PointSize with the magnitude, I gotta do that here as well
	drawIDShader = GLProgram{
		vertexCode = template([[
#version 460

in vec3 pos;
in float lum;

out float discardv;
out vec3 color;

uniform float pointSizeBias;
uniform float pickSizeBias;

uniform float sliceRMin, sliceRMax;
uniform vec3 orbit;

uniform mat4 modelViewMatrix;
uniform mat4 projectionMatrix;

void main() {
	vec4 vtx = vec4(pos.xyz, 1.);
	vec4 vmv = modelViewMatrix * vtx;
	gl_Position = projectionMatrix * vmv;

	<?=calcPointSize?>
	gl_PointSize = max(0., gl_PointSize);
	gl_PointSize += pickSizeBias;

	discardv = 0.;
	vec3 orbitToPos = pos - orbit;
	float distFromOrbitSq_in_Pc2 = dot(orbitToPos, orbitToPos);
	if (distFromOrbitSq_in_Pc2 < sliceRMin * sliceRMin ||
		distFromOrbitSq_in_Pc2 > sliceRMax * sliceRMax)
	{
		discardv = 1.;
	}

	float i = gl_VertexID;
	color.r = mod(i, 256.);
	i = (i - color.r) / 256.;
	color.r *= 1. / 255.;
	color.g = mod(i, 256.);
	i = (i - color.g) / 256.;
	color.g *= 1. / 255.;
	color.b = mod(i, 256.);
	i = (i - color.b) / 256.;
	color.b *= 1. / 255.;
}
]], {calcPointSize = calcPointSize}),
		fragmentCode = template[[
#version 460

in vec3 color;
in float discardv;

void main() {
	if (discardv > 0.) {
		discard;
	}

	gl_FragColor = vec4(color, 1.);
}
]],
		attrs = pt_t_attrs,
	}
	glreport'here'
	
	drawIDShader:useNone()
	glreport'here'


	-- i've neglected the postprocessing, and this has become the main shader
	-- which does the fake-postproc just with a varying gl_PointSize
	accumStarPointShader = GLProgram{
		vertexCode = template([[
<?
local clnumber = require 'cl.obj.number'
?>
#version 460

in vec3 pos;
in float lum;
in float temp;

out float lumv;
out vec3 tempcolor;
out float discardv;

uniform mat4 modelViewMatrix;
uniform mat4 projectionMatrix;
uniform float pointSizeBias;
uniform sampler2D tempTex;

//how to occlude, based on radial distance, in Parsecs -- *FROM THE ORBIT*
uniform float sliceRMin, sliceRMax;

//current orbiting position, used for occlusion
uniform vec3 orbit;

void main() {
	vec4 vtx = vec4(pos.xyz, 1.);
	vec4 vmv = modelViewMatrix * vtx;
	gl_Position = projectionMatrix * vmv;

	<?=calcPointSize?>

	discardv = 0.;
	vec3 orbitToPos = pos - orbit;
	float distFromOrbitSq_in_Pc2 = dot(orbitToPos, orbitToPos);
	if (distFromOrbitSq_in_Pc2 < sliceRMin * sliceRMin ||
		distFromOrbitSq_in_Pc2 > sliceRMax * sliceRMax)
	{
		discardv = 1.;
	}

	lumv = 1.;

	// if the point size is < .5 then just make the star dimmer instead
	const float pointSizeMin = .5;
	float dimmer = gl_PointSize - pointSizeMin;
	if (dimmer < 0) {
		gl_PointSize = pointSizeMin;
		lumv *= pow(2., dimmer);
	}

	float tempfrac = (temp - <?=clnumber(tempMin)?>) * <?=clnumber(1/(tempMax - tempMin))?>;
	tempcolor = texture2D(tempTex, vec2(tempfrac, .5)).rgb;
}
]], 	{
			calcPointSize = calcPointSize,
			constellations = constellations,
			tempMin = tempMin,
			tempMax = tempMax,
		}),
		fragmentCode = template([[
#version 460

in float lumv;
in vec3 tempcolor;
in float discardv;

uniform float alpha;

void main() {
	if (discardv > 0.) {
		discard;
	}

	float lumf = lumv;

#if 0
	// make point smooth
	vec2 d = gl_PointCoord.xy * 2. - 1.;
	float rsq = dot(d,d);
	lumf *= 1. / (10. * rsq + .1);
#endif

	gl_FragColor = vec4(tempcolor * lumf * alpha, 1.);
}
]]),
		uniforms = {
			tempTex = 0,
		},
		attrs = pt_t_attrs,
	}
	

	accumStarPointShader:useNone()
	glreport'here'

	accumStarLineShader = GLProgram{
		vertexCode = [[
#version 460

in vec3 pos;
in vec3 vel;

out float lumv;

uniform float velScalar;
uniform bool normalizeVel;

uniform mat4 modelViewMatrix;
uniform mat4 projectionMatrix;

void main() {
	vec4 vtx = vec4(pos.xyz, 1.);

	vec3 velv = vel;
	if (normalizeVel) velv = normalize(velv);
	
	//trying to use a shader variable:	
	float end = float(gl_VertexID % 2 == 0);
	vtx.xyz += velv * end * velScalar;
	
	gl_Position = projectionMatrix * modelViewMatrix * vtx;
	lumv = 1.;//gl_Vertex.w;
}
]],
		fragmentCode = [[
#version 460

in float lumv;

uniform float alpha;

void main() {
	float lumf = lumv;

#if 0	//inv sq dim
	float z = gl_FragCoord.z / gl_FragCoord.w;
	z *= .001;
	lumf *= 1. / (z * z);
#endif

#if 0	//make the point smooth 
	vec2 d = gl_PointCoord.xy * 2. - 1.;
	float rsq = dot(d,d);
	lumf *= 1. / (10. * rsq + .1);
#endif

	vec3 color = vec3(.1, 1., .1) * lumf * alpha;
	gl_FragColor = vec4(color, 1.); 
	//gl_FragColor = vec4(1., 1., 1., 100.);
}
]],
		attrs = pt_t_attrs,
	}
	accumStarLineShader:useNone()
	glreport'here'

	renderAccumShader = GLProgram{
		vertexCode = [[
#version 460

out vec2 texcoord;

void main() {
	texcoord = gl_Vertex.xy;
	gl_Position = vec4(gl_Vertex.x * 2. - 1., gl_Vertex.y * 2. - 1., 0., 1.);
}
]],
		fragmentCode = template[[
<?
local clnumber = require 'cl.obj.number'
?>

in vec2 texcoord;

uniform sampler2D fbotex;
uniform sampler1D hsvtex;
uniform float hdrScale;
uniform float hdrGamma;
uniform float hsvRange;
uniform bool showDensity;
uniform float bloomLevels;

void main() {
	gl_FragColor = vec4(0., 0., 0., 0.);
<? 
local maxLevels = 8
for level=0,maxLevels-1 do 
?>	if (bloomLevels >= <?=clnumber(level)?>) gl_FragColor += texture2D(fbotex, texcoord, <?=clnumber(level)?>);
<? 
end 
?>
	gl_FragColor *= hdrScale * <?=clnumber(1/maxLevels)?>;

	if (showDensity) {
		gl_FragColor = texture1D(hsvtex, log(dot(gl_FragColor.rgb, vec3(.3, .6, .1)) + 1.) * hsvRange);
	} else {
		//tone mapping, from https://learnopengl.com/Advanced-Lighting/HDR
		//gl_FragColor.rgb = gl_FragColor.rgb / (gl_FragColor.rgb + vec3(1.));
		gl_FragColor.rgb = pow(gl_FragColor.rgb, vec3(1. / hdrGamma));
		gl_FragColor.rgb = log(gl_FragColor.rgb + vec3(1.));
	}

	gl_FragColor.a = 1.;
}
]],
		uniforms = {
			fbotex = 0,
			hsvtex = 1,
			showDensity = false,
		},
	}
	glreport'here'
	renderAccumShader:useNone()
	glreport'here'

	if buildNeighborhood then
		drawNbhdLineShader = GLProgram{
			vertexCode = [[
#version 460

in vec3 pos;
in float dist;

out float lumv;

uniform mat4 modelViewMatrix;
uniform mat4 projectionMatrix;

void main() {
	vec4 vtx = vec4(pos, 1.);
	vec4 vmv = modelViewMatrix * vtx;
	gl_Position = projectionMatrix * vmv;
//	float distInPc = length(vmv.xyz);
	lumv = 10. / (dist 
//		* (distInPc + 1.)
);
}
]],
			fragmentCode = [[
#version 460

in float lumv;

void main() {
	float lumf = lumv;
	vec3 color = vec3(.1, 1., .1) * lumf;
	gl_FragColor = vec4(color, 1.); 
}
]],
			attrs = nbhd_t_attrs,
		}
		glreport'here'
		drawNbhdLineShader:useNone()
		glreport'here'
	end
end

ffi.cdef[[
typedef union {
	uint8_t rgba[4];
	uint32_t i;
} pixel4_t;
]]

local pixel = ffi.new'pixel4_t'

local selectedIndex

local modelViewMatrix = matrix_ffi.zeros(4,4)
local projectionMatrix = matrix_ffi.zeros(4,4)

function App:drawPickScene()
	gl.glClearColor(1,1,1,1)
	gl.glClear(bit.bor(gl.GL_COLOR_BUFFER_BIT, gl.GL_DEPTH_BUFFER_BIT))
	gl.glEnable(gl.GL_DEPTH_TEST)

	-- TODO 
	-- 1) smaller viewpoint of 3x3 pixels
	-- 2) pick matrix to zoom in on the specific mouse location

	gl.glEnable(gl.GL_PROGRAM_POINT_SIZE)
	
	drawIDShader:use()
	drawIDShader:setUniforms{
		pointSizeBias = pointSizeBias,
		pickSizeBias = pickSizeBias,
		sliceRMin = sliceRMin,
		sliceRMax = sliceRMax,
		orbit = {self.view.orbit:unpack()},	-- TODO
		modelViewMatrix = modelViewMatrix.ptr,
		projectionMatrix = projectionMatrix.ptr,
	}

	drawIDShader.vao:use()
	gl.glDrawArrays(gl.GL_POINTS, 0, numPts)
	drawIDShader.vao:useNone()

	drawIDShader:useNone()

	gl.glDisable(gl.GL_PROGRAM_POINT_SIZE)

	gl.glFlush()

	gl.glReadPixels(
		self.mouse.ipos.x,
		self.height - self.mouse.ipos.y - 1,
		1,
		1,
		gl.GL_RGB,
		gl.GL_UNSIGNED_BYTE,
		pixel)

	selectedIndex = tonumber(pixel.i)
	gl.glDisable(gl.GL_DEPTH_TEST)
end

function App:drawScene()
	gl.glClearColor(0,0,0,0)
	gl.glClear(gl.GL_COLOR_BUFFER_BIT)
	gl.glEnable(gl.GL_BLEND)
	gl.glBlendFunc(gl.GL_SRC_ALPHA, gl.GL_ONE)
	
	if drawPoints then
		gl.glEnable(gl.GL_PROGRAM_POINT_SIZE)
		
		accumStarPointShader:use()
		accumStarPointShader:setUniforms{
			alpha = alphaValue,
			pointSizeBias = pointSizeBias,
			sliceRMin = sliceRMin,
			sliceRMax = sliceRMax,
			modelViewMatrix = modelViewMatrix.ptr,
			projectionMatrix = projectionMatrix.ptr,
			--orbit = self.view.orbit.s,		-- TODO orbit is a vec3 is a glsl float type
			orbit = {self.view.orbit:unpack()},	-- but view.orbit is double[3], so we can't pass it as a ptr of the type associated with the glsl type(float)
		}

		tempTex:bind()
	
		accumStarPointShader.vao:use()
		gl.glDrawArrays(gl.GL_POINTS, 0, numPts)
		accumStarPointShader.vao:useNone()
	
		tempTex:unbind()
		
		accumStarPointShader:useNone()
		
		gl.glDisable(gl.GL_PROGRAM_POINT_SIZE)
	end
	if drawVelLines and buildVelocityBuffers then
		accumStarLineShader:use()
		accumStarLineShader:setUniforms{
			alpha = lineAlphaValue,
			velScalar = velScalarValue,
			normalizeVel = normalizeVelValue and 1 or 0,
			modelViewMatrix = modelViewMatrix.ptr,
			projectionMatrix = projectionMatrix.ptr,
		}

		accumStarLineShader.vao:use()
		gpuVelLineElemBuf:bind()	-- vao:use() also calls bind() on the associated buffers, so do the element bind last
		gl.glDrawElements(gl.GL_LINES, cpuVelLineElemBuf.size, gl.GL_UNSIGNED_INT, nil)
		gpuVelLineElemBuf:unbind() 
		accumStarLineShader.vao:useNone()

		accumStarLineShader:useNone()
	end

	gl.glDisable(gl.GL_BLEND)
end

local lastWidth, lastHeight
function App:drawWithAccum()
	if self.width ~= lastWidth or self.height ~= lastHeight then
		lastWidth = self.width
		lastHeight = self.height

		fbo = GLFBO{
			width=self.width,
			height=self.height,
		}
		
		fbotex = GLTex2D{
			width = fbo.width,
			height = fbo.height,
			format = gl.GL_RGBA,
			type = gl.GL_FLOAT,
			internalFormat = gl.GL_RGBA32F,
			minFilter = gl.GL_LINEAR_MIPMAP_LINEAR,
			magFilter = gl.GL_LINEAR,
		}
	end
	
	fbo:draw{
		viewport = {0,0,fbo.width,fbo.height},
		dest = fbotex,
		callback = function()
			self:drawScene()
		end,
	}

	fbotex:bind()
	gl.glGenerateMipmap(fbotex.target)
	fbotex:unbind()

	gl.glClear(gl.GL_COLOR_BUFFER_BIT)
	gl.glViewport(0, 0, self.width, self.height)
	
	gl.glMatrixMode(gl.GL_PROJECTION)
	gl.glPushMatrix()
	gl.glLoadIdentity()
	gl.glOrtho(0,1,0,1,-1,1)
	gl.glMatrixMode(gl.GL_MODELVIEW)
	gl.glPushMatrix()
	gl.glLoadIdentity()

	renderAccumShader:use()
	renderAccumShader:setUniforms{
		hdrScale = hdrScaleValue,
		hdrGamma = hdrGammaValue,
		hsvRange = hsvRangeValue,
		bloomLevels = bloomLevelsValue,
		showDensity = showDensityValue and 1 or 0,
	}
	fbotex:bind(0)
	hsvtex:bind(1)

	gl.glBegin(gl.GL_QUADS)
	gl.glVertex2f(0,0)
	gl.glVertex2f(0,1)
	gl.glVertex2f(1,1)
	gl.glVertex2f(1,0)
	gl.glEnd()

	hsvtex:unbind(1)
	fbotex:unbind(0)
	renderAccumShader:useNone()

	gl.glMatrixMode(gl.GL_PROJECTION)
	gl.glPopMatrix()
	gl.glMatrixMode(gl.GL_MODELVIEW)
	gl.glPopMatrix()

end

local function sphericalToCartesian(r,theta,phi)
	local ct = math.cos(theta)
	local st = math.sin(theta)
	local cp = math.cos(phi)
	local sp = math.sin(phi)
	return vec3d(
		r * cp * st,
		r * sp * st,
		r * ct
	)
end

function App:update()
	gl.glGetFloatv(gl.GL_MODELVIEW_MATRIX, modelViewMatrix.ptr)
	gl.glGetFloatv(gl.GL_PROJECTION_MATRIX, projectionMatrix.ptr)
	
	self:drawPickScene()

	if not showPickScene then
		-- [[
		self:drawScene()
		--]]
		--[[
		self:drawWithAccum()
		--]]
	end


	-- TODO inv square reduce this.... by inv square of one another, and by inv square from view
	if buildNeighborhood and showNeighbors then
		gl.glEnable(gl.GL_BLEND)
		drawNbhdLineShader:use()
		drawNbhdLineShader:setUniforms{
			modelViewMatrix = modelViewMatrix.ptr,
			projectionMatrix = projectionMatrix.ptr,
		}

		drawNbhdLineShader.vao:use()
		gl.glDrawArrays(gl.GL_LINES, 0, cpuNbhdLineBuf.size)
		drawNbhdLineShader.vao:useNone()
		drawNbhdLineShader:useNone()
		gl.glDisable(gl.GL_BLEND)
	end


	-- TODO draw around origin?  or draw around view orbit?
	if drawGrid then
		gl.glEnable(gl.GL_BLEND)
		gl.glBlendFunc(gl.GL_SRC_ALPHA, gl.GL_ONE)
		gl.glColor3f(.25, .25, .25)
		gl.glBegin(gl.GL_LINES)
		local idiv = 24
		local dphi = 2 * math.pi / idiv
		local jdiv = 12
		local dtheta = math.pi / jdiv
		for i=0,idiv-1 do
			local phi = 2 * math.pi * i / idiv
			for j=0,jdiv-1 do
				local theta = math.pi * j / jdiv
				gl.glVertex3f((sphericalToCartesian(gridRadius, theta, phi) + self.view.orbit):unpack())
				gl.glVertex3f((sphericalToCartesian(gridRadius, theta + dtheta, phi) + self.view.orbit):unpack())
				if j > 0 then
					gl.glVertex3f((sphericalToCartesian(gridRadius, theta, phi) + self.view.orbit):unpack())
					gl.glVertex3f((sphericalToCartesian(gridRadius, theta, phi + dphi) + self.view.orbit):unpack())
				end
			end
		end
		gl.glEnd()
		gl.glDisable(gl.GL_BLEND)
	end

	if showConstellations then
		for _,constellation in ipairs(constellations) do
			if constellation.enabled then
				gl.glColor3f(.5, .5, .5)
				for i=0,7 do
					local ir = constellation.dist[bit.band(i, 1) == 0 and 'min' or 'max']
					local ith = math.pi/2 - (constellation.dec[bit.band(i, 2) == 0 and 'min' or 'max'])
					local iph = constellation.ra[bit.band(i, 4) == 0 and 'min' or 'max']
					for j=0,2 do
						local k = bit.bxor(i, bit.lshift(1, j))
						if k > i then
							local jr = constellation.dist[bit.band(k, 1) == 0 and 'min' or 'max']
							local jth = math.pi/2 - (constellation.dec[bit.band(k, 2) == 0 and 'min' or 'max'])
							local jph = constellation.ra[bit.band(k, 4) == 0 and 'min' or 'max']
							local div = 20
							gl.glBegin(gl.GL_LINE_STRIP)
							for m=0,div do
								local s = m/div
								local t = 1 - s
								local r = s * ir + t * jr
								local th = s * ith + t * jth
								local ph = s * iph + t * jph
								gl.glVertex3f(sphericalToCartesian(r, th, ph):unpack())
							end
							gl.glEnd()
						end
					end
				end
			end
		end
	end

	glreport'here'
	App.super.update(self)
end

--[[
--]]

local float = ffi.new'float[1]'
local function sliderFloatTable(title, t, key, ...)
	float[0] = t[key]
	local result = ig.igSliderFloat(title, float, ...) 
	if result then t[key] = float[0] end
	return result
end

local function inputFloatTable(title, t, key, ...)
	float[0] = t[key]
	local result = ig.igInputFloat(title, float, ...) 
	if result then t[key] = float[0] end
	return result
end


local bool = ffi.new'bool[1]'
local function checkboxTable(title, t, key, ...)
	bool[0] = t[key]
	local result = ig.igCheckbox(title, bool, ...)
	if result then t[key] = bool[0] end
	return result
end


-- TODO dynamic sized buffer?
local str = ffi.new'char[256]'
local function textTable(title, t, k, ...)
	local src = tostring(t[k])
	local len = math.min(ffi.sizeof(str)-1, #src)
	ffi.copy(str, src, len)
	str[len] = 0
	if ig.igInputText(title, str, ffi.sizeof(str), ...) then
		t[k] = ffi.string(str)
		return true
	end
end


local search = {
	orbit = '',
	lookat = '',
}
function App:updateGUI()
	checkboxTable('draw points', _G, 'drawPoints')
	sliderFloatTable('point size', _G, 'pointSizeBias', -10, 10)
	sliderFloatTable('pick size', _G, 'pickSizeBias', 0, 20)
	inputFloatTable('point alpha value', _G, 'alphaValue')

--[[ not used atm	
	checkboxTable('show density', _G, 'showDensityValue')
	sliderFloatTable('hdr scale', _G, 'hdrScaleValue', 0, 1000, '%.7f', 10)
	sliderFloatTable('hdr gamma', _G, 'hdrGammaValue', 0, 1000, '%.7f', 10)
	sliderFloatTable('hsv range', _G, 'hsvRangeValue', 0, 1000, '%.7f', 10)
	sliderFloatTable('bloom levels', _G, 'bloomLevelsValue', 0, 8)
--]]	
	
	inputFloatTable('slice r min', _G, 'sliceRMin')
	inputFloatTable('slice r max', _G, 'sliceRMax')

	
	checkboxTable('show pick scene', _G, 'showPickScene')	
	checkboxTable('show grid', _G, 'drawGrid')	
	inputFloatTable('grid radius', _G, 'gridRadius')

	-- draw nhbd lines stuff which looks dumb right now
	if buildNeighborhood then
		checkboxTable('show nbhd', _G, 'showNeighbors')
	end

	-- view stuff
	ig.igText('dist (Pc) '..(self.view.pos - self.view.orbit):length())
	inputFloatTable('znear', self.view, 'znear')
	inputFloatTable('zfar', self.view, 'zfar')
	sliderFloatTable('fov y', self.view, 'fovY', 0, 180)

	if names then
		if textTable('orbit', search, 'orbit', ig.ImGuiInputTextFlags_EnterReturnsTrue) then
			for i,v in pairs(names) do
				if v == search.orbit then
					assert(i >= 0 and i < numPts, "oob index in name table "..i)
					local pt = cpuPointBuf[i]
					self.view.orbit:set(pt.pos:unpack())
				end
			end
		end
		if textTable('look at', search, 'lookat', ig.ImGuiInputTextFlags_EnterReturnsTrue) then
			for i,v in pairs(names) do
				if v == search.lookat then
					local orbitDist = (self.view.pos - self.view.orbit):length()
					local fwd = -self.view.angle:zAxis()
					
					assert(i >= 0 and i < numPts, "oob index in name table "..i)
					local pt = cpuPointBuf[i]
					local to = (pt.pos - self.view.pos):normalize()

					local angle = math.acos(math.clamp(fwd:dot(to), -1, 1))
					local axis = fwd:cross(to):normalize()

					local rot = quatd():fromAngleAxis(axis.x, axis.y, axis.z, math.deg(angle))
					quatd.mul(rot, self.view.angle, self.view.angle)
					
					self.view.pos = self.view.orbit + self.view.angle:zAxis() * orbitDist
				end
			end
		end
	end


	-- draw vel lines
	checkboxTable('draw vel lines', _G, 'drawVelLines')
	inputFloatTable('line alpha value', _G, 'lineAlphaValue')
	inputFloatTable('vel scalar', _G, 'velScalarValue')
	checkboxTable('normalize velocity', _G, 'normalizeVelValue')

	-- gui stuff
	checkboxTable('show mouseover info', _G, 'showInformation')

	checkboxTable('show constellations', _G, 'showConstellations')
	if showConstellations then
		local count = 0
		for _,constellation in ipairs(constellations) do
			if constellation.name then
				if count > 0 
				and count % 10 ~= 0
				then 
					ig.igSameLine() 
				end
				ig.igPushIDStr(constellation.name)
				checkboxTable('', constellation, 'enabled')
				if ig.igIsItemHovered(ig.ImGuiHoveredFlags_None) then
					ig.igBeginTooltip()
					ig.igText(constellation.name)
					ig.igEndTooltip()
				end
				ig.igPopID()
				count = count + 1
			end
		end
	end

	ig.igImage(
		ffi.cast('void*', ffi.cast('intptr_t', tempTex.id)),
		ig.ImVec2(128, 24),
		ig.ImVec2(0,0),
		ig.ImVec2(1,1))

	if showInformation 
	and selectedIndex < 0xffffff 
	and selectedIndex >= 0
	and selectedIndex < numPts
	then
		local s = table()
		s:insert('index: '..('%06x'):format(selectedIndex))

		local name = names and names[selectedIndex] or nil
		if name then
			s:insert('name: '..tostring(name))
		end
		local pt = cpuPointBuf[selectedIndex]
		local dist = (pt.pos - self.view.orbit):length()
			
		local LStarOverLSun = pt.lum
		local absmag = (-2.5 / math.log(10)) * math.log(LStarOverLSun * LSunOverL0)
		local appmag = absmag - 5 + (5 / math.log(10)) * math.log(dist)

		s:insert('dist (Pc): '..dist)
		s:insert('lum (LSun): '..LStarOverLSun)
		s:insert('temp (K): '..pt.temp)
		s:insert('abs mag: '..absmag)
		s:insert('app mag: '..appmag)

		ig.igBeginTooltip()
		ig.igText(s:concat'\n')
		ig.igEndTooltip()
	end
end

--[[
watershed of velocity functions ...

dphi/dx(p1) = v1_x
dphi/dy(p1) = v1_y
dphi/dz(p1) = v1_z

dphi/dx(p2) = v1_x
dphi/dy(p2) = v1_y
dphi/dz(p2) = v1_z

...

dphi/dx(pn) = v1_x
dphi/dy(pn) = v1_y
dphi/dz(pn) = v1_z

becomes

(phi(p[i] + h e_x) - phi(p[i] - h e_x)) / (2h) ~= v[i]_x
(phi(p[i] + h e_y) - phi(p[i] - h e_y)) / (2h) ~= v[i]_y
(phi(p[i] + h e_z) - phi(p[i] - h e_z)) / (2h) ~= v[i]_z

is not invertible for odd # of elements ...


--]]

App():run()
