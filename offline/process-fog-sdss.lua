#!/usr/bin/env luajit
--[[
here's my attempt at the FOG removal 
courtesy of OpenCL and LuaJIT
--]]
require 'ext'
local template = require 'template'
local vec3f = require 'vec-ffi.vec3f'
local vec4f = require 'vec-ffi.vec4f'
local bit = require 'bit'
local ffi = require 'ffi'
local gl = require 'gl'
local ig = require 'imgui'
local glreport = require 'gl.report'
local GLProgram = require 'gl.program'
local clnumber = require 'cl.obj.number'
local CLEnv = require 'cl.obj.env'
	
local c = 299792.458	--km/s
local H0 = 69.32	--km/s/Mpc

local App = require 'imguiapp.withorbit'()
App.title = 'FoG tool'
App.viewDist = 2

local redshiftMin = .01

local cpuPtsBuf, cpuPtsXYZBuf
local cpuColorBuf
local glPtsXYZBufID = ffi.new'GLuint[1]'
local glColorBufID = ffi.new'GLuint[1]'
local n
local clPtsBuf, clPtsXYZBuf
local clColorBuf
local env

--[[
now for linking numbers ...

from 2006 Berlind et al:

b_par = 1.5
b_perp = .11
SubbaRao says b_perp = 1/8 b_par
n_g = 0.00673 to 0.02434

the 2008 SubbaRao paper has all sorts of symbol screwups (swapping perpendicul for parallel, swapping + and - ...)
so I'm just going by intuition here ... 
...parallel distance would be difference in r's, i.e. difference in redshifts
...perpendicular distance would be r theta, where r = .5 (r1 + r2), and theta ~ sin theta at close distances, so (r1 + r2) (.5 theta) ~ (r1 + r2) sin(.5 theta), which can be represented as a half angle of the sin written wrt the dot product 
...and the perpendicular distance should be 1/8th the parallel distance so clusters form greater in the radial direction
D_perp_ij = (c/H0) (z_i + z_j) sin (theta_ij / 2)
D_par_ij = (c/H0) |z_i - z_j|

link if
D_perp_ij < b_perp 1/cbrt(n_g)
D_par_ij < b_par 1/cbrt(n_g)

for b_par = 1.5 and n_g = .02434 we get D_par_threshold = 5.1758824097784
for b_par = 1.5 and n_g = .00673 we get D_par_threshold = 7.9448597976334
c/H0 |z_i - z_j| would be the distance in Mpc
--]]
-- [[
--local b_par = 1.5
--local b_perp = .11
--local n_g = .00673
--local b_perp = b_par / 8
local b_perp = .001
local b_par = b_perp * 8
local n_g = .02434
local D_par_threshold = b_par / n_g ^ (1/3)
local D_perp_threshold = b_perp / n_g ^ (1/3)
--]]
--[[
local D_par_threshold = .001
local D_perp_threshold = 1/8 * D_par_threshold	-- because b_perp = 1/8 b_par
--]]

function doClusteringOnCPU()
	-- here I'm gonna try binning stuff ...
	local zmin, zmax, zbins = 0, c / H0, 500
	local ramin, ramax, rabins = 0, 360, math.floor(360*.9)
	local decmin, decmax, decbins = -90, 90, math.floor(180*.9)
	
	local dz = (zmax - zmin) / zbins
	-- note ra and dec are still in degrees
	local dra = (ramax - ramin) / rabins
	local ddec = (decmax - decmin) / decbins
	print('dz (Mpc)', dz)
	print('dra (deg)', dra)
	print('ddec (deg)', ddec)

	local startTime = os.clock()
	print'collecting bins...'
	local bins = {}
	for i=0,n-1 do
		--- xyz is z, ra, dec.  z is redshift fraction of c 0 to 1, ra and dec are degrees.  ra is 0 to 360, dec is -90 to 90
		local pt = cpuPtsBuf[i]
		local z, ra, dec = pt.x, pt.y, pt.z
		if z > redshiftMin then
			z = z * c / H0
			local zi = math.clamp(math.floor((z - zmin) / dz), 0, zbins-1)
			local rai = math.clamp(math.floor((ra - ramin) / dra), 0, rabins-1)
			local deci = math.clamp(math.floor((dec - decmin) / ddec), 0, decbins-1)
			local binIndex = zi + zbins * (rai + rabins * (deci))
			bins[binIndex] = bins[binIndex] or {}
			table.insert(bins[binIndex], i)
		end
	end
	print('...took '..(os.clock() - startTime)..' seconds')
	
	local totalBins = 0
	-- distribution of each, binned
	local zDist = {}
	local raDist = {}
	local decDist = {}

	local clusterID = ffi.new('int[?]', n)
	ffi.fill(clusterID, 0, ffi.sizeof'int' * n)
	local nextClusterID = 0

	local startTime = os.clock()
	print'clustering...'
	local lastTime = startTime
	for deci=0,decbins-1 do
		for rai=0,rabins-1 do
			for zi=0,zbins-1 do
				local binIndex = zi + zbins * (rai + rabins * deci)
				local thisBin = bins[binIndex]
				if thisBin then
					
					-- for point in this bin
					for _,i in ipairs(thisBin) do
						-- for each other point in this bin and neighbors (considering appropriate radial and z distances)
						
						-- how many dz's does D_par_threshold span?  probably 1
						local dzj = math.ceil(D_par_threshold / dz)
						local draj = math.ceil(D_perp_threshold / dra)
						local ddecj = math.ceil(D_perp_threshold / ddec)

						for decj=math.max(0, deci-ddecj), math.min(decbins-1, deci+ddecj) do
							for raj=math.max(0, rai-draj), math.min(rabins-1, rai+draj) do
								for zj=math.max(0, zi-dzj), math.min(zbins-1, zi+dzj) do
									local nbhBinIndex = zj + zbins * (raj + rabins * decj)
									local nbhBin = bins[nbhBinIndex]

local thisTime = math.floor(os.clock())
if thisTime ~= lastTime then
	lastTime = thisTime
	print(
		'#='..nextClusterID..' '..
		zi..'/'..zbins..' '..
		rai..'/'..rabins..' '..
		deci..'/'..decbins..' '..
		zj..'/'..zbins..' '..
		raj..'/'..rabins..' '..
		decj..'/'..decbins)
end



									if nbhBin then
										for _,j in ipairs(nbhBin) do
											if i ~= j then
												-- test distances between points i and j
												local pi, pj = cpuPtsBuf[i], cpuPtsBuf[j]
												local pi_z, pj_z = pi.s0, pj.s0
												local D_par = math.abs(pi_z - pj_z)		-- 2006 Berlind says -, 2008 SubbaRao says +
												if D_par <= D_par_threshold then
													local pi_xyz, pj_xyz = cpuPtsXYZBuf[i], cpuPtsXYZBuf[j]
													local dot = pi_xyz.x * pj_xyz.x 
														+ pi_xyz.y * pj_xyz.y 
														+ pi_xyz.z * pj_xyz.z
													dot = math.clamp(dot, -1, 1)
													local halfsinthSq = math.abs(.5 * (1 - dot))
													local D_perpSq = (pi_z + pj_z)
													D_perpSq = D_perpSq * D_perpSq * halfsinthSq
													if D_perpSq < D_perp_threshold * D_perp_threshold then
														-- WE'RE LINKED
														local ci = clusterID[i]
														local cj = clusterID[j] 
														if ci == 0
														and cj == 0
														then
															local nid = nextClusterID + 1
															nextClusterID = nid + 1
															clusterID[i] = nid
															clusterID[j] = nid
														elseif ci ~= 0 
														and cj == 0
														then
															clusterID[j] = ci
														elseif cj ~= 0
														and ci == 0
														then
															clusterID[i] = cj
														elseif ci ~=0 
														and cj ~= 0
														and ci ~= cj 
														then
															-- hmm, merge two clusters ...
															-- get rid of the larger number ... (so the other doesn't become invalidated)
															if cj < ci then
																ci, cj = cj, ci
															end
															for k=0,n-1 do
																local ck = clusterID[k]
																if ck == cj then
																	clusterID[k] = ci
																elseif ck > cj then
																	assert(ck > 0)
																	clusterID[k] = ck - 1
																end
															end
															nextClusterID = nextClusterID - 1
														end
													end
												end
											end
										end
									end
								end
							end
						end
					end

					-- count distributions:
--					print(binIndex, thisBin)
					totalBins = totalBins + 1 
					zDist[zi] = (zDist[zi] or 0) + #thisBin
					raDist[rai] = (raDist[rai] or 0) + #thisBin
					decDist[deci] = (decDist[deci] or 0) + #thisBin
				end
			end
		end
	end

	print('made '..nextClusterID..' clusters')
	-- now give each cluster a random color
	local colors = range(0,nextClusterID):map(function(k) 
		return vec3f(math.random(), math.random(), math.random()):normalize(), k
	end)
	for i=0,n-1 do
		local cid = clusterID[i]
		--assert(cid ~= 0, "found a pt without a cluster!")
		for j=0,2 do
			cpuColorBuf[i].s[j] = colors[cid]:ptr()[j]
		end
	end
	print('...took '..(os.clock() - startTime)..' seconds')
	
	print()

	print('z dist')
	for zi=0,zbins-1 do
		print(zi, zDist[zi])
	end
	print()
	print('ra dist')
	for rai=0,rabins-1 do
		print(rai, raDist[rai])
	end
	print()
	print('dec dist')
	for deci=0,decbins-1 do
		print(deci, decDist[deci])
	end
						

	print('total bins', totalBins) 
end

local GPUClustering = class()

function GPUClustering:init()
	-- init (only run this once)
	clColorBuf = env:buffer{name='cluster', type='_float3', data=cpuColorBuf}
	self.startIndex = 0
	self.indexStep = 100

	self.updateKernel = env:kernel{
		argsIn = {clPtsXYZBuf, {name='startIndex', type='int'}},
		argsOut = {clColorBuf},
		body = template([[
#if 0	
	for (int neighbor = startIndex; neighbor < startIndex + <?=self.indexStep?> && neighbor < size.x; ++neighbor) {
#endif
#if 1	//let each point start at itself
	for (int offset = 0; offset < <?=self.indexStep?>; ++offset) {
		int neighbor = (index + startIndex + offset) % size.x;
#endif
		if (index == neighbor) continue;
		real4 pi = pts[index];
		real4 pj = pts[neighbor];
		
		real pi_z = pi.w;
		real pj_z = pj.w;
		if (pi_z < <?=clnumber(redshiftMin)?> || pj_z < <?=clnumber(redshiftMin)?>) continue;

		real D_par = fabs(pi_z - pj_z);
		if (D_par < <?=clnumber(D_par_threshold)?>) {
			real dot = pi.x * pj.x + pi.y * pj.y + pi.z * pj.z;
			dot = clamp(dot, -1., 1.);
			real halfsinthSq = fabs(.5 * (1. - dot));
			
			real D_perpSq = pi_z + pj_z; D_perpSq *= D_perpSq * halfsinthSq;
			if (D_perpSq < <?=clnumber(D_perp_threshold * D_perp_threshold)?>) {
				if (index > neighbor) {
					cluster[neighbor] = cluster[index];
				} else {
					cluster[index] = cluster[neighbor];
				}
			}
		}
	}
]], {
	self = self,
	clnumber = clnumber,
	D_par_threshold = D_par_threshold,
	D_perp_threshold = D_perp_threshold,
	redshiftMin = redshiftMin,
}),
	}
	self.updateKernel:compile()
end

function resetClusters()
	for i=0,n-1 do
		for j=0,2 do
			cpuColorBuf[i].s[j] = math.random()
		end
	end
	
	clColorBuf:fromCPU(cpuColorBuf) 

	gl.glBindBuffer(gl.GL_ARRAY_BUFFER, glColorBufID[0])
	local glColorBufMap = gl.glMapBuffer(gl.GL_ARRAY_BUFFER, gl.GL_WRITE_ONLY)
	clColorBuf:toCPU(glColorBufMap)
	gl.glUnmapBuffer(gl.GL_ARRAY_BUFFER)
	gl.glBindBuffer(gl.GL_ARRAY_BUFFER, 0)
end

function GPUClustering:update()
	-- TODO set the startIndex to a new value
	self.updateKernel.obj:setArg(2, ffi.new('int[1]', self.startIndex))
	self.updateKernel()
	self.startIndex = self.startIndex + self.indexStep
	if self.startIndex > n then self.startIndex = 0 end

	-- refresh the color buffer
	gl.glBindBuffer(gl.GL_ARRAY_BUFFER, glColorBufID[0])
	local glColorBufMap = gl.glMapBuffer(gl.GL_ARRAY_BUFFER, gl.GL_WRITE_ONLY)
	clColorBuf:toCPU(glColorBufMap)
	gl.glUnmapBuffer(gl.GL_ARRAY_BUFFER)
	gl.glBindBuffer(gl.GL_ARRAY_BUFFER, 0)
end

local gpuClustering

function refreshPoints()
	gl.glBindBuffer(gl.GL_ARRAY_BUFFER, glPtsXYZBufID[0])
	local glPtsBufMap = gl.glMapBuffer(gl.GL_ARRAY_BUFFER, gl.GL_WRITE_ONLY)
	clPtsBuf:toCPU(glPtsBufMap)
	gl.glUnmapBuffer(gl.GL_ARRAY_BUFFER)
	gl.glBindBuffer(gl.GL_ARRAY_BUFFER, 0)
end

function App:initGL(...)
	App.super.initGL(self, ...)

	gl.glEnable(gl.GL_DEPTH_TEST)

	self.view.znear = .001
	self.view.zfar = 3

	-- init cl after gl so it has sharing
	local data = path'datasets/sdss3/points/spherical.f64':read()	-- format is double z, ra, dec
	n = #data / (3 * ffi.sizeof'double')
	print('n='..n)

	env = CLEnv{precision='double', size=n}

	local real3code = template([[
<?
for _,t in ipairs{'float', 'double'} do
?>
typedef union {
	<?=t?> s[3];
	struct { <?=t?> s0, s1, s2; };
	struct { <?=t?> x, y, z; };
} _<?=t?>3;
<?
end
?>
typedef _<?=env.real?>3 real3;
]], {
		env = env,
	})

	ffi.cdef(real3code)
	env.code = env.code .. real3code

	cpuPtsBuf = ffi.new('real3[?]', n)
	ffi.copy(cpuPtsBuf, data, #data)

	-- unit directions of each point, w= zshift in Mpc
	cpuPtsXYZBuf = ffi.new('real4[?]', n)
	for i=0,n-1 do
		local pt = cpuPtsBuf[i]
		local pt_ra = pt.s1
		local pt_dec = pt.s2
		local cos_pt_dec = math.cos(math.rad(pt_dec))
		cpuPtsXYZBuf[i].x = math.cos(math.rad(pt_ra)) * cos_pt_dec
		cpuPtsXYZBuf[i].y = math.sin(math.rad(pt_ra)) * cos_pt_dec
		cpuPtsXYZBuf[i].z = math.sin(math.rad(pt_dec))
		cpuPtsXYZBuf[i].w = pt.s0
	end

	clPtsXYZBuf = env:buffer{name='pts', type='real4', data=cpuPtsXYZBuf}

	-- based on cluster 
	cpuColorBuf = ffi.new('_float3[?]', n)
	for i=0,n-1 do
		for j=0,2 do
			cpuColorBuf[i].s[j] = math.random()
		end
	end

	--doClusteringOnCPU()
	gpuClustering = GPUClustering()	

	--[[
	TODO
	have a kernel, domain is each 2mil pts
	kernel is to cycle through each other pt
	only do 1000 at a time
	run for 3000 iterations ~ 5 mins ... hmm that's still a long time ...
	and find the closest pt to each other pt

	then reiterate: seed all points a unique ID
	then propagate their seed to their nearest neighbor ... if they are closer than their nearest is .. ?'

	--]]

	-- not needed at the moment ...
	clPtsBuf = env:buffer{name='pts', type='real3', data=cpuPtsBuf}

	gl.glGenBuffers(1, glPtsXYZBufID)
	gl.glBindBuffer(gl.GL_ARRAY_BUFFER, glPtsXYZBufID[0])
	gl.glBufferData(gl.GL_ARRAY_BUFFER, n*ffi.sizeof'real4', cpuPtsXYZBuf, gl.GL_STATIC_DRAW)
	gl.glBindBuffer(gl.GL_ARRAY_BUFFER, 0)
	--refreshPoints()
	
	gl.glGenBuffers(1, glColorBufID)
	gl.glBindBuffer(gl.GL_ARRAY_BUFFER, glColorBufID[0])
	gl.glBufferData(gl.GL_ARRAY_BUFFER, n*ffi.sizeof'_float3', cpuColorBuf, gl.GL_DYNAMIC_DRAW)
	gl.glBindBuffer(gl.GL_ARRAY_BUFFER, 0)

	sphereCoordShader = GLProgram{
		vertexCode = template([[
#define M_PI <?=clnumber(math.pi)?>
varying vec4 color;
void main() {
	vec4 vtx = vec4(gl_Vertex.xyz * gl_Vertex.w, 1.);
	gl_Position = gl_ModelViewProjectionMatrix * vtx;
	color = gl_Color;
}
]], {
	clnumber = clnumber,
}),
		fragmentCode = [[
varying vec4 color;
uniform float alpha;
void main() {
	float z = gl_FragCoord.z / gl_FragCoord.w;
	float lum = max(.005, .001 / (z * z));
	gl_FragColor = vec4(color.rgb, color.a * lum * alpha);
}
]],
	}
	sphereCoordShader:useNone()
	glreport'here'
end

local useAlpha = ffi.new('bool[1]', false)
local alphaValue = ffi.new('float[1]', 1)
local pointSize = ffi.new('float[1]', 1)
local runGPUClustering = ffi.new('bool[1]', false)
function App:update()
	if runGPUClustering[0] then	
		gpuClustering:update()
	end

	gl.glClear(bit.bor(gl.GL_DEPTH_BUFFER_BIT, gl.GL_COLOR_BUFFER_BIT))
	
	gl.glBlendFunc(gl.GL_SRC_ALPHA, gl.GL_ONE)
	if useAlpha[0] then 
		gl.glEnable(gl.GL_BLEND)
	end

	gl.glPointSize(pointSize[0])
	gl.glEnable(gl.GL_POINT_SMOOTH)
	gl.glHint(gl.GL_POINT_SMOOTH_HINT, gl.GL_NICEST)

	gl.glEnableClientState(gl.GL_VERTEX_ARRAY)
	gl.glEnableClientState(gl.GL_COLOR_ARRAY)
	
	gl.glBindBuffer(gl.GL_ARRAY_BUFFER, glColorBufID[0])
	gl.glColorPointer(3, gl.GL_FLOAT, 0, nil)	-- call every frame
	
	gl.glBindBuffer(gl.GL_ARRAY_BUFFER, glPtsXYZBufID[0])
	gl.glVertexPointer(4, gl.GL_DOUBLE, 0, nil)	-- call every frame
	
	gl.glBindBuffer(gl.GL_ARRAY_BUFFER, 0)
	
	sphereCoordShader:use()
	gl.glUniform1f(sphereCoordShader.uniforms.alpha.loc, alphaValue[0])
	gl.glDrawArrays(gl.GL_POINTS, 0, n)
	sphereCoordShader:useNone()
	
	gl.glDisableClientState(gl.GL_VERTEX_ARRAY)
	gl.glDisableClientState(gl.GL_COLOR_ARRAY)
	
	gl.glPointSize(1)
	
	gl.glDisable(gl.GL_BLEND)
	
	glreport'here'
	App.super.update(self)
end

local float = ffi.new'float[1]'
local int = ffi.new'int[1]'
function App:updateGUI()
	ig.igSliderFloat('point size', pointSize, 1, 10)
	ig.igCheckbox('blend', useAlpha)
	ig.igSliderFloat('alpha value', alphaValue, 0, 1, '%.3f', 10)
	ig.igCheckbox('run gpu clustering', runGPUClustering)
	ig.igText('gpu clustering index: '..gpuClustering.startIndex)	
	
	int[0] = gpuClustering.indexStep
	if ig.igInputInt('gpu clustering step', int) then
		gpuClustering.indexStep = int[0]
	end
	
	float[0] = D_par_threshold
	if ig.igInputFloat('parallel threshold', float) then
		D_par_threshold = float[0]
	end

	float[0] = D_perp_threshold
	if ig.igInputFloat('perpendicular threshold', float) then
		D_perp_threshold = float[0]
	end
	
	if ig.igButton'reset clusters' then
		resetClusters()
	end

	if ig.igButton'CPU clustering' then
		doClusteringOnCPU()
	end
end

App():run()
