/* Last changed Time-stamp: <2001-03-07 17:22:04 stadler> */
/* hash_util.h */

#ifndef _hash_util_h
#define _hash_util_h

extern void * lookup_hash (void *x);
extern int write_hash (void *x);
extern void delete_hash (void *x);
extern void kill_hash();
extern void initialize_hash();

typedef struct _hash_entry {
  char *structure;
  float energy;
  int basin;
  int GradientBasin;  /* for Gradient Basins */
  struct _hash_entry *down;
} hash_entry;

#endif

/* End of file */