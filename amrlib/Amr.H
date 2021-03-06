
#ifndef AMREX_Amr_H_
#define AMREX_Amr_H_ 

#include <fstream>
#include <memory>
#include <list>

#include <AMReX_Box.H>
#include <AMReX_Geometry.H>
#include <AMReX_BoxArray.H>
#include <AMReX_Array.H>
#include <AMReX_Vector.H>
#include <AMReX_BCRec.H>

namespace amrex {

class AmrLevel;
class LevelBld;
class BoxDomain;

//
//
// Manages hierarchy of levels for time-dependent AMR computations.
//
// The Amr class is designed to manage parts of the computation  which do
// not belong on a single level, like establishing and updating the hierarchy
// of levels, global timestepping, and managing the different AmrLevels
//

class Amr
{
public:
    //
    // The constructor.
    //
    Amr ();
    //
    // The destructor.
    //
    virtual ~Amr ();
    //
    // Init data after construction. Must be called before timestepping.
    //

    virtual void init (Real strt_time, Real stop_time);
    //
    // Set the timestep on each level.
    //
    void setDt(Real dt);
    int blockingFactor (int lev) const;
    int Space_blockingFactor (int lev) const;
    int Time_blockingFactor () const;
    int get_MAX_NUM_SLAB () const;
    int get_slab_dt_type () const;

    Vector<Real> AMR_volume_history;
    int AMR_volume_history_recorded;

    int maxGridSize (int lev) const;
    int maxLevel () const;
    int finestLevel () const;
    Real cumTime () const;
    Real getDt();

    const Vector<Geometry>& Geom () const { return geom; }
    const Vector<DistributionMapping>& DistributionMap () const { return dmap; }
    const Vector<BoxArray>& boxArray () const { return grids; }

    const Geometry& Geom (int lev) const { return geom[lev]; }
    const DistributionMapping& DistributionMap (int lev) const 
      { return dmap[lev]; }
    const BoxArray& boxArray (int lev) const { return grids[lev]; }

    Vector<Geometry>& Geom () { return geom; }
    Geometry& Geom (int lev) { return geom[lev]; }

    void SetDistributionMap (int lev, const DistributionMapping& dmap_in) noexcept;
    void SetBoxArray (int lev, const BoxArray& ba_in) noexcept;

    void ClearDistributionMap (int lev) noexcept;
    void ClearBoxArray (int lev) noexcept;

    int levelSteps (int lev) const;
    int levelCount (int lev) const;
    int regridInt (int lev) const;
    int checkInt () const;
    Real checkPer() const;
    int plotInt () const;
    int sliceInt () const;
    Real plotPer () const;

    static const std::list<std::string>& statePlotVars ();
    static bool isStatePlotVar (const std::string& name);
    static void addStatePlotVar (const std::string& name);
    static void deleteStatePlotVar (const std::string& name);
    static void clearStatePlotVarList ();
    static void fillStatePlotVarList ();
    static bool Plot_Files_Output ();

    static void Initialize ();
    void InitAmr ();
    static void Finalize ();
    //
    // Number of buffer cells in error estimator.
    //
    int nErrorBuf (int lev) const;
    //
    // Minimum allowable grid efficiency.
    //
    Real gridEff () const;
    //
    // Number of cells to define proper nesting 
    //
    int nProper () const;
    //
    // was PArray of AmrLevels.
    // OLD: PArray<AmrLevel>& getAmrLevels ();
    Vector<std::unique_ptr<AmrLevel> >& getAmrLevels () noexcept;
    //
    // More work to be done?
    //
    int okToContinue () noexcept;
    //
    // Do a complete integration cycle.
    //
    virtual void coarseTimeStep (Real stop_time);
    //
    // Name of the restart file.
    //
    const std::string& theRestartFile () const;
    //
    // The ith datalog file.  Do with it what you want.
    //
    std::ostream& DataLog (int i);
    //

    void writeDEBUG_PlotFile(int num,int SDC_outer_sweeps_in,int slab_step_in);

      // get local variable "recalesce_flag", 0, 1 static, 2 dynamic
    int AMR_recalesce_flag(int im) const;

     // initialize class member "recalesce_state_new"
    void recalesce_put_state(Vector<Real>& recalesce_state_in,int nmat); 
     // get class member "recalesce_state_old"
    void recalesce_get_state(Vector<Real>& recalesce_state_out,int nmat); 
    void recalesce_init(int nmat);

     // copies new to old.
    void recalesce_copy_new_to_old(int nmat);
     // copies old to new.
    void recalesce_copy_old_to_new(int nmat);

    void print_cells_advanced();

    AmrLevel& getLevel (int lev) noexcept { return *amr_level[lev]; }

#ifdef AMREX_PARTICLES
    //! Redistribute particles
    void RedistributeParticles ();
#endif

protected:
    //
    // Used by grid_places().
    //
    static void ProjPeriodic (BoxList&        bd,
                              const Geometry& geom);
    //
    // Write the plot file to be used for visualization.
    //
    void writePlotFile (
     const std::string& root, 
     int num,
     int do_plot,int do_slice,
     int SDC_outer_sweeps_in,int slab_step_in);
    //
    // Initialize grid hierarchy.
    //
    void initialInit (Real strt_time, Real stop_time);
    //
    // Check for valid input.
    //
    void checkInput ();
    //
    // Write current state into a chk* file.
    //
    void checkPoint ();
    //
    // Restart from a checkpoint file.
    //
    void restart (const std::string& filename);
    //
    // Define and initialize coarsest level.
    //
    void defBaseLevel (Real start_time);
    //
    // Define and initialize refined levels.
    //
    void bldFineLevels (Real start_time);
    //
    // Rebuild grid hierarchy finer than lbase.
    //
    void regrid (int  lbase,
                 Real time,
                 bool initial = false);
    //
    // Define new grid locations (called from regrid) and put into new_grids.
    //
    void grid_places (int              lbase,
                      int&             new_finest,
                      Vector<BoxArray>& new_grids);
    //
    // Do a single timestep on level L.
    //
    void timeStep (Real time,
                   Real stop_time);


    void printGridInfo (std::ostream& os,
                        int           min_lev,
                        int           max_lev);

    void printGridSummary (std::ostream& os,
                           int           min_lev,
                           int           max_lev);

    void setRecordGridInfo (const std::string&);

    void setRecordRunInfo (const std::string&);

    void setRecordRunInfoTerse (const std::string&);

    void setRecordDataInfo (int i, const std::string&);
    //
    // The data ...
    //

      // nmat x (stage,time,.....)
    Vector<Real> recalesce_state_old;
    Vector<Real> recalesce_state_new;
    
    int              max_level;    // Maximum allowed level.
    int              finest_level; // Current finest level.

    // OLD: PArray<AmrLevel> amr_level;    // Array of levels
    Vector<std::unique_ptr<AmrLevel> > amr_level;    //!< Vector of levels

    Vector<Geometry>  geom;         // Index extent of domain at all levels.
    Vector<DistributionMapping> dmap;
    Vector<BoxArray> grids;

    Real             cumtime;      // Physical time variable.
    Real             dt_AMR;     // Timestep 
    Vector<Real>      level_cells_advanced;  //not checkpointed 
    Vector<int>       level_steps;  // Number of time steps at this level.
    Vector<int>       level_count;
    bool             isPeriodic[BL_SPACEDIM];  // Domain periodic?
    Vector<int>       regrid_int;      // Interval between regridding.
    Vector<int>       n_error_buf;     // Buffer cells around each tagged cell.
    Vector<int>       blocking_factor; // Blocking factor (by level).
    Vector<int>       space_blocking_factor; 
    int              time_blocking_factor;
    int              slab_dt_type; 
    int              MAX_NUM_SLAB; 
    Vector<int>       max_grid_size;   // Maximum allowable grid size (by level).
    Real             grid_eff;        // Grid efficiency.
    int              n_proper;        // # cells required for proper nesting.
    int              last_checkpoint; // Step number of previous checkpoint.
    int              check_int;       // How often checkpoint (# time steps).
    Real             check_per;       // How often checkpoint (units of time).
    std::string      check_file_root; // Root name of checkpoint file.
    int              last_plotfile;   // Step number of previous plotfile.
    int              plot_int;        // How often plotfile (# of time steps)
    int              slice_int;       // How often plotfile (# of time steps)
    Real             plot_per;        // How often plotfile (in units of time)
    int              file_name_digits; // How many digits to use in the plotfile and checkpoint names
    std::string      plot_file_root;  // Root name of plotfile.
    int              verbose;
    int              record_grid_info;
    int              record_run_info;
    int              record_run_info_terse;
    std::ofstream    gridlog;
    std::ofstream    runlog;
    std::ofstream    runlog_terse;
    // OLD: PArray<std::ofstream> datalog;
    Vector<std::unique_ptr<std::fstream> > datalog;
    std::string      restart_file;
    LevelBld*        levelbld;
    //
    // The static data ...
    //
    static std::list<std::string> state_plot_vars;  // State Vars to dump to plotfile 
    static bool                   first_plotfile;

private:
    //
    // Disallowed
    //
    Amr (const Amr& rhs);
    Amr& operator= (const Amr& rhs);
};


inline
int
Amr::blockingFactor (int lev) const
{
    return blocking_factor[lev];
}

inline
int 
Amr::Space_blockingFactor (int lev) const   
{   
    return space_blocking_factor[lev];
}  

inline
int
Amr::maxGridSize (int lev) const
{
    return max_grid_size[lev];
}

inline
int
Amr::maxLevel () const
{
    return max_level;
}

inline
int
Amr::finestLevel () const
{
    return finest_level;
}

inline
Real
Amr::getDt() 
{
    return dt_AMR;
}

inline
int
Amr::checkInt () const
{
    return check_int;
}

inline
Real
Amr::checkPer () const
{
    return check_per;
}

inline
int
Amr::plotInt () const
{
    return plot_int;
}
inline
int
Amr::sliceInt () const
{
    return slice_int;
}

inline
Real
Amr::plotPer () const
{
    return plot_per;
}

inline
const std::list<std::string>&
Amr::statePlotVars ()
{
    return state_plot_vars;
}


inline
int
Amr::levelSteps (int i) const
{
    return level_steps[i];
}

inline
int
Amr::levelCount (int i) const
{
    return level_count[i];
}

inline
Real
Amr::cumTime () const
{
    return cumtime;
}

inline
int
Amr::regridInt (int lev) const
{
    return regrid_int[lev];
}

inline
int
Amr::nErrorBuf (int lev) const
{
    return n_error_buf[lev];
}

inline
Real
Amr::gridEff () const
{
    return grid_eff;
}

inline
int
Amr::nProper () const
{
    return n_proper;
}

inline
const std::string&
Amr::theRestartFile () const
{
    return restart_file;
}

} /* namespace amrex */

#endif /*AMREX_Amr_H_*/
