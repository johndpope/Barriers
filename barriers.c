/* Last changed Time-stamp: <2002-01-15 21:11:46 studla> */
/* barriers.c */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <limits.h>
#include "ringlist.h"
#include "stapel.h"
#include "utils.h"
#include "hash_util.h"
#include "barrier_types.h"
#include "treeplot.h"
#include "simple_set.h"

/* Tons of static arrays in this one! */
static char UNUSED rcsid[] = "$Id: barriers.c,v 1.13 2002/01/16 20:39:08 studla Exp $";

static char *form;         /* array for configuration */ 
static loc_min *lmin;      /* array for local minima */

/* "global" variables */

static int n_lmin;
static unsigned int max_lmin;
static int n_saddle;
static double minh;
static double energy;
    /* energy of last read structure (for check_neighbors) */
static double mfe;           /* used for scaling Z */

static void (*move_it)(char *);
static void (*free_move_it)(void) = NULL;
static char *(*pack_my_structure)(const char *) ;
static char *(*unpack_my_structure)(const char *) ;

static double kT= -1;
extern unsigned long collisions;

/* global switches */  /* defaults changed */
static int print_saddles = 1;
static int bsize = 1;
static int shut_up = 0;
static int verbose = 0;
static int max_print = 0;
static int IS_RNA = 0;

/* private functions */
static void walk_limb (hash_entry *hp, int LM, int inc, const char *tag);
static void backtrack_path_rec (int l1, int l2, const char *tag);
static int *make_sorted_index(int *truemin);
static void Sorry(char *GRAPH);
static int  read_data(FILE *FP, double *energy,char *strucb, int len);

static void merge_components(int c1, int c2);
static int comp_comps(const void *A, const void *B);

/* public functiones */
int      *make_truemin(loc_min *Lmin);
loc_min  *barriers(barrier_options opt);

static int  compare(const void *a, const void *b);
void check_neighbors(void);
static void merge_basins(void);
void print_results(loc_min *L, int *tm);
void ps_tree(loc_min *Lmin, int *truemin);

struct comp {
  Set *basins; /* set of basins connected by these saddles */
  char *saddle; /* one representative (first found) */
  int size;
};

static int *truecomp;
static struct comp *comp;
static int max_comp=1024, n_comp;     
/* ----------------------------------------------------------- */

void set_barrier_options(barrier_options opt) {
  print_saddles = opt.print_saddles;
  bsize = opt.bsize;
  shut_up = opt.want_quiet;
  max_print = opt.max_print;
  minh = opt.minh;
  verbose = opt.want_verbose;
  switch(opt.GRAPH[0]) {
  case 'R' :    /* RNA secondary Structures */
    if (strncmp(opt.GRAPH, "RNA", 3)==0) {
      int nolp=0, shift=1;
      IS_RNA=1;
      if (opt.kT<=-300) opt.kT=37;
      kT = 0.00198717*(273.15+opt.kT);   /* kT at 37C in kcal/mol */
      move_it = RNA_move_it;
      free_move_it = RNA_free_rl;
      pack_my_structure = pack_structure;
      unpack_my_structure = unpack_structure;
      if (strstr(opt.GRAPH,   "noLP")) nolp=1;
      if (strstr(opt.MOVESET, "noShift")) shift=0;
      RNA_init(opt.seq, shift, nolp);
      if (verbose) 
	fprintf(stderr, "Graph is RNA with noLP=%d, Shift=%d\n", nolp, shift);
    } else Sorry(opt.GRAPH);
    break;
  case 'Q' :    /* Haming graphs */
    if (strcmp(opt.GRAPH,"Q2")==0) {   /* binary +- alphabet */
      if(strcmp(opt.MOVESET,"c")==0) {
	move_it = SPIN_complement_move_it;
	if (verbose)
	  fprintf(stderr, "Graph is Q2 with complementation moves\n");
      }
      else {  
	move_it = SPIN_move_it;
	if (verbose) fprintf(stderr, "Graph is Q2\n");
      }
      pack_my_structure = pack_spin;
      unpack_my_structure = unpack_spin;
    }
    else {
      int alphabetsize=0;
      int numconv, i;
      char *ALPHA;
      ALPHA = (char *) space(sizeof(opt.GRAPH));
      numconv = sscanf(opt.GRAPH,"Q%d,%s",&alphabetsize,ALPHA);
      switch(numconv) {
      case 2 :
	if((int)strlen(ALPHA)!=alphabetsize) Sorry(opt.GRAPH);
	break;
      case 1 :
	if((alphabetsize<=0)||(alphabetsize>26)) Sorry(opt.GRAPH);
	free(ALPHA);
	ALPHA = (char *) space(sizeof(char)*(alphabetsize+1));
	for(i=0;i<alphabetsize;i++) ALPHA[i] = (char) 65+i;
	break;
      default:
	Sorry(opt.GRAPH);
	break;
      }
      String_set_alpha(ALPHA);
      move_it = String_move_it;
      pack_my_structure = strdup;
      unpack_my_structure = strdup;
      if(verbose)
	fprintf(stderr, "Graph is Q%d with Alphabet '%s'\n",
		alphabetsize,ALPHA);
    } 
    break;
  case 'P' :    /* Permutations */
    switch(*opt.MOVESET) {
    case 'R' :
      move_it = Reversal_move_it; break;
    case 'C' :
      move_it = CTranspos_move_it; break;
    case 'T':
    default:
      
      move_it = Transpos_move_it;
    }
    pack_my_structure = strdup;
    unpack_my_structure = strdup;
    if (verbose) 
      fprintf(stderr, "Graph is Permutations with moveset %c\n",
	      *opt.MOVESET ? *opt.MOVESET : 'T');
    break;
  case 'T' :    /* Phylogenetic Trees */
    move_it = NNI_move_it;
    pack_my_structure = strdup;
    unpack_my_structure = strdup;
    if (verbose) 
      fprintf(stderr, "Graph is Trees with NNI moves\n");
    break;   
  case 'X' : /* Johnson graph J(n,n/2) = balanced +/- with exchange moves */
    move_it = EXCH_move_it;
    pack_my_structure = pack_spin;
    unpack_my_structure = unpack_spin;    
    break;
  default :
    Sorry(opt.GRAPH);
  }
  if (kT<0) {
    if (opt.kT<=-300) kT=1;
    else kT=opt.kT;
  }
}

static void Sorry(char *GRAPH) {
  fprintf(stderr,"Graph \"%s\" is not implemented\n",GRAPH);
  exit(-2);
}

static FILE *mergefile=NULL;
static int read=0;
loc_min *barriers(barrier_options opt) {
  int length;
  double new_en=0;

  set_barrier_options(opt);
  length = (int) strlen(opt.seq);
  max_lmin = 1023;
  lmin = (loc_min *) space((max_lmin + 1) * sizeof(loc_min));
  n_lmin = 0;
  
  form = (char *) space((length+1)*sizeof(char));
  comp = (struct comp *) space((max_comp+1) * sizeof(struct comp));
  truecomp = (int *) space((max_comp+1) * sizeof(int));
  
  ini_stapel(length);
  if (opt.ssize) {
    mergefile = fopen("saddles.txt", "w");
    if (!mergefile) fprintf(stderr, "can't open saddle file\n");
  }
  
  while (read_data(opt.INFILE, &new_en,form,length)) {
    if (read==0) mfe=energy=new_en;
    if (new_en<energy) 
      nrerror("unsorted list!\n");
    if (new_en>energy) {
      merge_basins();
      /* fprintf(stderr, "%d %d\n", read, lmin[1].my_pool); */
      n_comp=0;
    }
    energy = new_en;
    read++;   
    move_it(form);       /* generate all neighbor of configuration */
    check_neighbors();   /* flood the energy landscape */
    reset_stapel();
    if (n_saddle+1 == max_print)
      break;  /* we've found all we want to know */
  }
  merge_basins();
  if (mergefile) fclose(mergefile);
  if(!shut_up) fprintf(stderr,
		       "read %d structures, to find %d saddles\n",
		       read, n_saddle);
  
  if (max_print == 0 || max_print > n_lmin)
    max_print = n_lmin;
  
  lmin[0].fathers_pool = n_lmin;   /* store size here; pfs 03 2001 */
  lmin[0].E_saddle = energy + 0.001;
  lmin[0].energy = lmin[1].energy;

  if (free_move_it) 
    free_move_it();
  free_stapel();
  free(form);
  fflush(stdout);
  if(!shut_up) fprintf(stderr, "%lu hash table collisions\n", collisions);
  free(truecomp);
  free(comp);
  return lmin;
}

int *make_truemin(loc_min *Lmin) {
  int *truemin, nlmin, i,ii;
  nlmin = Lmin[0].fathers_pool;
  truemin = (int *) space((nlmin+1)*sizeof(int));
  /* truemin[0] = nlmin; */

  for (ii=i=1; (i<=max_print)&&(ii<=n_lmin); ii++) {
    if (!lmin[ii].father) lmin[ii].E_saddle = energy + 0.000001;
    if (lmin[ii].E_saddle - lmin[ii].energy >= minh) 
      truemin[ii]=i++;
  }
  truemin[0] = i-1;
  return truemin;
}



/*=============================================================*/
static int read_data(FILE *FP, double *energy,char *strucb, int len) {

  static char struc[501];
  int r;

  r = fscanf(FP, "%500s %lf", struc, energy);
  if (r==EOF) return 0;
  if (r!=2) {
    fprintf(stderr, "Error in input file\n");
    exit(123);
  }

  if(strlen(struc) != len) {
    fprintf(stderr,"read_data():\n%s\n unequal length !!\n", struc);
    exit (1);
  }
  strcpy(strucb, struc);

  return (1);
}

/*=====================================*/
static int compare(const void *a, const void *b) {
  int A, B;
  A = ((basinT *)a)->basin; B = ((basinT *)b)->basin;
  return (A - B);
}


/*======================*/
void check_neighbors(void)
{
  char *p, *pp, *pform;
  int basin, obasin=-1;
  hash_entry *hp, h, *down=NULL;
  Set *basins; basinT b;
    
  float minenergia;         /* for Gradient Basins */
  int   gradmin=0;          /* for Gradient Basins */
  int is_min=1;
  int ccomp=0;              /* which connected component */
  basins = new_set(10);
  minenergia = 100000000.0; /* for Gradient Basins */
  
  /* foreach neighbor structure of configuration "Structure" */
  while ((p = pop())) {
    pp = pack_my_structure(p); 
    h.structure = pp;
    
    /* check whether we've seen the structure before */
    if ((hp = lookup_hash(&h))) {
      /* because we've seen this structure before, it already */
      /* belongs to the basin of attraction of a local minimum */
      basin = hp->basin;
      if ( hp->energy < energy) is_min=0;
      if ( hp->energy < minenergia ) { /* for Gradient Basins */
	minenergia = hp->energy;
	gradmin = hp->GradientBasin;
	down = hp;
      }
      if ( hp->energy == minenergia )
	if (down->basin > basin) {
	  gradmin = hp->GradientBasin;
	  down = hp;
	}
	
      if ( fabs(hp->energy - energy)<=1e-6*fabs(energy)) {
	int tc; tc = hp->ccomp;
	while (tc != truecomp[tc]) tc = truecomp[tc];
	if (ccomp==0)
	  ccomp = tc;
	else {
	  while (ccomp != truecomp[ccomp]) ccomp = truecomp[ccomp];
	  if (ccomp != tc) merge_components(tc, ccomp);
	  ccomp = truecomp[ccomp];
	}
      }
      /* the basin of attraction of this local minimum may have been */
      /* merged with the basin of attraction of an energetically */
      /* "deeper" local minimum in a previous step */
      /* go and find this "deeper" local minimum! */
      while (lmin[basin].father) basin=lmin[basin].father;
      
      /* put the "deepest" local minimum into the basins-list */
      if (basin != obasin) {
	b.hp = hp; b.basin = basin;
	set_add(basins, &b);
      }
      obasin = basin;
    }
    free(pp);
  }
  
  pform = pack_my_structure(form);
  
  if (ccomp==0) {
    /* new compnent */
    Set *set; 
    set = new_set(10);
    if (++n_comp>max_comp) {
      max_comp *= 2; 
      comp = (struct comp*) xrealloc(comp, (max_comp+1)*sizeof(struct comp));
      truecomp = (int*) xrealloc(truecomp, (max_comp+1)*sizeof(int));
    }
    comp[n_comp].basins = set;
    comp[n_comp].saddle = pform;
    comp[n_comp].size = 0;
    truecomp[n_comp] = ccomp = n_comp;
  }
  
  if (is_min) {
    basinT b;
    /* Structure is a "new" local minimum */
    gradmin = ++n_lmin;        /* for Gradient Basins */
    down = NULL;
    /* need to allocate more space for the lmin-list */
    if (n_lmin > max_lmin) {
      fprintf(stderr, "increasing lmin array to %d\n",max_lmin*2);
      lmin = (loc_min *) xrealloc(lmin, (max_lmin*2+1)*sizeof(loc_min));
      memset(lmin + max_lmin +1, 0, max_lmin);
      max_lmin *= 2;
    }
    
    /* store configuration "Structure" in lmin-list */
    lmin[n_lmin].father = 0;
    lmin[n_lmin].structure = pform;
    lmin[n_lmin].energy = energy;
    lmin[n_lmin].my_GradPool = 0;
    lmin[n_lmin].my_pool = 1;
    lmin[n_lmin].Z = exp((mfe-energy)/kT);
    lmin[n_lmin].Zg = 0;

    b.basin = n_lmin; b.hp=NULL;
    set_add(basins, &b);
  }
  else comp[ccomp].size++;
  set_merge(comp[ccomp].basins, basins);

  { 
    int i_lmin;
    i_lmin = (is_min) ? n_lmin : basins->data[0].basin;
    set_kill(basins);
    /* store configuration "Structure" in hash table */
    hp = (hash_entry *) space(sizeof(hash_entry));
    hp->structure = pform;
    hp->energy = energy;
    hp->basin = i_lmin;
    hp->GradientBasin = gradmin;    /* for Gradient Basins */
    hp->down = down;
    hp->ccomp = ccomp;
    hp->n = read;
    lmin[gradmin].my_GradPool++;
    lmin[gradmin].Zg += exp(-(energy-mfe)/kT);
    if (write_hash(hp))
      nrerror("duplicate structure");
  }
}

static void merge_basins() {
  int c, i, t;
  for (i=t=1; i<=n_comp; i++) {
    if (truecomp[i]==i) 
      comp[t++]=comp[i];
    else set_kill(comp[i].basins);
  }
  n_comp = t-1;
  qsort(comp+1, n_comp, sizeof(struct comp), comp_comps);
  for (c=1; c<=n_comp; c++) { /* foreach connected component */
    /* merge all lmins connected by this component */
    static int false_lmin=0;
    int i, father, pool=0;
    double Z=0;
    basinT *basins;

    if (mergefile && (comp[c].basins->num_elem>1)) {
      const char format[2][16] = {"%13.5f %4d %s", "%6.2f %4d %s"};
      char *saddle;
      saddle = unpack_my_structure(comp[c].saddle);
      fprintf(mergefile, format[IS_RNA], energy, comp[c].size, saddle);
      free(saddle);
      for (i=0; i < comp[c].basins->num_elem; i++)
	fprintf(mergefile, " %2d", comp[c].basins->data[i].basin);
      fprintf(mergefile, "\n");
    }

    basins = comp[c].basins->data;
    father = basins[0].basin;
    while (lmin[father].father) father=lmin[father].father;

    for (i = 1; i < comp[c].basins->num_elem; i++) {
      int ii, l, r;
      ii = basins[i].basin;
      while (lmin[ii].father) ii=lmin[ii].father;
      if (ii!=father) {
	if (ii<father) {int tmp; tmp=ii; ii=father; father=tmp; l=0; r=i;}
	else {l=i; r=0;}
	/* going to merge ii with father  */
	if ((!max_print) || (ii<=max_print+false_lmin)) { 
	  /* found the saddle for a basin we're gonna print */
	  if (energy-lmin[ii].energy>=minh) n_saddle++;
	  else false_lmin++;
	}
      
	lmin[ii].father = father;
	lmin[ii].saddle = comp[c].saddle;
	lmin[ii].E_saddle = energy;
	lmin[ii].left =  basins[l].hp;
	lmin[ii].right = basins[r].hp;
	if (bsize) {
	  lmin[ii].fathers_pool = lmin[father].my_pool;
	  pool += lmin[ii].my_pool;
	  Z += lmin[ii].Z; 
	}
      }
    }
    if (bsize) {
      lmin[father].my_pool += pool + comp[c].size;
      lmin[father].Z += Z + comp[c].size * exp((mfe-energy)/kT);
    }
    set_kill(comp[c].basins);
  }
}

/*====================*/
void print_results(loc_min *Lmin, int *truemin)
{
  int i,ii,j;
  char *struc;
  char *format;
  
  if (IS_RNA)
    format = "%4d %s %6.2f %4d %6.2f";
  else
    format = "%4d %s %13.5f %4d %13.5f";
      
  n_lmin = Lmin[0].fathers_pool;

  for (i = 1; i <= n_lmin; i++) {
    int f;
    if ((ii = truemin[i])==0) continue;

    struc = unpack_my_structure(Lmin[i].structure);
    f = Lmin[i].father; if (f>0) f = truemin[f];
    printf(format, ii, struc, Lmin[i].energy, f,
	   Lmin[i].E_saddle - Lmin[i].energy);
    free(struc);
    
    if (print_saddles) { 
      if (Lmin[i].saddle)  {
	struc = unpack_my_structure(Lmin[i].saddle);
	printf(" %s", struc);
	free(struc);
      }
      else {
	printf(" ");
	for (j=0;j<strlen(struc);j++) { printf("~"); }
      }
      
    }
    if (bsize) 
      printf (" %12ld %8ld %7.3f %8ld %7.3f",
	      Lmin[i].my_pool, Lmin[i].fathers_pool, mfe -kT*log(lmin[i].Z),
	      Lmin[i].my_GradPool, mfe -kT*log(lmin[i].Zg));
    printf("\n");
  }
}

void ps_tree(loc_min *Lmin, int *truemin)
{
  nodeT *nodes;
  int i,ii;
  int nlmin;
  
  nlmin = Lmin[0].fathers_pool;
  
  if (max_print>truemin[0]) max_print=truemin[0];

  nodes = (nodeT *) space(sizeof(nodeT)*(max_print+1));
  for (i=0,ii=1; i<max_print && ii<=nlmin; ii++)
    {
      register int s1, f;
      double E_saddle;
      if ((s1=truemin[ii])==0) continue;
      if (s1>max_print) 
	nrerror("inconsistency in ps_tree, aborting");
      E_saddle = Lmin[ii].E_saddle;
      f = Lmin[ii].father; 
      if (f==0) {
	E_saddle = Lmin[0].E_saddle; /* maximum energy */
	f=1;                         /* join with mfe  */
      }
      nodes[s1-1].father = truemin[f]-1;
      nodes[s1-1].height = Lmin[ii].energy;
      nodes[s1-1].saddle_height = E_saddle;
      i++;
    }
  PS_tree_plot(nodes, max_print, "tree.ps");
  free(nodes);
}

/*=========================*/
char * get_taxon_label(int i)
{
  char *label;

  if (i == -1)
    return (NULL);
  label = (char *) space(20);
  sprintf(label, "%d", i);

  return (label);
}

/*=========================================*/
static int indx_comp(const void *i1, const void *i2)
{
  int i,j;

  i = *((int *)i1);
  j = *((int *)i2);
  if (lmin[i].E_saddle == lmin[j].E_saddle)
    return (i-j);
  return (lmin[i].E_saddle < lmin[j].E_saddle) ? (-1) : (1);
}

/*===============================*/
static int *make_sorted_index(int *truemin)
{
  int i, ii, *index;

  index = (int *) space((max_print+1)*sizeof(int));

  /* include only up to max_print local minima */
  for (i = 0, ii=1; i < max_print; ii++)
    if (truemin[ii])
      index[i++] = ii;
  
  /* sort local minima by saddle-point-heights */
  /* starting with the 1st excited state */
  qsort(index+1, max_print-1, sizeof(int), indx_comp);

  return (index);
}

static path_entry *path;
static int np, max_path=128;

static int path_cmp(const void *a, const void *b) {
  path_entry *A, *B; int d;
  A = (path_entry *) a;
  B = (path_entry *) b;
  if ((d=strcmp(A->key, B->key))==0) return (A->num-B->num);
  else return (d);
}

/*=======*/
path_entry *backtrack_path(int l1, int l2, loc_min *LM, int *truemin) {
  int n_lmin, i, ll1=0, ll2=0;
  char *tag;
  lmin = LM;
  n_lmin = lmin[0].fathers_pool;
  for (i=1; i<=n_lmin; i++) {
    if (truemin[i]==l1) ll1=i;
    if (truemin[i]==l2) ll2=i;
  }
  path = (path_entry *) space(max_path*sizeof(path_entry));
  tag = (char *) space(16);
  backtrack_path_rec(ll1, ll2, tag);  path[np].hp = NULL;
  qsort(path, np, sizeof(path_entry), path_cmp);

  return(path);
}

static void backtrack_path_rec (int l1, int l2, const char *tag)
{
  hash_entry h;
  int dir=1, left=1, swap=0, child, father, maxsaddle;
  /* if left==1 left points toward l2 else toward l1 */
  if (l1>l2) {
    dir = -1;
    {int t; t=l1; l1=l2; l2=t;}
  }
  child  = l2; father = l1;
  maxsaddle = child;
  /* find saddle connecting l1 and l2 */
  while (lmin[child].father != father) { 
    int tmp;
    if (lmin[child].father == 0){
      fprintf(stderr, "ERROR in backtrack_path(): ");
      fprintf(stderr,"No saddle between lmin %d and lmin %d\n", l2, l1);
      exit (1);
    }
    child = lmin[child].father;
    if (child<father) {tmp = child; child = father; father = tmp; swap= !swap;}
    if (lmin[child].E_saddle > lmin[maxsaddle].E_saddle) {
      maxsaddle = child;
      if (swap) left= -left;
    }
  }
  h.structure = lmin[maxsaddle].saddle;
  path[np].hp = lookup_hash(&h); 
  strcpy(path[np].key,tag); strcat(path[np].key, "M");
  np++;

  if (left>0) {
    /* branch to l2 */
    if (lmin[maxsaddle].left) /* else saddle==l2 and we're done */
      walk_limb (lmin[maxsaddle].left, l2, -dir, tag);
    /* branch to l1 (to father) */
    walk_limb (lmin[maxsaddle].right, l1, dir, tag);
  } else {
    walk_limb (lmin[maxsaddle].right, l2, -dir, tag);
    if (lmin[maxsaddle].left)
      walk_limb (lmin[maxsaddle].left, l1, dir, tag);
  }    
}

/*=======================================================================*/
static void walk_limb (hash_entry *hp, int LM, int inc, const char *tag)
{
  char *tmp; int num=0;
  hash_entry *htmp;

  tmp = (char *) space(strlen(tag)+4);;
  strcpy(tmp, tag);
  strcat(tmp, (inc>0) ? "R" : "LZ");
  /* walk down until u hit a local minimum */
  for (htmp = hp; htmp->down != NULL; htmp = htmp->down, num += inc, np++) {
    if (np+2>=max_path) {
      max_path *= 2;
      path = (path_entry *) xrealloc(path, max_path*sizeof(path_entry));
    }
    path[np].hp = htmp;
    strcpy(path[np].key, tmp);
    path[np].num = num; 
  }

  /* store local minimum (but only once) */
  if (htmp->basin == LM) {
    path[np].hp = htmp;
    strcpy(path[np].key, tmp);
    path[np++].num = num;
  }

  if (inc<0) tmp[strlen(tmp)-1] = '\0';
  /* wrong local minimum start cruising again */
  if (htmp->basin != LM) {
    if (inc == -1)
      backtrack_path_rec (htmp->basin, LM, tmp);
    else
      backtrack_path_rec (LM, htmp->basin, tmp);
  }
}

void print_path(FILE *PATH, path_entry *path, int *tm) {
  int i;
  for (i=0; path[i].hp; i++) {
    char c[6] = {0,0,0,0}, *struc; 
    if (path[i].hp->down==NULL) {
      sprintf(c, "L%04d", tm[path[i].hp->basin]);
    } else 
      if (path[i].key[strlen(path[i].key)-1] == 'M')
	c[0] = 'S';
      else c[0] = 'I';
    struc = unpack_my_structure(path[i].hp->structure);
    fprintf(PATH, "%s (%6.2f) %-5s\n", struc,  path[i].hp->energy, c);
    free(struc);
  }
}

static void merge_components(int c1, int c2) {
  if (comp[c1].size<comp[c2].size) {int cc; cc=c1; c1=c2; c2=cc;}
  comp[c1].size += comp[c2].size;
  truecomp[c2]=c1;
  set_merge(comp[c1].basins, comp[c2].basins);
}

static int comp_comps(const void *A, const void *B) {
  struct comp *a, *b;
  int r, i=0;
  a = (struct comp *)A;
  b = (struct comp *)B;
  for (i=0; i<a->basins->num_elem && i<b->basins->num_elem; i++) {
    r = a->basins->data[i].basin - b->basins->data[i].basin;
    if (r!=0) return r;
  }
  return (i==a->basins->num_elem)? -1:1;
}
