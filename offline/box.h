#pragma once

#include "vec.h"
#include "macros.h"

template <int DIM, typename T>
class _box;

template <typename T> 
class _box<3,T> {
	typedef _vec<3,T> bvec;
public:

	bvec min, max;
	
	static int dim() { return 3; }

	_box() {}
	_box(const bvec &v) : min(v), max(v) {}
	_box(const bvec &_min, const bvec &_max) : min(_min), max(_max) {}
	_box(const T& a, const T& b, const T& c, const T& d, const T& e, const T& f) : min(a,b,c), max(d,e,f) {}

	_box &set(const T& a, const T& b, const T& c, const T& d, const T& e, const T& f) {
		min.x = a; min.y = b; min.z = c;
		max.x = d; max.y = e; max.z = f;
		return *this;
	}
	_box &set(const bvec &v) { min = v; max = v; return *this; }
	_box &set(const bvec &_min, const bvec &_max) { min = _min; max = _max; return *this; }

	void stretch(const bvec &s_min, const bvec &s_max) {
		//this used to be a for loop.  but from my experiences, for-loops are unraveled slower than template metaprograms are. lesson: write it all out
		if (s_min.x < min.x) min.x = s_min.x;
		if (s_max.x > max.x) max.x = s_max.x;
		if (s_min.y < min.y) min.y = s_min.y;
		if (s_max.y > max.y) max.y = s_max.y;
		if (s_min.z < min.z) min.z = s_min.z;
		if (s_max.z > max.z) max.z = s_max.z;
	}
	void stretch(const _box &b) { stretch(b.min, b.max); }
	void stretch(const bvec &v) { stretch(v,v); }

	void shrink(const bvec &s_min, const bvec &s_max) {
		if (s_min.x > min.x) min.x = s_min.x;
		if (s_max.x < max.x) max.x = s_max.x;
		if (s_min.y > min.y) min.y = s_min.y;
		if (s_max.y < max.y) max.y = s_max.y;
		if (s_min.z > min.z) min.z = s_min.z;
		if (s_max.z < max.z) max.z = s_max.z;
	}
	void shrink(const _box &b) { shrink(b.min, b.max); }
	void shrink(const bvec &v) { shrink(v,v); }

	//feed it the outer box min,max & inner box min,max vectors
	bool contains(const bvec &_min, const bvec &_max) const { return min < _min && max > _max; }
	bool contains(const _box &b) const { return contains(b.min,b.max); }
	bool contains(const bvec &v) const { return contains(v,v); }
	//the E suffix means we're also testing equivalence.  especially useful with integer instances of vectors
	bool containsE(const bvec &_min, const bvec &_max) const { return min <= _min && max >= _max; }
	bool containsE(const _box &b) const { return containsE(b.min,b.max); }
	bool containsE(const bvec &v) const { return containsE(v,v); }

//	bool touches(const bvec &_min, const bvec &_max) const	{ return min < _max && max > _min; }
//	bool touches(const _box &b) const						{ return min < b.min && max > b.max; }
//	bool touchesE(const bvec &_min, const bvec &_max) const { return min <= _max && max >= _min; }
//	bool touchesE(const _box &b) const						{ return min <= b.min && max >= b.max; }
	bool touches(const bvec &_min, const bvec &_max) const {
		return	(min.x < _max.x && max.x > _min.x) &&
				(min.y < _max.y && max.y > _min.y) &&
				(min.z < _max.z && max.z > _min.z);
	}
	bool touches(const _box &b) const { return touches(b.min, b.max); }
	bool touchesE(const bvec &_min, const bvec &_max) const {
		return	(min.x <= _max.x && max.x >= _min.x) &&
				(min.y <= _max.y && max.y >= _min.y) &&
				(min.z <= _max.z && max.z >= _min.z);
	}
	bool touchesE(const _box &b) const { return touchesE(b.min, b.max); }

	bvec &clampInPlace(bvec &v) const {
		for (int i = 0; i < dim(); i++) {
			if (v[i] < min[i]) v[i] = min[i];
			if (v[i] > max[i]) v[i] = max[i];
		}
		return v;
	}

	bvec clamp(bvec v) const {
		for (int i = 0; i < dim(); i++) {
			if (v[i] < min[i]) v[i] = min[i];
			if (v[i] > max[i]) v[i] = max[i];
		}
		return v;
	}

	_box &clampInPlace(_box &b) const {
		clampInPlace(b.min);
		clampInPlace(b.max);
		return b;
	}
	
	_box clamp(_box b) const {
		return _box(clamp(b.min), clamp(b.max));
	}
	
	void calcVtxs(bvec vtxs[1<<3]) const {
		for (int i = 0; i < 1<<3; i++) {	//which vertex?
			for (int j = 0; j < 3; j++) {	//what element of the vertex?
				vtxs[i][j] = (i & (bitflag(j))) ? max[j] : min[j];
			}
		}
	}
	
	template<typename U> operator _box<2,U>() { return _box<2,U>( (U)min.x, (U)min.y, (U)max.x, (U)max.y ); }

	template<typename U> operator _box<3,U>() { return _box<3,U>((_vec<3,U>)this->min, (_vec<3,U>)this->max); }

	template<typename U> _box scale(const _vec<3,U> &b) { return _box<3,T>( vec3ScaleElem(min, b), vec3ScaleElem(max, b) ); }

	ADD_VECTOR_INDEXING(bvec&, 2, &min)

	bvec size() const { return max - min; }
	bvec center() const { return (max + min) * 0.5; }
};

typedef _box<3,int> box3i;
typedef _box<3,float> box3f;

//box/scalar operators:
template<int DIM, typename T, typename U> 
inline _box<DIM,T> &operator/=(_box<DIM,T> &a, const U &b) { a.min /= b; a.max /= b; return a; }

//box/vector operations:

template<int DIM, typename T, typename U>
inline _box<DIM,T> operator+(const _box<DIM,T> &a, const U &b) {
	return _box<DIM,T>(
		(_vec<DIM,T>)(a.min + b),
		(_vec<DIM,T>)(a.max + b));
}

template<int DIM, typename T, typename U>
inline _box<DIM,T> operator-(const _box<DIM,T> &a, const U &b) {
	return _box<DIM,T>(
		(_vec<DIM,T>)(a.min - b),
		(_vec<DIM,T>)(a.max - b));
}

template<int DIM, typename T, typename U>
inline _box<DIM,T> operator*(const _box<DIM,T> &a, const U &b) {
	return _box<DIM,T>(
		(_vec<DIM,T>)(a.min * b),
		(_vec<DIM,T>)(a.max * b));
}


template<int DIM, typename T, typename U>
inline _box<DIM,T> &operator+=(_box<DIM,T> &a, const _vec<DIM,U> &b) { a.min += b; a.max += b; return a; }

template<int DIM, typename T, typename U>
inline _box<DIM,T> &operator-=(_box<DIM,T> &a, const _vec<DIM,U> &b) { a.min -= b; a.max -= b; return a; }

template<int DIM, typename T>
inline _box<DIM,T> &operator++(_box<DIM,T> &a) { a.min++; a.max++; return a; }

template<int DIM, typename T>
inline _box<DIM,T> &operator--(_box<DIM,T> &a) { a.min--; a.max--; return a; }

template<int DIM, typename T, typename U>
inline _box<DIM,T> &operator*=(_box<DIM,T> &a, const U &b) { a.min *= b; a.max *= b; return a; }

//box/box operations:
template<int DIM, typename T, typename U>
inline _box<DIM,T> &operator+=(_box<DIM,T> &a, const _box<DIM,U> &b) { a.min += b.min; a.max += b.max; return a; }

template<int DIM, typename T, typename U>
inline _box<DIM,T> &operator-=(_box<DIM,T> &a, const _box<DIM,U> &b) { a.min -= b.min; a.max -= b.max; return a; }

//boolean operations
template<int DIM, typename T, typename U>
inline bool operator==(_box<DIM,T> &a, const _box<DIM,U> &b) { return a.min == b.min && a.max == b.max; }

template<int DIM, typename T, typename U>
inline bool operator!=(_box<DIM,T> &a, const _box<DIM,U> &b) { return !operator==(a,b); }

//writing out
template<int DIM, typename T> inline std::ostream &operator<<(std::ostream &o, const _box<DIM,T> &b) {
	return o << "min:" << b.min << " max:" << b.max;
}
