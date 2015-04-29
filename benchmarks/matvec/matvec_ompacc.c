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

#if defined (DEVICE_NVGPU_SUPPORT)
#include "xomp_cuda_lib_inlined.cu" 
__global__ void OUT__3__5904__(long n, long start_n, long length_n,REAL *_dev_a,REAL *_dev_x,REAL *_dev_y)
{
  int i,j;
  long _dev_lower;
  long  _dev_upper;
  long _dev_loop_chunk_size;
  long _dev_loop_sched_index;
  long _dev_loop_stride;
  int _dev_thread_num = getCUDABlockThreadCount(1);
  int _dev_thread_id = getLoopIndexFromCUDAVariables(1);
  XOMP_static_sched_init(start_n,start_n + length_n - 1,1,1,_dev_thread_num,_dev_thread_id,&_dev_loop_chunk_size,&_dev_loop_sched_index,&_dev_loop_stride);
  while(XOMP_static_sched_next(&_dev_loop_sched_index,start_n + length_n - 1,1,_dev_loop_stride,_dev_loop_chunk_size,_dev_thread_num,_dev_thread_id,&_dev_lower,&_dev_upper))
    for (i = _dev_lower; i <= _dev_upper; i += 1) {
        for (j = 0; j<n; j++)
         _dev_y[i] += _dev_a[i*n+j] * _dev_x[j];
//		printf("x[%d]: %f, y[%d]: %f\n", i, x[i], i, y[i]);
    }
}
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
#if defined (DEVICE_NVGPU_SUPPORT)
	if (devtype == OMP_DEVICE_NVGPU) {
		int threads_per_team = omp_get_optimal_threads_per_team(off->dev);
		int teams_per_league = omp_get_optimal_teams_per_league(off->dev, threads_per_team, length_n);
        OUT__3__5904__<<<teams_per_league,threads_per_team, 0, off->stream->systream.cudaStream>>>(n, start_n, length_n,(REAL *)a,(REAL *)x,(REAL *)y);
	} else
#endif
    if (devtype == OMP_DEVICE_THSIM || devtype == OMP_DEVICE_HOSTCPU) {
        int i, j;
//#pragma omp parallel for shared(y, x, a, start_n, length_n) private(i,j)
        for (i = start_n; i < start_n + length_n; i++) {
            for (j = 0; j < n; j++)
                y[i] += a[i*n + j] * x[j];
            //printf ("error part!!");
        }
    } else {
        fprintf(stderr, "device type is not supported for this call\n");
        abort();
    }

}

int matvec_mdev_v = 2;

double matvec_ompacc_mdev(REAL *a, REAL *x, REAL *y, long n) {
    double ompacc_init_time = read_timer_ms();

    /* get number of target devices specified by the programmers */
    int __num_target_devices__ = omp_get_num_active_devices(); /*XXX: = runtime or compiler generated code */

    omp_device_t *__target_devices__[__num_target_devices__];
    /**TODO: compiler generated code or runtime call to init the __target_devices__ array */
    int __i__;
    for (__i__ = 0; __i__ < __num_target_devices__; __i__++) {
        __target_devices__[__i__] = &omp_devices[__i__]; /* currently this is simple a copy of the pointer */
    }

    /**TODO: compiler generated code or runtime call to init the topology, topology(top1[4][4])  4x4 topology named top1 */
    omp_grid_topology_t __top__;
    int __top_ndims__ = 1;
    int __top_dims__[__top_ndims__];
    int __top_periodic__[__top_ndims__];
    int __id_map__[__num_target_devices__];
    omp_grid_topology_init_simple(&__top__, __target_devices__, __num_target_devices__, __top_ndims__, __top_dims__,
                                  __top_periodic__, __id_map__);

    int __num_mapped_array__ = 3; /* XXX: need compiler output */
    omp_data_map_info_t __data_map_infos__[__num_mapped_array__];

    omp_offloading_info_t __offloading_info__;
    __offloading_info__.offloadings = (omp_offloading_t *) alloca(sizeof(omp_offloading_t) * __num_target_devices__);
    struct OUT__3__5904__other_args args;
    args.a = a;
    args.n = n;
    args.x = x;
    args.y = y;
    __offloading_info__.per_iteration_profile.num_fp_operations = n*n;
    __offloading_info__.per_iteration_profile.num_load = 2;
    __offloading_info__.per_iteration_profile.num_store = 1;
    omp_dist_info_t loop_nest_dist[1];
    /* we use universal args and launcher because matvec can do it */
    omp_offloading_init_info("matvec kernel", &__offloading_info__, &__top__, __target_devices__, 1,
                             OMP_OFFLOADING_DATA_CODE, __num_mapped_array__, __data_map_infos__, OUT__3__5904__launcher,
                             &args, loop_nest_dist, 1);

    omp_data_map_info_t *__info__ = &__data_map_infos__[0];
    long x_dims[1];
    x_dims[0] = n;
    omp_data_map_t x_maps[__num_target_devices__];
    omp_dist_info_t x_dist[1];
    omp_data_map_init_info("x", __info__, &__offloading_info__, x, 1, x_dims, sizeof(REAL), x_maps, OMP_DATA_MAP_TO,
                           OMP_DATA_MAP_AUTO, x_dist);

    __info__ = &__data_map_infos__[1];
    long y_dims[1];
    y_dims[0] = n;
    omp_data_map_t y_maps[__num_target_devices__];
    omp_dist_info_t y_dist[1];
    omp_data_map_init_info("y", __info__, &__offloading_info__, y, 1, y_dims, sizeof(REAL), y_maps, OMP_DATA_MAP_TOFROM,
                           OMP_DATA_MAP_AUTO, y_dist);


    __info__ = &__data_map_infos__[2];
    long a_dims[2];
    a_dims[0] = n;
    a_dims[1] = n;
    omp_data_map_t a_maps[__num_target_devices__];
    omp_dist_info_t a_dist[2];
    omp_data_map_init_info("a", __info__, &__offloading_info__, a, 2, a_dims, sizeof(REAL), a_maps, OMP_DATA_MAP_TO,
                           OMP_DATA_MAP_AUTO, a_dist);


    if (matvec_mdev_v == 3) { /* version 3 */
        omp_dist_init_info(&x_dist[0], OMP_DIST_POLICY_DUPLICATE, 0, n, 0);
        omp_dist_init_info(&y_dist[0], OMP_DIST_POLICY_BLOCK, 0, n, 0);
        omp_dist_init_info(&a_dist[0], OMP_DIST_POLICY_BLOCK, 0, n, 0);
        omp_dist_init_info(&a_dist[1], OMP_DIST_POLICY_DUPLICATE, 0, n, 0);
        omp_align_dist_init_info(&loop_nest_dist[0], OMP_DIST_POLICY_ALIGN, &__data_map_infos__[1],
                                 OMP_DIST_TARGET_DATA_MAP, 0);
        printf("version 3: BLOCK dist policy for x and y, and loop dist aligns with x\n");
    } else if (matvec_mdev_v == 4) {/* version 4 */
        omp_dist_init_info(&loop_nest_dist[0], OMP_DIST_POLICY_AUTO, 0, n, 0);
        omp_dist_init_info(&x_dist[0], OMP_DIST_POLICY_DUPLICATE, 0, n, 0);
        omp_align_dist_init_info(&y_dist[0], OMP_DIST_POLICY_ALIGN, &__offloading_info__,
                                 OMP_DIST_TARGET_LOOP_ITERATION, 0);
        omp_align_dist_init_info(&a_dist[0], OMP_DIST_POLICY_ALIGN, &__offloading_info__,
                                 OMP_DIST_TARGET_LOOP_ITERATION, 0);
        omp_dist_init_info(&a_dist[1], OMP_DIST_POLICY_DUPLICATE, 0, n, 0);
        printf("version 4: AUTO dist policy for loop, and x and y align with loop dist\n");
    }
    else { /* default, version 2, block */
        omp_dist_init_info(&x_dist[0], OMP_DIST_POLICY_DUPLICATE, 0, n, 0);
        omp_dist_init_info(&y_dist[0], OMP_DIST_POLICY_BLOCK, 0, n, 0);
        omp_dist_init_info(&a_dist[0], OMP_DIST_POLICY_BLOCK, 0, n, 0);
        omp_dist_init_info(&a_dist[1], OMP_DIST_POLICY_DUPLICATE, 0, n, 0);

        omp_dist_init_info(&loop_nest_dist[0], OMP_DIST_POLICY_BLOCK, 0, n, 0);
        printf("version 2: BLOCK dist policy for x, y, and loop\n");
    }

    /*********** NOW notifying helper thread to work on this offload ******************/
#if DEBUG_MSG
	 printf("=========================================== offloading to %d targets ==========================================\n", __num_target_devices__);
#endif

    ompacc_init_time = read_timer_ms() - ompacc_init_time;
  //  printf("init time: %fs\n", ompacc_init_time);
    /* here we do not need sync start */
    double off_total = read_timer_ms();
    /* here we do not need sync start */
    int it; int total_its = 20;
    for (it=0; it<total_its; it++) omp_offloading_start(&__offloading_info__, it==total_its-1);
    off_total = (read_timer_ms() - off_total)/total_its;
#if defined (OMP_BREAKDOWN_TIMING)
	omp_offloading_info_report_profile(&__offloading_info__);
#endif
    omp_offloading_fini_info(&__offloading_info__);

    off_total += ompacc_init_time;
    return off_total;
}
