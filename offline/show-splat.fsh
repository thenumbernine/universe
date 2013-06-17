varying vec3 pos;
void main() {
	float nz = 1. / pos.z;
	float v = nz * nz; 
	gl_FragColor = vec4(v,v,v,1.);
}
