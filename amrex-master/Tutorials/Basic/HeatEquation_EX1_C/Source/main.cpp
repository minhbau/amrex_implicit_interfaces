
#include <AMReX_PlotFileUtil.H>
#include <AMReX_ParmParse.H>
#include <AMReX_Print.H>

#include "myfunc.H"
#include "myfunc_F.H"

using namespace amrex;

int main (int argc, char* argv[])
{
    amrex::Initialize(argc,argv);
    
    main_main();
    
    amrex::Finalize();
    return 0;
}

void main_main ()
{
    // What time is it now?  We'll use this to compute total run time.
    Real strt_time = amrex::second();

    // AMREX_SPACEDIM: number of dimensions
    int n_cell, max_grid_size, nsteps, plot_int;
     
    // DEFAULT: ALL PERIODIC 
    // is_periodic(dir)=1 in all direction by default (dir=0,1,.., sdim-1)
    Vector<int> is_periodic(AMREX_SPACEDIM,1);  

    int dirichlet_condition=1;
    int neumann_condition=2;
    int periodic_condition=3;

    Vector<Real> a_vector(AMREX_SPACEDIM);
    a_vector[0]=2.0;
    a_vector[1]=1.0;

    // u_{t} - divergence F(u) =0
    // flux_type==0 => heat equation =>
    //   F(u)=grad u
    // flux_type==1 => linear advection equation =>
    //   F(u)=-a_vector u
    // flux_type==2 => inviscid Burger's equation
    //   F(u)=-a_vector u^{2}/2     
    int flux_type=0;

    // DEFAULT: ALL PERIODIC 
    // bc_vector(dir)=periodic_condition
    Vector<int> bc_vector(AMREX_SPACEDIM*2,periodic_condition);
    // default is Real=double  (also known as REAL*8 in fortran)
    Vector<Real> bc_value(AMREX_SPACEDIM*2,0.0); // default homogeneous

    // inputs parameters
    {
        // ParmParse is way of reading inputs from the inputs file
        ParmParse pp;

        // We need to get n_cell from the inputs file - this is the number of cells on each side of 
        //   a square (or cubic) domain.
        pp.get("n_cell",n_cell);

        // The domain is broken into boxes of size max_grid_size
        pp.get("max_grid_size",max_grid_size);

        // Default plot_int to -1, allow us to set it to something else in the inputs file
        //  If plot_int < 0 then no plot files will be writtenq
        plot_int = -1;
        pp.query("plot_int",plot_int);

        // Default nsteps to 10, allow us to set it to something else in the inputs file
        nsteps = 10;
        pp.query("nsteps",nsteps);

        pp.queryarr("is_periodic", is_periodic);
    }

    int probtype=0; // all periodic conditions (heat equation)
    probtype=1;  // all dirichlet conditions (heat equation)
    probtype=2;  // linear advection in the x direction u_t + u_x =0
    probtype=3;  // inviscid Burger's equation in periodic domain

    if (probtype==0) {

      is_periodic[0]=1;
      is_periodic[1]=1;
      
      for (int dir=0;dir<AMREX_SPACEDIM;dir++) {
	for (int side=0;side<=1;side++) {
	  int index=2*dir+side;
          bc_vector[index]=periodic_condition;
	}
       }
      flux_type=0;
      a_vector[0]=1.0;
      a_vector[1]=1.0;

    } else if (probtype==1) {

      is_periodic[0]=0;
      is_periodic[1]=0;

      for (int dir=0;dir<AMREX_SPACEDIM;dir++) {
	for (int side=0;side<=1;side++) {
	  int index=2*dir+side;
          bc_vector[index]=dirichlet_condition;
	}
       }
        // For Neumann boundary condition: grad u dot n = g where n
        // is the outward facing normal.  i.e. if dir=0, side=0 (xlo), then
        // n=(-1 0)^T 
        // xlo
       int dir=0;
       int side=0;
       int index=2*dir+side;
       bc_vector[index]=neumann_condition;
       bc_value[index]=0.0;
        // xhi
       dir=0;
       side=1;
       index=2*dir+side;
       bc_vector[index]=neumann_condition;
       bc_value[index]=1.0;
        // ylo
       dir=1;
       side=0;
       index=2*dir+side;
       bc_value[index]=0.0;
        // yhi
       dir=1;
       side=1;
       index=2*dir+side;
       bc_value[index]=1.0;

       flux_type=0;
       a_vector[0]=1.0;
       a_vector[1]=1.0;

    } else if (probtype==2) {

        // periodic boundary conditions (default) ylo and yhi
       is_periodic[0]=0;
       is_periodic[1]=1;

       int dir=0;  //xlo
       int side=0;
       int index=2*dir+side;
       bc_vector[index]=dirichlet_condition;
       bc_value[index]=2.0;
        // xhi
       dir=0;
       side=1;
       index=2*dir+side;
       bc_vector[index]=neumann_condition;
       bc_value[index]=0.0;
   
       flux_type=1;
       a_vector[0]=1.0;
       a_vector[1]=0.0;

    } else if (probtype==3) {
    
      is_periodic[0]=1;
      is_periodic[1]=1;
 
      for (int dir=0;dir<AMREX_SPACEDIM;dir++) {
	for (int side=0;side<=1;side++) {
	  int index=2*dir+side;
          bc_vector[index]=periodic_condition;
	}
       }
      flux_type=2;
      a_vector[0]=1.0;
      a_vector[1]=0.0;
    
    } else
      amrex::Error("probtype invalid");

    // make BoxArray and Geometry
    BoxArray ba;
    Geometry geom;
    {
        IntVect dom_lo(AMREX_D_DECL(       0,        0,        0));
        IntVect dom_hi(AMREX_D_DECL(n_cell-1, n_cell-1, n_cell-1));
        Box domain(dom_lo, dom_hi);

        // Initialize the boxarray "ba" from the single box "bx"
        ba.define(domain);
        // Break up boxarray "ba" into chunks no larger than "max_grid_size" along a direction
        ba.maxSize(max_grid_size);

       // This defines the physical box, [-1,1] in each direction.
        RealBox real_box({AMREX_D_DECL(-1.0,-1.0,-1.0)},
                         {AMREX_D_DECL( 1.0, 1.0, 1.0)});

        // This defines a Geometry object
        geom.define(domain,&real_box,CoordSys::cartesian,is_periodic.data());
    }

    // Nghost = number of ghost cells for each array 
    int Nghost = 1;
    
    // Ncomp = number of components for each array
    int Ncomp  = 1;
  
    // How Boxes are distrubuted among MPI processes
    DistributionMapping dm(ba);

    // we allocate two phi multifabs; one will store the old state, the other the new.
    MultiFab phi_old(ba, dm, Ncomp, Nghost);
    MultiFab phi_new(ba, dm, Ncomp, Nghost);

    // Initialize phi_new by calling a Fortran routine.
    // MFIter = MultiFab Iterator
    for ( MFIter mfi(phi_new); mfi.isValid(); ++mfi )
    {
        const Box& bx = mfi.validbox();

        init_phi(BL_TO_FORTRAN_BOX(bx),
                 BL_TO_FORTRAN_ANYD(phi_new[mfi]),           
                 geom.CellSize(), geom.ProbLo(), geom.ProbHi(),
                 &probtype);
    }
    ParallelDescriptor::Barrier();    

    // compute the time step
    const Real* dx = geom.CellSize();
    Real dt=1.0e+20;
    for ( MFIter mfi(phi_new); mfi.isValid(); ++mfi )
    {
      const Box& bx = mfi.validbox();

      compute_dt(BL_TO_FORTRAN_BOX(bx),
                 BL_TO_FORTRAN_ANYD(phi_new[mfi]),
                 a_vector.dataPtr(),&flux_type,&dt,
                 geom.CellSize(), geom.ProbLo(), geom.ProbHi());
    } 
    ParallelDescriptor::Barrier();
    ParallelDescriptor::ReduceRealMax(dt);

    // time = starting time in the simulation
    Real time = 0.0;

    // Write a plotfile of the initial data if plot_int > 0 (plot_int was defined in the inputs file)
    if (plot_int > 0)
    {
        int n = 0;
        const std::string& pltfile = amrex::Concatenate("plt",n,5);
        WriteSingleLevelPlotfile(pltfile, phi_new, {"phi"}, geom, time, 0);
    }

    // build the flux multifabs
    Array<MultiFab, AMREX_SPACEDIM> flux;
    for (int dir = 0; dir < AMREX_SPACEDIM; dir++)
    {
        // flux(dir) has one component, zero ghost cells, and is nodal in direction dir
        BoxArray edge_ba = ba;
        edge_ba.surroundingNodes(dir);
        flux[dir].define(edge_ba, dm, 1, 0);
    }

    for (int n = 1; n <= nsteps; ++n)
    {
        MultiFab::Copy(phi_old, phi_new, 0, 0, 1, 0);

        // new_phi = old_phi + dt * (something)
        advance(phi_old, phi_new, flux, dt, geom,
		a_vector,flux_type,
		bc_vector,bc_value,dirichlet_condition,
                neumann_condition,periodic_condition,time,probtype); 
        time = time + dt;
        
        // Tell the I/O Processor to write out which step we're doing
        amrex::Print() << "Advanced step " << n << "\n";

        // Write a plotfile of the current data (plot_int was defined in the inputs file)
        if (plot_int > 0 && n%plot_int == 0)
        {
            const std::string& pltfile = amrex::Concatenate("plt",n,5);
            WriteSingleLevelPlotfile(pltfile, phi_new, {"phi"}, geom, time, n);
        }

        // compute the time step
        dt=1.0e+20;
        for ( MFIter mfi(phi_new); mfi.isValid(); ++mfi )
        {
          const Box& bx = mfi.validbox();

          compute_dt(BL_TO_FORTRAN_BOX(bx),
                 BL_TO_FORTRAN_ANYD(phi_new[mfi]),
		 a_vector.dataPtr(),&flux_type,
                 &dt,
                 geom.CellSize(), geom.ProbLo(), geom.ProbHi());
        } 
        ParallelDescriptor::Barrier();
        ParallelDescriptor::ReduceRealMax(dt);

    }  // n=1 .. nsteps

    // Call the timer again and compute the maximum difference between the start time and stop time
    //   over all processors
    Real stop_time = amrex::second() - strt_time;
    const int IOProc = ParallelDescriptor::IOProcessorNumber();
    ParallelDescriptor::ReduceRealMax(stop_time,IOProc);

    // Tell the I/O Processor to write out the "run time"
    amrex::Print() << "Run time = " << stop_time << std::endl;
}
