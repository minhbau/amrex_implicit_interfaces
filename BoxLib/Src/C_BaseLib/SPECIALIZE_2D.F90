
#ifndef BL_LANG_FORT
#define BL_LANG_FORT
#endif

#include <REAL.H>
#include <CONSTANTS.H>
#include <SPECIALIZE_F.H>
#include <ArrayLim.H>

      subroutine FORT_FASTCOPY ( &
       dest,DIMS(dest), &
       imin, jmin, imax, jmax, &
       src, &
       DIMS(src), &
       imn,  jmn, &
       ncomp)

      implicit none
      INTEGER_T imin, jmin, imax, jmax
      INTEGER_T DIMDEC(dest)
      INTEGER_T imn,  jmn
      INTEGER_T DIMDEC(src)
      INTEGER_T ncomp
      
      REAL_T  dest(DIMV(dest),ncomp)
      REAL_T  src(DIMV(src),ncomp)
      INTEGER_T i,j,k,ioff,joff

      ioff=imn-imin
      joff=jmn-jmin

      do k = 1, ncomp
         do j = jmin,jmax
            do i = imin,imax
               dest(i,j,k) = src(i+ioff,j+joff,k)
            end do
         end do
      end do

      end
      subroutine FORT_FASTSETVAL ( &
       val,lo,hi, &
       dest,DIMS(dest), &
       ncomp)

      implicit none

      INTEGER_T ncomp
      INTEGER_T lo(2), hi(2)
      INTEGER_T DIMDEC(dest)
      REAL_T  val
      REAL_T  dest(DIMV(dest),ncomp)
      INTEGER_T i,j,k
      INTEGER_T imin,jmin,imax,jmax

      imin = lo(1)
      jmin = lo(2)
      imax = hi(1)
      jmax = hi(2)

      do k = 1, ncomp
         do j = jmin,jmax
            do i = imin,imax
               dest(i,j,k) = val
            end do
         end do
      end do

      end
      subroutine FORT_FASTZERONORM ( &
       src,DIMS(src), &
       lo,hi,ncomp,nrm)

      implicit none

      INTEGER_T ncomp
      INTEGER_T lo(3), hi(3)
      INTEGER_T DIMDEC(src)
      REAL_T  src(DIMV(src),ncomp) 
      REAL_T nrm
      INTEGER_T i,j,k
      INTEGER_T imin,jmin,imax,jmax

      imin = lo(1)
      jmin = lo(2)
      imax = hi(1)
      jmax = hi(2)

      nrm = 0.0d0

      do k = 1,ncomp
         do j = jmin,jmax
            do i = imin,imax
               nrm = max(nrm, abs(src(i,j,k)))
            end do
         end do
      end do

      end
      subroutine FORT_FASTONENORM ( &
       src,DIMS(src), &
       lo,hi,ncomp,nrm)

      implicit none

      INTEGER_T ncomp
      INTEGER_T lo(3), hi(3)
      INTEGER_T DIMDEC(src)
      REAL_T  src(DIMV(src),ncomp)
      REAL_T  nrm
      INTEGER_T i,j,k
      INTEGER_T imin,jmin,imax,jmax

      imin = lo(1)
      jmin = lo(2)
      imax = hi(1)
      jmax = hi(2)

      nrm = 0.0d0

      do k = 1, ncomp
         do j = jmin,jmax
            do i = imin,imax
               nrm = nrm + abs(src(i,j,k))
            end do
         end do
      end do

      end
      subroutine FORT_FASTPLUS ( &
       dest,DIMS(dest), &
       imin, jmin, imax, jmax, &
       src, &
       DIMS(src), &
       imn,  jmn, &
       ncomp)

      implicit none
      INTEGER_T imin, jmin, imax, jmax
      INTEGER_T DIMDEC(dest)
      INTEGER_T imn,  jmn
      INTEGER_T DIMDEC(src)
      INTEGER_T ncomp
      
      REAL_T  dest(DIMV(dest),ncomp)
      REAL_T  src(DIMV(src),ncomp)
      INTEGER_T i,j,k,ioff,joff

      ioff=imn-imin
      joff=jmn-jmin

      do k = 1, ncomp
         do j = jmin,jmax
            do i = imin,imax
               dest(i,j,k) = dest(i,j,k) + src(i+ioff,j+joff,k)
            end do
         end do
      end do

      end
      subroutine FORT_FASTMULT ( &
       dest,DIMS(dest), &
       imin, jmin, imax, jmax, &
       src, &
       DIMS(src), &
       imn,  jmn, &
       ncomp)

      implicit none
      INTEGER_T imin, jmin, imax, jmax
      INTEGER_T DIMDEC(dest)
      INTEGER_T imn,  jmn
      INTEGER_T DIMDEC(src)
      INTEGER_T ncomp
      
      REAL_T  dest(DIMV(dest),ncomp)
      REAL_T  src(DIMV(src),ncomp)
      INTEGER_T i,j,k,ioff,joff

      ioff=imn-imin
      joff=jmn-jmin

      do k = 1, ncomp
         do j = jmin,jmax
            do i = imin,imax
               dest(i,j,k) = dest(i,j,k) * src(i+ioff,j+joff,k)
            end do
         end do
      end do

      end
      subroutine FORT_FASTMINUS ( &
       dest, &
       DIMS(dest), &
       imin, jmin, imax, jmax, &
       src, &
       DIMS(src), &
       imn,  jmn, &
       ncomp)

      implicit none
      INTEGER_T imin, jmin, imax, jmax
      INTEGER_T DIMDEC(dest)
      INTEGER_T imn,  jmn
      INTEGER_T DIMDEC(src)
      INTEGER_T ncomp
      
      REAL_T  dest(DIMV(dest),ncomp)
      REAL_T  src(DIMV(src),ncomp)
      INTEGER_T i,j,k,ioff,joff

      ioff=imn-imin
      joff=jmn-jmin

      do k = 1, ncomp
         do j = jmin,jmax
            do i = imin,imax
               dest(i,j,k) = dest(i,j,k) - src(i+ioff,j+joff,k)
            end do
         end do
      end do

      end
      subroutine FORT_FASTDIVIDE ( &
       dest, &
       DIMS(dest), &
       imin, jmin, imax, jmax, &
       src, &
       DIMS(src), &
       imn,  jmn, &
       ncomp)

      implicit none
      INTEGER_T imin, jmin, imax, jmax
      INTEGER_T DIMDEC(dest)
      INTEGER_T imn,  jmn
      INTEGER_T DIMDEC(src)
      INTEGER_T ncomp
      
      REAL_T  dest(DIMV(dest),ncomp)
      REAL_T  src(DIMV(src),ncomp)
      INTEGER_T i,j,k,ioff,joff

      ioff=imn-imin
      joff=jmn-jmin

      do k = 1, ncomp
         do j = jmin,jmax
            do i = imin,imax
               dest(i,j,k) = dest(i,j,k) / src(i+ioff,j+joff,k)
            end do
         end do
      end do

      end
