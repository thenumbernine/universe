#!/usr/bin/env luajit
-- simple pointcloud visualizer
local ffi = require 'ffi'
require 'ext'
local ig = require 'ffi.imgui'
local template = require 'template'
local ImGuiApp = require 'imguiapp'
local Orbit = require 'glapp.orbit'
local View = require 'glapp.view'
local Image = require 'image'
local gl = require 'gl'
local glreport = require 'gl.report'
local GLProgram = require 'gl.program'
local GLFBO = require 'gl.fbo'
local GLTex2D = require 'gl.tex2d'
local GLHSVTex = require 'gl.hsvtex'
local CLEnv = require 'cl.obj.env'

local App = class(Orbit(View.apply(ImGuiApp)))
App.title = 'pointcloud visualization tool'
App.viewDist = 2

ffi.cdef[[
typedef struct {
	float pos[3];
	float vel[3];
	float rad;
	
	/*
	temperature, in Kelvin
	*/
	float temp;
	
	/*
	solar luminosity
	https://en.wikipedia.org/wiki/Solar_luminosity
	3.828e+26 watts
	total emitted over the whole surface 
	L_sun = 4 pi k I_sun A^2 
		for L_sum solar luminosity (watts)
		and I_sun solar irradiance (watts/meter^2)
	*/
	float lum;
} pt_t;
]]

local glPointBufID = ffi.new'GLuint[1]'

-- 2x
local glLinePosBufID = ffi.new'GLuint[1]'
local glLineVelBufID = ffi.new'GLuint[1]'

local env
local accumStarPointShader
local accumStarLineShader
local renderAccumShader

local fbo
local fbotex

local tempMin = 3300
local tempMax = 8000
local tempTex

function App:initGL(...)
	App.super.initGL(self, ...)

	gl.glDisable(gl.GL_DEPTH_TEST)
	gl.glBlendFunc(gl.GL_SRC_ALPHA, gl.GL_ONE)

	self.view.znear = 1
	self.view.zfar = 10000


	local data = file['datasets/gaia/points/points-9col.f32']
	n = #data / ffi.sizeof'pt_t'

	env = CLEnv{precision='float', size=n}
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

	local s = ffi.cast('char*', data)
	local cpuPointBuf = ffi.cast('pt_t*', s)
	
	local cpuLinePosBuf = ffi.new('_float4[?]', 2*n)	-- pts buf x number of vtxs
	local cpuLineVelBuf = ffi.new('_float4[?]', 2*n)

	for i=0,n-1 do
		for j=0,1 do
			cpuLinePosBuf[j+2*i].x = cpuPointBuf[i].pos[0]
			cpuLinePosBuf[j+2*i].y = cpuPointBuf[i].pos[1]
			cpuLinePosBuf[j+2*i].z = cpuPointBuf[i].pos[2]
			cpuLinePosBuf[j+2*i].w = cpuPointBuf[i].lum
			cpuLineVelBuf[j+2*i].x = cpuPointBuf[i].vel[0]
			cpuLineVelBuf[j+2*i].y = cpuPointBuf[i].vel[1]
			cpuLineVelBuf[j+2*i].z = cpuPointBuf[i].vel[2]
			cpuLineVelBuf[j+2*i].w = j
		end
	end
	
	gl.glGenBuffers(1, glPointBufID)
	gl.glBindBuffer(gl.GL_ARRAY_BUFFER, glPointBufID[0])
	gl.glBufferData(gl.GL_ARRAY_BUFFER, n*ffi.sizeof'pt_t', cpuPointBuf, gl.GL_STATIC_DRAW)
	
	gl.glGenBuffers(1, glLinePosBufID)
	gl.glBindBuffer(gl.GL_ARRAY_BUFFER, glLinePosBufID[0])
	gl.glBufferData(gl.GL_ARRAY_BUFFER, 2*n*ffi.sizeof'_float4', cpuLinePosBuf, gl.GL_STATIC_DRAW)

	gl.glGenBuffers(1, glLineVelBufID)
	gl.glBindBuffer(gl.GL_ARRAY_BUFFER, glLineVelBufID[0])
	gl.glBufferData(gl.GL_ARRAY_BUFFER, 2*n*ffi.sizeof'_float4', cpuLineVelBuf, gl.GL_STATIC_DRAW)
	
	gl.glBindBuffer(gl.GL_ARRAY_BUFFER, 0)
	
	--refreshPoints()
	
	accumStarPointShader = GLProgram{
		vertexCode = [[
#version 130
attribute float lum;
attribute float temp;
varying float lumv;
varying float tempv;
void main() {
	vec4 vtx = vec4(gl_Vertex.xyz, 1.);
	gl_Position = gl_ModelViewProjectionMatrix * vtx;
	lumv = lum;
	tempv = temp;
}
]],
		fragmentCode = template([[
<?
local clnumber = require 'cl.obj.number'
?>
#version 130
uniform sampler2D tempTex;
varying float lumv;
varying float tempv;
uniform float alpha;
void main() {
	float lumf = lumv;
	
	float z = gl_FragCoord.z / gl_FragCoord.w;
	z *= .001;
	lumf *= 1. / (z * z);
	
	vec2 d = gl_PointCoord.xy * 2. - 1.;
	float rsq = dot(d,d);
	lumf *= 1. / (10. * rsq + .1);

	float tempfrac = (tempv - <?=clnumber(tempMin)?>) * <?=clnumber(1/(tempMax - tempMin))?>;
	vec3 tempcolor = texture2D(tempTex, vec2(tempfrac, .5)).rgb;
	gl_FragColor = vec4(tempcolor * lumf * alpha, 1.);
}
]],			{
				tempMin = tempMin,
				tempMax = tempMax,
			}),
		uniforms = {
			tempTex = 0,
		},
	}
	accumStarPointShader:useNone()
	glreport'here'

	accumStarLineShader = GLProgram{
		vertexCode = [[
#version 130
attribute vec4 vel;
uniform float velScalar;
uniform bool normalizeVel;
varying float lum;
void main() {
	vec4 vtx = vec4(gl_Vertex.xyz, 1.);

	vec3 _vel = vel.xyz;
	if (normalizeVel) _vel = normalize(_vel);
	
	//using an extra channel
	float end = vel.w;
	//trying to use a shader variable:	
	//float end = float(gl_VertexID % 2 == 0);
	vtx.xyz += _vel * end * velScalar;
	
	
	gl_Position = gl_ModelViewProjectionMatrix * vtx;
	lum = 1.;//gl_Vertex.w;
}
]],
		fragmentCode = [[
#version 130
varying float lum;
uniform float alpha;
void main() {
	
	float _lum = lum;
	
	float z = gl_FragCoord.z / gl_FragCoord.w;
	z *= .001;
	_lum *= 1. / (z * z);
	
	vec2 d = gl_PointCoord.xy * 2. - 1.;
	float rsq = dot(d,d);
	_lum *= 1. / (10. * rsq + .1);

	vec3 color = vec3(.1, 1., .1) * _lum * alpha;
	gl_FragColor = vec4(color, 1.); 
	//gl_FragColor = vec4(1., 1., 1., 100.);
}
]],
	}
	accumStarLineShader:useNone()
	glreport'here'

	renderAccumShader = GLProgram{
		vertexCode = [[
#version 130
varying vec2 texcoord;
void main() {
	texcoord = gl_Vertex.xy;
	gl_Position = vec4(gl_Vertex.x * 2. - 1., gl_Vertex.y * 2. - 1., 0., 1.);
}
]],
		fragmentCode = template[[
<?
local clnumber = require 'cl.obj.number'
?>
varying vec2 texcoord;
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
	renderAccumShader:useNone()

	hsvtex = GLHSVTex(1024, nil, true)

--[[
	-- https://www.tannerhelland.com/4435/convert-temperature-rgb-algorithm-code/
	local tempTexWidth = 1024
	local tempImg = Image(tempTexWidth, 1, 3, 'unsigned char', function(i,j)
		local frac = (i + .5) / tempTexWidth
		local temp = frac * (tempMax - tempMin) + tempMin
		local t = temp / 100
		local r = t <= 66 and 255 or math.clamp(329.698727446 * ((t - 60) ^ -0.1332047592), 0, 255)
		local g = t <= 66
			and math.clamp(99.4708025861 * math.log(t) - 161.1195681661, 0, 255)
			or math.clamp(288.1221695283 * ((t - 60) ^ -0.0755148492), 0, 255)
		local b = t >= 66 
			and 255 or (
				t <= 19 
					and 0 
					or math.clamp(138.5177312231 * math.log(t - 10) - 305.0447927307, 0, 255)
			)
		return r,g,b
	end)
--]]
-- [[ black body color table from http://www.vendian.org/mncharity/dir3/blackbody/UnstableURLs/bbr_color_D58.html
	local rgbs = table()
	for l in io.lines'bbr_color_D58.txt' do
		if l ~= '' and l:sub(1,1) ~= '#' then
			local cmf = l:sub(11,15)
			if cmf == '10deg' then
				local temp = tonumber(l:sub(2,6):trim())
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
--]]
	tempTex = GLTex2D{
		width = tempImg.width,
		height = 1,
		internalFormat = gl.GL_RGB,
		format = gl.GL_RGB,
		type = gl.GL_UNSIGNED_BYTE,
		magFilter = gl.GL_LINEAR,
		minFilter = gl.GL_NEAREST,
		wrap = {gl.GL_CLAMP_TO_EDGE, gl.GL_CLAMP_TO_EDGE},
		image = tempImg,
	}
end

-- _G so that sliderFloatTable can use them
alphaValue = 1
pointSize = 2	-- TODO point size according to luminosity
hsvRangeValue = .2
hdrScaleValue = .001
hdrGammaValue = 1
bloomLevelsValue = 1
showDensityValue = false
velScalarValue = 1
drawPoints = true
drawLines = false
normalizeVelValue = 0

local lastWidth, lastHeight
function App:update()
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
			gl.glClear(gl.GL_COLOR_BUFFER_BIT)

			gl.glEnable(gl.GL_BLEND)

			
			if drawPoints then
				accumStarPointShader:use()
				if accumStarPointShader.uniforms.alpha then
					gl.glUniform1f(accumStarPointShader.uniforms.alpha.loc, alphaValue)
				end

				tempTex:bind()
				
				gl.glPointSize(pointSize)

				gl.glEnableClientState(gl.GL_VERTEX_ARRAY)
				gl.glBindBuffer(gl.GL_ARRAY_BUFFER, glPointBufID[0])
				gl.glVertexPointer(3, gl.GL_FLOAT, ffi.sizeof'pt_t', nil)

				if accumStarPointShader.attrs.lum then
					gl.glEnableVertexAttribArray(accumStarPointShader.attrs.lum.loc)
					gl.glBindBuffer(gl.GL_ARRAY_BUFFER, glPointBufID[0])
					gl.glVertexAttribPointer(accumStarPointShader.attrs.lum.loc, 1, gl.GL_FLOAT, false, ffi.sizeof'pt_t', ffi.cast('void*', (ffi.offsetof('pt_t', 'lum'))))
				end
				if accumStarPointShader.attrs.temp then
					gl.glEnableVertexAttribArray(accumStarPointShader.attrs.temp.loc)
					gl.glBindBuffer(gl.GL_ARRAY_BUFFER, glPointBufID[0])
					gl.glVertexAttribPointer(accumStarPointShader.attrs.temp.loc, 1, gl.GL_FLOAT, false, ffi.sizeof'pt_t', ffi.cast('void*', (ffi.offsetof('pt_t', 'temp'))))
				end

				gl.glBindBuffer(gl.GL_ARRAY_BUFFER, 0)
			
				gl.glDrawArrays(gl.GL_POINTS, 0, n)
			
				gl.glDisableClientState(gl.GL_VERTEX_ARRAY)
				if accumStarPointShader.attrs.lum then
					gl.glDisableVertexAttribArray(accumStarPointShader.attrs.lum.loc)
				end
				
				tempTex:unbind()
				accumStarPointShader:useNone()
				
				gl.glPointSize(1)
			end
			if drawLines then
				accumStarLineShader:use()
				if accumStarLineShader.uniforms.alpha then
					gl.glUniform1f(accumStarLineShader.uniforms.alpha.loc, alphaValue)
				end
				if accumStarLineShader.uniforms.velScalar then
					gl.glUniform1f(accumStarLineShader.uniforms.velScalar.loc, velScalarValue)
				end
				if accumStarLineShader.uniforms.normalizeVel then
					gl.glUniform1f(accumStarLineShader.uniforms.normalizeVel.loc, normalizeVelValue)
				end

				gl.glEnableClientState(gl.GL_VERTEX_ARRAY)
				gl.glBindBuffer(gl.GL_ARRAY_BUFFER, glLinePosBufID[0])
				gl.glVertexPointer(4, gl.GL_FLOAT, 0, nil)

				if accumStarLineShader.attrs.vel then
					gl.glEnableVertexAttribArray(accumStarLineShader.attrs.vel.loc)
					gl.glBindBuffer(gl.GL_ARRAY_BUFFER, glLineVelBufID[0])
					gl.glVertexAttribPointer(accumStarLineShader.attrs.vel.loc, 4, gl.GL_FLOAT, false, 0, nil)	-- 'normalize' doesn't seem to make a difference ...
				end
				
				gl.glBindBuffer(gl.GL_ARRAY_BUFFER, 0)
			
				gl.glDrawArrays(gl.GL_LINES, 0, 2*n)
			
				gl.glDisableClientState(gl.GL_VERTEX_ARRAY)
				if accumStarLineShader.attrs.vel then
					gl.glDisableVertexAttribArray(accumStarLineShader.attrs.vel.loc)
				end
				
				accumStarLineShader:useNone()
			end
			
			gl.glDisable(gl.GL_BLEND)
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
	fbotex:bind(0)
	hsvtex:bind(1)
	if renderAccumShader.uniforms.hdrScale then
		gl.glUniform1f(renderAccumShader.uniforms.hdrScale.loc, hdrScaleValue)
	end
	if renderAccumShader.uniforms.hdrGamma then
		gl.glUniform1f(renderAccumShader.uniforms.hdrGamma.loc, hdrGammaValue)
	end
	if renderAccumShader.uniforms.hsvRange then
		gl.glUniform1f(renderAccumShader.uniforms.hsvRange.loc, hsvRangeValue)
	end
	if renderAccumShader.uniforms.bloomLevels then
		gl.glUniform1f(renderAccumShader.uniforms.bloomLevels.loc, bloomLevelsValue)
	end
	if renderAccumShader.uniforms.showDensity then
		gl.glUniform1i(renderAccumShader.uniforms.showDensity.loc, showDensityValue and 1 or 0)
	end

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

local bool = ffi.new'bool[1]'
local function checkboxTable(title, t, key, ...)
	bool[0] = t[key]
	local result = ig.igCheckbox(title, bool, ...)
	if result then t[key] = bool[0] end
	return result
end

function App:updateGUI()
	sliderFloatTable('point size', _G, 'pointSize', 1, 10)
	sliderFloatTable('alpha value', _G, 'alphaValue', 0, 1000, '%.7f', 10)
	
	sliderFloatTable('hdr scale', _G, 'hdrScaleValue', 0, 1000, '%.7f', 10)
	sliderFloatTable('hdr gamma', _G, 'hdrGammaValue', 0, 1000, '%.7f', 10)
	sliderFloatTable('hsv range', _G, 'hsvRangeValue', 0, 1000, '%.7f', 10)
	sliderFloatTable('bloom levels', _G, 'bloomLevelsValue', 0, 8)
	checkboxTable('show density', _G, 'showDensityValue')

	checkboxTable('draw points', _G, 'drawPoints')
	
	checkboxTable('draw lines', _G, 'drawLines')
	
	sliderFloatTable('vel scalar', _G, 'velScalarValue', 0, 1000000000, '%.7f', 10)

	checkboxTable('normalize velocity', _G, 'normalizeVelValue')

	sliderFloatTable('fov y', self.view, 'fovY', 0, 180)

	ig.igImage(
		ffi.cast('void*', ffi.cast('intptr_t', tempTex.id)),
		ig.ImVec2(128, 24),
		ig.ImVec2(0,0),
		ig.ImVec2(1,1))
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
