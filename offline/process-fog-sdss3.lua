#!/usr/bin/env luajit
--[[
here's my attempt at the FOG removal 
courtesy of OpenCL and LuaJIT
--]]
require 'ext'
local ffi = require 'ffi'
local ImGuiApp = require 'imguiapp'
local Orbit = require 'glapp.orbit'
local View = require 'glapp.view'
local gl = require 'gl'
local ig = require 'ffi.imgui'
local bit = require 'bit'
local glreport = require 'gl.report'
local GLProgram = require 'gl.program'

local App = class(Orbit(View.apply(ImGuiApp)))
App.title = 'FoG tool'
App.viewDist = 2

local glbuf = ffi.new'GLuint[1]'
local n
local cdata
local env

function refreshPoints()
	gl.glBindBuffer(gl.GL_ARRAY_BUFFER, glbuf[0])
	gl.glBufferData(gl.GL_ARRAY_BUFFER, 3*n*ffi.sizeof'GLdouble', cdata, gl.GL_STATIC_DRAW)
	gl.glBindBuffer(gl.GL_ARRAY_BUFFER, 0)
end

function App:initGL(...)
	App.super.initGL(self, ...)
	
	-- init cl after gl so it has sharing
	local data = file['datasets/sdss3/points/spherical.f64']	-- format is double z, ra, dec
	n = #data / (3 * ffi.sizeof'double')
	print(n)
	cdata = ffi.new('double[?]', 3*n)
	ffi.copy(cdata, data, #data)

	env = require 'cl.obj.env'{precision='double', size=n}

	local real3code = [[
typedef union {
	real s[3];
	struct { real s0, s1, s2; };
	struct { real x, y, z; };
} real3;
]]

	ffi.cdef(real3code)
	env.code = env.code .. real3code

	local clbuf = env:buffer{data=cdata}
	local cllinks = env:buffer{type='char'}

	env:kernel{
		argsOut = {cllinks},
		argsIn = {clbuf},
		body = [[
		]],
	}
	
	gl.glGenBuffers(1, glbuf)
	refreshPoints()
	
	sphereCoordShader = GLProgram{
		vertexCode = [[
varying vec4 color;
void main() {
	float z = gl_Vertex.x;
	float rad_ra = gl_Vertex.y;
	float rad_dec = gl_Vertex.z;

	float cos_dec = cos(rad_dec);
	vec4 vtx = vec4(
		z * cos(rad_ra) * cos_dec,
		z * sin(rad_ra) * cos_dec,
		z * sin(rad_dec),
		1.);
	gl_Position = gl_ModelViewProjectionMatrix * vtx;
	color = gl_Color;
}
]],
		fragmentCode = [[
varying vec4 color;
void main() {
	gl_FragColor = color;
}
]],
	}
	sphereCoordShader:useNone()
	glreport'here'
end

local alpha = ffi.new('float[1]', 1)
local pointSize = ffi.new('float[1]', 1)
function App:update()
	gl.glClear(bit.bor(gl.GL_DEPTH_BUFFER_BIT, gl.GL_COLOR_BUFFER_BIT))
	
	gl.glEnable(gl.GL_BLEND)
	gl.glBlendFunc(gl.GL_SRC_ALPHA, gl.GL_ONE)
	gl.glColor4f(1,1,1,alpha[0])
	gl.glPointSize(pointSize[0])
	gl.glEnable(gl.GL_POINT_SMOOTH)

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
	ig.igSliderFloat('size', pointSize, 1, 10)
	ig.igSliderFloat('alpha', alpha, 0, 1, '%.3f', 10)
end

App():run()
