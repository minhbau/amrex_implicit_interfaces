
#include "myfunc.H"
#include "myfunc_F.H"

using namespace amrex;

void advance (MultiFab& phi_old,
              MultiFab& phi_new,
	      Array<MultiFab, AMREX_SPACEDIM>& flux,
	      amrex::Real dt,
              const Geometry& geom,
              Vector<Real> a_vector,
              int flux_type,
              Vector<int> bc_vector,
              Vector<Real> bc_value,
              int dirichlet_condition,
              int neumann_condition,
              int periodic_condition,
              amrex::Real time_n,
              int probtype)
{  
    // Fill the ghost cells of each grid from the other grids
    // includes periodic domain boundaries
    phi_old.FillBoundary(geom.periodicity());

    int Ncomp = phi_old.nComp();
    int ng_p = phi_old.nGrow();
    int ng_f = flux[0].nGrow();

    const amrex::Real* dx = geom.CellSize();
    amrex::Real time_np1=time_n+dt;

    //
    // Note that this simple example is not optimized.
    // The following two MFIter loops could be merged
    // and we do not have to use flux MultiFab.
    // 

    const Box& domain_bx = geom.Domain();

    // Compute fluxes one grid at a time
    for ( MFIter mfi(phi_old); mfi.isValid(); ++mfi )
    {
        const Box& bx = mfi.validbox();

        compute_flux(BL_TO_FORTRAN_BOX(bx),
                     BL_TO_FORTRAN_BOX(domain_bx),
                     BL_TO_FORTRAN_ANYD(phi_old[mfi]),
                     BL_TO_FORTRAN_ANYD(flux[0][mfi]),
                     BL_TO_FORTRAN_ANYD(flux[1][mfi]),
#if (AMREX_SPACEDIM == 3)   
                     BL_TO_FORTRAN_ANYD(flux[2][mfi]),
#endif
                     dx,
                     bc_vector.dataPtr(),
                     bc_value.dataPtr(),
                     &dirichlet_condition,
                     &neumann_condition,
                     &periodic_condition,
                     a_vector.dataPtr(),
                     &flux_type,
                     geom.ProbLo(),
                     &time_n,
                     &time_np1,
                     &probtype);
    }
    
    // Advance the solution one grid at a time
    for ( MFIter mfi(phi_old); mfi.isValid(); ++mfi )
    {
        const Box& bx = mfi.validbox();
        
        update_phi(BL_TO_FORTRAN_BOX(bx),
                   BL_TO_FORTRAN_ANYD(phi_old[mfi]),
                   BL_TO_FORTRAN_ANYD(phi_new[mfi]),
                   BL_TO_FORTRAN_ANYD(flux[0][mfi]),
                   BL_TO_FORTRAN_ANYD(flux[1][mfi]),
#if (AMREX_SPACEDIM == 3)   
                   BL_TO_FORTRAN_ANYD(flux[2][mfi]),
#endif
                   dx, &dt,
                   geom.ProbLo(),&time_n,&time_np1,
                   &probtype);
    }
}
