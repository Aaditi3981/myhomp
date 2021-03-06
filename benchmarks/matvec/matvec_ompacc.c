#include "matvec.h"
#include "homp.h"

#if 0

/* v2: explicit distribution of both data and loop:
 * the a[0:n][0:n], and x[0:n] will be evenly distributed among the ndev devices,
 * scalars such as a and n will each have a mapped copy in all the devices, loop will also be evenly distributed 
 */
void matvec_mdev_v2(REAL* x, REAL* y,  long n, REAL *a) {
long i, j;
#pragma omp target device (*) map(tofrom: y[0:n] dist_data(BLOCK)) map(to: x[0:n] dist_data(DUPLICATE),a[0:n][0:n] dist_data(BLOCK, DUPLICATE),n)
#pragma omp parallel for shared(x, y, n, a) dist_iteration(BLOCK)
  for (i = 0; i < n; ++i)
    for (j = 0; j < n; ++j)
    y[i] += a[i*n+j] * x[j];
}

/* v3: block distribute array x and y and let the loop distribution aligh with a*/
 
void matvec_mdev_v3(REAL* x, REAL* y,  long n, REAL *a) {
long i, j;
#pragma omp target device (*) map(tofrom: y[0:n] dist_data(ALIGN(lp1))) map(to: x[0:n] dist_data(DUPLICATE),a[0:n][0:n] dist_data(ALIGN(lp1), DUPLICATE),n)
#pragma omp parallel for shared(x, y, n, a) dist_iteration(ALIGN(lp1))
lp1: for (i = 0; i < n; ++i)
    for (j = 0; j < n; ++j)
   y[i] += a[i*n+j] * x[j];
}


/* v4: AUTO-distribute the loop iteration and let the distribution of array a to be aligned with loop distribution.*/
 
void matvec_mdev_v4(REAL* x, REAL* y,  long n, REAL *a) {
long i, j;
#pragma omp target device (*) map(tofrom: y[0:n] dist_data(ALIGN)) map(to: x[0:n] dist_data(DUPLICATE),a[0:n][0:n] dist_data(ALIGN, DUPLICATE),n)
#pragma omp parallel for shared(x, y, n, a) dist_iteration(AUTO)
  for (i = 0; i < n; ++i)
    for (j = 0; j < n; ++j)
    y[i] += a[i*n+j] * x[j];
}


/* NOTE: the compiler needs to do the analysis for multiple pragma(s) and loop nest. The x[:] in the mapped_range x[:] should
 * be in the previous pragma's map clause
 *
 * Thus this requires the runtime to keep track of the mapped variables and all the details. In some examples, those information could
 * be provided by code-generation of compiler. but in other cases, e.g. the orphaned directive, one need to retrieve from the runtime
 * to get those information. Thus in current implementation, we simply keep all the information in the runtime, regardless of using it
 * or not.
 */
#endif



struct OUT__3__5904__other_args {
    REAL *a;
    long n;
    REAL *x;
    REAL *y;
};

/* called by the helper thread */
void OUT__3__5904__launcher(omp_offloading_t *off, void *args) {
    struct OUT__3__5904__other_args *iargs = (struct OUT__3__5904__other_args *) args;
    long start_n, length_n;
    long n = iargs->n;
    omp_data_map_t *map_x = omp_map_get_map(off, iargs->x, 0);
    omp_data_map_t *map_y = omp_map_get_map(off, iargs->y, 1);
    omp_data_map_t *map_a = omp_map_get_map(off, iargs->a, 2);

    REAL *x = (REAL *) map_x->map_dev_ptr;
    REAL *y = (REAL *) map_y->map_dev_ptr;
    REAL *a = (REAL *) map_a->map_dev_ptr;

    omp_loop_get_range(off, 0, &start_n, &length_n);

    //long omp_loop_get_range(omp_offloading_t * off, int loop_depth, long * start, long* length) {
    //printf("devseqid: %d, start_n: %d, length_n: %d, x: %X, y: %X\n", off->devseqid, start_n, length_n, x, y);

    omp_device_type_t devtype = off->dev->type;
    if (devtype == OMP_DEVICE_NVGPU) {
#if defined (DEVICE_NVGPU_CUDA_SUPPORT)
		matvec_nvgpu_cuda_wrapper(off, n, start_n, length_n,(REAL *)a,(REAL *)x,(REAL *)y);
#endif
    } else if (devtype == OMP_DEVICE_ITLMIC) {
#if defined(DEVICE_ITLMIC_SUPPORT)
		matvec_itlmic_wrapper(off, n, start_n, length_n,(REAL *)a,(REAL *)x,(REAL *)y);
#endif
    } else if (devtype == OMP_DEVICE_THSIM || devtype == OMP_DEVICE_HOSTCPU) {
        matvec_cpu_omp_wrapper(off, n, start_n, length_n,(REAL *)a,(REAL *)x,(REAL *)y);
    } else {
        fprintf(stderr, "device type is not supported for this call\n");
        abort();
    }

}

double matvec_ompacc_mdev(int ndevs, int *targets, REAL *a, REAL *x, REAL *y, long n) {
    double ompacc_init_time = read_timer_ms();

    omp_grid_topology_t * __top__ = omp_grid_topology_init(ndevs, targets, 1);

    int __num_maps__ = 3; /* XXX: need compiler output */
    struct OUT__3__5904__other_args args;
    args.a = a; args.n = n; args.x = x;args.y = y;

    omp_offloading_info_t *__off_info__ = omp_offloading_init_info("matvec_kernel", __top__, 0, OMP_OFFLOADING_DATA_CODE,
                                                                   __num_maps__, OUT__3__5904__launcher, &args, 1);
    omp_offloading_append_profile_per_iteration(__off_info__, 2 * n, 2*n, 1);

    omp_data_map_info_t *__x_map_info__ = &__off_info__->data_map_info[0];
    omp_data_map_init_info("x", __x_map_info__, __off_info__, x, 1, sizeof(REAL), OMP_DATA_MAP_TO, OMP_DATA_MAP_AUTO);
    omp_data_map_info_set_dims_1d(__x_map_info__, n);

    omp_data_map_info_t *__y_map_info__ = &__off_info__->data_map_info[1];
    omp_data_map_init_info("y", __y_map_info__, __off_info__, y, 1, sizeof(REAL), OMP_DATA_MAP_TOFROM, OMP_DATA_MAP_AUTO);
    omp_data_map_info_set_dims_1d(__y_map_info__, n);

    omp_data_map_info_t *__a_map_info__ = &__off_info__->data_map_info[2];
    omp_data_map_init_info("a", __a_map_info__, __off_info__, a, 2, sizeof(REAL), OMP_DATA_MAP_TO, OMP_DATA_MAP_AUTO);
    omp_data_map_info_set_dims_2d(__a_map_info__, n, n);
    //printf("x: %X, y: %X, a: %X\n", x, y, a);

#if 0
    omp_data_map_dist_init_info(__x_map_info__, 0, OMP_DIST_POLICY_FULL, 0, n, 0, 0);
    omp_data_map_dist_init_info(__y_map_info__, 0, OMP_DIST_POLICY_BLOCK, 0, n, 0, 0);
    omp_data_map_dist_init_info(__a_map_info__, 0, OMP_DIST_POLICY_BLOCK, 0, n, 0, 0);
    omp_data_map_dist_init_info(__a_map_info__, 1, OMP_DIST_POLICY_FULL, 0, n, 0, 0);
    omp_loop_dist_init_info(__off_info__, 0, OMP_DIST_POLICY_BLOCK, 0, n, 0, 0);
    printf("version 2: BLOCK dist policy for x, y, and loop\n");
#endif
#if 0
    omp_data_map_dist_init_info(__x_map_info__, 0, OMP_DIST_POLICY_FULL, 0, n, 0, 0);
    omp_data_map_dist_init_info(__y_map_info__, 0, OMP_DIST_POLICY_BLOCK, 0, n, 0, 0);
    omp_data_map_dist_align_with_data_map(__a_map_info__, 0, 0, __y_map_info__, 0);
    //omp_data_map_dist_init_info(__a_map_info__, 0, OMP_DIST_POLICY_BLOCK, 0, n, 0);
    omp_data_map_dist_init_info(__a_map_info__, 1, OMP_DIST_POLICY_FULL, 0, n, 0, 0);
    omp_loop_dist_align_with_data_map(__off_info__, 0, 0, __y_map_info__, 0);
    printf("version 3: BLOCK dist policy for x and y, and loop dist aligns with x\n");
#endif

    omp_loop_dist_init_info(__off_info__, 0, LOOP_DIST_POLICY, 0, n, LOOP_DIST_CHUNK_SIZE, 0);
    omp_data_map_dist_init_info(__x_map_info__, 0, OMP_DIST_POLICY_FULL, 0, n, 0, 0);
    omp_data_map_dist_align_with_loop(__y_map_info__, 0, 0, __off_info__, 0);
    omp_data_map_dist_align_with_loop(__a_map_info__, 0, 0, __off_info__, 0);
    omp_data_map_dist_init_info(__a_map_info__, 1, OMP_DIST_POLICY_FULL, 0, n, 0, 0);
    /*********** NOW notifying helper thread to work on this offload ******************/
#if DEBUG_MSG
	 printf("=========================================== offloading to %d targets ==========================================\n", __num_target_devices__);
#endif

    ompacc_init_time = read_timer_ms() - ompacc_init_time;
  //  printf("init time: %fs\n", ompacc_init_time);
    /* here we do not need sync start */
    double off_total = read_timer_ms();
    /* here we do not need sync start */
    int it; int total_its = 10;
    for (it=0; it<total_its; it++) omp_offloading_start(__off_info__);
    off_total = (read_timer_ms() - off_total)/total_its;
#if defined (OMP_BREAKDOWN_TIMING)
    omp_print_map_info(__x_map_info__);
    omp_print_map_info(__y_map_info__);
    omp_print_map_info(__a_map_info__);
    omp_offloading_info_report_profile(__off_info__, total_its);
#endif
    omp_offloading_fini_info(__off_info__);
    omp_grid_topology_fini(__top__);

    off_total += ompacc_init_time;
    return off_total;
}
