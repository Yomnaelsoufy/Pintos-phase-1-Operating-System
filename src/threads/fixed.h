#include <debug.h>
#include <list.h>
#include <stdint.h>



struct real{
   int value;
};

// floating point representation

struct real integer_into_fixed_point(int n);
int fixed_point_into_nearest_integer(int x);

// round to zero

int fixed_point_to_integer(int x);
struct real multiplication_of_two_fixed_point(int x,int y);
struct real multiplication_of_fixed_point_with_integer(int x,int n);
struct real division_of_fixed_point(int x,int y);
struct real division_of_fixed_point_with_integer(int x,int n);
struct real add_two_fixed_point(int x, int y);
struct real add_fixed_point_to_integer(int x, int n);
struct real subtract_fixed_point_to_integer(int x, int n);
struct real subtract_two_fixed_point(int x, int y);
