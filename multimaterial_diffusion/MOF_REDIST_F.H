#ifndef _MOF_REDIST_F_H_
#define _MOF_REDIST_F_H_ 

#ifdef BL_LANG_FORT
#    define FORT_NODE_TO_CELL node_to_cell
#    define FORT_FD_NODE_NORMAL fd_node_normal
#    define FORT_FD_NORMAL fd_normal
#    define FORT_LEVELSTRIP levelstrip
#    define FORT_STENINIT steninit
#    define FORT_FACEINITTEST faceinittest
#    define FORT_FACEINIT faceinit
#    define FORT_FACEPROCESS faceprocess
#    define FORT_CORRECT_UNINIT correct_uninit
#else
#  ifdef BL_FORT_USE_UPPERCASE
#    define FORT_NODE_TO_CELL NODE_TO_CELL
#    define FORT_FD_NODE_NORMAL FD_NODE_NORMAL
#    define FORT_FD_NORMAL FD_NORMAL
#    define FORT_LEVELSTRIP LEVELSTRIP
#    define FORT_STENINIT STENINIT
#    define FORT_FACEINITTEST FACEINITTEST
#    define FORT_FACEINIT FACEINIT
#    define FORT_FACEPROCESS FACEPROCESS
#    define FORT_CORRECT_UNINIT CORRECT_UNINIT
#  else
#  ifdef BL_FORT_USE_LOWERCASE
#    define FORT_NODE_TO_CELL node_to_cell
#    define FORT_FD_NODE_NORMAL fd_node_normal
#    define FORT_FD_NORMAL fd_normal
#    define FORT_LEVELSTRIP levelstrip
#    define FORT_STENINIT steninit
#    define FORT_FACEINITTEST faceinittest
#    define FORT_FACEINIT faceinit
#    define FORT_FACEPROCESS faceprocess
#    define FORT_CORRECT_UNINIT correct_uninit
#  else
#    define FORT_NODE_TO_CELL node_to_cell_
#    define FORT_FD_NODE_NORMAL fd_node_normal_
#    define FORT_FD_NORMAL fd_normal_
#    define FORT_LEVELSTRIP levelstrip_
#    define FORT_STENINIT steninit_
#    define FORT_FACEINITTEST faceinittest_
#    define FORT_FACEINIT faceinit_
#    define FORT_FACEPROCESS faceprocess_
#    define FORT_CORRECT_UNINIT correct_uninit_
#  endif
#  endif

#include <AMReX_ArrayLim.H>

namespace amrex{

  extern "C" {


     void FORT_FD_NORMAL(
       int* level,
       int* finest_level,
       Real* lsnew,AMREX_ARLIM_P(lsnewlo),AMREX_ARLIM_P(lsnewhi),
       Real* lsnrmFD,AMREX_ARLIM_P(lsnrmFDlo),AMREX_ARLIM_P(lsnrmFDhi),
       const int* tilelo,const int* tilehi,
       const int* fablo,const int* fabhi,
       int* bfact,
       const Real* xlo,const Real* dx,
       int* nmat);


     void FORT_FD_NODE_NORMAL(
       int* level,
       int* finest_level,
       Real* lsnew,AMREX_ARLIM_P(lsnewlo),AMREX_ARLIM_P(lsnewhi),
       Real* lsnrmFD,AMREX_ARLIM_P(lsnrmFDlo),AMREX_ARLIM_P(lsnrmFDhi),
       const int* tilelo,const int* tilehi,
       const int* fablo,const int* fabhi,
       int* bfact,
       const Real* xlo,const Real* dx,
       int* nmat,
       int* nten,
       int* n_normal,
       int* ngrow_dest);

     void FORT_NODE_TO_CELL(
       int* level,
       int* finest_level,
       Real* lsnew,AMREX_ARLIM_P(lsnewlo),AMREX_ARLIM_P(lsnewhi),
       Real* lsnrmFD,AMREX_ARLIM_P(lsnrmFDlo),AMREX_ARLIM_P(lsnrmFDhi),
       Real* curvcell,AMREX_ARLIM_P(curvcelllo),AMREX_ARLIM_P(curvcellhi),
       const int* tilelo,const int* tilehi,
       const int* fablo,const int* fabhi,
       int* bfact,
       const Real* xlo,const Real* dx,
       int* nmat,
       int* nten,
       int* n_normal,
       int* ngrow_nrm);


     void FORT_LEVELSTRIP(
       int* keep_all_interfaces,
       int* nprocessed,
       Real* minLS,
       Real* maxLS,
       Real* max_problen,
       int* level,
       int* finest_level,
       int* truncate_volume_fractions,
       Real* latent_heat,
       Real* maskfab,AMREX_ARLIM_P(maskfablo),AMREX_ARLIM_P(maskfabhi),
       Real* facepairX,
       AMREX_ARLIM_P(facepairXlo),AMREX_ARLIM_P(facepairXhi),
       Real* facepairY,
       AMREX_ARLIM_P(facepairYlo),AMREX_ARLIM_P(facepairYhi),
       Real* facepairZ,
       AMREX_ARLIM_P(facepairZlo),AMREX_ARLIM_P(facepairZhi),
       Real* facefab,AMREX_ARLIM_P(facefablo),AMREX_ARLIM_P(facefabhi),
       Real* facetest,AMREX_ARLIM_P(facetestlo),AMREX_ARLIM_P(facetesthi),
       Real* stenfab,AMREX_ARLIM_P(stenfablo),AMREX_ARLIM_P(stenfabhi),
       Real* vofrecon,AMREX_ARLIM_P(vofreconlo),AMREX_ARLIM_P(vofreconhi),
       Real* origdist,AMREX_ARLIM_P(origdistlo),AMREX_ARLIM_P(origdisthi),
       Real* newfab,AMREX_ARLIM_P(newfablo),AMREX_ARLIM_P(newfabhi),
       Real* touchfab,AMREX_ARLIM_P(touchfablo),AMREX_ARLIM_P(touchfabhi),
       Real* crsetouch,AMREX_ARLIM_P(crsetouchlo),AMREX_ARLIM_P(crsetouchhi),
       Real* crsedist,AMREX_ARLIM_P(crsedistlo),AMREX_ARLIM_P(crsedisthi),
       const int* tilelo,const int* tilehi,
       const int* fablo,const int* fabhi,
       int* bfact,
       const int* bc,const int* rz_flag,
       const Real* xlo,const Real* dx,
       Real* time,
       int* ngrowLS,
       int* nmat,int* nten,
       int* nstar,
       int* nface,
       int* nface_dst);

     void FORT_CORRECT_UNINIT(
       Real* minLS,
       Real* maxLS,
       Real* max_problen,
       int* level,
       int* finest_level,
       Real* newfab,AMREX_ARLIM_P(newfablo),AMREX_ARLIM_P(newfabhi),
       Real* touchfab,AMREX_ARLIM_P(touchfablo),AMREX_ARLIM_P(touchfabhi),
       const int* tilelo,const int* tilehi,
       const int* fablo,const int* fabhi,
       int* bfact,
       const Real* xlo,const Real* dx,
       Real* time,
       int* nmat);

     void FORT_STENINIT(
       int* level,int* finest_level,
       Real* stenfab,AMREX_ARLIM_P(stenfablo),AMREX_ARLIM_P(stenfabhi),
       Real* mask,AMREX_ARLIM_P(masklo),AMREX_ARLIM_P(maskhi),
       Real* vofrecon,AMREX_ARLIM_P(vofreconlo),AMREX_ARLIM_P(vofreconhi),
       const int* tilelo,const int* tilehi,
       const int* fablo,const int* fabhi,
       int* bfact,
       const int* rz_flag,
       const Real* xlo,const Real* dx,
       Real* time,int* ngrowLS,
       int* nmat,int* nstar);


     void FORT_FACEINIT(
       int* tid,
       int* tessellate,
       int* nten,
       int* level,
       int* finest_level,
       Real* facefab,AMREX_ARLIM_P(facefablo),AMREX_ARLIM_P(facefabhi),
       Real* maskfab,AMREX_ARLIM_P(maskfablo),AMREX_ARLIM_P(maskfabhi),
       Real* vofrecon,AMREX_ARLIM_P(vofreconlo),AMREX_ARLIM_P(vofreconhi),
       const int* tilelo,const int* tilehi,
       const int* fablo,const int* fabhi,
       int* bfact,
       const int* rz_flag,
       const Real* xlo,const Real* dx,
       Real* time,
       int* ngrow,
       int* nmat,
       int* nface,
       int* nface_decomp);

     void FORT_FACEINITTEST(
       int* tid,
       int* tessellate,
       int* level,
       int* finest_level,
       Real* facefab,AMREX_ARLIM_P(facefablo),AMREX_ARLIM_P(facefabhi),
       Real* facetest,AMREX_ARLIM_P(facetestlo),AMREX_ARLIM_P(facetesthi),
       Real* maskfab,AMREX_ARLIM_P(maskfablo),AMREX_ARLIM_P(maskfabhi),
       Real* vofrecon,AMREX_ARLIM_P(vofreconlo),AMREX_ARLIM_P(vofreconhi),
       const int* tilelo,const int* tilehi,
       const int* fablo,const int* fabhi,
       int* bfact,
       const int* rz_flag,
       const Real* xlo,const Real* dx,
       Real* time,
       int* ngrow,
       int* nmat,
       int* nface);

     void FORT_FACEPROCESS(
       int* ngrow_source,
       int* ngrow_dest,
       int* tid,
       int* dir,
       int* tessellate,
       int* level,
       int* finest_level,
       Real* dstfab,AMREX_ARLIM_P(dstfablo),AMREX_ARLIM_P(dstfabhi),
       Real* facefab,AMREX_ARLIM_P(facefablo),AMREX_ARLIM_P(facefabhi),
       Real* vofrecon,AMREX_ARLIM_P(vofreconlo),AMREX_ARLIM_P(vofreconhi),
       const int* tilelo,const int* tilehi,
       const int* fablo,const int* fabhi,
       int* bfact,
       const int* rz_flag,
       const Real* xlo,const Real* dx,
       Real* time,
       int* nmat,int* nface_src,int* nface_dst);

  };

}/* namespace amrex */

#endif

#endif

