// #include <winstd.H>

#include <algorithm>
#include <vector>

#if defined(BL_OLD_STL)
#include <stdio.h>
#include <math.h>
#else
#include <cstdio>
#include <cmath>
#endif

#include <AMReX_CArena.H>
#include <AMReX_REAL.H>
#include <AMReX_Utility.H>
#include <AMReX_IntVect.H>
#include <AMReX_Box.H>
#include <AMReX_ParmParse.H>
#include <AMReX_ParallelDescriptor.H>

#include <Amr.H>

using namespace amrex;

namespace amrex{

extern void fortran_parameters();

}

int
main (int   argc,
      char* argv[])
{

    std::cout.imbue(std::locale("C"));
    amrex::Initialize(argc,argv);  // mpi initialization.
    std::fflush(NULL);
    amrex::ParallelDescriptor::Barrier();
    std::cout << 
	"Multimaterial SUPERMESH/SPECTRAL, 07/19/20, 1:25am on proc " << 
        amrex::ParallelDescriptor::MyProc() << "\n";
    std::cout << "PROC= " << amrex::ParallelDescriptor::MyProc() << 
	    " thread_class::nthreads= " << 
	    thread_class::nthreads << '\n';
    std::fflush(NULL);
    amrex::ParallelDescriptor::Barrier();
    if (amrex::ParallelDescriptor::IOProcessor())
     std::cout << "after the barrier on IO processor " << 
	    amrex::ParallelDescriptor::MyProc() << "\n";

    const double run_strt = amrex::ParallelDescriptor::second();

    int  wait_for_key=0;
    int  max_step;  // int is 4 bytes
    amrex::Real strt_time;
    amrex::Real stop_time;

    ParmParse pp;

    max_step  = -1;    
    strt_time =  0.0;  
    stop_time = -1.0;  

    double sleepsec=0.0;

    pp.query("wait_for_key",wait_for_key);
    pp.query("max_step",max_step);
    pp.query("strt_time",strt_time);
    pp.query("stop_time",stop_time);

    pp.query("sleepsec",sleepsec);

    if (strt_time < 0.0)
        amrex::Abort("MUST SPECIFY a non-negative strt_time");

    if (max_step < 0 && stop_time < 0.0)
    {
        amrex::Abort(
            "Exiting because neither max_step nor stop_time is non-negative.");
    }

     
    Amr* amrptr = new Amr();

    amrex::ParallelDescriptor::Barrier();

    fortran_parameters();

      // Amr::init 
    amrptr->init(strt_time,stop_time);

    amrex::ParallelDescriptor::Barrier();

      // if not subcycling then levelSteps(level) is independent of "level"
      // initially, cumTime()==0.0
    while ( amrptr->okToContinue()           &&
           (amrptr->levelSteps(0) < max_step || max_step < 0) &&
           (amrptr->cumTime() < stop_time || stop_time < 0.0) )
    {
        amrex::ParallelDescriptor::Barrier();
        std::fflush(NULL);
	BL_PROFILE_INITIALIZE();
        std::fflush(NULL);

         // coarseTimeStep is in amrlib/Amr.cpp
        amrptr->coarseTimeStep(stop_time); // synchronizes internally

        amrex::ParallelDescriptor::Barrier();
        std::fflush(NULL);
	BL_PROFILE_FINALIZE();
        std::fflush(NULL);
        std::cout << "TIME= " << amrptr->cumTime() << " PROC= " <<
          amrex::ParallelDescriptor::MyProc() << " sleepsec= " << sleepsec << '\n';
        std::fflush(NULL);
	amrex::USleep(sleepsec);
        amrex::ParallelDescriptor::Barrier();
    }
    amrex::ParallelDescriptor::Barrier();

    delete amrptr;

    if (CArena* arena = dynamic_cast<CArena*>(amrex::The_Arena()))
    {
        //
        // We're using a CArena -- output some FAB memory stats.
        // This'll output total # of bytes of heap space in the Arena.
        // It's actually the high water mark of heap space required by FABs.
        //
        char buf[256];

        sprintf(buf,
                "CPU(%d): Heap Space (bytes) used by Coalescing FAB Arena: %ld",
                amrex::ParallelDescriptor::MyProc(),
                arena->heap_space_used());

        std::cout << buf << '\n';
    }

    const int IOProc   = amrex::ParallelDescriptor::IOProcessorNumber();
    double    run_stop = amrex::ParallelDescriptor::second() - run_strt;

    amrex::ParallelDescriptor::ReduceRealMax(run_stop,IOProc);

    if (amrex::ParallelDescriptor::IOProcessor())
        std::cout << "Run time = " << run_stop << '\n';

    amrex::Finalize();

    return 0;

}

