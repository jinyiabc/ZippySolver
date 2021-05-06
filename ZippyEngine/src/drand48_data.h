
#ifndef _DRAND48_DATA_H_
#define _DRAND48_DATA_H_


 struct drand48_data{
    unsigned short int __x[3];        /* Current state.  */
    unsigned short int __old_x[3]; /* Old state.  */
    unsigned short int __c;        /* Additive const. in congruential formula.  */
    unsigned short int __init;        /* Flag for initializing.  */
    unsigned long long int __a;        /* Factor in congruential formula.  */
  } ;

#endif