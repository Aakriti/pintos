#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

static int p = 17, q = 14, f = 0;

void init_f_value();
__inline__ int convert_to_fixed_point(int n);
__inline__ int covert_to_integer(int x);
__inline__ int covert_to_integer_round(int x);
__inline__ int add_fixed_point(int x, int y);
__inline__ int subtract_fixed_point(int x, int y);
__inline__ int add_fixed_and_integer(int x, int n);
__inline__ int sub_fixed_and_integer(int x, int n);
__inline__ int multiply_fixed_point(int x, int y);
__inline__ int multiply_fixed_and_integer(int x, int n);
__inline__ int divide_fixed_point(int x, int y);
__inline__ int divide_fixed_and_integer(int x, int n);

/* function to initialize the value of f */
void init_f_value()
{
  int i = 1;
  f = 2;
  while(i < q)
  {
    f = f*2;
    i++;
  }
}


/* function to convert n to fixed point */
__inline__ int convert_to_fixed_point(int n)
{
  return n * f;
}

/* function to convert x to integer (rounding toward zero) */
__inline__ int covert_to_integer(int x)
{
  return x / f;
}

/* function to convert x to integer (rounding to nearest) */
__inline__ int covert_to_integer_round(int x)
{
  if(x >= 0)
    return (x + f / 2) / f;
  else
    return (x - f / 2) / f;
}

/* function to add two fixed point numbers x and y */
__inline__ int add_fixed_point(int x, int y)
{
  return x + y;
}

/* function to subtract fixed point numbers (y from x) */
__inline__ int subtract_fixed_point(int x, int y)
{
  return x - y;
}

/* function to add a fixed point number x and a integer n */
__inline__ int add_fixed_and_integer(int x, int n)
{
  return x + (n * f);
}

/* function to subtract a interger n from a fixed point number x */
__inline__ int sub_fixed_and_integer(int x, int n)
{
  return x - (n * f);
}

/* function to multiply fixed point number x by y */
__inline__ int multiply_fixed_point(int x, int y)
{
  return ((int64_t) x) * y / f;
}

/* function to multiply fixed point number x by integer y */
__inline__ int multiply_fixed_and_integer(int x, int n)
{
  return x * n;
}

/* function to divide fixed point number x by y */
__inline__ int divide_fixed_point(int x, int y)
{
  return ((int64_t) x) * f / y;
}

/* function to divide fixed point number x by integer y */
__inline__ int divide_fixed_and_integer(int x, int n)
{
  return x / n;
}

#endif /* threads/fixed-point.h */
