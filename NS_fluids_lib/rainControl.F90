#undef BL_LANG_CC
#define BL_LANG_FORT

#include "AMReX_REAL.H"
#include "AMReX_CONSTANTS.H"
#include "AMReX_SPACE.H"
#include "AMReX_BC_TYPES.H"

#if (AMREX_SPACEDIM==3)
#define SDIM 3
#elif (AMREX_SPACEDIM==2)
#define SDIM 2
#else  
print *,"dimension bust"
stop
#endif

module rainControl_module

implicit none

      INTEGER_T, PARAMETER :: maxDrop=0  ! was 400
      INTEGER_T, PARAMETER :: maxDrop_alloc=400 
      REAL_T, PARAMETER :: period=0.0004
      REAL_T, PARAMETER :: rDrop=0.1
      REAL_T, PARAMETER :: uDrop=-600.0
      REAL_T xRange,xShift,xMax,xMin 
      REAL_T startPosi(maxDrop_alloc)
      REAL_T xrand(maxDrop_alloc)
      REAL_T LSDrop(maxDrop_alloc)

contains

! probtype=701
      subroutine get_rain_vfrac(x,y,z,dx,vfrac,cenbc,time,nmat,dir,adv_vel)
      IMPLICIT NONE

      REAL_T adv_vel
      INTEGER_T nmat,dir
      REAL_T x,y,z,time
      INTEGER_T im
      REAL_T dx(SDIM)
      REAL_T vfrac(nmat)
      REAL_T cenbc(nmat,SDIM)

      INTEGER_T nDrop
      REAL_T LS
      REAL_T startPosi(maxDrop_alloc)
      REAL_T xrand(maxDrop_alloc)
      REAL_T nowTime,rainDuTime
      INTEGER_T iLoc,nLoc,iFound
      INTEGER_T maxDrop_for_mod;


      if (maxDrop.gt.0) then

       maxDrop_for_mod=maxDrop;

       if (dir.eq.1) then
         rainDuTime=rDrop*2/abs(adv_vel)
       else
         rainDuTime=rDrop*2/abs(uDrop)
       endif
       nLoc=ceiling(rainDuTime/period)
       if (nLoc.gt.maxDrop) then
         print*, "rainDuTime=",rainDuTime,"period=",period
         print*, "increase the size of xrand, nLoc=",nLoc
         stop
       endif
        ! if period is smaller than ranDuTime
        ! we want to increase the possible locations 
        ! raindrops can go into the domain
       Call random_seed
       call random_number(startPosi)
       nDrop=mod(ceiling(time/rainDuTime),maxDrop_for_mod)+1
       !nowTime=MOD(time,rainDuTime)
       nowTime=MOD(time,period) ! assume period>rainDuTime
       if (dir.eq.1) then
         xMax=2.0
         xMin=1.0
         ! evenly distribute the points
         xrand(1)=startPosi(nDrop)*(xMax-xMin)+xMin
         do iLoc=2,nLoc
          xrand(iLoc)=xrand(1)+(xMax-xMin)*(iLoc-1.0)/(nLoc-1.0)
          if (xrand(iLoc).gt.xMax)xrand(iLoc)=xrand(iLoc)-(xMax-xMin)
         enddo
         If (nowTime.le.rainDuTime) then
          LS=sqrt(rDrop*rDrop-(rDrop-adv_vel*nowTime)**2)
          iLoc=1
          iFound=0
          do while (iLoc.le.nLoc.and.iFound.eq.0)
            if (abs(y-(xrand(iLoc)+uDrop*nowTime)).le.LS) then
             vfrac(1)=zero
             iFound=1
             !print*,"iLoc=",iLoc,"xLoc=",xrand(iLoc),"nDrop=",nDrop
            else
             vfrac(1)=one
            endif
            iLoc=iLoc+1
          enddo
         else
           vfrac(1)=one
         endif  
       elseif (dir.eq.2) then    
         xMax=10.0
         xMin=-4.0
         ! evenly distribute the points
         xrand(1)=startPosi(nDrop)*(xMax-xMin)+xMin
         do iLoc=2,nLoc
          xrand(iLoc)=xrand(1)+(xMax-xMin)*(iLoc-1.0)/(nLoc-1.0)
          if (xrand(iLoc).gt.xMax) xrand(iLoc)=xrand(iLoc)-(xMax-xMin)
         enddo
         If (nowTime.le.rainDuTime) then
          LS=sqrt(rDrop*rDrop-(rDrop-abs(uDrop)*nowTime)**2)
          iLoc=1
          iFound=0
          do while (iLoc.le.nLoc.and.iFound.eq.0)
            if (abs(x-xrand(iLoc)-adv_vel*nowTime).le.LS) then
             vfrac(1)=zero
             iFound=1
             !print*,"iLoc=",iLoc,"xLoc=",xrand(iLoc),"nDrop=",nDrop
            else
             vfrac(1)=one
            endif
            iLoc=iLoc+1
          enddo
         else
           vfrac(1)=one
         endif  
       else
         print*, "wrong dir in get_rain_vfrac"
         stop      
       endif 
      else if (maxDrop.eq.0) then
       vfrac(1)=zero
      else
       print *,"maxDrop invalid"
       stop
      endif

      vfrac(2)=1.0-vfrac(1)
      do im=3,nmat
         vfrac(im)=zero
      enddo

      return
      end subroutine get_rain_vfrac

      subroutine get_rain_velocity(x,y,z,dx,vel,vel_rain,time,dir,nmat, &
         adv_vel)
      IMPLICIT NONE

      REAL_T x,y,z,time,adv_vel
      REAL_T dx(SDIM)
      REAL_T vel,vel_rain

      INTEGER_T nmat
      INTEGER_T dir
      REAL_T VOF(nmat)
      REAL_T cenbc(nmat,SDIM)

      call get_rain_vfrac(x,y,z,dx,VOF,cenbc,time,nmat,dir,adv_vel) 

      !if (x.gt.1.0.and.x.le.1.3) print*, "x12=",x, VOF(1)

      ! depending on the dir direction 
      ! vel_rain can be the x or y component of the velocity
      ! vof(1)=0 liquid
      if (VOF(1).le.zero) then
       vel=vel_rain
      !else
       ! should not add anything here       
      endif

      return
      end subroutine get_rain_velocity

      subroutine xloLS_rain(x,y,z,nmat,LSparm,adv_vel,time,bigdist)
      IMPLICIT NONE

      REAL_T x,y,z,time
      REAL_T adv_vel
      INTEGER_T nmat
      REAL_T LSparm(nmat)
      REAL_T LS

      INTEGER_T nDrop,iLoc,nLoc,insiderDrop
      REAL_T bigdist,LS0
      REAL_T nowTime,rainDuTime
      INTEGER_T im
      INTEGER_T maxDrop_for_mod;

      if (maxDrop.gt.0) then

       maxDrop_for_mod=maxDrop;

       rainDuTime=2.0*rDrop/abs(adv_vel)
       nLoc=ceiling(rainDuTime/period)
       if (nLoc.gt.maxDrop) then
         print*, "rainDuTime=",rainDuTime,"period=",period
         print*, "increase the size of xrand, nLoc=",nLoc
         stop
       endif
       !nowTime=MOD(time,rainDuTime)
       nowTime=MOD(time,period)
       Call random_seed
       call random_number(startPosi)
       nDrop=mod(ceiling(time/rainDuTime),maxDrop_for_mod)+1
       xrand(1)=startPosi(nDrop)*(xMax-xMin)+xMin
       do iLoc=2,nLoc
        xrand(iLoc)=xrand(1)+(xMax-xMin)*(iLoc-1.0)/(nLoc-1.0)
        if (xrand(iLoc).gt.xMax) xrand(iLoc)=xrand(iLoc)-(xMax-xMin)
       enddo
       if (nowTime.le.rainDuTime) then
        LS0=sqrt(rDrop*rDrop-(rDrop-abs(adv_vel)*nowTime)**2)
        ! there are nLoc drops here, need to find the level set distance
        do iLoc=1,nLoc
         LSDrop(iLoc)=LS0-abs(y-(xrand(iLoc)+uDrop*nowTime))
        enddo
           
        insiderDrop=0
        do iLoc=1,nLoc
          if (LSDrop(iLoc).ge.0.0) then  ! assume drops are not close
            insiderDrop=1
            LSparm(1)=LSDrop(iLoc)
            LSparm(2)=-LSDrop(iLoc)
            !print*, "iLoc2=", iLOC, "xra=",xrand(iLoc),"adv",adv_vel
          endif
        enddo
      
        if (insiderDrop.eq.0) then ! point not in any drops find the smallest
          LS=bigdist
          do iLoc=1,nLoc
            if (abs(LSDrop(iLoc)).le.abs(LS)) LS=LSDrop(iLoc)
          enddo
          LSparm(1)=LS
          LSparm(2)=-LS
        endif

       else if (maxDrop.eq.0) then
        LSparm(1)=-bigdist
        LSparm(2)=bigdist
       else
        print *,"maxDrop invalid"
        stop
       endif
       do im=3,nmat
        LSparm(im)=-bigdist
       enddo
      endif

      end subroutine xloLS_rain


      subroutine yhiLS_rain(x,y,z,nmat,LSparm,adv_vel,time,bigdist)
      IMPLICIT NONE

      REAL_T x,y,z,time
      REAL_T adv_vel
      INTEGER_T nmat
      REAL_T LSparm(nmat)
      REAL_T LS

      INTEGER_T nDrop,iLoc,nLoc,insiderDrop
      REAL_T bigdist,LS0
      REAL_T nowTime,rainDuTime
      INTEGER_T im
      INTEGER_T maxDrop_for_mod;

      if (maxDrop.gt.0) then

       maxDrop_for_mod=maxDrop;

       rainDuTime=2.0*rDrop/abs(uDrop)
       nLoc=ceiling(rainDuTime/period)
       if (nLoc.gt.maxDrop) then
         print*, "rainDuTime=",rainDuTime,"period=",period
         print*, "increase the size of xrand, nLoc=",nLoc
         stop
       endif

       !nowTime=MOD(time,rainDuTime)
       nowTime=MOD(time,period)
       Call random_seed
       call random_number(startPosi)
       nDrop=mod(ceiling(time/rainDuTime),maxDrop_for_mod)+1
       xrand(1)=startPosi(nDrop)*(xMax-xMin)+xMin
       do iLoc=2,nLoc
         xrand(iLoc)=xrand(1)+(xMax-xMin)*(iLoc-1.0)/(nLoc-1.0)
         if (xrand(iLoc).gt.xMax) xrand(iLoc)=xrand(iLoc)-(xMax-xMin)
       enddo
       if (nowTime.le.rainDuTime) then
        LS0=sqrt(rDrop*rDrop-(rDrop-abs(uDrop)*nowTime)**2)
        ! there are nLoc drops here, need to find the level set distance
        do iLoc=1,nLoc
          LSDrop(iLoc)=LS0-abs(x-xrand(iLoc)-adv_vel*nowTime)
        enddo
        
        insiderDrop=0
        do iLoc=1,nLoc
           if (LSDrop(iLoc).ge.0.0) then  ! assume drops are not close
             insiderDrop=1
             LSparm(1)=LSDrop(iLoc)
             LSparm(2)=-LSDrop(iLoc)
             !print*, "iLoc2=", iLOC, "xra=",xrand(iLoc),"adv",adv_vel
           endif
        enddo
       
        if (insiderDrop.eq.0) then ! point not in any drops find the smallest
          LS=bigdist
          do iLoc=1,nLoc
             if (abs(LSDrop(iLoc)).le.abs(LS)) LS=LSDrop(iLoc)
          enddo
          LSparm(1)=LS
          LSparm(2)=-LS
        endif

       else if (maxDrop.eq.0) then
        LSparm(1)=-bigdist
        LSparm(2)=bigdist
       else
        print *,"maxDrop invalid"
        stop
       endif

       do im=3,nmat
          LSparm(im)=-bigdist
       enddo       
      endif

      end subroutine yhiLS_rain


end module rainControl_module


