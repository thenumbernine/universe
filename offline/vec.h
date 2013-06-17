#ifndef VEC_H
#define VEC_H

#include <assert.h>

#define REPEAT_WITH_CONST(head, body)	\
	head { body; }				\
	const head const { body; }

//generic indexing template so we dont have to retype so much code
#define ADD_VECTOR_INDEXING(returntype, vectormaxsize, vectorptr)	\
	REPEAT_WITH_CONST(returntype operator[](int i), assert(0 <= i && i < vectormaxsize); return (vectorptr)[i]; )	\
	REPEAT_WITH_CONST(returntype operator()(int i), assert(0 <= i && i < vectormaxsize); return (vectorptr)[i]; )

#define REPEAT_TYPECAST_WITH_CONST(type,code)	\
	operator type() { code; }	\
	operator const type() const { code; }

#define ADD_VECTOR_SUBSET_ACCESS()	\
	/*you'll have to use that template parameter for subset size*/	\
	/*rely on typecase T* operator*/ 	\
	template<int DIM2> _vec<DIM2,T> &subset(int start) {	\
		assert(start >= 0 && start + DIM2 <= dim());	\
		return *(_vec<DIM2,T>*)((T*)(*this) + start);	\
	}	\
	_vec<0,T> subset(int start, int subsize) {	\
		assert(start >= 0 && start + subsize <= dim());	\
		return _vec().setRemote(subsize, (T*)(*this) + start);	\
	}

template<int DIM, typename T>
class _vec;

/**
 * This is the class used to represent 3D vectors
 */
template<typename T>
class _vec<3,T> {
public:

	T x, y, z;

	typedef T type;
	int dim() const { return 3; }

	_vec() : x(T()), y(T()), z(T()) {}
	_vec(T _v) : x(_v), y(_v), z(_v) {}
	_vec(T _x, T _y, T _z) : x(_x), y(_y), z(_z) {}
	_vec(const T *v) : x(v[0]), y(v[1]), z(v[2]) {}

	_vec &set(const T v) { x = v; y = v; z = v; return *this; }
	_vec &set(const T *v) { x = v[0]; y = v[1]; z = v[2]; return *this; }
	_vec &set(T _x, T _y, T _z) { x = _x; y = _y; z = _z; return *this; }

	_vec &operator=(const T &a) { x = a; y = a; z = a; return *this; }
	_vec &operator=(const _vec<3,T> &a) { x = a.x; y = a.y; z = a.z; return *this; }

	//auto-type-cast between vectors of the same size
	template<typename U> operator _vec<3,U>() const { return _vec<3,U>((U)x, (U)y, (U)z); }
	//auto-type-cast between vectors of the same type
	template<int DIM2> operator _vec<DIM2,T>() const { assert(DIM2 <= 3); return *(_vec<DIM2,T>*)this; }
	//auto-type-cast from vector to its pointer.  seems this once caused some problems. . . 
	REPEAT_TYPECAST_WITH_CONST(T*, return &x)

	//indexing
	ADD_VECTOR_INDEXING(T&, dim(), &x)
	
	//subset access
	ADD_VECTOR_SUBSET_ACCESS()

	//member methods:
	template<typename U> T dot(const _vec<3,U> &v) const { return x*(T)v.x + y*(T)v.y + z*(T)v.z; }
	T lenSq() const { return dot(*this); }
	double len() const { return sqrt((double)lenSq()); }
	_vec &normalize() { *this /= len(); return *this; }
	
	_vec &cross(const _vec<3,T> &a, const _vec<3,T> &b) {	//in-place
		T _x  = a.y*b.z-a.z*b.y;
		T _y  = a.z*b.x-a.x*b.z;
			z = a.x*b.y-a.y*b.x;
			x = _x; y = _y;
		return *this;
	}
	
	T volume() const { return x * y * z; }
};

typedef _vec<3,char> vec3c;
typedef _vec<3,unsigned char> vec3uc;
typedef _vec<3,short> vec3s;
typedef _vec<3,unsigned short> vec3us;
typedef _vec<3,int> vec3i;
typedef _vec<3,unsigned int> vec3ui;
typedef _vec<3,float> vec3f;
typedef _vec<3,double> vec3d;

#endif
