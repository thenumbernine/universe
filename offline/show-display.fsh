varying vec2 pos;

/*
srcTex is the source of our iterative blur
it is static so long as the scene is static
*/
uniform sampler2D srcTex;

/*
last iteration's tex
*/
uniform sampler2D lastTex;

/*
0 = clear last tex, 1 = iterate using last tex
*/
uniform float clearTexFlag;

/*
how much to scale the source by
*/
uniform float lumScale;

/*
step in tex space for neighbor sampling
*/
uniform vec2 dx;

#define filterRadius 2
void main() {
#if 0	//nothing at all
	gl_FragColor = lumScale * texture2D(srcTex, pos);
#endif
#if 0	//stupid slow image kernel
	//TODO glare or HDR or whatever
	gl_FragColor = lumScale * (
		41. * texture2D(srcTex, pos)
		
		+ 26. * (texture2D(srcTex, pos+vec2(dx.x,0.))
			+ texture2D(srcTex, pos+vec2(-dx.x,0.))
			+ texture2D(srcTex, pos+vec2(0.,dx.y))
			+ texture2D(srcTex, pos+vec2(0.,-dx.y))
		)
		+ 16. * (texture2D(srcTex, pos+vec2(dx.x, dx.y))
			+ texture2D(srcTex, pos+vec2(dx.x, -dx.y))
			+ texture2D(srcTex, pos+vec2(-dx.x, dx.y))
			+ texture2D(srcTex, pos+vec2(-dx.x, -dx.y))
		)
		+ 7. * (texture2D(srcTex, pos+vec2(2.*dx.x, 0.))
			+ texture2D(srcTex, pos+vec2(-2.*dx.x, 0.))
			+ texture2D(srcTex, pos+vec2(0.,2.*dx.y))
			+ texture2D(srcTex, pos+vec2(0.,-2.*dx.y))
		)
#if 0
		+ 4. * (texture2D(srcTex, pos+vec2(2.*dx.x, dx.y))
			+ texture2D(srcTex, pos+vec2(-2.*dx.x, dx.y))
			+ texture2D(srcTex, pos+vec2(2.*dx.x, -dx.y))
			+ texture2D(srcTex, pos+vec2(-2.*dx.x, -dx.y))
			+ texture2D(srcTex, pos+vec2(dx.x, 2.*dx.y))
			+ texture2D(srcTex, pos+vec2(dx.x, -2.*dx.y))
			+ texture2D(srcTex, pos+vec2(-dx.x, 2.*dx.y))
			+ texture2D(srcTex, pos+vec2(-dx.x, -2.*dx.y))
		)
		+ texture2D(srcTex, pos+2.*dx)
		+ texture2D(srcTex, pos-2.*dx)
		+ texture2D(srcTex, pos+2.*vec2(dx.x, -dx.y))
		+ texture2D(srcTex, pos-2.*vec2(dx.x, -dx.y))
#endif
	);
#endif
#if 1	//iterative
	float src = lumScale * texture2D(srcTex, pos).r;
	float xp = texture2D(lastTex, pos + vec2(dx.x, 0.)).r;
	float xn = texture2D(lastTex, pos - vec2(dx.x, 0.)).r;
	float yp = texture2D(lastTex, pos + vec2(0., dx.y)).r;
	float yn = texture2D(lastTex, pos - vec2(0., dx.y)).r;

	float dst = src + .25 * (xp + yp + xn + yn);

	dst *= clearTexFlag;
	gl_FragColor = vec4(dst, dst, dst, 1.);

#endif
}

