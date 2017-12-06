#!/usr/bin/env luajit
--[[
here's my attempt at the FOG removal 
courtesy of OpenCL and LuaJIT
--]]
require 'ext'
local template = require 'template'
local vec4f = require 'ffi.vec.vec4f'
local bit = require 'bit'
local ffi = require 'ffi'
local gl = require 'gl'
local ig = require 'ffi.imgui'
local ImGuiApp = require 'imguiapp'
local Orbit = require 'glapp.orbit'
local View = require 'glapp.view'
local glreport = require 'gl.report'
local GLProgram = require 'gl.program'
local clnumber = require 'cl.obj.number'
local CLEnv = require 'cl.obj.env'

local App = class(Orbit(View.apply(ImGuiApp)))
App.title = 'FoG tool'
App.viewDist = 2

local glbuf = ffi.new'GLuint[1]'
local n
local clbuf
local env

function refreshPoints()
	gl.glBindBuffer(gl.GL_ARRAY_BUFFER, glbuf[0])
	local glbufmap = gl.glMapBuffer(gl.GL_ARRAY_BUFFER, gl.GL_WRITE_ONLY)
	clbuf:toCPU(glbufmap)
	gl.glUnmapBuffer(gl.GL_ARRAY_BUFFER)
	gl.glBindBuffer(gl.GL_ARRAY_BUFFER, 0)
end

function App:initGL(...)
	App.super.initGL(self, ...)

	self.view.znear = .001
	self.view.zfar = 3

	-- init cl after gl so it has sharing
	local data = file['datasets/sdss3/points/spherical.f64']	-- format is double z, ra, dec
	n = #data / (3 * ffi.sizeof'double')
	print('n='..n)
	local cdata = ffi.new('double[?]', 3*n)
	ffi.copy(cdata, data, #data)

	env = CLEnv{precision='double', size=n}

	local real3code = [[
typedef union {
	real s[3];
	struct { real s0, s1, s2; };
	struct { real x, y, z; };
} real3;
]]

	ffi.cdef(real3code)
	env.code = env.code .. real3code

	clbuf = env:buffer{type='real3', data=cdata}
	local cllinks = env:buffer{type='char'}

	env:kernel{
		argsOut = {cllinks},
		argsIn = {clbuf},
		body = [[
		]],
	}
	
	gl.glGenBuffers(1, glbuf)
	gl.glBindBuffer(gl.GL_ARRAY_BUFFER, glbuf[0])
	gl.glBufferData(gl.GL_ARRAY_BUFFER, 3*n*ffi.sizeof'GLdouble', nil, gl.GL_STATIC_DRAW)
	gl.glBindBuffer(gl.GL_ARRAY_BUFFER, 0)
	refreshPoints()

	sphereCoordShader = GLProgram{
		vertexCode = template([[
#define M_PI <?=clnumber(math.pi)?>
varying vec4 color;
void main() {
	float z = gl_Vertex.x;
	float rad_ra = gl_Vertex.y * M_PI / 180.;
	float rad_dec = gl_Vertex.z * M_PI / 180.;

	float cos_dec = cos(rad_dec);
	vec4 vtx = vec4(
		z * cos(rad_ra) * cos_dec,
		z * sin(rad_ra) * cos_dec,
		z * sin(rad_dec),
		1.);
	gl_Position = gl_ModelViewProjectionMatrix * vtx;
	color = gl_Color;
}
]], {
	clnumber = clnumber,
}),
		fragmentCode = [[
varying vec4 color;
void main() {
	float z = gl_FragCoord.z / gl_FragCoord.w;
	float lum = max(.005, .001 / (z * z));
	gl_FragColor = vec4(color.rgb, color.a * lum);
}
]],
	}
	sphereCoordShader:useNone()
	glreport'here'
end

local useAlpha = ffi.new('bool[1]', false)
local alphaValue = ffi.new('float[1]', 1)
local pointSize = ffi.new('float[1]', 1)
function App:update()
	gl.glClear(bit.bor(gl.GL_DEPTH_BUFFER_BIT, gl.GL_COLOR_BUFFER_BIT))
	
	gl.glBlendFunc(gl.GL_SRC_ALPHA, gl.GL_ONE)
	if useAlpha[0] then 
		gl.glEnable(gl.GL_BLEND)
	end

	gl.glColor4f(1,1,1,alphaValue[0])
	gl.glPointSize(pointSize[0])
	gl.glEnable(gl.GL_POINT_SMOOTH)
	gl.glHint(gl.GL_POINT_SMOOTH_HINT, gl.GL_NICEST)

	gl.glEnableClientState(gl.GL_VERTEX_ARRAY)
	sphereCoordShader:use()
	gl.glBindBuffer(gl.GL_ARRAY_BUFFER, glbuf[0])
	gl.glVertexPointer(3, gl.GL_DOUBLE, 0, nil)	-- call every frame
	
	gl.glDrawArrays(gl.GL_POINTS, 0, n)
	
	gl.glBindBuffer(gl.GL_ARRAY_BUFFER, 0)
	sphereCoordShader:useNone()
	gl.glDisableClientState(gl.GL_VERTEX_ARRAY)
	
	gl.glPointSize(1)
	
	gl.glDisable(gl.GL_BLEND)
	
	glreport'here'
	App.super.update(self)
end

function App:updateGUI()
	ig.igSliderFloat('point size', pointSize, 1, 10)
	ig.igCheckbox('use alpha', useAlpha)
	ig.igSliderFloat('alpha value', alphaValue, 0, 1, '%.3f', 10)
end

App():run()
