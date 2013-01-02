//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#if !defined(tensor_Vector_i_cc)
#define tensor_Vector_i_cc

namespace LCM {

//
// return dimension
//
template<typename T>
inline
Index
Vector<T>::get_dimension() const
{
  return e.size();
}

//
// set dimension
//
template<typename T>
inline
void
Vector<T>::set_dimension(const Index N)
{
  e.resize(N);
  return;
}

//
// default constructor
//
template<typename T>
inline
Vector<T>::Vector()
{
  return;
}

//
// constructor that initializes to NaNs
//
template<typename T>
inline
Vector<T>::Vector(const Index N)
{

  set_dimension(N);

  switch (N) {

  default:
    for (Index i =0; i < N; ++i) {
      e[i] = not_a_number<T>();
    }
    break;

  case 3:
    e[0] = not_a_number<T>();
    e[1] = not_a_number<T>();
    e[2] = not_a_number<T>();
    break;

  case 2:
    e[0] = not_a_number<T>();
    e[1] = not_a_number<T>();
    break;

  }

  return;
}

//
// R^N create vector from a scalar
// \param s all components are set equal to this value
//
template<typename T>
inline
Vector<T>::Vector(const Index N, T const & s)
{
  set_dimension(N);

  switch (N) {

  default:
    for (Index i =0; i < N; ++i) {
      e[i] = s;
    }
    break;

  case 3:
    e[0] = s;
    e[1] = s;
    e[2] = s;
    break;

  case 2:
    e[0] = s;
    e[1] = s;
    break;

  }

  return;
}

//
// Create vector specifying components
// \param N dimension
// \param s0, s1 are the vector components in the R^2 canonical basis
//
template<typename T>
inline
Vector<T>::Vector(T const & s0, T const & s1)
{
  set_dimension(2);

  e[0] = s0;
  e[1] = s1;

  return;
}

//
// Create vector specifying components
// \param N dimension
// \param s0, s1, s2 are the vector components in the R^3 canonical basis
//
template<typename T>
inline
Vector<T>::Vector(T const & s0, T const & s1, T const & s2)
{
  set_dimension(3);

  e[0] = s0;
  e[1] = s1;
  e[2] = s2;

  return;
}

//
// R^N create vector from array - const version
// \param N dimension
// \param data_ptr
//
template<typename T>
inline
Vector<T>::Vector(const Index N, T const * data_ptr)
{
  assert(data_ptr != NULL);

  set_dimension(N);

  switch (N) {

  default:
    for (Index i = 0; i < N; ++i) {
      e[i] = data_ptr[i];
    }
    break;

  case 3:
    e[0] = data_ptr[0];
    e[1] = data_ptr[1];
    e[2] = data_ptr[2];
    break;

  case 2:
    e[0] = data_ptr[0];
    e[1] = data_ptr[1];
    break;

  }

  return;
}

//
// R^N create vector from array
// \param N dimension
// \param data_ptr
//
template<typename T>
inline
Vector<T>::Vector(const Index N, T * data_ptr)
{
  assert(data_ptr != NULL);

  set_dimension(N);

  switch (N) {

  default:
    for (Index i = 0; i < N; ++i) {
      e[i] = data_ptr[i];
    }
    break;

  case 3:
    e[0] = data_ptr[0];
    e[1] = data_ptr[1];
    e[2] = data_ptr[2];
    break;

  case 2:
    e[0] = data_ptr[0];
    e[1] = data_ptr[1];
    break;

  }

  return;
}

//
// R^N copy constructor
// \param v the values of its components are copied to the new vector
//
template<typename T>
inline
Vector<T>::Vector(Vector<T> const & v)
{
  const Index
  N = v.get_dimension();

  set_dimension(N);

  switch (N) {

  default:
    for (Index i = 0; i < N; ++i) {
      e[i] = v.e[i];
    }
    break;

  case 3:
    e[0] = v.e[0];
    e[1] = v.e[1];
    e[2] = v.e[2];
    break;

  case 2:
    e[0] = v.e[0];
    e[1] = v.e[1];
    break;

  }

  return;
}

//
// R^N simple destructor
//
template<typename T>
inline
Vector<T>::~Vector()
{
  return;
}

//
// R^N indexing for constant vector
// \param i the index
//
template<typename T>
inline
const T &
Vector<T>::operator()(const Index i) const
{
  assert(i < get_dimension());
  return e[i];
}

//
// R^N vector indexing
// \param i the index
//
template<typename T>
inline
T &
Vector<T>::operator()(const Index i)
{
  assert(i < get_dimension());
  return e[i];
}

//
// R^N copy assignment
// \param v the values of its components are copied to this vector
//
template<typename T>
inline
Vector<T> &
Vector<T>::operator=(Vector<T> const & v)
{
  if (this != &v) {

    const Index
    N = v.get_dimension();

    set_dimension(N);

    switch (N) {

    default:
      for (Index i = 0; i < N; ++i) {
        e[i] = v.e[i];
      }
      break;

    case 3:
      e[0] = v.e[0];
      e[1] = v.e[1];
      e[2] = v.e[2];
      break;

    case 2:
      e[0] = v.e[0];
      e[1] = v.e[1];
      break;

    }
  }

  return *this;
}

//
// R^N vector increment
// \param v added to currrent vector
//
template<typename T>
inline
Vector<T> &
Vector<T>::operator+=(Vector<T> const & v)
{
  const Index
  N = get_dimension();

  assert(v.get_dimension() == N);

  switch (N) {

  default:
    for (Index i = 0; i < N; ++i) {
      e[i] += v.e[i];
    }
    break;

  case 3:
    e[0] += v.e[0];
    e[1] += v.e[1];
    e[2] += v.e[2];
    break;

  case 2:
    e[0] += v.e[0];
    e[1] += v.e[1];
    break;

  }

  return *this;
}

//
// R^N vector decrement
// \param v substracted from current vector
//
template<typename T>
inline
Vector<T> &
Vector<T>::operator-=(Vector<T> const & v)
{
  const Index
  N = get_dimension();

  assert(v.get_dimension() == N);

  switch (N) {

  default:
    for (Index i = 0; i < N; ++i) {
      e[i] -= v.e[i];
    }
    break;

  case 3:
    e[0] -= v.e[0];
    e[1] -= v.e[1];
    e[2] -= v.e[2];
    break;

  case 2:
    e[0] -= v.e[0];
    e[1] -= v.e[1];
    break;

  }

  return *this;
}

//
// R^N fill with zeros
//
template<typename T>
inline
void
Vector<T>::clear()
{
  const Index
  N = get_dimension();

  switch (N) {

  default:
    for (Index i = 0; i < N; ++i) {
      e[i] = 0.0;
    }
    break;

  case 3:
    e[0] = 0.0;
    e[1] = 0.0;
    e[2] = 0.0;
    break;

  case 2:
    e[0] = 0.0;
    e[1] = 0.0;
    break;

  }

  return;
}

//
// R^N vector addition
// \param u
// \param v the operands
// \return \f$ u + v \f$
//
template<typename T>
inline
Vector<T>
operator+(Vector<T> const & u, Vector<T> const & v)
{
  const Index
  N = u.get_dimension();

  assert(v.get_dimension() == N);

  Vector<T> s(N);

  switch (N) {

  default:
    for (Index i = 0; i < N; ++i) {
      s(i) = u(i) + v(i);
    }
    break;

  case 3:
    s(0) = u(0) + v(0);
    s(1) = u(1) + v(1);
    s(2) = u(2) + v(2);
    break;

  case 2:
    s(0) = u(0) + v(0);
    s(1) = u(1) + v(1);
    break;

  }

  return s;
}

//
// R^N vector substraction
// \param u
// \param v the operands
// \return \f$ u - v \f$
//
template<typename T>
inline
Vector<T>
operator-(Vector<T> const & u, Vector<T> const & v)
{
  const Index
  N = u.get_dimension();

  assert(v.get_dimension() == N);

  Vector<T> s(N);

  switch (N) {

  default:
    for (Index i = 0; i < N; ++i) {
      s(i) = u(i) - v(i);
    }
    break;

  case 3:
    s(0) = u(0) - v(0);
    s(1) = u(1) - v(1);
    s(2) = u(2) - v(2);
    break;

  case 2:
    s(0) = u(0) - v(0);
    s(1) = u(1) - v(1);
    break;

  }

  return s;
}

//
// R^N vector minus
// \param u
// \return \f$ -u \f$
//
template<typename T>
inline
Vector<T>
operator-(Vector<T> const & u)
{
  const Index
  N = u.get_dimension();

  Vector<T> v(N);

  switch (N) {

  default:
    for (Index i = 0; i < N; ++i) {
      v(i) = - u(i);
    }
    break;

  case 3:
    v(0) = - u(0);
    v(1) = - u(1);
    v(2) = - u(2);
    break;

  case 2:
    v(0) = - u(0);
    v(1) = - u(1);
    break;

  }

  return v;
}

//
// R^N vector dot product
// \param u
// \param v the operands
// \return \f$ u \cdot v \f$
//
template<typename T>
inline
T
operator*(Vector<T> const & u, Vector<T> const & v)
{
  return dot(u, v);
}

//
// R^N vector equality tested by components
// \param u
// \param v the operands
// \return \f$ u \equiv v \f$
//
template<typename T>
inline
bool
operator==(Vector<T> const & u, Vector<T> const & v)
{
  const Index
  N = u.get_dimension();

  assert(v.get_dimension() == N);

  switch (N) {

  default:
    for (Index i = 0; i < N; ++i) {
      if (v(i) != u(i)) {
        return false;
      }
    }
    break;

  case 3:
    return u(0)==v(0) && u(1)==v(1) && u(2)==v(2);
    break;

  case 2:
    return u(0)==v(0) && u(1)==v(1);
    break;

  }

  return true;
}

//
// R^N, vector inequality tested by components
// \param u
// \param v the operands
// \return \f$ u \neq v \f$
//
template<typename T>
inline
bool
operator!=(Vector<T> const & u, Vector<T> const & v)
{
  return !(u==v);
}

//
// R^N scalar vector product
// \param s scalar factor
// \param u vector factor
// \return \f$ s u \f$
//
template<typename T, typename S>
inline
Vector<T>
operator*(S const & s, Vector<T> const & u)
{
  const Index
  N = u.get_dimension();

  Vector<T> v(N);

  switch (N) {

  default:
    for (Index i = 0; i < N; ++i) {
      v(i) = s * u(i);
    }
    break;

  case 3:
    v(0) = s * u(0);
    v(1) = s * u(1);
    v(2) = s * u(2);
    break;

  case 2:
    v(0) = s * u(0);
    v(1) = s * u(1);
    break;

  }

  return v;
}

//
// R^N vector scalar product
// \param u vector factor
// \param s scalar factor
// \return \f$ s u \f$
//
template<typename T, typename S>
inline
Vector<T>
operator*(Vector<T> const & u, S const & s)
{
  return s * u;
}

//
// R^N vector scalar division
// \param u vector
// \param s scalar that divides each component of vector
// \return \f$ u / s \f$
//
template<typename T, typename S>
inline
Vector<T>
operator/(Vector<T> const & u, S const & s)
{
  const Index
  N = u.get_dimension();

  Vector<T> v(N);

  switch (N) {

  default:
    for (Index i = 0; i < N; ++i) {
      v(i) = u(i) / s;
    }
    break;

  case 3:
    v(0) = u(0) / s;
    v(1) = u(1) / s;
    v(2) = u(2) / s;
    break;

  case 2:
    v(0) = u(0) / s;
    v(1) = u(1) / s;
    break;

  }

  return v;
}

//
// R^N vector dot product
// \param u
// \param v operands
// \return \f$ u \cdot v \f$
//
template<typename T>
inline
T
dot(Vector<T> const & u, Vector<T> const & v)
{
  const Index
  N = u.get_dimension();

  assert(v.get_dimension() == N);

  T s = 0.0;

  switch (N) {

  default:
    for (Index i = 0; i < N; ++i) {
      s += u(i) * v(i);
    }
    break;

  case 3:
    s = u(0)*v(0) + u(1)*v(1) + u(2)*v(2);
    break;

  case 2:
    s = u(0)*v(0) + u(1)*v(1);
    break;

  }

  return s;
}

//
// Cross product only valid for R^3.
// R^N with N != 3 will produce an error.
// \param u
// \param v operands
// \return \f$ u \times v \f$
//
template<typename T>
inline
Vector<T>
cross(Vector<T> const & u, Vector<T> const & v)
{
  const Index
  N = u.get_dimension();

  assert(v.get_dimension() == N);

  Vector<T> w(N);

  switch (N) {

  default:
    std::cerr << "ERROR: Cross product undefined for R^" << N << std::endl;
    exit(1);
    break;

  case 3:
    w(0) = u(1)*v(2) - u(2)*v(1);
    w(1) = u(2)*v(0) - u(0)*v(2);
    w(2) = u(0)*v(1) - u(1)*v(0);
    break;

  }

  return w;
}

//
// R^N vector 2-norm
// \return \f$ \sqrt{u \cdot u} \f$
//
template<typename T>
inline
T
norm(Vector<T> const & u)
{
  const Index
  N = u.get_dimension();

  T s = 0.0;

  switch (N) {

  default:
    s = sqrt(dot(u, u));
    break;

  case 3:
    s = sqrt(u(0)*u(0) + u(1)*u(1) + u(2)*u(2));
    break;

  case 2:
    s = sqrt(u(0)*u(0) + u(1)*u(1));
    break;

  }

  return s;
}

//
// R^N vector 2-norm square for fast distance calculations.
// \return \f$ u \cdot u \f$
//
template<typename T>
inline
T
norm_square(Vector<T> const & u)
{
  const Index
  N = u.get_dimension();

  T s = 0.0;

  switch (N) {

  default:
    s = dot(u, u);
    break;

  case 3:
    s = u(0)*u(0) + u(1)*u(1) + u(2)*u(2);
    break;

  case 2:
    s = u(0)*u(0) + u(1)*u(1);
    break;

  }

  return s;
}

//
// R^N vector 1-norm
// \return \f$ \sum_i |u_i| \f$
//
template<typename T>
inline
T
norm_1(Vector<T> const & u)
{
  const Index
  N = u.get_dimension();

  T s = 0.0;

  switch (N) {

  default:
    for (Index i = 0; i < N; ++i) {
      s += std::abs(u(i));
    }
    break;

  case 3:
    s = std::abs(u(0)) + std::abs(u(1)) + std::abs(u(2));
    break;

  case 2:
    s = std::abs(u(0)) + std::abs(u(1));
    break;

  }

  return s;
}

//
// R^N vector infinity-norm
// \return \f$ \max(|u_0|,...|u_i|,...|u_N|) \f$
//
template<typename T>
inline
T
norm_infinity(Vector<T> const & u)
{
  const Index
  N = u.get_dimension();

  T s = 0.0;

  switch (N) {

  default:
    for (Index i = 0; i < N; ++i) {
      s = std::max(s, std::abs(u(i)));
    }
    break;

  case 3:
    s = std::max(std::max(std::abs(u(0)),std::abs(u(1))),std::abs(u(2)));
    break;

  case 2:
    s = std::max(std::abs(u(0)),std::abs(u(1)));
    break;

  }

  return s;
}


} // namespace LCM

#endif // tensor_Vector_i_cc