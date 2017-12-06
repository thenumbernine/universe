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
local bit = require 'bit'
local glreport = require 'gl.report'

local App = class(Orbit(View.apply(ImGuiApp)))
App.title = 'FoG tool'
App.viewDist = 2

local buf = ffi.new'GLuint[1]'
local n
local cdata
local env
function App:initGL()

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
	
	gl.glGenBuffers(1, buf)
	gl.glBindBuffer(gl.GL_ARRAY_BUFFER, buf[0])
	gl.glBufferData(gl.GL_ARRAY_BUFFER, 3*n*ffi.sizeof'GLdouble', cdata, gl.GL_STATIC_DRAW)
	gl.glVertexPointer(3, gl.GL_DOUBLE, 0, nil)
	gl.glBindBuffer(gl.GL_ARRAY_BUFFER, 0)
	glreport'here'
end

function App:update()
	gl.glClear(bit.bor(gl.GL_DEPTH_BUFFER_BIT, gl.GL_COLOR_BUFFER_BIT))
	gl.glEnableClientState(gl.GL_VERTEX_ARRAY)
	gl.glBindBuffer(gl.GL_ARRAY_BUFFER, buf[0])
	gl.glDrawArrays(gl.GL_POINTS, 0, n)
	gl.glBindBuffer(gl.GL_ARRAY_BUFFER, 0)
	gl.glDisableClientState(gl.GL_VERTEX_ARRAY)
	glreport'here'
	App.super.update(self)
end

function App:updateGUI()
end

App():run()
