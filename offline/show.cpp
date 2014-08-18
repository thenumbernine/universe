#include <SDL/SDL.h>
#ifdef __WINDOWS__
#include <GL/glew.h>
#include <GL/wglew.h>
#include <GL/gl.h>
#endif
#ifdef __APPLE__
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#endif
#include "vec.h"

#include <string>
#include <vector>
#include <fstream>
#include <iostream>

#include "exception.h"
#include "util.h"
#include "stat.h"
#include "octree.h"
#include "mrucache.h"

using namespace std;

struct OctreeNodeCacheEntry {
	typedef OctreeNode ArgType;
	ArgType *obj;
	vec3f *vtxbuf;
	streamsize vtxcount;
	OctreeNodeCacheEntry(ArgType *obj_) : obj(obj_) {
		vtxbuf = (vec3f*)getFile(obj->getFileName().c_str(), &vtxcount);
		vtxcount /= sizeof(vec3f);
	}
	~OctreeNodeCacheEntry() {
		delete[] (char*)vtxbuf;	
	}
	const ArgType *getArg() const { return obj; }
};

typedef MRUCache<OctreeNodeCacheEntry> OctreeNodeCache;
OctreeNodeCache octreeNodeCache(100);

#define checkGL()	\
	{int err = glGetError(); if (err) cout << __LINE__ << " " << err << endl; }

GLuint createShaderProgram(const string &vertexFilename, const string &fragmentFilename) {
	const GLchar *shaderCodes[1];
	streamsize shaderCodeLen;
	GLint shaderCodeLeni; 
	GLint status;
	
	GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
	shaderCodes[0] = (char*)getFile(vertexFilename, &shaderCodeLen);
	shaderCodeLeni = (GLint)shaderCodeLen;
	glShaderSource(vertexShader, 1, shaderCodes, &shaderCodeLen);
	delete shaderCodes[0];
	
	GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	shaderCodes[0] = (char*)getFile(fragmentFilename, &shaderCodeLen);
	shaderCodeLeni = (GLint)shaderCodeLen;
	glShaderSource(fragmentShader, 1, shaderCodes, &shaderCodeLen);
	delete shaderCodes[0];

	GLuint shaders[] = {vertexShader, fragmentShader};
	const char *shaderNames[] = {"vertex", "fragment"};
	for (int i = 0; i < numberof(shaders); i++) {
		GLuint shader = shaders[i];
		glCompileShader(shader);
		glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
		if (status == GL_FALSE) {
			GLint len;
			glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
			char *log = new char[len+1];
			log[len] = '\0';
			glGetShaderInfoLog(shader, len, &status, log);
			cerr << shaderNames[i] << " shader failed!" << endl << log << endl;
		}
	}

	GLuint shaderProgram = glCreateProgram();
	glAttachShader(shaderProgram, vertexShader);
	glAttachShader(shaderProgram, fragmentShader);
	glLinkProgram(shaderProgram);
	checkGL();

	return shaderProgram;
}

struct RenderBase {
	bool sceneChanged;
	bool orbit;
	quatd viewAngle;
	vec3d viewPos;
	bool done;
	long lasttick;
	bool leftButtonDown;
	bool shiftDown;
	int viewWidth;
	int viewHeight;
	int currentFileIndex;
	bool resetFile;
	FILE *currentFile;
	static const int vtxBufferCount = 100000;
	float *vtxBuffer;
	float lumScale;
	const vector<string> &filenames;

	RenderBase(const vector<string> &filenames_/*, const string &datasetname*/) :
		filenames(filenames_),
		sceneChanged(true),
		orbit(true),
		viewPos(0,0,6000),	//6000 can see it all
		done(false),
		leftButtonDown(false),
		shiftDown(false),
		viewWidth(512),
		viewHeight(512),
		currentFileIndex(0),
		resetFile(true),
		currentFile(NULL),
		vtxBuffer(NULL),
		lumScale(100.)
	{
/*
		StatSet totalStats;
		totalStats.read((string("datasets/") + datasetname + "/stats/total.stats").c_str());
		
		OctreeNode *root = new OctreeNode(NULL, -1, vec3f(
			totalStats.vars()[STATSET_X].min,
			totalStats.vars()[STATSET_Y].min,
			totalStats.vars()[STATSET_Z].min
		), vec3f(
			totalStats.vars()[STATSET_X].max,
			totalStats.vars()[STATSET_Y].max,
			totalStats.vars()[STATSET_Z].max
		));
*/
		assert(SDL_Init(SDL_INIT_VIDEO) == 0);

		SDL_Event event;
		event.type = SDL_VIDEORESIZE;
		event.resize.w = viewWidth;
		event.resize.h = viewHeight;
		SDL_PushEvent(&event);

		SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
		
		SDL_Surface *screen = SDL_SetVideoMode(event.resize.w, event.resize.h, 32, SDL_OPENGL | SDL_RESIZABLE);
		SDL_WM_SetCaption("2MASS All Sky Survey", NULL);

#ifdef __WINDOWS__
		if (glewInit() != GLEW_OK) throw Exception() << "failed to init GLEW";
#endif
		vtxBuffer = new float[3*vtxBufferCount];
		
		glEnableClientState(GL_VERTEX_ARRAY);
		glVertexPointer(3, GL_FLOAT, 0, vtxBuffer);

	}

	virtual void loop() {
		do {
			update();
		} while (!done);

		SDL_Quit();
	}

	virtual void update() {
		SDL_Event event;
		while (SDL_PollEvent(&event) && !done) {
			handleEvent(event);
		}
	
		draw();
		
		SDL_GL_SwapBuffers();

		checkGL();
	}

	virtual void handleEvent(const SDL_Event &event) {
		if (event.type == SDL_QUIT) {
			done = true;
		} else if (event.type == SDL_VIDEORESIZE) {
			viewWidth = event.resize.w;
			viewHeight = event.resize.h;
			//screen = SDL_SetVideoMode(event.resize.w, event.resize.h, 32, SDL_OPENGL | SDL_RESIZABLE);
			
			//reset rendering
			if (currentFile) { 
				fclose(currentFile); 
				currentFile = NULL; 
			}
			currentFileIndex = 0;
			resetFile = true;
		} else if (event.type == SDL_KEYUP || event.type == SDL_KEYDOWN) {
			if (event.type == SDL_KEYDOWN) {
				if (event.key.keysym.sym == 'q' && (event.key.keysym.mod & KMOD_META)) {
					done = true;
				}
				if (event.key.keysym.sym == SDLK_F4 && (event.key.keysym.mod & KMOD_LALT || event.key.keysym.mod & KMOD_RALT)) {
					done = true;
				}
				if (event.key.keysym.sym == SDLK_a || event.key.keysym.sym == SDLK_z) {
					if (event.key.keysym.sym == SDLK_a) {
						lumScale *= 1.3;
					} else if (event.key.keysym.sym == SDLK_z) {
						lumScale /= 1.3;
					}
					cout << "lumScale " << lumScale << endl;
					sceneChanged = true;
				}
				if (event.key.keysym.sym == SDLK_o) {
					orbit = !orbit;
				}
			}

			if (event.key.keysym.sym == SDLK_LSHIFT || event.key.keysym.sym == SDLK_RSHIFT) {
				shiftDown = event.type == SDL_KEYDOWN;
			}
		} else if (event.type == SDL_MOUSEMOTION) {
			if (leftButtonDown) {
				double idx = event.motion.xrel;
				double idy = event.motion.yrel;
				if (shiftDown) {
					viewPos *= exp(.01 * (double)idy);
				} else {
					double magn = sqrt(idx*idx + idy*idy);
					double dx = idx / magn;
					double dy = idy / magn;
					viewAngle = (viewAngle * angleAxisToQuat(-dy, -dx, 0., magn)).normalize();
				}

				//reset rendering
				if (currentFile) { 
					fclose(currentFile); 
					currentFile = NULL; 
				}
				currentFileIndex = 0;
				resetFile = true;
				sceneChanged = true;
			}
		} else if (event.type == SDL_MOUSEBUTTONDOWN) {
			leftButtonDown = true;
		} else if (event.type == SDL_MOUSEBUTTONUP) {
			leftButtonDown = false;
		}
	}

	virtual void draw() = 0;
};

struct RenderSimple;

struct RenderSimplePointSet {
	RenderSimple &renderSimple;
	streamsize numVtxs, numLinks;
	vec3f *vtxs;
	vec2i *links;
	int *clusters;
	bool visible;

	RenderSimplePointSet(RenderSimple &renderSimple_, const string &filename);
	~RenderSimplePointSet();
	void draw();
};

//draw one buffer in realtime
struct RenderSimple : public RenderBase {
	int keyReg;
	bool showLinks;
	int showOnly;

	vector<RenderSimplePointSet*> pointSets;

	RenderSimple(const vector<string> &filenames/*, const string &datasetname*/)
	: RenderBase(filenames/*, datasetname*/),
		showLinks(true),
		keyReg(0),
		showOnly(-1)
	{
		for (vector<string>::const_iterator i = filenames.begin(); i != filenames.end(); ++i) {
			const string &filename = *i; 
			pointSets.push_back(new RenderSimplePointSet(*this, filename));
		}

		glPointSize(2);
	}

	virtual ~RenderSimple() {
		pointSets.clear();	
	}

	virtual void draw() {
		glViewport(0, 0, viewWidth, viewHeight);
		glClear(GL_COLOR_BUFFER_BIT);

		double zNear = 1.;
		double zFar = 1e+6;
		double tanFovX = 1.;
		double tanFovY = 1.;
		double aspectRatio = (double)viewWidth / (double)viewHeight;
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glFrustum(-zNear * aspectRatio * tanFovX, zNear * aspectRatio * tanFovX, -zNear * tanFovY, zNear * tanFovY, zNear, zFar);;

		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		if (orbit) {
			vec4d aa = quatToAngleAxis(viewAngle);
			glTranslated(-viewPos.x, -viewPos.y, -viewPos.z);
			glRotated(-aa.w, aa.x, aa.y, aa.z);
		} else {
			vec4d aa = quatToAngleAxis(viewAngle);
			glTranslated(-viewPos.x, -viewPos.y, -viewPos.z);
			glRotated(-aa.w, aa.x, aa.y, aa.z);
		}
		glEnable(GL_BLEND);
		glDisable(GL_DEPTH_TEST);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		if (showOnly >= 0 && showOnly < pointSets.size()) { 
			pointSets[showOnly]->draw();
		} else {
			for (vector<RenderSimplePointSet*>::iterator i = pointSets.begin(); i != pointSets.end(); ++i) {
				RenderSimplePointSet *p = *i;
				p->draw();
			}
		}
	}

	virtual void handleEvent(const SDL_Event &event) {
		RenderBase::handleEvent(event);
		if (event.type == SDL_KEYDOWN) {

			if (event.key.keysym.sym == 'l') {
				showLinks = !showLinks;
			} else if (event.key.keysym.sym == 'v') {
				//1-based, so 'v'-mashing doesn't flicker buffer 0
				if (!keyReg) {
				} else if (keyReg < 1 || keyReg > pointSets.size()) {
					cerr << "failed to find point set " << keyReg << endl;
				} else {
					RenderSimplePointSet *p = pointSets[keyReg-1]; 
					p->visible = !p->visible;
				}
			} else if (event.key.keysym.sym == '=') {	//or '+' ...
				showOnly++;
				if (showOnly >= pointSets.size()) showOnly = -1;
			} else if (event.key.keysym.sym == '-') {
				showOnly--;
				if (showOnly < -1) showOnly = pointSets.size()-1;
			}
			
			//gonna go for vim-style input
			if (event.key.keysym.sym >= '0' && event.key.keysym.sym <= '9') {
				int i = event.key.keysym.sym - '0';
				keyReg *= 10;
				keyReg += i;
			} else {
				keyReg = 0;
			}	
		}
	}
};

//RenderSimplePointSet body

RenderSimplePointSet::RenderSimplePointSet(RenderSimple &renderSimple_, const string &filename) 
: 	renderSimple(renderSimple_),
	numVtxs(0),
	numLinks(0),
	vtxs(NULL),
	links(NULL),
	clusters(NULL),
	visible(true)
{
	vtxs = (vec3f*)getFile(filename, &numVtxs);
	numVtxs /= sizeof(vec3f);
	streamsize clusterFileSize = 0;
	clusters = (int*)getFile(filename.substr(0, filename.length()-3)+"clusters", &clusterFileSize);
	assert(clusterFileSize / sizeof(int) == numVtxs);
	numLinks = 0;
	links = (vec2i*)getFile(filename.substr(0, filename.length()-3)+"links", &numLinks);
	numLinks /= sizeof(vec2i);	
	
	for (int i = 0; i < numVtxs-1; i++) {
		int j = i+1;
		for (; j < numVtxs; j++) {
			if (clusters[i] == clusters[j]) {
				break;
			}
		}
		if (j == numVtxs) {
			clusters[i] = -1;
		}
	}

	//pick some random numbers...
	for (int i = 0; i < numVtxs; i++) {
		if (clusters[i] == -1) {
			clusters[i] = 0xff7f7f7f;
		} else {
			srand(clusters[i]);
			rand();
			rand();
			rand();
			vec3f v(frand(), frand(), frand());
			float l = v.len();
			if (l < 1) v /= l;
			clusters[i] =
				(int)(v.x * 255.f) |
				((int)(v.y * 255.f) << 8) |
				((int)(v.z * 255.f) << 16) |
				0xff000000;
		}
	}

}

RenderSimplePointSet::~RenderSimplePointSet() {
	delete[] (char*)vtxs;
	delete[] (char*)links;
	delete[] (char*)clusters;
}

void RenderSimplePointSet::draw() {
	if (!visible) return;

	glVertexPointer(3, GL_FLOAT, 0, vtxs);
	glColorPointer(4, GL_UNSIGNED_BYTE, 0, clusters);

	glEnableClientState(GL_COLOR_ARRAY);

	glDrawArrays(GL_POINTS, 0, numVtxs);
	
	if (renderSimple.showLinks) {
		glDrawElements(GL_LINES, numLinks, GL_UNSIGNED_INT, links);
	}	
}

//accumulate multiple buffers in FBO ... so slow that I diverted this effort to show-still, the software single frame render
struct RenderMultipleAccum : public RenderBase {
	GLuint displayShaderProgram;
	GLuint displaySrcTexUniform; 
	GLuint displayLastTexUniform; 
	GLuint dxUniform;
	GLuint lumScaleUniform;
	GLuint displayClearTexFlagLocation;
	GLuint fboID;
	GLuint splatShaderProgram;
	GLuint accumTexID;
	GLsizei screenTexWidth;
	GLsizei screenTexHeight;
	GLuint displayTexIDs[2];
	int currentBuffer;

	RenderMultipleAccum(const vector<string> &filenames, const string &datasetname) 
	: RenderBase(filenames/*, datasetname*/),
		displayShaderProgram(0),
		displaySrcTexUniform(0),
		displayLastTexUniform(0),
		dxUniform(0),
		lumScaleUniform(0),
		displayClearTexFlagLocation(0),
		splatShaderProgram(0), 
		fboID(0),
		accumTexID(0),
		screenTexWidth(2048),
		screenTexHeight(2048),
		currentBuffer(0)
	{
		//float texture to hold the deferred/accumulated render buffer
		glGenTextures(1, &accumTexID);
		glBindTexture(GL_TEXTURE_2D, accumTexID);
		{
			size_t datasize = screenTexWidth * screenTexHeight * sizeof(float);
			char *data = new char[datasize];
			memset(data, 0, datasize);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE32F_ARB, screenTexWidth, screenTexHeight, 0, GL_LUMINANCE, GL_FLOAT, data);
			delete[] data;
		}
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glBindTexture(GL_TEXTURE_2D, 0);
		checkGL();

		//rgb byte texture to hold the PSF byproduct of the acucm tex 
		glGenTextures(2, displayTexIDs);
		for (int i = 0; i < 2; i++) {
			glBindTexture(GL_TEXTURE_2D, displayTexIDs[i]);
			{
				size_t datasize = screenTexWidth * screenTexHeight;
				char *data = new char[datasize];
				memset(data, 0, datasize);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, screenTexWidth, screenTexHeight, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, data);
				delete[] data;
			}
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glBindTexture(GL_TEXTURE_2D, 0);
			checkGL();
		}

		//accumulating stars via inverse-distance-squared
		// will GL_FOG do this? or is its math only for fixed-point buffers (like I am suspicious alpha blending is as well?)
		splatShaderProgram = createShaderProgram("show-splat.vsh", "show-splat.fsh");

		//shader for HDR/whatever other rendering to screen
	
		//framebuffer for deferred rendering
		glGenFramebuffers(1, &fboID);
		glBindFramebuffer(GL_FRAMEBUFFER, fboID);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, accumTexID, 0);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, displayTexIDs[0], 0);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, displayTexIDs[1], 0);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		checkGL();
		
		displayShaderProgram = createShaderProgram("show-display.vsh", "show-display.fsh");
		glUseProgram(displayShaderProgram);
		displaySrcTexUniform = glGetUniformLocation(displayShaderProgram, "srcTex");
		displayLastTexUniform = glGetUniformLocation(displayShaderProgram, "lastTex");
		dxUniform = glGetUniformLocation(displayShaderProgram, "dx");
		lumScaleUniform = glGetUniformLocation(displayShaderProgram, "lumScale");
		displayClearTexFlagLocation = glGetUniformLocation(displayShaderProgram, "clearTexFlag");	
		glUniform1i(displaySrcTexUniform, 0);
		glUniform1i(displayLastTexUniform, 1);
		glUniform1f(displayClearTexFlagLocation, 1.);
		//TODO onresize, should be equal to a pixel in the texture's space
		glUniform1f(lumScaleUniform, lumScale);
		glUseProgram(0);
	}

	void update() {
		GLint drawBuffer;
		glGetIntegerv(GL_DRAW_BUFFER, &drawBuffer);
		if (currentFile && feof(currentFile)) {
			fclose(currentFile);
			currentFile = NULL;
			currentFileIndex++;
		}
		if (currentFileIndex < filenames.size()) {
			
			glBindFramebuffer(GL_FRAMEBUFFER, fboID);
			int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
			if (status != GL_FRAMEBUFFER_COMPLETE) {
				cout << "framebuffer incomplete with status " << status << endl;
			}
			glDrawBuffer(GL_COLOR_ATTACHMENT0);
		
			//draw next pass into FBO

			glViewport(0, 0, screenTexWidth, screenTexHeight); 
			
			if (resetFile) {
				resetFile = false;
				glClearColor(0,0,0,1);
				glClear(GL_COLOR_BUFFER_BIT);
			}
			
			double zNear = 1.;
			double zFar = 1e+6;
			double tanFovX = 1.;
			double tanFovY = 1.;
			double aspectRatio = (double)viewWidth / (double)viewHeight;
			glMatrixMode(GL_PROJECTION);
			glLoadIdentity();
			glFrustum(-zNear * aspectRatio * tanFovX, zNear * aspectRatio * tanFovX, -zNear * tanFovY, zNear * tanFovY, zNear, zFar);;

			glMatrixMode(GL_MODELVIEW);
			glLoadIdentity();
			vec4d aa = quatToAngleAxis(viewAngle);
			glTranslated(-viewPos.x, -viewPos.y, -viewPos.z);
			glRotated(-aa.w, aa.x, aa.y, aa.z);

			glEnable(GL_BLEND);
			glBlendFunc(GL_ONE, GL_ONE);


			if (!currentFile) {
				currentFile = fopen(filenames[currentFileIndex].c_str(), "rb");
				assert(currentFile);
			}
			
			int numVtxsRead = fread(vtxBuffer, sizeof(float)*3, vtxBufferCount, currentFile);
			
			if (feof(currentFile)) {
				fclose(currentFile);
				currentFile = NULL;
				currentFileIndex++;
			}

			//cout << "drawing " << (currentFileIndex+1) << " of " << filenames.size() << endl;
			glUseProgram(splatShaderProgram);
			glDrawArrays(GL_POINTS, 0, numVtxsRead);
			glUseProgram(0);
			
			glDisable(GL_BLEND);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			checkGL();
		}
/*
displayTexIDs[0] = GL_COLOR_ATTACHMENT1
displayTexIDs[1] = GL_COLOR_ATTACHMENT2
*/	
		{
			int nextBuffer = !currentBuffer;
			// Now we draw the postprocessed image (hdr filters whatever)
			// does the framebuffer need to be unbound & rebound, or does it know to flush just by changing glDrawBuffer ?
			
			glBindFramebuffer(GL_FRAMEBUFFER, fboID);
			int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
			if (status != GL_FRAMEBUFFER_COMPLETE) {
				cout << "framebuffer incomplete with status " << status << endl;
			}
			glDrawBuffer(GL_COLOR_ATTACHMENT1 + nextBuffer);
			
			glViewport(0, 0, screenTexWidth, screenTexHeight); 

			glMatrixMode(GL_PROJECTION);
			glLoadIdentity();
			glOrtho(0, 1, 0, 1, -1, 1);
			glMatrixMode(GL_MODELVIEW);
			glLoadIdentity();
			checkGL();

			glUseProgram(displayShaderProgram);
			checkGL();
			
			glUniform1f(lumScaleUniform, lumScale);
		
			if (sceneChanged) {
				glUniform1f(displayClearTexFlagLocation, 0.);
				sceneChanged = false;
			} else {
				glUniform1f(displayClearTexFlagLocation, 1.);
			}
			//glUniform2f(dxUniform, 1./(float)viewWidth, 1./(float)viewHeight);
			glUniform2f(dxUniform, 1./(float)viewWidth, 1./(float)viewHeight);
		
			glUniform1f(lumScaleUniform, lumScale);
			
			checkGL();
			glBindTexture(GL_TEXTURE_2D, accumTexID);
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, displayTexIDs[currentBuffer]);
			glActiveTexture(GL_TEXTURE0);
			checkGL();
			
			glBegin(GL_QUADS);
			glVertex2f(0,0);
			glVertex2f(1,0);
			glVertex2f(1,1);
			glVertex2f(0,1);
			glEnd();
			checkGL();
			
			glUseProgram(0);
			checkGL();
			
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			checkGL();
	
			currentBuffer = nextBuffer; 
		}
		
		glDrawBuffer(drawBuffer);

		//draw it to the screen

		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, displayTexIDs[currentBuffer]);
		
		glViewport(0, 0, viewWidth, viewHeight);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(0, 1, 0, 1, -1, 1);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();

		glBegin(GL_QUADS);
		glTexCoord2f(0,0);	glVertex2f(0,0);
		glTexCoord2f(1,0);	glVertex2f(1,0);
		glTexCoord2f(1,1);	glVertex2f(1,1);
		glTexCoord2f(0,1);	glVertex2f(0,1);
		glEnd();
		
		glDisable(GL_TEXTURE_2D);	
	
	}
};


void showhelp() {
	cout
	<< "usage: show <options>" << endl
	<< "options:" << endl
	<< "	--set <set>			specify the dataset. default is 'allsky'." << endl
	<< "	--file	<file> add that file." << endl
	<< "	--all	use all files." << endl
	;
}

void handleArgs(int argc, char **argv, vector<string> &filenames, string &datasetname) {
	datasetname = "allsky";
	for (int k = 1; k < argc; k++) {
		if (!strcmp(argv[k], "--set") && k < argc-1) {
			datasetname = argv[++k];
		} else if (!strcmp(argv[k], "--all")) {
			string dirname = string("datasets/") + datasetname + "/points";
			list<string> dirFilenames = getDirFileNames(dirname);
			for (list<string>::iterator i = dirFilenames.begin(); i != dirFilenames.end(); ++i) {
				const string &filename = *i;
				string base, ext;
				getFileNameParts(filename, base, ext);
				if (ext == "f32") {
					filenames.push_back(dirname + "/" + filename);
				}
			}
		} else if (!strcmp(argv[k], "--file") && k < argc-1) {
			filenames.push_back(argv[++k]);
		} else {
			showhelp();
			exit(0);
		}
	}

	if (!filenames.size()) {
		cout << "got no files!" << endl;
		showhelp();
		exit(0);
	}
}

int main(int argc, char **argv) {
	vector<string> filenames;
	string datasetname;
	
	handleArgs(argc, argv, filenames, datasetname);
	
	RenderSimple render(filenames);
	//RenderMultipleAccum render(filenames);

	render.loop();
}

