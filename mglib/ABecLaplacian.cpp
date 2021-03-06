//#include <winstd.H>
#if defined(BL_OLD_STL)
#include <stdlib.h>
#else
#include <cstdlib>
#endif

#include <algorithm>

#include <AMReX_ParmParse.H>
#include <AMReX_Utility.H>
#include <AMReX_ParallelDescriptor.H>

#include <ABecLaplacian.H>
#include <LO_F.H>
#include <ABec_F.H>
#include <CG_F.H>
#include <MG_F.H>
#include <Zeyu_Matrix_Functions.H>

#define SCALAR_WORK_NCOMP 9

namespace amrex{

#define profile_solver 0

int ABecLaplacian::abec_use_bicgstab = 0;

int ABecLaplacian::mglib_blocking_factor = 2;

int ABecLaplacian::gmres_max_iter = 64;
int ABecLaplacian::gmres_precond_iter_base_mg = 4;
int ABecLaplacian::nghostRHS=0;
int ABecLaplacian::nghostSOLN=1;

Real ABecLaplacian::a_def     = 0.0;
Real ABecLaplacian::b_def     = 1.0;

int ABecLaplacian::CG_def_maxiter = 200;
int ABecLaplacian::CG_def_verbose = 0;

int ABecLaplacian::MG_def_nu_0         = 1;
int ABecLaplacian::MG_def_nu_f         = 8;
int ABecLaplacian::MG_def_verbose      = 0;
int ABecLaplacian::MG_def_nu_b         = 0;

extern void GMRES_MIN_CPP(Real** HH,Real beta, Real* yy,
 int m,int m_small,
 int caller_id,int project_option,
 int mg_level,int& status);

static
void
Spacer (std::ostream& os, int lev)
{
 for (int k = 0; k < lev; k++) {
  os << "   ";
 }
}

// level==0 is the finest level
void
ABecLaplacian::apply (MultiFab& out,MultiFab& in,
  int level,MultiFab& pbdry,Vector<int> bcpres_array) {

    applyBC(in,level,pbdry,bcpres_array);
    Fapply(out,in,level);
}


// level==0 is the finest level
void
ABecLaplacian::applyBC (MultiFab& inout,int level,
       MultiFab& pbdry,Vector<int> bcpres_array) {

#if (profile_solver==1)
 std::string subname="ABecLaplacian::applyBC";
 std::stringstream popt_string_stream(std::stringstream::in |
      std::stringstream::out);
 popt_string_stream << cfd_project_option;
 std::string profname=subname+popt_string_stream.str();
 profname=profname+"_";
 std::stringstream lev_string_stream(std::stringstream::in |
      std::stringstream::out);
 lev_string_stream << level;
 profname=profname+lev_string_stream.str();

 BLProfiler bprof(profname);

 bprof.stop();
#endif

#if (profile_solver==1)
 bprof.start();
#endif

 int bfact=bfact_array[level];
 int bfact_top=bfact_array[0];

 if (inout.nGrow()!=1) {
  std::cout << "inout ngrow= " << inout.nGrow() << '\n';
  std::cout << "level= " << level << '\n';
  std::cout << "bfact_top= " << bfact_top << '\n';
  amrex::Error("inout ngrow<>1");
 }
 if (pbdry.nGrow()!=1)
  amrex::Error("pbdry ngrow<>1");
 
 if (inout.nComp()!=nsolve_bicgstab)
  amrex::Error("inout.nComp invalid");
 if (pbdry.nComp()!=nsolve_bicgstab)
  amrex::Error("pbdry.nComp invalid");

 inout.FillBoundary(geomarray[level].periodicity());

  //
  // Fill boundary cells.
  //

 if (bcpres_array.size()!=gbox[0].size()*AMREX_SPACEDIM*2*nsolve_bicgstab)
  amrex::Error("bcpres_array size invalid");

 if (maskvals[level]->nGrow()!=1)
  amrex::Error("maskvals invalid ngrow");

#if (profile_solver==1)
 bprof.stop();
#endif

 if (thread_class::nthreads<1)
  amrex::Error("thread_class::nthreads invalid");
 thread_class::init_d_numPts(inout.boxArray().d_numPts());

   // if openmp and no tiling, then tilegrid=validbox
   // and the grids are distributed amongst the threads.
#ifdef _OPENMP
#pragma omp parallel
#endif
{

  // MFIter declared in AMReX_MFIter.H
  // do_tiling=false
 for (MFIter mfi(inout,false); mfi.isValid(); ++mfi) {

#if (profile_solver==1)
  std::string subname="APPLYBC";
  std::stringstream popt_string_stream(std::stringstream::in |
      std::stringstream::out);
  popt_string_stream << cfd_project_option;
  std::string profname=subname+popt_string_stream.str();
  profname=profname+"_";
  std::stringstream lev_string_stream(std::stringstream::in |
      std::stringstream::out);
  lev_string_stream << level;
  profname=profname+lev_string_stream.str();

  BLProfiler bprof(profname);
#endif

  BL_ASSERT(gbox[level][mfi.index()] == mfi.validbox());
  const int gridno = mfi.index();
  const Box& tilegrid=mfi.tilebox(); 
  const Box& fabgrid=gbox[level][gridno];
  const int* tilelo=tilegrid.loVect();
  const int* tilehi=tilegrid.hiVect();
  const int* fablo=fabgrid.loVect();
  const int* fabhi=fabgrid.hiVect();        

  Vector<int> bcpres;
  bcpres.resize(2*AMREX_SPACEDIM*nsolve_bicgstab);
  int ibase=2*AMREX_SPACEDIM*gridno*nsolve_bicgstab;
  for (int i=0;i<2*AMREX_SPACEDIM*nsolve_bicgstab;i++)
   bcpres[i]=bcpres_array[i+ibase];
  FArrayBox& mfab=(*maskvals[level])[gridno];
  FArrayBox& bfab=pbdry[gridno];

  int tid_current=0;
#ifdef _OPENMP
  tid_current = omp_get_thread_num();
#endif
  if ((tid_current>=0)&&(tid_current<thread_class::nthreads)) {
   // do nothing
  } else
   amrex::Error("tid_current invalid");

  thread_class::tile_d_numPts[tid_current]+=tilegrid.d_numPts();

  FORT_APPLYBC( 
   &nsolve_bicgstab,
   inout[gridno].dataPtr(),
   ARLIM(inout[gridno].loVect()),ARLIM(inout[gridno].hiVect()),
   bfab.dataPtr(),ARLIM(bfab.loVect()),ARLIM(bfab.hiVect()),
   mfab.dataPtr(),ARLIM(mfab.loVect()),ARLIM(mfab.hiVect()),
   bcpres.dataPtr(),
   tilelo,tilehi,
   fablo,fabhi,&bfact,&bfact_top);

#if (profile_solver==1)
  bprof.stop();
#endif
 }  // mfi
} // omp
 
 thread_class::sync_tile_d_numPts();
 ParallelDescriptor::ReduceRealSum(thread_class::tile_d_numPts[0]);
 thread_class::reconcile_d_numPts(2);
}


void
ABecLaplacian::residual (MultiFab& residL,MultiFab& rhsL,
  MultiFab& solnL,
  int level,
  MultiFab& pbdry,Vector<int> bcpres_array) {

#if (profile_solver==1)
 std::string subname="ABecLaplacian::residual";
 std::stringstream popt_string_stream(std::stringstream::in |
   std::stringstream::out);
 popt_string_stream << cfd_project_option;
 std::string profname=subname+popt_string_stream.str();
 profname=profname+"_";
 std::stringstream lev_string_stream(std::stringstream::in |
   std::stringstream::out);
 lev_string_stream << level;
 profname=profname+lev_string_stream.str();

 BLProfiler bprof(profname);
#endif

 int mg_coarsest_level=MG_numlevels_var-1;

 bool use_tiling=cfd_tiling;

 if (rhsL.boxArray()==laplacian_ones[level]->boxArray()) {
  // do nothing
 } else
  amrex::Error("rhsL.boxArray()!=laplacian_ones[level]->boxArray()");

 if (laplacian_ones[level]->nComp()==1) {
  // do nothing
 } else
  amrex::Error("laplacian_ones[level]->nComp()!=1");

#if (profile_solver==1)
 bprof.stop();
#endif

 if (residL.nGrow()!=0)
  amrex::Error("residL invalid ngrow");
  
 apply(residL,solnL,level,pbdry,bcpres_array);
 Fdiagsum(*MG_CG_diagsumL[level],level);
 int bfact=bfact_array[level];
 int bfact_top=bfact_array[0];

 if (thread_class::nthreads<1)
  amrex::Error("thread_class::nthreads invalid");
 thread_class::init_d_numPts(solnL.boxArray().d_numPts());

#ifdef _OPENMP
#pragma omp parallel
#endif
{
  for (MFIter mfi(solnL,use_tiling); mfi.isValid(); ++mfi) {

#if (profile_solver==1)
   std::string subname="RESIDL";
   std::stringstream popt_string_stream(std::stringstream::in |
      std::stringstream::out);
   popt_string_stream << cfd_project_option;
   std::string profname=subname+popt_string_stream.str();
   profname=profname+"_";
   std::stringstream lev_string_stream(std::stringstream::in |
      std::stringstream::out);
   lev_string_stream << level;
   profname=profname+lev_string_stream.str();

   BLProfiler bprof(profname);
#endif

   int nc = residL.nComp();
   if (nc!=nsolve_bicgstab)
    amrex::Error("nc invalid in residual");
   BL_ASSERT(gbox[level][mfi.index()] == mfi.validbox());
   const int gridno = mfi.index();
   const Box& tilegrid=mfi.tilebox();
   const Box& fabgrid=gbox[level][gridno];
   const int* tilelo=tilegrid.loVect();
   const int* tilehi=tilegrid.hiVect();
   const int* fablo=fabgrid.loVect();
   const int* fabhi=fabgrid.hiVect();

   FArrayBox& ones_FAB=(*laplacian_ones[level])[mfi];

   int tid_current=0;
#ifdef _OPENMP
   tid_current = omp_get_thread_num();
#endif
   if ((tid_current>=0)&&(tid_current<thread_class::nthreads)) {
    // do nothing
   } else
    amrex::Error("tid_current invalid");

   thread_class::tile_d_numPts[tid_current]+=tilegrid.d_numPts();

     // in: LO_3D.F90
   FORT_RESIDL(
    &level,
    &mg_coarsest_level,
    &nsolve_bicgstab,
    ones_FAB.dataPtr(), 
    ARLIM(ones_FAB.loVect()),ARLIM(ones_FAB.hiVect()),
    residL[mfi].dataPtr(), 
    ARLIM(residL[mfi].loVect()),ARLIM(residL[mfi].hiVect()),
    rhsL[mfi].dataPtr(), 
    ARLIM(rhsL[mfi].loVect()),ARLIM(rhsL[mfi].hiVect()),
    residL[mfi].dataPtr(), 
    ARLIM(residL[mfi].loVect()),ARLIM(residL[mfi].hiVect()),
    tilelo,tilehi,
    fablo,fabhi,&bfact,&bfact_top); 

     // mask off the residual if the off diagonal entries sum to 0.
   FArrayBox& diagfab=(*MG_CG_diagsumL[level])[mfi];
   FArrayBox& residfab=residL[mfi];
   residfab.mult(diagfab,tilegrid,0,0,nsolve_bicgstab); 

#if (profile_solver==1)
   bprof.stop();
#endif
  } // mfi
} // omp
 thread_class::sync_tile_d_numPts();
 ParallelDescriptor::ReduceRealSum(thread_class::tile_d_numPts[0]);
 thread_class::reconcile_d_numPts(3);

}

void
ABecLaplacian::smooth(MultiFab& solnL,MultiFab& rhsL,
  int level,MultiFab& pbdry,Vector<int> bcpres_array,
  int smooth_type) {

    int nc = solnL.nComp();
    if (nc!=nsolve_bicgstab)
     amrex::Error("nc invalid in smooth");
    int ngrow_soln=solnL.nGrow();
    int ngrow_rhs=rhsL.nGrow();
    if (ngrow_soln<1)
     amrex::Error("ngrow_soln invalid");
    if (ngrow_rhs!=0)
     amrex::Error("ngrow_rhs invalid");

    applyBC(solnL,level,pbdry,bcpres_array);
    Fsmooth(solnL, rhsL, level, smooth_type);

}

// L2 norm
Real
ABecLaplacian::LPnorm(MultiFab &in, int level) const
{


#if (profile_solver==1)
 std::string subname="ABecLaplacian::norm";
 std::stringstream popt_string_stream(std::stringstream::in |
    std::stringstream::out);
 popt_string_stream << cfd_project_option;
 std::string profname=subname+popt_string_stream.str();
 profname=profname+"_";
 std::stringstream lev_string_stream(std::stringstream::in |
    std::stringstream::out);
 lev_string_stream << level;
 profname=profname+lev_string_stream.str();

 BLProfiler bprof(profname);
#endif

 int nc = in.nComp();
 if (nc!=nsolve_bicgstab)
  amrex::Error("nc invalid in norm");

 Real mf_norm=0.0;
 for (int n=0;n<nc;n++) {
  Real test_norm=in.norm2(n);
  test_norm*=test_norm;
  mf_norm+=test_norm;
 }

#if (profile_solver==1)
 bprof.stop();
#endif

 return mf_norm;
}

// level==0 is the finest level
// level is the coarse level
// level-1 is the fine level
// avg=0  just sum
// avg=1  take avg
// avg=2  this is the ones_mf variable
void
ABecLaplacian::makeCoefficients (
	MultiFab& crs,
        const MultiFab& fine,
        int             level,
        int             avg)
{

  //
  // Determine index type of incoming MultiFab.
  //
 const IndexType iType(fine.boxArray()[0].ixType());

 const IndexType cType(D_DECL(IndexType::CELL,IndexType::CELL,IndexType::CELL));
 const IndexType xType(D_DECL(IndexType::NODE,IndexType::CELL,IndexType::CELL));
 const IndexType yType(D_DECL(IndexType::CELL,IndexType::NODE,IndexType::CELL));
 const IndexType zType(D_DECL(IndexType::CELL,IndexType::CELL,IndexType::NODE));

 int cdir;
 if (iType == cType) {
  cdir = -1;
 } else if (iType == xType) {
  cdir = 0;
 } else if (iType == yType) {
  cdir = 1;
 } else if ((iType == zType)&&(AMREX_SPACEDIM==3)) {
  cdir = 2;
 } else {
  amrex::Error("ABecLaplacian::makeCoeffients: Bad index type");
 }

#if (profile_solver==1)
 std::string subname="ABecLaplacian::makeCoefficients";
 std::stringstream popt_string_stream(std::stringstream::in |
     std::stringstream::out);
 popt_string_stream << cfd_project_option;
 std::string profname=subname+popt_string_stream.str();
 profname=profname+"_";
 std::stringstream lev_string_stream(std::stringstream::in |
     std::stringstream::out);
 lev_string_stream << level;
 profname=profname+lev_string_stream.str();
 profname=profname+"_";
 std::stringstream cdir_string_stream(std::stringstream::in |
     std::stringstream::out);
 cdir_string_stream << cdir;
 profname=profname+cdir_string_stream.str();

 BLProfiler bprof(profname);
#endif

 if (level>0) {
  // do nothing
 } else
  amrex::Error("level invalid");

 int flevel=level-1;
 int clevel=level;
 int bfact_coarse=bfact_array[clevel];
 int bfact_fine=bfact_array[flevel];
 int bfact_top=bfact_array[0];

 int nComp_expect = nsolve_bicgstab;
 if ((avg==0)||(avg==1)) {
  // do nothing
 } else if (avg==2) { // ones_mf variable
  nComp_expect=1;
 } else
  amrex::Error("avg invalid");

 if (nComp_expect==fine.nComp()) {
  // do nothing
 } else
  amrex::Error("nComp_expect!=fine.nComp()");

 BoxArray d(gbox[level]);
 if ((cdir>=0)&&(cdir<AMREX_SPACEDIM)) {
  d.surroundingNodes(cdir);
 } else if (cdir==-1) {
  // do nothing
 } else
  amrex::Error("cdir invalid");

   //
   // Only single-component solves supported (verified) by this class.
   //
 int nGrow=fine.nGrow();

 int ngrow_expect=0;

 if ((avg==0)||(avg==1)) {
  ngrow_expect=0;
 } else if (avg==2) { // ones_mf variable
  ngrow_expect=1;
  if (cdir==-1) {
   // do nothing
  } else
   amrex::Error("cdir invalid");
 } else
  amrex::Error("avg invalid");

 if (nGrow==ngrow_expect) {
  // do nothing
 } else
  amrex::Error("ngrow invalid in makecoeff");

 if (crs.boxArray()==d) {
  // do nothing
 } else
  amrex::Error("crs.boxArray() invalid");

 if (crs.nComp()==nComp_expect) {
  // do nothing
 } else
  amrex::Error("crs.nComp() invalid");

 if (crs.nGrow()==nGrow) {
  // do nothing
 } else
  amrex::Error("crs.nGrow() invalid");

 if ((avg==0)||(avg==1)) {
  crs.setVal(0.0,0,nComp_expect,nGrow); 
 } else if (avg==2) { // ones_mf variable
  crs.setVal(1.0,0,nComp_expect,nGrow); 
 } else
  amrex::Error("avg invalid");

 const BoxArray& grids = gbox[level]; // coarse grids

#if (profile_solver==1)
 bprof.stop();
#endif

 if (thread_class::nthreads<1)
  amrex::Error("thread_class::nthreads invalid");
 thread_class::init_d_numPts(crs.boxArray().d_numPts());

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(crs,false); mfi.isValid();++mfi) {

#if (profile_solver==1)
  std::string subname="AVERAGE_CC_or_EC";
  std::stringstream popt_string_stream(std::stringstream::in |
      std::stringstream::out);
  popt_string_stream << cfd_project_option;
  std::string profname=subname+popt_string_stream.str();
  profname=profname+"_";
  std::stringstream lev_string_stream(std::stringstream::in |
      std::stringstream::out);
  lev_string_stream << level;
  profname=profname+lev_string_stream.str();
  profname=profname+"_";
  std::stringstream cdir_string_stream(std::stringstream::in |
      std::stringstream::out);
  cdir_string_stream << cdir;
  profname=profname+cdir_string_stream.str();

  BLProfiler bprof(profname);
#endif

  const Box& tilegrid=mfi.tilebox();
  const Box& fabgrid=grids[mfi.index()];
  const int* fablo=fabgrid.loVect();
  const int* fabhi=fabgrid.hiVect();
 
  int tid_current=0;
#ifdef _OPENMP
  tid_current = omp_get_thread_num();
#endif
  if ((tid_current>=0)&&(tid_current<thread_class::nthreads)) {
   // do nothing
  } else
   amrex::Error("tid_current invalid");

  thread_class::tile_d_numPts[tid_current]+=tilegrid.d_numPts();

  if (cdir==-1) {
   FORT_AVERAGECC(
     &nsolve_bicgstab,
     &nComp_expect,
     crs[mfi].dataPtr(), 
     ARLIM(crs[mfi].loVect()),
     ARLIM(crs[mfi].hiVect()),
     fine[mfi].dataPtr(),
     ARLIM(fine[mfi].loVect()),
     ARLIM(fine[mfi].hiVect()),
     fablo,fabhi,
     &avg,
     &nGrow,
     &bfact_coarse,&bfact_fine,&bfact_top);
  } else if ((cdir>=0)&&(cdir<AMREX_SPACEDIM)) {
   FORT_AVERAGEEC(
     &nComp_expect,
     crs[mfi].dataPtr(), 
     ARLIM(crs[mfi].loVect()),
     ARLIM(crs[mfi].hiVect()),
     fine[mfi].dataPtr(), 
     ARLIM(fine[mfi].loVect()),
     ARLIM(fine[mfi].hiVect()),
     fablo,fabhi,
     &cdir,&avg,
     &bfact_coarse,&bfact_fine,&bfact_top);
  } else
   amrex::Error("ABecLaplacian:: bad coefficient coarsening direction!");

#if (profile_solver==1)
  bprof.stop();
#endif
 }  // mfi
} //omp

 thread_class::sync_tile_d_numPts();
 ParallelDescriptor::ReduceRealSum(thread_class::tile_d_numPts[0]);
 thread_class::reconcile_d_numPts(4);

 if ((avg==0)||(avg==1)) {
  // do nothing
 } else if (avg==2) { // ones_mf variable
  crs.FillBoundary(geomarray[level].periodicity());
 } else
  amrex::Error("avg invalid");

} // subroutine makeCoefficients


// level==0 is the finest level
void 
ABecLaplacian::buildMatrix() {

#if (profile_solver==1)
 std::string subname="ABecLaplacian::buildMatrix";
 std::stringstream popt_string_stream(std::stringstream::in |
    std::stringstream::out);
 popt_string_stream << cfd_project_option;
 std::string profname=subname+popt_string_stream.str();

 BLProfiler bprof(profname);
 bprof.stop();
#endif

 bool use_tiling=cfd_tiling;
 use_tiling=false;  // two different tile instances might mod the same data.

 int ncwork=AMREX_SPACEDIM*3+SCALAR_WORK_NCOMP;

 for (int level=0;level<MG_numlevels_var;level++) {
#if (profile_solver==1)
  bprof.start();
#endif

  if ((level>=1)&&(level<MG_numlevels_var)) {

   // remark: in the multigrid, average==1 too, so that one has:
   //  rhs[level-1] ~ volume_fine div u/dt
   //  rhs[level] ~ volume_coarse div u/dt  (1/2^d)
   //
   // divide by 4 in 2D and 8 in 3D
   // acoefs[level-1] ~ volume_fine
   // acoefs[level] ~ volume_coarse/ 2^d
   int avg=1;
   makeCoefficients(*acoefs[level],*acoefs[level-1],level,avg);

   avg=2;
   makeCoefficients(*laplacian_ones[level],*laplacian_ones[level-1],level,avg);

   for (int dir = 0; dir < AMREX_SPACEDIM; ++dir) {
    // divide by 2 in 2D and 4 in 3D.
    // bcoefs[level-1] ~ area_fine/dx_fine  
    // bcoefs[level] ~ (area_coarse/dx_fine)/2^(d-1) =
    //                 4(area_coarse/dx_coarse)/2^d
    avg=1;
    makeCoefficients(*bcoefs[level][dir],*bcoefs[level-1][dir],level,avg);
    MultiFab& bmf=*bcoefs[level][dir];

    int ncomp=bmf.nComp();
    if (ncomp!=nsolve_bicgstab)
     amrex::Error("ncomp invalid");

    int ngrow=bmf.nGrow();
    if (ngrow!=0)
     amrex::Error("bcoefs should have ngrow=0");

     // after this step: bcoefs[level] ~ (area_coarse/dx_coarse)/2^d
    bmf.mult(0.25);
   } // dir=0..sdim-1

   Real denom=0.0;
   if (AMREX_SPACEDIM==2) {
    denom=0.25;
   } else if (AMREX_SPACEDIM==3) {
    denom=0.125;
   } else
    amrex::Error("dimension bust");

  } else if (level==0) {
   // do nothing
  } else
   amrex::Error("level invalid");

  if (acoefs[level]->nGrow()!=nghostRHS)
   amrex::Error("acoefs[level]->nGrow() invalid");

  if (workcoefs[level]->nComp()==ncwork*nsolve_bicgstab) {
   // do nothing
  } else
   amrex::Error("workcoefs[level]->nComp() invalid");

  if (workcoefs[level]->nGrow()==nghostSOLN) {
   // do nothing
  } else 
   amrex::Error("workcoefs[level]->nGrow() invalid");

  workcoefs[level]->setVal(0.0,0,ncwork*nsolve_bicgstab,nghostSOLN);

  int bxleftcomp=0;
  int byleftcomp=bxleftcomp+1;
  int bzleftcomp=byleftcomp+AMREX_SPACEDIM-2;
  int bxrightcomp=bzleftcomp+1;
  int byrightcomp=bxrightcomp+1;
  int bzrightcomp=byrightcomp+AMREX_SPACEDIM-2;
  int icbxcomp=bzrightcomp+1;
  int icbycomp=icbxcomp+1;
  int icbzcomp=icbycomp+AMREX_SPACEDIM-2;

  int diag_comp=icbzcomp+1;
  int maskcomp=diag_comp+1;

  int icdiagcomp=maskcomp+1;
  int icdiagrbcomp=icdiagcomp+1;
  int axcomp=icdiagrbcomp+1;
  int solnsavecomp=axcomp+1;
  int rhssavecomp=solnsavecomp+1;
  int redsolncomp=rhssavecomp+1;
  int blacksolncomp=redsolncomp+1;

  if (workcoefs[level]->nComp()<=blacksolncomp*nsolve_bicgstab)
   amrex::Error("workcoefs[level] invalid");

  int bfact=bfact_array[level];
  int bfact_top=bfact_array[0];

#if (profile_solver==1)
  bprof.stop();
#endif

  for (int isweep=0;isweep<4;isweep++) {
   for (int veldir=0;veldir<nsolve_bicgstab;veldir++) {

    if (thread_class::nthreads<1)
     amrex::Error("thread_class::nthreads invalid");
    thread_class::init_d_numPts(workcoefs[level]->boxArray().d_numPts());

#ifdef _OPENMP
#pragma omp parallel
#endif
{
    for (MFIter mfi(*workcoefs[level],use_tiling); mfi.isValid(); ++mfi) {

#if (profile_solver==1)
     std::string subname="BUILDMAT";
     std::stringstream popt_string_stream(std::stringstream::in |
      std::stringstream::out);
     popt_string_stream << cfd_project_option;
     std::string profname=subname+popt_string_stream.str();
     profname=profname+"_";
     std::stringstream lev_string_stream(std::stringstream::in |
       std::stringstream::out);
     lev_string_stream << level;
     profname=profname+lev_string_stream.str();
     profname=profname+"_";
     std::stringstream isweep_string_stream(std::stringstream::in |
      std::stringstream::out);
     isweep_string_stream << isweep;
     profname=profname+isweep_string_stream.str();
     profname=profname+"_";
     std::stringstream veldir_string_stream(std::stringstream::in |
      std::stringstream::out);
     veldir_string_stream << veldir;
     profname=profname+veldir_string_stream.str();

     BLProfiler bprof(profname);
#endif

     BL_ASSERT(gbox[level][mfi.index()] == mfi.validbox());
     const int gridno = mfi.index();
     const Box& tilegrid=mfi.tilebox();
     const Box& fabgrid=gbox[level][gridno];
     const int* tilelo=tilegrid.loVect();
     const int* tilehi=tilegrid.hiVect();
     const int* fablo=fabgrid.loVect();
     const int* fabhi=fabgrid.hiVect();

     int ofs=veldir*ncwork;

     FArrayBox& workFAB=(*workcoefs[level])[mfi];
     FArrayBox& onesFAB=(*laplacian_ones[level])[mfi];
     FArrayBox& aFAB=(*acoefs[level])[mfi];
     FArrayBox& bxFAB=(*bcoefs[level][0])[mfi];
     FArrayBox& byFAB=(*bcoefs[level][1])[mfi];
     FArrayBox& bzFAB=(*bcoefs[level][AMREX_SPACEDIM-1])[mfi];

     int tid_current=0;
#ifdef _OPENMP
     tid_current = omp_get_thread_num();
#endif
     if ((tid_current>=0)&&(tid_current<thread_class::nthreads)) {
      // do nothing
     } else
      amrex::Error("tid_current invalid");

     thread_class::tile_d_numPts[tid_current]+=tilegrid.d_numPts();

      // in: LO_3D.F90
     FORT_BUILDMAT(
      &level, // level==0 is finest
      &veldir,
      &nsolve_bicgstab,
      &isweep,
      onesFAB.dataPtr(),
      ARLIM(onesFAB.loVect()),ARLIM(onesFAB.hiVect()),
      aFAB.dataPtr(veldir),
      ARLIM(aFAB.loVect()),ARLIM(aFAB.hiVect()),
      bxFAB.dataPtr(veldir),
      ARLIM(bxFAB.loVect()),ARLIM(bxFAB.hiVect()),
      byFAB.dataPtr(veldir),
      ARLIM(byFAB.loVect()),ARLIM(byFAB.hiVect()),
      bzFAB.dataPtr(veldir),
      ARLIM(bzFAB.loVect()),ARLIM(bzFAB.hiVect()),
      workFAB.dataPtr(diag_comp+ofs),
      ARLIM(workFAB.loVect()),
      ARLIM(workFAB.hiVect()),
      workFAB.dataPtr(bxleftcomp+ofs),
      workFAB.dataPtr(bxrightcomp+ofs), 
      workFAB.dataPtr(byleftcomp+ofs),
      workFAB.dataPtr(byrightcomp+ofs), 
      workFAB.dataPtr(bzleftcomp+ofs),
      workFAB.dataPtr(bzrightcomp+ofs), 
      workFAB.dataPtr(icbxcomp+ofs), 
      workFAB.dataPtr(icbycomp+ofs), 
      workFAB.dataPtr(icbzcomp+ofs), 
      workFAB.dataPtr(icdiagcomp+ofs), 
      workFAB.dataPtr(icdiagrbcomp+ofs), 
      workFAB.dataPtr(maskcomp+ofs), 
      ARLIM(workFAB.loVect()),
      ARLIM(workFAB.hiVect()),
      tilelo,tilehi,
      fablo,fabhi,&bfact,&bfact_top);

#if (profile_solver==1)
     bprof.stop();
#endif
    } // mfi
} // omp

    thread_class::sync_tile_d_numPts();
    ParallelDescriptor::ReduceRealSum(thread_class::tile_d_numPts[0]);
    thread_class::reconcile_d_numPts(5);

   } // veldir=0...nsolve_bicgstab-1
  } // isweep=0..3

  MG_res[level]->setVal(0.0,0,nsolve_bicgstab,nghostRHS);
  MG_rhs[level]->setVal(0.0,0,nsolve_bicgstab,nghostRHS);
  MG_cor[level]->setVal(0.0,0,nsolve_bicgstab,nghostSOLN);
  MG_pbdrycoarser[level]->setVal(0.0,0,nsolve_bicgstab,nghostSOLN);
  if (level == 0) {
   MG_initialsolution->setVal(0.0,0,nsolve_bicgstab,nghostSOLN);
  }

 } // level=0..MG_numlevels_var-1

} // end subroutine buildMatrix

ABecLaplacian::ABecLaplacian (
 const BoxArray& grids,
 const Geometry& geom,
 const DistributionMapping& dmap,
 int bfact,
 int cfd_level_in,
 int cfd_project_option_in,
 int nsolve_in,
 bool ns_tiling_in,
 int _use_mg_precond_at_top) {

#if (profile_solver==1)
 std::string subname="ABecLaplacian::ABecLaplacian";
 std::stringstream popt_string_stream(std::stringstream::in |
    std::stringstream::out);
 popt_string_stream << cfd_project_option_in;
 std::string profname=subname+popt_string_stream.str();
 profname=profname+"_";
 std::stringstream lev_string_stream(std::stringstream::in |
    std::stringstream::out);
 lev_string_stream << cfd_level_in;
 profname=profname+lev_string_stream.str();

 BLProfiler bprof(profname);
#endif

 CG_use_mg_precond_at_top=_use_mg_precond_at_top;

 if (CG_use_mg_precond_at_top==0) {
  MG_numlevels_var=1;
  CG_numlevels_var=1;
 } else if (CG_use_mg_precond_at_top==1) {
  MG_numlevels_var = MG_numLevels(grids);
  if (MG_numlevels_var==1) {
   CG_numlevels_var=1;
  } else if (MG_numlevels_var>=2) {
   CG_numlevels_var=2;
  } else
   amrex::Error("MG_numlevels_var invalid");

  if (cfd_level_in==0) {
   // do nothing
  } else
   amrex::Error("cfd_level_in invalid");

 } else
  amrex::Error("CG_use_mg_precond_at_top invalid");

 laplacian_solvability=0;

 cfd_level=cfd_level_in;
 cfd_project_option=cfd_project_option_in;
 cfd_tiling=ns_tiling_in;

 CG_maxiter = CG_def_maxiter;
 CG_verbose = CG_def_verbose;

 CG_error_history.resize(CG_maxiter);
 CG_A_error_history.resize(CG_maxiter);
 CG_rAr_error_history.resize(CG_maxiter);
 for (int ehist=0;ehist<CG_error_history.size();ehist++) {
  for (int ih=0;ih<4;ih++) {
   CG_error_history[ehist][ih]=0.0;
   CG_A_error_history[ehist][ih]=0.0;
   CG_rAr_error_history[ehist][ih]=0.0;
  }
 }

 nsolve_bicgstab=nsolve_in; 

 gbox.resize(MG_numlevels_var);
 dmap_array.resize(MG_numlevels_var);
 geomarray.resize(MG_numlevels_var);
 bfact_array.resize(MG_numlevels_var);
 maskvals.resize(MG_numlevels_var);
 acoefs.resize(MG_numlevels_var,(MultiFab*)0);
 bcoefs.resize(MG_numlevels_var);
 for (int lev=0;lev<MG_numlevels_var;lev++) {
  for (int dir=0;dir<AMREX_SPACEDIM;dir++)
   bcoefs[lev][dir]=(MultiFab*)0;
 }
 workcoefs.resize(MG_numlevels_var,(MultiFab*)0);
 laplacian_ones.resize(MG_numlevels_var,(MultiFab*)0);
 MG_CG_diagsumL.resize(MG_numlevels_var,(MultiFab*)0);
 MG_CG_ones_mf_copy.resize(MG_numlevels_var,(MultiFab*)0);

 MG_res.resize(MG_numlevels_var, (MultiFab*)0);
 MG_rhs.resize(MG_numlevels_var, (MultiFab*)0);
 MG_cor.resize(MG_numlevels_var, (MultiFab*)0);
 MG_pbdrycoarser.resize(MG_numlevels_var, (MultiFab*)0);
 MG_CG_diagsumL.resize(MG_numlevels_var, (MultiFab*)0);
 MG_CG_ones_mf_copy.resize(MG_numlevels_var, (MultiFab*)0);

 GMRES_V_MF.resize(gmres_max_iter*nsolve_bicgstab);
 GMRES_U_MF.resize(gmres_max_iter*nsolve_bicgstab);
 GMRES_Z_MF.resize(gmres_max_iter*nsolve_bicgstab);

 for (int coarsefine=0;coarsefine<CG_numlevels_var;coarsefine++) {
  for (int m=0;m<gmres_max_iter*nsolve_bicgstab;m++) {
   GMRES_V_MF[m][coarsefine]=(MultiFab*)0;
   GMRES_U_MF[m][coarsefine]=(MultiFab*)0;
   GMRES_Z_MF[m][coarsefine]=(MultiFab*)0;
  }
  GMRES_W_MF[coarsefine]=(MultiFab*)0;
  CG_delta_sol[coarsefine]=(MultiFab*)0;
  CG_r[coarsefine]=(MultiFab*)0;
  CG_z[coarsefine]=(MultiFab*)0;
  CG_Av_search[coarsefine]=(MultiFab*)0;
  CG_p_search[coarsefine]=(MultiFab*)0;
  CG_p_search_SOLN[coarsefine]=(MultiFab*)0;
  CG_v_search[coarsefine]=(MultiFab*)0;
  CG_rhs_resid_cor_form[coarsefine]=(MultiFab*)0;
  CG_pbdryhom[coarsefine]=(MultiFab*)0;
 } // coarsefine=0,...,CG_numlevels_var-1

 MG_initialsolution=(MultiFab*) 0; 
 
 for (int level=0;level<MG_numlevels_var;level++) {

  if (level==0) {

   gbox[level] = grids;

   dmap_array[level] = dmap;

   geomarray[level] = geom;
   bfact_array[level] = bfact;

   // mask=0 at fine/fine interfaces
   maskvals[level]=new MultiFab(grids,dmap_array[level],1,nghostSOLN,
      MFInfo().SetTag("maskvals level"),FArrayBoxFactory());
   maskvals[level]->setVal(0.0,0,1,nghostSOLN);
   maskvals[level]->setBndry(1.0);
   maskvals[level]->FillBoundary(geom.periodicity());

  } else if (level>0) {

   gbox[level] = gbox[level-1];
   gbox[level].coarsen(2);
   if (gbox[level].size()!=gbox[level-1].size())
    amrex::Error("gbox[level].size()!=gbox[level-1].size()");

   dmap_array[level] = dmap_array[level-1];

   geomarray[level].define(amrex::coarsen(geomarray[level-1].Domain(),2));
   bfact_array[level] = bfact_array[level-1];

   // mask=0 at fine/fine interfaces
   maskvals[level]=new MultiFab(gbox[level],dmap_array[level],1,nghostSOLN,
     MFInfo().SetTag("maskvals level"),FArrayBoxFactory());
   maskvals[level]->setVal(0.0,0,1,nghostSOLN);
   maskvals[level]->setBndry(1.0);
   maskvals[level]->FillBoundary(geomarray[level].periodicity());

  } else
   amrex::Error("level invalid");

  MG_CG_ones_mf_copy[level]=new MultiFab(gbox[level],dmap_array[level],
    1,nghostSOLN,
    MFInfo().SetTag("MG_CG_ones_mf_copy"),FArrayBoxFactory());
  MG_CG_ones_mf_copy[level]->setVal(1.0,0,1,nghostSOLN);

  MG_CG_diagsumL[level]=new MultiFab(gbox[level],dmap_array[level],
	nsolve_bicgstab,nghostRHS,
        MFInfo().SetTag("MG_CG_diagsumL"),FArrayBoxFactory());
  MG_CG_diagsumL[level]->setVal(0.0,0,nsolve_bicgstab,nghostRHS);

  acoefs[level]=new MultiFab(gbox[level],dmap_array[level],
	nsolve_bicgstab,nghostRHS,
        MFInfo().SetTag("acoefs"),FArrayBoxFactory());
  acoefs[level]->setVal(a_def,0,nsolve_bicgstab,nghostRHS);

  int ncomp_work=(AMREX_SPACEDIM*3)+SCALAR_WORK_NCOMP;
  workcoefs[level]=new MultiFab(gbox[level],dmap_array[level],
    ncomp_work*nsolve_bicgstab,nghostSOLN,
    MFInfo().SetTag("workcoefs"),FArrayBoxFactory());
  workcoefs[level]->setVal(0.0,0,ncomp_work*nsolve_bicgstab,nghostSOLN);

  laplacian_ones[level]=new MultiFab(gbox[level],dmap_array[level],
	1,nghostSOLN,
        MFInfo().SetTag("laplacian_ones"),FArrayBoxFactory());
  laplacian_ones[level]->setVal(1.0,0,1,nghostSOLN);

  MG_res[level] = new MultiFab(gbox[level],dmap_array[level],
	 nsolve_bicgstab,nghostRHS,
	 MFInfo().SetTag("MG_res"),FArrayBoxFactory());
  MG_res[level]->setVal(0.0,0,nsolve_bicgstab,nghostRHS);

  MG_rhs[level] = new MultiFab(gbox[level],dmap_array[level],
    nsolve_bicgstab,nghostRHS,
    MFInfo().SetTag("MG_rhs"),FArrayBoxFactory());
  MG_rhs[level]->setVal(0.0,0,nsolve_bicgstab,nghostRHS);

  MG_cor[level] = new MultiFab(gbox[level],dmap_array[level],
    nsolve_bicgstab,nghostSOLN,
    MFInfo().SetTag("MG_cor"),FArrayBoxFactory());
  MG_cor[level]->setVal(0.0,0,nsolve_bicgstab,nghostSOLN);

  MG_pbdrycoarser[level] = 
     new MultiFab(gbox[level],dmap_array[level],
	nsolve_bicgstab,nghostSOLN,
	MFInfo().SetTag("MG_pbdrycoarser"),FArrayBoxFactory());
  MG_pbdrycoarser[level]->setVal(0.0,0,nsolve_bicgstab,nghostSOLN);

  // no ghost cells for edge or node coefficients
  for (int dir = 0; dir < AMREX_SPACEDIM; ++dir) {
   BoxArray edge_boxes(gbox[level]);
   edge_boxes.surroundingNodes(dir);
   bcoefs[level][dir]=new MultiFab(edge_boxes,dmap_array[level],
     nsolve_bicgstab,0,
     MFInfo().SetTag("bcoefs"),FArrayBoxFactory());
   bcoefs[level][dir]->setVal(b_def,0,nsolve_bicgstab,0);
  }

 }  // level=0..MG_numlevels_var-1

 int finest_mg_level=0;

 MG_initialsolution = new MultiFab(gbox[finest_mg_level],
   dmap_array[finest_mg_level],
   nsolve_bicgstab,nghostSOLN,
   MFInfo().SetTag("MG_initialsolution"),FArrayBoxFactory());
 MG_initialsolution->setVal(0.0,0,nsolve_bicgstab,nghostSOLN);

 MG_pbdryhom = new MultiFab(gbox[finest_mg_level],
   dmap_array[finest_mg_level],
   nsolve_bicgstab,nghostSOLN,
   MFInfo().SetTag("MG_pbdryhom"),FArrayBoxFactory());
 MG_pbdryhom->setVal(0.0,0,nsolve_bicgstab,nghostSOLN);

 for (int coarsefine=0;coarsefine<CG_numlevels_var;coarsefine++) {

  int level=0;
  if (coarsefine==0) {
   level=0;
  } else if ((coarsefine==1)&&(coarsefine<CG_numlevels_var)) {
   level=MG_numlevels_var-1;
  } else
   amrex::Error("coarsefine invalid");

  for (int j=0;j<gmres_precond_iter_base_mg*nsolve_bicgstab;j++) {
   GMRES_V_MF[j][coarsefine]=new MultiFab(gbox[level],dmap_array[level],
    nsolve_bicgstab,nghostRHS,
    MFInfo().SetTag("GMRES_V_MF"),FArrayBoxFactory()); 
   GMRES_U_MF[j][coarsefine]=new MultiFab(gbox[level],dmap_array[level],
    nsolve_bicgstab,nghostRHS,
    MFInfo().SetTag("GMRES_U_MF"),FArrayBoxFactory()); 
   GMRES_Z_MF[j][coarsefine]=new MultiFab(gbox[level],dmap_array[level],
    nsolve_bicgstab,nghostSOLN,
    MFInfo().SetTag("GMRES_Z_MF"),FArrayBoxFactory()); 

   GMRES_V_MF[j][coarsefine]->setVal(0.0,0,nsolve_bicgstab,nghostRHS);
   GMRES_U_MF[j][coarsefine]->setVal(0.0,0,nsolve_bicgstab,nghostRHS);
   GMRES_Z_MF[j][coarsefine]->setVal(0.0,0,nsolve_bicgstab,nghostSOLN);
  } //j=0;j<gmres_precond_iter_base_mg*nsolve_bicgstab
  GMRES_W_MF[coarsefine]=new MultiFab(gbox[level],dmap_array[level],
   nsolve_bicgstab,nghostRHS,
   MFInfo().SetTag("GMRES_W_MF"),FArrayBoxFactory());

  CG_delta_sol[coarsefine]=new MultiFab(gbox[level],dmap_array[level],
    nsolve_bicgstab,nghostSOLN,
    MFInfo().SetTag("CG_delta_sol"),FArrayBoxFactory());
  CG_r[coarsefine]=new MultiFab(gbox[level],dmap_array[level],
    nsolve_bicgstab,nghostRHS,
    MFInfo().SetTag("CG_r"),FArrayBoxFactory());
  CG_z[coarsefine]=new MultiFab(gbox[level],dmap_array[level],
    nsolve_bicgstab,nghostSOLN,
    MFInfo().SetTag("CG_z"),FArrayBoxFactory());
  CG_Av_search[coarsefine]=new MultiFab(gbox[level],dmap_array[level],
    nsolve_bicgstab,nghostRHS,
    MFInfo().SetTag("CG_Av_search"),FArrayBoxFactory());
  CG_p_search[coarsefine]=new MultiFab(gbox[level],dmap_array[level],
    nsolve_bicgstab,nghostRHS,
    MFInfo().SetTag("CG_p_search"),FArrayBoxFactory());
  CG_p_search_SOLN[coarsefine]=new MultiFab(gbox[level],dmap_array[level],
    nsolve_bicgstab,nghostSOLN,
    MFInfo().SetTag("CG_p_search_SOLN"),FArrayBoxFactory());
  CG_v_search[coarsefine]=new MultiFab(gbox[level],dmap_array[level],
    nsolve_bicgstab,nghostRHS,
    MFInfo().SetTag("CG_v_search"),FArrayBoxFactory());
  CG_rhs_resid_cor_form[coarsefine]=new MultiFab(gbox[level],dmap_array[level],
    nsolve_bicgstab,nghostRHS,
    MFInfo().SetTag("CG_rhs_resid_cor_form"),FArrayBoxFactory());
  CG_pbdryhom[coarsefine]=new MultiFab(gbox[level],dmap_array[level],
    nsolve_bicgstab,nghostSOLN,
    MFInfo().SetTag("CG_pbdryhom"),FArrayBoxFactory());

 } // coarsefine=0..CG_numlevels_var-1


 ParmParse ppcg("cg");

 ppcg.query("abec_use_bicgstab", abec_use_bicgstab);
 ppcg.query("mglib_blocking_factor", mglib_blocking_factor);

 ppcg.query("maxiter", CG_def_maxiter);
 ppcg.query("v", CG_def_verbose);
 ppcg.query("verbose", CG_def_verbose);

 if (ParallelDescriptor::IOProcessor() && CG_def_verbose) {
  std::cout << "CGSolver settings...\n";
  std::cout << "   CG_def_maxiter   = " << CG_def_maxiter << '\n';
 }
    
 ParmParse ppmg("mg");
 ParmParse ppLp("Lp");

 ppmg.query("nu_0", MG_def_nu_0);
 ppmg.query("nu_f", MG_def_nu_f);
 ppmg.query("v", MG_def_verbose);
 ppmg.query("verbose", MG_def_verbose);
 ppmg.query("nu_b", MG_def_nu_b);

 if ((ParallelDescriptor::IOProcessor())&&(MG_def_verbose)) {
  std::cout << "MultiGrid settings...\n";
  std::cout << "   def_nu_0 =         " << MG_def_nu_0         << '\n';
  std::cout << "   def_nu_f =         " << MG_def_nu_f         << '\n';
  std::cout << "   def_nu_b =         " << MG_def_nu_b         << '\n';
 }

 MG_nu_0         = MG_def_nu_0;
 MG_nu_f         = MG_def_nu_f;
 MG_verbose      = MG_def_verbose;
 MG_nu_b         = MG_def_nu_b;

 if ((ParallelDescriptor::IOProcessor())&&(MG_verbose > 2)) {
  std::cout << "MultiGrid: " << MG_numlevels_var
    << " multigrid levels created for this solve" << '\n';
  std::cout << "Grids: " << '\n';
  BoxArray tmp = LPboxArray(0);
  for (int i = 0; i < MG_numlevels_var; ++i) {
   if (i > 0)
    tmp.coarsen(2);
   std::cout << " Level: " << i << '\n';
   for (int k = 0; k < tmp.size(); k++) {
    const Box& b = tmp[k];
    std::cout << "  [" << k << "]: " << b << "   ";
    for (int j = 0; j < AMREX_SPACEDIM; j++)
     std::cout << b.length(j) << ' ';
    std::cout << '\n';
   }
  }
 }


#if (profile_solver==1)
 bprof.stop();
#endif

}

ABecLaplacian::~ABecLaplacian ()
{
 for (int level=0;level<MG_numlevels_var;level++) {
  delete maskvals[level];
  maskvals[level]=(MultiFab*)0;
  delete MG_CG_ones_mf_copy[level];
  MG_CG_ones_mf_copy[level]=(MultiFab*)0;
  delete MG_CG_diagsumL[level];
  MG_CG_diagsumL[level]=(MultiFab*)0;
  delete acoefs[level];
  acoefs[level]=(MultiFab*)0;
  delete workcoefs[level];
  workcoefs[level]=(MultiFab*)0;
  delete laplacian_ones[level];
  laplacian_ones[level]=(MultiFab*)0;
  delete MG_res[level];
  MG_res[level]=(MultiFab*)0;
  delete MG_rhs[level];
  MG_rhs[level]=(MultiFab*)0;
  delete MG_cor[level];
  MG_cor[level]=(MultiFab*)0;
  delete MG_pbdrycoarser[level];
  MG_pbdrycoarser[level]=(MultiFab*)0;
  for (int dir = 0; dir < AMREX_SPACEDIM; ++dir) {
   delete bcoefs[level][dir];
   bcoefs[level][dir]=(MultiFab*)0;
  }
 } // level=0..MG_numlevels-1

 delete MG_initialsolution;
 MG_initialsolution=(MultiFab*)0;

 delete MG_pbdryhom;
 MG_pbdryhom=(MultiFab*)0;

 for (int coarsefine=0;coarsefine<CG_numlevels_var;coarsefine++) {
  for (int j=0;j<gmres_precond_iter_base_mg*nsolve_bicgstab;j++) {
   delete GMRES_V_MF[j][coarsefine];
   GMRES_V_MF[j][coarsefine]=(MultiFab*)0;
   delete GMRES_U_MF[j][coarsefine];
   GMRES_U_MF[j][coarsefine]=(MultiFab*)0;
   delete GMRES_Z_MF[j][coarsefine];
   GMRES_Z_MF[j][coarsefine]=(MultiFab*)0;
  }
  delete GMRES_W_MF[coarsefine];
  GMRES_W_MF[coarsefine]=(MultiFab*)0;

  delete CG_delta_sol[coarsefine];
  CG_delta_sol[coarsefine]=(MultiFab*)0;
  delete CG_r[coarsefine];
  CG_r[coarsefine]=(MultiFab*)0;
  delete CG_z[coarsefine];
  CG_z[coarsefine]=(MultiFab*)0;
  delete CG_Av_search[coarsefine];
  CG_Av_search[coarsefine]=(MultiFab*)0;
  delete CG_p_search[coarsefine];
  CG_p_search[coarsefine]=(MultiFab*)0;
  delete CG_p_search_SOLN[coarsefine];
  CG_p_search_SOLN[coarsefine]=(MultiFab*)0;
  delete CG_v_search[coarsefine];
  CG_v_search[coarsefine]=(MultiFab*)0;
  delete CG_rhs_resid_cor_form[coarsefine];
  CG_rhs_resid_cor_form[coarsefine]=(MultiFab*)0;
  delete CG_pbdryhom[coarsefine];
  CG_pbdryhom[coarsefine]=(MultiFab*)0;
 } // coarsefine=0..CG_numlevels_var-1

}

void
ABecLaplacian::Fsmooth (MultiFab& solnL,
                        MultiFab& rhsL,
                        int level,
                        int smooth_type) {

#if (profile_solver==1)
 std::string subname="ABecLaplacian::Fsmooth";
 std::stringstream popt_string_stream(std::stringstream::in |
      std::stringstream::out);
 popt_string_stream << cfd_project_option;
 std::string profname=subname+popt_string_stream.str();
 profname=profname+"_";
 std::stringstream lev_string_stream(std::stringstream::in |
      std::stringstream::out);
 lev_string_stream << level;
 profname=profname+lev_string_stream.str();

 BLProfiler bprof(profname);
#endif

 int mg_coarsest_level=MG_numlevels_var-1;

 bool use_tiling=cfd_tiling;

 int gsrb_timing=0;
  // double second() 
 double t1=0.0;
 double t2=0.0;

 const BoxArray& bxa = gbox[level];

#if (profile_solver==1)
 bprof.stop();
#endif

 const MultiFab & work=*workcoefs[level];

#if (profile_solver==1)
 bprof.start();
#endif

 int ncwork=AMREX_SPACEDIM*3+SCALAR_WORK_NCOMP;

 int nctest = work.nComp();
 if (nctest!=ncwork*nsolve_bicgstab)
  amrex::Error("ncwork invalid");

 int nc = solnL.nComp();
 if (nc!=nsolve_bicgstab)
  amrex::Error("nc bust");
 int ngrow=solnL.nGrow();
 if (ngrow<1)
  amrex::Error("ngrow solnL invalid");
 int ngrow_rhs=rhsL.nGrow();
 if (ngrow_rhs!=0)
  amrex::Error("ngrow rhsL invalid");

#if (profile_solver==1)
 bprof.stop();
#endif

 const MultiFab& ones_mf=*laplacian_ones[level];
 const BoxArray& temp_ba_test=ones_mf.boxArray();
 if ((temp_ba_test==bxa)&&(ones_mf.nComp()==1)) {
  // do nothing
 } else {
  amrex::Error("ones_mf invalid in fsmooth");
 }

 int bxleftcomp=0;
 int byleftcomp=bxleftcomp+1;
 int bzleftcomp=byleftcomp+AMREX_SPACEDIM-2;
 int bxrightcomp=bzleftcomp+1;
 int byrightcomp=bxrightcomp+1;
 int bzrightcomp=byrightcomp+AMREX_SPACEDIM-2;
 int icbxcomp=bzrightcomp+1;
 int icbycomp=icbxcomp+1;
 int icbzcomp=icbycomp+AMREX_SPACEDIM-2;

 int diag_comp=icbzcomp+1;
 int maskcomp=diag_comp+1;

 int icdiagcomp=maskcomp+1;
 int icdiagrbcomp=icdiagcomp+1;
 int axcomp=icdiagrbcomp+1;
 int solnsavecomp=axcomp+1;
 int rhssavecomp=solnsavecomp+1;
 int redsolncomp=rhssavecomp+1;
 int blacksolncomp=redsolncomp+1;

 int number_grids=gbox[level].size();

 int bfact=bfact_array[level];
 int bfact_top=bfact_array[0];

 int num_sweeps=0;
 if (smooth_type==0) // GSRB
  num_sweeps=6;
 else if (smooth_type==3) // Jacobi
  num_sweeps=4;
 else if (smooth_type==2) // ILU
  num_sweeps=7;
 else if (smooth_type==1) // ICRB
  num_sweeps=6;
 else
  amrex::Error("smooth_type invalid");


 for (int isweep=0;isweep<num_sweeps;isweep++) {

  if (thread_class::nthreads<1)
   amrex::Error("thread_class::nthreads invalid");
  thread_class::init_d_numPts(solnL.boxArray().d_numPts());

#ifdef _OPENMP
#pragma omp parallel
#endif
{
  for (MFIter mfi(solnL,use_tiling); mfi.isValid(); ++mfi) {
   BL_ASSERT(bxa[mfi.index()] == mfi.validbox());
   int gridno = mfi.index();
   if (gridno>=number_grids)
    amrex::Error("gridno invalid");
   const Box& tilegrid=mfi.tilebox();
   const Box& fabgrid=gbox[level][gridno];
   const int* tilelo=tilegrid.loVect();
   const int* tilehi=tilegrid.hiVect();
   const int* fablo=fabgrid.loVect();
   const int* fabhi=fabgrid.hiVect();

   if (gsrb_timing==1) 
    t1 = ParallelDescriptor::second();

   int tid_current=0;
#ifdef _OPENMP
   tid_current = omp_get_thread_num();
#endif
   if ((tid_current>=0)&&(tid_current<thread_class::nthreads)) {
    // do nothing
   } else
    amrex::Error("tid_current invalid");

   thread_class::tile_d_numPts[tid_current]+=tilegrid.d_numPts();

   for (int veldir=0;veldir<nsolve_bicgstab;veldir++) {

#if (profile_solver==1)
    std::string subname="GSRB";
    std::stringstream popt_string_stream(std::stringstream::in |
      std::stringstream::out);
    popt_string_stream << cfd_project_option;
    std::string profname=subname+popt_string_stream.str();
    profname=profname+"_";
    std::stringstream lev_string_stream(std::stringstream::in |
      std::stringstream::out);
    lev_string_stream << level;
    profname=profname+lev_string_stream.str();
    profname=profname+"_";
    std::stringstream isweep_string_stream(std::stringstream::in |
      std::stringstream::out);
    isweep_string_stream << isweep;
    profname=profname+isweep_string_stream.str();
    profname=profname+"_";
    std::stringstream veldir_string_stream(std::stringstream::in |
      std::stringstream::out);
    veldir_string_stream << veldir;
    profname=profname+veldir_string_stream.str();

    BLProfiler bprof(profname);
#endif

    int ofs=veldir*ncwork;

     // in: ABec_3D.F90
    FORT_GSRB(
     &level,
     &mg_coarsest_level,
     &isweep,
     &num_sweeps,
     ones_mf[mfi].dataPtr(), 
     ARLIM(ones_mf[mfi].loVect()),ARLIM(ones_mf[mfi].hiVect()),
     solnL[mfi].dataPtr(veldir), 
     ARLIM(solnL[mfi].loVect()),ARLIM(solnL[mfi].hiVect()),
     rhsL[mfi].dataPtr(veldir), 
     ARLIM(rhsL[mfi].loVect()), ARLIM(rhsL[mfi].hiVect()),

     work[mfi].dataPtr(diag_comp+ofs),
     ARLIM(work[mfi].loVect()), ARLIM(work[mfi].hiVect()),

     work[mfi].dataPtr(bxleftcomp+ofs),
     work[mfi].dataPtr(bxrightcomp+ofs), 
     work[mfi].dataPtr(byleftcomp+ofs),
     work[mfi].dataPtr(byrightcomp+ofs), 
     work[mfi].dataPtr(bzleftcomp+ofs),
     work[mfi].dataPtr(bzrightcomp+ofs), 
     work[mfi].dataPtr(icbxcomp+ofs), 
     work[mfi].dataPtr(icbycomp+ofs), 
     work[mfi].dataPtr(icbzcomp+ofs), 
     work[mfi].dataPtr(icdiagcomp+ofs), 
     work[mfi].dataPtr(icdiagrbcomp+ofs), 
     work[mfi].dataPtr(maskcomp+ofs), 
     work[mfi].dataPtr(axcomp+ofs), 
     work[mfi].dataPtr(solnsavecomp+ofs), 
     work[mfi].dataPtr(rhssavecomp+ofs), 
     work[mfi].dataPtr(redsolncomp+ofs), 
     work[mfi].dataPtr(blacksolncomp+ofs), 
     tilelo,tilehi,
     fablo,fabhi,&bfact,&bfact_top,
     &smooth_type);

#if (profile_solver==1)
    bprof.stop();
#endif
   } // veldir=0...nsolve_bicgstab-1

   if (gsrb_timing==1) {
    t2 = ParallelDescriptor::second();
    std::cout << "GSRB time, level= " << level << " smooth_type=" <<
     smooth_type << " gridno= " << gridno << " t2-t1=" << t2-t1 << '\n';
   }
  } // mfi
} // omp

  thread_class::sync_tile_d_numPts();
  ParallelDescriptor::ReduceRealSum(thread_class::tile_d_numPts[0]);
  thread_class::reconcile_d_numPts(6);

 } // isweep

} // subroutine Fsmooth

// y=Ax
void
ABecLaplacian::Fapply (MultiFab& y,
                       MultiFab& x,
                       int level)
{

 int mg_coarsest_level=MG_numlevels_var-1;

 bool use_tiling=cfd_tiling;

 const BoxArray& bxa = gbox[level];

 const MultiFab & work=*workcoefs[level];
 int ncwork=AMREX_SPACEDIM*3+SCALAR_WORK_NCOMP;

 int nctest = work.nComp();
 if (nctest!=ncwork*nsolve_bicgstab)
  amrex::Error("ncwork invalid");

 int nc = y.nComp();
 if (nc!=nsolve_bicgstab)
  amrex::Error("nc bust");
 int ngrow_y=y.nGrow();
 if (ngrow_y!=0) {
  std::cout << "ngrow_y= " << ngrow_y << '\n';
  amrex::Error("ngrow y invalid");
 }
 int ngrow_x=x.nGrow();
 if (ngrow_x<1)
  amrex::Error("ngrow x invalid");

 const MultiFab& ones_mf=*laplacian_ones[level];
 const BoxArray& temp_ba_test=ones_mf.boxArray();
 if ((temp_ba_test==bxa)&&(ones_mf.nComp()==1)) {
  // do nothing
 } else {
  amrex::Error("ones_mf invalid in fapply");
 }

 int bxleftcomp=0;
 int byleftcomp=bxleftcomp+1;
 int bzleftcomp=byleftcomp+AMREX_SPACEDIM-2;
 int bxrightcomp=bzleftcomp+1;
 int byrightcomp=bxrightcomp+1;
 int bzrightcomp=byrightcomp+AMREX_SPACEDIM-2;
 int icbxcomp=bzrightcomp+1;
 int icbycomp=icbxcomp+1;
 int icbzcomp=icbycomp+AMREX_SPACEDIM-2;

 int diag_comp=icbzcomp+1;
 int maskcomp=diag_comp+1;

 int icdiagcomp=maskcomp+1;
 int icdiagrbcomp=icdiagcomp+1;
 int axcomp=icdiagrbcomp+1;
 int solnsavecomp=axcomp+1;
 int rhssavecomp=solnsavecomp+1;
 int redsolncomp=rhssavecomp+1;
 int blacksolncomp=redsolncomp+1;

 if (work.nComp()<=blacksolncomp*nsolve_bicgstab)
  amrex::Error("work.nComp() invalid");

 int number_grids=gbox[level].size();
 int bfact=bfact_array[level];
 int bfact_top=bfact_array[0];

 if (thread_class::nthreads<1)
  amrex::Error("thread_class::nthreads invalid");
 thread_class::init_d_numPts(y.boxArray().d_numPts());

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(y,use_tiling); mfi.isValid(); ++mfi) {
  BL_ASSERT(bxa[mfi.index()] == mfi.validbox());
  int gridno=mfi.index();
  if (gridno>=number_grids)
   amrex::Error("gridno invalid");
  const Box& tilegrid=mfi.tilebox();
  const Box& fabgrid=gbox[level][gridno];
  const int* tilelo=tilegrid.loVect();
  const int* tilehi=tilegrid.hiVect();
  const int* fablo=fabgrid.loVect();
  const int* fabhi=fabgrid.hiVect();

  int tid_current=0;
#ifdef _OPENMP
  tid_current = omp_get_thread_num();
#endif
  if ((tid_current>=0)&&(tid_current<thread_class::nthreads)) {
   // do nothing
  } else
   amrex::Error("tid_current invalid");

  thread_class::tile_d_numPts[tid_current]+=tilegrid.d_numPts();

  for (int veldir=0;veldir<nsolve_bicgstab;veldir++) {

#if (profile_solver==1)
   std::string subname="ADOTX";
   std::stringstream popt_string_stream(std::stringstream::in |
      std::stringstream::out);
   popt_string_stream << cfd_project_option;
   std::string profname=subname+popt_string_stream.str();
   profname=profname+"_";
   std::stringstream lev_string_stream(std::stringstream::in |
      std::stringstream::out);
   lev_string_stream << level;
   profname=profname+lev_string_stream.str();
   profname=profname+"_";
   std::stringstream veldir_string_stream(std::stringstream::in |
      std::stringstream::out);
   veldir_string_stream << veldir;
   profname=profname+veldir_string_stream.str();

   BLProfiler bprof(profname);
#endif

   int ofs=veldir*ncwork;

    // in: ABec_3D.F90
   FORT_ADOTX(
    &level,
    &mg_coarsest_level,
    ones_mf[mfi].dataPtr(), 
    ARLIM(ones_mf[mfi].loVect()),ARLIM(ones_mf[mfi].hiVect()),
    y[mfi].dataPtr(veldir),
    ARLIM(y[mfi].loVect()), ARLIM(y[mfi].hiVect()),
    x[mfi].dataPtr(veldir),
    ARLIM(x[mfi].loVect()), ARLIM(x[mfi].hiVect()),

    work[mfi].dataPtr(diag_comp+ofs),
    ARLIM(work[mfi].loVect()),ARLIM(work[mfi].hiVect()),

    work[mfi].dataPtr(bxleftcomp+ofs),
    work[mfi].dataPtr(bxrightcomp+ofs),
    work[mfi].dataPtr(byleftcomp+ofs),
    work[mfi].dataPtr(byrightcomp+ofs),
    work[mfi].dataPtr(bzleftcomp+ofs),
    work[mfi].dataPtr(bzrightcomp+ofs),
    tilelo,tilehi,
    fablo,fabhi,&bfact,&bfact_top);

#if (profile_solver==1)
   bprof.stop();
#endif
  } // veldir=0..nsolve_bicgstab-1
 } // mfi
} // omp

 thread_class::sync_tile_d_numPts();
 ParallelDescriptor::ReduceRealSum(thread_class::tile_d_numPts[0]);
 thread_class::reconcile_d_numPts(7);

} // end subroutine Fapply



void
ABecLaplacian::LP_update (MultiFab& sol,
   Real alpha,MultiFab& y,
   const MultiFab& p,int level) {

#if (profile_solver==1)
 std::string subname="ABecLaplacian::LP_update";
 std::stringstream popt_string_stream(std::stringstream::in |
   std::stringstream::out);
 popt_string_stream << cfd_project_option;
 std::string profname=subname+popt_string_stream.str();
 profname=profname+"_";
 std::stringstream lev_string_stream(std::stringstream::in |
   std::stringstream::out);
 lev_string_stream << level;
 profname=profname+lev_string_stream.str();

 BLProfiler bprof(profname);
#endif

    //
    // compute sol=y+alpha p  
    //
 bool use_tiling=cfd_tiling;

 if (level>=MG_numlevels_var)
  amrex::Error("level invalid in LP_update");

 const BoxArray& gboxlev = gbox[level];
 int ncomp = sol.nComp();
 if (ncomp==nsolve_bicgstab) {
  // do nothing
 } else
  amrex::Error("ncomp invalid");

 int bfact=bfact_array[level];
 int bfact_top=bfact_array[0];

#if (profile_solver==1)
 bprof.stop();
#endif

 if (thread_class::nthreads<1)
  amrex::Error("thread_class::nthreads invalid");
 thread_class::init_d_numPts(sol.boxArray().d_numPts());

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(sol,use_tiling); mfi.isValid(); ++mfi) {
  BL_ASSERT(mfi.validbox() == gboxlev[mfi.index()]);
  const int gridno = mfi.index();
  const Box& tilegrid=mfi.tilebox();
  const Box& fabgrid=gbox[level][gridno];
  const int* tilelo=tilegrid.loVect();
  const int* tilehi=tilegrid.hiVect();
  const int* fablo=fabgrid.loVect();
  const int* fabhi=fabgrid.hiVect();

  int tid_current=0;
#ifdef _OPENMP
  tid_current = omp_get_thread_num();
#endif
  if ((tid_current>=0)&&(tid_current<thread_class::nthreads)) {
   // do nothing
  } else
   amrex::Error("tid_current invalid");

  thread_class::tile_d_numPts[tid_current]+=tilegrid.d_numPts();

  for (int veldir=0;veldir<nsolve_bicgstab;veldir++) {

#if (profile_solver==1)
   std::string subname="CGUPDATE";
   std::stringstream popt_string_stream(std::stringstream::in |
      std::stringstream::out);
   popt_string_stream << cfd_project_option;
   std::string profname=subname+popt_string_stream.str();
   profname=profname+"_";
   std::stringstream lev_string_stream(std::stringstream::in |
      std::stringstream::out);
   lev_string_stream << level;
   profname=profname+lev_string_stream.str();
   profname=profname+"_";
   std::stringstream veldir_string_stream(std::stringstream::in |
      std::stringstream::out);
   veldir_string_stream << veldir;
   profname=profname+veldir_string_stream.str();

   BLProfiler bprof(profname);
#endif

   FORT_CGUPDATE(
    sol[mfi].dataPtr(veldir),
    ARLIM(sol[mfi].loVect()), ARLIM(sol[mfi].hiVect()),
    &alpha,
    y[mfi].dataPtr(veldir),
    ARLIM(y[mfi].loVect()), ARLIM(y[mfi].hiVect()),
    p[mfi].dataPtr(veldir),
    ARLIM(p[mfi].loVect()), ARLIM(p[mfi].hiVect()),
    tilelo,tilehi,
    fablo,fabhi,&bfact,&bfact_top);

#if (profile_solver==1)
   bprof.stop();
#endif
  } // veldir=0 ... nsolve_bicgstab-1
 } // mfi
} // omp

 thread_class::sync_tile_d_numPts();
 ParallelDescriptor::ReduceRealSum(thread_class::tile_d_numPts[0]);
 thread_class::reconcile_d_numPts(8);

} // end subroutine LP_update


void ABecLaplacian::LP_dot(const MultiFab& w_in,
		           const MultiFab& p_in,
                           int level_in,
			   Real& dot_result) {

#define profile_dot 0

#if (profile_dot==1)
  std::string subname="ABecLaplacian::LP_dot";
  std::stringstream popt_string_stream(std::stringstream::in |
   std::stringstream::out);
  popt_string_stream << cfd_project_option;
  std::string profname=subname+popt_string_stream.str();
  profname=profname+"_";
  std::stringstream lev_string_stream(std::stringstream::in |
   std::stringstream::out);
  lev_string_stream << level_in;
  profname=profname+lev_string_stream.str();

  BLProfiler bprof(profname);
#endif
 
 bool use_tiling=cfd_tiling;

 if (level_in>=MG_numlevels_var)
  amrex::Error("level_in invalid in LP_dot");

 if (level_in>=gbox.size()) {
  std::cout << "level_in= " << level_in << '\n';
  std::cout << "gboxsize= " << gbox.size() << '\n';
  std::cout << "num levels = " << MG_numlevels_var << '\n';
  amrex::Error("level exceeds gbox size");
 }

 if (thread_class::nthreads<1)
  amrex::Error("thread_class::nthreads invalid");

#if (profile_dot==1)
  bprof.stop();

  std::string subname2="ABecLaplacian::LP_dot_pw";
  std::string profname2=subname2+popt_string_stream.str();
  profname2=profname2+"_";
  profname2=profname2+lev_string_stream.str();

  BLProfiler bprof2(profname2);
#endif

 pw_dotprod_var.resize(thread_class::nthreads);
 for (int tid_local=0;tid_local<thread_class::nthreads;tid_local++) {
  pw_dotprod_var[tid_local] = 0.0;
 }

#if (profile_dot==1)
  bprof2.stop();

  std::string subname3="ABecLaplacian::LP_dot_gboxlev";
  std::string profname3=subname3+popt_string_stream.str();
  profname3=profname3+"_";
  profname3=profname3+lev_string_stream.str();

  BLProfiler bprof3(profname3);
#endif 

 const BoxArray& gboxlev = gbox[level_in];
 int ncomp = p_in.nComp();
 if (ncomp!=nsolve_bicgstab)
  amrex::Error("ncomp invalid p_in");
 if (w_in.nComp()!=nsolve_bicgstab)
  amrex::Error("ncomp invalid w_in");

 if (p_in.boxArray()==w_in.boxArray()) {
  // do nothing
 } else
  amrex::Error("p_in.boxArray()!=w_in.boxArray()");

 if (p_in.boxArray()==gboxlev) {
  // do nothing
 } else
  amrex::Error("p_in.boxArray()!=gboxlev");

 int bfact=bfact_array[level_in];
 int bfact_top=bfact_array[0];

#if (profile_dot==1)
  bprof3.stop();

  std::string subname4="ABecLaplacian::LP_dot_MFIter";
  std::string profname4=subname4+popt_string_stream.str();
  profname4=profname4+"_";
  profname4=profname4+lev_string_stream.str();

  BLProfiler bprof4(profname4);
 
  bprof4.stop();
#endif

 thread_class::init_d_numPts(w_in.boxArray().d_numPts());

#ifdef _OPENMP
#pragma omp parallel 
#endif
{
 for (MFIter mfi(w_in,use_tiling); mfi.isValid(); ++mfi) {

#if (profile_dot==1)
   std::string subname5="ABecLaplacian::LP_dot_MFIter_tilebox";
   std::string profname5=subname5+popt_string_stream.str();
   profname5=profname5+"_";
   profname5=profname5+lev_string_stream.str();

   BLProfiler bprof5(profname5);
#endif 

  Real tpw=0.0;

  if (mfi.validbox()==gboxlev[mfi.index()]) {
   // do nothing
  } else
   amrex::Error("mfi.validbox()!=gboxlev[mfi.index()] LP_dot");

  const int gridno = mfi.index();
   // MFIter::tilebox () is declared in AMReX_MFIter.cpp
   // MFIter::Initialize (), which is in AMReX_MFIter.cpp,
   // calls getTileArray which is in AMReX_FabArrayBase.cpp
  const Box& tilegrid=mfi.tilebox();
  const Box& fabgrid=gboxlev[gridno];
  const int* tilelo=tilegrid.loVect();
  const int* tilehi=tilegrid.hiVect();
  const int* fablo=fabgrid.loVect();
  const int* fabhi=fabgrid.hiVect();

  int tid_current=0;
#ifdef _OPENMP
  tid_current = omp_get_thread_num();
#endif
  if ((tid_current>=0)&&(tid_current<thread_class::nthreads)) {
   // do nothing
  } else
   amrex::Error("tid_current invalid");

#if (profile_dot==1)
   bprof5.stop();

   std::string subname6="CGXDOTY";
   std::string profname6=subname6+popt_string_stream.str();
   profname6=profname6+"_";
   profname6=profname6+lev_string_stream.str();

   BLProfiler bprof6(profname6);
#endif 

  const FArrayBox& pfab=p_in[mfi];
  const FArrayBox& wfab=w_in[mfi];

  thread_class::tile_d_numPts[tid_current]+=tilegrid.d_numPts();

   // in: CG_3D.F90
  FORT_CGXDOTY(
   &ncomp,
   &tpw, // init to 0.0d0 in CGXDOTY
   pfab.dataPtr(),ARLIM(pfab.loVect()),ARLIM(pfab.hiVect()),
   wfab.dataPtr(),ARLIM(wfab.loVect()),ARLIM(wfab.hiVect()),
   tilelo,tilehi,
   fablo,fabhi,&bfact,&bfact_top);
  pw_dotprod_var[tid_current] += tpw;

#if (profile_dot==1)
  bprof6.stop();
#endif 

 } // MFIter
} // omp

#if (profile_dot==1)
  bprof4.start();
#endif

 thread_class::sync_tile_d_numPts();

 for (int tid_local=1;tid_local<thread_class::nthreads;tid_local++) {
  pw_dotprod_var[0]+=pw_dotprod_var[tid_local];
 }

  // no Barrier needed since all processes must wait in order to receive the
  // reduced value.
 ParallelDescriptor::ReduceRealSum(pw_dotprod_var[0]);

 ParallelDescriptor::ReduceRealSum(thread_class::tile_d_numPts[0]);
 thread_class::reconcile_d_numPts(1);

 dot_result=pw_dotprod_var[0];

#if (profile_dot==1)
 bprof4.stop();
#endif 

#undef profile_dot 

} // subroutine LP_dot


void ABecLaplacian::init_checkerboard(MultiFab& v_in,
                           int level_in) {

#define profile_checkerboard 0

#if (profile_checkerboard==1)
 std::string subname="ABecLaplacian::init_checkerboard";
 std::stringstream popt_string_stream(std::stringstream::in |
  std::stringstream::out);
 popt_string_stream << cfd_project_option;
 std::string profname=subname+popt_string_stream.str();
 profname=profname+"_";
 std::stringstream lev_string_stream(std::stringstream::in |
  std::stringstream::out);
 lev_string_stream << level_in;
 profname=profname+lev_string_stream.str();

 BLProfiler bprof(profname);
 bprof.stop();
#endif

 if (laplacian_solvability==0) { // system is nonsingular.

  // do nothing
  
 } else if (laplacian_solvability==1) { // system is singular

  bool use_tiling=cfd_tiling;

  if (level_in>=MG_numlevels_var)
   amrex::Error("level_in invalid in init_checkerboard");

  if (level_in>=gbox.size()) {
    std::cout << "level_in= " << level_in << '\n';
    std::cout << "gboxsize= " << gbox.size() << '\n';
    std::cout << "num levels = " << MG_numlevels_var << '\n';
    amrex::Error("level exceeds gbox size");
  }

  if (thread_class::nthreads<1)
    amrex::Error("thread_class::nthreads invalid");

  const BoxArray& gboxlev = gbox[level_in];
  int ncomp = v_in.nComp();
  if (ncomp!=nsolve_bicgstab)
    amrex::Error("ncomp invalid v_in");

  if (v_in.boxArray()==gboxlev) {
    // do nothing
  } else
    amrex::Error("v_in.boxArray()!=gboxlev");

  int bfact=bfact_array[level_in];
  int bfact_top=bfact_array[0];

  thread_class::init_d_numPts(v_in.boxArray().d_numPts());

#ifdef _OPENMP
#pragma omp parallel 
#endif
{
  for (MFIter mfi(v_in,use_tiling); mfi.isValid(); ++mfi) {

    if (mfi.validbox()==gboxlev[mfi.index()]) {
     // do nothing
    } else
     amrex::Error("mfi.validbox()!=gboxlev[mfi.index()] init_checkerboard");

    const int gridno = mfi.index();
     // MFIter::tilebox () is declared in AMReX_MFIter.cpp
     // MFIter::Initialize (), which is in AMReX_MFIter.cpp,
     // calls getTileArray which is in AMReX_FabArrayBase.cpp
    const Box& tilegrid=mfi.tilebox();
    const Box& fabgrid=gboxlev[gridno];
    const int* tilelo=tilegrid.loVect();
    const int* tilehi=tilegrid.hiVect();
    const int* fablo=fabgrid.loVect();
    const int* fabhi=fabgrid.hiVect();

    int tid_current=0;
#ifdef _OPENMP
    tid_current = omp_get_thread_num();
#endif
    if ((tid_current>=0)&&(tid_current<thread_class::nthreads)) {
     // do nothing
    } else
     amrex::Error("tid_current invalid");

    FArrayBox& vfab=v_in[mfi];

    thread_class::tile_d_numPts[tid_current]+=tilegrid.d_numPts();

     // in: MG_3D.F90
    FORT_CHECKERBOARD_RB(
     &ncomp,
     vfab.dataPtr(),ARLIM(vfab.loVect()),ARLIM(vfab.hiVect()),
     tilelo,tilehi,
     fablo,fabhi, 
     &bfact,&bfact_top);

  } // MFIter
} // omp

  thread_class::sync_tile_d_numPts();

  ParallelDescriptor::ReduceRealSum(thread_class::tile_d_numPts[0]);
  thread_class::reconcile_d_numPts(1);

 } else
  amrex::Error("laplacian_solvability invalid");

#undef profile_checkerboard

} // subroutine init_checkerboard



// onesCoefficients:
// =1 
void
ABecLaplacian::project_null_space(MultiFab& rhsL,int level) {

#if (profile_solver==1)
 std::string subname="ABecLaplacian::project_null_space";
 std::stringstream popt_string_stream(std::stringstream::in |
      std::stringstream::out);
 popt_string_stream << cfd_project_option;
 std::string profname=subname+popt_string_stream.str();
 profname=profname+"_";
 std::stringstream lev_string_stream(std::stringstream::in |
      std::stringstream::out);
 lev_string_stream << level;
 profname=profname+lev_string_stream.str();

 BLProfiler bprof(profname);

 bprof.stop();
#endif

 if (laplacian_solvability==0) { // system is nonsingular.

  // do nothing

 } else if (laplacian_solvability==1) { // system is singular

  if (nsolve_bicgstab!=1)
   amrex::Error("nsolve_bicgstab invalid");

  if ((rhsL.nComp()==1)&&
      (laplacian_ones[level]->nComp()==1)) {
   MultiFab::Multiply(rhsL,*laplacian_ones[level],0,0,1,0);
  } else {
   std::cout << "laplacian_solvability= " << 
    laplacian_solvability << '\n';
   amrex::Error("rhsL or ones_mf invalid nComp");
  }
  if ((laplacian_ones[level]->nComp()==1)&&
      (laplacian_ones[level]->nGrow()==1)) {
   MultiFab::Copy(*MG_CG_ones_mf_copy[level],
      *laplacian_ones[level],0,0,1,1);

   Real dot_result=0.0;
   Real domainsum=0.0;
   LP_dot(rhsL,*laplacian_ones[level],level,dot_result);
   LP_dot(*MG_CG_ones_mf_copy[level],
          *laplacian_ones[level],level,domainsum); 

   double total_cells=gbox[level].d_numPts();
   if (domainsum>total_cells) {
     std::cout << "level= " << level << '\n';
     std::cout << "dot_result= " << dot_result << '\n';
     std::cout << "cfd_level= " << cfd_level << '\n';
     std::cout << "cfd_project_option= " << cfd_project_option << '\n';
     std::cout << "cfd_tiling= " << cfd_tiling << '\n';

     std::cout << "pw_dotprod_var.size()=" << pw_dotprod_var.size() << '\n';
     for (int tid_local=0;tid_local<pw_dotprod_var.size();tid_local++) {
      std::cout << "tid_local= " << tid_local << 
	     " pw_dotprod_var[tid_local]= " <<
	     pw_dotprod_var[tid_local] << '\n';
     }

     std::cout << "domainsum= " << domainsum << '\n';
     std::cout << "total_cells= " << total_cells << '\n';
     std::cout << "MG_CG_ones_mf_copy[level]->boxArray()" << 
	     MG_CG_ones_mf_copy[level]->boxArray() << '\n';
     std::cout << "laplacian_ones[level]->boxArray()" << 
	     laplacian_ones[level]->boxArray() << '\n';
     std::cout << "MG_CG_ones_mf_copy[level]->nComp()" << 
	     MG_CG_ones_mf_copy[level]->nComp() << '\n';
     std::cout << "laplacian_ones[level]->nComp()" << 
	     laplacian_ones[level]->nComp() << '\n';

     for (int local_comp=0;local_comp<laplacian_ones[level]->nComp();
          local_comp++) {
      std::cout << "comp= " << local_comp << 
       " MG_CG_ones_mf_copy[level]->min=" <<
       MG_CG_ones_mf_copy[level]->min(local_comp) << '\n';
      std::cout << "comp= " << local_comp << 
       " MG_CG_ones_mf_copy[level]->max=" <<
       MG_CG_ones_mf_copy[level]->max(local_comp) << '\n';

      std::cout << "comp= " << local_comp <<
       " laplacian_ones[level]->min=" <<
       laplacian_ones[level]->min(local_comp) << '\n';
      std::cout << "comp= " << local_comp <<
       " laplacian_ones[level]->max=" <<
       laplacian_ones[level]->max(local_comp) << '\n';
     } // local_comp=0..nComp-1

     amrex::Error("domainsum too big");
   }
   if (1==0) {
     std::cout << "cfd_level= " << cfd_level << '\n';
     std::cout << "cfd_project_option= " << cfd_project_option << '\n';
     std::cout << "level= " << level << '\n';
     std::cout << "dot_result= " << dot_result << '\n';
     std::cout << "domainsum= " << domainsum << '\n';
     std::cout << "total_cells= " << total_cells << '\n';
   }

   if (domainsum>=1.0) {
     Real coef=-dot_result/domainsum;
      // rhsL=rhsL+coef * ones_mf
     LP_update(rhsL,coef,rhsL,*laplacian_ones[level],level); 
   } else if (domainsum==0.0) {
     // do nothing
   } else
     amrex::Error("domainsum invalid");

   MultiFab::Multiply(rhsL,*laplacian_ones[level],0,0,1,0);

   if (1==0) {
     std::cout << "check rhsL after projection \n";
     LP_dot(rhsL,*laplacian_ones[level],level,dot_result);
     std::cout << "level= " << level << '\n';
     std::cout << "dot_result= " << dot_result << '\n';
   }

  } else
    amrex::Error("laplacian_ones[level]: ncomp or ngrow invalid");

 } else
  amrex::Error("laplacian solvability incorrect");

} // subroutine project_null_space


// off diagonal sum flag 
void
ABecLaplacian::Fdiagsum(MultiFab&       y,
                       int             level) {

 bool use_tiling=cfd_tiling;

 const BoxArray& bxa = gbox[level];
 const MultiFab& bX  = *bcoefs[level][0];
 const MultiFab& bY  = *bcoefs[level][1];
 const MultiFab& bZ  = *bcoefs[level][AMREX_SPACEDIM-1];
 int nc = y.nComp();
 if (nc!=nsolve_bicgstab)
  amrex::Error("nc bust");

 int bfact=bfact_array[level];
 int bfact_top=bfact_array[0];

 if (thread_class::nthreads<1)
  amrex::Error("thread_class::nthreads invalid");
 thread_class::init_d_numPts(y.boxArray().d_numPts());

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(y,use_tiling); mfi.isValid(); ++mfi) {
  BL_ASSERT(bxa[mfi.index()] == mfi.validbox());
  const int gridno = mfi.index();
  const Box& tilegrid=mfi.tilebox();
  const Box& fabgrid=gbox[level][gridno];
  const int* tilelo=tilegrid.loVect();
  const int* tilehi=tilegrid.hiVect();
  const int* fablo=fabgrid.loVect();
  const int* fabhi=fabgrid.hiVect();

  int tid_current=0;
#ifdef _OPENMP
  tid_current = omp_get_thread_num();
#endif
  if ((tid_current>=0)&&(tid_current<thread_class::nthreads)) {
   // do nothing
  } else
   amrex::Error("tid_current invalid");

  thread_class::tile_d_numPts[tid_current]+=tilegrid.d_numPts();

  for (int veldir=0;veldir<nsolve_bicgstab;veldir++) {

#if (profile_solver==1)
   std::string subname="DIAGSUM";
   std::stringstream popt_string_stream(std::stringstream::in |
      std::stringstream::out);
   popt_string_stream << cfd_project_option;
   std::string profname=subname+popt_string_stream.str();
   profname=profname+"_";
   std::stringstream lev_string_stream(std::stringstream::in |
      std::stringstream::out);
   lev_string_stream << level;
   profname=profname+lev_string_stream.str();
   profname=profname+"_";
   std::stringstream veldir_string_stream(std::stringstream::in |
      std::stringstream::out);
   veldir_string_stream << veldir;
   profname=profname+veldir_string_stream.str();

   BLProfiler bprof(profname);
#endif
  
    // in: ABec_3D.F90
   FORT_DIAGSUM(
    y[mfi].dataPtr(veldir),
    ARLIM(y[mfi].loVect()), ARLIM(y[mfi].hiVect()),
    bX[mfi].dataPtr(veldir), 
    ARLIM(bX[mfi].loVect()), ARLIM(bX[mfi].hiVect()),
    bY[mfi].dataPtr(veldir), 
    ARLIM(bY[mfi].loVect()), ARLIM(bY[mfi].hiVect()),
    bZ[mfi].dataPtr(veldir), 
    ARLIM(bZ[mfi].loVect()), ARLIM(bZ[mfi].hiVect()),
    tilelo,tilehi,
    fablo,fabhi,&bfact,&bfact_top);

#if (profile_solver==1)
   bprof.stop();
#endif
  } // veldir
 } // mfi
} // omp

 thread_class::sync_tile_d_numPts();
 ParallelDescriptor::ReduceRealSum(thread_class::tile_d_numPts[0]);
 thread_class::reconcile_d_numPts(9);

} // end subroutine Fdiagsum



// z=K^{-1}r
// z=project_null_space(z)
void
ABecLaplacian::pcg_solve(
    MultiFab* z_in,
    MultiFab* r_in,
    Real eps_abs,Real bot_atol,
    MultiFab* pbdryhom_in,
    Vector<int> bcpres_array,
    int usecg_at_bottom,
    int smooth_type,int bottom_smooth_type,
    int presmooth,int postsmooth,
    int use_PCG,
    int level,
    int caller_id) {

#if (profile_solver==1)
 std::string subname="ABecLaplacian::pcg_solve";
 std::stringstream popt_string_stream(std::stringstream::in |
      std::stringstream::out);
 popt_string_stream << cfd_project_option;
 std::string profname=subname+popt_string_stream.str();
 profname=profname+"_";
 std::stringstream lev_string_stream(std::stringstream::in |
      std::stringstream::out);
 lev_string_stream << level;
 profname=profname+lev_string_stream.str();

 BLProfiler bprof(profname);

 bprof.stop();
#endif

 project_null_space(*r_in,level);

 // z=K^{-1} r
 z_in->setVal(0.0,0,nsolve_bicgstab,nghostSOLN);
 if (use_PCG==0) {
  MultiFab::Copy(*z_in,
		 *r_in,0,0,nsolve_bicgstab,nghostRHS);
 } else if (use_PCG==1) {
  if ((CG_use_mg_precond_at_top==1)&&
      (level==0)&&
      (MG_numlevels_var-1>0)) {

   if (CG_numlevels_var==2) {
    if (CG_verbose>2) {
     std::cout << "calling MG_solve level=" << level << '\n';
     std::cout << "calling MG_solve presmooth=" << presmooth << '\n';
     std::cout << "calling MG_solve postsmooth=" << postsmooth << '\n';
    }
    MG_solve(0,
      *z_in,
      *r_in,
      eps_abs,bot_atol,
      usecg_at_bottom,*pbdryhom_in,bcpres_array,
      smooth_type,bottom_smooth_type,
      presmooth,postsmooth);
   } else
    amrex::Error("CG_numlevels_var invalid");

  } else if ((CG_use_mg_precond_at_top==0)||
	     (level==MG_numlevels_var-1)) {
   for (int j=0;j<presmooth+postsmooth;j++) {
    if (CG_verbose>2) {
     std::cout << "calling smooth level=" << level << '\n';
     std::cout << "calling smooth j=" << j << '\n';
     std::cout << "calling smooth presmooth=" << presmooth << '\n';
     std::cout << "calling smooth postsmooth=" << postsmooth << '\n';
    }
    smooth(*z_in,
	   *r_in,
	   level,
	   *pbdryhom_in,bcpres_array,smooth_type);
    project_null_space(*z_in,level);
   }
  } else
   amrex::Error("use_mg_precond invalid");
 } else
  amrex::Error("use_PCG invalid");

 project_null_space(*z_in,level);

 if (CG_verbose>2) {
  std::cout << "end of pcg_solve level=" << level << '\n';
  std::cout << "end of pcg_solve caller_id=" << caller_id << '\n';
 }

} // subroutine pcg_solve



// z=K^{-1}r
// z=project_null_space(z)
void
ABecLaplacian::pcg_MULTI_solve(
    int gmres_precond_iter,
    MultiFab* z_in,
    MultiFab* r_in,
    Real eps_abs,Real bot_atol,
    MultiFab* pbdryhom_in,
    Vector<int> bcpres_array,
    int usecg_at_bottom,
    int smooth_type,int bottom_smooth_type,
    int presmooth,int postsmooth,
    int use_PCG,
    int level,
    int caller_id) {

#if (profile_solver==1)
 std::string subname="ABecLaplacian::pcg_MULTI_solve";
 std::stringstream popt_string_stream(std::stringstream::in |
      std::stringstream::out);
 popt_string_stream << cfd_project_option;
 std::string profname=subname+popt_string_stream.str();
 profname=profname+"_";
 std::stringstream lev_string_stream(std::stringstream::in |
      std::stringstream::out);
 lev_string_stream << level;
 profname=profname+lev_string_stream.str();

 BLProfiler bprof(profname);

 bprof.stop();
#endif

 if (nsolve_bicgstab>=1) {
  // do nothing
 } else
  amrex::Error("nsolve_bicgstab invalid");

 if (gmres_precond_iter_base_mg>=1) {
  // do nothing
 } else
  amrex::Error("gmres_precond_iter_base_mg invalid");

 int coarsefine=0;
 if (level==0) {
  coarsefine=0;
 } else if ((level==MG_numlevels_var-1)&&(CG_numlevels_var==2)) {
  coarsefine=1;
 } else
  amrex::Error("level invalid");

 project_null_space(*r_in,level);

 // z=K^{-1} r
 z_in->setVal(0.0,0,nsolve_bicgstab,nghostSOLN);

 if (gmres_precond_iter==0) {
   // Z=M^{-1}R
   // z=project_null_space(z)
   // first calls: z_in->setVal(0.0,0,nsolve_bicgstab,nghostSOLN)
  pcg_solve(
   z_in,r_in,
   eps_abs,bot_atol,
   pbdryhom_in,
   bcpres_array,
   usecg_at_bottom,
   smooth_type,bottom_smooth_type,
   presmooth,postsmooth,
   use_PCG,
   level,1);

 } else if ((gmres_precond_iter>0)&&
            (gmres_precond_iter<=gmres_max_iter)) {

  GMRES_V_MF[0][coarsefine]->setVal(0.0,0,nsolve_bicgstab,nghostRHS);
  GMRES_Z_MF[0][coarsefine]->setVal(0.0,0,nsolve_bicgstab,nghostSOLN);

  MultiFab::Copy(*GMRES_V_MF[0][coarsefine],*r_in,
                 0,0,nsolve_bicgstab,nghostRHS);

  for (int vcycle_iter=0;vcycle_iter<gmres_precond_iter;vcycle_iter++) {

   // Z=M^{-1}R
   // z=project_null_space(z)
   pcg_solve(
    GMRES_Z_MF[0][coarsefine],
    GMRES_V_MF[0][coarsefine],
    eps_abs,bot_atol,
    pbdryhom_in,
    bcpres_array,
    usecg_at_bottom,
    smooth_type,bottom_smooth_type,
    presmooth,postsmooth,
    use_PCG,
    level,1);

   // z_in=_z_in+GMRES_Z_MF
   // src,src_comp,dest_comp,num_comp,src_nghost,dst_nghost
   z_in->ParallelAdd(*GMRES_Z_MF[0][coarsefine],0,0,nsolve_bicgstab,0,0);
   project_null_space(*z_in,level);
   GMRES_Z_MF[0][coarsefine]->setVal(0.0,0,nsolve_bicgstab,nghostSOLN);
   // resid,rhs,soln
   residual((*GMRES_V_MF[0][coarsefine]),  // resid
            (*r_in),  // rhs
            (*z_in),  // soln
            level,
            (*pbdryhom_in),bcpres_array);
   project_null_space((*GMRES_V_MF[0][coarsefine]),level);
  } // vcycle_iter=0...gmres_precond_iter-1
 } else
  amrex::Error("gmres_precond_iter invalid");

 project_null_space(*z_in,level);

 if (CG_verbose>2) {
  std::cout << "end of pcg_MULTI_solve level=" << level << '\n';
  std::cout << "end of pcg_MULTI_solve caller_id=" << caller_id << '\n';
 }

} // subroutine pcg_MULTI_solve



// z=K^{-1}r
void
ABecLaplacian::pcg_GMRES_solve(
    int gmres_precond_iter,
    MultiFab* z_in,
    MultiFab* r_in,
    Real eps_abs,Real bot_atol,
    MultiFab* pbdryhom_in,
    Vector<int> bcpres_array,
    int usecg_at_bottom,
    int smooth_type,int bottom_smooth_type,
    int presmooth,int postsmooth,
    int use_PCG,
    int level,
    int nit) {

 int caller_id=0;

 if (nsolve_bicgstab>=1) {
  // do nothing
 } else
  amrex::Error("nsolve_bicgstab invalid");

 if (gmres_precond_iter_base_mg>=1) {
  // do nothing
 } else
  amrex::Error("gmres_precond_iter_base_mg invalid");

 int coarsefine=0;
 if (level==0) {
  coarsefine=0;
 } else if ((level==MG_numlevels_var-1)&&(CG_numlevels_var==2)) {
  coarsefine=1;
 } else
  amrex::Error("level invalid");

 project_null_space((*r_in),level);

 // z=K^{-1} r
 z_in->setVal(0.0,0,nsolve_bicgstab,nghostSOLN);

 if (gmres_precond_iter==0) {
   // Z=M^{-1}R
   // z=project_null_space(z)
   // first calls: z_in->setVal(0.0,0,nsolve_bicgstab,nghostSOLN)
  pcg_solve(
   z_in,r_in,
   eps_abs,bot_atol,
   pbdryhom_in,
   bcpres_array,
   usecg_at_bottom,
   smooth_type,bottom_smooth_type,
   presmooth,postsmooth,
   use_PCG,
   level,1);

 } else if ((gmres_precond_iter>0)&&
            (gmres_precond_iter<=gmres_max_iter)) {

  int m=nsolve_bicgstab*gmres_precond_iter;

  Real GMRES_tol=KRYLOV_NORM_CUTOFF;
  Real beta=0.0;
  LP_dot(*r_in,*r_in,level,beta);

  if (beta>0.0) {

   beta=sqrt(beta);

   GMRES_V_MF[0][coarsefine]->setVal(0.0,0,nsolve_bicgstab,nghostRHS);
   GMRES_Z_MF[0][coarsefine]->setVal(0.0,0,nsolve_bicgstab,nghostSOLN);

   Real aa=1.0/beta;
    // V0=V0+aa R = R/||R||
   CG_advance((*GMRES_V_MF[0][coarsefine]),aa,
              (*GMRES_V_MF[0][coarsefine]),(*r_in),level);
   project_null_space((*GMRES_V_MF[0][coarsefine]),level);

    // H_j is a j+2 x j+1 matrix  j=0..m-1
   Real** HH=new Real*[m+1];
   for (int i=0;i<m+1;i++) { 
    HH[i]=new Real[m];
    for (int j=0;j<m;j++) 
     HH[i][j]=0.0;
   }

   Real* yy=new Real[m];
   Real* beta_e1=new Real[2*(m+1)];
   for (int j=0;j<2*(m+1);j++)
    beta_e1[j]=0.0;
   beta_e1[0]=beta;

   int status=1;

   int m_small=m;

   if (breakdown_free_flag==0) {

    for (int j=1;j<m;j++) {
     if (j>=gmres_precond_iter_base_mg*nsolve_bicgstab) {
      GMRES_V_MF[j][coarsefine]=new MultiFab(gbox[level],dmap_array[level],
       nsolve_bicgstab,nghostRHS,
       MFInfo().SetTag("GMRES_V_MF"),FArrayBoxFactory()); 
      GMRES_Z_MF[j][coarsefine]=new MultiFab(gbox[level],dmap_array[level],
       nsolve_bicgstab,nghostSOLN,
       MFInfo().SetTag("GMRES_Z_MF"),FArrayBoxFactory()); 
     }

     GMRES_V_MF[j][coarsefine]->setVal(0.0,0,nsolve_bicgstab,nghostRHS);
     GMRES_Z_MF[j][coarsefine]->setVal(0.0,0,nsolve_bicgstab,nghostSOLN);
    } // j=1..m 

    for (int j=0;j<m_small;j++) {

     GMRES_W_MF[coarsefine]->setVal(0.0,0,nsolve_bicgstab,nghostRHS);

      // Zj=M^{-1}Vj
      // Z=project_null_space(Z)
      // Vj is a normalized vector
     pcg_solve(
      GMRES_Z_MF[j][coarsefine],
      GMRES_V_MF[j][coarsefine],
      eps_abs,bot_atol,
      pbdryhom_in,
      bcpres_array,
      usecg_at_bottom,
      smooth_type,bottom_smooth_type,
      presmooth,postsmooth,
      use_PCG,
      level,2);

     // w=A Z
     apply(*GMRES_W_MF[coarsefine],
          *GMRES_Z_MF[j][coarsefine],
          level,*pbdryhom_in,bcpres_array);
     project_null_space(*GMRES_W_MF[coarsefine],level);

     for (int i=0;i<=j;i++) {
      LP_dot(*GMRES_W_MF[coarsefine],
             *GMRES_V_MF[i][coarsefine],level,HH[i][j]);

      aa=-HH[i][j];
       // W=W+aa Vi
      CG_advance((*GMRES_W_MF[coarsefine]),aa,
                 (*GMRES_W_MF[coarsefine]),
                 (*GMRES_V_MF[i][coarsefine]),level);
      project_null_space(*GMRES_W_MF[coarsefine],level);
     } // i=0..j

     LP_dot(*GMRES_W_MF[coarsefine],
            *GMRES_W_MF[coarsefine],level,HH[j+1][j]);

     if (HH[j+1][j]>=0.0) {
      HH[j+1][j]=sqrt(HH[j+1][j]);
     } else
      amrex::Error("HH[j+1][j] invalid");
      
     if (HH[j+1][j]>KRYLOV_NORM_CUTOFF) {

      status=1;

      if ((j>=0)&&(j<m_small)) {
       caller_id=33;
       GMRES_MIN_CPP(HH,beta,yy,
	m,j+1,
	caller_id,cfd_project_option,
        level,status);
      } else
       amrex::Error("j invalid");

      if (status==1) {
 
       if ((j>=0)&&(j<m-1)) {
        aa=1.0/HH[j+1][j];
         // V=V+aa W
        CG_advance((*GMRES_V_MF[j+1][coarsefine]),aa,
                   (*GMRES_V_MF[j+1][coarsefine]),
                   (*GMRES_W_MF[coarsefine]),level);
        project_null_space(*GMRES_V_MF[j+1][coarsefine],level);
       } else if (j==m-1) {
        // do nothing
       } else
        amrex::Error("j invalid");

      } else if (status==0) {

       m_small=j;

       if (1==0) {
        std::cout << "j= " << j << " HH[j][j]= " <<
          HH[j][j] << endl;
        std::cout << "j= " << j << " HH[j+1][j]= " <<
          HH[j+1][j] << endl;
	std::cout << "j= " << j << " beta= " << beta << endl;
       }

      } else
       amrex::Error("status invalid");

     } else if ((HH[j+1][j]>=0.0)&&(HH[j+1][j]<=KRYLOV_NORM_CUTOFF)) {
      m_small=j;
     } else {
      std::cout << "HH[j+1][j]= " << HH[j+1][j] << '\n';
      amrex::Error("HH[j+1][j] invalid");
     }

    } // j=0..m_small-1
 
    status=1;

    if ((m_small>=1)&&
        (m_small<=m)) {

     caller_id=3;
     GMRES_MIN_CPP(HH,beta,yy,
       m,m_small,
       caller_id,cfd_project_option,
       level,status);

     if (status==1) {
      // do nothing
     } else
      amrex::Error("expecting status==1 here");

    } else if (m_small==0) {

     // Z=M^{-1}R
     // Z=project_null_space(Z)
     // first calls: z_in->setVal(0.0,0,nsolve_bicgstab,nghostSOLN)
     pcg_solve(
      z_in,r_in,
      eps_abs,bot_atol,
      pbdryhom_in,
      bcpres_array,
      usecg_at_bottom,
      smooth_type,bottom_smooth_type,
      presmooth,postsmooth,
      use_PCG,
      level,3);

    } else {
     std::cout << "m_small= " << m_small << '\n';
     amrex::Error("CGSolver.cpp m_small invalid");
    }
 
    if (status==1) {
     z_in->setVal(0.0,0,nsolve_bicgstab,nghostSOLN);
     for (int j=0;j<m_small;j++) {
      aa=yy[j];
       // Z=Z+aa Zj
      CG_advance((*z_in),aa,(*z_in),
                 (*GMRES_Z_MF[j][coarsefine]),level);
      project_null_space((*z_in),level);
     }
    } else
     amrex::Error("status invalid");

    for (int j=gmres_precond_iter_base_mg*nsolve_bicgstab;j<m;j++) {
     delete GMRES_V_MF[j][coarsefine];
     delete GMRES_Z_MF[j][coarsefine];
     GMRES_V_MF[j][coarsefine]=(MultiFab*)0;
     GMRES_Z_MF[j][coarsefine]=(MultiFab*)0;
    }

   } else if (breakdown_free_flag==1) {

    Real breakdown_tol=KRYLOV_NORM_CUTOFF;

     // G_j is a p(j)+1 x j+1 matrix   j=0..m-1
    Real** GG=new Real*[m+1];
    for (int i=0;i<m+1;i++) { 
     GG[i]=new Real[m];
     for (int j=0;j<m;j++) 
      GG[i][j]=0.0;
    }

    Real** HHGG=new Real*[2*m+2];
    for (int i=0;i<2*m+2;i++) { 
     HHGG[i]=new Real[2*m+1];
     for (int j=0;j<2*m+1;j++) 
      HHGG[i][j]=0.0;
    }

    int convergence_flag=0;
    int p_local=-1;
    int p_local_allocated=gmres_precond_iter_base_mg*nsolve_bicgstab;
    int p_local_init_allocated=p_local_allocated;

    int j_local=0;

    if (debug_BF_GMRES==1) {
     std::cout << "BF_GMRES mglib: level= " << level 
	    << "gmres_precond_iter= " << gmres_precond_iter << '\n';
     std::cout << "BF_GMRES mglib: beta= " << beta << '\n';
    }

    Vector<Real> error_history;
    error_history.resize(m);

    int use_previous_iterate=0;

    for (j_local=0;((j_local<m_small)&&(convergence_flag==0));j_local++) {

     use_previous_iterate=0;

     if (j_local>=gmres_precond_iter_base_mg*nsolve_bicgstab) {
      GMRES_Z_MF[j_local][coarsefine]=
        new MultiFab(gbox[level],dmap_array[level],
                     nsolve_bicgstab,nghostSOLN,
                     MFInfo().SetTag("GMRES_Z_MF"),FArrayBoxFactory()); 
     }
     GMRES_Z_MF[j_local][coarsefine]->setVal(0.0,0,nsolve_bicgstab,nghostSOLN);

     GMRES_W_MF[coarsefine]->setVal(0.0,0,nsolve_bicgstab,nghostRHS);

      // Zj=M^{-1}Vj
      // z=project_null_space(z)
     pcg_solve(
      GMRES_Z_MF[j_local][coarsefine],
      GMRES_V_MF[j_local][coarsefine],
      eps_abs,bot_atol,
      pbdryhom_in,
      bcpres_array,
      usecg_at_bottom,
      smooth_type,bottom_smooth_type,
      presmooth,postsmooth,
      use_PCG,
      level,4);

     // w=A Z
     apply(*GMRES_W_MF[coarsefine],
          *GMRES_Z_MF[j_local][coarsefine],
          level,*pbdryhom_in,bcpres_array);
     project_null_space(*GMRES_W_MF[coarsefine],level);

      // H_j     is a j+2 x j+1 matrix  j=0..m-1
      // H_{j-1} is a j+1 x j   matrix  j=0..m-1
      // This step is equivalent to:
      // H_j=[ H_{j-1}    h_{j}  
      //         0        h_{j+1,j}  ]
      // h_{j} is a j+1 x 1 vector.
     for (int i=0;i<=j_local;i++) {
       // H_ij=W dot Vi
      LP_dot(*GMRES_W_MF[coarsefine],
	     *GMRES_V_MF[i][coarsefine],level,HH[i][j_local]);
     } // i=0..j_local

      // p_local=p(j-1)
      // G_j      is a p(j)+1   x j+1 matrix  j=0..m-1
      // G_{j-1}  is a p(j-1)+1 x j   matrix  j=0..m-1
      // G'_j     is a p(j-1)+1 x j+1 matrix  j=0..m-1
      // This step is equivalent to:
      // G'_j= [ G_{j-1} g_{j} ]
      // g_{j} is a p(j-1)+1 x 1 vector
     for (int i=0;i<=p_local;i++) {
       // G_ij=W dot Ui
      LP_dot(*GMRES_W_MF[coarsefine],
	     *GMRES_U_MF[i][coarsefine],level,GG[i][j_local]);
     } // i=0..p_local

     for (int i=0;i<=j_local;i++) {
      aa=-HH[i][j_local];
       // W=W+aa Vi
      CG_advance((*GMRES_W_MF[coarsefine]),aa,
                 (*GMRES_W_MF[coarsefine]),
                 (*GMRES_V_MF[i][coarsefine]),level);
      project_null_space(*GMRES_W_MF[coarsefine],level);
     }
     for (int i=0;i<=p_local;i++) {
      aa=-GG[i][j_local];
       // W=W+aa Ui
      CG_advance((*GMRES_W_MF[coarsefine]),aa,
                 (*GMRES_W_MF[coarsefine]),
                 (*GMRES_U_MF[i][coarsefine]),level);
      project_null_space(*GMRES_W_MF[coarsefine],level);
     }

      // H_j is a j+2 x j+1 matrix  j=0..m-1
     LP_dot(*GMRES_W_MF[coarsefine],
            *GMRES_W_MF[coarsefine],level,HH[j_local+1][j_local]);

     if (HH[j_local+1][j_local]>=0.0) {
      HH[j_local+1][j_local]=sqrt(HH[j_local+1][j_local]);
     } else
      amrex::Error("HH[j_local+1][j_local] invalid");

     double local_tol=breakdown_tol;
     if (p_local>=0) {
      local_tol/=pow(10.0,2*p_local);
     } else if (p_local==-1) {
      // do nothing
     } else
      amrex::Error("p_local invalid");

       // NOTE: if h_{j+1,j}==0 this means that ||w||=0,
       //  which means that the new candidate v_{j+1} will
       //  be a linear combination of v_{0},...,v_{j}.
       //  Also, if h_{j+1,j}=0, this means that the last
       //  row of H_j will be all zeroes which implies that the
       //  last column of H_{j}^T will be all zeros.  
       //  But, h_{j+1,j}=0, does not imply that 
       //  condnum(H_{j})=infinity.  Theoretically, ||w||<>0
       //  if h_j not the zero vector?

     int condition_number_blowup=0;
     double zeyu_condnum=1.0;

     if (HH[j_local+1][j_local]>KRYLOV_NORM_CUTOFF) {

      if (CG_verbose>2) {
       std::cout << "calling CondNum level=" << level << '\n';
       std::cout << "calling CondNum j_local=" << j_local << '\n';
       std::cout << "calling CondNum m=" << m << '\n';
      }

       // Real** HH   i=0..m  j=0..m-1
       // active region: i=0..j_local+1  j=0..j_local
      if (disable_additional_basis==0) {
       zeyu_condnum=CondNum(HH,m+1,m,j_local+2,j_local+1,local_tol);
      } else if (disable_additional_basis==1) {
       zeyu_condnum=0.0;
      } else
       amrex::Error("disable_additional_basis invalid");

      if (CG_verbose>2) {
       std::cout << "after CondNum level=" << level << '\n';
       std::cout << "after CondNum zeyu_condnum=" << zeyu_condnum << '\n';
      }
      if (zeyu_condnum>1.0/local_tol) { 
       condition_number_blowup=1;  
      } else if (zeyu_condnum<=1.0/local_tol) {
       condition_number_blowup=0;  
      } else
       amrex::Error("zeyu_condnum NaN");

     } else if ((HH[j_local+1][j_local]>=0.0)&&
		(HH[j_local+1][j_local]<=KRYLOV_NORM_CUTOFF)) {
      condition_number_blowup=2;  
      use_previous_iterate=1;
     } else
      amrex::Error("HH[j_local+1][j_local] invalid");

     if (debug_BF_GMRES==1) {
      std::cout << "BFGMRES mglib: j_local="<< j_local << '\n';
      std::cout << "BFGMRES mglib: zeyu_condnum="<< zeyu_condnum << '\n';
      std::cout << "BFGMRES mglib: condition_number_blowup="<< 
	     condition_number_blowup << '\n';

      std::cout << "BFGMRES mglib: HH[j_local+1][j_local]=" <<
	      HH[j_local+1][j_local] << '\n';
      std::cout << "BFGMRES mglib: HH[j_local+1][j_local]=" <<
	      HH[j_local+1][j_local] << '\n';
     }

     if (condition_number_blowup==1) {

      p_local++;

       // variables initialized to 0.0  
      if (p_local>=p_local_allocated) {
       GMRES_U_MF[p_local][coarsefine]=
	 new MultiFab(gbox[level],dmap_array[level],
                      nsolve_bicgstab,nghostRHS,
                      MFInfo().SetTag("GMRES_U_MF"),FArrayBoxFactory()); 
       p_local_allocated++;
      }

      GMRES_U_MF[p_local][coarsefine]->setVal(0.0,0,nsolve_bicgstab,nghostRHS);
    
      MultiFab::Copy(*GMRES_U_MF[p_local][coarsefine],
		     *GMRES_V_MF[j_local][coarsefine],0,0,
		     nsolve_bicgstab,nghostRHS);

      if (j_local==0) {
	// do nothing if j_local==0

        // In the paper by Reichel and Ye:
	//  H_k     is a k+1 x k matrix  k=1..m	      
	//  H_{k-1} is a k x k-1 matrix  k=1..m	      
	// In this code:
        // G_{j-1} is a p(j-1)+1 x j   matrix  j=0..m-1
        // G'_j    is a p(j-1)+1 x j+1 matrix  j=0..m-1
        // H_{j}   is a j+2 x j+1      matrix  j=0..m-1
        // H_{j-1} is a j+1 x j        matrix  j=0..m-1
	// G_j     is a p(j)+1 x j+1   matrix  j=0..m-1
	// G_j= [                          G'_j
	//        last (j+1)th row of H_{j-1}        0  ]
      } else if ((j_local>=1)&&(j_local<m)) {
       for (int i=0;i<j_local;i++) {
        GG[p_local][i]=HH[j_local][i];
	HH[j_local][i]=0.0;
       }
      } else
       amrex::Error("j_local invalid");

      int max_vhat_sweeps=4;
      int vhat_counter=0;
      for (vhat_counter=0;((vhat_counter<max_vhat_sweeps)&&
			   (condition_number_blowup==1));vhat_counter++) {

       GMRES_V_MF[j_local][coarsefine]->setVal(1.0,0,nsolve_bicgstab,nghostRHS);
       init_checkerboard((*GMRES_V_MF[j_local][coarsefine]),level);
       project_null_space((*GMRES_V_MF[j_local][coarsefine]),level);
        // v_i dot v_j = 0 i=0..j-1
	// u_p dot v_j = 0
	// for i=0..j-1
	//  v_j = v_j - (vj,vi)vi/(vi,vi)

       double vj_dot_vi=0.0;
       double vi_dot_vi=0.0;
       for (int i=0;i<=j_local;i++) {
	MultiFab* vi_MF;
        if ((i>=0)&&(i<j_local)) {
         vi_MF=GMRES_V_MF[i][coarsefine];
        } else if (i==j_local) {
         vi_MF=GMRES_U_MF[p_local][coarsefine];
        } else
         amrex::Error("i invalid");

	 // SANITY CHECK
        LP_dot((*GMRES_V_MF[j_local][coarsefine]),
               (*GMRES_V_MF[j_local][coarsefine]),
    	       level,vi_dot_vi);
	
        if (vi_dot_vi>0.0) {
	 // do nothing
	} else {	
         std::cout << "vi_dot_vi= " << vi_dot_vi << endl;
	 std::cout << "i= " << i << endl;
         std::cout << "vhat_counter,j_local,p_local,m " <<
          vhat_counter << ' ' << ' ' << j_local << ' ' <<
          p_local << ' ' << m << endl;
         std::cout << "beta= " << beta << endl;
         for (int eh=0;eh<j_local;eh++) {
          std::cout << "eh,error_history[eh] " << eh << ' ' <<
           error_history[eh] << endl;
         }
         std::cout << "level= " << level << endl;
         std::cout << "nsolve_bicgstab= " << nsolve_bicgstab << endl;
         std::cout << "cfd_project_option= " << cfd_project_option << '\n';
         std::cout << "gbox[level].size()= " << gbox[level].size() << '\n';
         std::cout << "gbox[level]= " << gbox[level] << '\n';

         amrex::Error("vi_dot_vi==0.0 (mid loop sanity check)");
	}

        LP_dot(*vi_MF,
               *GMRES_V_MF[j_local][coarsefine],
	       level,vj_dot_vi);
        LP_dot(*vi_MF,
               *vi_MF,
	       level,vi_dot_vi);
	if (vi_dot_vi>0.0) {
         aa=-vj_dot_vi/vi_dot_vi;
          // vj=vj+aa vi
         CG_advance((*GMRES_V_MF[j_local][coarsefine]),aa,
                    (*GMRES_V_MF[j_local][coarsefine]),
                    *vi_MF,level);
         project_null_space(*GMRES_V_MF[j_local][coarsefine],level);
	} else {
         std::cout << "vi_dot_vi= " << vi_dot_vi << endl;
         std::cout << "vhat_counter,i,j_local,p_local,m " <<
           vhat_counter << ' ' << i << ' ' << j_local << ' ' <<
           p_local << ' ' << m << endl;
         std::cout << "beta= " << beta << endl;
         for (int eh=0;eh<j_local;eh++) {
          std::cout << "eh,error_history[eh] " << eh << ' ' <<
           error_history[eh] << endl;
         }
         std::cout << "level= " << level << endl;
         std::cout << "nsolve_bicgstab= " << nsolve_bicgstab << endl;
         std::cout << "cfd_project_option= " << cfd_project_option << '\n';
         std::cout << "gbox[level].size()= " << gbox[level].size() << '\n';
         std::cout << "gbox[level]= " << gbox[level] << '\n';
 	 amrex::Error("vi_dot_vi==0.0");
        }
       } // i=0..j_local
       LP_dot((*GMRES_V_MF[j_local][coarsefine]),
              (*GMRES_V_MF[j_local][coarsefine]),
	      level,vi_dot_vi);
       if (vi_dot_vi>0.0) {
        aa=1.0/vi_dot_vi;
	GMRES_V_MF[j_local][coarsefine]->mult(aa,0,nsolve_bicgstab,nghostRHS);
        project_null_space(*GMRES_V_MF[j_local][coarsefine],level);
       } else {

        std::cout << "vi_dot_vi= " << vi_dot_vi << endl;
        std::cout << "vhat_counter,j_local,p_local,m " <<
          vhat_counter << ' ' << ' ' << j_local << ' ' <<
          p_local << ' ' << m << endl;
        std::cout << "beta= " << beta << endl;
        for (int eh=0;eh<j_local;eh++) {
         std::cout << "eh,error_history[eh] " << eh << ' ' <<
          error_history[eh] << endl;
        }
        std::cout << "level= " << level << endl;
        std::cout << "nsolve_bicgstab= " << nsolve_bicgstab << endl;
        std::cout << "cfd_project_option= " << cfd_project_option << '\n';
        std::cout << "gbox[level].size()= " << gbox[level].size() << '\n';
        std::cout << "gbox[level]= " << gbox[level] << '\n';

        amrex::Error("vi_dot_vi==0.0 (renormalization)");
       }

        // Zj=M^{-1}Vj
        // z=project_null_space(z)
       pcg_solve(
        GMRES_Z_MF[j_local][coarsefine],
        GMRES_V_MF[j_local][coarsefine],
        eps_abs,bot_atol,
        pbdryhom_in,
        bcpres_array,
        usecg_at_bottom,
        smooth_type,bottom_smooth_type,
        presmooth,postsmooth,
        use_PCG,
        level,5);

       // w=A Z
       apply(*GMRES_W_MF[coarsefine],
            *GMRES_Z_MF[j_local][coarsefine],
            level,*pbdryhom_in,bcpres_array);
       project_null_space(*GMRES_W_MF[coarsefine],level);

        // H_j is a j+2 x j+1 matrix  j=0..m-1
	// last column of H_j is replaced by h_j
	//  (except the (j+2th,j+1th) component)
       for (int i=0;i<=j_local;i++) {
        // H_ij=W dot Vi
        LP_dot(*GMRES_W_MF[coarsefine],
  	       *GMRES_V_MF[i][coarsefine],level,HH[i][j_local]);
       } // i=0..j_local

        // G_j is a p(j)+1 x j+1 matrix  j=0..m-1
        // g_j is a p(j)+1 x 1   vector  j=0..m-1
       for (int i=0;i<=p_local;i++) {
        // G_ij=W dot Ui
	// last column of G_j is replaced by g_j
        LP_dot(*GMRES_W_MF[coarsefine],
  	       *GMRES_U_MF[i][coarsefine],level,GG[i][j_local]);
       } // i=0..p_local

       for (int i=0;i<=j_local;i++) {
        aa=-HH[i][j_local];
         // W=W+aa Vi
        CG_advance((*GMRES_W_MF[coarsefine]),aa,
                   (*GMRES_W_MF[coarsefine]),
                   (*GMRES_V_MF[i][coarsefine]),level);
       }
       for (int i=0;i<=p_local;i++) {
        aa=-GG[i][j_local];
         // W=W+aa Ui
        CG_advance((*GMRES_W_MF[coarsefine]),aa,
                   (*GMRES_W_MF[coarsefine]),
                   (*GMRES_U_MF[i][coarsefine]),level);
        project_null_space(*GMRES_W_MF[coarsefine],level);
       }

        // H_j is a j+2 x j+1 matrix  j=0..m-1
       LP_dot(*GMRES_W_MF[coarsefine],
              *GMRES_W_MF[coarsefine],level,HH[j_local+1][j_local]);

       condition_number_blowup=0;
       zeyu_condnum=1.0;

       if (HH[j_local+1][j_local]>=0.0) {
        HH[j_local+1][j_local]=sqrt(HH[j_local+1][j_local]);
       } else
        amrex::Error("HH[j_local+1][j_local] invalid");

       if (HH[j_local+1][j_local]>KRYLOV_NORM_CUTOFF) {
         // Real** HH   i=0..m  j=0..m-1
         // active region: i=0..j_local+1  j=0..j_local
         
        zeyu_condnum=CondNum(HH,m+1,m,j_local+2,j_local+1,local_tol);
        if (zeyu_condnum>1.0/local_tol) { 
         condition_number_blowup=1;  
        } else if (zeyu_condnum<=1.0/local_tol) {
         condition_number_blowup=0;  
        } else
         amrex::Error("zeyu_condnum NaN");

       } else if ((HH[j_local+1][j_local]>=0.0)&&
  		  (HH[j_local+1][j_local]<=KRYLOV_NORM_CUTOFF)) {
        condition_number_blowup=2;  
        use_previous_iterate=1;
	p_local--;
       } else
        amrex::Error("HH[j_local+1][j_local] invalid");

       if (debug_BF_GMRES==1) {
        std::cout << "BFGMRES: j_local="<< j_local << '\n';
        std::cout << "BFGMRES: p_local="<< p_local << '\n';
        std::cout << "BFGMRES: vhat_counter="<< vhat_counter << '\n';
        std::cout << "BFGMRES: zeyu_condnum="<< zeyu_condnum << '\n';
        std::cout << "BFGMRES: condition_number_blowup="<< 
	     condition_number_blowup << '\n';

        std::cout << "BFGMRES: HH[j_local+1][j_local]=" <<
	      HH[j_local+1][j_local] << '\n';
        std::cout << "BFGMRES: HH[j_local+1][j_local]=" <<
	      HH[j_local+1][j_local] << '\n';
       }

      } // vhat_counter=0..max_vhat_sweeps-1 or condition_number_blowup==0

      if (vhat_counter==max_vhat_sweeps) {

       use_previous_iterate=1;

      } else if ((vhat_counter>0)&&(vhat_counter<max_vhat_sweeps)) {
       if (condition_number_blowup==0) {
        // do nothing
       } else if (condition_number_blowup==2) {
        use_previous_iterate=1;
       } else
        amrex::Error("condition_number_blowup invalid");
      } else
       amrex::Error("vhat_counter invalid");

     } else if (condition_number_blowup==0) {
      // do nothing
     } else if (condition_number_blowup==2) {
      use_previous_iterate=1;
     } else
      amrex::Error("condition_number_blowup invalid");

     if (use_previous_iterate==0) {

       // H_j is a j+2 x j+1 matrix j=0..m-1
       // G_j is a p(j)+1 x j+1 matrix j=0..m-1
      for (int i1=0;i1<=j_local+1;i1++) {
       for (int i2=0;i2<=j_local;i2++) {
        HHGG[i1][i2]=HH[i1][i2];
       }
      }
      for (int i1=0;i1<=p_local;i1++) {
       for (int i2=0;i2<=j_local;i2++) {
        HHGG[i1+j_local+2][i2]=GG[i1][i2];
       }
      }
      status=1;
      z_in->setVal(0.0,0,nsolve_bicgstab,nghostSOLN);

       // sub box dimensions: p_local+j_local+3  x j_local+1
       // j_local=0..m-1
      LeastSquaresQR(HHGG,yy,beta_e1,2*m+2,2*m+1,
		    p_local+j_local+3,j_local+1);
      if (status==1) {
       for (int i2=0;i2<=j_local;i2++) {
        aa=yy[i2];
         // Z=Z+aa Z_{i2}
        CG_advance((*z_in),aa,(*z_in),
                   (*GMRES_Z_MF[i2][coarsefine]),level);
        project_null_space((*z_in),level);
       }
      } else
       amrex::Error("status invalid");

      if (disable_additional_basis==0) {

       project_null_space(*z_in,level);
       // resid,rhs,soln
       residual((*GMRES_W_MF[coarsefine]),  // resid
                (*r_in),  // rhs
                (*z_in),  // soln
                level,
  	        (*pbdryhom_in),bcpres_array);
       project_null_space((*GMRES_W_MF[coarsefine]),level);

       Real beta_compare=0.0;
       LP_dot(*GMRES_W_MF[coarsefine],
              *GMRES_W_MF[coarsefine],level,beta_compare);
       if (beta_compare>=0.0) {
        beta_compare=sqrt(beta_compare);
        if (beta_compare<=GMRES_tol*beta) {
         convergence_flag=1;
        } else if (beta_compare>GMRES_tol*beta) {
         convergence_flag=0;
        } else
         amrex::Error("beta_compare invalid");
       } else
        amrex::Error("beta_compare invalid");

       if (debug_BF_GMRES==1) {
        std::cout << "BFGMRES mglib: beta_compare=" <<
         beta_compare << " beta=" << beta << '\n';
       }

       error_history[j_local]=beta_compare;

       if (j_local==0) {
        if (beta_compare>beta) {
         convergence_flag=1;
        } else if (beta_compare<=beta) {
         // do nothing
        } else
         amrex::Error("beta_compare or beta invalid");
       } else if (j_local>0) {
        if (beta_compare>error_history[j_local-1]) {
         convergence_flag=1;
        } else if (beta_compare<=error_history[j_local-1]) {
         // do nothing
        } else
         amrex::Error("beta_compare or error_history[j_local-1] invalid");
       } else
        amrex::Error("j_local invalid");

      } else if (disable_additional_basis==1) {

       convergence_flag=0;

      } else
       amrex::Error("disable_additional_basis invalid");

      if (convergence_flag==0) {

       if (HH[j_local+1][j_local]>0.0) {
        if ((j_local>=0)&&(j_local<m-1)) {

         if (j_local+1>=gmres_precond_iter_base_mg*nsolve_bicgstab) {
          GMRES_V_MF[j_local+1][coarsefine]=
           new MultiFab(gbox[level],dmap_array[level],
                        nsolve_bicgstab,nghostRHS,
                        MFInfo().SetTag("GMRES_V_MF"),FArrayBoxFactory()); 
         }
         GMRES_V_MF[j_local+1][coarsefine]->
           setVal(0.0,0,nsolve_bicgstab,nghostRHS);

         aa=1.0/HH[j_local+1][j_local];
          // V=V+aa W
         CG_advance((*GMRES_V_MF[j_local+1][coarsefine]),aa,
                    (*GMRES_V_MF[j_local+1][coarsefine]),
                    (*GMRES_W_MF[coarsefine]),level);
         project_null_space(*GMRES_V_MF[j_local+1][coarsefine],level);
        } else if (j_local==m-1) {
         // do nothing
        } else
         amrex::Error("j_local invalid");
       } else if (HH[j_local+1][j_local]==0.0) {
        amrex::Error("HH[j_local+1][j_local] should not be 0");
       } else {
        amrex::Error("HH[j_local+1][j_local] invalid");
       }

      } else if (convergence_flag==1) {
       // do nothing
      } else
       amrex::Error("convergence_flag invalid");

     } else if (use_previous_iterate==1) {
      convergence_flag=1;
      if (j_local==0) {
 
       // Z=M^{-1}R
       // z=project_null_space(z)
       pcg_solve(
        z_in,r_in,
        eps_abs,bot_atol,
        pbdryhom_in,
        bcpres_array,
        usecg_at_bottom,
        smooth_type,bottom_smooth_type,
        presmooth,postsmooth,
        use_PCG,
        level,6);
      } 
     } else
      amrex::Error("use_previous_iterate invalid");

    } // j_local=0..m_small-1 (and convergence_flag==0)

    if (debug_BF_GMRES==1) {
     for (int i=0;i<j_local;i++) {
      std::cout << "mglib: i,error_history[i] " << i << ' ' <<
	     error_history[i] << endl;
     } 
    }

    for (int i=gmres_precond_iter_base_mg*nsolve_bicgstab;i<j_local;i++) {
     delete GMRES_Z_MF[i][coarsefine];
     GMRES_Z_MF[i][coarsefine]=(MultiFab*)0;
    }
    for (int i=gmres_precond_iter_base_mg*nsolve_bicgstab;i<j_local;i++) {
     delete GMRES_V_MF[i][coarsefine];
     GMRES_V_MF[i][coarsefine]=(MultiFab*)0;
    }
    for (int i=p_local_init_allocated;i<p_local_allocated;i++) {
     delete GMRES_U_MF[i][coarsefine];
     GMRES_U_MF[i][coarsefine]=(MultiFab*)0;
    }

    for (int i=0;i<m+1;i++) 
     delete [] GG[i];
    delete [] GG;

    for (int i=0;i<2*m+2;i++) 
     delete [] HHGG[i];
    delete [] HHGG;

   } else
    amrex::Error("breakdown_free_flag invalid");

   delete [] yy;
   delete [] beta_e1;

   for (int i=0;i<m+1;i++) 
    delete [] HH[i];
   delete [] HH;

  } else if (beta==0.0) {

   // Z=M^{-1}R
   // z=project_null_space(z)
   pcg_solve(
    z_in,r_in,
    eps_abs,bot_atol,
    pbdryhom_in,
    bcpres_array,
    usecg_at_bottom,
    smooth_type,bottom_smooth_type,
    presmooth,postsmooth,
    use_PCG,
    level,6);

  } else
   amrex::Error("beta invalid");
  
 } else {
  std::cout << "gmres_precond_iter= " << gmres_precond_iter << '\n';
  std::cout << "level= " << level << '\n';
  std::cout << "cfd_project_option= " << cfd_project_option << '\n';
  std::cout << "MG_numlevels_var= " << MG_numlevels_var << '\n';
  std::cout << "CG_numlevels_var= " << CG_numlevels_var << '\n';

  int dump_size=CG_error_history.size();
  if (nit+1<dump_size)
   dump_size=nit+1;
  for (int ehist=0;ehist<dump_size;ehist++) {
   std::cout << "nit " << ehist << " CG_error_history coarsefine " <<
     coarsefine << " rnorm=" <<
     CG_error_history[ehist][2*coarsefine+0] << " eps_abs=" <<
     CG_error_history[ehist][2*coarsefine+1] << '\n';
   std::cout << "nit " << ehist << " CG_A_error_history coarsefine " <<
     coarsefine << " Ar_norm=" <<
     CG_A_error_history[ehist][2*coarsefine+0] << " eps_abs=" <<
     CG_A_error_history[ehist][2*coarsefine+1] << '\n';
   std::cout << "nit " << ehist << " CG_rAr_error_history coarsefine " <<
     coarsefine << " rAr_norm=" <<
     CG_rAr_error_history[ehist][2*coarsefine+0] << " eps_abs=" <<
     CG_rAr_error_history[ehist][2*coarsefine+1] << '\n';
  }

  amrex::Error("ABecLaplacian.cpp: gmres_precond_iter invalid");
 }

  // if v in nullspace(A) then we require that the solution satisfies:
  // z dot v = 0 which will desingularize the matrix system.
  // (z=z0 + c v,  z dot v=0 => c=-z0 dot v/(v dot v)
 project_null_space((*z_in),level);

} // subroutine pcg_GMRES_solve

void 
ABecLaplacian::CG_check_for_convergence(
  int coarsefine,
  int presmooth,int postsmooth,
  Real rnorm,
  Real rnorm_init,
  Real Ar_norm,
  Real Ar_norm_init,
  Real rAr_norm,
  Real rAr_norm_init,
  Real eps_abs,
  Real relative_error,int nit,int& error_close_to_zero,
  int level) {

 if ((coarsefine==0)||
     (coarsefine==1)) {
  // do nothing
 } else
  amrex::Error("coarsefine invalid");

 int critical_nit=presmooth+postsmooth;
 Real critical_abs_tol=1.0e-14;
 Real critical_rel_tol=1.0e-14;
 if (critical_abs_tol>eps_abs)
  critical_abs_tol=eps_abs;
 if (critical_rel_tol>relative_error)
  critical_rel_tol=relative_error;

 Real scale_Ar_norm=Ar_norm;
 if (Ar_norm_init>0.0) {
  scale_Ar_norm=Ar_norm*rnorm_init/Ar_norm_init;
 } else if (Ar_norm_init==0.0) {
  // do nothing
 } else
  amrex::Error("Ar_norm_init invalid");

 int base_check=((rnorm<=eps_abs)||
                 (rnorm<=relative_error*rnorm_init));
 if (base_check==0) {
  base_check=((scale_Ar_norm<=eps_abs)||
              (Ar_norm<=relative_error*Ar_norm_init));
 } else if (base_check==1) {
  // do nothing
 } else
  amrex::Error("base_check invalid");

 Real scale_rAr_norm=rAr_norm;
 if (rAr_norm_init>0.0) {
  scale_rAr_norm=rAr_norm*rnorm_init/rAr_norm_init;
 } else if (rAr_norm_init==0.0) {
  // do nothing
 } else
  amrex::Error("rAr_norm_init invalid");

 if (base_check==0) {
  base_check=((scale_rAr_norm<=eps_abs)||
              (rAr_norm<=relative_error*rAr_norm_init));
 } else if (base_check==1) {
  // do nothing
 } else
  amrex::Error("base_check invalid");

 if (nit>critical_nit) {

  error_close_to_zero=base_check;

 } else if ((nit>=0)&&(nit<=critical_nit)) {

  error_close_to_zero=((rnorm<=critical_abs_tol)||
                       (rnorm<=critical_rel_tol*rnorm_init));

  if (error_close_to_zero==0) {
   error_close_to_zero=((scale_Ar_norm<=critical_abs_tol)||
                        (Ar_norm<=critical_rel_tol*Ar_norm_init));
  } else if (error_close_to_zero==1) {
   // do nothing
  } else
   amrex::Error("error_close_to_zero invalid");

  if (error_close_to_zero==0) {
   error_close_to_zero=((scale_rAr_norm<=critical_abs_tol)||
                        (rAr_norm<=critical_rel_tol*rAr_norm_init));
  } else if (error_close_to_zero==1) {
   // do nothing
  } else
   amrex::Error("error_close_to_zero invalid");



  if ((error_close_to_zero==0)&&
      (base_check==1))
   error_close_to_zero=2;

 } else
  amrex::Error("nit invalid");

 if (ParallelDescriptor::IOProcessor()) {
  if (CG_verbose>1) {
   std::cout << "in: CG_check_for_convergence nit= " << nit << 
	  " rnorm_init= " <<
	  rnorm_init << " rnorm= " << rnorm << '\n';
   std::cout << "in: CG_check_for_convergence nit= " << nit << 
	  " Ar_norm_init= " <<
	  Ar_norm_init << " Ar_norm= " << Ar_norm << '\n';
   std::cout << "in: CG_check_for_convergence nit= " << nit << 
	  " rAr_norm_init= " <<
	  rAr_norm_init << " rAr_norm= " << rAr_norm << '\n';
  }
 }

} // CG_check_for_convergence

void 
ABecLaplacian::CG_dump_params(
		Real rnorm,
		Real rnorm_init,
		Real Ar_norm,
		Real Ar_norm_init,
		Real rAr_norm,
		Real rAr_norm_init,
		Real eps_abs,Real relative_error,
                int is_bottom,Real bot_atol,
                int usecg_at_bottom,int smooth_type,
		int bottom_smooth_type,int presmooth,
		int postsmooth,MultiFab& mf1,
		MultiFab& mf2,int level) {

 if (ParallelDescriptor::IOProcessor()) {
  std::cout << "level= " << level << '\n';
  std::cout << "rnorm_init,rnorm,eps_abs,relative_error " <<
   rnorm_init << ' ' << rnorm << ' ' <<  eps_abs << 
   ' ' << relative_error << '\n';
  std::cout << "Ar_norm_init,Ar_norm,eps_abs,relative_error " <<
   Ar_norm_init << ' ' << Ar_norm << ' ' <<  eps_abs << 
   ' ' << relative_error << '\n';
  std::cout << "rAr_norm_init,rAr_norm,eps_abs,relative_error " <<
   rAr_norm_init << ' ' << rAr_norm << ' ' <<  eps_abs << 
   ' ' << relative_error << '\n';
  std::cout << "is_bottom= " << is_bottom << '\n';
  std::cout << "bot_atol= " << bot_atol << '\n';
  std::cout << "usecg_at_bottom= " << usecg_at_bottom << '\n';
  std::cout << "smooth_type= " << smooth_type << '\n';
  std::cout << "bottom_smooth_type= " << bottom_smooth_type << '\n';
  std::cout << "presmooth= " << presmooth << '\n';
  std::cout << "postsmooth= " << postsmooth << '\n';
  std::cout << "cfd_level= " << cfd_level << '\n';
  std::cout << "cfd_project_option= " << cfd_project_option << '\n';
  std::cout << "laplacian_solvability= " << 
          laplacian_solvability << '\n';
  std::cout << "nsolve_bicgstab= " << nsolve_bicgstab << '\n';
  std::cout << "gbox[0].size()= " << gbox[0].size() << '\n';
  std::cout << "numLevels()= " << MG_numlevels_var << '\n';
  std::cout << "mf1.boxArray()= " << mf1.boxArray() << '\n';
  std::cout << "LPnorm(mf1,lev)= " << LPnorm(mf1,level) << '\n';
  std::cout << "mf2.boxArray()= " << mf2.boxArray() << '\n';
  std::cout << "LPnorm(mf2,lev)= " << LPnorm(mf2,level) << '\n';
 }

} // end subroutine CG_dump_params

void
ABecLaplacian::CG_solve(
    int& cg_cycles_out,
    int nsverbose,int is_bottom,
    MultiFab& sol,
    MultiFab& rhs,
    Real eps_abs, // save_atol_b : caller
    Real bot_atol,// bottom_bottom_tol : caller
    MultiFab& pbdry,
    Vector<int> bcpres_array,
    int usecg_at_bottom,
    int& meets_tol,
    int smooth_type,int bottom_smooth_type,
    int presmooth,int postsmooth,
    Real& error_init,
    Real& A_error_init,
    Real& rAr_error_init,
    int level)
{

#if (profile_solver==1)
 std::string subname="ABecLaplacian::solve";
 std::stringstream popt_string_stream(std::stringstream::in |
      std::stringstream::out);
 popt_string_stream << cfd_project_option;
 std::string profname=subname+popt_string_stream.str();
 profname=profname+"_";
 std::stringstream lev_string_stream(std::stringstream::in |
      std::stringstream::out);
 lev_string_stream << level;
 profname=profname+lev_string_stream.str();
 BLProfiler bprof(profname);
#endif

 int coarsefine=0;
  // finest level of the hierarchy
 if (level==0) {
  coarsefine=0;
  // coarsest level of the hierarchy
 } else if ((level==MG_numlevels_var-1)&&(CG_numlevels_var==2)) {
  coarsefine=1;
 } else
  amrex::Error("level invalid CG_solve");

 //
 // algorithm:
 //
 //   k=0;r=rhs-A*soln_0;
 //   while (||r_k||^2_2 > eps^2*||r_o||^2_2 && k < maxiter {
 //      k++
 //      solve Mz_k-1 = r_k-1 (if preconditioning, else z_k-1 = r_k-1)
 //      rho_k-1 = r_k-1 dot z_k-1
 //      if (k=1) { p_1 = z_0 }
 //      else { beta = rho_k-1/rho_k-2; p = z + beta*p }
 //      Ap = A*p
 //      alpha = rho_k-1/(p dot Ap)
 //      x += alpha p
 //      r = b - A*x
 //   }
 //
 if (sol.boxArray() == LPboxArray(level)) {
  // do nothing
 } else
  amrex::Error("sol.boxArray() != LPboxArray(level)");

 if (rhs.boxArray() == LPboxArray(level)) {
  // do nothing
 } else
  amrex::Error("rhs.boxArray() != LPboxArray(level)");

 if (pbdry.boxArray() == LPboxArray(level)) {
  // do nothing
 } else
  amrex::Error("bdry.boxArray() != LPboxArray(level)");

 Real relative_error=1.0e-12;

 int ncomp = sol.nComp();
 if (ncomp!=nsolve_bicgstab)
  amrex::Error("ncomp invalid");

 CG_pbdryhom[coarsefine]->setVal(0.0,0,nsolve_bicgstab,nghostSOLN);

#if (profile_solver==1)
 bprof.stop();
#endif

 project_null_space(rhs,level);
 project_null_space(sol,level);

 CG_rhs_resid_cor_form[coarsefine]->setVal(0.0,0,nsolve_bicgstab,nghostRHS); 
 MultiFab::Copy(*CG_rhs_resid_cor_form[coarsefine],
   rhs,0,0,nsolve_bicgstab,nghostRHS);

 if ((CG_verbose>0)||(nsverbose>0)||(1==0))
  if (ParallelDescriptor::IOProcessor())
   std::cout << "CGSolver: is_bottom= " << is_bottom << '\n';

  // resid,rhs,soln
 residual((*CG_r[coarsefine]),(*CG_rhs_resid_cor_form[coarsefine]),
    sol,level,pbdry,bcpres_array);

 project_null_space((*CG_r[coarsefine]),level);

  // put solution and residual in residual correction form
 CG_delta_sol[coarsefine]->setVal(0.0,0,nsolve_bicgstab,1);
 MultiFab::Copy(*CG_rhs_resid_cor_form[coarsefine],
   *CG_r[coarsefine],0,0,nsolve_bicgstab,nghostRHS);

 Real rnorm = LPnorm(*CG_r[coarsefine], level);

 MultiFab::Copy(*CG_p_search_SOLN[coarsefine],
       *CG_r[coarsefine],0,0,nsolve_bicgstab,0);
 apply(*CG_Av_search[coarsefine],
       *CG_p_search_SOLN[coarsefine],level,
       *CG_pbdryhom[coarsefine],bcpres_array);
 project_null_space((*CG_Av_search[coarsefine]),level);

 Real Ar_norm = LPnorm(*CG_Av_search[coarsefine],level);

 Real rAr_norm=0.0;
 LP_dot(*CG_Av_search[coarsefine],*CG_r[coarsefine],level,rAr_norm); 
 if (rAr_norm<0.0) {
  rAr_norm=0.0;
 } else if (rAr_norm>=0.0) {
  rAr_norm=sqrt(rAr_norm);
 } else
  amrex::Error("rAr_norm invalid");

#if (profile_solver==1)
 bprof.start();
#endif

 if (rnorm>=0.0) {
  rnorm=sqrt(rnorm);
 } else {
  amrex::Error("rnorm invalid");
 }
 if (Ar_norm>=0.0) {
  Ar_norm=sqrt(Ar_norm);
 } else {
  amrex::Error("Ar_norm invalid");
 }
	 
 Real rnorm_init=rnorm;
 Real Ar_norm_init=Ar_norm;
 Real rAr_norm_init=rAr_norm;
 error_init=rnorm_init;
 A_error_init=Ar_norm_init;
 rAr_error_init=rAr_norm_init;

 if ((CG_verbose>0)||(nsverbose>0)) {
  if (ParallelDescriptor::IOProcessor()) {
   if (is_bottom==1)
    std::cout << "CGsolver(BOTTOM):Initial error(r0,Ar0,rAr0)="<<
	    rnorm << ' ' << Ar_norm << ' ' << rAr_norm << '\n';
   else
    std::cout << "CGsolver(NOBOT):Initial error(r0,Ar0,rAr0)="<<
	    rnorm << ' ' << Ar_norm << ' ' << rAr_norm << '\n';
  }
 }

 int nit=0;

 int use_PCG=1;

 if (use_PCG==0) {
  // do nothing
 } else if (CG_use_mg_precond_at_top==1) {
  // do nothing
 } else if (CG_use_mg_precond_at_top==0) {
  // do nothing
 } else {
  std::cout << "CG_use_mg_precond_at_top=" << 
	  CG_use_mg_precond_at_top << '\n';
  amrex::Error("CG_use_mg_precond_at_top invalid");
 }

 if (CG_z[coarsefine]->nComp()!=nsolve_bicgstab)
  amrex::Error("ncomp invalid");

 Real beta=0.0;
 Real rho=1.0;
 Real rho_old=1.0;
 Real omega=1.0;
 Real alpha=1.0;
 CG_p_search_SOLN[coarsefine]->setVal(0.0,0,nsolve_bicgstab,nghostSOLN); 
 CG_p_search[coarsefine]->setVal(0.0,0,nsolve_bicgstab,nghostRHS); 
 CG_v_search[coarsefine]->setVal(0.0,0,nsolve_bicgstab,nghostRHS); 

 Real restart_tol=0.0;
 int restart_flag=0;
 int gmres_precond_iter=gmres_precond_iter_base_mg;

 int local_presmooth=presmooth;
 int local_postsmooth=postsmooth;

  // check if the initial residual passes the convergence test.
 int error_close_to_zero=0;
 CG_check_for_convergence(
   coarsefine,
   local_presmooth,local_postsmooth,
   rnorm,
   rnorm_init,
   Ar_norm,
   Ar_norm_init,
   rAr_norm,
   rAr_norm_init,
   eps_abs,relative_error,nit,
   error_close_to_zero,level);

 if (ParallelDescriptor::IOProcessor()) {
  if (CG_verbose>1) {
   if (is_bottom==1)
    std::cout << "CGSolver(BOT): r0,Ar0,rAr0,eps_abs,eps_rel " <<
     rnorm_init << ' ' << 
     Ar_norm_init << ' ' << 
     rAr_norm_init << ' ' << 
     eps_abs << ' ' << relative_error << '\n';
   else
    std::cout << "CGSolver(NOBOT): r0,Ar0,rAr0,eps_abs,eps_rel " <<
     rnorm_init << ' ' << 
     Ar_norm_init << ' ' << 
     rAr_norm_init << ' ' << 
     eps_abs << ' ' << relative_error << '\n';
  }
 }

#if (profile_solver==1)
 bprof.stop();
#endif

 for (int ehist=0;ehist<CG_error_history.size();ehist++) {
  for (int ih=0;ih<2;ih++) {
   CG_error_history[ehist][2*coarsefine+ih]=0.0;
   CG_A_error_history[ehist][2*coarsefine+ih]=0.0;
   CG_rAr_error_history[ehist][2*coarsefine+ih]=0.0;
  }
 }

 int local_use_bicgstab=abec_use_bicgstab;

 for(nit = 0;((nit < CG_maxiter)&&(error_close_to_zero!=1)); ++nit) {

  restart_flag=0;

  rho_old=rho; // initially or on restart, rho_old=rho=1

  rnorm=LPnorm(*CG_r[coarsefine],level);

  if (rnorm>=0.0) {
   rnorm=sqrt(rnorm);
  } else {
   amrex::Error("rnorm invalid mglib");
  }

  MultiFab::Copy(*CG_p_search_SOLN[coarsefine],
       *CG_r[coarsefine],0,0,nsolve_bicgstab,0);
  apply(*CG_Av_search[coarsefine],
       *CG_p_search_SOLN[coarsefine],level,
       *CG_pbdryhom[coarsefine],bcpres_array);
  project_null_space((*CG_Av_search[coarsefine]),level);

  Ar_norm = LPnorm(*CG_Av_search[coarsefine],level);

  LP_dot(*CG_Av_search[coarsefine],*CG_r[coarsefine],level,rAr_norm); 
  if (rAr_norm<0.0) {
   rAr_norm=0.0;
  } else if (rAr_norm>=0.0) {
   rAr_norm=sqrt(rAr_norm);
  } else
   amrex::Error("rAr_norm invalid");

  if (Ar_norm>=0.0) {
   Ar_norm=sqrt(Ar_norm);
  } else {
   amrex::Error("Ar_norm invalid");
  }

  if (nit==0) {
   rnorm_init=rnorm;
   Ar_norm_init=Ar_norm;
   rAr_norm_init=rAr_norm;
  } else if (nit>0) {
   // do nothing
  } else
   amrex::Error("nit invalid");

  CG_error_history[nit][2*coarsefine]=rnorm;
  CG_error_history[nit][2*coarsefine+1]=eps_abs;

  CG_A_error_history[nit][2*coarsefine]=Ar_norm;
  CG_A_error_history[nit][2*coarsefine+1]=eps_abs;

  CG_rAr_error_history[nit][2*coarsefine]=rAr_norm;
  CG_rAr_error_history[nit][2*coarsefine+1]=eps_abs;

  CG_check_for_convergence(
   coarsefine,
   local_presmooth,local_postsmooth,
   rnorm,
   rnorm_init,
   Ar_norm,
   Ar_norm_init,
   rAr_norm,
   rAr_norm_init,
   eps_abs,relative_error,nit,
   error_close_to_zero,level);

  if ((error_close_to_zero==0)||  // tolerances not met.
      (error_close_to_zero==2)) { // normal tolerance met, nit<nit_min

    // "meets_tol" informs the main solver in NavierStokes3.cpp 
    // whether it needs to continue.
   if ((nit==0)&&(rnorm>eps_abs*10.0)) {
    meets_tol=0;
   } else if ((nit==0)&&(rnorm<=eps_abs*10.0)) {
    // do nothing
   } else if (nit>0) {
    // do nothing
   } else
    amrex::Error("nit invalid");

   if (local_use_bicgstab==0) { //MGPCG

    // z=K^{-1} r
    // z=project_null_space(z)
    pcg_solve(
     CG_z[coarsefine],
     CG_r[coarsefine],
     eps_abs,bot_atol,
     CG_pbdryhom[coarsefine],
     bcpres_array,
     usecg_at_bottom,
     smooth_type,bottom_smooth_type,
     local_presmooth,local_postsmooth,
     use_PCG, // 0=no preconditioning  1=depends:"CG_use_mg_precond_at_top"
     level,nit);

     // rho=z dot r
    LP_dot(*CG_z[coarsefine],*CG_r[coarsefine],level,rho); 

    if (rho>=0.0) {
     if (rho_old>restart_tol) {
      beta=rho/rho_old;
       // CG_p_search=0 initially or on restart.
       // p=z + beta p
      CG_advance( (*CG_p_search[coarsefine]),beta,(*CG_z[coarsefine]),
               (*CG_p_search[coarsefine]),level );
      project_null_space((*CG_p_search[coarsefine]),level);

       // Av_search=A*p
      MultiFab::Copy(*CG_p_search_SOLN[coarsefine],
       *CG_p_search[coarsefine],0,0,nsolve_bicgstab,0);

      apply(*CG_Av_search[coarsefine],
	    *CG_p_search_SOLN[coarsefine],level,
            *CG_pbdryhom[coarsefine],bcpres_array);
      project_null_space((*CG_Av_search[coarsefine]),level);

      Real pAp=0.0;
      LP_dot(*CG_p_search[coarsefine],*CG_Av_search[coarsefine],level,pAp);

      if (pAp>=0.0) {

       if (pAp>restart_tol) {
        alpha=rho/pAp;
         // x=x+alpha p
        LP_update( (*CG_delta_sol[coarsefine]), alpha, 
                   (*CG_delta_sol[coarsefine]),
        	   (*CG_p_search_SOLN[coarsefine]),level );
        project_null_space((*CG_delta_sol[coarsefine]),level);

        residual(
         (*CG_r[coarsefine]),
         (*CG_rhs_resid_cor_form[coarsefine]),
         (*CG_delta_sol[coarsefine]),
    	 level,
         *CG_pbdryhom[coarsefine],
	 bcpres_array); 
        project_null_space((*CG_r[coarsefine]),level);
       } else if ((pAp>=0.0)&&(pAp<=restart_tol)) {
        restart_flag=1;
       } else if (pAp<0.0) {
        restart_flag=1;
       } else {
        std::cout << "pAp= " << pAp << endl;
        amrex::Error("pAp invalid in bottom solver");
       }
      } else {
       std::cout << "pAp= " << pAp << endl;
       amrex::Error("pAp invalid in bottom solver");
      }
     } else if ((rho_old>=0.0)&&(rho_old<=restart_tol)) {
      restart_flag=1;
     } else if (rho_old<0.0) {
      restart_flag=1;
     } else
      amrex::Error("rho_old invalid");
    } else if (rho<0.0) {
     restart_flag=1;
    } else {
     std::cout << "rho= " << rho << '\n';
     amrex::Error("rho invalid mglib, cg");
    }

   } else if (local_use_bicgstab==1) { //MG-GMRES PCG

     // rho=r0 dot r
    LP_dot(*CG_rhs_resid_cor_form[coarsefine],*CG_r[coarsefine],level,rho); 

    if (rho>=0.0) {
     if ((rho_old>restart_tol)&&(omega>restart_tol)) {
      beta=rho*alpha/(rho_old*omega);
       // p=p - omega v
      CG_advance( (*CG_p_search[coarsefine]),-omega,
        (*CG_p_search[coarsefine]),
        (*CG_v_search[coarsefine]),level );
       // p=r + beta p
      CG_advance( (*CG_p_search[coarsefine]),beta,(*CG_r[coarsefine]),
               (*CG_p_search[coarsefine]),level );
      project_null_space((*CG_p_search[coarsefine]),level);
       // z=K^{-1} p
       // z=project_null_space(z)
      pcg_GMRES_solve(
       gmres_precond_iter,
       CG_z[coarsefine],
       CG_p_search[coarsefine],
       eps_abs,bot_atol,
       CG_pbdryhom[coarsefine],
       bcpres_array,
       usecg_at_bottom,
       smooth_type,bottom_smooth_type,
       local_presmooth,local_postsmooth,
       use_PCG,level,nit);
       // v_search=A*z 
      apply(*CG_v_search[coarsefine],
            *CG_z[coarsefine],level,
            *CG_pbdryhom[coarsefine],bcpres_array);
      project_null_space((*CG_v_search[coarsefine]),level);
     } else if ((rho_old<=restart_tol)||(omega<=restart_tol)) {
      restart_flag=1;
     } else
      amrex::Error("rho_old or omega invalid");
    } else if (rho<0.0) {
     restart_flag=1;
    } else
     amrex::Error("rho invalid mglib");

    if (restart_flag==0) {
     LP_dot(*CG_rhs_resid_cor_form[coarsefine],
            *CG_v_search[coarsefine],level,alpha);

     if (alpha>restart_tol) {
      alpha=rho/alpha;

       // x=x+alpha z
      LP_update( (*CG_delta_sol[coarsefine]), alpha, 
                 (*CG_delta_sol[coarsefine]),
		 (*CG_z[coarsefine]),level );
      project_null_space((*CG_delta_sol[coarsefine]),level);
      residual((*CG_r[coarsefine]),(*CG_rhs_resid_cor_form[coarsefine]),
        (*CG_delta_sol[coarsefine]),
	level,
        *CG_pbdryhom[coarsefine],
        bcpres_array); 
      project_null_space((*CG_r[coarsefine]),level);

      rnorm=LPnorm(*CG_r[coarsefine],level);
      if (rnorm>=0.0) {
       rnorm=sqrt(rnorm);
      } else {
       amrex::Error("rnorm invalid mglib");
      }

      MultiFab::Copy(*CG_p_search_SOLN[coarsefine],
       *CG_r[coarsefine],0,0,nsolve_bicgstab,0);
      apply(*CG_Av_search[coarsefine],
       *CG_p_search_SOLN[coarsefine],level,
       *CG_pbdryhom[coarsefine],bcpres_array);
      project_null_space((*CG_Av_search[coarsefine]),level);

      Ar_norm = LPnorm(*CG_Av_search[coarsefine],level);

      LP_dot(*CG_Av_search[coarsefine],*CG_r[coarsefine],level,rAr_norm); 
      if (rAr_norm<0.0) {
       rAr_norm=0.0;
      } else if (rAr_norm>=0.0) {
       rAr_norm=sqrt(rAr_norm);
      } else
       amrex::Error("rAr_norm invalid");

      CG_check_for_convergence(
        coarsefine,
        local_presmooth,local_postsmooth,
	rnorm,
	rnorm_init,
	Ar_norm,
	Ar_norm_init,
	rAr_norm,
	rAr_norm_init,
	eps_abs,relative_error,nit,
        error_close_to_zero,level);

      if ((error_close_to_zero==0)||  // tolerances not met.
          (error_close_to_zero==2)) { // normal tolerance met, nit<nit_min
       // z=K^{-1} r
       // z=project_null_space(z)
       pcg_GMRES_solve(
        gmres_precond_iter,
        CG_z[coarsefine],
	CG_r[coarsefine],
	eps_abs,bot_atol,
	CG_pbdryhom[coarsefine],
	bcpres_array,
        usecg_at_bottom,
	smooth_type,bottom_smooth_type,
        local_presmooth,local_postsmooth,
	use_PCG,
	level,nit);
       // Av_search=A*z
       apply(*CG_Av_search[coarsefine],
	     *CG_z[coarsefine],level,
             *CG_pbdryhom[coarsefine],bcpres_array);
       project_null_space((*CG_Av_search[coarsefine]),level);
       Real rAz=0.0;
       Real zAAz=0.0;
       // rAz=(Az) dot r =z^T A^T r = z^T A^T K z >=0 if A and K SPD.
       LP_dot(*CG_Av_search[coarsefine],*CG_r[coarsefine],level,rAz);
       if (rAz>=0.0) {

        LP_dot(*CG_Av_search[coarsefine],
               *CG_Av_search[coarsefine],level,zAAz);
	//Az dot Az >=0 if A SPD
        if (zAAz>restart_tol) {

         omega=rAz/zAAz;
	 if (omega>=0.0) {
	  // do nothing
	 } else
	  amrex::Error("omega invalid mglib");

         // x=x+omega z
         LP_update( (*CG_delta_sol[coarsefine]), omega, 
                    (*CG_delta_sol[coarsefine]),
		    (*CG_z[coarsefine]),level );
         project_null_space((*CG_delta_sol[coarsefine]),level);
         residual(
	  (*CG_r[coarsefine]),
	  (*CG_rhs_resid_cor_form[coarsefine]),
          (*CG_delta_sol[coarsefine]),
    	  level,
          *CG_pbdryhom[coarsefine],
	  bcpres_array); 
         project_null_space((*CG_r[coarsefine]),level);
        } else if ((zAAz>=0.0)&&(zAAz<=restart_tol)) {
         restart_flag=1;
	} else if (zAAz<0.0) {
         restart_flag=1;
        } else
 	 amrex::Error("zAAz invalid");

       } else if (rAz<0.0) {
        restart_flag=1;
       } else
        amrex::Error("rAz invalid");

      } else if (error_close_to_zero==1) {
       // do nothing
      } else
       amrex::Error("error_close_to_zero invalid");
     } else if (alpha<=restart_tol) {
      restart_flag=1;
     } else
      amrex::Error("alpha invalid");
    } else if (restart_flag==1) {
     // do nothing
    } else 
     amrex::Error("restart_flag invalid");

   } else 
    amrex::Error("local_use_bicgstab invalid");

   if (restart_flag==1) {

    if ((CG_verbose>0)||(nsverbose>0)) {
     if (ParallelDescriptor::IOProcessor()) {
      std::cout << "WARNING:RESTARTING: nit= " << nit << '\n';
      std::cout << "WARNING:RESTARTING: level= " << level << '\n';
      std::cout << "RESTARTING: local_use_bicgstab=" << 
	      local_use_bicgstab << '\n';
      std::cout << "RESTARTING: gmres_precond_iter= " << 
       gmres_precond_iter << '\n';
      std::cout << "RESTARTING: local_presmooth= " <<
        local_presmooth << '\n';
      std::cout << "RESTARTING: local_postsmooth= " <<
        local_postsmooth << '\n';

      std::cout << "RESTARTING: rnorm= " << 
       rnorm << '\n';
      std::cout << "RESTARTING: Ar_norm= " << 
       Ar_norm << '\n';
      std::cout << "RESTARTING: rAr_norm= " << 
       rAr_norm << '\n';
      std::cout << "RESTARTING: nsolve_bicgstab= " << 
        nsolve_bicgstab << '\n';
     }
    } else if ((CG_verbose==0)&&(nsverbose==0)) {
     // do nothing
    } else
     amrex::Error("CG_verbose or nsverbose invalid");

    if ((error_close_to_zero==0)||  // tolerances not met.
        (error_close_to_zero==2)) { // normal tolerance met, nit<nit_min
     beta=0.0;
     rho=1.0;
     rho_old=1.0;
     omega=1.0;
     alpha=1.0;
     CG_p_search[coarsefine]->setVal(0.0,0,nsolve_bicgstab,nghostRHS); 
     CG_p_search_SOLN[coarsefine]->setVal(0.0,0,nsolve_bicgstab,nghostSOLN); 
     CG_v_search[coarsefine]->setVal(0.0,0,nsolve_bicgstab,nghostRHS); 
     sol.ParallelAdd(*CG_delta_sol[coarsefine],0,0,nsolve_bicgstab,0,0);
     project_null_space(sol,level);
     CG_delta_sol[coarsefine]->setVal(0.0,0,nsolve_bicgstab,1);
     MultiFab::Copy(*CG_rhs_resid_cor_form[coarsefine],
       *CG_r[coarsefine],0,0,nsolve_bicgstab,0);
    } else if (error_close_to_zero==1) {
     amrex::Error("cannot have both restart_flag and error_close_to_zero==1");
    } else
     amrex::Error("error_close_to_zero invalid");

   } else if (restart_flag==0) {

    if ((error_close_to_zero==0)||  // tolerances not met.
        (error_close_to_zero==2)) { // normal tolerance met, nit<nit_min
     // do nothing
    } else if (error_close_to_zero==1) {
     // do nothing
    } else
     amrex::Error("error_close_to_zero invalid");

    sol.ParallelAdd(*CG_delta_sol[coarsefine],0,0,nsolve_bicgstab,0,0);
    project_null_space(sol,level);
    CG_delta_sol[coarsefine]->setVal(0.0,0,nsolve_bicgstab,1);
    MultiFab::Copy(*CG_rhs_resid_cor_form[coarsefine],
      *CG_r[coarsefine],0,0,nsolve_bicgstab,0);

   } else 
    amrex::Error("restart_flag invalid");

  } else if (error_close_to_zero==1) {
   // do nothing
  } else
   amrex::Error("error_close_to_zero invalid");

  if (restart_flag==1) {
   local_presmooth++;
   local_postsmooth++;
   if ((local_presmooth>1000)||(local_postsmooth>1000)) {
    std::cout << "local_presmooth overflow " <<
       local_presmooth << '\n';
    std::cout << "local_postsmooth overflow " <<
       local_postsmooth << '\n';
   } 

  } else if (restart_flag==0) {
   // do nothing
  } else
   amrex::Error("restart_flag invalid");

 }  // end of CGSolver loop

  // sol=sol+CG_delta_sol
  // src,src_comp,dest_comp,num_comp,src_nghost,dst_nghost
 sol.ParallelAdd(*CG_delta_sol[coarsefine],0,0,nsolve_bicgstab,0,0);
 project_null_space(sol,level);

 cg_cycles_out=nit;

 if ((CG_verbose>0)||(nsverbose>0)) {
  if (ParallelDescriptor::IOProcessor()) {
   if (CG_use_mg_precond_at_top==1) {
    if (is_bottom==0)
     std::cout << "mgpcg (mac) nit (NOBOT)" << nit << '\n';
    else
     std::cout << "mgpcg (mac) nit (BOT)" << nit << '\n';
   } else if (CG_use_mg_precond_at_top==0) {
    if (is_bottom==0) {
     std::cout << "pcg (mac) nit (NOBOT)" << nit << '\n';
    }
   } else
    amrex::Error("CG_use_mg_precond_at_top invalid");
  }
 }

 if ((error_close_to_zero==0)||  // tolerances not met.
     (error_close_to_zero==2)) { // normal tolerance met, nit<nit_min

  if (ParallelDescriptor::IOProcessor()) {
   std::cout << "Warning: ABecLaplacian:: failed to converge! \n";
   std::cout << "coarsefine= " << coarsefine << '\n';
   int dump_size=CG_error_history.size();
   if (nit+1<dump_size)
    dump_size=nit+1;
   for (int ehist=0;ehist<dump_size;ehist++) {
    std::cout << "nit " << ehist << " CG_error_history[nit][0,1] " <<
     CG_error_history[ehist][2*coarsefine+0] << ' ' <<
     CG_error_history[ehist][2*coarsefine+1] << '\n';
    std::cout << "nit " << ehist << " CG_A_error_history[nit][0,1] " <<
     CG_A_error_history[ehist][2*coarsefine+0] << ' ' <<
     CG_A_error_history[ehist][2*coarsefine+1] << '\n';
    std::cout << "nit " << ehist << " CG_rAr_error_history[nit][0,1] " <<
     CG_rAr_error_history[ehist][2*coarsefine+0] << ' ' <<
     CG_rAr_error_history[ehist][2*coarsefine+1] << '\n';
   }
  }

  CG_dump_params(
    rnorm,
    rnorm_init,
    Ar_norm,
    Ar_norm_init,
    rAr_norm,
    rAr_norm_init,
    eps_abs,relative_error,
    is_bottom,bot_atol,
    usecg_at_bottom,smooth_type,
    bottom_smooth_type,local_presmooth, 
    local_postsmooth,
    sol,
    rhs,
    level);
 } else if (error_close_to_zero==1) {
  // do nothing
 } else {
  amrex::Error("error_close_to_zero invalid");
 }

 if (ncomp!=nsolve_bicgstab)
  amrex::Error("ncomp invalid");

 if ((CG_verbose>0)||(nsverbose>0)) {
  residual((*CG_r[coarsefine]),rhs,sol,level,pbdry,bcpres_array);
  project_null_space(*CG_r[coarsefine],level);

  Real testnorm=LPnorm(*CG_r[coarsefine],level);
  if (testnorm>=0.0) {
   testnorm=sqrt(testnorm);
  } else {
   amrex::Error("testnorm invalid mglib");
  }

  MultiFab::Copy(*CG_p_search_SOLN[coarsefine],
       *CG_r[coarsefine],0,0,nsolve_bicgstab,0);
  apply(*CG_Av_search[coarsefine],
       *CG_p_search_SOLN[coarsefine],level,
       *CG_pbdryhom[coarsefine],bcpres_array);
  project_null_space((*CG_Av_search[coarsefine]),level);

  Real A_testnorm = LPnorm(*CG_Av_search[coarsefine],level);
  if (A_testnorm>=0.0) {
   A_testnorm=sqrt(A_testnorm);
  } else {
   amrex::Error("A_testnorm invalid mglib");
  }

  Real rAr_testnorm=0.0;
  LP_dot(*CG_Av_search[coarsefine],*CG_r[coarsefine],level,rAr_testnorm); 
  if (rAr_testnorm<0.0) {
   rAr_testnorm=0.0;
  } else if (rAr_testnorm>=0.0) {
   rAr_testnorm=sqrt(rAr_testnorm);
  } else
   amrex::Error("rAr_testnorm invalid");

  if (ParallelDescriptor::IOProcessor()) {
   if (is_bottom==1) {
    std::cout << "residual non-homogeneous bc (BOT) " << testnorm << '\n';  
    std::cout << "A_residual non-homogeneous bc (BOT) " << A_testnorm << '\n';  
    std::cout << "rAr_residual non-homogeneous bc (BOT) " << 
	    rAr_testnorm << '\n';  
   } else {
    std::cout << "residual non-homogeneous bc (NOBOT)"<< testnorm << '\n';  
    std::cout << "A_residual non-homogeneous bc (NOBOT)"<< A_testnorm << '\n';  
    std::cout << "rAr_residual non-homogeneous bc (NOBOT)"<< 
	    rAr_testnorm << '\n';  
   }
  }
 }

} // subroutine ABecLaplacian::CG_solve

// p=z+beta y
void ABecLaplacian::CG_advance (
       MultiFab& p,
       Real beta, 
       const MultiFab& z,
       MultiFab& y, 
       int level) {
    //
    // Compute p = z  +  beta y
    // only interior cells are updated.
    //
    const BoxArray& gbox_local = LPboxArray(level);
    int ncomp = p.nComp();
    if (ncomp!=nsolve_bicgstab)
     amrex::Error("p ncomp invalid");
    if (z.nComp()!=nsolve_bicgstab)
     amrex::Error("z ncomp invalid");
    if (y.nComp()!=nsolve_bicgstab)
     amrex::Error("y ncomp invalid");
    if ((p.nGrow()!=0)&&(p.nGrow()!=1))
     amrex::Error("p ngrow invalid");
    if ((z.nGrow()!=0)&&(z.nGrow()!=1))
     amrex::Error("z ngrow invalid");
    if ((y.nGrow()!=0)&&(y.nGrow()!=1))
     amrex::Error("y ngrow invalid");

    const BoxArray& zbox = z.boxArray();

    int bfact=get_bfact_array(level);
    int bfact_top=get_bfact_array(0);

    bool use_tiling=cfd_tiling;

    if (thread_class::nthreads<1)
     amrex::Error("thread_class::nthreads invalid");
    thread_class::init_d_numPts(p.boxArray().d_numPts());

#ifdef _OPENMP
#pragma omp parallel
#endif
{
    for (MFIter mfi(p,use_tiling); mfi.isValid(); ++mfi) {
     BL_ASSERT(zbox[mfi.index()] == gbox_local[mfi.index()]);
     int gridno=mfi.index();
     const Box& tilegrid=mfi.tilebox();
     const Box& fabgrid=gbox_local[gridno];
     const int* tilelo=tilegrid.loVect();
     const int* tilehi=tilegrid.hiVect();
     const int* fablo=fabgrid.loVect();
     const int* fabhi=fabgrid.hiVect();

     int tid_current=0;
#ifdef _OPENMP
     tid_current = omp_get_thread_num();
#endif
     if ((tid_current>=0)&&(tid_current<thread_class::nthreads)) {
      // do nothing
     } else
      amrex::Error("tid_current invalid");

     thread_class::tile_d_numPts[tid_current]+=tilegrid.d_numPts();

     for (int veldir=0;veldir<nsolve_bicgstab;veldir++) {

#if (profile_solver==1)
      std::string subname="CGADVCP";
      std::stringstream popt_string_stream(std::stringstream::in |
        std::stringstream::out);
      popt_string_stream << cfd_project_option;
      std::string profname=subname+popt_string_stream.str();
      profname=profname+"_";
      std::stringstream lev_string_stream(std::stringstream::in |
        std::stringstream::out);
      lev_string_stream << level;
      profname=profname+lev_string_stream.str();
      profname=profname+"_";
      std::stringstream veldir_string_stream(std::stringstream::in |
        std::stringstream::out);
      veldir_string_stream << veldir;
      profname=profname+veldir_string_stream.str();

      BLProfiler bprof(profname);
#endif

      FORT_CGADVCP(
       p[mfi].dataPtr(veldir),
       ARLIM(p[mfi].loVect()), ARLIM(p[mfi].hiVect()),
       z[mfi].dataPtr(veldir),
       ARLIM(z[mfi].loVect()), ARLIM(z[mfi].hiVect()),
       y[mfi].dataPtr(veldir),
       ARLIM(y[mfi].loVect()), ARLIM(y[mfi].hiVect()),
       &beta,
       tilelo,tilehi,
       fablo,fabhi,&bfact,&bfact_top);

#if (profile_solver==1)
      bprof.stop();
#endif
     } // veldir
    } // mfi
} // omp

    thread_class::sync_tile_d_numPts();
    ParallelDescriptor::ReduceRealSum(thread_class::tile_d_numPts[0]);
    thread_class::reconcile_d_numPts(10);

} // end subroutine advance

Real
ABecLaplacian::MG_errorEstimate(int level,
  MultiFab& pbdry,Vector<int> bcpres_array) {
  
 residual(*(MG_res[level]),*(MG_rhs[level]),*(MG_cor[level]), 
     level,pbdry,bcpres_array);
 MultiFab& resid = *(MG_res[level]);
 int ncomp=resid.nComp();
 if (ncomp!=nsolve_bicgstab)
  amrex::Error("ncomp invalid");

 Real local_error=sqrt(LPnorm(resid,level));
     
 return local_error;
}  // end subroutine MG_errorEstimate

void ABecLaplacian::MG_residualCorrectionForm (MultiFab& newrhs,
      MultiFab& oldrhs,MultiFab& solnL,
      MultiFab& inisol,MultiFab& pbdry,Vector<int> bcpres_array,
      int level) {

#if (profile_solver==1)
 std::string subname="ABecLaplacian::MG_residualCorrectionForm";
 std::stringstream popt_string_stream(std::stringstream::in |
   std::stringstream::out);
 popt_string_stream << cfd_project_option;
 std::string profname=subname+popt_string_stream.str();
 profname=profname+"_";
 std::stringstream lev_string_stream(std::stringstream::in |
   std::stringstream::out);
 lev_string_stream << level;
 profname=profname+lev_string_stream.str();

 BLProfiler bprof(profname);
#endif

 if (solnL.nComp()!=nsolve_bicgstab)
  amrex::Error("ncomp invalid");
 if (solnL.nGrow()!=1)
  amrex::Error("solution should have ngrow=1");

 MG_initialsolution->ParallelCopy(inisol);
 solnL.ParallelCopy(inisol);

#if (profile_solver==1)
 bprof.stop();
#endif

 residual(newrhs, oldrhs, solnL, level, pbdry,bcpres_array);
 solnL.setVal(0.0,0,nsolve_bicgstab,1);
}

void
ABecLaplacian::MG_solve (int nsverbose,
  MultiFab& _sol, MultiFab& _rhs,
  Real _eps_abs,Real _atol_b,
  int usecg_at_bottom,MultiFab& pbdry,
  Vector<int> bcpres_array,
  int smooth_type,
  int bottom_smooth_type,int presmooth,
  int postsmooth) {

#if (profile_solver==1)
 std::string subname="ABecLaplacian::MG_solve0_";
 std::stringstream popt_string_stream(std::stringstream::in |
      std::stringstream::out);
 popt_string_stream << cfd_project_option;
 std::string profname=subname+popt_string_stream.str();

 BLProfiler bprof(profname);
#endif

 int level = 0;

#if (profile_solver==1)
 bprof.stop();
#endif

 project_null_space(_rhs,level);
 MG_residualCorrectionForm(*MG_rhs[level],_rhs,*MG_cor[level],
     _sol,pbdry,bcpres_array,level);
 project_null_space(*MG_rhs[level],level);
 MG_pbdryhom->setVal(0.0,0,nsolve_bicgstab,nghostSOLN);

 MG_solve_(nsverbose,_sol, 
   _eps_abs, _atol_b, 
   *MG_pbdryhom,bcpres_array,
   usecg_at_bottom,
   smooth_type,
   bottom_smooth_type,presmooth,postsmooth);

} // subroutine MG_solve

// pbdry will always be identically zero since residual correction form.
void
ABecLaplacian::MG_solve_ (int nsverbose,MultiFab& _sol,
  Real eps_abs,Real atol_b,MultiFab& pbdry,Vector<int> bcpres_array,
  int usecg_at_bottom,
  int smooth_type,int bottom_smooth_type,int presmooth,int postsmooth) {

 int  level=0;

#if (profile_solver==1)
 std::string subname="ABecLaplacian::MG_solve_";
 std::stringstream popt_string_stream(std::stringstream::in |
   std::stringstream::out);
 popt_string_stream << cfd_project_option;
 std::string profname=subname+popt_string_stream.str();
 profname=profname+"_";
 std::stringstream lev_string_stream(std::stringstream::in |
   std::stringstream::out);
 lev_string_stream << level;
 profname=profname+lev_string_stream.str();

 BLProfiler bprof(profname);
#endif

  //
  // Relax system maxiter times, stop if 
  // absolute err <= _abs_eps
  //
 int ncomp=_sol.nComp();
 if (ncomp!=nsolve_bicgstab)
  amrex::Error("ncomp invalid");

 const Real error0 = MG_errorEstimate(level,pbdry,bcpres_array);
 Real error = error0;
 if ((ParallelDescriptor::IOProcessor()) && 
     ((MG_verbose)||(nsverbose>0))) {
   Spacer(std::cout, level);
   std::cout << "MultiGrid: Initial error (error0) = " << error0 << '\n';
 }

  //
  // Initialize correction to zero at this level (auto-filled at levels below)
  //
 (*MG_cor[level]).setVal(0.0);

#if (profile_solver==1)
 bprof.stop();
#endif

 
 MG_relax(*MG_cor[level],*MG_rhs[level],level,eps_abs,
   atol_b,usecg_at_bottom,pbdry,bcpres_array,
   smooth_type,bottom_smooth_type,
   presmooth,postsmooth);
 project_null_space(*MG_cor[level],level);

 error = MG_errorEstimate(level,pbdry,bcpres_array);

 if (ParallelDescriptor::IOProcessor()) {
  if (MG_verbose > 1 ) {
   Spacer(std::cout, level);
   std::cout << "MultiGrid: error/error0 "
             << error/error0 
             << " error " 
             << error << '\n';
  }
 }

 _sol.ParallelCopy(*MG_cor[level]);
   // src,src_comp,dest_comp,num_comp,src_nghost,dst_nghost
 _sol.ParallelAdd(*MG_initialsolution,0,0,_sol.nComp(),0,0);
 project_null_space(_sol,level);

}  // subroutine MG_solve_

int
ABecLaplacian::MG_numLevels (const BoxArray& grids) const
{
 int MG_numLevelsMAX=1024;

 int ng = grids.size();
 int lv = MG_numLevelsMAX;

 for (int i = 0; i < ng; ++i) {
  int llv = 0;
  Box tmp = grids[i];
  for (;;) {
   tmp.coarsen(2);
   int block_check=1;
   for (int dir=0;dir<BL_SPACEDIM;dir++) {
    int len=tmp.length(dir);
    if ((len/mglib_blocking_factor)*mglib_blocking_factor!=len)
     block_check=0;
   } // dir=0..sdim-1
   if (block_check==0)
    break;
   llv++;
  }
  //
  // Set number of levels so that every box can be refined to there.
  //
  if (lv >= llv)
      lv = llv;
 } // i=0..ng-1

 return lv+1; // Including coarsest.

} // end subroutine MG_numLevels

void
ABecLaplacian::MG_coarsestSmooth(MultiFab& solL,MultiFab& rhsL,
   int level,
   Real eps_abs,
   Real atol_b,
   int usecg_at_bottom,
   MultiFab& pbdry,Vector<int> bcpres_array,
   int smooth_type,int bottom_smooth_type,
   int presmooth,int postsmooth)
{


 int ncomp=solL.nComp();
 if (ncomp!=nsolve_bicgstab)
  amrex::Error("ncomp invalid");

 int is_bottom=1;

 if (usecg_at_bottom==0) {
  Real error0;
  if (MG_verbose) {
   error0 = MG_errorEstimate(level,pbdry,bcpres_array);
   if (ParallelDescriptor::IOProcessor())
    std::cout << "   Bottom Smoother: Initial error (error0) = " 
       << error0 << '\n';
  }

  project_null_space(rhsL,level);

  for (int i = MG_nu_f; i > 0; i--) {
   smooth(solL,rhsL,level,pbdry,bcpres_array,smooth_type); 
   project_null_space(solL,level);

   if (MG_verbose > 1 || (i == 1 && MG_verbose)) {
    Real error = MG_errorEstimate(level,pbdry,bcpres_array);
    if (ParallelDescriptor::IOProcessor())
     std::cout << "   Bottom Smoother: Iteration " << i
       << " error/error0 " << error/error0 << " error " 
       << error << '\n';
   }
  } // i=MG_nu_f downto 1
 } else {
  int local_meets_tol=0;
  Real local_error0=0.0;
  Real local_A_error0=0.0;
  Real local_rAr_error0=0.0;
  int nsverbose=0;
  int cg_cycles_parm=0;

  CG_solve(
    cg_cycles_parm,
    nsverbose,is_bottom,
    solL,rhsL, 
    atol_b, 
    atol_b,
    pbdry,bcpres_array,usecg_at_bottom,
    local_meets_tol,
    bottom_smooth_type,bottom_smooth_type,
    presmooth,postsmooth,
    local_error0,
    local_A_error0,
    local_rAr_error0,
    level);
 }
}


void
ABecLaplacian::MG_relax (MultiFab& solL,MultiFab& rhsL,
   int level,Real eps_abs,
   Real atol_b,int usecg_at_bottom,
   MultiFab& pbdry,Vector<int> bcpres_array,
   int smooth_type,int bottom_smooth_type,int presmooth,
   int postsmooth) {

#if (profile_solver==1)
 std::string subname="ABecLaplacian::MG_relax";
 std::stringstream popt_string_stream(std::stringstream::in |
   std::stringstream::out);
 popt_string_stream << cfd_project_option;
 std::string profname=subname+popt_string_stream.str();
 profname=profname+"_";
 std::stringstream lev_string_stream(std::stringstream::in |
   std::stringstream::out);
 lev_string_stream << level;
 profname=profname+lev_string_stream.str();

 BLProfiler bprof(profname);
#endif

 int ncomp=solL.nComp();
 if (ncomp!=nsolve_bicgstab)
  amrex::Error("ncomp invalid");

#if (profile_solver==1)
 bprof.stop();
#endif

 if (level < MG_numlevels_var - 1 ) {
 
  if (presmooth!=postsmooth)
   amrex::Error("presmooth must equal postsmooth for mgpcg");

  project_null_space(rhsL,level);

  for (int i = presmooth ; i > 0 ; i--) {
   smooth(solL,rhsL,level,pbdry,bcpres_array,smooth_type);
   project_null_space(solL,level);
  }
  residual(*MG_res[level],rhsL,solL,level,pbdry,bcpres_array);
  project_null_space(*MG_res[level],level);
  MG_average(*MG_rhs[level+1], *MG_res[level],level+1,level);
  MG_cor[level+1]->setVal(0.0);

  if (!((usecg_at_bottom==0)||(usecg_at_bottom==1)))
   amrex::Error("usecg_at_bottom invalid");

  MG_pbdrycoarser[level+1]->setVal(0.0,0,nsolve_bicgstab,nghostSOLN); 
  for (int i = MG_def_nu_0; i > 0 ; i--) {
   MG_relax(*(MG_cor[level+1]),*(MG_rhs[level+1]),level+1,
    eps_abs,atol_b,usecg_at_bottom,
    *(MG_pbdrycoarser[level+1]),bcpres_array,
    smooth_type,bottom_smooth_type,presmooth,postsmooth);
  }

  MG_interpolate(solL, *(MG_cor[level+1]),level+1,level);
  for (int i = postsmooth; i > 0 ; i--) {
   smooth(solL, rhsL, level,pbdry,bcpres_array,smooth_type);
  }
 } else {
  MG_coarsestSmooth(solL,rhsL,level,eps_abs,atol_b,
   usecg_at_bottom,pbdry,bcpres_array,
   smooth_type,bottom_smooth_type,
   presmooth,postsmooth);
 }
} // subroutine MG_relax

void
ABecLaplacian::MG_average (MultiFab& c,MultiFab& f,
  int clevel,int flevel)
{

#if (profile_solver==1)
 std::string subname="ABecLaplacian::MG_average";
 std::stringstream popt_string_stream(std::stringstream::in |
   std::stringstream::out);
 popt_string_stream << cfd_project_option;
 std::string profname=subname+popt_string_stream.str();
 profname=profname+"_";
 std::stringstream lev_string_stream(std::stringstream::in |
   std::stringstream::out);
 lev_string_stream << clevel;
 profname=profname+lev_string_stream.str();

 BLProfiler bprof(profname);
#endif

 if (clevel!=flevel+1)
  amrex::Error("clevel invalid");
 if (flevel<0)
  amrex::Error("flevel invalid"); 

 int bfact_coarse=get_bfact_array(clevel);
 int bfact_fine=get_bfact_array(flevel);
 int bfact_top=get_bfact_array(0);

 if (thread_class::nthreads<1)
  amrex::Error("thread_class::nthreads invalid");
 thread_class::init_d_numPts(c.boxArray().d_numPts());

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(c,false); mfi.isValid(); ++mfi) {

  BL_ASSERT(c.boxArray().get(mfi.index()) == mfi.validbox());

  const Box& tilegrid=mfi.tilebox();
  const Box& bx = mfi.validbox();

  int nc = c.nComp();
  if (nc!=nsolve_bicgstab) {
   std::cout << "nc,nsolve_bicgstab = " << nc << ' ' << 
    nsolve_bicgstab << '\n';
   amrex::Error("nc invalid in average");
  }

  int tid_current=0;
#ifdef _OPENMP
  tid_current = omp_get_thread_num();
#endif
  if ((tid_current>=0)&&(tid_current<thread_class::nthreads)) {
   // do nothing
  } else
   amrex::Error("tid_current invalid");

  thread_class::tile_d_numPts[tid_current]+=tilegrid.d_numPts();

   // divide by 4 in 2D and 8 in 3D
  int iaverage=1;
  for (int veldir=0;veldir<nsolve_bicgstab;veldir++) {
   FORT_AVERAGE(
    c[mfi].dataPtr(veldir),
    ARLIM(c[mfi].loVect()), ARLIM(c[mfi].hiVect()),
    f[mfi].dataPtr(veldir),
    ARLIM(f[mfi].loVect()), ARLIM(f[mfi].hiVect()),
    bx.loVect(), bx.hiVect(),&iaverage,
    &bfact_coarse,&bfact_fine,&bfact_top);
  }  // veldir
 } // mfi
} // omp

 thread_class::sync_tile_d_numPts();
 ParallelDescriptor::ReduceRealSum(thread_class::tile_d_numPts[0]);
 thread_class::reconcile_d_numPts(11);

#if (profile_solver==1)
 bprof.stop();
#endif
}  // end subroutine average

void
ABecLaplacian::MG_interpolate (MultiFab& f,MultiFab& c,
  int clevel,int flevel)
{

#if (profile_solver==1)
 std::string subname="ABecLaplacian::MG_interpolate";
 std::stringstream popt_string_stream(std::stringstream::in |
   std::stringstream::out);
 popt_string_stream << cfd_project_option;
 std::string profname=subname+popt_string_stream.str();
 profname=profname+"_";
 std::stringstream lev_string_stream(std::stringstream::in |
   std::stringstream::out);
 lev_string_stream << clevel;
 profname=profname+lev_string_stream.str();

 BLProfiler bprof(profname);
#endif

 if (clevel!=flevel+1)
  amrex::Error("clevel invalid");
 if (flevel<0)
  amrex::Error("flevel invalid"); 

 int bfact_coarse=get_bfact_array(clevel);
 int bfact_fine=get_bfact_array(flevel);
 int bfact_top=get_bfact_array(0);

 if (thread_class::nthreads<1)
  amrex::Error("thread_class::nthreads invalid");
 thread_class::init_d_numPts(f.boxArray().d_numPts());

 //
 // Use fortran function to interpolate up (prolong) c to f
 // Note: returns f=f+P(c) , i.e. ADDS interp'd c to f.
 //
#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(f,false); mfi.isValid(); ++mfi) {

  BL_ASSERT(f.boxArray().get(mfi.index()) == mfi.validbox());

  const Box& tilegrid=mfi.tilebox();
  const Box& bx = c.boxArray()[mfi.index()];
  int nc = f.nComp();
  if (nc!=nsolve_bicgstab) {
   std::cout << "nc,nsolve_bicgstab = " << nc << ' ' << 
    nsolve_bicgstab << '\n';
   amrex::Error("nc invalid in interpolate");
  }

  int tid_current=0;
#ifdef _OPENMP
  tid_current = omp_get_thread_num();
#endif
  if ((tid_current>=0)&&(tid_current<thread_class::nthreads)) {
   // do nothing
  } else
   amrex::Error("tid_current invalid");

  thread_class::tile_d_numPts[tid_current]+=tilegrid.d_numPts();

  for (int veldir=0;veldir<nsolve_bicgstab;veldir++) {
    // in: MG_3D.F90
   FORT_INTERP(
     &bfact_coarse,&bfact_fine,&bfact_top,
     f[mfi].dataPtr(veldir),
     ARLIM(f[mfi].loVect()), ARLIM(f[mfi].hiVect()),
     c[mfi].dataPtr(veldir),
     ARLIM(c[mfi].loVect()), ARLIM(c[mfi].hiVect()),
     bx.loVect(), bx.hiVect());
  } // veldir

 } // mfi
} // omp

 thread_class::sync_tile_d_numPts();
 ParallelDescriptor::ReduceRealSum(thread_class::tile_d_numPts[0]);
 thread_class::reconcile_d_numPts(12);

#if (profile_solver==1)
 bprof.stop();
#endif
}  // end subroutine interpolate


#undef profile_solver

}/* namespace amrex */

