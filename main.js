import {vec3, quat} from '/js/gl-matrix-3.4.1/index.js';
import {assertExists, DOM, getIDs, removeFromParent, show, hide, hidden, preload, asyncfor} from '/js/util.js';
import {GLUtil} from '/js/gl-util.js';
import {makeGradient} from '/js/gl-util-Gradient.js';
import {Mouse3D} from '/js/mouse3d.js';
const ids = getIDs();
const urlparams = new URLSearchParams(location.search);
window.ids = ids;

//populated in init
let canvas;
let gl;
let glutil;
let panelIsOpen;

let pointShader;
let selectedShader;
let selected, hover;
let galaxyTex;

let gridObj;
let gridAlpha = .25;
let showGrid = false;
let gridScale;
let gridFadeOutSize;
let gridFadeOutTime;
let gridFadeInSize;
let gridFadeInTime;

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
let dataSets = [];
let dataSetsByName = {};

let lastMouseRot;
let panelCloseButton;	//

let KEY_LEFT_FLAG = 1;
let KEY_UP_FLAG = 2;
let KEY_RIGHT_FLAG = 4;
let KEY_DOWN_FLAG = 8;
let keysDownFlags = 0;

function closeSidePanel() {
	panelIsOpen = false;
	if (ids.desc) ids.desc.innerHTML = '';
	refreshPanelSize();
}

function refreshPanelSize() {
	if (panelIsOpen) {
		ids.panel.style.width = '375px';
		ids.panel.style.height = window.innerHeight;
		if (!panelCloseButton) {
			panelCloseButton = DOM('img', {
				src:'close.png', 
				width:'48px',
				css:{
					position:'absolute',
					top:'10px',
					left:'300px',
					zIndex:1,
				},
				click:function() {
					closeSidePanel();
				},
				appendTo : document.body,
			});
		}
	} else {
		ids.panel.style.width = '200px';
		ids.panel.style.height = '330px';
		if (panelCloseButton) {
			removeFromParent(panelCloseButton);
			panelCloseButton = undefined;
		}
	}
}

let viewDistance = undefined;
let refreshDistanceInterval = undefined;
function refreshDistance() {
	if (refreshDistanceInterval !== undefined) return;
	refreshDistanceInterval = setInterval(doRefreshDistance, 500);
}
function doRefreshDistance() {
	refreshDistanceInterval = undefined;

	let centerX = selected.arrayBuf[0];
	let centerY = selected.arrayBuf[1];
	let centerZ = selected.arrayBuf[2];

	ids.distance.innerText = parseFloat(viewDistance).toFixed(4);
	ids.orbitCoords.innerText = centerX+', '+centerY+', '+centerZ;
	ids.gridSize.innerText = gridScale >= 1 ? (gridScale + ' Mpc') : '';
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
		let i = 3*pointIndex;
		
		let x = dataSet.arrayBuffer[i++];
		let y = dataSet.arrayBuffer[i++];
		let z = dataSet.arrayBuffer[i++];
		selected.setPos(x,y,z);
		refreshDistance();

		const buildWikiPage = obj => {
			//todo pretty up the names
			let cols;
			if (dataSet.title == '2MRS') {
				cols = ['_2MASS_ID', 'bibliographicCode', 'galaxyName', 'galaxyType', 'sourceOfType'];
			} else if (dataSet.title == 'SIMBAD') {
				cols = ['id', 'otype'];
				obj.otype = otypeDescs[obj.otype] || obj.otype;
			}
			cols.forEach(col => {
				DOM('div', {
					text : col+': '+obj[col]+' ',
					appendTo : ids.target,
				});
			});

			closeSidePanel();

			let search;
			if (dataSet.title == '2MRS') {
				search = obj.galaxyName;
				search = search.split('_');
				for (let j = 0; j < search.length; j++) {
					let v = parseFloat(search[j]);
					if (v == v) search[j] = v;	//got a valid number
				}
				search = search.join(' ');
			} else if (dataSet.title == 'SIMBAD') {
				search = obj.id;
			}
			
			//console.log("searching "+search);
			ids.desc.innerHTML = '';
			/*
			$.ajax({
				url:'https://en.wikipedia.org/w/api.php',
				dataType:'jsonp',
				data:{action:'query', titles:search, format:'json', prop:'revisions'},
				cache:true,
				success:function(response) {
					console.log(response);
			*/
					fetch('https://en.wikipedia.org/w/api.php', {
						data : {
							action : 'parse',
							page : search,
							format : 'json',
							prop : 'text',
						},
					}).then(response => {
						if (!response.ok) throw 'not ok';
						response.json()
						.then(obj => {
console.log(obj);
							const parse = obj.parse;
							if (!parse) return; 
							const title = parse.title
							if (!title) return; 
							const text = parse.text;
							if (!text) return;
							text = text['*'];
							if (!text) return;
							lastTitle = title;
							lastText = text;
							DOM('br', {appendTo:ids.desc});
							DOM('br', {appendTo:ids.desc});
							DOM('a', {
								text:'Wikipedia:', 
								href:'https://www.wikipedia.org',
								css:{
									fontSize:'10pt',
								},
								attrs : {
									target : '_blank',
								},
								appendTo : ids.desc,
							});
							DOM('h2', {text:title, appendTo : ids.desc});
							const textDiv = DOM('div', {innerHTML : text, appendTo : ids.desc});
							document.querySelectorAll('a', textDiv).forEach(anchor => {
								const href = anchor.href;
								if (href[0] == '#') {
									//https://stackoverflow.com/questions/586024/disabling-links-with-jquery
									//TODO? anchor.after(anchor.innerText);
									removeFromParent(anchor);
									return;
								}
								if (href[0] == '/') {
									anchor.href = 'https://en.wikipedia.org'+href;
								}
								anchor.target = '_blank';
							});


							panelIsOpen = true;
							refreshPanelSize();
						});
					}).catch(e => {
						console.log(e)
						ids.desc.innerHTML = "Error retrieving data";
						setTimeout(function() {
							ids.desc.innerHTML = '';
						}, 3000);
					});
			/*
				}
			});
			*/
		};

		//assumes the first set is 2MRS
		ids.target.innerHTML = '';
		//if (dataSet.title == 'Simbad') {
		//	buildWikiPage(simbadCatalog[pointIndex]);	//for local json support.  this file is 1mb, so i'm not going to use that online.
		//} else
		if (dataSet.title == '2MRS' || dataSet.title == 'SIMBAD') {
			DOM('img', {
				src:"loading.gif",
				style:"padding-top:10px; padding-left:10px",
				appendTo:ids.target,
			});
			const fetchArgs = new URLSearchParams();
			fetchArgs.set('set', dataSet.title.toLowerCase());
			fetchArgs.set('point', pointIndex);
			fetch('getpoint.lua?'+fetchArgs.toString())
			.then(response => {
				if (!response.ok) throw 'not ok';
				response.json()
				.then(obj => {
					console.log('got', obj);
					ids.target.innerhTML = '';
					buildWikiPage(obj);
				});
			}).catch(e => {
				console.log(e);
				ids.target.innerHTML = "Error retrieving data";
				setTimeout(function() {
					ids.target.innerHTML = '';
				}, 3000);
			});
		}
	}

	//update the cleared last mouse rotation / update change in target
	//update();
}

let mouse;
let tmpV;
let tmpQ;
let maxDist = 4100;
function rescaleViewPos(scale) {
	if (selected.index !== undefined) {
		vec3.sub(tmpV, glutil.view.pos, selected.arrayBuf);
	} else {
		vec3.copy(tmpV, glutil.view.pos);
	}
	let oldlen = vec3.length(tmpV);
	let len = oldlen * scale;
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
	
	let rotAngle = Math.PI / 180 * .01 * Math.sqrt(dx*dx + dy*dy);
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
	let scale = Math.exp(-.0003 * zoomChange);
	rescaleViewPos(scale);
};

function universeUpdateHover() {
	let bestDist = Infinity;
	let bestDot = 0;
	let bestDataSet;
	let bestIndex;

	let bestX, bestY, bestZ;
	let viewX = glutil.view.pos[0];
	let viewY = glutil.view.pos[1];
	let viewZ = glutil.view.pos[2];
	//fast axis extraction from quaternions.  z is negative'd to get the fwd dir
	//TODO make use of my added vec3.quatXYZAxis functions in gl-util.js
	let viewFwdX = -2 * (glutil.view.angle[0] * glutil.view.angle[2] + glutil.view.angle[3] * glutil.view.angle[1]); 
	let viewFwdY = -2 * (glutil.view.angle[1] * glutil.view.angle[2] - glutil.view.angle[3] * glutil.view.angle[0]); 
	let viewFwdZ = -(1 - 2 * (glutil.view.angle[0] * glutil.view.angle[0] + glutil.view.angle[1] * glutil.view.angle[1])); 
	let viewRightX = 1 - 2 * (glutil.view.angle[1] * glutil.view.angle[1] + glutil.view.angle[2] * glutil.view.angle[2]); 
	let viewRightY = 2 * (glutil.view.angle[0] * glutil.view.angle[1] + glutil.view.angle[2] * glutil.view.angle[3]); 
	let viewRightZ = 2 * (glutil.view.angle[0] * glutil.view.angle[2] - glutil.view.angle[3] * glutil.view.angle[1]); 
	let viewUpX = 2 * (glutil.view.angle[0] * glutil.view.angle[1] - glutil.view.angle[3] * glutil.view.angle[2]);
	let viewUpY = 1 - 2 * (glutil.view.angle[0] * glutil.view.angle[0] + glutil.view.angle[2] * glutil.view.angle[2]);
	let viewUpZ = 2 * (glutil.view.angle[1] * glutil.view.angle[2] + glutil.view.angle[3] * glutil.view.angle[0]);
	
	let aspectRatio = glutil.canvas.width / glutil.canvas.height;
	let mxf = mouse.xf * 2 - 1;
	let myf = 1 - mouse.yf * 2;
	//why is fwd that much further away?  how does 
	
	let tanFovY = Math.tan(glutil.view.fovY * Math.PI / 360);
	let mouseDirX = viewFwdX + tanFovY * (viewRightX * aspectRatio * mxf + viewUpX * myf);
	let mouseDirY = viewFwdY + tanFovY * (viewRightY * aspectRatio * mxf + viewUpY * myf);
	let mouseDirZ = viewFwdZ + tanFovY * (viewRightZ * aspectRatio * mxf + viewUpZ * myf);
	let mouseDirLength = Math.sqrt(mouseDirX * mouseDirX + mouseDirY * mouseDirY + mouseDirZ * mouseDirZ);
/* mouse line debugging * /
	hover.index = 3088;
	hover.sceneObj.hidden = false;
	hover.setPos(viewX + mouseDirX, viewY + mouseDirY, viewZ + mouseDirZ);
	return;
/**/

	for (let j = 0; j < dataSets.length; j++) {
		let dataSet = dataSets[j];
		if (dataSet.sceneObj.hidden) continue;
					
		let arrayBuffer = dataSet.arrayBuffer;
		for (let i = 0; i < arrayBuffer.length; ) {
			//point that we're testing intersection for
			let pointX = arrayBuffer[i++];
			let pointY = arrayBuffer[i++];
			let pointZ = arrayBuffer[i++];
			//vector from the view origin to the point
			let viewToPointX = pointX - viewX;
			let viewToPointY = pointY - viewY;
			let viewToPointZ = pointZ - viewZ;

			let viewToPointLength = Math.sqrt(viewToPointX * viewToPointX + viewToPointY * viewToPointY + viewToPointZ * viewToPointZ);
			let viewToPointDotMouseDir = viewToPointX * mouseDirX + viewToPointY * mouseDirY + viewToPointZ * mouseDirZ;
			
			let dot = viewToPointDotMouseDir / (mouseDirLength * viewToPointLength); 
			if (dot > .99) {
				let dist = viewToPointLength;
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
	
	window.addEventListener('resize', resize);

	/*
	arrow keys:
	37 = left
	38 = up
	39 = right
	40 = down
	*/
	window.addEventListener('keydown', ev => {
		if (ev.keyCode == '/'.charCodeAt(0)) {
			ev.preventDefault();
			ids.find.dispatchEvent(new Event('click'));
		} else if (ev.keyCode == 'F'.charCodeAt(0) && ev.ctrlKey) {
			ev.preventDefault();
			ids.find.dispatchEvent(new Event('click'));
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
	window.addEventListener('keyup', ev => {
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

	let touchHoverSet = undefined;
	let touchHoverIndex = undefined;
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

function findObject(ident) {
	ids.find.style.color = 'grey';
	const fetchArgs = new URLSearchParams();
	fetchArgs.set('ident', ident);
	fetch('findpoint.lua?'+fetchArgs.toString())
	.then(response => {
		if (!response.ok) throw 'not ok';
		response.json()
		.then(results => {
			if (!(results && results.indexes.length)) throw 'no results';
			//TODO send parameters of what sets are visible, search across requested sets
			//NOTICE this assumes set0 is the Simbad results, which is the only one the find and getinfo webservices are linked to
			setSelectedGalaxy(dataSetsByName['Simbad'], results.indexes[0]);
			ids.find.style.color = 'cyan';
		});
	}).catch(e => {
		ids.find.style.color = 'red';
		setTimeout(() => {
			ids.find.style.color = 'cyan';
		}, 3000);
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
	const div = DOM('div');
	
	const downloadAnchor = DOM('a', {
		click : () => {download();},
		appendTo : div,
	});
	
	DOM('img', {
		src : 'download.png',
		appendTo : downloadAnchor,
	});
	DOM('span', {text:args.title, appendTo:div});
	
	const download = () => {
		removeFromParent(downloadAnchor);
		const progress = DOM('progress', {
			attrs : {
				max : 100,
				value : 0,
			},
			appendTo : div,
		});
		
		const xhr = new XMLHttpRequest();
		xhr.open('GET', args.url, true);
		xhr.responseType = 'arraybuffer';
		xhr.addEventListener('progress', e => {
			if (e.total) {
				progress.setAttribute('value', parseInt(e.loaded / e.total * 100));
			}
		});
		xhr.addEventListener('load', e => {
			progress.setAttribute('value', '100');

			const input = DOM('input', {
				type:'checkbox',
				change:e => {
					args.toggle(e.target.checked);
				},
			});
			div.parentNode.insertBefore(input, div);
			const anchor = DOM('a', {
				text:args.title, 
				href:args.source,
				attrs : {
					target : '_blank',
				},
			});
			div.parentNode.insertBefore(anchor, div);
			div.parentNode.insertBefore(DOM('br'), div);
			removeFromParent(div);

			const arrayBuffer = xhr.response;
			const data = new DataView(arrayBuffer);
			
			const floatBuffer = new Float32Array(data.byteLength / Float32Array.BYTES_PER_ELEMENT);
			const len = floatBuffer.length;
			for (let j = 0; j < len; ++j) {
				floatBuffer[j] = data.getFloat32(j * Float32Array.BYTES_PER_ELEMENT, true);
			}

			args.load(floatBuffer, input, anchor);
		});
		xhr.send();
	};
	
	return {div:div, download:download};
}

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
	let posDist = vec3.length(tmpV);
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
	
	const viewX = glutil.view.pos[0], viewY = glutil.view.pos[1], viewZ = glutil.view.pos[2];
	const pointX = selected.arrayBuf[0], pointY = selected.arrayBuf[1], pointZ = selected.arrayBuf[2];
	let viewToSelX = pointX - viewX, viewToSelY = pointY - viewY, viewToSelZ = pointZ - viewZ;
	const viewFwdX = -2 * (glutil.view.angle[0] * glutil.view.angle[2] + glutil.view.angle[3] * glutil.view.angle[1]); 
	const viewFwdY = -2 * (glutil.view.angle[1] * glutil.view.angle[2] - glutil.view.angle[3] * glutil.view.angle[0]); 
	const viewFwdZ = -(1 - 2 * (glutil.view.angle[0] * glutil.view.angle[0] + glutil.view.angle[1] * glutil.view.angle[1])); 
	const viewToSelInvLen = 1 / Math.sqrt(viewToSelX * viewToSelX + viewToSelY * viewToSelY + viewToSelZ * viewToSelZ);
	viewToSelX *= viewToSelInvLen; viewToSelY *= viewToSelInvLen; viewToSelZ *= viewToSelInvLen;
	const viewFwdInvLen = 1 / Math.sqrt(viewFwdX * viewFwdX + viewFwdY * viewFwdY + viewFwdZ * viewFwdZ);
	let axisX = viewFwdY * viewToSelZ - viewFwdZ * viewToSelY;
	let axisY = viewFwdZ * viewToSelX - viewFwdX * viewToSelZ;
	let axisZ = viewFwdX * viewToSelY - viewFwdY * viewToSelX;
	const axisLen = Math.sqrt(axisX * axisX + axisY * axisY + axisZ * axisZ);
	if (axisLen < .00002) return false;

	const axisInvLen = 1 / axisLen;
	axisX *= axisInvLen; axisY *= axisInvLen; axisZ *= axisInvLen;
	let cosOmega = viewFwdX * viewToSelX + viewFwdY * viewToSelY + viewFwdZ * viewToSelZ;
	if (cosOmega < -1) cosOmega = -1;
	if (cosOmega > 1) cosOmega = 1;
	const theta = .05 * Math.acos(cosOmega);
	const cosTheta = Math.cos(theta);
	const sinTheta = Math.sin(theta);
	const lookQuat = [axisX * sinTheta, axisY * sinTheta, axisZ * sinTheta, cosTheta];

	quat.mul(glutil.view.angle, lookQuat, glutil.view.angle);
	quat.normalize(glutil.view.angle, glutil.view.angle);
	
	return true;
}

/*
let updateInterval = undefined;
function update() {
	if (updateInterval) return;
	updateInterval = requestAnimationFrame(doUpdate);
}
*/
let spriteWidth;
function doUpdate() {
	//updateInterval = undefined;
	let selRes = lookAtSelected();
	let rotRes = applyLastMouseRot();
	universeUpdateHover();
	//if (!selRes && !rotRes) return;

	let centerX = selected.arrayBuf[0];
	let centerY = selected.arrayBuf[1];
	let centerZ = selected.arrayBuf[2];

	let dx = glutil.view.pos[0] - centerX; 
	let dy = glutil.view.pos[1] - centerY; 
	let dz = glutil.view.pos[2] - centerZ; 
	let d2 = dx * dx + dy * dy + dz * dz;
	let newViewDistance = Math.sqrt(d2);
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

let initDataSet = urlparams.get('dataset') || '2MRS';

closeSidePanel();

canvas = DOM('canvas', {
	css : {
		left : 0,
		top : 0,
		position : 'absolute',
		background : 'red',
		userSelect : 'none',
	},
	prependTo:document.body,
});

ids.help.addEventListener('click', () => {
	alert(`click and drag mouse to rotate
shift+click, mousewheel, or pinch to zoom
click a point to orbit it and for more information
ctrl+f or / to find an object
`);
});

ids.find.addEventListener('click', () => {
	let ident = prompt('Enter Object:');
	if (ident === null || ident.length == 0) return;
	findObject(ident);
});

ids.showgrid.checked = showGrid;
ids.showgrid.addEventListener('change', e => {
	showGrid = e.target.checked;
});

preload(['loading.gif'], init1);

function init1() {
	try {
		glutil = new GLUtil({canvas:canvas});
		gl = glutil.context;
	} catch (e) {
		removeFromParent(ids.panel);
		removeFromParent(canvas);
		show(ids.webglfail);
		throw e;
	}

	lastMouseRot = quat.create();
	tmpV = vec3.create();
	tmpQ = quat.create();	

	glutil.onfps = function(fps) {
		ids.fps.innerText = fps.toFixed(2) + " fps";
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
		onload : () => { init2(); },
	});
}

function init2() {
	//create shaders

	pointShader = new glutil.Program({
		vertexCode : `
in vec3 vertex;
uniform mat4 mvMat;
uniform mat4 projMat;
uniform float spriteWidth;
uniform float screenWidth;
void main() {
	vec4 eyePos = mvMat * vec4(vertex, 1.);
	gl_Position = projMat * eyePos;
	gl_PointSize = spriteWidth*screenWidth/gl_Position.w;
}
`,
		fragmentCode : `
uniform sampler2D tex;
out vec4 fragColor;
void main() {
	float z = gl_FragCoord.z;
	fragColor = texture(tex, gl_PointCoord);
	fragColor *= fragColor;
	//float v = 1. / (z * z);
	//fragColor *= v;
}
`,
		uniforms : {
			tex : 0, 
			spriteWidth : 1./10.,
			screenWidth : glutil.canvas.width
		}
	});

	selectedShader = new glutil.Program({
		vertexCode : `
in vec3 vertex;
uniform mat4 mvMat;
uniform mat4 projMat;
uniform float spriteWidth;
uniform float screenWidth;
void main() {
	vec4 eyePos = mvMat * vec4(vertex, 1.);
	gl_Position = projMat * eyePos;
	gl_PointSize = spriteWidth*screenWidth/gl_Position.w;
}
`,
		fragmentCode : `
uniform vec3 color;
out vec4 fragColor;
void main() {
	vec2 absDelta = abs(gl_PointCoord - .5);
	float dist = max(absDelta.x, absDelta.y);
	if (dist < .4) discard;
	fragColor = vec4(color, 1.); 
}
`,
		uniforms : {spriteWidth:1./10./2.}
	});
	
	//scene objs

	selected = new Highlight();
	hover = new Highlight();
	hover.sceneObj.uniforms.color = [0,1,0];
	hover.sceneObj.hidden = true;
	hover.stayHidden = false;

	//coordinate chart overlay

	let vertexes = [];
	let thetaDivs = 100;
	let radiusDivs = 2;
	let largeThetaDivs = 6;
	let maxRadius = 1;
	let zDivs = 2;
	let zMin = -.5;
	let zMax = .5;
	for (let zIndex = 0; zIndex < zDivs; ++zIndex) {
		let z = zIndex / (zDivs-1) * (zMax - zMin) + zMin;
		let z2 = z + (zMax - zMin) / (zDivs-1);
		for (let thetaIndex = 0; thetaIndex < thetaDivs; ++thetaIndex) {
			let th1 = 2*Math.PI*thetaIndex/thetaDivs;
			let th2 = 2*Math.PI*(thetaIndex+1)/thetaDivs;
			for (let radiusIndex = 1; radiusIndex <= radiusDivs; ++radiusIndex) {
				let radius = maxRadius * radiusIndex / radiusDivs;
				vertexes.push(radius*Math.cos(th1));
				vertexes.push(radius*Math.sin(th1));
				vertexes.push(z);
				vertexes.push(radius*Math.cos(th2));
				vertexes.push(radius*Math.sin(th2));
				vertexes.push(z);
			}
		}
		if (zIndex < zDivs - 1) {
			for (let thetaIndex = 0; thetaIndex < largeThetaDivs; ++thetaIndex) {
				let theta = 2*Math.PI*thetaIndex/largeThetaDivs;
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
		shader : new glutil.Program({
			vertexCode : `
in vec3 vertex;
uniform mat4 mvMat;
uniform mat4 projMat;
uniform float scale;
void main() {
	gl_Position = projMat * (mvMat * vec4(scale * vertex, 1.));
}
`,
			fragmentCode : `
uniform float alpha;
out vec4 fragColor;
void main() {
	fragColor = vec4(1., 1., 1., alpha);
}
`
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

	init3();
}


function init3() {
	[
		{title:'2MRS', url:'2mrs.f32', source:'https://tdc-www.cfa.harvard.edu/2mrs/'},
		{title:'6dF GS', url:'6dfgs.f32', source:'https://www.aao.gov.au/6dFGS/'},
		{title:'SDSS-DR16', url:'sdss-dr16.f32', source:'https://www.sdss.org/dr16/'},
		{title:'SIMBAD', url:'simbad.f32', source:'https://simbad.u-strasbg.fr/simbad/'},
		{title:'Gaia stars', url:'gaia.f32', source:'https://sci.esa.int/gaia/'},
	].forEach((v,k) => {
		let sceneObj;
		const request = fileRequest({
			title:v.title,
			url:v.url,
			source:v.source,
			load:(arrayBuffer, input) => {
				
				let pointVtxBuf = new glutil.ArrayBuffer({
					data : arrayBuffer,
					usage : gl.STATIC_DRAW
				});

				let dataSet = {
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
					input.removeAttribute('checked');
				} else {
					input.checked = true;
				}
				
				dataSet.sceneObj = sceneObj;
	
				//start off the render loop:
				let ondraw;
				ondraw = function() {
					glutil.draw();
	
					if (selected.index !== undefined) { 
						gridObj.pos[0] = selected.arrayBuf[0];
						gridObj.pos[1] = selected.arrayBuf[1];
						gridObj.pos[2] = selected.arrayBuf[2];
					}
					//draw fades between various grids depending on where the view scale is
					let newGridScale = (1 << parseInt(Math.log2(viewDistance * 1024))) / 1024;
					if (newGridScale != gridScale) {
						gridFadeOutSize = gridScale;
						gridFadeOutTime = Date.now();
						gridFadeInSize = newGridScale;
						gridFadeInTime = Date.now();
						gridScale = newGridScale;
					}
					if (showGrid && gridScale >= 1) {
						let gridFadeTime = 1000;
						if (gridFadeOutSize !== undefined) {
							let alpha = gridAlpha * (1 - (Date.now() - gridFadeOutTime) / gridFadeTime);
							if (alpha > 0) {
								gridObj.draw({
									uniforms : {
										scale : gridFadeOutSize,
										alpha : alpha,
									}
								});
							}
						}
						if (gridFadeInSize !== undefined) {
							let alpha = gridAlpha * Math.min(1, (Date.now() - gridFadeInTime) / gridFadeTime);
							gridObj.draw({
								uniforms : {
									scale : gridFadeInSize,
									alpha : alpha,
								}
							});
						}
					}

					doUpdate();
					requestAnimationFrame(ondraw);
				};
				ondraw();

				//I only have search data for 2mrs right now
				if (v.title == '2MRS' || v.title == 'SIMBAD') {
					//substring 1 removes the preface ?
					const obj = urlparams.get('obj');
					if (obj) findObject(obj);
				}
			},
			toggle:function(enabled) {
				sceneObj.hidden = !enabled;
			}
		});
		ids.fileRequests.appendChild(request.div);
		if (k == 0) request.download();
	});
}
