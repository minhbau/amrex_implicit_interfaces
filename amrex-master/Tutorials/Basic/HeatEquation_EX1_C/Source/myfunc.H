#ifndef MYFUNC_H_
#define MYFUNC_H_

#include <AMReX_Geometry.H>
#include <AMReX_MultiFab.H>
#include <AMReX_Array.H>

void main_main ();

// Fab = Fortran Array Box (two dimensional array of Reals)
// MultiFab = an array of Fabs.
void advance (amrex::MultiFab& phi_old,
              amrex::MultiFab& phi_new,
	      amrex::Array<amrex::MultiFab, AMREX_SPACEDIM>& flux,
	      amrex::Real dt,
              const amrex::Geometry& geom,
              amrex::Vector<amrex::Real> a_vector,
              int flux_type,
              amrex::Vector<int> bc_vector,
              amrex::Vector<amrex::Real> bc_value,
              int dirichlet_condition,
              int neumann_condition,
              int periodic_condition,
              amrex::Real time,
              int probtype);

#endif
