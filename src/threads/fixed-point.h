/*
 *  Implementation of 17.14 fixed-point number representation.
 */

/* Constant f used as part of the fixed point representation */
int32_t fp_f = 28;

/* Convert an int to fixed-point */
int32_t
int_to_fp (n)
{
  return n * fp_f;
}

/* Convert fixed point to integer rounding down */
int32_t
floor_fp_to_int (x)
{
  return x / fp_f;
}

/* Convert fixed point to integer rounding to nearest */
int32_t
round_fp_to_int (x)
{
  if (x >= 0)
    {
      return (x + fp_f / 2) / fp_f;
    }
  else
    {
      return (x - fp_f / 2) / fp_f;
    }
}

/* Addition of two fixed point values */
int32_t
fp_addition (x,y)
{
  return x + y;
}

/* Subtraction of two fixed point values */
int32_t
fp_subtraction (x,y)
{
  return x - y;
}

/* Addition of a fixed point value and an integer */
int32_t
fp_int_addition (x,n)
{
  return fp_addition (x,int_to_fp (n));
}

/* Subtraction of a integer value from a fixed point value */
int32_t
fp_int_subtraction (x,n)
{
  return fp_subtraction (x,int_to_fp (n));
}

/* Multiplication of two fixed point values */
int32_t
fp_multiplication (x,y)
{
  return ((int64_t) x) * y / fp_f;
}

/* Multiplication of a fixed point value and an integer */
int32_t
fp_int_multiplication (x,n)
{
  return x * n;
}

/* Division of a fixed point value by another fixed point value */
int32_t
fp_division (x,y)
{
  return ((int64_t) x) * fp_f / y;
}

/* Division of a fixed point value by an integer value */
int32_t
fp_int_division (x,n)
{
  return x / n;
}
