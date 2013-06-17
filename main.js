//populated in init
var canvas;
var gl;
var panel, panelIsOpen;
var distanceElem;
var targetElem;
var descElem;
var fpsElem;

var pointShader;
var selectedShader;
var selected, hover;
var galaxyTex;

function Highlight() {
	this.set = undefined;	//which set this belongs to
	this.index = undefined;	//which index
	this.arrayBuf = new Float32Array(3);
	this.vtxBuf = new GL.ArrayBuffer({
		data : this.arrayBuf,
		usage : gl.DYNAMIC_DRAW
	});
	this.sceneObj = new GL.SceneObject({
		mode : gl.POINTS,
		vertexBuffer : this.vtxBuf,
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
					left:'300px'
				},
				click:function() {
					closeSidePanel();
				}
			});
		}
		panelCloseButton.appendTo(document.body);	
	} else {
		panel.css('width', '200px');
		panel.css('height', '320px');
		if (panelCloseButton) {
			panelCloseButton.remove();
			panelCloseButton = undefined;
		}
	}
}

//TODO DOM updates are slow, so put in a request / interval it
var refreshDistanceInterval = undefined;
var refreshDistanceValue = undefined;
function refreshDistance(d) {
	refreshDistanceValue = d;
	if (refreshDistanceInterval !== undefined) return;
	refreshDistanceInterval = setInterval(doRefreshDistance, 500);
}
function doRefreshDistance() {
	refreshDistanceInterval = undefined;
	var d = refreshDistanceValue; 

	var centerX = selected.arrayBuf[0];
	var centerY = selected.arrayBuf[1];
	var centerZ = selected.arrayBuf[2];

	if (d === undefined) {
		var dx = GL.view.pos[0] - centerX; 
		var dy = GL.view.pos[1] - centerY; 
		var dz = GL.view.pos[2] - centerZ; 
		var d2 = dx * dx + dy * dy + dz * dz;
		d = Math.sqrt(d2);
	}
	distanceElem.empty();
	$('<div>', {text:'Distance: '+parseFloat(d).toFixed(4)+' Mpc '}).appendTo(distanceElem);
	$('<div>', {
		text : 'Orbit Coords: '+centerX+', '+centerY+', '+centerZ+' ',
		css : {
			width : 200
		}
	}).appendTo(distanceElem);
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
	
		//assumes the first set is 2MRS
		targetElem.empty();
		if (dataSet.title == '2MRS') {
			targetElem.append($('<img src="loading.gif" style="padding-top:10px; padding-left:10px"/>'));
			$.ajax({
				url:'getpoint.lua',
				dataType:'json',
				data:{point:pointIndex},
				success:function(obj) {
					targetElem.empty();
					//todo pretty up the names
					var cols = ['_2MASS_ID', 'bibliographicCode', 'galaxyName', 'galaxyType', 'sourceOfType'];
					$.each(cols, function(k,col) {
						$('<div>', {text:col+': '+obj[col]+' '}).appendTo(targetElem);
					});

					closeSidePanel();

					var search = obj.galaxyName;
					search = search.split('_');
					for (var j = 0; j < search.length; j++) {
						var v = parseFloat(search[j]);
						if (v == v) search[j] = v;	//got a valid number
					}
					search = search.join(' ');
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
				},
				error:function() {
					targetElem.innerHTML("Error retrieving data");
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

function rescaleViewPos(scale) {
	if (selected.index !== undefined) {
		vec3.sub(tmpV, GL.view.pos, selected.arrayBuf);
	} else {
		vec3.copy(tmpV, GL.view.pos);
	}
	var oldlen = vec3.length(tmpV);
	var len = oldlen * scale;
	if (len < 1e-5) len = 1e-5;
	if (len > 1200) len = 1200;
	scale = len / oldlen;
	vec3.scale(tmpV, tmpV, scale);
	if (selected.index !== undefined) {
		vec3.add(GL.view.pos, selected.arrayBuf, tmpV);
	} else {
		vec3.copy(GL.view.pos, tmpV);
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
	//mat4.translate(GL.mvMat, GL.mvMat, [10*dx/canvas.width, -10*dy/canvas.height, 0]);

	//put tmpQ into the frame of GL.view.angle, so we can rotate the view vector by it
	//  lastMouseRot = GL.view.angle-1 * tmpQ * GL.view.angle
	// now newViewAngle = GL.view.angle * tmpQ = lastMouseRot * GL.view.angle
	// therefore lastMouseRot is the global transform equivalent of the local transform of tmpQ
	quat.mul(lastMouseRot, GL.view.angle, tmpQ);
	quat.conjugate(tmpQ, GL.view.angle);
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
	var bestDataSet;
	var bestIndex;

	var bestX, bestY, bestZ;
	var viewX = GL.view.pos[0];
	var viewY = GL.view.pos[1];
	var viewZ = GL.view.pos[2];
	//fast axis extraction from quaternions.  z is negative'd to get the fwd dir
	//I should make a method out of this ... so should glmatrix.net ...
	var viewFwdX = -2 * (GL.view.angle[0] * GL.view.angle[2] + GL.view.angle[3] * GL.view.angle[1]); 
	var viewFwdY = -2 * (GL.view.angle[1] * GL.view.angle[2] - GL.view.angle[3] * GL.view.angle[0]); 
	var viewFwdZ = -(1 - 2 * (GL.view.angle[0] * GL.view.angle[0] + GL.view.angle[1] * GL.view.angle[1])); 
	var viewRightX = 1 - 2 * (GL.view.angle[1] * GL.view.angle[1] + GL.view.angle[2] * GL.view.angle[2]); 
	var viewRightY = 2 * (GL.view.angle[0] * GL.view.angle[1] + GL.view.angle[2] * GL.view.angle[3]); 
	var viewRightZ = 2 * (GL.view.angle[0] * GL.view.angle[2] - GL.view.angle[3] * GL.view.angle[1]); 
	var viewUpX = 2 * (GL.view.angle[0] * GL.view.angle[1] - GL.view.angle[3] * GL.view.angle[2]);
	var viewUpY = 1 - 2 * (GL.view.angle[0] * GL.view.angle[0] + GL.view.angle[2] * GL.view.angle[2]);
	var viewUpZ = 2 * (GL.view.angle[1] * GL.view.angle[2] + GL.view.angle[3] * GL.view.angle[0]);
	
	var aspectRatio = canvas.width / canvas.height;
	var mxf = mouse.xf * 2 - 1;
	var myf = 1 - mouse.yf * 2;
	//why is fwd that much further away?  how does 
	
	var tanFovY = Math.tan(GL.view.fovY * Math.PI / 360);
	var mouseDirX = viewFwdX + tanFovY * (viewRightX * aspectRatio * mxf + viewUpX * myf);
	var mouseDirY = viewFwdY + tanFovY * (viewRightY * aspectRatio * mxf + viewUpY * myf);
	var mouseDirZ = viewFwdZ + tanFovY * (viewRightZ * aspectRatio * mxf + viewUpZ * myf);
	var mouseDirLen = Math.sqrt(mouseDirX * mouseDirX + mouseDirY * mouseDirY + mouseDirZ * mouseDirZ);
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
			var mouseDirLength = Math.sqrt(mouseDirX * mouseDirX + mouseDirY * mouseDirY + mouseDirZ * mouseDirZ);
			var viewToPointDotMouseDir = viewToPointX * mouseDirX + viewToPointY * mouseDirY + viewToPointZ * mouseDirZ;
			
			var dot = viewToPointDotMouseDir / (mouseDirLength * viewToPointLength); 
			if (dot > .99) {
				var dist = viewToPointLength;
				if (dist < bestDist) {
					bestDist = dist;
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
		pressObj : canvas,
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

function init() {
	try {
		gl = GL.init(canvas);
	} catch (e) {
		panel.remove();
		$(canvas).remove();
		$('#webglfail').show();
		throw e;
	}

	lastMouseRot = quat.create();
	tmpV = vec3.create();
	tmpQ = quat.create();	

	GL.onfps = function(fps) {
		fpsElem.text(fps.toFixed(2) + " fps");
	}

	GL.view.pos[2] = 60;
	GL.view.zNear = 1e-5;
	GL.view.zFar = 5000;

	//create shaders

	pointShader = new GL.ShaderProgram({
		vertexCodeID : 'point-vsh',
		fragmentCodeID : 'point-fsh',
		uniforms : {
			tex : 0, 
			spriteWidth : 1./4.,
			screenWidth : canvas.width
		}
	});

	selectedShader = new GL.ShaderProgram({
		vertexCodeID : 'point-vsh',
		fragmentCodeID : 'select-fsh',
		uniforms : {spriteWidth:1./16.}
	});
	
	//init texture
	galaxyTex = new GL.Texture2D({
		flipY : true,
		generateMipmap : true,
		magFilter : gl.LINEAR,
		minFilter : gl.LINEAR_MIPMAP_LINEAR,
		url : 'galaxy.png'
	});

	//scene objs

	selected = new Highlight();
	hover = new Highlight();
	hover.sceneObj.uniforms.color = [0,1,0];
	hover.sceneObj.hidden = true;
	hover.stayHidden = false;

	//draw
	initCallbacks();
		
	resize();
	refreshDistance();
}

function findObject(ident) {
	var findAnchor = $('#find');
	findAnchor.css('color', 'grey');
	$.ajax({
		url:'findpoint.lua',
		dataType:'json',
		data:{ident:ident},
		success: function(results) {
			if (results && results.indexes.length) {
				//NOTICE this assumes set0 is the 2MRS results, which is the only one the find and getinfo webservices are linked to
				setSelectedGalaxy(dataSetsByName['2MRS'], results.indexes[0]);
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
	var loading = $('<span>', {text:'Loading '+args.title}).appendTo(div);
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

	return div;
}

$(document).ready(function() {

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

	var fileRequestDiv = $('<div>').appendTo(panel);
	
	distanceElem = $('<div>', {css:{paddingRight:20}}).appendTo(panel);
	targetElem = $('<div>').appendTo(panel);
	descElem = $('<div>').appendTo(panel);

	init();

	$.each([
		{title:'2MRS', url:'2mrs.f32', source:'http://tdc-www.cfa.harvard.edu/2mrs/'},
		{title:'6dF GS', url:'6dfgs.f32', source:'http://www.aao.gov.au/6dFGS/'}
	], function(k,v) {
		var sceneObj;
		fileRequest({
			title:v.title,
			url:v.url,
			source:v.source,
			load:function(arrayBuffer, input) {
				
				var pointVtxBuf = new GL.ArrayBuffer({
					data:arrayBuffer
				});

				var dataSet = {
					title:v.title,
					arrayBuffer:arrayBuffer
				};
				dataSets.push(dataSet);
				dataSetsByName[v.title] = dataSet;

				sceneObj = new GL.SceneObject({
					mode : gl.POINTS,
					vertexBuffer : pointVtxBuf,
					shader : pointShader,
					blend : [gl.SRC_ALPHA, gl.ONE],
					texs : [galaxyTex]
				});
				sceneObj.arrayBuffer = arrayBuffer;
				sceneObj.hidden = v.title != '2MRS';
				if (sceneObj.hidden) {
					input.removeAttr('checked');
				} else {
					input.attr('checked', 'checked');
				}
				
				dataSet.sceneObj = sceneObj;
	
				//start off the render loop:
				var ondraw;
				ondraw = function() {
					GL.draw();
					doUpdate();
					requestAnimFrame(ondraw);
				};
				ondraw();

				//I only have search data for 2mrs right now
				if (v.title == '2MRS') {
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
		}).appendTo(fileRequestDiv);
	});
})

function resize() {
	canvas.width = window.innerWidth;
	canvas.height = window.innerHeight;

	refreshPanelSize();

	gl.useProgram(pointShader.obj);
	pointShader.setUniform('screenWidth', canvas.width);
	gl.useProgram(selectedShader.obj);
	selectedShader.setUniform('screenWidth', canvas.width);
	gl.useProgram(null);

	GL.resize();
}

function applyLastMouseRot() {
	//if (lastMouseRot[3] == 1) return false;
	
	vec3.sub(tmpV, GL.view.pos, selected.arrayBuf);
	var posDist = vec3.length(tmpV);
	vec3.transformQuat(tmpV, tmpV, lastMouseRot);
	vec3.normalize(tmpV, tmpV);
	vec3.scale(tmpV, tmpV, posDist);
	if (selected.index !== undefined) {
		vec3.add(GL.view.pos, selected.arrayBuf, tmpV);
	} else {
		vec3.copy(GL.view.pos, tmpV);
	}
	
	//RHS apply so it is relative to current view 
	//newViewAngle := GL.view.angle * tmpQ
	quat.mul(GL.view.angle, lastMouseRot, GL.view.angle);
	quat.normalize(GL.view.angle, GL.view.angle);

	return true;
}

function lookAtSelected() {
	if (selected.index === undefined) return false;
	
	var viewX = GL.view.pos[0], viewY = GL.view.pos[1], viewZ = GL.view.pos[2];
	var pointX = selected.arrayBuf[0], pointY = selected.arrayBuf[1], pointZ = selected.arrayBuf[2];
	var viewToSelX = pointX - viewX, viewToSelY = pointY - viewY, viewToSelZ = pointZ - viewZ;
	var viewFwdX = -2 * (GL.view.angle[0] * GL.view.angle[2] + GL.view.angle[3] * GL.view.angle[1]); 
	var viewFwdY = -2 * (GL.view.angle[1] * GL.view.angle[2] - GL.view.angle[3] * GL.view.angle[0]); 
	var viewFwdZ = -(1 - 2 * (GL.view.angle[0] * GL.view.angle[0] + GL.view.angle[1] * GL.view.angle[1])); 
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

	quat.mul(GL.view.angle, lookQuat, GL.view.angle);
	quat.normalize(GL.view.angle, GL.view.angle);
	
	return true;
}

/*
var updateInterval = undefined;
function update() {
	if (updateInterval) return;
	updateInterval = requestAnimFrame(doUpdate);
}
*/
function doUpdate() {
	//updateInterval = undefined;
	var selRes = lookAtSelected();
	var rotRes = applyLastMouseRot();
	universeUpdateHover();
	//if (!selRes && !rotRes) return;

	//update();
}

