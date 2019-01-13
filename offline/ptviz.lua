#!/usr/bin/env luajit
-- simple pointcloud visualizer
local ffi = require 'ffi'
require 'ext'
local ig = require 'ffi.imgui'
local template = require 'template'
local ImGuiApp = require 'imguiapp'
local Orbit = require 'glapp.orbit'
local View = require 'glapp.view'
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
	float temp;
	float lum;
} pt_t;
]]

local cpuPtsBuf
local glPtsBufID = ffi.new'GLuint[1]'
local n
local env
local accumStarShader
local renderAccumShader

local fbo
local fbotex

function App:initGL(...)
	App.super.initGL(self, ...)

	gl.glDisable(gl.GL_DEPTH_TEST)
	gl.glBlendFunc(gl.GL_SRC_ALPHA, gl.GL_ONE)

	self.view.znear = 1
	self.view.zfar = 10000


	local data = file['datasets/gaia/points/points.f32']
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
	local pts = ffi.cast('pt_t*', s)
	cpuPtsBuf = ffi.new('_float4[?]', n)
	-- distances are in kparsecs
	for i=0,n-1 do
		cpuPtsBuf[i].x = pts[i].pos[0]
		cpuPtsBuf[i].y = pts[i].pos[1]
		cpuPtsBuf[i].z = pts[i].pos[2]
		cpuPtsBuf[i].w = pts[i].lum
	end
	
	gl.glGenBuffers(1, glPtsBufID)
	gl.glBindBuffer(gl.GL_ARRAY_BUFFER, glPtsBufID[0])
	gl.glBufferData(gl.GL_ARRAY_BUFFER, n*ffi.sizeof'_float4', cpuPtsBuf, gl.GL_STATIC_DRAW)
	gl.glBindBuffer(gl.GL_ARRAY_BUFFER, 0)
	--refreshPoints()
	
	accumStarShader = GLProgram{
		vertexCode = template[[
<?
local clnumber = require 'cl.obj.number'
?>
#version 130
varying vec4 color;
varying float lum;
void main() {
	vec4 vtx = vec4(gl_Vertex.xyz, 1.);
	gl_Position = gl_ModelViewProjectionMatrix * vtx;
	lum = gl_Vertex.w;
	color = gl_Color;
}
]],
		fragmentCode = [[
#version 130
varying vec4 color;
varying float lum;
uniform float alpha;
void main() {
	float _lum = lum;
	
	float z = gl_FragCoord.z / gl_FragCoord.w;
	_lum *= .0001 / (z * z);
	
	vec2 d = gl_PointCoord.xy * 2. - 1.;
	float rsq = dot(d,d);
	_lum *= 1. / (rsq + .1);

	//gl_FragColor = vec4(color.rgb, color.a * _lum * alpha);
	gl_FragColor = vec4(color.rgb, lum);
	//gl_FragColor = vec4(color.rgb, 1.);
}
]],
	}
	accumStarShader:useNone()
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

	//tone mapping, from https://learnopengl.com/Advanced-Lighting/HDR
	//gl_FragColor.rgb = gl_FragColor.rgb / (gl_FragColor.rgb + vec3(1.));
	
	gl_FragColor.rgb = pow(gl_FragColor.rgb, vec3(1. / hdrGamma));

	gl_FragColor.rgb = log(gl_FragColor.rgb + vec3(1.));
	
	//gl_FragColor = texture1D(hsvtex, log(gl_FragColor.a + 1.) * hsvRange);
	
	gl_FragColor.a = 1.;
}
]],
		uniforms = {
			fbotex = 0,
			hsvtex = 1,
		},
	}
	renderAccumShader:useNone()

	hsvtex = GLHSVTex(1024, nil, true)
end

local alphaValue = ffi.new('float[1]', .05)
local pointSize = ffi.new('float[1]', 1)
local hsvRangeValue = ffi.new('float[1]', .2)
local hdrScaleValue = ffi.new('float[1]', .001)
local hdrGammaValue = ffi.new('float[1]', 2.2)
local bloomLevelsValue = ffi.new('float[1]', 1)

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

			gl.glColor3f(1,1,1)
			gl.glEnable(gl.GL_BLEND)

			gl.glPointSize(pointSize[0])

			gl.glEnableClientState(gl.GL_VERTEX_ARRAY)
			
			gl.glBindBuffer(gl.GL_ARRAY_BUFFER, glPtsBufID[0])
			gl.glVertexPointer(4, gl.GL_FLOAT, 0, nil)	-- call every frame
			
			gl.glBindBuffer(gl.GL_ARRAY_BUFFER, 0)
			
			accumStarShader:use()
			if accumStarShader.uniforms.alpha then
				gl.glUniform1f(accumStarShader.uniforms.alpha.loc, alphaValue[0])
			end
			gl.glDrawArrays(gl.GL_POINTS, 0, n)
			accumStarShader:useNone()
			
			gl.glDisableClientState(gl.GL_VERTEX_ARRAY)
			
			gl.glPointSize(1)
			
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
		gl.glUniform1f(renderAccumShader.uniforms.hdrScale.loc, hdrScaleValue[0])
	end
	if renderAccumShader.uniforms.hdrGamma then
		gl.glUniform1f(renderAccumShader.uniforms.hdrGamma.loc, hdrGammaValue[0])
	end
	if renderAccumShader.uniforms.hsvRange then
		gl.glUniform1f(renderAccumShader.uniforms.hsvRange.loc, hsvRangeValue[0])
	end
	if renderAccumShader.uniforms.bloomLevels then
		gl.glUniform1f(renderAccumShader.uniforms.bloomLevels.loc, bloomLevelsValue[0])
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

local float = ffi.new'float[1]'
local int = ffi.new'int[1]'
function App:updateGUI()
	ig.igSliderFloat('point size', pointSize, 1, 10)
	ig.igSliderFloat('alpha value', alphaValue, 0, 1, '%.7f', 10)
	ig.igSliderFloat('hdr scale', hdrScaleValue, 0, 1000, '%.7f', 10)
	ig.igSliderFloat('hdr gamma', hdrGammaValue, 0, 1000, '%.7f', 10)
	ig.igSliderFloat('hsv range', hsvRangeValue, 0, 1000, '%.7f', 10)
	ig.igSliderFloat('bloom levels', bloomLevelsValue, 0, 8)
	
	float[0] = self.view.fovY
	if ig.igSliderFloat('fov y', float, 0, 180) then
		self.view.fovY = float[0]
	end
end

App():run()
