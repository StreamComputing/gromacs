/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright (c) 2012,2013,2014, by the GROMACS development team, led by
 * Mark Abraham, David van der Spoel, Berk Hess, and Erik Lindahl,
 * and including many others, as listed in the AUTHORS file in the
 * top-level source directory and at http://www.gromacs.org.
 *
 * GROMACS is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * GROMACS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GROMACS; if not, see
 * http://www.gnu.org/licenses, or write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 *
 * If you want to redistribute modifications to GROMACS, please
 * consider that scientific software is very special. Version
 * control is crucial - bugs must be traceable. We will be happy to
 * consider code for inclusion in the official distribution, but
 * derived work must not be called official GROMACS. Details are found
 * in the README & COPYING files - if they are missing, get the
 * official version at http://www.gromacs.org.
 *
 * To help us fund GROMACS development, we humbly ask that you cite
 * the research papers on the package. Check out http://www.gromacs.org.
 */
 
/** \file nbnxn_ocl_types.h
 *  \brief OpenCL equivalent of nbnxn_cuda_types.h
 */

#ifndef NBNXN_OPENCL_TYPES_H
#define NBNXN_OPENCL_TYPES_H

/* kernel does #include "gromacs/math/utilities.h" */
/* Move the actual useful stuff here: */
#define M_FLOAT_1_SQRTPI 0.564189583547756f
#include "gromacs/utility/real.h"

#include "types/interaction_const.h"

/* Fixing headers: Mixed host/device structure.. Header had to be modified to avoid irrelevant types in
 * device code, as tMPI_atomic_t.*/
//#include "nbnxn_pairlist.h"
#include "../nbnxn_pairlist.h"

/* Fixing headers: not needed anymore, host structure nbnxn_opencl not included anymore in device code... 
 * Dependency was wallclock_gpu_t */
/* #include "nbnxn_opencl_types_ext.h" */ 

/* Fixing headers: Only dependency is WARP_SIZE. The rest are host api code.. In OpenCL warp size can
    differ anyway!. For now it is define in upper-level (kernel-utils.h) */
/* #include "../../gmxlib/cuda_tools/cudautils.cuh" */

#ifdef __cplusplus
extern "C" {
#endif
/*! \brief Electrostatic OpenCL kernel flavors.
 *
 *  Types of electrostatics implementations available in the OpenCL non-bonded
 *  force kernels. These represent both the electrostatics types implemented
 *  by the kernels (cut-off, RF, and Ewald - a subset of what's defined in
 *  enums.h) as well as encode implementation details analytical/tabulated
 *  and single or twin cut-off (for Ewald kernels).
 *  Note that the cut-off and RF kernels have only analytical flavor and unlike
 *  in the CPU kernels, the tabulated kernels are ATM Ewald-only.
 *
 *  The row-order of pointers to different electrostatic kernels defined in
 *  nbnxn_cuda.cu by the nb_*_kfunc_ptr function pointer table
 *  should match the order of enumerated types below.
 */
enum eelOcl {
    eelOclCUT, eelOclRF, eelOclEWALD_TAB, eelOclEWALD_TAB_TWIN, eelOclEWALD_ANA, eelOclEWALD_ANA_TWIN, eelOclNR
};

/*! \brief VdW OpenCL kernel flavors.
 *
 * The enumerates values correspond to the LJ implementations in the OpenCL non-bonded
 * kernels.
 *
 * The column-order of pointers to different electrostatic kernels defined in
 * nbnxn_cuda.cu by the nb_*_kfunc_ptr function pointer table
 * should match the order of enumerated types below.
 */
enum evdwOcl {
    evdwOclCUT, evdwOclFSWITCH, evdwOclPSWITCH, evdwOclEWALDGEOM, evdwOclEWALDLB, evdwOclNR
};

/** \internal
 * \brief Staging area for temporary data downloaded from the GPU.
 *
 *  The energies/shift forces get downloaded here first, before getting added
 *  to the CPU-side aggregate values.
 */
typedef struct cl_nb_staging
{
    float   *e_lj;      /**< LJ energy                       */
    float   *e_el;      /**< electrostatic energy            */    
    float *fshift;      /**< float3 buffer with shift forces */
    // TODO: review fshift data type and how its size is computed
}cl_nb_staging_t;

/** \internal
 * \brief Nonbonded atom data - both inputs and outputs.
 */
typedef struct cl_atomdata
{
    int      natoms;            /**< number of atoms                              */
    int      natoms_local;      /**< number of local atoms                        */
    int      nalloc;            /**< allocation size for the atom data (xq, f)    */

    cl_mem xq;                  /**< float4 buffer with atom coordinates + charges, size natoms */
    cl_mem f;                   /**< float3 buffer with force output array, size natoms         */

    cl_mem e_lj;                /**< LJ energy output, size 1                       */
    cl_mem e_el;                /**< Electrostatics energy input, size 1            */

    cl_mem fshift;              /**< float3 buffer with shift forces                */

    int      ntypes;            /**< number of atom types                           */   
    cl_mem atom_types;          /**< int buffer with atom type indices, size natoms */

    cl_mem shift_vec;           /**< float3 buffer with shifts values               */
    cl_bool     bShiftVecUploaded; /**< true if the shift vector has been uploaded  */
} cl_atomdata_t;

/** \internal
 * \brief Parameters required for the OpenCL nonbonded calculations.
 */
typedef struct cl_nbparam
{

    int             eeltype;          /**< type of electrostatics, takes values from #eelOcl */
    int             vdwtype;          /**< type of VdW impl., takes values from #evdwOcl     */

    float           epsfac;           /**< charge multiplication factor                      */
    float           c_rf;             /**< Reaction-field/plain cutoff electrostatics const. */
    float           two_k_rf;         /**< Reaction-field electrostatics constant            */
    float           ewald_beta;       /**< Ewald/PME parameter                               */
    float           sh_ewald;         /**< Ewald/PME correction term substracted from the direct-space potential */
    float           sh_lj_ewald;      /**< LJ-Ewald/PME correction term added to the correction potential        */
    float           ewaldcoeff_lj;    /**< LJ-Ewald/PME coefficient                          */

    float           rcoulomb_sq;      /**< Coulomb cut-off squared                           */

    float           rvdw_sq;          /**< VdW cut-off squared                               */
    float           rvdw_switch;      /**< VdW switched cut-off                              */
    float           rlist_sq;         /**< pair-list cut-off squared                         */

    shift_consts_t  dispersion_shift; /**< VdW shift dispersion constants           */
    shift_consts_t  repulsion_shift;  /**< VdW shift repulsion constants            */
    switch_consts_t vdw_switch;       /**< VdW switch constants                     */

    /* LJ non-bonded parameters - accessed through texture memory */
    cl_mem                  nbfp_climg2d;      /**< nonbonded parameter table with C6/C12 pairs per atom type-pair, 2*ntype^2 elements */
    cl_mem                  nbfp_comb_climg2d; /**< nonbonded parameter table per atom type, 2*ntype elements                          */

    /* Ewald Coulomb force table data - accessed through texture memory */
    int                    coulomb_tab_size;   /**< table size (s.t. it fits in texture cache) */
    float                  coulomb_tab_scale;  /**< table scale/spacing                        */
    cl_mem                 coulomb_tab_climg2d;/**< pointer to the table in the device memory  */    
}cl_nbparam_t;

// Data structure shared between the OpenCL device code and OpenCL host code
// Must not contain OpenCL objects (buffers)
// TODO: review, improve
typedef struct cl_nbparam_params
{

    int             eeltype;          /**< type of electrostatics, takes values from #eelCu */
    int             vdwtype;          /**< type of VdW impl., takes values from #evdwCu     */

    float           epsfac;           /**< charge multiplication factor                      */
    float           c_rf;             /**< Reaction-field/plain cutoff electrostatics const. */
    float           two_k_rf;         /**< Reaction-field electrostatics constant            */
    float           ewald_beta;       /**< Ewald/PME parameter                               */
    float           sh_ewald;         /**< Ewald/PME correction term substracted from the direct-space potential */
    float           sh_lj_ewald;      /**< LJ-Ewald/PME correction term added to the correction potential        */
    float           ewaldcoeff_lj;    /**< LJ-Ewald/PME coefficient                          */

    float           rcoulomb_sq;      /**< Coulomb cut-off squared                           */

    float           rvdw_sq;          /**< VdW cut-off squared                               */
    float           rvdw_switch;      /**< VdW switched cut-off                              */
    float           rlist_sq;         /**< pair-list cut-off squared                         */

    shift_consts_t  dispersion_shift; /**< VdW shift dispersion constants           */
    shift_consts_t  repulsion_shift;  /**< VdW shift repulsion constants            */
    switch_consts_t vdw_switch;       /**< VdW switch constants                     */   

    /* Ewald Coulomb force table data - accessed through texture memory */
    int                    coulomb_tab_size;   /**< table size (s.t. it fits in texture cache) */
    float                  coulomb_tab_scale;  /**< table scale/spacing                        */    
}cl_nbparam_params_t;


/** \internal
 * \brief Pair list data.
 */
typedef struct cl_plist
{
    int              na_c;        /**< number of atoms per cluster                  */

    int              nsci;        /**< size of sci, # of i clusters in the list     */
    int              sci_nalloc;  /**< allocation size of sci                       */    
    cl_mem           sci;         /**< list of i-cluster ("super-clusters").
                                       It contains elements of type nbnxn_sci_t     */

    int              ncj4;        /**< total # of 4*j clusters                      */
    int              cj4_nalloc;  /**< allocation size of cj4                       */    
    cl_mem           cj4;         /**< 4*j cluster list, contains j cluster number and
                                       index into the i cluster list.
                                       It contains elements of type nbnxn_cj4_t     */    
    cl_mem           excl;        /**< atom interaction bits                        
                                       It contains elements of type nbnxn_excl_t    */
    int              nexcl;       /**< count for excl                               */
    int              excl_nalloc; /**< allocation size of excl                      */

    cl_bool          bDoPrune;    /**< true if pair-list pruning needs to be
                                       done during the  current step                */
}cl_plist_t;


/** \internal
 * \brief OpenCL events used for timing GPU kernels and H2D/D2H transfers.
 *
 * The two-sized arrays hold the local and non-local values and should always
 * be indexed with eintLocal/eintNonlocal.
 */
typedef struct cl_timers
{
    cl_event atdat;             /**< event for atom data transfer (every PS step)                 */    

    cl_event nb_h2d[2];         /**< events for x/q H2D transfers (l/nl, every step)              */

    cl_event nb_d2h_f[2];       /**< events for f D2H transfer (l/nl, every step)                 */
    cl_event nb_d2h_fshift[2];  /**< events for fshift D2H transfer (l/nl, every step)            */
    cl_event nb_d2h_e_el[2];    /**< events for e_el D2H transfer (l/nl, every step)              */
    cl_event nb_d2h_e_lj[2];    /**< events for e_lj D2H transfer (l/nl, every step)              */    
    
    cl_event pl_h2d_sci[2];     /**< events for pair-list sci H2D transfers (l/nl, every PS step) */
    cl_event pl_h2d_cj4[2];     /**< events for pair-list cj4 H2D transfers (l/nl, every PS step) */
    cl_event pl_h2d_excl[2];    /**< events for pair-list excl H2D transfers (l/nl, every PS step)*/    
    
    cl_event nb_k[2];           /**< event for non-bonded kernels (l/nl, every step)              */    
}cl_timers_t;

/** \internal
 * \brief Main data structure for OpenCL nonbonded force calculations.
 */
struct nbnxn_opencl
{
    ocl_gpu_info_t *dev_info;        /**< OpenCL device information                                  */    

                                     /** non-bonded kernels */
                                     /** organized similar with nb_kfunc_xxx arrays in nbnxn_ocl.cpp */
    cl_kernel           kernel_noener_noprune_ptr[eelOclNR][evdwOclNR];
    cl_kernel           kernel_ener_noprune_ptr[eelOclNR][evdwOclNR] ;
    cl_kernel           kernel_noener_prune_ptr[eelOclNR][evdwOclNR];
    cl_kernel           kernel_ener_prune_ptr[eelOclNR][evdwOclNR];

                                    /** auxiliary kernels implementing memset like functions         */
    cl_kernel           kernel_memset_f;
    cl_kernel           kernel_memset_f2;
    cl_kernel           kernel_memset_f3;
    cl_kernel           kernel_zero_e_fshift;

    cl_bool          bUseTwoStreams; /**< true if doing both local/non-local NB work on GPU          */
    cl_bool          bUseStreamSync; /**< true if the standard synchronization is used
                                          and not memory polling-based waiting                       */
    cl_atomdata_t   *atdat;          /**< atom data                                                  */
    cl_nbparam_t    *nbparam;        /**< parameters required for the non-bonded calc.               */
    cl_plist_t      *plist[2];       /**< pair-list data structures (local and non-local)            */
    cl_nb_staging_t  nbst;           /**< staging area where fshift/energies get downloaded          */

    cl_mem           debug_buffer;

    cl_command_queue stream[2];      /**< local and non-local GPU queues                             */

    /** events used for synchronization */
    cl_event    nonlocal_done;    /**< event triggered when the non-local non-bonded kernel
                                        is done (and the local transfer can proceed)                 */
    cl_event    misc_ops_done;    /**< event triggered when the operations that precede the
                                          main force calculations are done (e.g. buffer 0-ing)       */

    /* NOTE: With current CUDA versions (<=5.0) timing doesn't work with multiple
     * concurrent streams, so we won't time if both l/nl work is done on GPUs.
     * Timer init/uninit is still done even with timing off so only the condition
     * setting bDoTime needs to be change if this CUDA "feature" gets fixed. */
    cl_bool          bDoTime;       /**< True if event-based timing is enabled.                     */
    cl_timers_t     *timers;        /**< OpenCL event-based timers.                                 */
    wallclock_gpu_t *timings;       /**< Timing data.                                               */
};

#ifdef __cplusplus
}
#endif

#endif  /* NBNXN_OPENCL_TYPES_H */