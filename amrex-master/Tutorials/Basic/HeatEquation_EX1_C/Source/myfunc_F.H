#ifndef MYFUNC_F_H_
#define MYFUNC_F_H_

#include <AMReX_BLFort.H>

#ifdef __cplusplus
extern "C"
{
#endif

    void init_phi(const int* lo, const int* hi,
                  amrex_real* data, const int* dlo, const int* dhi,
                  const amrex_real* dx, 
                  const amrex_real* prob_lo, const amrex_real* prob_hi,
                  int* probtype);

    void compute_dt(const int* lo, const int* hi,
                  amrex_real* data, const int* dlo, const int* dhi,
		  amrex_real* a_vector,int* flux_type,amrex_real* dt,
                  const amrex_real* dx, const amrex_real* prob_lo, 
                  const amrex_real* prob_hi);
    



    void compute_flux(const int* lo, const int* hi,
                      const int* domlo, const int* domhi,
                      amrex_real* phi, const int* philo, const int* phihi,
                      amrex_real* fluxx, const int* fxlo, const int* fxhi,
                      amrex_real* fluxy, const int* fylo, const int* fyhi,
#if (BL_SPACEDIM == 3)   
                      amrex_real* fluxz, const int* fzlo, const int* fzhi,
#endif
                      const amrex_real* dx,
                      int* bc_vector,amrex_real* bc_value,
                      int* dirichlet_condition,
                      int* neumann_condition,
                      int* periodic_condition,
                      amrex_real* a_vector,
                      int* flux_type,
                      const amrex_real* problo,
                      amrex_real* time_n,amrex_real* time_np1,
                      int* probtype);

    void update_phi(const int* lo, const int* hi,
                    amrex_real* pold, const int* polo, const int* pohi,
                    amrex_real* pnew, const int* pnlo, const int* pnhi,
                    amrex_real* fluxx, const int* fxlo, const int* fxhi,
                    amrex_real* fluxy, const int* fylo, const int* fyhi,
#if (BL_SPACEDIM == 3)   
                    amrex_real* fluxz, const int* fzlo, const int* fzhi,
#endif
                    const amrex_real* dx, const amrex_real* dt,
                    const amrex_real* problo,amrex_real* time_n,
                    amrex_real* time_np1,int* probtype);

#ifdef __cplusplus
}
#endif

#endif
