#undef  BL_LANG_CC
#ifndef BL_LANG_FORT
#define BL_LANG_FORT
#endif

#include "AMReX_REAL.H"
#include "AMReX_CONSTANTS.H"
#include "AMReX_BC_TYPES.H"
#include "AMReX_ArrayLim.H"

#define SDIM 1

! ::: -----------------------------------------------------------
! ::: This routine is intended to be a generic fill function
! ::: for cell-centered data.  It knows how to extrapolate
! ::: and reflect data and is used to supplement the problem-specific
! ::: fill functions which call it.
! ::: 
! ::: INPUTS/OUTPUTS:
! ::: q           <=  array to fill
! ::: lo,hi        => index extent of q array
! ::: domlo,domhi  => index extent of problem domain
! ::: bc	   => array of boundary flags bc(SPACEDIM,lo:hi)
! ::: 
! ::: NOTE: all corner as well as edge data is filled if not EXT_DIR
! ::: -----------------------------------------------------------

      subroutine filcc( &
       q,DIMS(q), &
       domlo,domhi,bc)

      INTEGER_T    DIMDEC(q)
      INTEGER_T    domlo(SDIM), domhi(SDIM)
      INTEGER_T    bc(SDIM,2)
      REAL_T     q(DIMV(q))

      INTEGER_T    nlft, nrgt
      INTEGER_T    ilo, ihi
      INTEGER_T    i
      INTEGER_T    is, ie

      nlft = max(0,domlo(1)-ARG_L1(q))
      nrgt = max(0,ARG_H1(q)-domhi(1))

      is = max(ARG_L1(q),domlo(1))
      ie = min(ARG_H1(q),domhi(1))

!     ::::: first fill sides
      if (nlft .gt. 0) then
         ilo = domlo(1)

	 if (bc(1,1) .eq. FOEXTRAP) then
	    do i = 1, nlft
	       q(ilo-i) = q(ilo)
	    end do
	 else if (bc(1,1) .eq. HOEXTRAP) then
	    do i = 2, nlft
	       q(ilo-i) = q(ilo) 
	    end do 
            if (ilo+2 .le. ie) then 
               q(ilo-1) = (fifteen*q(ilo) - ten*q(ilo+1) +  &
                              three*q(ilo+2)) * eighth
            else 
	       q(ilo-1) = half*(three*q(ilo) - q(ilo+1))
            end if
	 else if (bc(1,1) .eq. REFLECT_EVEN) then
	    do i = 1, nlft
	       q(ilo-i) = q(ilo+i-1)
	    end do
	 else if (bc(1,1) .eq. REFLECT_ODD) then
	    do i = 1, nlft
	       q(ilo-i) = -q(ilo+i-1)
	    end do
	 end if
      end if

      if (nrgt .gt. 0) then
         ihi = domhi(1)

	 if (bc(1,2) .eq. FOEXTRAP) then
	    do i = 1, nrgt
	       q(ihi+i) = q(ihi)
	    end do
         else if (bc(1,2) .eq. HOEXTRAP) then
            do i = 2, nrgt
               q(ihi+i) = q(ihi)
            end do
            if (ihi-2 .ge. is) then
	       q(ihi+1) = (fifteen*q(ihi) - ten*q(ihi-1) +  &
                              three*q(ihi-2)) * eighth
            else
	       q(ihi+1) = half*(three*q(ihi) - q(ihi-1))
            end if
	 else if (bc(1,2) .eq. REFLECT_EVEN) then
	    do i = 1, nrgt
	       q(ihi+i) = q(ihi-i+1)
	    end do
	 else if (bc(1,2) .eq. REFLECT_ODD) then
	    do i = 1, nrgt
	       q(ihi+i) = -q(ihi-i+1)
	    end do
	 end if
      end if

      return
      end
