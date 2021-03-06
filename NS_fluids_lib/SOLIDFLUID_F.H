#ifndef _SOLIDFLUID_F_H_
#define _SOLIDFLUID_F_H_ 

#ifdef BL_LANG_FORT
#    define FORT_HEADERMSG headermsg
#    define FORT_FILLCONTAINER fillcontainer
#else
#  ifdef BL_FORT_USE_UPPERCASE
#    define FORT_HEADERMSG HEADERMSG
#    define FORT_FILLCONTAINER FILLCONTAINER
#  else
#  ifdef BL_FORT_USE_LOWERCASE
#    define FORT_HEADERMSG headermsg
#    define FORT_FILLCONTAINER fillcontainer
#  else
#    define FORT_HEADERMSG headermsg_
#    define FORT_FILLCONTAINER fillcontainer_
#  endif
#  endif

#include <AMReX_ArrayLim.H>

namespace amrex{

  extern "C" {

    void FORT_HEADERMSG(
     int* tid,
     int* tilenum,
     const int* gridno,
     int* nthread_parm,
     int* level,
     int* finest_level,
     const int* max_level,
     int* FSI_operation,
     int* FSI_sub_operation,
     const int* tilelo,const int* tilehi,
     const int* fablo,const int* fabhi,
     int* bfact,
     const Real* xlo,
     const Real* dx,
     Real* dx_max_level,
     Real* problo,
     Real* probhi,
     const int* velbc,
     const int* vofbc,
     Real* FSIdata,AMREX_ARLIM_P(FSIdatalo),AMREX_ARLIM_P(FSIdatahi),
     Real* veldata,AMREX_ARLIM_P(veldatalo),AMREX_ARLIM_P(veldatahi),
     Real* masknbr,AMREX_ARLIM_P(masknbrlo),AMREX_ARLIM_P(masknbrhi),
     Real* maskfiner,AMREX_ARLIM_P(maskfinerlo),AMREX_ARLIM_P(maskfinerhi),
     int* nFSI,
     int* nFSI_sub,
     int* ngrowFSI,
     int* nparts,
     int* im_solid_map,
     Real* h_small,
     Real* time,
     Real* dt,
     int* FSI_refine_factor,
     int* FSI_bounding_box_ngrow,
     int* touch_flag,
     int* CTML_FSI_INIT,
     int* CTML_force_model,
     int* iter,
     int* current_step,
     int* plot_interval,
     int* ioproc);

    void FORT_FILLCONTAINER(
     int* level,
     int* finest_level,
     const int* sci_max_level,
     Real* time,
     Real* dt,
     int* tilelo_array,
     int* tilehi_array,
     Real* xlo_array,
     const Real* dx,
     Real* dx_max_level,
     int* num_grids_on_level,
     int* num_grids_on_level_proc,
     int* gridno_array,
     int* num_tiles_on_thread_proc,
     int* nthread_parm,
     int* max_num_tiles_on_thread_proc,
     int* tile_dim,
     int* nmat,
     int* nparts,
     int* im_solid_map,
     Real* problo,
     Real* probhi);

  };

}/* namespace amrex */

#endif

#endif

