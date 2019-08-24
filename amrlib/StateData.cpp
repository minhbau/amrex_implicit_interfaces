
#include <winstd.H>
#include <iostream>
#include <algorithm>

#include <RealBox.H>
#include <StateData.H>
#include <StateDescriptor.H>
#include <ParallelDescriptor.H>
#include <iostream>
#include <sstream>
#include <fstream>

#include <INTERP_F.H>

const Real INVALID_TIME = -1.0e200;

StateData::StateData () 
{
   StateData_MAX_NUM_SLAB=33;
   StateData_slab_dt_type=0;

   desc = 0;
   descGHOST = 0;

   time_array.resize(StateData_MAX_NUM_SLAB);
   new_data.resize(StateData_MAX_NUM_SLAB);

   for (int i=0;i<StateData_MAX_NUM_SLAB;i++) {
    new_data[i]=0;
    time_array[i] = INVALID_TIME;
   }
   bfact_time_order=0;
}

StateData::StateData (
  int level,
  const Box& p_domain,
  const BoxArray& grds,
  const DistributionMapping& dm,
  const StateDescriptor* d,
  const StateDescriptor* dGHOST,
  Real cur_time,
  Real dt,
  int time_order,
  int slab_dt_type, // 0=SEM 1=evenly spaced
  int MAX_NUM_SLAB)
{
    define(level,p_domain, grds, dm, *d, *dGHOST, cur_time, dt,
           time_order,slab_dt_type,MAX_NUM_SLAB);
}

void
StateData::define (
  int level,
  const Box& p_domain,
  const BoxArray& grds,
  const DistributionMapping& dm,
  const StateDescriptor& d,
  const StateDescriptor& dGHOST,
  Real time,
  Real dt,
  int time_order,
  int slab_dt_type, // 0=SEM 1=evenly spaced
  int MAX_NUM_SLAB)
{

    if (dt<=0.0) {
     std::cout << "dt = " << dt << '\n';
     BoxLib::Error("dt invalid in StateData define");
    }
    if (time<0.0) {
     std::cout << "time = " << time << '\n';
     BoxLib::Error("time invalid in StateData define");
    }
    
    StateData_MAX_NUM_SLAB=MAX_NUM_SLAB;
    if (StateData_MAX_NUM_SLAB<33)
     BoxLib::Error("StateData_MAX_NUM_SLAB too small");

    StateData_slab_dt_type=slab_dt_type;
    if ((StateData_slab_dt_type!=0)&&
        (StateData_slab_dt_type!=1))
     BoxLib::Error("StateData_slab_dt_type invalid");
 
    bfact_time_order=time_order;
    if ((bfact_time_order>StateData_MAX_NUM_SLAB)||
        (bfact_time_order<1)) {
     std::cout << "bfact_time_order= " << bfact_time_order << '\n';
     BoxLib::Error("bfact_time_order invalid in define");
    }
    time_array.resize(StateData_MAX_NUM_SLAB);
    new_data.resize(StateData_MAX_NUM_SLAB);

    domain = p_domain;
    desc = &d;
    descGHOST = &dGHOST;
    grids = grds;
    dmap = dm;
    //
    // Convert to proper type.
    //
    IndexType typ(desc->getType());
    if (!typ.cellCentered())
    {
        domain.convert(typ);
        grids.convert(typ);
    }
      //time_array[0]=time-dt
      //time_array[bfact_time_order]=time
    Real slablow=time-dt;
    Real slabhigh=time;
    int do_scale_time=0;

    if (dt>=1.0) {
     do_scale_time=1;
     slablow=time/dt-1.0;
     slabhigh=time/dt;
    } else if ((dt>0.0)&&(dt<1.0)) {
     // do nothing
    } else {
     BoxLib::Error("dt invalid");
    }

    FORT_GL_SLAB(time_array.dataPtr(),&StateData_slab_dt_type,
                 &bfact_time_order,&slablow,&slabhigh);

    if (do_scale_time==1) {
     for (int islab=0;islab<=bfact_time_order;islab++)
      time_array[islab]=time_array[islab]*dt;
    } else if (do_scale_time==0) {
     // do nothing
    } else {
     BoxLib::Error("do_scale_time invalid");
    }

    for (int i=0;i<=bfact_time_order-1;i++) {
     if (time_array[i]>time_array[i+1]) {
      std::cout << "i= " << i << " time_array " << time_array[i];
      std::cout << "i+1= " << i+1 << " time_array " << time_array[i+1];
      BoxLib::Error("time_array corruption");
     }
    }
 
    int ncomp = desc->nComp();

    for (int i=0;i<=bfact_time_order;i++)
     new_data[i] = new MultiFab(grids,ncomp,desc->nExtra(),dmap,Fab_allocate);

    buildBC();
}

void
StateData::restart (
  int time_order,
  int slab_dt_type, // 0=SEM 1=evenly spaced
  int MAX_NUM_SLAB,
  int level,
  std::istream& is,
  const Box& p_domain,
  const BoxArray&        grds,
  const DistributionMapping& dm,
  const StateDescriptor& d,
  const StateDescriptor& dGHOST,
  const std::string&     chkfile)
{
    StateData_MAX_NUM_SLAB=MAX_NUM_SLAB;
    if (StateData_MAX_NUM_SLAB<33)
     BoxLib::Error("StateData_MAX_NUM_SLAB too small");

    StateData_slab_dt_type=slab_dt_type;
    if ((StateData_slab_dt_type!=0)&&
        (StateData_slab_dt_type!=1))
     BoxLib::Error("StateData_slab_dt_type invalid");

    time_array.resize(StateData_MAX_NUM_SLAB);
    new_data.resize(StateData_MAX_NUM_SLAB);

    bfact_time_order=time_order;

    if ((bfact_time_order<1)||
        (bfact_time_order>StateData_MAX_NUM_SLAB)) {
     std::cout << "bfact_time_order= " << bfact_time_order << '\n';
     BoxLib::Error("bfact_time_order invalid in restart");
    }

    desc = &d;
    descGHOST = &dGHOST;

    domain = p_domain;
    grids = grds;
    dmap = dm;

    // Convert to proper type.
    IndexType typ(desc->getType());
    if (!typ.cellCentered()) {
        domain.convert(typ);
        grids.convert(typ);
    }

    Box domain_in;
    BoxArray grids_in;

    is >> domain_in;
    grids_in.readFrom(is);

    if (domain_in!=domain) {
     std::cout << "domain: " << domain;
     std::cout << "domain_in: " << domain_in;
     BoxLib::Error("domain_in invalid");
    }

    if (! BoxLib::match(grids_in,grids))
     BoxLib::Error("grids_in invalid");     

    for (int i=0;i<=bfact_time_order;i++) {
     is >> time_array[i];
    }

    int nsets;
    is >> nsets;
    if (nsets!=bfact_time_order)
     BoxLib::Error("all slab data should be checkpointed");

    for (int i=0;i<StateData_MAX_NUM_SLAB;i++)
     new_data[i]=0;

    std::string mf_name;
    std::string FullPathName;

    for (int i=0;i<=bfact_time_order;i++) {

     new_data[i] = new MultiFab(grids,desc->nComp(),desc->nExtra(),dmap);

       // read the file name from the header file.
     is >> mf_name;
      //
      // Note that mf_name is relative to the Header file.
      // We need to prepend the name of the chkfile directory.
      //
     FullPathName = chkfile;
     if (!chkfile.empty() && chkfile[chkfile.length()-1] != '/')
      FullPathName += '/';
     FullPathName += mf_name;
       // read from a file other than the header file.
     VisMF::Read(*new_data[i], FullPathName);

    }  // i=0 ... bfact_time_order

    buildBC();
}

void
StateData::buildBC ()
{
    int ncomp = desc->nComp();
    bc.resize(ncomp);
    for (int i = 0; i < ncomp; i++)
    {
        bc[i].resize(grids.size());
        for (int j = 0; j < grids.size(); j++)
        {
            BCRec bcr;
            BoxLib::setBC(grids[j],domain,desc->getBC(i),bcr);
            bc[i][j]=bcr;
        }
    }

    int ncompGHOST = descGHOST->nComp();
    bcGHOST.resize(ncompGHOST);
    for (int i = 0; i < ncompGHOST; i++)
    {
        bcGHOST[i].resize(grids.size());
        for (int j = 0; j < grids.size(); j++)
        {
            BCRec bcr;
            BoxLib::setBC(grids[j],domain,descGHOST->getBC(i),bcr);
            bcGHOST[i][j]=bcr;
        }
    }
}

StateData::~StateData()
{
   desc = 0;
   descGHOST = 0;
   for (int i=0;i<=bfact_time_order;i++) 
    delete new_data[i];
}

const StateDescriptor*
StateData::descriptor () const
{
    return desc;
}


const StateDescriptor*
StateData::descriptorGHOST () const
{
    return descGHOST;
}


int StateData::get_bfact_time_order() const
{
 return bfact_time_order;
}

const Box&
StateData::getDomain () const
{
    return domain;
}

Real
StateData::slabTime (int slab_index) const
{
 if ((slab_index<0)||(slab_index>bfact_time_order))
  BoxLib::Error("slab_index invalid");

 return time_array[slab_index];
}


MultiFab&
StateData::newData (int slab_index)
{
 int project_slab_index=slab_index;
 if (project_slab_index==-1)
  project_slab_index=0;
 if (project_slab_index==bfact_time_order+1)
  project_slab_index=bfact_time_order;
 if ((project_slab_index<0)||
     (project_slab_index>bfact_time_order)) {
  std::cout << "bfact_time_order= " << bfact_time_order << '\n';
  std::cout << "project_slab_index= " << project_slab_index << '\n';
  BoxLib::Error("project_slab_index invalid1");
 }
 BL_ASSERT(new_data[project_slab_index] != 0);
 return *new_data[project_slab_index];
}

const MultiFab&
StateData::newData (int slab_index) const
{
 int project_slab_index=slab_index;
 if (project_slab_index==-1)
  project_slab_index=0;
 if (project_slab_index==bfact_time_order+1)
  project_slab_index=bfact_time_order;
 if ((project_slab_index<0)||
     (project_slab_index>bfact_time_order)) {
  std::cout << "bfact_time_order= " << bfact_time_order << '\n';
  std::cout << "project_slab_index= " << project_slab_index << '\n';
  BoxLib::Error("project_slab_index invalid2");
 }
 BL_ASSERT(new_data[project_slab_index] != 0);
 return *new_data[project_slab_index];
}

Array<BCRec>&
StateData::getBCs (int comp)
{
    return bc[comp];
}

const BCRec&
StateData::getBC (int comp, int i) const
{
    return bc[comp][i];
}


Array<BCRec>&
StateData::getBCsGHOST (int comp)
{
    return bcGHOST[comp];
}

const BCRec&
StateData::getBCGHOST (int comp, int i) const
{
    return bcGHOST[comp][i];
}



bool
StateData::hasNewData (int slab_index) const
{
 if ((slab_index<0)||(slab_index>bfact_time_order))
  BoxLib::Error("slab_index invalid");
 return new_data[slab_index] != 0;
}

// dt will be modified if t_new>0 and t_new-dt<0
void
StateData::setTimeLevel (Real time,Real& dt)
{
  //time_array[0]=time-dt
  //time_array[bfact_time_order]=time

 if (time==0.0) {
  // do nothing
 } else if ((time>0.0)&&(time-dt<0.0)) {
  dt=time;
 } else if ((time>0.0)&&(time-dt>=0.0)) {
  // do nothing
 } else
  BoxLib::Error("time or dt bust");

 if (dt<1.0e-12) {
  std::cout << "dt= " << dt << '\n';
  BoxLib::Error("dt<1e-12 in setTimeLevel StateData");
 }
 if (dt<1.0e-99) {
  std::cout << "dt= " << dt << '\n';
  BoxLib::Error("dt<1e-99 in setTimeLevel StateData");
 }
 if (time<0.0)
  BoxLib::Error("time<0 in setTimeLevel StateData");

 if ((StateData_slab_dt_type!=0)&&
     (StateData_slab_dt_type!=1)) 
  BoxLib::Error("StateData_slab_dt_type invalid");

 Real slablow=time-dt;
 Real slabhigh=time;
 int do_scale_time=0;

 if (dt>=1.0) {
  do_scale_time=1;
  slablow=time/dt-1.0;
  slabhigh=time/dt;
 } else if ((dt>0.0)&&(dt<1.0)) {
  // do nothing
 } else {
  BoxLib::Error("dt invalid");
 }

 FORT_GL_SLAB(time_array.dataPtr(),&StateData_slab_dt_type,
              &bfact_time_order,&slablow,&slabhigh);

 if (do_scale_time==1) {
  for (int islab=0;islab<=bfact_time_order;islab++)
   time_array[islab]=time_array[islab]*dt;
 } else if (do_scale_time==0) {
  // do nothing
 } else {
  BoxLib::Error("do_scale_time invalid");
 }

 if (do_scale_time==1) {
  if (fabs(time_array[0]/dt-slablow)>1.0e-10)
   BoxLib::Error("time_array[0] inv in setTimeLevel StateData");
  if (fabs(time_array[bfact_time_order]/dt-slabhigh)>1.0e-10)
   BoxLib::Error("time_array[bfact_time_order] inv setTimeLevel StateData");
 } else if (do_scale_time==0) {
  if (fabs(time_array[0]-slablow)>1.0e-10*dt)
   BoxLib::Error("time_array[0] inv in setTimeLevel StateData");
  if (fabs(time_array[bfact_time_order]-slabhigh)>1.0e-10*dt)
   BoxLib::Error("time_array[bfact_time_order] inv setTimeLevel StateData");
 } else {
  BoxLib::Error("dt invalid");
 }
  
} // subroutine setTimeLevel

void
StateData::FillBoundary (
 int level,
 FArrayBox& dest,
 Real time,
 const Real* dx,
 const RealBox& prob_domain,
 int dcomp,
 Array<int> scompBC_map,
 int ncomp,
 int bfact)
{
    BL_ASSERT(dest.box().ixType() == desc->getType());

    if (domain.contains(dest.box())) return;

    const Box& bx  = dest.box();
    const int* dlo = dest.loVect();
    const int* dhi = dest.hiVect();
    const int* plo = domain.loVect();
    const int* phi = domain.hiVect();

    Array<int> bcrs;

    Real xlo[BL_SPACEDIM];
    BCRec bcr;
    const Real* problo = prob_domain.lo();

    for (int i = 0; i < BL_SPACEDIM; i++)
    {
        xlo[i] = problo[i] + dx[i]*(dlo[i]-plo[i]);
    }
    int dc_offset=0;
    for (int i = 0; i < ncomp; )
    {
        const int dc  = dcomp+dc_offset;
        const int sc  = scompBC_map[i];
        Real*     dat = dest.dataPtr(dc);

        if (desc->master(sc))
        {
            int groupsize = desc->groupsize(sc);

            BL_ASSERT(groupsize != 0);

            if (groupsize+i <= ncomp)
            {
                //
                // Can do the whole group at once.
                //
                bcrs.resize(2*BL_SPACEDIM*groupsize);
                int* bci  = bcrs.dataPtr();

                for (int j = 0; j < groupsize; j++)
                {
                    dc_offset+=1;

                    BoxLib::setBC(bx,domain,desc->getBC(sc+j),bcr);

                    const int* bc = bcr.vect();

                    for (int k = 0; k < 2*BL_SPACEDIM; k++)
                        bci[k] = bc[k];

                    bci += 2*BL_SPACEDIM;
                }
                //
                // Use the "group" boundary fill routine.
                //
                desc->bndryFill(sc)(
                  &level,dat,dlo,dhi,plo,phi,dx,xlo,
                  &time,bcrs.dataPtr(),&sc,&groupsize,&bfact,true);

                i += groupsize;
            }
            else
            {
                int single_ncomp=1;
                dc_offset+=1;

                BoxLib::setBC(bx,domain,desc->getBC(sc),bcr);
                desc->bndryFill(sc)( 
                  &level,dat,dlo,dhi,plo,phi,dx,xlo,
                  &time,bcr.vect(),&sc,&single_ncomp,&bfact);
                i++;
            }
        }
        else
        {
            int single_ncomp=1;
            dc_offset+=1;

            BoxLib::setBC(bx,domain,desc->getBC(sc),bcr);
            desc->bndryFill(sc)(
              &level,dat,dlo,dhi,plo,phi,dx,xlo,
              &time,bcr.vect(),&sc,&single_ncomp,&bfact);
            i++;
        }
    }
} //  StateData::FillBoundary 




void
StateData::FillBoundaryGHOST (
 int level,
 FArrayBox& dest,
 Real time,
 const Real* dx,
 const RealBox& prob_domain,
 int dcomp,
 Array<int> scompBC_map,
 int ncomp,
 int bfact)
{
    BL_ASSERT(dest.box().ixType() == descGHOST->getType());

    if (domain.contains(dest.box())) return;

    const Box& bx  = dest.box();
    const int* dlo = dest.loVect();
    const int* dhi = dest.hiVect();
    const int* plo = domain.loVect();
    const int* phi = domain.hiVect();

    Array<int> bcrs;

    Real xlo[BL_SPACEDIM];
    BCRec bcr;
    const Real* problo = prob_domain.lo();

    for (int i = 0; i < BL_SPACEDIM; i++)
    {
        xlo[i] = problo[i] + dx[i]*(dlo[i]-plo[i]);
    }
    int dc_offset=0;
    for (int i = 0; i < ncomp; )
    {
        const int dc  = dcomp+dc_offset;
        const int sc  = scompBC_map[i];
        Real*     dat = dest.dataPtr(dc);

        if (descGHOST->master(sc))
        {
            int groupsize = descGHOST->groupsize(sc);

            BL_ASSERT(groupsize != 0);

            if (groupsize+i <= ncomp)
            {
                //
                // Can do the whole group at once.
                //
                bcrs.resize(2*BL_SPACEDIM*groupsize);
                int* bci  = bcrs.dataPtr();

                for (int j = 0; j < groupsize; j++)
                {
                    dc_offset+=1;

                    BoxLib::setBC(bx,domain,descGHOST->getBC(sc+j),bcr);

                    const int* bc = bcr.vect();

                    for (int k = 0; k < 2*BL_SPACEDIM; k++)
                        bci[k] = bc[k];

                    bci += 2*BL_SPACEDIM;
                }
                //
                // Use the "group" boundary fill routine.
                //
                descGHOST->bndryFill(sc)(
                  &level,dat,dlo,dhi,plo,phi,dx,xlo,
                  &time,bcrs.dataPtr(),&sc,&groupsize,&bfact,true);

                i += groupsize;
            }
            else
            {
                int single_ncomp=1;
                dc_offset+=1;

                BoxLib::setBC(bx,domain,descGHOST->getBC(sc),bcr);
                descGHOST->bndryFill(sc)(
                  &level,dat,dlo,dhi,plo,phi,dx,xlo,
                  &time,bcr.vect(),&sc,&single_ncomp,&bfact);
                i++;
            }
        }
        else
        {
            int single_ncomp=1;
            dc_offset+=1;

            BoxLib::setBC(bx,domain,descGHOST->getBC(sc),bcr);
            descGHOST->bndryFill(sc)(
              &level,dat,dlo,dhi,plo,phi,dx,xlo,
              &time,bcr.vect(),&sc,&single_ncomp,&bfact);
            i++;
        }
    }
} //  StateData::FillBoundaryGHOST 


void
StateData::get_time_index(Real time,Real &nudge_time,int& best_index) {

 nudge_time=time;
 if (bfact_time_order<1)
  BoxLib::Error("bfact_time_order invalid");

 if ((time_array[0]<=0.0)&&(time_array[bfact_time_order]==0.0)) {
  best_index=bfact_time_order;
  nudge_time=0.0;
 } else if ((time_array[0]>=0.0)&&(time_array[bfact_time_order]>0.0)) {

  Real time_scale_begin=time_array[0]/time_array[bfact_time_order];
  Real time_scale=time_scale_begin;

  Real eps=(1.0-time_scale_begin)*1.0e-10;
  if (eps<=0.0) {
   std::cout << "time_scale_begin= " << time_scale_begin << '\n';
   std::cout << "time_array(0)= " << time_array[0] << '\n';
   std::cout << "time_array(bfact_time_order)= " << 
    time_array[bfact_time_order] << '\n';
   std::cout << "bfact_time_order= " << bfact_time_order << '\n';
   std::cout << "time= " << time << '\n';
   std::cout << "desc->nComp() " << desc->nComp() << '\n';

   for (int i=0;i<desc->nComp();i++)
    std::cout << "i= " << i << " name= " << desc->name(i) << '\n';

   std::cout << "desc->nExtra() " << desc->nExtra() << '\n';

   BoxLib::Error("eps invalid (a)");
  }
  time_scale=time/time_array[bfact_time_order];
  if (time_scale<time_scale_begin-eps)
   BoxLib::Error("time_scale too small");
  if (time_scale>1.0+eps)
   BoxLib::Error("time_scale too big");

  best_index=bfact_time_order;
  nudge_time=time_array[bfact_time_order];

  for (int slab_step=bfact_time_order-1;slab_step>=0;slab_step--) {
   Real time_scale_current=time_array[slab_step]/time_array[bfact_time_order];
   Real time_scale_best=time_array[best_index]/time_array[bfact_time_order];
   if (fabs(time_scale-time_scale_current)<
       fabs(time_scale-time_scale_best)) {
    best_index=slab_step;
    nudge_time=time_array[slab_step];
   }
  } // slabstep

 } else {
  std::cout << "bfact_time_order= " << bfact_time_order << '\n';
  std::cout << "time= " << time << '\n';
  for (int itime=0;itime<=bfact_time_order;itime++) {
   std::cout << "itime= " << itime << "time_array[itime]= " <<
    time_array[itime] << '\n';
  }
  BoxLib::Error("time_array bust");
 }

} // get_time_index

void
StateData::get_time_bounding_box(Real time,Real &nudge_time,
  int &start_index) {

 nudge_time=time;
 if (bfact_time_order<1)
  BoxLib::Error("bfact_time_order invalid");

 if ((time_array[0]<=0.0)&&(time_array[bfact_time_order]==0.0)) {
  start_index=bfact_time_order-1;
  nudge_time=0.0;
 } else if ((time_array[0]>=0.0)&&(time_array[bfact_time_order]>0.0)) {

  Real time_scale_begin=time_array[0]/time_array[bfact_time_order];
  Real time_scale=time_scale_begin;

  Real eps=(1.0-time_scale_begin)*1.0e-10;
  if (eps<=0.0) {
   std::cout << "time_scale_begin= " << time_scale_begin << '\n';
   std::cout << "time_array(0)= " << time_array[0] << '\n';
   std::cout << "time_array(bfact_time_order)= " << 
    time_array[bfact_time_order] << '\n';
   std::cout << "bfact_time_order= " << bfact_time_order << '\n';
   std::cout << "time= " << time << '\n';
   std::cout << "desc->nComp() " << desc->nComp() << '\n';

   for (int i=0;i<desc->nComp();i++)
    std::cout << "i= " << i << " name= " << desc->name(i) << '\n';

   std::cout << "desc->nExtra() " << desc->nExtra() << '\n';

   BoxLib::Error("eps invalid (b)");
  }

  time_scale=time/time_array[bfact_time_order];
  if (time_scale<time_scale_begin-eps)
   BoxLib::Error("time_scale too small");
  if (time_scale>1.0+eps)
   BoxLib::Error("time_scale too big");

  Real time_scale_current=time_array[1]/time_array[bfact_time_order];
  Real time_scale_current_alt=
   time_array[bfact_time_order-1]/time_array[bfact_time_order];

  if (time_scale<=time_scale_current) {
   if (time_scale<time_scale_begin)
    nudge_time=time_array[0];
   start_index=0;
  } else if (time_scale>=time_scale_current_alt) {
   if (time_scale>1.0)
    nudge_time=time_array[bfact_time_order];
   start_index=bfact_time_order-1;
  } else {
   start_index=0;
   Real time_scale_loop= 
	  time_array[start_index+1]/time_array[bfact_time_order];
   while (time_scale>time_scale_loop) {
    start_index++;
    if (start_index>=bfact_time_order)
     BoxLib::Error("start_index invalid");
    time_scale_loop= 
      time_array[start_index+1]/time_array[bfact_time_order];
   }
  }
 }

} // get_time_bounding_box

void
StateData::CopyNewToOld() {

 MultiFab & newmulti = *new_data[bfact_time_order];
 int ncomp=newmulti.nComp();
 int ngrow=newmulti.nGrow();
  
 for (int i=0;i<bfact_time_order;i++) {
  MultiFab & oldmulti = *new_data[i];
  MultiFab::Copy(oldmulti,newmulti,0,0,ncomp,ngrow);
 }

}


void
StateData::CopyOldToNew() {

 MultiFab & oldmulti = *new_data[0];
 int ncomp=oldmulti.nComp();
 int ngrow=oldmulti.nGrow();
 for (int i=1;i<=bfact_time_order;i++) {
  MultiFab & newmulti = *new_data[i];
  MultiFab::Copy(newmulti,oldmulti,0,0,ncomp,ngrow);
 }

}


// os=HeaderFile 
// how=VisMF::OneFilePerCPU
void
StateData::checkPoint (const std::string& name,
                       const std::string& fullpathname,
                       std::ostream&  os)
{

    std::string NewSuffix[StateData_MAX_NUM_SLAB];
    std::string mf_name[StateData_MAX_NUM_SLAB];
    for (int i=0;i<=bfact_time_order;i++) {
     std::stringstream slab_string_stream(std::stringstream::in |
      std::stringstream::out);
     slab_string_stream << i;
     std::string slab_string=slab_string_stream.str();
     NewSuffix[i]="_New_MF" + slab_string;
     mf_name[i]=name;
     mf_name[i]+=NewSuffix[i];
    }

    if (desc->store_in_checkpoint()) {

     for (int i=0;i<=bfact_time_order;i++) 
      if (new_data[i]==0)
       BoxLib::Error("new_data not allocated");

     if (ParallelDescriptor::IOProcessor()) {

        os << domain << '\n';

        grids.writeOn(os);

        for (int i=0;i<=bfact_time_order;i++)  {
         os << time_array[i] << '\n';
        }

         
         // output to the header file:
        os << bfact_time_order << '\n';
        for (int i=0;i<=bfact_time_order;i++) {
          os << mf_name[i] << '\n';
        }  // i

     }  // IOProcessor ?

     for (int i=0;i<=bfact_time_order;i++) {
      BL_ASSERT(new_data[i]);

      std::string mf_fullpath_new = fullpathname; 
      mf_fullpath_new += NewSuffix[i];
       // this file is not the header file
      VisMF::Write(*new_data[i],mf_fullpath_new);
     } 
   

    } else if (! desc->store_in_checkpoint()) {
     BoxLib::Error("should never reach this point");
    } else
     BoxLib::Error("store in checkpoint invalid");
}

void
StateData::printTimeInterval (std::ostream &os) const
{
    os << '['
       << time_array[0]
       << "] ["
       << time_array[bfact_time_order]
       << ']'
       << '\n';
}

StateDataPhysBCFunct::StateDataPhysBCFunct (
 StateData& sd, 
 const Geometry& geom_)
    : statedata(&sd),
      geom(geom_)
{ }

// dx,prob_domain come from variables that are local to this class.
void
StateDataPhysBCFunct::FillBoundary (
 int level,
 MultiFab& mf, 
 Real time,
 int dcomp, 
 Array<int> scompBC_map,
 int ncomp, 
 int bfact)
{
 BL_PROFILE("StateDataPhysBCFunct::FillBoundary");

 const Box&     domain      = statedata->getDomain();
 const int*     domainlo    = domain.loVect();
 const int*     domainhi    = domain.hiVect();
 const Real*    dx          = geom.CellSize();
 const RealBox& prob_domain = geom.ProbDomain();

#ifdef CRSEGRNDOMP
#ifdef _OPENMP
#pragma omp parallel
#endif
#endif
 {
  for (MFIter mfi(mf); mfi.isValid(); ++mfi) {
   FArrayBox& dest = mf[mfi];
   const Box& bx = dest.box();
           
   bool has_phys_bc = false;
   bool is_periodic = false;
   for (int i = 0; i < BL_SPACEDIM; ++i) {
    bool touch = bx.smallEnd(i) < domainlo[i] || bx.bigEnd(i) > domainhi[i];
    if (geom.isPeriodic(i)) {
     is_periodic = is_periodic || touch;
    } else {
     has_phys_bc = has_phys_bc || touch;
    }
   } // i
           
   if (has_phys_bc) {
    statedata->FillBoundary(
     level,
     dest, 
     time, 
     dx, 
     prob_domain, 
     dcomp, 
     scompBC_map, 
     ncomp,
     bfact);
       	
    if (is_periodic) { // fix corner
     Box GrownDomain = domain;
     for (int dir = 0; dir < BL_SPACEDIM; dir++) {
      if (!geom.isPeriodic(dir)) {
       const int lo = domainlo[dir] - bx.smallEnd(dir);
       const int hi = bx.bigEnd(dir) - domainhi[dir];
       if (lo > 0) GrownDomain.growLo(dir,lo);
       if (hi > 0) GrownDomain.growHi(dir,hi);
      }
     }
        	    
     for (int dir = 0; dir < BL_SPACEDIM; dir++) {
      if (!geom.isPeriodic(dir)) continue;
        		
      Box lo_slab = bx;
      Box hi_slab = bx;
      lo_slab.shift(dir, domain.length(dir));
      hi_slab.shift(dir,-domain.length(dir));
      lo_slab &= GrownDomain;
      hi_slab &= GrownDomain;
      if (lo_slab.ok()) {
       lo_slab.shift(dir,-domain.length(dir));
       FArrayBox tmp;
       tmp.resize(lo_slab,ncomp);
       tmp.copy(dest,dcomp,0,ncomp);
       tmp.shift(dir,domain.length(dir));

       int dcomp_tmp=0;

       statedata->FillBoundary(
        level,
        tmp, 
        time, 
        dx, 
        prob_domain, 
        dcomp_tmp, 
        scompBC_map, 
        ncomp,
        bfact);
        		    
       tmp.shift(dir,-domain.length(dir));
       dest.copy(tmp,0,dcomp,ncomp);
      } // lo_slab
      if (hi_slab.ok()) {
       hi_slab.shift(dir,domain.length(dir));
       FArrayBox tmp;
       tmp.resize(hi_slab,ncomp);
       tmp.copy(dest,dcomp,0,ncomp);
       tmp.shift(dir,-domain.length(dir));

       int dcomp_tmp=0;

       statedata->FillBoundary(
        level,
        tmp, 
        time, 
        dx, 
        prob_domain, 
        dcomp_tmp, 
        scompBC_map, 
        ncomp,
        bfact);
        		    
       tmp.shift(dir,domain.length(dir));
       dest.copy(tmp,0,dcomp,ncomp);
      } // hi_slab
     } // dir
    } // periodic?
   } // has_phys_bc?
  } // mfi
 } // omp

} // StateDataPhysBCFunct::FillBoundary


StateDataPhysBCFunctGHOST::StateDataPhysBCFunctGHOST (
 StateData& sd, 
 const Geometry& geom_)
    : statedata(&sd),
      geom(geom_)
{ }

// dx,prob_domain come from variables that are local to this class.
void
StateDataPhysBCFunctGHOST::FillBoundary (
 int level,
 MultiFab& mf, 
 Real time,
 int dcomp, 
 Array<int> scompBC_map,
 int ncomp, 
 int bfact)
{
 BL_PROFILE("StateDataPhysBCFunctGHOST::FillBoundary");

 const Box&     domain      = statedata->getDomain();
 const int*     domainlo    = domain.loVect();
 const int*     domainhi    = domain.hiVect();
 const Real*    dx          = geom.CellSize();
 const RealBox& prob_domain = geom.ProbDomain();

#ifdef CRSEGRNDOMP
#ifdef _OPENMP
#pragma omp parallel
#endif
#endif
 {
  for (MFIter mfi(mf); mfi.isValid(); ++mfi) {
   FArrayBox& dest = mf[mfi];
   const Box& bx = dest.box();
           
   bool has_phys_bc = false;
   bool is_periodic = false;
   for (int i = 0; i < BL_SPACEDIM; ++i) {
    bool touch=((bx.smallEnd(i)<domainlo[i])||(bx.bigEnd(i)>domainhi[i]));
    if (geom.isPeriodic(i)) {
     is_periodic = is_periodic || touch;
    } else {
     has_phys_bc = has_phys_bc || touch;
    }
   } // i
           
   if (has_phys_bc) {

    statedata->FillBoundaryGHOST(
     level,
     dest, 
     time, 
     dx, 
     prob_domain, 
     dcomp, 
     scompBC_map, 
     ncomp,
     bfact);
       	
    if (is_periodic) { // fix corner
     Box GrownDomain = domain;
     for (int dir = 0; dir < BL_SPACEDIM; dir++) {
      if (!geom.isPeriodic(dir)) {
       const int lo = domainlo[dir] - bx.smallEnd(dir);
       const int hi = bx.bigEnd(dir) - domainhi[dir];
       if (lo > 0) GrownDomain.growLo(dir,lo);
       if (hi > 0) GrownDomain.growHi(dir,hi);
      }
     } // dir
        	    
     for (int dir = 0; dir < BL_SPACEDIM; dir++) {
      if (!geom.isPeriodic(dir)) continue;
        		
      Box lo_slab = bx;
      Box hi_slab = bx;
      lo_slab.shift(dir, domain.length(dir));
      hi_slab.shift(dir,-domain.length(dir));
      lo_slab &= GrownDomain;
      hi_slab &= GrownDomain;
      if (lo_slab.ok()) {
         // lo_slab is in the GrownDomain.
       lo_slab.shift(dir,-domain.length(dir));
       FArrayBox tmp;
       tmp.resize(lo_slab,ncomp);
       tmp.copy(dest,dcomp,0,ncomp);
        // tmp.Box() is outside the Growndomain.
       tmp.shift(dir,domain.length(dir));

       if (1==0) {
        std::cout << "dir= " << dir << '\n';
        std::cout << "ncomp= " << ncomp << '\n';
        std::cout << "dcomp= " << dcomp << '\n';
        std::cout << "GrownDomain= " << GrownDomain << '\n';
        std::cout << "tmp.box() " << tmp.box() << '\n';
       }

       int dcomp_tmp=0;

       statedata->FillBoundaryGHOST(
        level,
        tmp, 
        time, 
        dx, 
        prob_domain, 
        dcomp_tmp, 
        scompBC_map,
        ncomp,
        bfact);
        		   
        // tmp.Box() is inside the Growndomain 
       tmp.shift(dir,-domain.length(dir));
       dest.copy(tmp,0,dcomp,ncomp);
      } // lo_slab
      if (hi_slab.ok()) {
       hi_slab.shift(dir,domain.length(dir));
       FArrayBox tmp;
       tmp.resize(hi_slab,ncomp);
       tmp.copy(dest,dcomp,0,ncomp);
       tmp.shift(dir,-domain.length(dir));

       int dcomp_tmp=0;

       statedata->FillBoundaryGHOST(
        level,
        tmp, 
        time, 
        dx, 
        prob_domain, 
        dcomp_tmp, 
        scompBC_map, 
        ncomp,
        bfact);
        		    
       tmp.shift(dir,domain.length(dir));
       dest.copy(tmp,0,dcomp,ncomp);
      } // hi_slab
     } // dir
    } // periodic?
   } // has_phys_bc?
  } // mfi
 } // omp

} // StateDataPhysBCFunctGHOST::FillBoundary

