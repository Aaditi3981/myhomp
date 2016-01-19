#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <omp.h>
#include <sys/time.h>
#include "homp.h"
#include "stencil2d.h"

/* 2D/3D stencil computation, take a radius sized coefficient matrix, and apply stencil computation to a matrix
 * The stencil could be cross-based, which only uses neighbors from one dimension stride (A[i-1][j][k], or square-based
 * which use neighbors from multiple dimension stride (A[i-2][j-1][k]).
 */

#define DEFAULT_DIMSIZE 256

/* use the macro (SQUARE_STENCIL) from compiler to build two versions of the stencil
 * 1. CROSS-based stencil, default, coefficient is an array of 4*radius+1, [0] is the center value, and then row and column values
 * 2. SQUARE-based stencil, coefficient is a square matrix with one dimension of (2*radius+1)
 */

void print_array(char * title, char * name, REAL * A, long n, long m) {
	printf("%s:\n", title);
	long i, j;
    for (i = 0; i < n; i++) {
        for (j = 0; j < m; j++) {
            printf("%s[%d][%d]:%f\n", name, i, j, A[i * m + j]);
        }
        printf("\n");
    }
    printf("\n");
}

void init_array(long N, REAL *A, REAL lower, REAL upper) {
	long i;

	for (i = 0; i < N; i++) {
		A[i] = (REAL)(lower + ((REAL)rand() / (REAL)RAND_MAX) * (upper - lower));
	}
}

REAL check_accdiff(const REAL *output, const REAL *reference, const long dimx, const long dimy, const int radius, REAL tolerance){
	int ix, iy;
	REAL acc_diff = 0.0;
	int count = 0;
	for (ix = -radius ; ix < dimx + radius ; ix++) {
		for (iy = -radius ; iy < dimy + radius ; iy++) {
			if (ix >= 0 && ix < dimx && iy >= 0 && iy < dimy) {
				// Determine the absolute difference
				REAL difference = fabs(*reference - *output);
				acc_diff += difference;

				REAL error;
				// Determine the relative error
				if (*reference != 0)
					error = difference / *reference;
				else
					error = difference;

				// Check the error is within the tolerance
				//printf("Data at point (%d,%d)\t%f instead of %f\n", ix, iy, *output, *reference);
				if (error > tolerance) {
	//				if (count++<16)	printf("Data error at point (%d,%d)\t%f instead of %f\n", ix, iy, *output, *reference);
				}
			}
			++output;
			++reference;
		}
	}
	return acc_diff;
}

void stencil2d_seq(long n, long m, REAL *u, int radius, REAL *filter, int num_its);
void stencil2d_omp(long n, long m, REAL *u, int radius, REAL *coeff, int num_its);
double stencil2d_omp_mdev(long u_dimX, long u_dimY, REAL *u, int radius, REAL *coeff, int num_its);
double stencil2d_omp_mdev_iterate(long u_dimX, long u_dimY, REAL *u, int radius, REAL *coeff, int num_its);

int dist_dim;
int dist_policy;
int num_runs = 1;

int main(int argc, char * argv[]) {
	long n = DEFAULT_DIMSIZE;
	long m = DEFAULT_DIMSIZE;
	int radius = 3;
	int num_its = 5000;

    fprintf(stderr,"Usage: jacobi [dist_dim(1|2|3)] [dist_policy(1|2|3)] [<n> <m> <radius> <num_its>]\n");
	fprintf(stderr, "\tdist_dim: 1: row dist; 2: column dist; 3: both row/column dist; default 1\n");
	fprintf(stderr, "\tdist_policy: 1: block_block; 2: block_align; 3: auto_align; default 1\n");
    fprintf(stderr, "\tn - grid dimension in x direction, default: %d\n", n);
    fprintf(stderr, "\tm - grid dimension in y direction, default: n if provided or %d\n", m);
    fprintf(stderr, "\tradius - Filter radius, default: %d\n", radius);
    fprintf(stderr, "\tnum_its  - # iterations for iterative solver, default: %d\n", num_its);

	dist_dim = 1;
	dist_policy = 1;
	if (argc >= 2) dist_dim = atoi(argv[1]);
	if (argc >= 3) dist_policy = atoi(argv[2]);
	if (dist_dim != 1 && dist_dim != 2 && dist_dim != 3) {
		fprintf(stderr, "Unknown dist dimensions: %d, now fall to default (1)\n", dist_dim);
		dist_dim = 1;
	}
	if (dist_policy != 1 && dist_policy != 2 && dist_policy != 3) {
		fprintf(stderr, "Unknown dist policy: %d, now fall to default (1)\n", dist_policy);
		dist_policy = 1;
	}

    if (argc == 4)      { sscanf(argv[3], "%d", &n); m = n; }
    else if (argc == 5) { sscanf(argv[3], "%d", &n); sscanf(argv[4], "%d", &m); }
    else if (argc == 6) { sscanf(argv[3], "%d", &n); sscanf(argv[4], "%d", &m); sscanf(argv[5], "%d", &radius); }
    else if (argc == 7) { sscanf(argv[3], "%d", &n); sscanf(argv[4], "%d", &m); sscanf(argv[5], "%d", &radius); sscanf(argv[6], "%d", &num_its); }
    else {
    	/* the rest of arg ignored */
    }

	if (num_its%2==0) num_its++; /* make it odd so uold/u exchange easier */

	long u_dimX = n+radius+radius;
	long u_dimY = m+radius+radius;
	long u_volumn = u_dimX*u_dimY;
	int coeff_volumn;
	coeff_volumn = (2*radius+1)*(2*radius+1); /* this is for square. Even the cross stencil that use only 4*radius +1, we will use the same square coeff simply */
//	coeff_volumn = 4*radius+1;
    REAL * u = (REAL *)malloc(sizeof(REAL)* u_volumn);
	REAL * u_omp = (REAL *)malloc(sizeof(REAL)* u_volumn);
	REAL * u_omp_mdev = (REAL *)omp_unified_malloc(sizeof(REAL)* u_volumn);
	REAL * u_omp_mdev_iterate = (REAL *)omp_unified_malloc(sizeof(REAL)* u_volumn);
	REAL *coeff = (REAL *) omp_unified_malloc(sizeof(REAL)*coeff_volumn);

	srand(0);
	init_array(u_volumn, u, 0.0, 1.0);
	init_array(coeff_volumn, coeff, 0.0, 1.0);
	memcpy(u_omp, u, sizeof(REAL)*u_volumn);
	memcpy(u_omp_mdev, u, sizeof(REAL)*u_volumn);
	memcpy(u_omp_mdev_iterate, u, sizeof(REAL)*u_volumn);
	//print_array("coeff", "coeff", coeff, 2*radius+1, 2*radius+1);
	//print_array("original", "u", u, u_dimX, u_dimY);

	printf("serial execution\n");
	REAL base_elapsed = read_timer_ms();
//	stencil2d_seq(n, m, u, radius, coeff, num_its);
	base_elapsed = read_timer_ms() - base_elapsed;
	//print_array("after stencil", "us", u, u_dimX, u_dimY);

	printf("OMP execution\n");
	REAL omp_elapsed = read_timer_ms();
	int i;
//	for (i=0;i<num_runs;i++) stencil2d_omp(n, m, u_omp, radius, coeff, num_its);
	omp_elapsed = (read_timer_ms() - omp_elapsed)/num_runs;

	omp_init_devices();
//	printf("OMP mdev execution\n");
//	REAL mdev_elapsed = 0.0;
//	mdev_elapsed = stencil2d_omp_mdev(n, m, u_omp_mdev, radius, coeff, num_its);

	printf("OMP mdev iterate execution\n");
	REAL mdev_iterate_elapsed = 0.0;
	mdev_iterate_elapsed = stencil2d_omp_mdev_iterate(n, m, u_omp_mdev_iterate, radius, coeff, num_its);

	long flops = n*m*radius;
#ifdef SQUARE_SETNCIL
	flops *= 8;
#else
	flops *= 16;
#endif

	printf("======================================================================================================\n");
	printf("\tStencil 2D: %dx%d, stencil radius: %d, #iteratins: %d\n", n, m, radius, num_its);
	printf("------------------------------------------------------------------------------------------------------\n");
	printf("Performance:\t\tRuntime (ms)\t MFLOPS \t\tError (compared to base)\n");
	printf("------------------------------------------------------------------------------------------------------\n");
	printf("base:\t\t%4f\t%4f \t\t%g\n", base_elapsed, flops / (1.0e-3 * base_elapsed), 0.0); //check_accdiff(u, u, u_dimX, u_dimY, radius, 1.0e-7));
	printf("omp: \t\t%4f\t%4f \t\t%g\n", omp_elapsed, flops / (1.0e-3 * omp_elapsed), check_accdiff(u, u_omp, n, m, radius, 0.00001f));
//	printf("omp_mdev: \t%4f\t%4f \t\t%g\n", mdev_elapsed, flops / (1.0e-3 * mdev_elapsed), check_accdiff(u, u_omp_mdev, n, m, radius, 0.00001f));
	printf("omp_mdev_it: \t%4f\t%4f \t\t%g\n", mdev_iterate_elapsed, flops / (1.0e-3 * mdev_iterate_elapsed), check_accdiff(u, u_omp_mdev_iterate, n, m, radius, 0.00001f));

	free(u);
	free(u_omp);
	omp_unified_free(u_omp_mdev);
	omp_unified_free(u_omp_mdev_iterate);
	omp_unified_free(coeff);
	omp_fini_devices();

	return 0;
}

void stencil2d_seq_normal(long n, long m, REAL *u, int radius, REAL *coeff, int num_its) {
	long it; /* iteration */
	long u_dimX = n + 2 * radius;
	long u_dimY = m + 2 * radius;
	int coeff_dimX = 2*radius+1;
	REAL *uold = (REAL*)malloc(sizeof(REAL)*u_dimX * u_dimY);
	memcpy(uold, u, sizeof(REAL)*u_dimX*u_dimY);
	coeff = coeff + (2*radius+1) * radius + radius; /* let coeff point to the center element */
	REAL * uold_save = uold;
	REAL * u_save = u;
	int count = 4*radius+1;
#ifdef SQUARE_SETNCIL
	count = coeff_dimX * coeff_dimX;
#endif

	for (it = 0; it < num_its; it++) {
		int ix, iy, ir;

		for (ix = 0; ix < n; ix++) {
			for (iy = 0; iy < m; iy++) {
				REAL * temp_u = &u[(ix+radius)*u_dimY+radius+iy];
				REAL * temp_uold = &uold[(ix+radius)*u_dimY+radius+iy];
				REAL result = temp_uold[0] * coeff[0];
				/* 2/4 way loop unrolling */
				for (ir = 1; ir <= radius; ir++) {
					result += coeff[ir] * temp_uold[ir];           		//horizontal right
					result += coeff[-ir]* temp_uold[-ir];                  // horizontal left
					result += coeff[-ir*coeff_dimX] * temp_uold[-ir * u_dimY]; //vertical up
					result += coeff[ir*coeff_dimX] * temp_uold[ir * u_dimY]; // vertical bottom
#ifdef SQUARE_SETNCIL
					result += coeff[-ir*coeff_dimX-ir] * temp_uold[-ir * u_dimY-ir] // left upper corner
					result += coeff[-ir*coeff_dimX+ir] * temp_uold[-ir * u_dimY+ir] // right upper corner
					result += coeff[ir*coeff_dimX-ir] * temp_uold[ir * u_dimY]-ir] // left bottom corner
					result += coeff[ir*coeff_dimX+ir] * temp_uold[ir * u_dimY]+ir] // right bottom corner
#endif
				}
				*temp_u = result/count;
			}
		}
		REAL * tmp = uold;
		uold = u;
		u = tmp;
//		if (it % 500 == 0)
//			printf("Finished %d iteration\n", it);
	} /*  End iteration loop */
	free(uold_save);
}

void stencil2d_seq(long n, long m, REAL *u, int radius, REAL *coeff, int num_its) {
	long it; /* iteration */
	long u_dimX = n + 2 * radius;
	long u_dimY = m + 2 * radius;
	int coeff_dimX = 2*radius+1;
	REAL *uold = (REAL*)malloc(sizeof(REAL)*u_dimX * u_dimY);
	memcpy(uold, u, sizeof(REAL)*u_dimX*u_dimY);
	coeff = coeff + (2*radius+1) * radius + radius; /* let coeff point to the center element */
	REAL * uold_save = uold;
	REAL * u_save = u;
	int count = 4*radius+1;
#ifdef SQUARE_SETNCIL
	count = coeff_dimX * coeff_dimX;
#endif

	for (it = 0; it < num_its; it++) {
		int ix, iy, ir;

		for (ix = 0; ix < n; ix++) {
			REAL * temp_u = &u[(ix+radius)*u_dimY+radius];
			REAL * temp_uold = &uold[(ix+radius)*u_dimY+radius];
			for (iy = 0; iy < m; iy++) {
				REAL result = temp_uold[0] * coeff[0];
				/* 2/4 way loop unrolling */
				for (ir = 1; ir <= radius; ir++) {
					result += coeff[ir] * temp_uold[ir];           		//horizontal right
					result += coeff[-ir]* temp_uold[-ir];                  // horizontal left
					result += coeff[-ir*coeff_dimX] * temp_uold[-ir * u_dimY]; //vertical up
					result += coeff[ir*coeff_dimX] * temp_uold[ir * u_dimY]; // vertical bottom
#ifdef SQUARE_SETNCIL
					result += coeff[-ir*coeff_dimX-ir] * temp_uold[-ir * u_dimY-ir] // left upper corner
					result += coeff[-ir*coeff_dimX+ir] * temp_uold[-ir * u_dimY+ir] // right upper corner
					result += coeff[ir*coeff_dimX-ir] * temp_uold[ir * u_dimY]-ir] // left bottom corner
					result += coeff[ir*coeff_dimX+ir] * temp_uold[ir * u_dimY]+ir] // right bottom corner
#endif
				}
				*temp_u = result/count;
				temp_u++;
				temp_uold++;
			}
		}
		REAL * tmp = uold;
		uold = u;
		u = tmp;
//		if (it % 500 == 0)
//			printf("Finished %d iteration\n", it);
	} /*  End iteration loop */
	free(uold_save);
}

void stencil2d_omp(long n, long m, REAL *u, int radius, REAL *coeff, int num_its) {
	long it; /* iteration */
	long u_dimX = n + 2 * radius;
	long u_dimY = m + 2 * radius;
	int coeff_dimX = 2 * radius + 1;
	REAL *uold = (REAL *) malloc(sizeof(REAL) * u_dimX * u_dimY);
	memcpy(uold, u, sizeof(REAL)*u_dimX*u_dimY);
	coeff = coeff + (2 * radius + 1) * radius + radius; /* let coeff point to the center element */
	int count = 4*radius+1;
#ifdef SQUARE_SETNCIL
	count = coeff_dimX * coeff_dimX;
#endif

	REAL * uold_save = uold;
	REAL * u_save = u;
#pragma omp parallel shared(n, m, radius, coeff, num_its, u_dimX, u_dimY, coeff_dimX, count) firstprivate(u, uold) private(it)
	{
		int ix, iy, ir;
		for (it = 0; it < num_its; it++) {
#pragma omp for
			for (ix = 0; ix < n; ix++) {
				REAL *temp_u = &u[(ix + radius) * u_dimY+radius];
				REAL *temp_uold = &uold[(ix + radius) * u_dimY+radius];
				for (iy = 0; iy < m; iy++) {
					REAL result = temp_uold[0] * coeff[0];
					/* 2/4 way loop unrolling */
					for (ir = 1; ir <= radius; ir++) {
						result += coeff[ir] * temp_uold[ir];                //horizontal right
						result += coeff[-ir] * temp_uold[-ir];                  // horizontal left
						result += coeff[-ir * coeff_dimX] * temp_uold[-ir * u_dimY]; //vertical up
						result += coeff[ir * coeff_dimX] * temp_uold[ir * u_dimY]; // vertical bottom
#ifdef SQUARE_SETNCIL
						result += coeff[-ir*coeff_dimX-ir] * temp_uold[-ir * u_dimY-ir] // left upper corner
						result += coeff[-ir*coeff_dimX+ir] * temp_uold[-ir * u_dimY+ir] // right upper corner
						result += coeff[ir*coeff_dimX-ir] * temp_uold[ir * u_dimY]-ir] // left bottom corner
						result += coeff[ir*coeff_dimX+ir] * temp_uold[ir * u_dimY]+ir] // right bottom corner
#endif
					}
					*temp_u = result/count;
					temp_u++;
					temp_uold++;
				}
			}
			REAL *tmp = uold;
			uold = u;
			u = tmp;
//		if (it % 500 == 0)
//			printf("Finished %d iteration by thread %d of %d\n", it, omp_get_thread_num(), omp_get_num_threads());
		} /*  End iteration loop */
	}
	free(uold_save);
}

#if 0
void stencil2d_omp_mdev(long n, long m, REAL *u, int radius, REAL *coeff, int num_its) {
	long it; /* iteration */
	long u_dimX = n + 2 * radius;
	long u_dimY = m + 2 * radius;
	int coeff_dimX = 2 * radius + 1;
	coeff = coeff + (2 * radius + 1) * radius + radius; /* let coeff point to the center element */
	int count = 4*radius+1;
#ifdef SQUARE_SETNCIL
	count = coeff_dimX * coeff_dimX;
#endif

	/* uold should be simpliy allocated on the dev and then copy data from u, here we simplified the initialization */
	REAL *uold = (REAL *) malloc(sizeof(REAL) * u_dimX * u_dimY);
	memcpy(uold, u, sizeof(REAL)*u_dimX * u_dimY);
#pragma omp target data device(*) map(to:n, m, u_dimX, u_dimY, radius, coeff_center, coeff[coeff_dimX][coeff_dimX]) \
  map(tofrom:u[u_dimX][u_dimY] dist_data(BLOCK,DUPLICATE) halo(radius,)) map(to:uold[u_dimX][u_dimY] dist_data(BLOCK,DUPLICATE) halo(radius,))
#pragma omp parallel shared(n, m, radius, coeff, num_its, u_dimX, u_dimY, coeff_dimX) private(it) firstprivate(u, uold) //num_threads(/* num of devices + number of cores */)
	{
		int ix, iy, ir;

/*
#pragma omp target device(*) dist_iteration(BLOCK)
#pragma omp for
		for (ix = 0; ix < u_dimX; ix++) {
			for (iy = 0; iy < u_dimY; iy++) {
				uold[ix * u_dimY + iy] = u[ix * u_dimY + iy];
			}
		}
*/
		for (it = 0; it < num_its; it++) {
#pragma omp target device(*) dist_iteration(BLOCK)
#pragma omp for
			for (ix = 0; ix < n; ix++) {
				REAL *temp_u = &u[(ix + radius) * u_dimY+radius];
				REAL *temp_uold = &uold[(ix + radius) * u_dimY+radius];
				for (iy = 0; iy < m; iy++) {
					REAL result = temp_uold[0] * coeff[0];
					/* 2/4 way loop unrolling */
					for (ir = 1; ir <= radius; ir++) {
						result += coeff[ir] * temp_uold[ir];                //horizontal right
						result += coeff[-ir] * temp_uold[-ir];                  // horizontal left
						result += coeff[-ir * coeff_dimX] * temp_uold[-ir * u_dimY]; //vertical up
						result += coeff[ir * coeff_dimX] * temp_uold[ir * u_dimY]; // vertical bottom
#ifdef SQUARE_SETNCIL
						result += coeff[-ir*coeff_dimX-ir] * temp_uold[-ir * u_dimY-ir] // left upper corner
						result += coeff[-ir*coeff_dimX+ir] * temp_uold[-ir * u_dimY+ir] // right upper corner
						result += coeff[ir*coeff_dimX-ir] * temp_uold[ir * u_dimY]-ir] // left bottom corner
						result += coeff[ir*coeff_dimX+ir] * temp_uold[ir * u_dimY]+ir] // right bottom corner
#endif
					}
					*temp_u = result/count;
					temp_u++;
					temp_uold++;
				}
			}
#pragma omp halo_exchange(u);
			REAL *tmp = uold;
			uold = u;
			u = tmp;
//		if (it % 500 == 0)
//			printf("Finished %d iteration by thread %d of %d\n", it, omp_get_thread_num(), omp_get_num_threads());
		} /*  End iteration loop */
	}
	free(uold);
}
#endif
