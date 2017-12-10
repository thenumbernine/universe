//populated in init
var canvas;
var gl;
var glutil;
var panel, panelIsOpen;
var distanceElem;
var orbitCoordsElem;
var gridSizeElem;
var targetElem;
var descElem;
var fpsElem;

var pointShader;
var selectedShader;
var selected, hover;
var galaxyTex;

var gridObj;
var gridAlpha = .25;
var showGrid = false;
var gridScale = undefined;

function Highlight() {
	this.set = undefined;	//which set this belongs to
	this.index = undefined;	//which index
	this.arrayBuf = new Float32Array(3);
	this.vtxBuf = new glutil.ArrayBuffer({
		data : this.arrayBuf,
		usage : gl.DYNAMIC_DRAW
	});
	this.sceneObj = new glutil.SceneObject({
		mode : gl.POINTS,
		attrs : {
			vertex : this.vtxBuf
		},
		shader : selectedShader,
		uniforms : {
			color : [1,0,0]
		}
	});
}
Highlight.prototype = {
	setPos : function(x,y,z) {
		this.arrayBuf[0] = x;
		this.arrayBuf[1] = y;
		this.arrayBuf[2] = z;
		this.vtxBuf.updateData(this.arrayBuf);
	}
};
var dataSets = [];
var dataSetsByName = {};

var lastMouseRot;
var panelCloseButton;	//

var KEY_LEFT_FLAG = 1;
var KEY_UP_FLAG = 2;
var KEY_RIGHT_FLAG = 4;
var KEY_DOWN_FLAG = 8;
var keysDownFlags = 0;

function closeSidePanel() {
	panelIsOpen = false;
	if (descElem) descElem.empty();
	refreshPanelSize();
}

function refreshPanelSize() {
	if (panelIsOpen) {
		panel.css('width', '375px');
		panel.css('height', window.innerHeight);
		if (!panelCloseButton) {
			panelCloseButton = $('<img>', {
				src:'close.png', 
				width:'48px',
				css:{
					position:'absolute',
					top:'10px',
					left:'300px',
					zIndex:1
				},
				click:function() {
					closeSidePanel();
				}
			});
		}
		panelCloseButton.appendTo(document.body);	
	} else {
		panel.css('width', '200px');
		panel.css('height', '330px');
		if (panelCloseButton) {
			panelCloseButton.remove();
			panelCloseButton = undefined;
		}
	}
}

var viewDistance = undefined;
var refreshDistanceInterval = undefined;
function refreshDistance() {
	if (refreshDistanceInterval !== undefined) return;
	refreshDistanceInterval = setInterval(doRefreshDistance, 500);
}
function doRefreshDistance() {
	refreshDistanceInterval = undefined;

	var centerX = selected.arrayBuf[0];
	var centerY = selected.arrayBuf[1];
	var centerZ = selected.arrayBuf[2];

	distanceElem.text(parseFloat(viewDistance).toFixed(4));
	orbitCoordsElem.text(centerX+', '+centerY+', '+centerZ);
	gridSizeElem.text(gridScale >= 1 ? (gridScale + ' Mpc') : '');
}

function setSelectedGalaxy(dataSet, pointIndex) {
	//console.log("selecting",dataSet,pointIndex);
	quat.identity(lastMouseRot);
	selected.set = dataSet;
	selected.index = pointIndex;
	if (dataSet === undefined || pointIndex === undefined) {
		selected.sceneObj.hidden = true;
	} else {
		selected.sceneObj.hidden = false;
		var i = 3*pointIndex;
		
		var x = dataSet.arrayBuffer[i++];
		var y = dataSet.arrayBuffer[i++];
		var z = dataSet.arrayBuffer[i++];
		selected.setPos(x,y,z);
		refreshDistance();

		var buildWikiPage = function(obj) {
			//todo pretty up the names
			var cols;
			if (dataSet.title == '2MRS') {
				cols = ['_2MASS_ID', 'bibliographicCode', 'galaxyName', 'galaxyType', 'sourceOfType'];
			} else if (dataSet.title == 'SIMBAD') {
				cols = ['id', 'otype'];
				obj.otype = otypeDescs[obj.otype] || obj.otype;
			}
			$.each(cols, function(k,col) {
				$('<div>', {text:col+': '+obj[col]+' '}).appendTo(targetElem);
			});

			closeSidePanel();

			var search;
			if (dataSet.title == '2MRS') {
				search = obj.galaxyName;
				search = search.split('_');
				for (var j = 0; j < search.length; j++) {
					var v = parseFloat(search[j]);
					if (v == v) search[j] = v;	//got a valid number
				}
				search = search.join(' ');
			} else if (dataSet.title == 'SIMBAD') {
				search = obj.id;
			}
			
			//console.log("searching "+search);
			descElem.empty();
			/*
			$.ajax({
				url:'http://en.wikipedia.org/w/api.php',
				dataType:'jsonp',
				data:{action:'query', titles:search, format:'json', prop:'revisions'},
				cache:true,
				success:function(response) {
					console.log(response);
			*/
					$.ajax({
						url:'http://en.wikipedia.org/w/api.php',
						dataType:'jsonp',
						data:{action:'parse', page:search, format:'json', prop:'text'},
						cache:true,
						success:function(response) {
							//console.log(response);
							var parse = response.parse;
							if (!parse) return; 
							var title = parse.title
							if (!title) return; 
							var text = parse.text;
							if (!text) return;
							text = text['*'];
							if (!text) return;
							lastTitle = title;
							lastText = text;
							$('<br><br>').appendTo(descElem);
							$('<a>', {
								text:'Wikipedia:', 
								href:'http://www.wikipedia.org',
								css:{
									fontSize:'10pt'
								}
							})	.attr('target', '_blank')
								.appendTo(descElem);
							$('<h2>', {text:title}).appendTo(descElem);
							var textDiv = $('<div>').html(text).appendTo(descElem);
							$('a', textDiv).each(function() {
								var href = $(this).attr('href');
								if (href[0] == '#') {
									//http://stackoverflow.com/questions/586024/disabling-links-with-jquery
									$(this).after($(this).text());
									$(this).remove();
									return;
								}
								if (href[0] == '/') {
									$(this).attr('href', 'http://en.wikipedia.org'+href);
								}
								$(this).attr('target', '_blank');
							});


							panelIsOpen = true;
							refreshPanelSize();
						},
						error:function() {
							descElem.innerHTML("Error retrieving data");
							setTimeout(function() {
								descElem.empty();
							}, 3000);
						}
					});
			/*
				}
			});
			*/
		};

		//assumes the first set is 2MRS
		targetElem.empty();
		//if (dataSet.title == 'Simbad') {
		//	buildWikiPage(simbadCatalog[pointIndex]);	//for local json support.  this file is 1mb, so i'm not going to use that online.
		//} else
		if (dataSet.title == '2MRS' || dataSet.title == 'SIMBAD') {
			targetElem.append($('<img src="loading.gif" style="padding-top:10px; padding-left:10px"/>'));
			$.ajax({
				url : 'getpoint.lua',
				dataType : 'json',
				data : {
					set : dataSet.title.toLowerCase(),
					point : pointIndex
				},
				success : function(obj) {
					targetElem.empty();
					buildWikiPage(obj);
				},
				error:function() {
					targetElem.html("Error retrieving data");
					setTimeout(function() {
						targetElem.empty();
					}, 3000);
				}
			});
		}
	}

	//update the cleared last mouse rotation / update change in target
	//update();
}

var mouse;
var tmpV;
var tmpQ;
var maxDist = 4100;
function rescaleViewPos(scale) {
	if (selected.index !== undefined) {
		vec3.sub(tmpV, glutil.view.pos, selected.arrayBuf);
	} else {
		vec3.copy(tmpV, glutil.view.pos);
	}
	var oldlen = vec3.length(tmpV);
	var len = oldlen * scale;
	if (len < 1e-5) len = 1e-5;
	if (len > maxDist) len = maxDist;
	scale = len / oldlen;
	vec3.scale(tmpV, tmpV, scale);
	if (selected.index !== undefined) {
		vec3.add(glutil.view.pos, selected.arrayBuf, tmpV);
	} else {
		vec3.copy(glutil.view.pos, tmpV);
	}
	refreshDistance();
}

function universeMouseDown(e) {
	quat.identity(lastMouseRot);
	universeUpdateHover();
};

function universeMouseRotate(dx, dy) {
	// mouse line debugging 
	//universeUpdateHover();
	//return;
	
	var rotAngle = Math.PI / 180 * .01 * Math.sqrt(dx*dx + dy*dy);
	quat.setAxisAngle(tmpQ, [-dy, -dx, 0], rotAngle);
	//mat4.translate(glutil.scene.mvMat, glutil.scene.mvMat, [10*dx/canvas.width, -10*dy/canvas.height, 0]);

	//put tmpQ into the frame of glutil.view.angle, so we can rotate the view vector by it
	//  lastMouseRot = glutil.view.angle-1 * tmpQ * glutil.view.angle
	// now newViewAngle = glutil.view.angle * tmpQ = lastMouseRot * glutil.view.angle
	// therefore lastMouseRot is the global transform equivalent of the local transform of tmpQ
	quat.mul(lastMouseRot, glutil.view.angle, tmpQ);
	quat.conjugate(tmpQ, glutil.view.angle);
	quat.mul(lastMouseRot, lastMouseRot, tmpQ);

	applyLastMouseRot();
	//update();
	
	universeUpdateHover();
}

function universeZoom(zoomChange) {
	var scale = Math.exp(-.0003 * zoomChange);
	rescaleViewPos(scale);
};

function universeUpdateHover() {
	var bestDist = Infinity;
	var bestDot = 0;
	var bestDataSet;
	var bestIndex;

	var bestX, bestY, bestZ;
	var viewX = glutil.view.pos[0];
	var viewY = glutil.view.pos[1];
	var viewZ = glutil.view.pos[2];
	//fast axis extraction from quaternions.  z is negative'd to get the fwd dir
	//TODO make use of my added vec3.quatXYZAxis functions in gl-util.js
	var viewFwdX = -2 * (glutil.view.angle[0] * glutil.view.angle[2] + glutil.view.angle[3] * glutil.view.angle[1]); 
	var viewFwdY = -2 * (glutil.view.angle[1] * glutil.view.angle[2] - glutil.view.angle[3] * glutil.view.angle[0]); 
	var viewFwdZ = -(1 - 2 * (glutil.view.angle[0] * glutil.view.angle[0] + glutil.view.angle[1] * glutil.view.angle[1])); 
	var viewRightX = 1 - 2 * (glutil.view.angle[1] * glutil.view.angle[1] + glutil.view.angle[2] * glutil.view.angle[2]); 
	var viewRightY = 2 * (glutil.view.angle[0] * glutil.view.angle[1] + glutil.view.angle[2] * glutil.view.angle[3]); 
	var viewRightZ = 2 * (glutil.view.angle[0] * glutil.view.angle[2] - glutil.view.angle[3] * glutil.view.angle[1]); 
	var viewUpX = 2 * (glutil.view.angle[0] * glutil.view.angle[1] - glutil.view.angle[3] * glutil.view.angle[2]);
	var viewUpY = 1 - 2 * (glutil.view.angle[0] * glutil.view.angle[0] + glutil.view.angle[2] * glutil.view.angle[2]);
	var viewUpZ = 2 * (glutil.view.angle[1] * glutil.view.angle[2] + glutil.view.angle[3] * glutil.view.angle[0]);
	
	var aspectRatio = glutil.canvas.width / glutil.canvas.height;
	var mxf = mouse.xf * 2 - 1;
	var myf = 1 - mouse.yf * 2;
	//why is fwd that much further away?  how does 
	
	var tanFovY = Math.tan(glutil.view.fovY * Math.PI / 360);
	var mouseDirX = viewFwdX + tanFovY * (viewRightX * aspectRatio * mxf + viewUpX * myf);
	var mouseDirY = viewFwdY + tanFovY * (viewRightY * aspectRatio * mxf + viewUpY * myf);
	var mouseDirZ = viewFwdZ + tanFovY * (viewRightZ * aspectRatio * mxf + viewUpZ * myf);
	var mouseDirLength = Math.sqrt(mouseDirX * mouseDirX + mouseDirY * mouseDirY + mouseDirZ * mouseDirZ);
/* mouse line debugging * /
	hover.index = 3088;
	hover.sceneObj.hidden = false;
	hover.setPos(viewX + mouseDirX, viewY + mouseDirY, viewZ + mouseDirZ);
	return;
/**/

	for (var j = 0; j < dataSets.length; j++) {
		var dataSet = dataSets[j];
		if (dataSet.sceneObj.hidden) continue;
					
		var arrayBuffer = dataSet.arrayBuffer;
		for (var i = 0; i < arrayBuffer.length; ) {
			//point that we're testing intersection for
			var pointX = arrayBuffer[i++];
			var pointY = arrayBuffer[i++];
			var pointZ = arrayBuffer[i++];
			//vector from the view origin to the point
			var viewToPointX = pointX - viewX;
			var viewToPointY = pointY - viewY;
			var viewToPointZ = pointZ - viewZ;

			var viewToPointLength = Math.sqrt(viewToPointX * viewToPointX + viewToPointY * viewToPointY + viewToPointZ * viewToPointZ);
			var viewToPointDotMouseDir = viewToPointX * mouseDirX + viewToPointY * mouseDirY + viewToPointZ * mouseDirZ;
			
			var dot = viewToPointDotMouseDir / (mouseDirLength * viewToPointLength); 
			if (dot > .99) {
				var dist = viewToPointLength;
				if (dist < bestDist && dot > bestDot) {
					bestDist = dist;
					bestDot = dot;
					bestX = pointX;
					bestY = pointY;
					bestZ = pointZ;
					bestIndex = i/3-1;
					bestDataSet = dataSet;
				}
			}
		}
	}

	hover.set = bestDataSet;
	hover.index = bestIndex;
	if (hover.set === undefined || hover.index === undefined) {
		hover.sceneObj.hidden = true;
	} else {
		if (!hover.stayHidden) {
			hover.sceneObj.hidden = false;
		}
		hover.setPos(bestX, bestY, bestZ);
	}
}

//looks like javascript always fires its click -- if the mouse hasn't moved far --
//no matter how long the mousedown / mouseup is
//how abuot moving this code to mouseup and testing distanced moved & time elapsed?
function universeMouseClick(e) {
	if (!mouse.isDragging) {
		hover.sceneObj.hidden = true;
		if (!e.shiftKey) {
			setSelectedGalaxy(hover.set, hover.index);
		}
	}
}

function initCallbacks() {
	
	$(window).resize(resize);

	/*
	arrow keys:
	37 = left
	38 = up
	39 = right
	40 = down
	*/
	$(window).bind('keydown', function(ev) {
		if (ev.keyCode == '/'.charCodeAt(0)) {
			ev.preventDefault();
			$('#find').click();
		} else if (ev.keyCode == 'F'.charCodeAt(0) && ev.ctrlKey) {
			ev.preventDefault();
			$('#find').click();
		} else if (ev.keyCode == 37) {
			keysDownFlags |= KEY_LEFT_FLAG;
			ev.preventDefault();
		} else if (ev.keyCode == 38) {
			keysDownFlags |= KEY_UP_FLAG;
			ev.preventDefault();
		} else if (ev.keyCode == 39) {
			keysDownFlags |= KEY_RIGHT_FLAG;
			ev.preventDefault();
		} else if (ev.keyCode == 40) {
			keysDownFlags |= KEY_DOWN_FLAG;
			ev.preventDefault();
		}
	});
	$(window).bind('keyup', function(ev) {
		if (ev.keyCode == 27) {	//not detected except in .onkeyup
			closeSidePanel();
		} else if (ev.keyCode == 37) {
			keysDownFlags &= ~KEY_LEFT_FLAG;
		} else if (ev.keyCode == 38) {
			keysDownFlags &= ~KEY_UP_FLAG;
		} else if (ev.keyCode == 39) {
			keysDownFlags &= ~KEY_RIGHT_FLAG;
		} else if (ev.keyCode == 40) {
			keysDownFlags &= ~KEY_DOWN_FLAG;
		}
	});

	var touchHoverSet = undefined;
	var touchHoverIndex = undefined;
	mouse = new Mouse3D({
		pressObj : glutil.canvas,
		mousedown : universeMouseDown,
		move : universeMouseRotate,
		zoom : universeZoom,
		click : universeMouseClick,
		touchclickstart : function() {
			//if we're using mobile based input then hide hover always
			hover.stayHidden = true;
			
			//keep track here of any mousedown
			//upon click, use this hover info, because it is proly invalid by touchend
			touchHoverSet = hover.set;
			touchHoverIndex = hover.index;
		},
		touchclickend : function() {
			hover.set = touchHoverSet;
			hover.index = touchHoverIndex;
		}
	});
}

function init(done) {
	try {
		glutil = new GLUtil({canvas:canvas});
		gl = glutil.context;
	} catch (e) {
		panel.remove();
		$(canvas).remove();
		$('#webglfail').show();
		throw e;
	}

	lastMouseRot = quat.create();
	tmpV = vec3.create();
	tmpQ = quat.create();	

	glutil.onfps = function(fps) {
		fpsElem.text(fps.toFixed(2) + " fps");
	}

	glutil.view.pos[2] = 60;
	glutil.view.zNear = 1e-5;
	glutil.view.zFar = 5000;


	//init texture
	galaxyTex = new glutil.Texture2D({
		flipY : true,
		generateMipmap : true,
		magFilter : gl.LINEAR,
		minFilter : gl.LINEAR_MIPMAP_LINEAR,
		url : 'galaxy.png',
		onload : function() {
			init2(done);
		}
	});
}

function init2(done) {
	//create shaders

	pointShader = new glutil.ShaderProgram({
		vertexPrecision : 'best',
		vertexCode : mlstr(function(){/*
attribute vec3 vertex;
uniform mat4 mvMat;
uniform mat4 projMat;
uniform float spriteWidth;
uniform float screenWidth;
void main() {
	vec4 eyePos = mvMat * vec4(vertex, 1.);
	gl_Position = projMat * eyePos;
	gl_PointSize = spriteWidth*screenWidth/gl_Position.w;
}
*/}),
		fragmentPrecision : 'best',
		fragmentCode : mlstr(function(){/*
uniform sampler2D tex;
void main() {
	float z = gl_FragCoord.z;
	gl_FragColor = texture2D(tex, gl_PointCoord);
	gl_FragColor *= gl_FragColor;
	//float v = 1. / (z * z);
	//gl_FragColor *= v;
}
*/}),
		uniforms : {
			tex : 0, 
			spriteWidth : 1./10.,
			screenWidth : glutil.canvas.width
		}
	});

	selectedShader = new glutil.ShaderProgram({
		vertexPrecision : 'best',
		vertexCode : mlstr(function(){/*
attribute vec3 vertex;
uniform mat4 mvMat;
uniform mat4 projMat;
uniform float spriteWidth;
uniform float screenWidth;
void main() {
	vec4 eyePos = mvMat * vec4(vertex, 1.);
	gl_Position = projMat * eyePos;
	gl_PointSize = spriteWidth*screenWidth/gl_Position.w;
}
*/}),
		fragmentPrecision : 'best',
		fragmentCode : mlstr(function(){/*
uniform vec3 color;
void main() {
	vec2 absDelta = abs(gl_PointCoord - .5);
	float dist = max(absDelta.x, absDelta.y);
	if (dist < .4) discard;
	gl_FragColor = vec4(color, 1.); 
}
*/}),
		uniforms : {spriteWidth:1./10./2.}
	});
	
	//scene objs

	selected = new Highlight();
	hover = new Highlight();
	hover.sceneObj.uniforms.color = [0,1,0];
	hover.sceneObj.hidden = true;
	hover.stayHidden = false;

	//coordinate chart overlay

	var vertexes = [];
	var thetaDivs = 100;
	var radiusDivs = 2;
	var largeThetaDivs = 6;
	var maxRadius = 1;
	var zDivs = 2;
	var zMin = -.5;
	var zMax = .5;
	for (var zIndex = 0; zIndex < zDivs; ++zIndex) {
		var z = zIndex / (zDivs-1) * (zMax - zMin) + zMin;
		var z2 = z + (zMax - zMin) / (zDivs-1);
		for (var thetaIndex = 0; thetaIndex < thetaDivs; ++thetaIndex) {
			var th1 = 2*Math.PI*thetaIndex/thetaDivs;
			var th2 = 2*Math.PI*(thetaIndex+1)/thetaDivs;
			for (var radiusIndex = 1; radiusIndex <= radiusDivs; ++radiusIndex) {
				var radius = maxRadius * radiusIndex / radiusDivs;
				vertexes.push(radius*Math.cos(th1));
				vertexes.push(radius*Math.sin(th1));
				vertexes.push(z);
				vertexes.push(radius*Math.cos(th2));
				vertexes.push(radius*Math.sin(th2));
				vertexes.push(z);
			}
		}
		if (zIndex < zDivs - 1) {
			for (var thetaIndex = 0; thetaIndex < largeThetaDivs; ++thetaIndex) {
				var theta = 2*Math.PI*thetaIndex/largeThetaDivs;
				vertexes.push(maxRadius*Math.cos(theta));
				vertexes.push(maxRadius*Math.sin(theta));
				vertexes.push(z);
				vertexes.push(0);
				vertexes.push(0);
				vertexes.push(z);
				vertexes.push(maxRadius*Math.cos(theta));
				vertexes.push(maxRadius*Math.sin(theta));
				vertexes.push(z2);
				vertexes.push(0);
				vertexes.push(0);
				vertexes.push(z2);
				vertexes.push(maxRadius*Math.cos(theta));
				vertexes.push(maxRadius*Math.sin(theta));
				vertexes.push(z);
				vertexes.push(maxRadius*Math.cos(theta));
				vertexes.push(maxRadius*Math.sin(theta));
				vertexes.push(z2);
			}
		}
	}

	gridObj = new glutil.SceneObject({
		parent : null,
		mode : gl.LINES,
		blend : [gl.SRC_ALPHA, gl.ONE],
		pos : [0,0,0],
		attrs : {
			vertex : new glutil.ArrayBuffer({data : vertexes})
		},
		shader : new glutil.ShaderProgram({
			vertexPrecision : 'best',
			vertexCode : mlstr(function(){/*
attribute vec3 vertex;
uniform mat4 mvMat;
uniform mat4 projMat;
uniform float scale;
void main() {
	gl_Position = projMat * (mvMat * vec4(scale * vertex, 1.));
}
*/}),
			fragmentPrecision : 'best',
			fragmentCode : mlstr(function(){/*
uniform float alpha;
void main() {
	gl_FragColor = vec4(1., 1., 1., alpha);
}
*/})
		}),
		uniforms : {
			scale : 1,
			alpha : 1
		}
	});

	//draw
	initCallbacks();
		
	resize();
	refreshDistance();

	done();
}

function findObject(ident) {
	var findAnchor = $('#find');
	findAnchor.css('color', 'grey');
	$.ajax({
		url : 'findpoint.lua',
		dataType : 'json',
		data : {
			ident : ident,
		},
		success: function(results) {
			if (results && results.indexes.length) {
				//TODO send parameters of what sets are visible, search across requested sets
				//NOTICE this assumes set0 is the Simbad results, which is the only one the find and getinfo webservices are linked to
				setSelectedGalaxy(dataSetsByName['Simbad'], results.indexes[0]);
				findAnchor.css('color', 'cyan');
			} else {
				findAnchor.css('color', 'red');
				setTimeout(function() {
					findAnchor.css('color', 'cyan');
				}, 3000);
			}
		},
		error:function() {
			findAnchor.css('color', 'red');
			setTimeout(function() {
				findAnchor.css('color', 'cyan');
			}, 3000);
		}
	});
}

/*
args:
	title
	url
	source
	load
	toggle
*/
function fileRequest(args) {
	var div = $('<div>');
	var loading = $('<span>', {text:args.title});

	var downloadAnchor;
	var download = function() {
		downloadAnchor.remove();
		var progress = $('<progress>').attr('max', '100').attr('value', '0').appendTo(div);
		
		var xhr = new XMLHttpRequest();
		xhr.open('GET', args.url, true);
		xhr.responseType = 'arraybuffer';
		xhr.onprogress = function(e) {
			if (e.total) {
				progress.attr('value', parseInt(e.loaded / e.total * 100));
			}
		};
		xhr.onload = function(e) {
			progress.attr('value', '100');

			var input = $('<input>', {
				type:'checkbox',
				checked:'checked',
				change:function(e) {
					args.toggle(e.target.checked);
				}
			}).insertBefore(div);
			var anchor = $('<a>', {
				text:args.title, 
				href:args.source
			})	.attr('target', '_blank')
				.insertBefore(div);
			$('<br>').insertBefore(div);
			div.remove();

			var arrayBuffer = this.response;
			var data = new DataView(arrayBuffer);
			
			var floatBuffer = new Float32Array(data.byteLength / Float32Array.BYTES_PER_ELEMENT);
			var len = floatBuffer.length;
			for (var j = 0; j < len; ++j) {
				floatBuffer[j] = data.getFloat32(j * Float32Array.BYTES_PER_ELEMENT, true);
			}

			args.load(floatBuffer, input, anchor);
		};
		xhr.send();
	};
	
	downloadAnchor = $('<a>', {
		click : function() {
			download();
		}
	});
	
	downloadAnchor.appendTo(div);
	$('<img>', {
		src : 'download.png'
	}).appendTo(downloadAnchor);
	
	loading.appendTo(div);
	
	return {div:div, download:download};
}

$(document).ready(function() {

	var initDataSet = $.url().param('dataset') || '2MRS';

	$(['loading.gif']).preload();
	
	panel = $('#panel');
	closeSidePanel();
	
	canvas = $('<canvas>', {
		css : {
			left : 0,
			top : 0,
			position : 'absolute',
			background : 'red'
		}
	}).prependTo(document.body).get(0);
	
	$(canvas).disableSelection();

	$('#help').bind('click', function() {
		alert("click and drag mouse to rotate\n"
			+"shift+click, mousewheel, or pinch to zoom\n"
			+"click a point to orbit it and for more information\n"
			+"ctrl+f or / to find an object\n");
	});

	$('#find').bind('click', function() {
		var ident = prompt('Enter Object:');
		if (ident === null || ident.length == 0) return;
		findObject(ident);
	});

	fpsElem = $('#fps');

	var fileRequestDiv = $('#sources');
	
	distanceElem = $('#distance');
	orbitCoordsElem = $('#orbitcoords');
	gridSizeElem = $('#gridsize');
	$('#showgrid')
		.prop('checked', showGrid)
		.bind('change', function(e) {
			showGrid = e.target.checked;
		});

	targetElem = $('#target');
	descElem = $('#desc');

	init(function() {
		$.each([
			{title:'2MRS', url:'2mrs.f32', source:'http://tdc-www.cfa.harvard.edu/2mrs/'},
			{title:'6dF GS', url:'6dfgs.f32', source:'http://www.aao.gov.au/6dFGS/'},
			{title:'SDSS3-DR14', url:'sdss3-dr14.f32', source:'http://www.sdss3.org/dr14/'},
			{title:'SIMBAD', url:'simbad.f32', source:'http://simbad.u-strasbg.fr/simbad/'},
			{title:'Gaia stars', url:'gaia.f32', source:'http://sci.esa.int/gaia/'}
		], function(k,v) {
			var sceneObj;
			var request = fileRequest({
				title:v.title,
				url:v.url,
				source:v.source,
				load:function(arrayBuffer, input) {
					
					var pointVtxBuf = new glutil.ArrayBuffer({
						data : arrayBuffer
					});

					var dataSet = {
						title:v.title,
						arrayBuffer:arrayBuffer
					};
					dataSets.push(dataSet);
					dataSetsByName[v.title] = dataSet;

					sceneObj = new glutil.SceneObject({
						mode : gl.POINTS,
						attrs : {
							vertex : pointVtxBuf
						},
						shader : pointShader,
						blend : [gl.SRC_ALPHA, gl.ONE],
						texs : [galaxyTex]
					});
					sceneObj.arrayBuffer = arrayBuffer;
					sceneObj.hidden = v.title != initDataSet;
					if (sceneObj.hidden) {
						input.removeAttr('checked');
					} else {
						input.attr('checked', 'checked');
					}
					
					dataSet.sceneObj = sceneObj;
		
					//start off the render loop:
					var ondraw;
					ondraw = function() {
						glutil.draw();
		
						if (selected.index !== undefined) { 
							gridObj.pos[0] = selected.arrayBuf[0];
							gridObj.pos[1] = selected.arrayBuf[1];
							gridObj.pos[2] = selected.arrayBuf[2];
						}
						//draw fades between various grids depending on where the view scale is
						var newGridScale = (1 << parseInt(Math.log2(viewDistance * 1024))) / 1024;
						if (newGridScale != gridScale) {
							gridFadeOutSize = gridScale;
							gridFadeOutTime = Date.now();
							gridFadeInSize = newGridScale;
							gridFadeInTime = Date.now();
							gridScale = newGridScale;
						}
						if (showGrid && gridScale >= 1) {
							var gridFadeTime = 1000;
							if (gridFadeOutSize !== undefined) {
								var alpha = gridAlpha * (1 - (Date.now() - gridFadeOutTime) / gridFadeTime);
								if (alpha > 0) {
									gridObj.draw({
										uniforms : {
											scale : gridFadeOutSize,
											alpha : alpha
										}
									});
								}
							}
							if (gridFadeInSize !== undefined) {
								var alpha = gridAlpha * Math.min(1, (Date.now() - gridFadeInTime) / gridFadeTime);
								gridObj.draw({
									uniforms : {
										scale : gridFadeInSize,
										alpha : alpha
									}
								});
							}
						}

						doUpdate();
						requestAnimFrame(ondraw);
					};
					ondraw();

					//I only have search data for 2mrs right now
					if (v.title == '2MRS' || v.title == 'SIMBAD') {
						//substring 1 removes the preface ?
						var urlkeys = {};
						var search = $('<a>', {href:location.href}).get(0).search;
						search = search.substring(1);
						if (search.length) {
							search = search.split('&');
							$.each(search, function(i,v) {
								v = v.split('=');
								var value = decodeURI(v[1].replace(/\+/g,' '));
								urlkeys[v[0]] = value;
							});
							if (urlkeys.obj) findObject(urlkeys.obj);
						}
					}
				},
				toggle:function(enabled) {
					sceneObj.hidden = !enabled;
				}
			});
			request.div.appendTo(fileRequestDiv);
			if (k == 0) request.download();
		});
	});
})

function resize() {
	glutil.canvas.width = window.innerWidth;
	glutil.canvas.height = window.innerHeight;

	refreshPanelSize();

	gl.useProgram(pointShader.obj);
	pointShader.setUniform('screenWidth', glutil.canvas.width);
	gl.useProgram(selectedShader.obj);
	selectedShader.setUniform('screenWidth', glutil.canvas.width);
	gl.useProgram(null);

	glutil.resize();
}

function applyLastMouseRot() {
	//if (lastMouseRot[3] == 1) return false;
	
	vec3.sub(tmpV, glutil.view.pos, selected.arrayBuf);
	var posDist = vec3.length(tmpV);
	vec3.transformQuat(tmpV, tmpV, lastMouseRot);
	vec3.normalize(tmpV, tmpV);
	vec3.scale(tmpV, tmpV, posDist);
	if (selected.index !== undefined) {
		vec3.add(glutil.view.pos, selected.arrayBuf, tmpV);
	} else {
		vec3.copy(glutil.view.pos, tmpV);
	}
	
	//RHS apply so it is relative to current view 
	//newViewAngle := glutil.view.angle * tmpQ
	quat.mul(glutil.view.angle, lastMouseRot, glutil.view.angle);
	quat.normalize(glutil.view.angle, glutil.view.angle);

	return true;
}

function lookAtSelected() {
	if (selected.index === undefined) return false;
	
	var viewX = glutil.view.pos[0], viewY = glutil.view.pos[1], viewZ = glutil.view.pos[2];
	var pointX = selected.arrayBuf[0], pointY = selected.arrayBuf[1], pointZ = selected.arrayBuf[2];
	var viewToSelX = pointX - viewX, viewToSelY = pointY - viewY, viewToSelZ = pointZ - viewZ;
	var viewFwdX = -2 * (glutil.view.angle[0] * glutil.view.angle[2] + glutil.view.angle[3] * glutil.view.angle[1]); 
	var viewFwdY = -2 * (glutil.view.angle[1] * glutil.view.angle[2] - glutil.view.angle[3] * glutil.view.angle[0]); 
	var viewFwdZ = -(1 - 2 * (glutil.view.angle[0] * glutil.view.angle[0] + glutil.view.angle[1] * glutil.view.angle[1])); 
	var viewToSelInvLen = 1 / Math.sqrt(viewToSelX * viewToSelX + viewToSelY * viewToSelY + viewToSelZ * viewToSelZ);
	viewToSelX *= viewToSelInvLen; viewToSelY *= viewToSelInvLen; viewToSelZ *= viewToSelInvLen;
	var viewFwdInvLen = 1 / Math.sqrt(viewFwdX * viewFwdX + viewFwdY * viewFwdY + viewFwdZ * viewFwdZ);
	var axisX = viewFwdY * viewToSelZ - viewFwdZ * viewToSelY;
	var axisY = viewFwdZ * viewToSelX - viewFwdX * viewToSelZ;
	var axisZ = viewFwdX * viewToSelY - viewFwdY * viewToSelX;
	axisLen = Math.sqrt(axisX * axisX + axisY * axisY + axisZ * axisZ);
	if (axisLen < .00002) return false;

	var axisInvLen = 1 / axisLen;
	axisX *= axisInvLen; axisY *= axisInvLen; axisZ *= axisInvLen;
	var cosOmega = viewFwdX * viewToSelX + viewFwdY * viewToSelY + viewFwdZ * viewToSelZ;
	if (cosOmega < -1) cosOmega = -1;
	if (cosOmega > 1) cosOmega = 1;
	var theta = .05 * Math.acos(cosOmega);
	var cosTheta = Math.cos(theta);
	var sinTheta = Math.sin(theta);
	var lookQuat = [axisX * sinTheta, axisY * sinTheta, axisZ * sinTheta, cosTheta];

	quat.mul(glutil.view.angle, lookQuat, glutil.view.angle);
	quat.normalize(glutil.view.angle, glutil.view.angle);
	
	return true;
}

/*
var updateInterval = undefined;
function update() {
	if (updateInterval) return;
	updateInterval = requestAnimFrame(doUpdate);
}
*/
var spriteWidth;
function doUpdate() {
	//updateInterval = undefined;
	var selRes = lookAtSelected();
	var rotRes = applyLastMouseRot();
	universeUpdateHover();
	//if (!selRes && !rotRes) return;

	var centerX = selected.arrayBuf[0];
	var centerY = selected.arrayBuf[1];
	var centerZ = selected.arrayBuf[2];

	var dx = glutil.view.pos[0] - centerX; 
	var dy = glutil.view.pos[1] - centerY; 
	var dz = glutil.view.pos[2] - centerZ; 
	var d2 = dx * dx + dy * dy + dz * dz;
	var newViewDistance = Math.sqrt(d2);
	if (newViewDistance != viewDistance) {
		viewDistance = newViewDistance;
		spriteWidth = .02*Math.sqrt(viewDistance);
		gl.useProgram(pointShader.obj);
		pointShader.setUniform('spriteWidth', spriteWidth);
		gl.useProgram(selectedShader.obj);
		selectedShader.setUniform('spriteWidth', spriteWidth);
		gl.useProgram(null);
	}

	//update();
}

