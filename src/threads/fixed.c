#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>


/* fixed point representation */

//Convert n to fixed point: 	n * f
struct real integer_into_fixed_point(int n)
{
    return ((n) * fraction);
}

//Convert x to integer (rounding to nearest):
int fixed_point_into_nearest_integer(int x)
{
   if(x >= 0)
    return (((x) + (fraction / 2)) / fraction );
   else
    return (((x) - (fraction / 2)) / fraction );
}

//Convert x to integer (rounding toward zero)
int fixed_point_to_integer(int x)
{
    return ((x) / fraction);
}

struct real multiplication_of_two_fixed_point(int x,int y)
//multiply two fixed point
{
    return (((int64_t)x) * y / fraction);
}

// multiply of fixed point with integer
struct real multiplication_of_fixed_point_with_integer(int x,int n)
{
    return x * n;
}
//division two fixed point
struct real division_of_fixed_point(int x,int y)
{
    return ((((int64_t)(x)) * fraction) / y);
}

// divide of fixed point with integer
struct real division_of_fixed_point_with_integer(int x,int n)
{
    return x * n;
}
//Add x and y: 	x + y
struct real add_two_fixed_point(int x, int y){
  return x + y;
}

// add fixed point to integer_into_fixed_point
struct real add_fixed_point_to_integer(int x, int n){
  return x + n * fraction;
}

// subtract fixed point to integer_into_fixed_point
struct real subtract_fixed_point_to_integer(int x, int n){
  return x - n * fraction;
}

//subtract x and y: 	x + y
struct real subtract_two_fixed_point(int x, int y){
  return x - y;
}
