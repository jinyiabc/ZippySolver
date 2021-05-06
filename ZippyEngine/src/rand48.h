#ifndef _RAND48_H_
#define _RAND48_H_

struct drand48_data
  {
    unsigned short int __x[3];        /* Current state.  */
    unsigned short int __old_x[3]; /* Old state.  */
    unsigned short int __c;        /* Additive const. in congruential formula.  */
    unsigned short int __init;        /* Flag for initializing.  */
    unsigned long long int __a;        /* Factor in congruential formula.  */
  };

/* Seed random number generator.  */
extern int srand48_r (
	 long int seedval,
	 struct drand48_data *buffer);

extern int __drand48_iterate (unsigned short int xsubi[3], struct drand48_data *buffer);

extern int erand48_r (
     unsigned short int xsubi[3],
     struct drand48_data *buffer,
     double *result);

extern int drand48_r (struct drand48_data *buffer, double *result);

#endif