varying vec2 pos;
void main() {
	pos = gl_Vertex.xy;	
	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
