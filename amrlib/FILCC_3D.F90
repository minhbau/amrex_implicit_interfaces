#undef  BL_LANG_CC
#ifndef BL_LANG_FORT
#define BL_LANG_FORT
#endif

#include "AMReX_REAL.H"
#include "AMReX_CONSTANTS.H"
#include "AMReX_SPACE.H"
#include "AMReX_BC_TYPES.H"
#include "AMReX_ArrayLim.H"

#define SDIM 3


      subroutine filcc(bfact, &
       q,DIMS(q), &
       domlo,domhi,bc)

      INTEGER_T    DIMDEC(q)
      INTEGER_T    domlo(SDIM), domhi(SDIM)
      REAL_T     q(DIMV(q))
      INTEGER_T    bc(SDIM,2)

      INTEGER_T bfact

      INTEGER_T    nlft, nrgt, nbot, ntop, nup, ndwn
      INTEGER_T    ilo, ihi, jlo, jhi, klo, khi
      INTEGER_T    is,  ie,  js,  je,  ks,  ke
      INTEGER_T    i, j, k

      if (bfact.lt.1) then
       print *,"bfact invalid710"
       stop
      endif

      is = max(ARG_L1(q),domlo(1))
      ie = min(ARG_H1(q),domhi(1))
      js = max(ARG_L2(q),domlo(2))
      je = min(ARG_H2(q),domhi(2))
      ks = max(ARG_L3(q),domlo(3))
      ke = min(ARG_H3(q),domhi(3))

      nlft = max(0,domlo(1)-ARG_L1(q))
      nrgt = max(0,ARG_H1(q)-domhi(1))
      nbot = max(0,domlo(2)-ARG_L2(q))
      ntop = max(0,ARG_H2(q)-domhi(2))
      ndwn = max(0,domlo(3)-ARG_L3(q))
      nup  = max(0,ARG_H3(q)-domhi(3))
!
!     ::::: first fill sides
!
      if (nlft .gt. 0) then
         ilo = domlo(1)

         if ((bc(1,1).eq.FOEXTRAP).or. &
             (bc(1,1).eq.EXT_DIR)) then
	    do i = 1, nlft
               do k = ARG_L3(q),ARG_H3(q)
                  do j = ARG_L2(q),ARG_H2(q)
                     q(ilo-i,j,k) = q(ilo,j,k)
                  end do
               end do
	    end do
	 else if (bc(1,1) .eq. HOEXTRAP) then
	    do i = 1, nlft
               do k = ARG_L3(q),ARG_H3(q)
                  do j = ARG_L2(q),ARG_H2(q)
                     q(ilo-i,j,k) = two*q(ilo-i+1,j,k)-q(ilo-i+2,j,k)
                  end do
               end do
	    end do
	 else if (bc(1,1) .eq. REFLECT_EVEN) then
	    do i = 1, nlft
               do k = ARG_L3(q),ARG_H3(q)
                  do j = ARG_L2(q),ARG_H2(q)
                     q(ilo-i,j,k) = q(ilo+i-1,j,k)
                  end do
               end do
	    end do
	 else if (bc(1,1) .eq. REFLECT_ODD) then
	    do i = 1, nlft
               do k = ARG_L3(q),ARG_H3(q)
                  do j = ARG_L2(q),ARG_H2(q)
                     q(ilo-i,j,k) = -q(ilo+i-1,j,k)
                  end do
               end do
	    end do
         else if (bc(1,1).eq.INT_DIR) then
          ! do nothing
         else
          print *,"bc invalid"
          stop
	 end if
      end if

      if (nrgt .gt. 0) then
         ihi = domhi(1)

         if ((bc(1,2).eq.FOEXTRAP).or. &
             (bc(1,2).eq.EXT_DIR)) then
	    do i = 1, nrgt
               do k = ARG_L3(q),ARG_H3(q)
                  do j = ARG_L2(q),ARG_H2(q)
                     q(ihi+i,j,k) = q(ihi,j,k)
                  end do
               end do
	    end do
         else if (bc(1,2) .eq. HOEXTRAP) then
            do i = 1, nrgt
               do k = ARG_L3(q),ARG_H3(q)
                  do j = ARG_L2(q),ARG_H2(q)
                     q(ihi+i,j,k) = two*q(ihi+i-1,j,k)-q(ihi+i-2,j,k)
                  end do
               end do
            end do
	 else if (bc(1,2) .eq. REFLECT_EVEN) then
	    do i = 1, nrgt
               do k = ARG_L3(q),ARG_H3(q)
                  do j = ARG_L2(q),ARG_H2(q)
                     q(ihi+i,j,k) = q(ihi-i+1,j,k)
                  end do
               end do
	    end do
	 else if (bc(1,2) .eq. REFLECT_ODD) then
	    do i = 1, nrgt
               do k = ARG_L3(q),ARG_H3(q)
                  do j = ARG_L2(q),ARG_H2(q)
                     q(ihi+i,j,k) = -q(ihi-i+1,j,k)
                  end do
               end do
	    end do
         else if (bc(1,2).eq.INT_DIR) then
          ! do nothing
         else
          print *,"bc invalid"
          stop
	 end if
      end if

      if (nbot .gt. 0) then
         jlo = domlo(2)
         
         if ((bc(2,1).eq.FOEXTRAP).or. &
             (bc(2,1).eq.EXT_DIR)) then
	    do j = 1, nbot
               do k = ARG_L3(q),ARG_H3(q)
                  do i = ARG_L1(q),ARG_H1(q)
                     q(i,jlo-j,k) = q(i,jlo,k)
                  end do
               end do
	    end do
         else if (bc(2,1) .eq. HOEXTRAP) then
            do j = 1, nbot
               do k = ARG_L3(q),ARG_H3(q)
                  do i = ARG_L1(q),ARG_H1(q)
                     q(i,jlo-j,k) = two*q(i,jlo-j+1,k)-q(i,jlo-j+2,k)
                  end do
               end do
            end do
	 else if (bc(2,1) .eq. REFLECT_EVEN) then
	    do j = 1, nbot 
               do k = ARG_L3(q),ARG_H3(q)
                  do i = ARG_L1(q),ARG_H1(q)
                     q(i,jlo-j,k) = q(i,jlo+j-1,k)
                  end do
               end do
	    end do
	 else if (bc(2,1) .eq. REFLECT_ODD) then
	    do j = 1, nbot
               do k = ARG_L3(q),ARG_H3(q)
                  do i = ARG_L1(q),ARG_H1(q)
                     q(i,jlo-j,k) = -q(i,jlo+j-1,k)
                  end do
               end do
	    end do
         else if (bc(2,1).eq.INT_DIR) then
          ! do nothing
         else
          print *,"bc invalid"
          stop
	 end if
      end if

      if (ntop .gt. 0) then
         jhi = domhi(2)

         if ((bc(2,2).eq.FOEXTRAP).or. &
             (bc(2,2).eq.EXT_DIR)) then
	    do j = 1, ntop
               do k = ARG_L3(q),ARG_H3(q)
                  do i = ARG_L1(q),ARG_H1(q)
                     q(i,jhi+j,k) = q(i,jhi,k)
                  end do
               end do
	    end do
         else if (bc(2,2) .eq. HOEXTRAP) then
            do j = 1, ntop
               do k = ARG_L3(q),ARG_H3(q)
                  do i = ARG_L1(q),ARG_H1(q)
                     q(i,jhi+j,k) = two*q(i,jhi+j-1,k)-q(i,jhi+j-2,k)
                  end do
               end do
            end do
	 else if (bc(2,2) .eq. REFLECT_EVEN) then
	    do j = 1, ntop
               do k = ARG_L3(q),ARG_H3(q)
                  do i = ARG_L1(q),ARG_H1(q)
                     q(i,jhi+j,k) = q(i,jhi-j+1,k)
                  end do
               end do
	    end do
	 else if (bc(2,2) .eq. REFLECT_ODD) then
	    do j = 1, ntop
               do k = ARG_L3(q),ARG_H3(q)
                  do i = ARG_L1(q),ARG_H1(q)
                     q(i,jhi+j,k) = -q(i,jhi-j+1,k)
                  end do
               end do
	    end do
         else if (bc(2,2).eq.INT_DIR) then
          ! do nothing
         else
          print *,"bc invalid"
          stop
	 end if
      end if

      if (ndwn .gt. 0) then
         klo = domlo(3)

         if ((bc(3,1).eq.FOEXTRAP).or. &
             (bc(3,1).eq.EXT_DIR)) then
	    do k = 1, ndwn
               do j = ARG_L2(q),ARG_H2(q)
                  do i = ARG_L1(q),ARG_H1(q)
                     q(i,j,klo-k) = q(i,j,klo)
                  end do
               end do
	    end do
         else if (bc(3,1) .eq. HOEXTRAP) then
            do k = 1, ndwn
               do j = ARG_L2(q),ARG_H2(q)
                  do i = ARG_L1(q),ARG_H1(q)
                     q(i,j,klo-k) = two*q(i,j,klo-k+1)-q(i,j,klo-k+2)
                  end do
               end do
            end do
	 else if (bc(3,1) .eq. REFLECT_EVEN) then
	    do k = 1, ndwn
               do j = ARG_L2(q),ARG_H2(q)
                  do i = ARG_L1(q),ARG_H1(q)
                     q(i,j,klo-k) = q(i,j,klo+k-1)
                  end do
               end do
	    end do
	 else if (bc(3,1) .eq. REFLECT_ODD) then
	    do k = 1, ndwn
               do j = ARG_L2(q),ARG_H2(q)
                  do i = ARG_L1(q),ARG_H1(q)
                     q(i,j,klo-k) = -q(i,j,klo+k-1)
                  end do
               end do
	    end do
         else if (bc(3,1).eq.INT_DIR) then
          ! do nothing
         else
          print *,"bc invalid"
          stop
	 end if
      end if

      if (nup .gt. 0) then
         khi = domhi(3)

         if ((bc(3,2).eq.FOEXTRAP).or. &
             (bc(3,2).eq.EXT_DIR)) then
	    do k = 1, nup
               do j = ARG_L2(q),ARG_H2(q)
                  do i = ARG_L1(q),ARG_H1(q)
                     q(i,j,khi+k) = q(i,j,khi)
                  end do
               end do
	    end do
         else if (bc(3,2) .eq. HOEXTRAP) then
            do k = 1, nup
               do j = ARG_L2(q),ARG_H2(q)
                  do i = ARG_L1(q),ARG_H1(q)
                     q(i,j,khi+k) = two*q(i,j,khi+k-1)-q(i,j,khi+k-2)
                  end do
               end do
            end do
	 else if (bc(3,2) .eq. REFLECT_EVEN) then
	    do k = 1, nup
               do j = ARG_L2(q),ARG_H2(q)
                  do i = ARG_L1(q),ARG_H1(q)
                     q(i,j,khi+k) = q(i,j,khi-k+1)
                  end do
               end do
	    end do
	 else if (bc(3,2) .eq. REFLECT_ODD) then
	    do k = 1, nup
               do j = ARG_L2(q),ARG_H2(q)
                  do i = ARG_L1(q),ARG_H1(q)
                     q(i,j,khi+k) = -q(i,j,khi-k+1)
                  end do
               end do
	    end do
         else if (bc(3,2).eq.INT_DIR) then
          ! do nothing
         else
          print *,"bc invalid"
          stop
	 end if
      end if

      end


! domlo,domhi are dimensions for face quantity (not cell) 
      subroutine efilcc(bfact, &
       q,DIMS(q), &
       domlo,domhi,bc,dir)
      IMPLICIT NONE

      INTEGER_T    DIMDEC(q)
      INTEGER_T    domlo(SDIM), domhi(SDIM)
      INTEGER_T    bc(SDIM,2),dir,bfact
      REAL_T     q(DIMV(q))

      INTEGER_T    nlft, nrgt, nbot, ntop, nup, ndwn
      INTEGER_T    ilo, ihi, jlo, jhi, klo, khi
      INTEGER_T    is,  ie,  js,  je,  ks,  ke
      INTEGER_T    i, j, k


      if ((dir.lt.0).or.(dir.gt.2)) then
       print *,"dir out of range in EFILCC"
       stop
      endif
      if (bfact.lt.1) then
       print *,"bfact invalid711"
       stop
      endif

      is = max(ARG_L1(q),domlo(1))
      ie = min(ARG_H1(q),domhi(1))
      js = max(ARG_L2(q),domlo(2))
      je = min(ARG_H2(q),domhi(2))
      ks = max(ARG_L3(q),domlo(3))
      ke = min(ARG_H3(q),domhi(3))

      nlft = max(0,domlo(1)-ARG_L1(q))
      nrgt = max(0,ARG_H1(q)-domhi(1))
      nbot = max(0,domlo(2)-ARG_L2(q))
      ntop = max(0,ARG_H2(q)-domhi(2))
      ndwn = max(0,domlo(3)-ARG_L3(q))
      nup  = max(0,ARG_H3(q)-domhi(3))

!     ::::: first fill sides
      if (nlft .ge. 0) then
         ilo = domlo(1)

         if ((bc(1,1).eq.FOEXTRAP).or. &
             (bc(1,1).eq.EXT_DIR).or. &
             (bc(1,1).eq.HOEXTRAP)) then
	    do i = 1, nlft
	    do k = ARG_L3(q),ARG_H3(q)
	    do j = ARG_L2(q),ARG_H2(q)
	       q(ilo-i,j,k) = q(ilo,j,k)
	    enddo
	    enddo
	    enddo
	 elseif (bc(1,1) .eq. REFLECT_EVEN) then
	    do i = 1, nlft
	    do k = ARG_L3(q),ARG_H3(q)
	    do j = ARG_L2(q),ARG_H2(q)
             if (dir.eq.0) then
	       q(ilo-i,j,k) = q(ilo+i,j,k)
             else
	       q(ilo-i,j,k) = q(ilo+i-1,j,k)
             endif
	    enddo
	    enddo
	    enddo
	 elseif (bc(1,1) .eq. REFLECT_ODD) then
	    do k = ARG_L3(q),ARG_H3(q)
	    do j = ARG_L2(q),ARG_H2(q)
              if (dir.eq.0) then
               q(ilo,j,k)=zero
              endif
	    do i = 1, nlft
             if (dir.eq.0) then
	       q(ilo-i,j,k) = -q(ilo+i,j,k)
             else
	       q(ilo-i,j,k) = -q(ilo+i-1,j,k)
             endif
	    enddo
	    enddo
	    enddo
         else if (bc(1,1).ne.INT_DIR) then
          print *,"bc invalid"
          stop
	 endif
      endif

      if (nrgt .ge. 0) then
         ihi = domhi(1)

         if ((bc(1,2).eq.FOEXTRAP).or. &
             (bc(1,2).eq.EXT_DIR).or. &
             (bc(1,2).eq.HOEXTRAP)) then
	    do i = 1, nrgt
	    do k = ARG_L3(q),ARG_H3(q)
	    do j = ARG_L2(q),ARG_H2(q)
	       q(ihi+i,j,k) = q(ihi,j,k)
	    enddo
	    enddo
	    enddo
	 elseif (bc(1,2) .eq. REFLECT_EVEN) then
	    do i = 1, nrgt
	    do k = ARG_L3(q),ARG_H3(q)
	    do j = ARG_L2(q),ARG_H2(q)
             if (dir.eq.0) then
	       q(ihi+i,j,k) = q(ihi-i,j,k)
             else
	       q(ihi+i,j,k) = q(ihi-i+1,j,k)
             endif
	    enddo
	    enddo
	    enddo
	 elseif (bc(1,2) .eq. REFLECT_ODD) then
	    do k = ARG_L3(q),ARG_H3(q)
	    do j = ARG_L2(q),ARG_H2(q)
              if (dir.eq.0) then
               q(ihi,j,k)=zero
              endif
	    do i = 1, nrgt
             if (dir.eq.0) then
	       q(ihi+i,j,k) = -q(ihi-i,j,k)
             else
	       q(ihi+i,j,k) = -q(ihi-i+1,j,k)
             endif
	    enddo
	    enddo
	    enddo
         else if (bc(1,2).ne.INT_DIR) then
          print *,"bc invalid"
          stop
	 endif
      endif

      if (nbot .ge. 0) then
         jlo = domlo(2)
	
         if ((bc(2,1).eq.FOEXTRAP).or. &
             (bc(2,1).eq.EXT_DIR).or. &
             (bc(2,1).eq.HOEXTRAP)) then
	    do j = 1, nbot
	    do k = ARG_L3(q),ARG_H3(q)
	    do i = ARG_L1(q),ARG_H1(q)
	       q(i,jlo-j,k) = q(i,jlo,k)
	    enddo
	    enddo
	    enddo
	 elseif (bc(2,1) .eq. REFLECT_EVEN) then
	    do j = 1, nbot 
	    do k = ARG_L3(q),ARG_H3(q)
	    do i = ARG_L1(q),ARG_H1(q)
             if (dir.eq.1) then
	       q(i,jlo-j,k) = q(i,jlo+j,k)
             else
	       q(i,jlo-j,k) = q(i,jlo+j-1,k)
             endif
	    enddo
	    enddo
	    enddo
	 elseif (bc(2,1) .eq. REFLECT_ODD) then
	    do k = ARG_L3(q),ARG_H3(q)
	    do i = ARG_L1(q),ARG_H1(q)
              if (dir.eq.1) then
               q(i,jlo,k)=zero
              endif
	    do j = 1, nbot
             if (dir.eq.1) then
	       q(i,jlo-j,k) = -q(i,jlo+j,k)
             else
	       q(i,jlo-j,k) = -q(i,jlo+j-1,k)
             endif
	    enddo
	    enddo
	    enddo
         else if (bc(2,1).ne.INT_DIR) then
          print *,"bc invalid"
          stop
	 endif
      endif

      if (ntop .ge. 0) then
         jhi = domhi(2)

         if ((bc(2,2).eq.FOEXTRAP).or. &
             (bc(2,2).eq.EXT_DIR).or. &
             (bc(2,2).eq.HOEXTRAP)) then
	    do j = 1, ntop
	    do k = ARG_L3(q),ARG_H3(q)
	    do i = ARG_L1(q),ARG_H1(q)
	       q(i,jhi+j,k) = q(i,jhi,k)
	    enddo
	    enddo
	    enddo
	 elseif (bc(2,2) .eq. REFLECT_EVEN) then
	    do j = 1, ntop
	    do k = ARG_L3(q),ARG_H3(q)
	    do i = ARG_L1(q),ARG_H1(q)
             if (dir.eq.1) then
	       q(i,jhi+j,k) = q(i,jhi-j,k)
             else
	       q(i,jhi+j,k) = q(i,jhi-j+1,k)
             endif
	    enddo
	    enddo
	    enddo
	 elseif (bc(2,2) .eq. REFLECT_ODD) then
	    do k = ARG_L3(q),ARG_H3(q)
	    do i = ARG_L1(q),ARG_H1(q)
              if (dir.eq.1) then
               q(i,jhi,k)=zero
              endif
	    do j = 1, ntop
             if (dir.eq.1) then
	       q(i,jhi+j,k) = -q(i,jhi-j,k)
             else
	       q(i,jhi+j,k) = -q(i,jhi-j+1,k)
             endif
	    enddo
	    enddo
	    enddo
         else if (bc(2,2).ne.INT_DIR) then
          print *,"bc invalid"
          stop
	 endif
      endif

      if (ndwn .ge. 0) then
         klo = domlo(3)

         if ((bc(3,1).eq.FOEXTRAP).or. &
             (bc(3,1).eq.EXT_DIR).or. &
             (bc(3,1).eq.HOEXTRAP)) then
	    do k = 1, ndwn
	    do j = ARG_L2(q),ARG_H2(q)
	    do i = ARG_L1(q),ARG_H1(q)
	       q(i,j,klo-k) = q(i,j,klo)
	    enddo
	    enddo
	    enddo
	 elseif (bc(3,1) .eq. REFLECT_EVEN) then
	    do k = 1, ndwn
	    do j = ARG_L2(q),ARG_H2(q)
	    do i = ARG_L1(q),ARG_H1(q)
             if (dir.eq.2) then
	       q(i,j,klo-k) = q(i,j,klo+k)
             else
	       q(i,j,klo-k) = q(i,j,klo+k-1)
             endif
	    enddo
	    enddo
	    enddo
	 elseif (bc(3,1) .eq. REFLECT_ODD) then
	    do j = ARG_L2(q),ARG_H2(q)
	    do i = ARG_L1(q),ARG_H1(q)
              if (dir.eq.2) then
               q(i,j,klo)=zero
              endif
	    do k = 1, ndwn
             if (dir.eq.2) then
	       q(i,j,klo-k) = -q(i,j,klo+k)
             else
	       q(i,j,klo-k) = -q(i,j,klo+k-1)
             endif
	    enddo
	    enddo
	    enddo
         else if (bc(3,1).ne.INT_DIR) then
          print *,"bc invalid"
          stop
	 endif
      endif

      if (nup .ge. 0) then
         khi = domhi(3)

         if ((bc(3,2).eq.FOEXTRAP).or. &
             (bc(3,2).eq.EXT_DIR).or. &
             (bc(3,2).eq.HOEXTRAP)) then
	    do k = 1, nup
	    do j = ARG_L2(q),ARG_H2(q)
	    do i = ARG_L1(q),ARG_H1(q)
	       q(i,j,khi+k) = q(i,j,khi)
	    enddo
	    enddo
	    enddo
	 elseif (bc(3,2) .eq. REFLECT_EVEN) then
	    do j = ARG_L2(q),ARG_H2(q)
	    do i = ARG_L1(q),ARG_H1(q)
	    do k = 1, nup
             if (dir.eq.2) then
	       q(i,j,khi+k) = q(i,j,khi-k)
             else
	       q(i,j,khi+k) = q(i,j,khi-k+1)
             endif
	    enddo
	    enddo
	    enddo
	 elseif (bc(3,2) .eq. REFLECT_ODD) then
	    do j = ARG_L2(q),ARG_H2(q)
	    do i = ARG_L1(q),ARG_H1(q)
              if (dir.eq.2) then
               q(i,j,khi)=zero
              endif
	    do k = 1, nup
             if (dir.eq.2) then
	       q(i,j,khi+k) = -q(i,j,khi-k)
             else
	       q(i,j,khi+k) = -q(i,j,khi-k+1)
             endif
	    enddo
	    enddo
	    enddo
         else if (bc(3,2).ne.INT_DIR) then
          print *,"bc invalid"
          stop
	 endif
      endif


      return
      end



! domlo,domhi are dimensions for node quantity (not cell) 
      subroutine ndfilcc(bfact, &
       q,DIMS(q), &
       domlo,domhi,bc)
      IMPLICIT NONE

      INTEGER_T bfact
      INTEGER_T    DIMDEC(q)
      INTEGER_T    domlo(SDIM), domhi(SDIM)
      INTEGER_T    bc(SDIM,2)
      REAL_T       q(DIMV(q))

      INTEGER_T    nlft, nrgt, nbot, ntop, nup, ndwn
      INTEGER_T    ilo, ihi, jlo, jhi, klo, khi
      INTEGER_T    is,  ie,  js,  je,  ks,  ke
      INTEGER_T    i, j, k
      INTEGER_T    kfirst,klast

      if (bfact.lt.1) then
       print *,"bfact invalid712"
       stop
      endif

      is = max(ARG_L1(q),domlo(1))
      ie = min(ARG_H1(q),domhi(1))
      js = max(ARG_L2(q),domlo(2))
      je = min(ARG_H2(q),domhi(2))

#if (AMREX_SPACEDIM==3)
      ks = max(ARG_L3(q),domlo(SDIM))
      ke = min(ARG_H3(q),domhi(SDIM))
      ndwn = max(0,domlo(SDIM)-ARG_L3(q))
      nup  = max(0,ARG_H3(q)-domhi(SDIM))
      kfirst=ARG_L3(q)
      klast=ARG_H3(q)
#elif (AMREX_SPACEDIM==2)
      ks=0
      ke=0
      kfirst=0
      klast=0
#else
      print *,"dimension bust"
      stop
#endif

      nlft = max(0,domlo(1)-ARG_L1(q))
      nrgt = max(0,ARG_H1(q)-domhi(1))
      nbot = max(0,domlo(2)-ARG_L2(q))
      ntop = max(0,ARG_H2(q)-domhi(2))

!     ::::: first fill sides
      if (nlft .ge. 0) then
         ilo = domlo(1)

         if ((bc(1,1).eq.FOEXTRAP).or. &
             (bc(1,1).eq.EXT_DIR).or. &
             (bc(1,1).eq.HOEXTRAP)) then
	    do i = 1, nlft
	    do k = kfirst,klast
	    do j = ARG_L2(q),ARG_H2(q)
	       q(D_DECL(ilo-i,j,k)) = q(D_DECL(ilo,j,k))
	    enddo
	    enddo
	    enddo
	 elseif (bc(1,1) .eq. REFLECT_EVEN) then
	    do i = 1, nlft
	    do k = kfirst,klast
	    do j = ARG_L2(q),ARG_H2(q)
	     q(D_DECL(ilo-i,j,k)) = q(D_DECL(ilo+i,j,k))
	    enddo
	    enddo
	    enddo
	 elseif (bc(1,1) .eq. REFLECT_ODD) then
	    do k = kfirst,klast
	    do j = ARG_L2(q),ARG_H2(q)
               q(D_DECL(ilo,j,k))=zero
	    do i = 1, nlft
	     q(D_DECL(ilo-i,j,k)) = -q(D_DECL(ilo+i,j,k))
	    enddo
	    enddo
	    enddo
         else if (bc(1,1).ne.INT_DIR) then
          print *,"bc invalid"
          stop
	 endif
      endif

      if (nrgt .ge. 0) then
         ihi = domhi(1)

         if ((bc(1,2).eq.FOEXTRAP).or. &
             (bc(1,2).eq.EXT_DIR).or. &
             (bc(1,2).eq.HOEXTRAP)) then
	    do i = 1, nrgt
	    do k = kfirst,klast
	    do j = ARG_L2(q),ARG_H2(q)
	       q(D_DECL(ihi+i,j,k)) = q(D_DECL(ihi,j,k))
	    enddo
	    enddo
	    enddo
	 elseif (bc(1,2) .eq. REFLECT_EVEN) then
	    do i = 1, nrgt
	    do k = kfirst,klast
	    do j = ARG_L2(q),ARG_H2(q)
	     q(D_DECL(ihi+i,j,k)) = q(D_DECL(ihi-i,j,k))
	    enddo
	    enddo
	    enddo
	 elseif (bc(1,2) .eq. REFLECT_ODD) then
	    do k = kfirst,klast
	    do j = ARG_L2(q),ARG_H2(q)
              q(D_DECL(ihi,j,k))=zero
	    do i = 1, nrgt
	     q(D_DECL(ihi+i,j,k)) = -q(D_DECL(ihi-i,j,k))
	    enddo
	    enddo
	    enddo
         else if (bc(1,2).ne.INT_DIR) then
          print *,"bc invalid"
          stop
	 endif
      endif

      if (nbot .ge. 0) then
         jlo = domlo(2)
	
         if ((bc(2,1).eq.FOEXTRAP).or. &
             (bc(2,1).eq.EXT_DIR).or. &
             (bc(2,1).eq.HOEXTRAP)) then
	    do j = 1, nbot
	    do k = kfirst,klast
	    do i = ARG_L1(q),ARG_H1(q)
	       q(D_DECL(i,jlo-j,k)) = q(D_DECL(i,jlo,k))
	    enddo
	    enddo
	    enddo
	 elseif (bc(2,1) .eq. REFLECT_EVEN) then
	    do j = 1, nbot 
	    do k = kfirst,klast
	    do i = ARG_L1(q),ARG_H1(q)
	     q(D_DECL(i,jlo-j,k)) = q(D_DECL(i,jlo+j,k))
	    enddo
	    enddo
	    enddo
	 elseif (bc(2,1) .eq. REFLECT_ODD) then
	    do k = kfirst,klast
	    do i = ARG_L1(q),ARG_H1(q)
              q(D_DECL(i,jlo,k))=zero
	    do j = 1, nbot
	     q(D_DECL(i,jlo-j,k)) = -q(D_DECL(i,jlo+j,k))
	    enddo
	    enddo
	    enddo
         else if (bc(2,1).ne.INT_DIR) then
          print *,"bc invalid"
          stop
	 endif
      endif

      if (ntop .ge. 0) then
         jhi = domhi(2)

         if ((bc(2,2).eq.FOEXTRAP).or. &
             (bc(2,2).eq.EXT_DIR).or. &
             (bc(2,2).eq.HOEXTRAP)) then
	    do j = 1, ntop
	    do k = kfirst,klast
	    do i = ARG_L1(q),ARG_H1(q)
	       q(D_DECL(i,jhi+j,k)) = q(D_DECL(i,jhi,k))
	    enddo
	    enddo
	    enddo
	 elseif (bc(2,2) .eq. REFLECT_EVEN) then
	    do j = 1, ntop
	    do k = kfirst,klast
	    do i = ARG_L1(q),ARG_H1(q)
	     q(D_DECL(i,jhi+j,k)) = q(D_DECL(i,jhi-j,k))
	    enddo
	    enddo
	    enddo
	 elseif (bc(2,2) .eq. REFLECT_ODD) then
	    do k = kfirst,klast
	    do i = ARG_L1(q),ARG_H1(q)
              q(D_DECL(i,jhi,k))=zero
	    do j = 1, ntop
	     q(D_DECL(i,jhi+j,k)) = -q(D_DECL(i,jhi-j,k))
	    enddo
	    enddo
	    enddo
         else if (bc(2,2).ne.INT_DIR) then
          print *,"bc invalid"
          stop
	 endif
      endif

      if (SDIM.eq.3) then

       if (ndwn .ge. 0) then
         klo = domlo(SDIM)

         if ((bc(SDIM,1).eq.FOEXTRAP).or. &
             (bc(SDIM,1).eq.EXT_DIR).or. &
             (bc(SDIM,1).eq.HOEXTRAP)) then
	    do k = 1, ndwn
	    do j = ARG_L2(q),ARG_H2(q)
	    do i = ARG_L1(q),ARG_H1(q)
	       q(D_DECL(i,j,klo-k)) = q(D_DECL(i,j,klo))
	    enddo
	    enddo
	    enddo
	 elseif (bc(SDIM,1) .eq. REFLECT_EVEN) then
	    do k = 1, ndwn
	    do j = ARG_L2(q),ARG_H2(q)
	    do i = ARG_L1(q),ARG_H1(q)
	     q(D_DECL(i,j,klo-k)) = q(D_DECL(i,j,klo+k))
	    enddo
	    enddo
	    enddo
	 elseif (bc(SDIM,1) .eq. REFLECT_ODD) then
	    do j = ARG_L2(q),ARG_H2(q)
	    do i = ARG_L1(q),ARG_H1(q)
              q(D_DECL(i,j,klo))=zero
	    do k = 1, ndwn
	     q(D_DECL(i,j,klo-k)) = -q(D_DECL(i,j,klo+k))
	    enddo
	    enddo
	    enddo
         else if (bc(SDIM,1).ne.INT_DIR) then
          print *,"bc invalid"
          stop
	 endif
       endif

       if (nup .ge. 0) then
         khi = domhi(SDIM)

         if ((bc(SDIM,2).eq.FOEXTRAP).or. &
             (bc(SDIM,2).eq.EXT_DIR).or. &
             (bc(SDIM,2).eq.HOEXTRAP)) then
	    do k = 1, nup
	    do j = ARG_L2(q),ARG_H2(q)
	    do i = ARG_L1(q),ARG_H1(q)
	       q(D_DECL(i,j,khi+k)) = q(D_DECL(i,j,khi))
	    enddo
	    enddo
	    enddo
	 elseif (bc(SDIM,2) .eq. REFLECT_EVEN) then
	    do j = ARG_L2(q),ARG_H2(q)
	    do i = ARG_L1(q),ARG_H1(q)
	    do k = 1, nup
	     q(D_DECL(i,j,khi+k)) = q(D_DECL(i,j,khi-k))
	    enddo
	    enddo
	    enddo
	 elseif (bc(SDIM,2) .eq. REFLECT_ODD) then
	    do j = ARG_L2(q),ARG_H2(q)
	    do i = ARG_L1(q),ARG_H1(q)
              q(D_DECL(i,j,khi))=zero
	    do k = 1, nup
	     q(D_DECL(i,j,khi+k)) = -q(D_DECL(i,j,khi-k))
	    enddo
	    enddo
	    enddo
         else if (bc(SDIM,2).ne.INT_DIR) then
          print *,"bc invalid"
          stop
	 endif
       endif

      else if (SDIM.eq.2) then
       ! do nothing
      else
       print *,"dimension bust"
       stop
      endif


      return
      end



