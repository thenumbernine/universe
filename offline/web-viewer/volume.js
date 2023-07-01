/*
timeline left and right
force directed graph 1D up and down
sync up the points
*/

window.requestAnimFrame = (function(){
	return window.requestAnimationFrame    || 
		window.webkitRequestAnimationFrame || 
		window.mozRequestAnimationFrame    || 
		window.oRequestAnimationFrame      || 
		window.msRequestAnimationFrame     || 
		function(callback) {
			window.setTimeout(callback, 1000 / 60);
		};
})();

//http://stackoverflow.com/questions/3954438/remove-item-from-array-by-value
Array.prototype.remove = function() {
	var what, a= arguments, L= a.length, ax;
	while(L && this.length){
		what= a[--L];
		while((ax= this.indexOf(what))!= -1){
			this.splice(ax, 1);
		}
	}
	return this;
};

(function($){
	//http://stackoverflow.com/questions/476679/preloading-images-with-jquery
	$.fn.preload = function(done) {
		var checklist = this.toArray();
		this.each(function() {
			$('<img>').attr({src:this}).load(function() {
				checklist.remove($(this).attr('src'));
				if (checklist.length == 0 && done !== undefined) done();
			});
		});
	};
})(jQuery);

function error(s) {
	$('<span>', {text:s}).prependTo(document.body);
	throw s;
}

function Shader(args) {
	var code;
	if (args.id) {
		var src = $('#'+args.id);
		//assert(src.attr('type') == this.domType);
		code = src.text();
	}
	if (args.code) {
		code = args.code;
	}
	if (!code) throw "expected code or id";

	this.obj = gl.createShader(this.shaderType());
	gl.shaderSource(this.obj, code);
	gl.compileShader(this.obj);
	if (!gl.getShaderParameter(this.obj, gl.COMPILE_STATUS)) throw gl.getShaderInfoLog(this.obj);
}

function VertexShader(args) {
	Shader.call(this, args);
}
VertexShader.prototype = {
	domType : 'x-shader/x-vertex',
	shaderType : function() { return gl.VERTEX_SHADER; }
};

function FragmentShader(args) {
	Shader.call(this, args);
}
FragmentShader.prototype = {
	domType : 'x-shader/x-fragment',
	shaderType : function() { return gl.FRAGMENT_SHADER; }
};

function ShaderProgram(args) {
	var thiz = this;
	this.vertexShader = args.vertexShader;
	if (!this.vertexShader) this.vertexShader = new VertexShader({id:args.vertexCodeID, code:args.vertexCode});
	if (!this.vertexShader) throw "expected vertexShader or vertexCode or vertexCodeID";

	this.fragmentShader = args.fragmentShader;
	if (!this.fragmentShader) this.fragmentShader = new FragmentShader({id:args.fragmentCodeID, code:args.fragmentCode});
	if (!this.fragmentShader) throw "expected fragmentShader or fragmentCode or fragmentCodeID";

	this.obj = gl.createProgram();
	gl.attachShader(this.obj, this.vertexShader.obj);
	gl.attachShader(this.obj, this.fragmentShader.obj);
	
	gl.linkProgram(this.obj);
	if (!gl.getProgramParameter(this.obj, gl.LINK_STATUS)) {
		error("Could not initialize shaders");
	}

	gl.useProgram(this.obj);

	this.attrs = {};
	if (args.attrs) {
		$.each(args.attrs, function(i,k) {
			thiz.attrs[k] = gl.getAttribLocation(thiz.obj, k);
		});
	}

	this.uniforms = {};
	if (args.uniforms) {
		$.each(args.uniforms, function(i,k) {
			thiz.uniforms[k] = gl.getUniformLocation(thiz.obj, k);
		});
	}
}


//populated in R.init
var canvas;
var projMat, mvMat, rotMat;
var plainShader, volumeSliceShader;
var quadVtxBuf;
var cubeWireVtxBuf, cubeWireIndexBuf;
var cubeVtxBuf, cubeNormalBuf;
var gl;

var R = new function() {
	var thiz = this;

	this.init = function() {
		
		//build dom

		canvas = $('<canvas>', {
			css : { margin : 'auto' }
		}).appendTo(document.body).get(0);

		//get gl context

		try {
			gl = canvas.getContext('experimental-webgl');
		} catch (e) {
		}
		if (!gl) {
			error("Couldn't initialize WebGL =(");
		}

		projMat = mat4.create();
		mvMat = mat4.create();
		rotMat = mat4.create();
		mat4.identity(rotMat);

		mat4.identity(mvMat);
		mat4.lookAt(mvMat, [0,0,3], [0,0,0], [0,1,0]);

		//create shaders

		plainShader = new ShaderProgram({
			vertexCodeID : 'plain-vsh',
			fragmentCodeID : 'plain-fsh',
			attrs : ['vtx'],
			uniforms : ['projMat', 'mvMat']
		});

		gl.enableVertexAttribArray(plainShader.attrs.vtx);


		volumeSliceShader = new ShaderProgram({
			vertexCodeID : 'plain-vsh',
			fragmentCodeID : 'volume-slice-fsh',
			attrs : ['vtx'],
			uniforms : ['projMat', 'mvMat', 'volTex', 'hsvTex', 'gamma', 'dz', 'solidThreshold']
		});

		gl.uniform1f(volumeSliceShader.uniforms.dz, 1/volume.dim);
		gl.uniform1i(volumeSliceShader.uniforms.volTex, 0);
		gl.uniform1i(volumeSliceShader.uniforms.hsvTex, 1);
		gl.enableVertexAttribArray(volumeSliceShader.attrs.vtx);

		gl.useProgram(null);

		//create buffers

			//quad

		var quadVtxs = [
			-1,1,0,
			-1,-1,0,
			1,1,0,
			1,-1,0,
		];
		
		quadVtxBuf = gl.createBuffer();
		gl.bindBuffer(gl.ARRAY_BUFFER, quadVtxBuf);
		gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(quadVtxs), gl.STATIC_DRAW);

			//wireframe cube

		var cubeWireVtxs = [
			-1,-1,-1,
			1,-1,-1,
			-1,1,-1,
			1,1,-1,
			-1,-1,1,
			1,-1,1,
			-1,1,1,
			1,1,1,
		];
		
		cubeWireVtxBuf = gl.createBuffer();
		gl.bindBuffer(gl.ARRAY_BUFFER, cubeWireVtxBuf);
		gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(cubeWireVtxs), gl.STATIC_DRAW);

		var cubeWireIndexes = [
			0,1,1,3,3,2,2,0,
			4,5,5,7,7,6,6,4,
			0,4,1,5,2,6,3,7,
		];

		cubeWireIndexBuf = gl.createBuffer();
		gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, cubeWireIndexBuf);
		gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint16Array(cubeWireIndexes), gl.STATIC_DRAW);

		//init draw
		
		gl.activeTexture(gl.TEXTURE0);
		gl.clearColor(0,0,0,1);
		gl.enable(gl.BLEND);
		gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
		//gl.blendFunc(gl.SRC_ALPHA, gl.ONE);

		//draw
		
		$(window).resize(resize);
		var tmpRotMat = mat4.create();	
		var mouseDown;
		var lastX, lastY;
		$(canvas).mousedown(function(e) {
			mouseDown = true;
			lastX = e.pageX;
			lastY = e.pageY;
		});
		$(window).mouseup(function() {
			mouseDown = false;	
		});
		$(canvas).mousemove(function(e) {
			if (mouseDown) {
				var deltaX = e.pageX - lastX;
				var deltaY = e.pageY - lastY;
				lastX = e.pageX;
				lastY = e.pageY;
				mat4.identity(tmpRotMat);
				mat4.rotate(tmpRotMat, tmpRotMat, 
					Math.PI / 180 * Math.sqrt(deltaX*deltaX + deltaY*deltaY),
					[deltaY, deltaX, 0]);
				//mat4.translate(mvMat, mvMat, [10*deltaX/canvas.width, -10*deltaY/canvas.height, 0]);
				mat4.mul(rotMat, tmpRotMat, rotMat);
				draw();
			}
		});
	};

	this.redraw = function() {
		if (this.drawInterval) return;
		this.drawInterval = requestAnimFrame(this.drawCallback);
	};

	this.drawCallback = function() { 
		thiz.drawInterval = undefined; 
		draw();
	};

};

var volume = new function() {
	this.dim = 256;
	this.init = function(f32Buffer) {
		var colors = [
			[0,0,0],
			[0,0,1],
			[1,0,1],
			[1,0,0],
			[1,1,0],
			[1,1,1]
		];
		var hsvDim = 256;
		var data = new Uint8Array(hsvDim * 3);
		for (var i = 0; i < hsvDim; i++) {
			var f = (i+.5)/hsvDim;
			f *= colors.length;
			var ip = parseInt(f);
			f -= ip;
			var iq = (ip + 1) % colors.length;
			var g = 1. - f;	
			for (var k = 0; k < 3; k++) {
				data[k+3*i] = 255*(colors[ip][k] * g + colors[iq][k] * f);
			}
		}
		this.hsvTex = gl.createTexture();
		gl.bindTexture(gl.TEXTURE_2D, this.hsvTex);
		gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGB, hsvDim, 1, 0, gl.RGB, gl.UNSIGNED_BYTE, data);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
		gl.bindTexture(gl.TEXTURE_2D, null);

		if (!gl.getExtension('OES_texture_float')) console.warn("Can't find support for OES_texture_float");

		//var data = new Uint8Array(this.dim * this.dim * 3);
		this.slices = [[],[],[]];	//for each axii 
		for (var axis = 0; axis < 3; axis++) {
			var axis1 = (axis+1)%3;
			var axis2 = (axis+2)%3;
			for (var w = 0; w < this.dim; w++) {
				var slice = {}; 
				slice.tex = gl.createTexture();
				this.slices[axis].push(slice);
			}
		}
		
		console.log("building slices...");
		var data = new Float32Array(this.dim * this.dim); 

		var x = vec3.create();
		for (var axis = 0; axis < 3; axis++) {
			var axis1 = (axis+1)%3;
			var axis2 = (axis+2)%3;
			for (var w = 0; w < this.dim; w++) {
				var slice = this.slices[axis][w]; 
				for (var u = 0; u < this.dim; u++) {
					for (var v = 0; v < this.dim; v++) {
						x[axis] = w; 
						x[axis1] = u; 
						x[axis2] = v; 
						data[u+this.dim*v] = f32Buffer[x[0]+this.dim*(x[1]+this.dim*x[2])];
					}
				}
				gl.bindTexture(gl.TEXTURE_2D, slice.tex);
				gl.texImage2D(gl.TEXTURE_2D, 0, gl.LUMINANCE, this.dim, this.dim, 0, gl.LUMINANCE, gl.FLOAT, data);
				gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
				gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
				gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
				gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
				gl.bindTexture(gl.TEXTURE_2D, null);
			}
		}
		console.log("...done building slices");
			
		R.redraw();
	};
};

var gamma = 10.;
var drawWireframe = true;
var drawSlices = true;
var drawVolume = false;
$(document).ready(function() {
	$('#gamma-slider').slider({
		range : 'max',
		width : '200px',
		min : 1,
		max : 100,
		value : gamma, 
		slide : function(event, ui) {
			gamma = ui.value;
			R.redraw();
		}
	});
	$('#draw-wireframe').click(function(e) {
		drawWireframe = e.target.checked;
		R.redraw();
	});
	$('#draw-slices').click(function(e) {
		drawSlices = e.target.checked; 
		R.redraw();
	});
	$('#draw-volume').click(function(e) {
		drawVolume = e.target.checked;
		R.redraw();
	});
	var xhr = new XMLHttpRequest();
	xhr.open('GET', '../datasets/2mrs/density.vol', true);
	xhr.responseType = 'arraybuffer';
	xhr.addEventListener('load', e => {
		var arrayBuffer = this.response;
		var data = new DataView(arrayBuffer);
		
		var f32Buffer = new Float32Array(data.byteLength / Float32Array.BYTES_PER_ELEMENT);
		var len = f32Buffer.length;
		for (var jj = 0; jj < len; ++jj) {
			f32Buffer[jj] = data.getFloat32(jj * Float32Array.BYTES_PER_ELEMENT, true);
		}
	
		R.init();

		adjustSize();

		volume.init(f32Buffer);
	});
	xhr.send();

})

function adjustSize() {
	$(canvas)
		.attr('width', window.innerWidth-220)
		.attr('height', window.innerHeight-20);
	
	gl.viewport(0, 0, canvas.width, canvas.height);
	mat4.perspective(projMat, 45, canvas.width / canvas.height, .1, 100);
}

function resize() {
	adjustSize();
	R.redraw();
}

var viewMat = mat4.create();	
var objMat = mat4.create();
function draw() {
	
	mat4.multiply(viewMat, mvMat, rotMat);
	
	gl.colorMask(0,0,0,1);
	gl.clear(gl.COLOR_BUFFER_BIT);	//get a first clear in before disabling alpha-writing OR ELSE THE CANVAS WILL BE INITIALIZED AS COMPLETELY TRANSPARENT
	gl.colorMask(1,1,1,0);	//don't allow destination alpha writes OR ELSE YOUR BLENDED IMAGE OPERATIONS WILL CAUSE YOUR CANVAS TO BECOME SEMI-TRANSPARENT
	gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
	
	if (drawWireframe) {
		gl.useProgram(plainShader.obj);
		gl.bindBuffer(gl.ARRAY_BUFFER, cubeWireVtxBuf);
		gl.vertexAttribPointer(plainShader.attrs.vtx, 3, gl.FLOAT, false, 0, 0);	//3=stride
		gl.uniformMatrix4fv(plainShader.uniforms.projMat, false, projMat);
		gl.uniformMatrix4fv(plainShader.uniforms.viewMat, false, mvMat);
		gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, cubeWireIndexBuf);
		gl.drawElements(gl.LINES, 24, gl.UNSIGNED_SHORT, 0);
	}

	if (drawSlices) {
		gl.useProgram(volumeSliceShader.obj);
		gl.bindBuffer(gl.ARRAY_BUFFER, quadVtxBuf);
		gl.vertexAttribPointer(volumeSliceShader.attrs.vtx, 3, gl.FLOAT, false, 0, 0);	//3=stride
		gl.uniformMatrix4fv(volumeSliceShader.uniforms.projMat, false, projMat);
		gl.uniform1f(volumeSliceShader.uniforms.gamma, gamma);
		gl.activeTexture(gl.TEXTURE1);
		gl.bindTexture(gl.TEXTURE_2D, volume.hsvTex);
		gl.activeTexture(gl.TEXTURE0);

		//now pick the dir and order (front vs back)
		// based on the major axis
		// (highest z-component of each axis)
		var axis = 0;
		var bestZ = viewMat[2];
		for (var i = 1; i < 3; i++) {
			var z = viewMat[4*i+2];
			if (Math.abs(z) > Math.abs(bestZ)) {
				bestZ = z;
				axis = i;
			}
		}

		var firstI, lastI, stepI;
		if (bestZ < 0) {
			firstI = volume.slices[axis].length-1;
			lastI = -1;
			stepI = -1;
		} else {
			firstI = 0;
			lastI = volume.slices[axis].length;
			stepI = 1;
		}

		switch (axis) {
		case 0://x-align: 
			mat4.rotate(viewMat, viewMat, Math.PI/2, [0,1,0]);
			mat4.rotate(viewMat, viewMat, Math.PI/2, [0,0,1]);
			break;	
		case 1:	//y-align:
			mat4.rotate(viewMat, viewMat, Math.PI/2, [-1,0,0]);
			mat4.rotate(viewMat, viewMat, Math.PI/2, [0,0,-1]);
			break;
		case 2:
			break;	//z-aligned is default
		}

		for (var i = firstI; i != lastI; i+=stepI) {
			mat4.translate(objMat, viewMat, [0,0,2*(i/(volume.slices[axis].length-1))-1]);
			var slice = volume.slices[axis][i];
			gl.bindTexture(gl.TEXTURE_2D, slice.tex);
			gl.uniformMatrix4fv(volumeSliceShader.uniforms.mvMat, false, objMat);
			gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);	//4 = vtxcount
		}
	}
}

