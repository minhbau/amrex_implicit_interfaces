MODULE CubicSpline
! 18 Jan 05 v7. Calculating the fraction of distance {0,1} for knots along spline points and
!			  saving in Coords(:,6)
  use SortNodePts, only : GaussQuad_data

implicit none
! used to restrict variables to prevent accidental calling outside
private      ! everything defaults to 'private' except those expressly labeled otherwise.
public								:: CurveFit, NewCoords
real*8, parameter					:: epsil=1e-6, smallepsil=1e-10
real*8, dimension(:,:), allocatable	:: NewCoords

contains 

! ***************
! Subroutine CurveFit
! ***************
! DESCRIPTION: given N points and S(1):S(N) and S'(1)=S'(N)=0, determines the linear or 
!   cubic spline fit between those points so the N equations match on x, x' and x'' 
!
! INPUT: knots, panel lengths
!
! OUTPUT: C[x] coordinate 
!
! CALLING PROGRAM: MAT_Greens
!
! Primary author: Anton VanderWyst
!	University of Michigan
!	1320 Beal Ave.
!	Ann Arbor, MI 48109
!	antonv@umich.edu 
!
! Version history:
!   03 Dec 2004 begun
!	07 Dec	v4	changing to calculate the total number of spline pts

SUBROUTINE CurveFit ( SplinePts, knotsx, knotsy, hX, hY, sizeknot, &
  PanelVals, ConeSize, CublicSplineFit, PreviousKnot, M )

! incoming variables
integer, intent(in)							:: sizeknot, ConeSize, CublicSplineFit, M
real*8, dimension(sizeknot), intent(in)		:: knotsx, knotsy, hX, hY
real*8, dimension(sizeknot,2), intent(in)	:: PanelVals

! outsourced variables
integer, intent(inout)						:: SplinePts
real*8, dimension(sizeknot,2),intent(inout) :: PreviousKnot

! subroutine entirely variables
integer										:: XYFit ! formerly logical
integer										:: i, j
real*8, dimension(sizeknot)					:: totDist
real*8, dimension(ConeSize,4)				:: Sx, Sy

continue ! body of program
!*******
! the final equation will look like Si[x]=ai+bi(x-xi)+ci(x-xi)^2+di(x-xi)^3
! setting up matricies and constants for 
! CubicSplineFit = 1 for cubic interpolation, = 0 for linear interpolation

! finds the spline coefficient fits (linear or cubic) for the knotsx/knotsy points 
!   using x vs. y. Use only Sx
!   NOTE: 'totDist' is meaningless in the first iteration
call LinearCubic ( Sx, Sy, knotsx, knotsy, hX, hY, sizeknot, ConeSize, totDist, CublicSplineFit, 1 )

! using Sx, calculates the total distance around the shape and the corresponding
!   number of total panels for a balance between accuracy and speed. Use totDist, SplinePts
totDist(:)=0
call totDistCalc ( totDist, SplinePts, Sx, knotsx, knotsy, ConeSize, sizeknot, 10 )

! using totDist, finds the spline coefficient fits (linear or cubic) for the 
!   knotsx/knotsy points using S vs. x and S vs. y. Use new Sx, Sy
call LinearCubic ( Sx, Sy, knotsx, knotsy, hX, hY, sizeknot, ConeSize, totDist, CublicSplineFit, 0 )

! using Sx, Sy for the new curve, returns the evenly space points SplineCoords
!   along the surface, Finally(!) get 'NewCoords' we desire
allocate(NewCoords(SplinePts,5))
NewCoords(:,:)=0
call SplineLengthFit (NewCoords, Sx, Sy, knotsx, knotsy, SplinePts, PanelVals, sizeknot, &
  totDist, ConeSize, CublicSplineFit, PreviousKnot, M)

END SUBROUTINE CurveFit

! ***************
! Subroutine LinearCubic
! ***************
! DESCRIPTION: given N points and S(1):S(N) and S'(1)=S'(N)=0, determines the linear or
!   cubic spline fit between those points so the N equations match on x, x' and x'' 
!
! INPUT: knots, end derivatives, panel lengths
!
! OUTPUT: C[x] coordinate 
!
! CALLING PROGRAM: MAT_Greens
!
! Primary author: Anton VanderWyst
!	University of Michigan
!	1320 Beal Ave.
!	Ann Arbor, MI 48109
!	antonv@umich.edu 
!
! Version history:
!   14 Nov 2004 begun
!   27 Nov		changing distance calculation to go along spline, not linear
!	02 Dec		doing an x-y spline fit, then calculating distance afterwards
!   03 Dec		pulling linear and cubic into 1 subroutine linearcubic

SUBROUTINE LinearCubic ( Sx, Sy, knotsx, knotsy, hX, hY, sizeknot, ConeSize, totDist, &
  CubicSplineFit, XYFit )

! incoming variables
integer, intent(in)							:: sizeknot, ConeSize, CubicSplineFit, XYFit
real*8, dimension(sizeknot), intent(in)		:: knotsx, knotsy, hX, hY, totDist

! outsourced variables
real*8, dimension(ConeSize,4), intent(inout)  :: Sx, Sy

! subroutine entirely variables
integer										:: i, j, CubicXYFit
real*8, dimension(ConeSize)					:: a, b, c, d, RHSCubic, aDiag, bDiag, &
												cDiag
real*8, dimension(sizeknot)					:: hDist, distChange

continue ! body of program
!*******
! the final equation will look like Si[x]=ai+bi(x-xi)+ci(x-xi)^2+di(x-xi)^3
! setting up matricies and constants for 
CubicXYFit = 10*XYFit + CubicSplineFit

select case (CubicXYFit)
case (11)				! First loop, x vs y, cubic interpolation
!-------------------------------------------------------------------
  RHSCubic(:)=0

  a(1)=1.*knotsy(1); a(ConeSize)=1.*knotsy(ConeSize)
  aDiag(1)=0; bDiag(1) = 1.; cDiag(1)=0
  aDiag(ConeSize) = 0; bDiag(ConeSize)=1.; cDiag(ConeSize)=0

  do i=2,ConeSize-1
    a(i)=1.*knotsy(i)
    aDiag(i)=1.*hX(i-1)/3.
    bDiag(i)=2.*hX(i-1)/3. + 2.*hX(i)/3.
    cDiag(i)=1.*hX(i)/3.
  enddo

  do i=2,ConeSize-1
    if ( abs(hX(i)) > smallepsil .AND. abs(hX(i-1))> smallepsil .AND. a(i+1)-a(i) > epsil/10. ) then
      RHSCubic(i)=1.*(a(i+1)-a(i))/hX(i) - 1.*(a(i)-a(i-1))/hX(i-1)
    else
      RHSCubic(i)=0
    endif
  enddo

  !RHSCubic(1)=(a(2)-a(1))/hX(1)
  RHSCubic(1)=0; RHSCubic(ConeSize)=0
  c(:)=0
  call Tridiag( c, aDiag, bDiag, cDiag, RHSCubic, ConeSize )

  do i=1,ConeSize-1
    if ( abs(hX(i)) > smallepsil ) then
      b(i) = 1.*( a(i+1)-a(i) )/hX(i) - (2.*hX(i)*c(i)/3. + hX(i)*c(i+1)/3.)
      d(i) = 1.*( c(i+1)-c(i) )/(3.*hX(i))
	else
	  b(i)=0; d(i)=0
	endif
  enddo

  Sx(:,1)=a(:); Sx(:,2)=b(:); Sx(:,3)=c(:); Sx(:,4)=d(:)

case (10)				! First loop, x vs y, linear interpolation
!-------------------------------------------------------------------
  Sx(:,3)=0; Sx(:,4)=0

  do i=1,ConeSize
    Sx(i,1)=1.*knotsy(i)
  enddo
  do i=1,ConeSize-1
    if ( abs(hX(i)) > smallepsil ) then
      Sx(i,2) = 1.*( Sx(i+1,1)-Sx(i,1) )/hX(i)
    else
	  Sx(i,2) = 0
	endif
  enddo

case (01)				! Second loop, x,y vs s, cubic interpolation
!-------------------------------------------------------------------
  distChange(1) = 1.*totDist(1)
  a(1)=1.*knotsx(1); a(ConeSize)=1.*knotsx(ConeSize)
  aDiag(1)=0; bDiag(1) = 1.; cDiag(1)=0
  aDiag(ConeSize) = 0; bDiag(ConeSize)=1.; cDiag(ConeSize)=0

  do i=2,ConeSize-1
    distChange(i) = 1.*(totDist(i)-totDist(i-1))

    a(i)=1.*knotsx(i)
    aDiag(i)=distChange(i-1)/3.
    bDiag(i)=2.*distChange(i-1)/3. + 2.*distChange(i)/3.
    cDiag(i)=distChange(i)/3.
  enddo

  do i=2,ConeSize-1
    if ( abs(distChange(i)) > smallepsil .AND. a(i+1)-a(i) > epsil/10. ) then
      RHSCubic(i)=1.*(a(i+1)-a(i))/distChange(i) - 1.*(a(i)-a(i-1))/distChange(i-1)
    else
      RHSCubic(i)=0
    endif
  enddo

  !RHSCubic(1)=(a(2)-a(1))/hX(1)
  RHSCubic(1)=0; RHSCubic(ConeSize)=0
  c(:)=0
  call Tridiag( c, aDiag, bDiag, cDiag, RHSCubic, ConeSize )

  do i=1,ConeSize-1
    if ( abs(distChange(i)) > smallepsil ) then
      b(i) = 1.*( a(i+1)-a(i) )/distChange(i) - (2.*distChange(i)*c(i)/3. + distChange(i)*c(i+1)/3.)
      d(i) = ( c(i+1)-c(i) )/(3.*distChange(i))
	else
	  b(i)=0; d(i)=0
	endif
  enddo

  Sx(:,1)=a(:); Sx(:,2)=b(:); Sx(:,3)=c(:); Sx(:,4)=d(:)
  !******
  ! Same section, now for y vs s.
  do i=1,ConeSize
    a(i)=1.*knotsy(i)
  enddo

  do i=2,ConeSize-1
    if ( abs(distChange(i)) > epsil .AND. a(i+1)-a(i) > epsil/10. ) then
      RHSCubic(i)=1.*(a(i+1)-a(i))/distChange(i) - 1.*(a(i)-a(i-1))/distChange(i-1)
    else
      RHSCubic(i)=0
    endif
  enddo

  !RHSCubic(1)=(a(2)-a(1))/hY(1)
  RHSCubic(1)=0; RHSCubic(ConeSize)=0
  c(:)=0
  call Tridiag( c, aDiag, bDiag, cDiag, RHSCubic, ConeSize )

  do i=1,ConeSize-1
    if ( abs(distChange(i)) > epsil ) then
      b(i) = 1.*( a(i+1)-a(i) )/distChange(i) - (2.*distChange(i)*c(i)/3. + distChange(i)*c(i+1)/3.)
      d(i) = 1.*( c(i+1)-c(i) )/(3.*distChange(i))
	else 
	  b(i) = 0; d(i)=0
	endif
  enddo

  Sy(:,1)=a(:); Sy(:,2)=b(:); Sy(:,3)=c(:); Sy(:,4)=d(:)

case (00)				! Second loop, x,y vs s, linear interpolation
!-------------------------------------------------------------------
  Sx(:,3)=0; Sx(:,4)=0
  Sy(:,3)=0; Sy(:,4)=0

  do i=1,ConeSize
    if (i /= 1) then
  	  distChange(i) = 1.*( totDist(i)-totDist(i-1) )
	else
  	  distChange(i) = 1.*totDist(i)
	endif

	Sx(i,1)=1.*knotsx(i)
	Sy(i,1)=1.*knotsy(i)
  enddo

  do i=1,ConeSize-1
    if ( abs(distChange(i)) > smallepsil ) then
	  Sx(i,2) = 1.*( Sx(i+1,1)-Sx(i,1) )/distChange(i)
	  Sy(i,2) = 1.*( Sy(i+1,1)-Sy(i,1) )/distChange(i)
	else 
	  Sx(i,2)=0; Sy(i,2)=0
	endif
  enddo
case default
  stop 'Error on CubicXYFit'
end select ! switch CubicXYFit

END SUBROUTINE LinearCubic

!----------
! ***************
! Subroutine SplineLengthFit
! ***************
! DESCRIPTION: takes linear and cubic line fits and places points along them evenly
!	Also changes 'Coords(:,5)' to be index {i} of previous knotsx(i), knotsy(i)
!
! INPUT: Sx, Sy
!
! OUTPUT: SplineCoords
!
! CALLING PROGRAM: Linear, Cubic
!
! Primary author: Anton VanderWyst
!	University of Michigan
!	1320 Beal Ave.
!	Ann Arbor, MI 48109
!	antonv@umich.edu 
!
! Version history:
!   21 Nov 2004 begun
!   10 Jan 2005 v6 changing SplineLength calcs. Removing data file printing

SUBROUTINE SplineLengthFit (SplineCoords, Sx, Sy, knotsx, knotsy, SplinePts, &
  PanelVals, sizeknot, totDist, ConeSize, CubicSplineFit, PreviousPan, M3 )

! incoming variables
integer, intent(in)							:: SplinePts, sizeknot, ConeSize, CubicSplineFit, M3
! CubicSplineFit = 1 for cubic interpolation, = 0 for linear interpolation
real*8, dimension(ConeSize,4), intent(in)	:: Sx, Sy
real*8, dimension(sizeknot,2), intent(in)	:: PanelVals
real*8, dimension(sizeknot), intent(in)		:: totDist, knotsx, knotsy

! outsourced variables
real*8,dimension(SplinePts,4),intent(inout) :: SplineCoords
real*8, dimension(sizeknot,2),intent(inout)	:: PreviousPan

! subroutine entirely variables
integer										:: Splineunit, tempJ, i, j
real*8										:: temptotDist, tempfracDist, tempknotDist, knotFrac

real*8, dimension(SplinePts)				:: SplineLength

continue

Splineunit=10
tempJ=1; SplineLength(:)=0; PreviousPan(1,1)=1
do i=1,SplinePts
  SplineLength(i) = 1.*(i-1)*totDist(sizeknot)/SplinePts
  if (tempJ /= 1) then
    j = tempJ
  else
    j = 1
  endif
  
  ChoosePanel: do while ( totDist(j) <= SplineLength(i) .AND. j<=sizeknot ) 
	j=j+1
  	PreviousPan(j,1)=i-1	! for knot {j}, the previous panel was = {...}
    PreviousPan(j,2)=1.*(totDist(j-1)-SplineLength(i-1))/(SplineLength(i)-SplineLength(i-1))
	
	!call KnotDistCalc (PreviousPan(j,2), SplineCoords(i-1,1:2), SplineCoords(i,1:2), &
	!  Sx(j,:), knotsx(j), knotsy(j), M3, totDist(i-1:i) )

    !SUBROUTINE KnotDistCalc ( FracDist, SplinePtPre, SplinePtPost, Sx2, knotx2, knoty2, M2, PantotDist )
  enddo ChoosePanel
  tempJ = j

  if (1==j) then
    temptotDist = 0
  else
    temptotDist = 1.*totDist(j-1)
  endif

  if (j<ConeSize) then
    tempfracDist = 1.*(SplineLength(i)-temptotDist)
    SplineCoords(i,1) = Sx(j,1) + Sx(j,2)*(tempfracDist) + &
	  Sx(j,3)*tempfracDist**2 + Sx(j,4)*tempfracDist**3
    SplineCoords(i,2) = Sy(j,1) + Sy(j,2)*(tempfracDist) + &
      Sy(j,3)*(tempfracDist)**2 + Sy(j,4)*(tempfracDist)**3
  else
    tempfracDist = ( SplineLength(i)-totDist(j-1)) / (totDist(j)-totDist(j-1))
	if (j /=sizeknot) then
  	  SplineCoords(i,1) = tempfracDist*(knotsx(j+1)-knotsx(j))+knotsx(j)
      SplineCoords(i,2) = tempfracDist*(knotsy(j+1)-knotsy(j))+knotsy(j)
	else
  	  SplineCoords(i,1) = tempfracDist*(knotsx(1)-knotsx(j))+knotsx(j)
      SplineCoords(i,2) = tempfracDist*(knotsy(1)-knotsy(j))+knotsy(j)
	endif
  endif
  SplineCoords(i,3) = 1.*PanelVals(j,1)
  SplineCoords(i,4) = 1.*PanelVals(j,2)
enddo

END SUBROUTINE SplineLengthFit

!----------
! ***************
! Subroutine Tridiag
! ***************
! DESCRIPTION: given a tridiagonal matrix, rapidly solves for Ax=b
!
! INPUT: three bands, rhs
!
! OUTPUT: x
!
! CALLING PROGRAM: Cubic
!
! Primary author: Anton VanderWyst
!	University of Michigan
!	1320 Beal Ave.
!	Ann Arbor, MI 48109
!	antonv@umich.edu 
!
! Version history:
!   14 Nov 2004 begun

SUBROUTINE Tridiag (u, leftrow, centerrow, rightrow, rhsTri, N)

! incoming variables
integer, intent(in)					:: N
real*8, dimension(N), intent(in)	:: leftrow, centerrow, rightrow, rhsTri

! outsourced variables
real*8, dimension(N), intent(inout)	:: u

! subroutine entirely variables
integer								:: j
real*8								:: bet
real*8, dimension(N)				:: gam

continue ! body of program
!*******
if (0.0==centerrow(1)) then
  stop 'Error 1 in tridiag'
endif

bet=centerrow(1)
u(1)=rhsTri(1)/bet

do j=2,N		! decomposition and forward substitution
  gam(j) = rightrow(j-1)/bet
  bet=centerrow(j)-leftrow(j)*gam(j)
  if (0==bet) then
    stop 'Error 2 in tridiag'
  else
    u(j)=( rhsTri(j)-leftrow(j)*u(j-1) )/bet
  endif
enddo

do j=N-1,1,-1	! back substitution
  u(j) = u(j) - gam(j+1)*u(j+1)
enddo

END SUBROUTINE Tridiag

!----------
! ***************
! Subroutine totDistCalc
! ***************
! DESCRIPTION: given a spline fitting coefficients, numerically integrates
!   along surface to get an accurate total distance around shape
!
! INPUT: knot locations, cubic spline coeffs, # of knots along test shape and 
!	overall
!
! OUTPUT: total distance around entire border, calced along spline arclength
!
! CALLING PROGRAM: Cubic
!
! Primary author: Anton VanderWyst
!	University of Michigan
!	1320 Beal Ave.
!	Ann Arbor, MI 48109
!	antonv@umich.edu 
!
! Version history:
!   27 Nov 2004 begun

SUBROUTINE totDistCalc ( totDist1, SplinePts1, Sx1, knotsx1, knotsy1, ConeSize1, sizeknot1, M1 )

! incoming variables
integer, intent(in)							:: ConeSize1, sizeknot1, M1
real*8, dimension(ConeSize1,4), intent(in)	:: Sx1
real*8, dimension(sizeknot1), intent(in)	:: knotsx1, knotsy1

! outsourced variables
integer, intent(out)						:: SplinePts1
real*8, dimension(sizeknot1), intent(inout)	:: totDist1

! subroutine entirely variables
integer										:: i,j, panel, totPan, ConePan
real*8										:: tempXDist, tempArcDist, tempXChange, &
												tempYChange
real*8, dimension(sizeknot1)				:: xPanLen, yPanLen
real*8, dimension(sizeknot1*M1)				:: xPan, yPan, dydx, PanGaussWeight

continue
 
totPan = sizeknot1*M1
ConePan = ConeSize1*M1; tempArcDist=0.

! dePanel = floor(1.*(i-1)/M)+1
do i = 1, sizeknot1
  if (i /= sizeknot1) then
    xPanLen(i) = knotsx1(i+1)-knotsx1(i)
	yPanLen(i) = knotsy1(i+1)-knotsy1(i)
  elseif (i == sizeknot1) then
    xPanLen(i) = knotsx1(1)-knotsx1(i)
	yPanLen(i) = knotsy1(1)-knotsy1(i)
  else
    stop 'xPanLen error'
  endif

  do j=1, M1
    panel= (i-1)*M1+j

    if (panel<=ConePan) then
      call GaussQuad_data ( xPan(panel), yPan(panel), PanGaussWeight(panel), M1, &
   	    knotsx1(i), xPanLen(i), knotsy1(i), tempArcDist, j )

	  tempXDist = xPan(panel)-knotsx1(i)

      yPan(panel) = Sx1(i,1) + Sx1(i,2)*(tempXDist) + Sx1(i,3)*(tempXDist)**2 + &
  	    Sx1(i,4)*(tempXDist)**3   
    else
      call GaussQuad_data ( xPan(panel), yPan(panel), PanGaussWeight(panel), M1, &
   	    knotsx1(i), xPanLen(i), knotsy1(i), yPanLen(i), j )
    endif
  enddo
enddo

do i = 1, sizeknot1
  tempArcDist = 0.

  do j=1, M1
    panel= (i-1)*M1+j
    if (panel /= totPan) then
      tempXChange = xPan(panel+1)-xPan(panel); tempYChange = yPan(panel+1)-yPan(panel)
    else
      tempXChange = xPan(1)-xPan(panel); tempYChange = yPan(1)-yPan(panel)  
    endif
    if ( abs(tempXChange) > smallepsil ) then
      dydx(panel) = tempYChange / tempXChange
    else
      dydx(panel) = 0
    endif

    if ( xPanLen(i) /= 0 ) then
  	  tempArcDist = tempArcDist + abs( xPanLen(i) )*0.5*PanGaussWeight(panel)*( (1+dydx(panel)**2) )**0.5
	else
	  tempArcDist = tempArcDist + abs( yPanLen(i) )*0.5*PanGaussWeight(panel)
	endif
  enddo

  if ( tempArcDist /= 0) then
    if (i /= 1) then
      totDist1(i) = totDist1(i-1) + tempArcDist
    else
      totDist1(i) = tempArcDist
    endif
  else
    print *, 'tempArcDist error'
	stop
  endif
enddo

! now calculate the optimal number of total panels for the calculation. For a balance between
!   speed and accuracy, it should be large enough so there are between 0.5 and 2x the number 
!   of spline points in totDist(ConeSize1) as ConeSize1 ( i.e. 0.5*ConeSize1 < SplinePts1 < 2*ConeSize1 )
SplinePts1 = 1.5*ConeSize1*(totDist1(sizeknot1)/totDist1(ConeSize1))
!SplinePts1 = 300

  goto 999		! actual program end, just lists formats and errors below
  ! ***********

! FORMAT listings
10  FORMAT(a, i8)

999 END SUBROUTINE totDistCalc

! ***************
! FUNCTION SolveCubicEq
! ***************
! DESCRIPTION: solves for the most positive root of (x^3) + a1(x^2) + a2(x) + a3=0
!
! INPUT: a+b(delX)+c(delX^2)+d(delX^3)=y
!
! OUTPUT: x
!
! CALLING PROGRAM: SplineLengthFit
!
! Primary author: Anton VanderWyst, 
!	University of Michigan 
!
! Version history:
!   18 Jan 2005 begun

FUNCTION SolveCubicEq ( a,b,c,d,y )

! incoming variables
real*8, intent(in)	:: a,b,c,d,y

! outgoing function variable
real*8				:: SolveCubicEq

! internal variables	
real*8				:: a1,a2,a3, Q, R, S, T

continue
! change input into form (x^3) + a1(x^2) + a2(x) + a3=0

a1=1.*c/d; a2=1.*b/d; a3=1.*(a-y)/d
Q = (3.*a2-a1**2)/9.
R = (9.*a1*a2-27.*a3-2.*a1**3)/54.
S = ( R+ ( Q**3 + R**2 )**0.5   )**(1./3.)
T = ( R- ( Q**3 + R**2 )**0.5   )**(1./3.)

SolveCubicEq = S + T -a1/3.
end FUNCTION SolveCubicEq

!----------
! ***************
! Subroutine KnotDistCalc
! ***************
! DESCRIPTION: given a spline fitting coefficients, numerically integrates
!   along surface to get an accurate distance along one panel. Changes last
!	column of Coords to include fraction knot/spline
!
! INPUT: knot locations, cubic spline coeffs, # of knots along test shape
!
! OUTPUT: total distance around entire border, calced along spline arclength.
!	A single number normed as a fraction of distance from Spline endpoints
!
! CALLING PROGRAM: SplineLengthFit
!
! Primary author: Anton VanderWyst
!	University of Michigan
!	1320 Beal Ave.
!	Ann Arbor, MI 48109
!	antonv@umich.edu 
!
! Version history:
!   18 Jan 2005 begun

SUBROUTINE KnotDistCalc ( FracDist, SplinePtPre, SplinePtPost, Sx2, knotx2, knoty2, M2, PantotDist )

! incoming variables
integer, intent(in)				:: M2
real*8, intent(in)				:: knotx2, knoty2
real*8, dimension(4),intent(in)	:: Sx2
real*8, dimension(2),intent(in)	:: SplinePtPre, SplinePtPost, PantotDist

! outsourced variables
real*8, intent(out)				:: FracDist

! subroutine entirely variables
integer							:: j, panel
real*8							:: xPanLen, yPanLen, tempArcDist, tempXDist, &
									tempXChange, tempYChange, dydx
real*8, dimension(M2)			:: xPan, yPan, PanGaussWeight

continue

xPanLen = knotx2-SplinePtPre(1)
yPanLen = knoty2-SplinePtPre(2)
tempArcDist = 0.

do j=1, M2
  call GaussQuad_data ( xPan(j), yPan(j), PanGaussWeight(j), M2, &
    SplinePtPre(1), xPanLen, SplinePtPre(2), tempArcDist, j )

  tempXDist = xPan(j)-SplinePtPre(1)

  yPan(j) = Sx2(1) + Sx2(2)*(tempXDist) + Sx2(3)*(tempXDist)**2 + &
  	Sx2(4)*(tempXDist)**3   
enddo

do j=1, M2
  panel= j
  if (panel /= M2) then
    tempXChange = xPan(panel+1)-xPan(panel); tempYChange = yPan(panel+1)-yPan(panel)
  else
    tempXChange = SplinePtPost(1)-xPan(panel); tempYChange = SplinePtPost(2)-yPan(panel)  
  endif

  if ( abs(tempXChange) > smallepsil ) then
    dydx = 1.*tempYChange / tempXChange
  else
    dydx = 0
  endif

  if ( xPanLen /= 0 ) then
    tempArcDist = tempArcDist + abs( xPanLen )*0.5*PanGaussWeight(panel)*( (1+dydx**2) )**0.5
  else
    tempArcDist = tempArcDist + abs( yPanLen )*0.5*PanGaussWeight(panel)
  endif
enddo

if ( tempArcDist /= 0) then
  FracDist  = 1.*tempArcDist/(PantotDist(2)-PantotDist(1))
else
  print *, 'KnotDistCalc error'
  stop
endif

end SUBROUTINE KnotDistCalc

END MODULE CubicSpline
