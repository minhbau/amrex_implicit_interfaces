#include <winstd.H>

#include <algorithm>
#include <vector>

#if defined(BL_OLD_STL)
#include <stdio.h>
#include <math.h>
#else
#include <cstdio>
#include <cmath>
#endif

#include <string>
#include <stdlib.h>

#include <CoordSys.H>
#include <Geometry.H>
#include <BoxDomain.H>
#include <ParmParse.H>
#include <FArrayBox.H>
#include <NavierStokes.H>
#include <ArrayLim.H>
#include <Utility.H>
#include <INTERP_F.H>
#include <MACOPERATOR_F.H>
#include <NAVIERSTOKES_F.H>
#include <GODUNOV_F.H>
#include <MASS_TRANSFER_F.H>
#include <PROB_F.H>
#include <MOF_F.H>
#include <TagBox.H>
#include <LEVEL_F.H>
#include <MOF_REDIST_F.H>
#include <ShallowWater_F.H>
#include <SOLIDFLUID_F.H>
#include <COORDSYS_F.H>
#include <DERIVE_F.H>
#include <LO_BCTYPES.H>

#ifdef MVAHABFSI
#include <CTMLFSI_F.H>
#endif

#define bogus_value 1.e20
#define Pi      3.14159265358979
#define show_norm2_flag 0

#define DEFAULT_MOFITERMAX 15
//
// Static objects.
//
BCRec NavierStokes::phys_bc;
BCRec NavierStokes::temperature_phys_bc;
BCRec NavierStokes::species_phys_bc;

int  NavierStokes::profile_debug=0;
bool NavierStokes::ns_tiling=false;

int NavierStokes::POLYGON_LIST_MAX=1000;

int  NavierStokes::nfluxSEM=0;
int  NavierStokes::nstate_SDC=0;
int  NavierStokes::ns_time_order=1; // time_blocking_factor
int  NavierStokes::slab_step=0;
int  NavierStokes::SDC_outer_sweeps=0;
int  NavierStokes::divu_outer_sweeps=0;
int  NavierStokes::num_divu_outer_sweeps=1;
Real NavierStokes::prev_time_slab=0.0;
Real NavierStokes::cur_time_slab=0.0;
Real NavierStokes::vel_time_slab=0.0;
Real NavierStokes::advect_time_slab=0.0;
Real NavierStokes::upper_slab_time=0.0;
Real NavierStokes::lower_slab_time=0.0;
Real NavierStokes::delta_slab_time=0.0;
Real NavierStokes::prescribed_vel_time_slab=0.0;
Real NavierStokes::dt_slab=1.0;
int NavierStokes::advect_iter=0;

// probably the density ratio will never exceed 1.0e+5
Real NavierStokes::NS_diag_regularization=1.0e-6;

int  NavierStokes::show_mem = 0;
int  NavierStokes::show_timings = 1;
int  NavierStokes::verbose      = 0;
int  NavierStokes::check_nan    = 0;
// 1=curv 2=error heat 3=both
int  NavierStokes::fab_verbose  = 0;
int  NavierStokes::output_drop_distribution = 0;
int  NavierStokes::extend_pressure_into_solid = 0;
Real NavierStokes::cfl          = 0.5;
int  NavierStokes::MOF_TURN_OFF_LS=0;
int  NavierStokes::MOF_DEBUG_RECON=0;
int  NavierStokes::MOFITERMAX=DEFAULT_MOFITERMAX;

/*
 continuous_mof=0

  regular MOF  minimize E=||x_ij^ref-x_ij^derived||
  subject to the constraint that F_ij^ref=F_ij^derived

   x_ij^ref=reference centroid in cell ij
   x_ij^derived=derived centroid in cell ij for a given slope and
     intercept.
   F_ij^ref=reference volume fraction in cell ij
   F_ij^derived=derived volume fraction in cell ij for a given slope and
     intercept.   


continuous_mof=2 (if same number of materials in center cell as in stencil)

  CMOF  minimize E=||xS_ij^ref-xS_ij^derived||  "S"=super cell
  subject to the constraint that F_ij^ref=F_ij^derived

   xS_ij^ref=reference centroid in cell stencil i'=i-1,..,i+1,
     j'=j-1,..,j+1

   xS_ij^derived=derived centroid in cell stencil for a given slope and
     intercept. 
   F_ij^ref=reference volume fraction in cell
   F_ij^derived=derived volume fraction in cell for a given
     slope and intercept.

continuous_mof=3 

  use CLSVOF in 2 material cells and MOF in >2 mat cells.

continuous_mof=4 

  use CLSVOF in 2 material cells and CMOF in >2 mat cells.

continuous_mof=5 

  use CLSVOF everywhere.


NOTE: rigid materials are not counted as materials in a cell.  Rigid 
materials are immersed into the fluid(s). 

future work:
use machine learning techniques in order to quickly derive an initial
guess for the MOF reconstruction.  Also, getting the intercept given
the volume fraction and slope.

calibrate sub-scale model parameters for any given problem, so that 
certain basic benchmark tests give the expected results. (e.g. drag
on an interface, growth rate of perturbations, pressure drop)
*/

int  NavierStokes::continuous_mof=0;
// 0  low order space and time
// 1  SEM space and time
// 2  SEM space 
// 3  SEM time
int  NavierStokes::enable_spectral=0;
int  NavierStokes::viscous_enable_spectral=0;
int  NavierStokes::projection_enable_spectral=0;
int  NavierStokes::SEM_upwind=1;
//0=div(uS)-S div(u)    1=u dot grad S
int  NavierStokes::SEM_advection_algorithm=0;
Array<int> NavierStokes::truncate_volume_fractions; // def=1  
Real NavierStokes::truncate_thickness=2.0;  
Real NavierStokes::init_shrink  = 1.0;
Real NavierStokes::change_max   = 1.1;
Real NavierStokes::change_max_init = 1.1;
Real NavierStokes::fixed_dt     = 0.0;
Real NavierStokes::fixed_dt_init = 0.0;
Real NavierStokes::min_velocity_for_dt = 1.0e-12;
Real NavierStokes::fixed_dt_velocity = 0.0;
Real NavierStokes::dt_max       = 1.0e+10;
Real NavierStokes::MUSHY_THICK  = 2.0;
Real NavierStokes::gravity      = 0.0;
// terminal_velocity_dt==1 =>
// use the terminal velocity for CFL condition instead 
// of the default condition: u dt < dx  u=g dt  g dt^2 < dx   dt<sqrt(dx/g)
int NavierStokes::terminal_velocity_dt = 0;
int NavierStokes::gravity_dir = BL_SPACEDIM;
int NavierStokes::invert_gravity = 0;
int  NavierStokes::sum_interval = -1;
int  NavierStokes::NUM_SCALARS  = 0;

// these vars used to be in probin
Real NavierStokes::denfact=1.0;
Real NavierStokes::velfact=0.0;

Real NavierStokes::xblob=0.0;
Real NavierStokes::yblob=0.0;
Real NavierStokes::zblob=0.0;
Real NavierStokes::radblob=1.0;

Real NavierStokes::xblob2=0.0;
Real NavierStokes::yblob2=0.0;
Real NavierStokes::zblob2=0.0;
Real NavierStokes::radblob2=0.0;

Real NavierStokes::xblob3=0.0;
Real NavierStokes::yblob3=0.0;
Real NavierStokes::zblob3=0.0;
Real NavierStokes::radblob3=0.0;

Real NavierStokes::xblob4=0.0;
Real NavierStokes::yblob4=0.0;
Real NavierStokes::zblob4=0.0;
Real NavierStokes::radblob4=0.0;

Real NavierStokes::xblob5=0.0;
Real NavierStokes::yblob5=0.0;
Real NavierStokes::zblob5=0.0;
Real NavierStokes::radblob5=0.0;

Real NavierStokes::xblob6=0.0;
Real NavierStokes::yblob6=0.0;
Real NavierStokes::zblob6=0.0;
Real NavierStokes::radblob6=0.0;

Real NavierStokes::xblob7=0.0;
Real NavierStokes::yblob7=0.0;
Real NavierStokes::zblob7=0.0;
Real NavierStokes::radblob7=0.0;

Real NavierStokes::xblob8=0.0;
Real NavierStokes::yblob8=0.0;
Real NavierStokes::zblob8=0.0;
Real NavierStokes::radblob8=0.0;

Real NavierStokes::xblob9=0.0;
Real NavierStokes::yblob9=0.0;
Real NavierStokes::zblob9=0.0;
Real NavierStokes::radblob9=0.0;

Real NavierStokes::xblob10=0.0;
Real NavierStokes::yblob10=0.0;
Real NavierStokes::zblob10=0.0;
Real NavierStokes::radblob10=0.0;

// force tag=0 outside this specified box if ractivex>0
Real NavierStokes::xactive=0.0;
Real NavierStokes::yactive=0.0;
Real NavierStokes::zactive=0.0;
Real NavierStokes::ractive=0.0;
Real NavierStokes::ractivex=0.0;
Real NavierStokes::ractivey=0.0;
Real NavierStokes::ractivez=0.0;

int  NavierStokes::probtype=0;
int  NavierStokes::adapt_quad_depth=1;

int  NavierStokes::visual_tessellate_vfrac=0;   
int  NavierStokes::visual_revolve=0;   
int  NavierStokes::visual_option=-2;  // -2 zonal tecplot,-1 plot files (visit)

int NavierStokes::visual_compare=0; 
Array<int> NavierStokes::visual_ncell;

// 0..sdim-1
int NavierStokes::slice_dir=0;
Array<Real> NavierStokes::xslice;

Array< Array<Real> > NavierStokes::min_face_wt;
Array< Array<Real> > NavierStokes::max_face_wt;

Array< Array<Real> > NavierStokes::DVOF;
Array< Array<Real> > NavierStokes::delta_mass;

Real NavierStokes::max_problen=0.0;
Array< Array<Real> > NavierStokes::minLS;
Array< Array<Real> > NavierStokes::maxLS;

Array<int> NavierStokes::map_forward_direct_split;
Array<int> NavierStokes::normdir_direct_split;
int NavierStokes::dir_absolute_direct_split=0;
int NavierStokes::order_direct_split=0; // base_step mod 2

Array<Real> NavierStokes::mdotplus;
Array<Real> NavierStokes::mdotminus;
Array<Real> NavierStokes::mdotcount;
Array<Real> NavierStokes::mdot_sum;
Array<Real> NavierStokes::mdot_sum2;
Array<Real> NavierStokes::mdot_lost;

Array<Real> NavierStokes::curv_min;
Array<Real> NavierStokes::curv_max;


// force AMR tagging within these specified boxes.
int NavierStokes::nblocks=0;
Array<Real> NavierStokes::xblocks;
Array<Real> NavierStokes::yblocks;
Array<Real> NavierStokes::zblocks;
Array<Real> NavierStokes::rxblocks;
Array<Real> NavierStokes::ryblocks;
Array<Real> NavierStokes::rzblocks;

int NavierStokes::tecplot_max_level=0;
int NavierStokes::max_level_two_materials=0;

// 0=> never adapt  -1=> always adapt
// otherwise, if radius<radius_cutoff * dx then adapt.
Array<int> NavierStokes::radius_cutoff;

// tagflag forced to 0 OUTSIDE these specified boxes.
int NavierStokes::ncoarseblocks=0;
Array<Real> NavierStokes::xcoarseblocks;
Array<Real> NavierStokes::ycoarseblocks;
Array<Real> NavierStokes::zcoarseblocks;
Array<Real> NavierStokes::rxcoarseblocks;
Array<Real> NavierStokes::rycoarseblocks;
Array<Real> NavierStokes::rzcoarseblocks;

int  NavierStokes::override_bc_to_homogeneous=0;

int  NavierStokes::num_species_var=0;

// search num_materials,BL_SPACEDIM+1,SDIM+1,idenbase,ipres_base,
//  iden_base,scomp_mofvars,nstate=,scomp_den,pressure_comp,dcomp,
//  pcomp,tcomp,scomp,scomp_pres,get_mm_scomp_solver,dencomp,scomp_tensor,
//  im_pres,velcomp,prescomp,flagcomp
int  NavierStokes::num_materials=0;
int  NavierStokes::num_materials_vel=1;
int  NavierStokes::num_materials_scalar_solve=1;

// set using elastic_viscosity
int  NavierStokes::num_materials_viscoelastic=0;

int  NavierStokes::num_state_material=SpeciesVar; // den,T
int  NavierStokes::num_state_base=SpeciesVar; // den,T
int  NavierStokes::ngeom_raw=BL_SPACEDIM+1;
int  NavierStokes::ngeom_recon=NUM_MOF_VAR;

int  NavierStokes::State_Type=0;
int  NavierStokes::Umac_Type=1;
int  NavierStokes::Vmac_Type=2;
int  NavierStokes::Wmac_Type=BL_SPACEDIM;
int  NavierStokes::LS_Type=BL_SPACEDIM+1;
int  NavierStokes::DIV_Type=BL_SPACEDIM+2;
int  NavierStokes::Solid_State_Type=BL_SPACEDIM+3;
int  NavierStokes::Tensor_Type=BL_SPACEDIM+4;
int  NavierStokes::NUM_STATE_TYPE=BL_SPACEDIM+5;

// 0=velocity stored at cells  
// 1=velocity stored at faces
int  NavierStokes::face_flag=0;
Array<Real> NavierStokes::elastic_time; // def=0

Array<int> NavierStokes::viscosity_state_model; // def=0
// 0,1 => viscoelastic FENE-CR material  2=> elastic material
Array<int> NavierStokes::viscoelastic_model; // def=0
Array<int> NavierStokes::les_model; // def=0
// E=u dot u/2 + e
// e=cv T
// 0=> conservative advection of temperature:
//    (rho E)_t + div(rho u E + up)=div(k grad T) 
//    it is assumed the fluid is inviscid and 
//    compressible for this option.
// 1=> non-conservative advection of temperature:
//    (rho e)_t + div(rho u e)+div(u)p=div(k grad T)
// if u is changed, then T must be changed if energy is to be conserved:
//  u_new^2/2 + cv T_new = u_old^2/2 + cv T_old
//  Tnew=Told+(1/cv)(u_old^2/2 - u_new^2/2)
Array<int> NavierStokes::temperature_primitive_variable; 

Array<Real> NavierStokes::elastic_viscosity; // def=0

Array<Real> NavierStokes::Carreau_alpha; // def=1
Array<Real> NavierStokes::Carreau_beta; // def=0
Array<Real> NavierStokes::Carreau_n; // def=1
Array<Real> NavierStokes::Carreau_mu_inf; // def=0

Array<Real> NavierStokes::concentration; // def=0
Array<Real> NavierStokes::etaL; // def=0 (etaL0)
Array<Real> NavierStokes::etaS; // def=0
Array<Real> NavierStokes::etaP; // def=0 (etaP0)
Array<Real> NavierStokes::polymer_factor; // def=0

 // 0 - centroid furthest from uncaptured centroid
 // 1 - use MOF error
int  NavierStokes::mof_error_ordering=0;
Array<int> NavierStokes::mof_ordering; // def=0

// adv_dir=1,..,sdim+1
int  NavierStokes::adv_dir=0;
Real NavierStokes::adv_vel=1.0;
int  NavierStokes::axis_dir=0;
Real NavierStokes::rgasinlet=0.0;
Real NavierStokes::slipcoeff=0.0;
Real NavierStokes::vinletgas=0.0;
Real NavierStokes::twall=0.1;
// if make_interface_incomp==1:
// density equation is Drho/Dt=0 in the incompressible zone.
// if make_interface_incomp==2:
// density equation is rhot_t+div(rho u)=0 in the incompressible zone.
int  NavierStokes::make_interface_incomp=0; 
Array<int> NavierStokes::advection_order; // def=1
// def=advection_order
Array<int> NavierStokes::density_advection_order; 

// 0=Sussman and Puckett algorithm 
// 1=EILE (default), -1=Weymouth Yue
// 2=always EI   3=always LE
int NavierStokes::EILE_flag=1;
// 0=directionally split
// 1=unsplit everywhere
// 2=unsplit in incompressible zones
// 3=unsplit in fluid cells that neighbor a prescribed solid cell
int NavierStokes::unsplit_flag=0;

// 0=no limiter 1=minmod 2=minmod with slope=0 at interface
// 3=no limit, but slope=0 at interface
int  NavierStokes::slope_limiter_option=2; 
int  NavierStokes::bicgstab_max_num_outer_iter=60;
Real NavierStokes::projection_pressure_scale=1.0;
Real NavierStokes::projection_velocity_scale=1.0;

int NavierStokes::curv_stencil_height=4; 
int NavierStokes::ngrow_distance=4;
int NavierStokes::ngrow_make_distance=3;

// blob_matrix,blob_RHS,blob_velocity,
// blob_integral_momentum,blob_energy,
// blob_mass_for_velocity (3 components)
// blob_volume, 
// blob_center_integral,blob_center_actual
// blob_perim, blob_perim_mat, blob_triple_perim, 
int NavierStokes::num_elements_blobclass=0;

int NavierStokes::ngrowFSI=3;
int NavierStokes::nFSI_sub=12; //velocity+LS+temperature+flag+stress (3D)
Array<int> NavierStokes::im_solid_map; //nparts components, in range 0..nmat-1
Array<int> NavierStokes::im_elastic_map; 

int NavierStokes::ngrow_expansion=2;
 
// 0=no bias 1=bias
int NavierStokes::use_StewartLay=1; 

// local_face(facecut_index+1)=prescribed_solid_scale
// if face has an adjoining prescribed rigid solid cell.
Array<Real> NavierStokes::prescribed_solid_scale; // def=0.0
//0=stair-step 1=2nd order 2=mod stair-step 3=modified normal
int NavierStokes::prescribed_solid_method=0; 
int NavierStokes::min_prescribed_opt_iter=10; 

int NavierStokes::hydrate_flag=0; 
int NavierStokes::singular_possible=0; 
int NavierStokes::solvability_projection=0; 
int NavierStokes::local_solvability_projection=0; 
int NavierStokes::post_init_pressure_solve=1; 

int NavierStokes::conservative_tension_force=0;

Array<Real> NavierStokes::tension_slope;
Array<Real> NavierStokes::tension_min;
Array<Real> NavierStokes::tension_T0;
Array<Real> NavierStokes::tension;
Array<Real> NavierStokes::prefreeze_tension;
Array<Real> NavierStokes::recalesce_model_parameters;

Array<Real> NavierStokes::outflow_velocity_buffer_size;

Array<Real> NavierStokes::cap_wave_speed;

Array<Real> NavierStokes::saturation_temp;

Array<int> NavierStokes::microlayer_substrate;
Array<Real> NavierStokes::microlayer_angle;
Array<Real> NavierStokes::microlayer_size;
Array<Real> NavierStokes::macrolayer_size;
Array<Real> NavierStokes::max_contact_line_size;

// if freezing_model==0:
//
// given im1,im2 pair:
// if microlayer_substrate(im1)>0 and
//    microlayer_temperature_substrate(im1)>0.0 and
//    solidheat_flag==0 (diffuse in solid) then
//  T=microlayer_temperature_substrate at im2/substrate boundary.
//
// if microlayer_substrate(im1 or im2)>0 and
//    microlayer_temperature_substrate(im1 or im2)>0.0 and
//    solidheat_flag==2 (Neumann at solid/fluid interface) then
//  grad T dot n=(microlayer_temperature_substrate-TSAT)/
//    macrolayer_size at (im1 or im2)/substrate boundary.

Array<Real> NavierStokes::microlayer_temperature_substrate; 

int NavierStokes::custom_nucleation_model=0;

int NavierStokes::FD_curv_select=0;
int NavierStokes::FD_curv_interp=1;

Array<Real> NavierStokes::cavitation_pressure;
Array<Real> NavierStokes::cavitation_vapor_density;
Array<Real> NavierStokes::cavitation_tension;
Array<int> NavierStokes::cavitation_species;
Array<int> NavierStokes::cavitation_model;

Array<Real> NavierStokes::species_evaporation_density;

Array<Real> NavierStokes::nucleation_pressure;
Array<Real> NavierStokes::nucleation_pmg;
Array<Real> NavierStokes::nucleation_mach;
Array<Real> NavierStokes::nucleation_temp;
Real NavierStokes::nucleation_period=0.0;
Real NavierStokes::nucleation_init_time=0.0;
int NavierStokes::n_sites=0;
Array<Real> NavierStokes::pos_sites;

int NavierStokes::perturbation_on_restart=0;
 // sin(2 pi k x /L)
int NavierStokes::perturbation_mode=0; // number of wavelen each dir.
// a fraction of delta T or the radial velocity rmax omega
Real NavierStokes::perturbation_eps_temp=0.0;
Real NavierStokes::perturbation_eps_vel=0.0;

// latent_heat<0 if condensation or solidification
// latent_heat>0 if boiling or melting
Array<Real> NavierStokes::latent_heat;
Array<Real> NavierStokes::reaction_rate;
// 0=sharp interface model (TSAT dirichlet BC)
//   interpolation assumes T=TSAT at the interface.
// 1=source term model (single equation for T with source term).
//   interpolation does not assume T=TSAT at the interface.
// 2=hydrate model 
//    a) V=V(P,T,RHO,C)
//    b) single equation for T with source term
//    c) single equation for C with source term.
// 3=wildfire combustion
// 4=source term model (single equation for T with source term).
//   Tanasawa model is used for evaporation and condensation.
//   TSAT used to determine if phase change happens.
//   expansion source, and offsetting sink evenly distributed.
// 5=evaporation/condensation
Array<int> NavierStokes::freezing_model;
Array<int> NavierStokes::mass_fraction_id;
//link diffused material to non-diff. (array 1..num_species_var)
Array<int> NavierStokes::spec_material_id; 
// 0 - distribute to the destination material (default)
//     V=mdot/rho_src
// 1 - distribute to the source material
//     V=mdot/rho_dst
Array<int> NavierStokes::distribute_from_target;
int NavierStokes::is_phasechange=0;
int NavierStokes::is_cavitation=0;
int NavierStokes::is_cavitation_mixture_model=0;
int NavierStokes::normal_probe_size=1;
// 0=dirichlet at inflow
// 1=dirichlet at inflow and outflow
// 2=dirichlet at inflow and walls.
// 3=dirichlet at inflow, outflow, and walls.
int NavierStokes::prescribe_temperature_outflow=0; // default is 0

int  NavierStokes::use_lsa=0;
Real NavierStokes::Uref=0.0;
Real NavierStokes::Lref=0.0;

Real NavierStokes::pgrad_dt_factor=1.0;
// 0=volume fraction  1=mass fraction 2=impedance fraction
int  NavierStokes::pressure_select_criterion=0;

int  NavierStokes::last_finest_level=-1;

Array<Real> NavierStokes::vorterr;
Array<Real> NavierStokes::pressure_error_cutoff;

// 0 (check mag, default) 1=check diff
int NavierStokes::pressure_error_flag=0;

Array<Real> NavierStokes::temperature_error_cutoff;

Array<Real> NavierStokes::tempcutoff;
Array<Real> NavierStokes::tempcutoffmax;
Array<Real> NavierStokes::tempconst;
Array<Real> NavierStokes::initial_temperature;
Real NavierStokes::initial_temperature_diffuse_duration=0.0;

Real NavierStokes::temperature_source=0.0;
Array<Real> NavierStokes::temperature_source_cen;
Array<Real> NavierStokes::temperature_source_rad;

Array<Real> NavierStokes::density_floor;  // def=0.0
Array<Real> NavierStokes::density_ceiling;  // def=1.0e+20
Array<Real> NavierStokes::density_floor_expansion;  // def=denconst
Array<Real> NavierStokes::density_ceiling_expansion;  // def=denconst
Array<Real> NavierStokes::denconst;
Array<Real> NavierStokes::denconst_interface;
Array<Real> NavierStokes::denconst_gravity; // def=1.0
int NavierStokes::stokes_flow=0;
Array<Real> NavierStokes::added_weight;

Array<Real> NavierStokes::stiffPINF;
Array<Real> NavierStokes::prerecalesce_stiffCP;  // def=4.1855E+7
Array<Real> NavierStokes::stiffCP;  // def=4.1855E+7
Array<Real> NavierStokes::stiffGAMMA;

int NavierStokes::constant_viscosity=0;

Real NavierStokes::angular_velocity=0.0;
Array<Real> NavierStokes::DrhoDT;  // def=0.0
Array<Real> NavierStokes::DrhoDz;  // def=0.0

 // 1=>rho=rho(T,z)
 // 2=>P_hydro=P_hydro(rho(T,z)) (Boussinesq like approximation)
Array<int> NavierStokes::override_density; // def=0
Array<Real> NavierStokes::prerecalesce_viscconst;
Array<Real> NavierStokes::viscconst;
Array<Real> NavierStokes::viscconst_eddy;
Array<Real> NavierStokes::speciesviscconst;// species mass diffusion coeff.
Array<Real> NavierStokes::prerecalesce_heatviscconst;
Array<Real> NavierStokes::heatviscconst;
Array<Real> NavierStokes::viscconst_interface;
Array<Real> NavierStokes::heatviscconst_interface;
Array<Real> NavierStokes::speciesconst;  // unused currently
Array<Real> NavierStokes::speciesviscconst_interface;
// 0=diffuse in solid 1=dirichlet 2=neumann
int NavierStokes::solidheat_flag=0; 
int NavierStokes::diffusionface_flag=1; // 0=use LS  1=use VOF
int NavierStokes::elasticface_flag=1; // 0=use LS  1=use VOF
int NavierStokes::temperatureface_flag=1; // 0=use LS  1=use VOF

Array<int> NavierStokes::material_type;

Real NavierStokes::wait_time=0.0;
Real NavierStokes::advbot=1.0;
Real NavierStokes::inflow_pressure=0.0;
Real NavierStokes::outflow_pressure=0.0;
Real NavierStokes::period_time=0.0;

     // 0 - MGPCG  1-PCG 
int  NavierStokes::project_solver_type=0;
// number of Jacobi method cycles elliptic solver initially does.
int  NavierStokes::initial_project_cycles=3;
// number of Jacobi method cycles elliptic solver initially does for the
// viscosity equation.
int  NavierStokes::initial_viscosity_cycles=3;
// number of Jacobi method cycles elliptic solver initially does for the
// temperature equation.
int  NavierStokes::initial_thermal_cycles=3;
// default is to do 5 MGPCG cycles, then restart the MGPCG iteration
// and do as many cycles as necessary in order to achieve convergence.
int  NavierStokes::initial_cg_cycles=5;
int  NavierStokes::debug_dot_product=0;

int NavierStokes::smooth_type = 2; // 0=GSRB 1=ICRB 2=ILU  3=Jacobi
int NavierStokes::bottom_smooth_type = 2; // 0=GSRB 1=ICRB 2=ILU 3=Jacobi
int NavierStokes::use_mg_precond_in_mglib=1;
int NavierStokes::use_bicgstab_in_mglib_pressure=0;
int NavierStokes::use_bicgstab_in_mglib_diffusion=1;
Real NavierStokes::bottom_bottom_tol_factor=0.1;

// 0=> u=u_solid if phi_solid>=0
// 1=> u=u_solid_ghost if phi_solid>=0
int NavierStokes::law_of_the_wall=0;

// 0 fluid (default)
// 1 prescribed rigid solid (PROB.F90)
// 2 prescribed rigid solid (sci_clsvof.F90)
// 3 FSI ice
// 4 FSI link w/Kourosh Shoele
// 5 FSI rigid solid (PROB.F90)
Array<int> NavierStokes::FSI_flag; 
Array<int> NavierStokes::FSI_touch_flag; // 0..nthreads-1
// default: 1
Array<int> NavierStokes::FSI_refine_factor; 
// default: 3
Array<int> NavierStokes::FSI_bounding_box_ngrow; 

int NavierStokes::CTML_FSI_numsolids = 0;
int NavierStokes::CTML_force_model = 0; // 0=Lag force 1=Lag stress
int NavierStokes::CTML_FSI_init = 0;

int NavierStokes::invert_solid_levelset = 0; 
int NavierStokes::elements_generated = 0; 

// 0=take into account sound speed only at t=0 if compressible.
// 1=always take into account sound speed
// 2=never take into account sound speed
Array<int> NavierStokes::shock_timestep; 

Real NavierStokes::visc_coef=0.0;

int NavierStokes::include_viscous_heating=0;

int NavierStokes::multilevel_maxcycle=200;

// mg.bot_atol
// mg.visc_bot_atol
// mg.thermal_bot_atol
Real NavierStokes::minimum_relative_error = 1.0e-11;
Real NavierStokes::diffusion_minimum_relative_error = 1.0e-11;
Real NavierStokes::mac_abs_tol = 1.0e-10;
Real NavierStokes::visc_abs_tol = 1.0e-10;
Real NavierStokes::thermal_abs_tol = 1.0e-10;
int NavierStokes::viscous_maxiter = 1;
Real NavierStokes::total_advance_time=0.0;

int NavierStokes::curv_index=0;
int NavierStokes::pforce_index=1;
int NavierStokes::faceden_index=2;
int NavierStokes::facecut_index=3;
int NavierStokes::icefacecut_index=4;
int NavierStokes::icemask_index=5;
int NavierStokes::facevisc_index=6;
int NavierStokes::faceheat_index=7;
int NavierStokes::facevel_index=8;
int NavierStokes::facespecies_index=9;
int NavierStokes::massface_index=10;
int NavierStokes::vofface_index=11;
int NavierStokes::ncphys=12;

void extra_circle_parameters(
 Real& xblob2,Real& yblob2,Real& zblob2,Real& radblob2,
 Real& xblob3,Real& yblob3,Real& zblob3,Real& radblob3,
 Real& xblob4,Real& yblob4,Real& zblob4,Real& radblob4,
 Real& xblob5,Real& yblob5,Real& zblob5,Real& radblob5,
 Real& xblob6,Real& yblob6,Real& zblob6,Real& radblob6,
 Real& xblob7,Real& yblob7,Real& zblob7,Real& radblob7,
 Real& xblob8,Real& yblob8,Real& zblob8,Real& radblob8,
 Real& xblob9,Real& yblob9,Real& zblob9,Real& radblob9,
 Real& xblob10,Real& yblob10,Real& zblob10,Real& radblob10 ) {

 xblob2=0.0; 
 yblob2=0.0; 
 zblob2=0.0; 
 radblob2=0.0; 

 xblob3=0.0; 
 yblob3=0.0; 
 zblob3=0.0; 
 radblob3=0.0; 

 xblob4=0.0; 
 yblob4=0.0; 
 zblob4=0.0; 
 radblob4=0.0; 

 xblob5=0.0; 
 yblob5=0.0; 
 zblob5=0.0; 
 radblob5=0.0; 

 xblob6=0.0; 
 yblob6=0.0; 
 zblob6=0.0; 
 radblob6=0.0; 

 xblob7=0.0; 
 yblob7=0.0; 
 zblob7=0.0; 
 radblob7=0.0; 

 xblob8=0.0; 
 yblob8=0.0; 
 zblob8=0.0; 
 radblob8=0.0; 

 xblob9=0.0; 
 yblob9=0.0; 
 zblob9=0.0; 
 radblob9=0.0; 

 xblob10=0.0; 
 yblob10=0.0; 
 zblob10=0.0; 
 radblob10=0.0; 

} // subroutine extra_circle_parameters

// ns.mof_ordering overrides this.
void mof_ordering_override(Array<int>& mof_ordering_local,
 int nmat,int probtype,int axis_dir,Real radblob3,
 Real radblob4,Real radblob7,
 int mof_error_ordering_local,
 Array<int> FSI_flag_temp) {

 if (nmat!=mof_ordering_local.size())
  BoxLib::Error("mof_ordering_local invalid size");
 if (nmat!=FSI_flag_temp.size())
  BoxLib::Error("FSI_flag_temp invalid size");
 if (nmat<1)
  BoxLib::Error("nmat out of range");

 for (int im=0;im<nmat;im++) {
  mof_ordering_local[im]=0;

  if (FSI_flag_temp[im]==0) { // fluid
   // do nothing
  } else if (FSI_flag_temp[im]==1) { // prescribed rigid solid (PROB.F90)
   mof_ordering_local[im]=1;
  } else if (FSI_flag_temp[im]==2) { // prescribed rigid solid (sci_clsvof.F90)
   mof_ordering_local[im]=1;
  } else if (FSI_flag_temp[im]==3) { // ice (PROB.F90)
   // do nothing
  } else if (FSI_flag_temp[im]==4) { // FSI link w/Kourosh (sci_clsvof.F90)
   mof_ordering_local[im]=1;
  } else if (FSI_flag_temp[im]==5) { // FSI rigid solid (PROB.F90)
   mof_ordering_local[im]=1;
  } else
   BoxLib::Error("FSI_flag_temp invalid");

 }

  // default: centroid farthest from uncaptured centroid.
 if (mof_error_ordering_local==0) { 

  for (int im=0;im<nmat;im++) {

   if (FSI_flag_temp[im]==0) { // fluid
    mof_ordering_local[im]=nmat;
   } else if (FSI_flag_temp[im]==1) { //prescribed rigid solid (PROB.F90)
    mof_ordering_local[im]=1;
   } else if (FSI_flag_temp[im]==2) { //prescribed rigid solid (sci_clsvof.F90)
    mof_ordering_local[im]=1;
   } else if (FSI_flag_temp[im]==3) { // ice (PROB.F90)
    mof_ordering_local[im]=nmat;
   } else if (FSI_flag_temp[im]==4) { // FSI link w/Kourosh (sci_clsvof.F90)
    mof_ordering_local[im]=1;
   } else if (FSI_flag_temp[im]==5) { // FSI rigid solid (PROB.F90)
    mof_ordering_local[im]=1;
   } else
    BoxLib::Error("FSI_flag_temp invalid");

  } // im

   // impinge jets unlike material
  if ((probtype==530)&&(BL_SPACEDIM==3)) {
   if (axis_dir==1)
    mof_ordering_local[1]=nmat+1;  // make gas have low priority
   else if (axis_dir!=0)
    BoxLib::Error("axis_dir invalid probtype=530");
  }

   // ns.mof_ordering overrides this.

   // 2d colliding droplets, boiling, freezing problems
  if ((probtype==55)&&(BL_SPACEDIM==2)) {
   if (radblob7>0.0)
    mof_ordering_local[1]=nmat+1;  // make gas have low priority
   if (axis_dir==0) {
    // do nothing
   } else if (axis_dir==1) {
    // 0=water 1=gas 2=ice 3=cold plate
    mof_ordering_local[2]=1;
   } else if (axis_dir==5) {
    // 0=water 1=gas 2=ice 3=cold plate
    mof_ordering_local[2]=1;
    // 0=water 1=vapor 2=hot plate or
    // 0=water 1=vapor 2=gas 3=hot plate 
   } else if (axis_dir==6) {  // nucleate boiling incompressible
    mof_ordering_local[nmat-1]=1;
    // 0=water 1=vapor 2=hot plate or
    // 0=water 1=vapor 2=gas 3=hot plate 
   } else if (axis_dir==7) {  // nucleate boiling compressible
    mof_ordering_local[nmat-1]=1;
   } else
    BoxLib::Error("axis_dir invalid probtype==55");
  }

  if (probtype==540) {
   if ((radblob4>0.0)&&(radblob3>0.0)) {
    BoxLib::Error("conflict of parametrs for 540");
   } else if (radblob3>0.0) {  
    mof_ordering_local[1]=nmat+1;  // make gas have low priority
   } else if (radblob4>0.0) {
    mof_ordering_local[2]=nmat+1;  // make filament gas have low priority
   }
  }

  if (probtype==202) {  // liquidlens
   mof_ordering_local[1]=1; // make (circle) material 2 have high priority
  }

  if ((probtype==17)&&(nmat==3)&&(1==0)) {  // droplet impact 3 materials
   mof_ordering_local[1]=1; // make gas material 2 have high priority
  }

 } else if (mof_error_ordering_local==1) {

  // do nothing, order=0 (except if FSI_flag_temp[im]==1,2,4,5)

 } else
  BoxLib::Error("mof_error_ordering invalid");

} // end subroutine mof_ordering_override

void fortran_parameters() {

 Real denfact;
 Real velfact=0.0;
 Real xblob;
 Real yblob;
 Real zblob;
 Real radblob;

 Real xblob2;
 Real yblob2;
 Real zblob2;
 Real radblob2;

 Real xblob3;
 Real yblob3;
 Real zblob3;
 Real radblob3;

 Real xblob4;
 Real yblob4;
 Real zblob4;
 Real radblob4;

 Real xblob5;
 Real yblob5;
 Real zblob5;
 Real radblob5;

 Real xblob6;
 Real yblob6;
 Real zblob6;
 Real radblob6;

 Real xblob7;
 Real yblob7;
 Real zblob7;
 Real radblob7;

 Real xblob8;
 Real yblob8;
 Real zblob8;
 Real radblob8;

 Real xblob9;
 Real yblob9;
 Real zblob9;
 Real radblob9;

 Real xblob10;
 Real yblob10;
 Real zblob10;
 Real radblob10;

 Real xactive;
 Real yactive;
 Real zactive;
 Real ractive;
 Real ractivex;
 Real ractivey;
 Real ractivez;

 Real fort_stop_time=-1.0;

 int probtype;
 int adv_dir;
 Real adv_vel;
 int axis_dir;
 Real rgasinlet;
 Real vinletgas;
 Real twall;
 Real advbot;
 Real inflow_pressure=0.0;
 Real outflow_pressure=0.0;
 Real period_time=0.0;

 ParmParse ppmain;
 fort_stop_time=-1.0;
 ppmain.query("stop_time",fort_stop_time);

 int ns_max_level;
 ParmParse ppamr("amr");
 ppamr.get("max_level",ns_max_level);
 Array<int> ns_space_blocking_factor;
 ns_space_blocking_factor.resize(ns_max_level+1);
 for (int lev=0;lev<=ns_max_level;lev++)
  ns_space_blocking_factor[lev]=2;
 ppamr.queryarr("space_blocking_factor",
   ns_space_blocking_factor,0,ns_max_level+1);

 int time_blocking_factor=1;
 ppamr.query("time_blocking_factor",time_blocking_factor); 

 ParmParse pp("ns");
 pp.get("probtype",probtype);
 pp.get("axis_dir",axis_dir);
 pp.get("zblob",zblob);

 extra_circle_parameters(
   xblob2,yblob2,zblob2,radblob2,
   xblob3,yblob3,zblob3,radblob3,
   xblob4,yblob4,zblob4,radblob4,
   xblob5,yblob5,zblob5,radblob5,
   xblob6,yblob6,zblob6,radblob6,
   xblob7,yblob7,zblob7,radblob7,
   xblob8,yblob8,zblob8,radblob8,
   xblob9,yblob9,zblob9,radblob9,
   xblob10,yblob10,zblob10,radblob10 );

 pp.get("denfact",denfact);
 pp.get("velfact",velfact);

 pp.get("xblob",xblob);
 pp.get("yblob",yblob);
 pp.get("zblob",zblob);
 pp.get("radblob",radblob);

 pp.query("xblob2",xblob2);
 pp.query("yblob2",yblob2);
 pp.query("zblob2",zblob2);
 pp.query("radblob2",radblob2);

 pp.query("xblob3",xblob3);
 pp.query("yblob3",yblob3);
 pp.query("zblob3",zblob3);
 pp.query("radblob3",radblob3);

 pp.query("xblob4",xblob4);
 pp.query("yblob4",yblob4);
 pp.query("zblob4",zblob4);
 pp.query("radblob4",radblob4);

 pp.query("xblob5",xblob5);
 pp.query("yblob5",yblob5);
 pp.query("zblob5",zblob5);
 pp.query("radblob5",radblob5);

 pp.query("xblob6",xblob6);
 pp.query("yblob6",yblob6);
 pp.query("zblob6",zblob6);
 pp.query("radblob6",radblob6);

 pp.query("xblob7",xblob7);
 pp.query("yblob7",yblob7);
 pp.query("zblob7",zblob7);
 pp.query("radblob7",radblob7);

 pp.query("xblob8",xblob8);
 pp.query("yblob8",yblob8);
 pp.query("zblob8",zblob8);
 pp.query("radblob8",radblob8);

 pp.query("xblob9",xblob9);
 pp.query("yblob9",yblob9);
 pp.query("zblob9",zblob9);
 pp.query("radblob9",radblob9);

 pp.query("xblob10",xblob10);
 pp.query("yblob10",yblob10);
 pp.query("zblob10",zblob10);
 pp.query("radblob10",radblob10);

 xactive=0.0;
 yactive=0.0;
 zactive=0.0;
 ractive=0.0;
 ractivex=0.0;
 ractivey=0.0;
 ractivez=0.0;

 pp.query("xactive",xactive);
 pp.query("yactive",yactive);
 pp.query("zactive",zactive);
 pp.query("ractive",ractive);
 if (ractive>0.0) {
  ractivex=ractive;
  ractivey=ractive;
  ractivez=ractive;
 }
 pp.query("ractivex",ractivex);
 pp.query("ractivey",ractivey);
 pp.query("ractivez",ractivez);

 pp.get("adv_dir",adv_dir);
 if ((adv_dir<1)||(adv_dir>2*BL_SPACEDIM+1))
  BoxLib::Error("adv_dir invalid");

 pp.get("adv_vel",adv_vel);
 pp.get("rgasinlet",rgasinlet);
 pp.get("vinletgas",vinletgas);
 pp.get("twall",twall);
 pp.get("advbot",advbot);
 pp.query("inflow_pressure",inflow_pressure);
 pp.query("outflow_pressure",outflow_pressure);
 pp.query("period_time",period_time);

 int invert_solid_levelset=0;
 pp.query("invert_solid_levelset",invert_solid_levelset);
 if (!((invert_solid_levelset==1)||(invert_solid_levelset==0)))
  BoxLib::Error("invert_solid_levelset invalid");

 int num_species_var=0;
 int num_materials=0;
 int num_materials_vel=1;
 int num_materials_scalar_solve=1;
 int num_materials_viscoelastic=0;

 int num_state_material=SpeciesVar;  // den,T
 int num_state_base=SpeciesVar;  // den,T
 int ngeom_raw=BL_SPACEDIM+1;
 int ngeom_recon=NUM_MOF_VAR;

 pp.get("num_materials",num_materials);
 if ((num_materials<2)||(num_materials>MAX_NUM_MATERIALS))
  BoxLib::Error("num materials invalid");

 int nmat=num_materials;

 pp.query("num_materials_vel",num_materials_vel);
 if (num_materials_vel!=1)
  BoxLib::Error("num_materials_vel==1 required");

 pp.query("num_materials_scalar_solve",num_materials_scalar_solve);
 if ((num_materials_scalar_solve!=1)&&(num_materials_scalar_solve!=nmat))
  BoxLib::Error("num_materials_scalar_solve invalid");

  // this is local variable, not static variable
 int MOFITERMAX=DEFAULT_MOFITERMAX;  
 pp.query("MOFITERMAX",MOFITERMAX);
 if ((MOFITERMAX<0)||(MOFITERMAX>50))
  BoxLib::Error("mof iter max invalid in navierstokes");

 int MOF_TURN_OFF_LS=0;  // this is local variable, not static variable
 pp.query("MOF_TURN_OFF_LS",MOF_TURN_OFF_LS);
 if ((MOF_TURN_OFF_LS!=0)&&(MOF_TURN_OFF_LS!=1))
  BoxLib::Error("mof turn off ls invalid in navierstokes");

 int MOF_DEBUG_RECON=0;  // this is local variable, not static variable
 pp.query("MOF_DEBUG_RECON",MOF_DEBUG_RECON);
 if ((MOF_DEBUG_RECON!=0)&&(MOF_DEBUG_RECON!=1)&&
     (MOF_DEBUG_RECON!=2))
  BoxLib::Error("mof debug recon invalid in navierstokes");

 int fortran_max_num_materials=MAX_NUM_MATERIALS;

 pp.get("num_species_var",num_species_var);
 if (num_species_var<0)
  BoxLib::Error("num species var invalid");

 num_state_base=SpeciesVar;  // den,Temperature
 num_state_material=SpeciesVar;  // den,Temperature
 num_state_material+=num_species_var;

 Array<Real> elastic_viscosity_temp;
 elastic_viscosity_temp.resize(nmat);
 for (int im=0;im<nmat;im++) 
  elastic_viscosity_temp[im]=0.0;
 pp.queryarr("elastic_viscosity",elastic_viscosity_temp,0,nmat);

 num_materials_viscoelastic=0;
 for (int im=0;im<nmat;im++) {
  if (elastic_viscosity_temp[im]>0.0) {
   num_materials_viscoelastic++;
  } else if (elastic_viscosity_temp[im]==0.0) {
   // do nothing
  } else
   BoxLib::Error("elastic_viscosity_temp invalid");
 } // im=0..nmat-1 

 Array<Real> denconst_temp(nmat);
 Array<Real> den_ceiling_temp(nmat);
 Array<Real> den_floor_temp(nmat);
 Array<Real> cavdenconst_temp(nmat);

 Array<Real> stiffPINFtemp(nmat);
 Array<Real> stiffCPtemp(nmat);
 Array<Real> stiffGAMMAtemp(nmat);

 Array<Real> DrhoDTtemp(nmat);
 Array<Real> DrhoDztemp(nmat);
 Array<Real> tempcutofftemp(nmat);
 Array<Real> tempcutoffmaxtemp(nmat);
 Array<Real> tempconst_temp(nmat);
 Array<Real> initial_temperature_temp(nmat);
 Array<Real> viscconst_temp(nmat);
 Array<Real> viscconst_eddy_temp(nmat);
 Array<int> viscosity_state_model_temp(nmat);
 Array<Real> heatviscconst_temp(nmat);
 Array<Real> speciesconst_temp((num_species_var+1)*nmat);
 Array<Real> speciesviscconst_temp((num_species_var+1)*nmat);
 Array<int> material_type_temp(nmat);
 Array<int> FSI_flag_temp(nmat);

 pp.getarr("material_type",material_type_temp,0,nmat);

 for (int im=0;im<nmat;im++) {

  stiffPINFtemp[im]=0.0;
  stiffCPtemp[im]=4.1855e+7;
  stiffGAMMAtemp[im]=0.0;

  DrhoDTtemp[im]=0.0;
  DrhoDztemp[im]=0.0;
  tempcutofftemp[im]=1.0e-8;
  tempcutoffmaxtemp[im]=1.0e+99;
  FSI_flag_temp[im]=0;
 }
 for (int im=0;im<(num_species_var+1)*nmat;im++) {
  speciesviscconst_temp[im]=0.0;
  speciesconst_temp[im]=0.0;
 }

 pp.queryarr("FSI_flag",FSI_flag_temp,0,nmat);

 pp.queryarr("tempcutoff",tempcutofftemp,0,nmat);
 pp.queryarr("tempcutoffmax",tempcutoffmaxtemp,0,nmat);

 pp.getarr("tempconst",tempconst_temp,0,nmat);
 for (int im=0;im<nmat;im++)
  initial_temperature_temp[im]=tempconst_temp[im];
 pp.queryarr("initial_temperature",initial_temperature_temp,0,nmat);

 pp.queryarr("DrhoDT",DrhoDTtemp,0,nmat);
 pp.queryarr("DrhoDz",DrhoDztemp,0,nmat);

 pp.queryarr("stiffPINF",stiffPINFtemp,0,nmat);

 pp.queryarr("stiffCP",stiffCPtemp,0,nmat);

 Array<Real> prerecalesce_stiffCP_temp(nmat);
 for (int im=0;im<nmat;im++)
  prerecalesce_stiffCP_temp[im]=stiffCPtemp[im];
 pp.queryarr("precalesce_stiffCP",prerecalesce_stiffCP_temp,0,nmat);

 pp.queryarr("stiffGAMMA",stiffGAMMAtemp,0,nmat);

 pp.getarr("denconst",denconst_temp,0,nmat);

 for (int im=0;im<nmat;im++) {
  cavdenconst_temp[im]=0.0;
  den_ceiling_temp[im]=1.0e+20;
  den_floor_temp[im]=0.0;
 }
 pp.queryarr("cavitation_vapor_density",cavdenconst_temp,0,nmat);
 pp.queryarr("density_floor",den_floor_temp,0,nmat);
 pp.queryarr("density_ceiling",den_ceiling_temp,0,nmat);

 pp.getarr("viscconst",viscconst_temp,0,nmat);
 for (int im=0;im<nmat;im++)
  viscconst_eddy_temp[im]=0.0;
 pp.queryarr("viscconst_eddy",viscconst_eddy_temp,0,nmat);

 Array<Real> prerecalesce_viscconst_temp(nmat);
 for (int im=0;im<nmat;im++)
  prerecalesce_viscconst_temp[im]=viscconst_temp[im];
 pp.queryarr("precalesce_viscconst",prerecalesce_viscconst_temp,0,nmat);

 for (int im=0;im<nmat;im++)
  viscosity_state_model_temp[im]=0;
 pp.queryarr("viscosity_state_model",
  viscosity_state_model_temp,0,nmat);

 pp.getarr("heatviscconst",heatviscconst_temp,0,nmat);

 Array<Real> prerecalesce_heatviscconst_temp(nmat);
 for (int im=0;im<nmat;im++)
  prerecalesce_heatviscconst_temp[im]=heatviscconst_temp[im];
 pp.queryarr("precalesce_heatviscconst",prerecalesce_heatviscconst_temp,0,nmat);

 if (num_species_var>0) {
  pp.queryarr("speciesconst",speciesconst_temp,0,num_species_var*nmat);
  pp.queryarr("speciesviscconst",speciesviscconst_temp,0,num_species_var*nmat);
 }

 int nten=( (nmat-1)*(nmat-1)+nmat-1 )/2;
 Array<Real> tension_slopetemp(nten);
 Array<Real> tension_T0temp(nten);
 Array<Real> tension_mintemp(nten);
 for (int im=0;im<nten;im++) {
  tension_slopetemp[im]=0.0;
  tension_T0temp[im]=293.0;
  tension_mintemp[im]=0.0;
 }
 Array<Real> tensiontemp(nten);
 Array<Real> prefreeze_tensiontemp(nten);
 pp.getarr("tension",tensiontemp,0,nten);
 pp.queryarr("tension_slope",tension_slopetemp,0,nten);
 pp.queryarr("tension_T0",tension_T0temp,0,nten);
 pp.queryarr("tension_min",tension_mintemp,0,nten);

 for (int im=0;im<nten;im++)
  prefreeze_tensiontemp[im]=tensiontemp[im];
 pp.queryarr("prefreeze_tension",prefreeze_tensiontemp,0,nten);


 for (int im=0;im<nmat;im++) {

  if (material_type_temp[im]==999) {

   if ((FSI_flag_temp[im]!=1)&& // prescribed PROB.F90 rigid solid
       (FSI_flag_temp[im]!=2)&& // prescribed sci_clsvof.F90 rigid solid
       (FSI_flag_temp[im]!=4))  // FSI CTML solid
    BoxLib::Error("FSI_flag_temp invalid");

  } else if (material_type_temp[im]==0) {

   if ((FSI_flag_temp[im]!=0)&& // fluid
       (FSI_flag_temp[im]!=3)&& // ice
       (FSI_flag_temp[im]!=5))  // FSI PROB.F90 rigid solid.
    BoxLib::Error("FSI_flag_temp invalid");

  } else if ((material_type_temp[im]>0)&& 
             (material_type_temp[im]<=MAX_NUM_EOS)) {

   if (FSI_flag_temp[im]!=0) // fluid
    BoxLib::Error("FSI_flag_temp invalid");

  } else {
   BoxLib::Error("material type invalid");
  }
 }

 if (num_state_base!=2)
  BoxLib::Error("num_state_base invalid 9");

 int rz_flag=0;
 if (CoordSys::IsRZ())
  rz_flag=1;
 else if (CoordSys::IsCartesian())
  rz_flag=0;
 else if (CoordSys::IsCYLINDRICAL())
  rz_flag=3;
 else
  BoxLib::Error("CoordSys bust 1");

 Real problox=Geometry::ProbLo(0);
 Real probloy=Geometry::ProbLo(1);
 Real probloz=Geometry::ProbLo(BL_SPACEDIM-1);
 Real probhix=Geometry::ProbHi(0);
 Real probhiy=Geometry::ProbHi(1);
 Real probhiz=Geometry::ProbHi(BL_SPACEDIM-1);

 int ioproc=0;
 if (ParallelDescriptor::IOProcessor())
  ioproc=1;

 int prescribe_temperature_outflow=0;
 pp.query("prescribe_temperature_outflow",prescribe_temperature_outflow);
 if ((prescribe_temperature_outflow<0)||
     (prescribe_temperature_outflow>3))
  BoxLib::Error("prescribe_temperature_outflow invalid (fortran_parameters)");

 Real MUSHY_THICK=2.0;
 pp.query("MUSHY_THICK",MUSHY_THICK);

 Real gravity=0.0; 
 int gravity_dir=BL_SPACEDIM;
 int invert_gravity=0;
 pp.query("gravity",gravity);
 pp.query("gravity_dir",gravity_dir);
 pp.query("invert_gravity",invert_gravity);
 if ((gravity_dir<1)||(gravity_dir>BL_SPACEDIM))
  BoxLib::Error("gravity dir invalid");

 int n_sites=0;
 pp.query("n_sites",n_sites);
 Real nucleation_init_time=0.0;
 pp.query("nucleation_init_time",nucleation_init_time);

 Array<Real> temp_pos_sites(4);
 for (int dir=0;dir<4;dir++)
  temp_pos_sites[dir]=0.0;
 
 if (n_sites>0) {
  temp_pos_sites.resize(4*n_sites);
  pp.getarr("pos_sites",temp_pos_sites,0,4*n_sites);
 } else if (n_sites==0) {
  // do nothing
 } else
  BoxLib::Error("n_sites invalid");

 int cpp_max_num_eos=MAX_NUM_EOS;

 FORT_OVERRIDE(
  &ns_max_level,
  ns_space_blocking_factor.dataPtr(),
  &time_blocking_factor,
  &prescribe_temperature_outflow,
  &rz_flag,
  FSI_flag_temp.dataPtr(),
  &invert_solid_levelset,
  &denfact,
  &velfact,
  &n_sites,
  &nucleation_init_time,
  temp_pos_sites.dataPtr(),
  &xblob,&yblob,&zblob,&radblob,
  &xblob2,&yblob2,&zblob2,&radblob2,
  &xblob3,&yblob3,&zblob3,&radblob3,
  &xblob4,&yblob4,&zblob4,&radblob4,
  &xblob5,&yblob5,&zblob5,&radblob5,
  &xblob6,&yblob6,&zblob6,&radblob6,
  &xblob7,&yblob7,&zblob7,&radblob7,
  &xblob8,&yblob8,&zblob8,&radblob8,
  &xblob9,&yblob9,&zblob9,&radblob9,
  &xblob10,&yblob10,&zblob10,&radblob10,
  &xactive,&yactive,&zactive,
  &ractivex,&ractivey,&ractivez,
  &probtype,&adv_dir,&adv_vel,
  &axis_dir,&rgasinlet,
  &vinletgas,&twall,&advbot, 
  &inflow_pressure,&outflow_pressure,
  &period_time,
  &problox,&probloy,&probloz,&probhix,&probhiy,&probhiz,
  &num_species_var,
  &num_materials_viscoelastic,
  &num_state_material,
  &num_state_base,
  &ngeom_raw,&ngeom_recon,
  &fortran_max_num_materials,
  &cpp_max_num_eos,
  &nmat,
  &num_materials_vel,
  &num_materials_scalar_solve,
  material_type_temp.dataPtr(),
  &nten,
  DrhoDTtemp.dataPtr(),
  DrhoDztemp.dataPtr(),
  tempconst_temp.dataPtr(),
  initial_temperature_temp.dataPtr(),
  tempcutofftemp.dataPtr(),
  tempcutoffmaxtemp.dataPtr(),
  stiffPINFtemp.dataPtr(),
  stiffCPtemp.dataPtr(),
  stiffGAMMAtemp.dataPtr(),
  denconst_temp.dataPtr(),
  den_floor_temp.dataPtr(),
  den_ceiling_temp.dataPtr(),
  cavdenconst_temp.dataPtr(),
  viscconst_temp.dataPtr(),
  viscconst_eddy_temp.dataPtr(),
  viscosity_state_model_temp.dataPtr(),
  elastic_viscosity_temp.dataPtr(),
  heatviscconst_temp.dataPtr(),
  prerecalesce_heatviscconst_temp.dataPtr(),
  prerecalesce_viscconst_temp.dataPtr(),
  prerecalesce_stiffCP_temp.dataPtr(),
  speciesconst_temp.dataPtr(),
  speciesviscconst_temp.dataPtr(),
  tensiontemp.dataPtr(),
  tension_slopetemp.dataPtr(),
  tension_T0temp.dataPtr(),
  tension_mintemp.dataPtr(),
  prefreeze_tensiontemp.dataPtr(),
  &MUSHY_THICK,
  &gravity,
  &gravity_dir,
  &invert_gravity,
  &fort_stop_time,
  &ioproc);

 int mof_error_ordering_local=0;
 pp.query("mof_error_ordering",mof_error_ordering_local);
 if ((mof_error_ordering_local!=0)&&
     (mof_error_ordering_local!=1))
  BoxLib::Error("mof_error_ordering_local invalid");
 Array<int> mof_ordering_local;
 mof_ordering_local.resize(nmat);

 mof_ordering_override(mof_ordering_local,
  nmat,probtype,
  axis_dir,radblob3,
  radblob4,radblob7,
  mof_error_ordering_local,
  FSI_flag_temp);

 pp.queryarr("mof_ordering",mof_ordering_local,0,nmat);
 for (int i=0;i<nmat;i++) {
  if ((mof_ordering_local[i]<0)||
      (mof_ordering_local[i]>nmat+1))
   BoxLib::Error("mof_ordering_local invalid");
 }

 int temp_POLYGON_LIST_MAX=1000;
 
 FORT_INITMOF(
   mof_ordering_local.dataPtr(),
   &nmat,&MOFITERMAX,
   &MOF_DEBUG_RECON,
   &MOF_TURN_OFF_LS,
   &thread_class::nthreads,
   &temp_POLYGON_LIST_MAX);

 if (ioproc==1) {
  std::cout << "in c++ code, after fort_override\n";
  for (int im=0;im<nmat;im++) {
   std::cout << "im= " << im << " mof_ordering_local= " <<
    mof_ordering_local[im] << '\n';
  }
 }
}  // subroutine fortran_parameters()




void
NavierStokes::variableCleanUp ()
{
    desc_lst.clear();
}

void
NavierStokes::read_geometry ()
{
    //
    // Must load coord here because CoordSys hasn't read it in yet.
    //
    ParmParse pp("geometry");
    ParmParse ppns("ns");

    int coord;
    pp.get("coord_sys",coord);

    if ((CoordSys::CoordType) coord == CoordSys::RZ)  
     if (BL_SPACEDIM==3)
      BoxLib::Error("No RZ in 3d");

    if (((CoordSys::CoordType) coord == CoordSys::RZ) && 
        (phys_bc.lo(0) != Symmetry)) {
        phys_bc.setLo(0,Symmetry);
        temperature_phys_bc.setLo(0,Symmetry);
        species_phys_bc.setLo(0,Symmetry);

        if (ParallelDescriptor::IOProcessor())
            std::cout << "\n WARNING: Setting phys_bc at xlo to Symmetry\n\n";
    }


} // subroutine read_geometry

void
NavierStokes::read_params ()
{
    ParmParse pp("ns");

    pp.query("check_nan",check_nan);
    pp.query("v",verbose);
    pp.query("fab_verbose",fab_verbose);
    pp.query("output_drop_distribution",output_drop_distribution);
    pp.query("extend_pressure_into_solid",extend_pressure_into_solid);
    pp.query("show_timings",show_timings);
    pp.query("show_mem",show_mem);

    pp.query("slice_dir",slice_dir);
    xslice.resize(BL_SPACEDIM);
    if ((slice_dir>=0)&&(slice_dir<BL_SPACEDIM)) {
     for (int i=0;i<BL_SPACEDIM;i++)
      xslice[i]=0.0;
     pp.queryarr("xslice",xslice,0,BL_SPACEDIM);
    } else
     BoxLib::Error("slice_dir invalid");


    if (ParallelDescriptor::IOProcessor()) {
     std::cout << "check_nan " << check_nan << '\n';
     std::cout << "NavierStokes.verbose " << verbose << '\n';
     std::cout << "slice_dir " << slice_dir << '\n';
     for (int i=0;i<BL_SPACEDIM;i++) {
      std::cout << "i=" << i << '\n';
      std::cout << "xslice " << xslice[i] << '\n';
     }
    } 

    pp.query("nblocks",nblocks);

    int nblocks_size=( (nblocks==0) ? 1 : nblocks );
    xblocks.resize(nblocks_size);
    yblocks.resize(nblocks_size);
    zblocks.resize(nblocks_size);
    rxblocks.resize(nblocks_size);
    ryblocks.resize(nblocks_size);
    rzblocks.resize(nblocks_size);

    if ((nblocks>0)&&(nblocks<10)) {
     pp.getarr("xblocks",xblocks,0,nblocks);
     pp.getarr("yblocks",yblocks,0,nblocks);
     pp.getarr("zblocks",zblocks,0,nblocks);
     pp.getarr("rxblocks",rxblocks,0,nblocks);
     pp.getarr("ryblocks",ryblocks,0,nblocks);
     pp.getarr("rzblocks",rzblocks,0,nblocks);
     if (ParallelDescriptor::IOProcessor()) {
      std::cout << "nblocks " << nblocks << '\n';
      for (int i=0;i<nblocks;i++) {
       std::cout << "i=" << i << '\n';
       std::cout << "xblocks " << xblocks[i] << '\n';
       std::cout << "yblocks " << yblocks[i] << '\n';
       std::cout << "zblocks " << zblocks[i] << '\n';
       std::cout << "rxblocks " << rxblocks[i] << '\n';
       std::cout << "ryblocks " << ryblocks[i] << '\n';
       std::cout << "rzblocks " << rzblocks[i] << '\n';
      }
     }
    } else if (nblocks!=0)
     BoxLib::Error("nblocks out of range");

    num_materials=0;
    num_materials_vel=1;
    num_materials_scalar_solve=1;
    pp.get("num_materials",num_materials);
    if ((num_materials<2)||(num_materials>MAX_NUM_MATERIALS))
     BoxLib::Error("num materials invalid");

    int nmat=num_materials;
    int nten=( (nmat-1)*(nmat-1)+nmat-1 )/2;

    pp.query("num_materials_vel",num_materials_vel);
    if (num_materials_vel!=1)
     BoxLib::Error("num_materials_vel==1 required");

    pp.query("num_materials_scalar_solve",num_materials_scalar_solve);
    if ((num_materials_scalar_solve!=1)&&
        (num_materials_scalar_solve!=nmat))
     BoxLib::Error("num_materials_scalar_solve invalid");

    // blob_matrix,blob_RHS,blob_velocity,
    // blob_integral_momentum,blob_energy,
    // blob_mass_for_velocity (3 components)
    // blob_volume, 
    // blob_center_integral,blob_center_actual
    // blob_perim, blob_perim_mat, blob_triple_perim, 
    num_elements_blobclass=
     3*(2*BL_SPACEDIM)*(2*BL_SPACEDIM)+
     3*(2*BL_SPACEDIM)+
     3*(2*BL_SPACEDIM)+
     2*(2*BL_SPACEDIM)+
     1+
     3+1+
     2*BL_SPACEDIM+1+nmat+nmat*nmat;

    int ns_max_level;
    Array<int> ns_max_grid_size;
    int the_max_grid_size=0;
    int cnt_max_grid_size;

    ParmParse ppamr("amr");
    ppamr.get("max_level",ns_max_level);
    Array<int> ns_n_error_buf;
    ns_n_error_buf.resize(ns_max_level);
    for (int ilev=0;ilev<ns_max_level;ilev++) 
     ns_n_error_buf[ilev]=1;
    ppamr.queryarr("n_error_buf",ns_n_error_buf,0,ns_max_level);

    cnt_max_grid_size=ppamr.countval("max_grid_size");
  
    if (cnt_max_grid_size==0) {
     ns_max_grid_size.resize(1);
     ns_max_grid_size[0]=0;
    } else if (cnt_max_grid_size==1) {
     ppamr.get("max_grid_size",the_max_grid_size);
     ns_max_grid_size.resize(1);
     ns_max_grid_size[0]=the_max_grid_size;
    } else if (cnt_max_grid_size>1) {
     ns_max_grid_size.resize(cnt_max_grid_size);
     ppamr.getarr("max_grid_size",ns_max_grid_size,0,cnt_max_grid_size);
    } else
     BoxLib::Error("cnt_max_grid_size invalid");

    tecplot_max_level=ns_max_level;
    max_level_two_materials=ns_max_level;
    pp.query("tecplot_max_level",tecplot_max_level);
    pp.query("max_level_two_materials",max_level_two_materials);

    radius_cutoff.resize(nmat);
    for (int i=0;i<nmat;i++)
     radius_cutoff[i]=0;

     // 0=>never adapt  -1=>always adapt
     // otherwise, if radius<radius_cutoff * dx then adapt.
    pp.queryarr("radius_cutoff",radius_cutoff,0,nmat);
    for (int i=0;i<nmat;i++)
     if (radius_cutoff[i]<-1)
      BoxLib::Error("radius_cutoff invalid");

    if ((tecplot_max_level<0)||
        (tecplot_max_level>ns_max_level))
     BoxLib::Error("tecplot_max_level invalid"); 

    if ((max_level_two_materials<0)||
        (max_level_two_materials>ns_max_level))
     BoxLib::Error("max_level_two_materials invalid"); 

    if (ParallelDescriptor::IOProcessor()) {
     std::cout << "tecplot_max_level " << 
       tecplot_max_level << '\n';
     std::cout << "max_level_two_materials " << 
       max_level_two_materials << '\n';
     for (int i=0;i<nmat;i++)
      std::cout << "im=" << i << " radius_cutoff= " << 
        radius_cutoff[i] << '\n';
    }
    if (ParallelDescriptor::IOProcessor()) {
     std::cout << "num_elements_blobclass= " << 
      num_elements_blobclass << '\n';
    }

    pp.query("ncoarseblocks",ncoarseblocks);
    int ncoarseblocks_size=( (ncoarseblocks==0) ? 1 : ncoarseblocks );
    xcoarseblocks.resize(ncoarseblocks_size);
    ycoarseblocks.resize(ncoarseblocks_size);
    zcoarseblocks.resize(ncoarseblocks_size);
    rxcoarseblocks.resize(ncoarseblocks_size);
    rycoarseblocks.resize(ncoarseblocks_size);
    rzcoarseblocks.resize(ncoarseblocks_size);
    if ((ncoarseblocks>0)&&(ncoarseblocks<10)) {
     pp.getarr("xcoarseblocks",xcoarseblocks,0,ncoarseblocks);
     pp.getarr("ycoarseblocks",ycoarseblocks,0,ncoarseblocks);
     pp.getarr("zcoarseblocks",zcoarseblocks,0,ncoarseblocks);
     pp.getarr("rxcoarseblocks",rxcoarseblocks,0,ncoarseblocks);
     pp.getarr("rycoarseblocks",rycoarseblocks,0,ncoarseblocks);
     pp.getarr("rzcoarseblocks",rzcoarseblocks,0,ncoarseblocks);

     if (ParallelDescriptor::IOProcessor()) {

      std::cout << "ncoarseblocks " << ncoarseblocks << '\n';
      for (int i=0;i<ncoarseblocks;i++) {
       std::cout << "i=" << i << '\n';
       std::cout << "xcoarseblocks " << xcoarseblocks[i] << '\n';
       std::cout << "ycoarseblocks " << ycoarseblocks[i] << '\n';
       std::cout << "zcoarseblocks " << zcoarseblocks[i] << '\n';
       std::cout << "rxcoarseblocks " << rxcoarseblocks[i] << '\n';
       std::cout << "rycoarseblocks " << rycoarseblocks[i] << '\n';
       std::cout << "rzcoarseblocks " << rzcoarseblocks[i] << '\n';
      }
     }
    } else if (ncoarseblocks!=0)
     BoxLib::Error("ncoarseblocks out of range");
     
    Array<int> lo_bc(BL_SPACEDIM);
    Array<int> hi_bc(BL_SPACEDIM);
    Array<int> temperature_lo_bc(BL_SPACEDIM);
    Array<int> temperature_hi_bc(BL_SPACEDIM);
    Array<int> species_lo_bc(BL_SPACEDIM);
    Array<int> species_hi_bc(BL_SPACEDIM);
    pp.getarr("lo_bc",lo_bc,0,BL_SPACEDIM);
    pp.getarr("hi_bc",hi_bc,0,BL_SPACEDIM);
    for (int i = 0; i < BL_SPACEDIM; i++) {
     phys_bc.setLo(i,lo_bc[i]);
     phys_bc.setHi(i,hi_bc[i]);
     temperature_phys_bc.setLo(i,lo_bc[i]);
     temperature_phys_bc.setHi(i,hi_bc[i]);
     temperature_lo_bc[i]=lo_bc[i];
     temperature_hi_bc[i]=hi_bc[i];
     species_phys_bc.setLo(i,lo_bc[i]);
     species_phys_bc.setHi(i,hi_bc[i]);
     species_lo_bc[i]=lo_bc[i];
     species_hi_bc[i]=hi_bc[i];
    }
    pp.queryarr("temperature_lo_bc",temperature_lo_bc,0,BL_SPACEDIM);
    pp.queryarr("temperature_hi_bc",temperature_hi_bc,0,BL_SPACEDIM);
    pp.queryarr("species_lo_bc",species_lo_bc,0,BL_SPACEDIM);
    pp.queryarr("species_hi_bc",species_hi_bc,0,BL_SPACEDIM);
    for (int i = 0; i < BL_SPACEDIM; i++) {
     temperature_phys_bc.setLo(i,temperature_lo_bc[i]);
     temperature_phys_bc.setHi(i,temperature_hi_bc[i]);
     species_phys_bc.setLo(i,species_lo_bc[i]);
     species_phys_bc.setHi(i,species_hi_bc[i]);
    }

     // call after phys_bc initialized since phys_bc might have to be
     // modified in this routine.
    read_geometry();

    //
    // Check phys_bc against possible periodic geometry
    // if periodic, must have internal BC marked.
    //
    Array<int> periodic_flag;
    periodic_flag.resize(BL_SPACEDIM);
    for (int dir = 0; dir < BL_SPACEDIM; dir++) {
     periodic_flag[dir]=0;
    }
    if (Geometry::isAnyPeriodic()) {
     for (int dir = 0; dir < BL_SPACEDIM; dir++) {
      if (Geometry::isPeriodic(dir)) {
       periodic_flag[dir]=1;
       if ((lo_bc[dir] != Interior)||
           (temperature_lo_bc[dir]!=Interior)) {
        std::cerr << "NavierStokes::variableSetUp:periodic in direction "
            << dir << " but low BC is not Interior\n";
        BoxLib::Abort("NavierStokes::read_params()");
       }
       if ((hi_bc[dir] != Interior)||
           (temperature_hi_bc[dir]!=Interior)) {
        std::cerr << "NavierStokes::variableSetUp:periodic in direction "
            << dir << " but high BC is not Interior\n";
        BoxLib::Abort("NavierStokes::read_params()");
       }
      } 
     }
    }

    FORT_SET_PERIODIC_VAR(periodic_flag.dataPtr());

    for (int dir = 0; dir < BL_SPACEDIM; dir++) {
     if (!Geometry::isPeriodic(dir)) {
      if ((lo_bc[dir] == Interior)||
          (temperature_lo_bc[dir]==Interior)) {
       std::cerr << "NavierStokes::variableSetUp:Interior bc in direction "
                 << dir << " but not defined as periodic\n";
       BoxLib::Abort("NavierStokes::read_params()");
      }
      if ((hi_bc[dir] == Interior)||
          (temperature_hi_bc[dir]==Interior)) {
       std::cerr << "NavierStokes::variableSetUp:Interior bc in direction "
                 << dir << " but not defined as periodic\n";
       BoxLib::Abort("NavierStokes::read_params()");
      }
     }
    }

    if (ParallelDescriptor::IOProcessor()) {
     std::cout << "phys_bc= " << phys_bc << '\n';
     std::cout << "temperature_phys_bc= " << temperature_phys_bc << '\n';
     std::cout << "species_phys_bc= " << species_phys_bc << '\n';
    }

    // if domain dimensions needed:
    //Geometry::ProbLo(dir);
    //Geometry::ProbHi(dir);

    //
    // Get timestepping parameters.
    //
    pp.get("cfl",cfl);
    if (cfl>0.9)
     BoxLib::Warning("WARNING: cfl should be less than or equal to 0.9");

    pp.query("enable_spectral",enable_spectral);

    if (enable_spectral==0) {
     viscous_enable_spectral=0;
    } else if (enable_spectral==1) {
     viscous_enable_spectral=0;
    } else if (enable_spectral==2) {
     viscous_enable_spectral=0;
    } else if (enable_spectral==3) {
     viscous_enable_spectral=0;
    } else
     BoxLib::Error("enable_spectral invalid");
	
    projection_enable_spectral=enable_spectral;

    pp.query("viscous_enable_spectral",viscous_enable_spectral);
    pp.query("projection_enable_spectral",projection_enable_spectral);

    if ((viscous_enable_spectral==1)||
        (viscous_enable_spectral==2)) {
     BoxLib::Error("no slip BC not implemented for space order>2");
    } else if ((viscous_enable_spectral==0)||
	       (viscous_enable_spectral==3)) {
     // do nothing
    } else
     BoxLib::Error("viscous_enable_spectral invalid");

    pp.query("SEM_upwind",SEM_upwind);
    if (SEM_upwind==1) {
     // do nothing
    } else if (SEM_upwind==0) {
     // do nothing
    } else
     BoxLib::Error("SEM_upwind invalid");

    pp.query("SEM_advection_algorithm",SEM_advection_algorithm);
    if (SEM_advection_algorithm==1) {
     // do nothing
    } else if (SEM_advection_algorithm==0) {
     // do nothing
    } else
     BoxLib::Error("SEM_advection_algorithm invalid");

    pp.query("continuous_mof",continuous_mof);

    pp.query("init_shrink",init_shrink);
    pp.query("dt_max",dt_max);

    pp.query("change_max",change_max);
    change_max_init=change_max;
    pp.query("change_max_init",change_max_init);

    pp.query("fixed_dt",fixed_dt);
    fixed_dt_init=fixed_dt;
    pp.query("fixed_dt_init",fixed_dt_init);

    pp.query("min_velocity_for_dt",min_velocity_for_dt);
    if (min_velocity_for_dt<0.0)
     BoxLib::Error("min_velocity_for_dt invalid");

    pp.query("fixed_dt_velocity",fixed_dt_velocity);
    pp.query("sum_interval",sum_interval);

    pp.query("profile_debug",profile_debug);
    if ((profile_debug!=0)&&(profile_debug!=1))
     BoxLib::Error("profile_debug invalid");

    pp.query("ns_tiling",ns_tiling);
    if ((ns_tiling!=true)&&(ns_tiling!=false))
     BoxLib::Error("ns_tiling invalid");

    if (thread_class::nthreads<1)
     BoxLib::Error("thread_class::nthreads invalid ns init");


    MUSHY_THICK=2.0;
    pp.query("MUSHY_THICK",MUSHY_THICK);

    pp.query("gravity",gravity);
    pp.query("gravity_dir",gravity_dir);
    pp.query("terminal_velocity_dt",terminal_velocity_dt);
    pp.query("invert_gravity",invert_gravity);
    if ((gravity_dir<1)||(gravity_dir>BL_SPACEDIM))
     BoxLib::Error("gravity dir invalid");
    if ((terminal_velocity_dt<0)||
        (terminal_velocity_dt>1))
     BoxLib::Error("terminal_velocity_dt invalid");

    pp.get("visc_coef",visc_coef);

    pp.query("include_viscous_heating",include_viscous_heating);
    if ((include_viscous_heating<0)||
        (include_viscous_heating>1))
     BoxLib::Error("include_viscous_heating invalid");
    
    if (ParallelDescriptor::IOProcessor()) {
     std::cout << "profile_debug= " << profile_debug << '\n';
     std::cout << "ns_tiling= " << ns_tiling << '\n';
     std::cout << "thread_class::nthreads= " << 
       thread_class::nthreads << '\n';
     std::cout << "sum_interval " << sum_interval << '\n';
     std::cout << "show_timings " << show_timings << '\n';
     std::cout << "show_mem " << show_mem << '\n';
     std::cout << "output_drop_distribution " << 
      output_drop_distribution << '\n';
     std::cout << "extend_pressure_into_solid " << 
      extend_pressure_into_solid << '\n';
     std::cout << "visc_coef " << visc_coef << '\n';
     std::cout << "include_viscous_heating " << include_viscous_heating << '\n';

     std::cout << "MUSHY_THICK " << MUSHY_THICK << '\n';

     std::cout << "gravity " << gravity << '\n';
     std::cout << "invert_gravity " << invert_gravity << '\n';
     std::cout << "gravity_dir " << gravity_dir << '\n';
     std::cout << "terminal_velocity_dt " << terminal_velocity_dt << '\n';
     std::cout << "cfl " << cfl << '\n';
     std::cout << "enable_spectral " << enable_spectral << '\n';
     std::cout << "viscous_enable_spectral " << 
       viscous_enable_spectral << '\n';
     std::cout << "projection_enable_spectral " << 
       projection_enable_spectral << '\n';
     std::cout << "SEM_upwind " << SEM_upwind << '\n';
     std::cout << "SEM_advection_algorithm " << 
       SEM_advection_algorithm << '\n';
     std::cout << "continuous_mof " << continuous_mof << '\n';
    }

    pp.query("FD_curv_interp",FD_curv_interp);
    if ((FD_curv_interp!=0)&&
        (FD_curv_interp!=1))
     BoxLib::Error("FD_curv_interp invalid");

    pp.query("FD_curv_select",FD_curv_select);
    if ((FD_curv_select!=0)&&
        (FD_curv_select!=1))
     BoxLib::Error("FD_curv_select invalid");

    custom_nucleation_model=0;
    pp.query("custom_nucleation_model",custom_nucleation_model);
    if ((custom_nucleation_model!=0)&&
        (custom_nucleation_model!=1))
     BoxLib::Error("custom_nucleation_model invalid");

    n_sites=0;
    pp.query("n_sites",n_sites);
    pos_sites.resize(4);
    for (int dir=0;dir<4;dir++)
     pos_sites[dir]=0.0;
    if (n_sites>0) {
     pos_sites.resize(4*n_sites);
     pp.getarr("pos_sites",pos_sites,0,4*n_sites);
    } else if (n_sites==0) {
     // do nothing
    } else
     BoxLib::Error("n_sites invalid");
   
    pp.get("denfact",denfact);
    pp.get("velfact",velfact);
    pp.get("xblob",xblob);
    pp.get("yblob",yblob);
    pp.get("zblob",zblob);
    pp.get("radblob",radblob);
    pp.get("probtype",probtype);
    pp.get("axis_dir",axis_dir);

    visual_ncell.resize(BL_SPACEDIM);
    for (int dir=0;dir<BL_SPACEDIM;dir++)
     visual_ncell[dir]=8;
    pp.queryarr("visual_ncell",visual_ncell,0,BL_SPACEDIM);
    pp.query("visual_compare",visual_compare);

    pp.query("visual_tessellate_vfrac",visual_tessellate_vfrac);
    pp.query("visual_revolve",visual_revolve);
    pp.query("visual_option",visual_option);

    if ((visual_tessellate_vfrac!=0)&&
        (visual_tessellate_vfrac!=1))
     BoxLib::Error("visual_tessellate_vfrac invalid");

    if (visual_revolve<0) {
     std::cout << "visual_revolve: " << visual_revolve << '\n';
     BoxLib::Error("visual_revolve invalid");
    }
    if ((visual_option<-2)||(visual_option>-1))
     BoxLib::Error("visual_option invalid try -1 or -2");

    if (BL_SPACEDIM==2) {
     adapt_quad_depth=2;
     if ((probtype==28)||(probtype==29)||(probtype==31))
      adapt_quad_depth=5; 
    } else if (BL_SPACEDIM==3) {
     adapt_quad_depth=1;
     if ((probtype==28)||(probtype==29)||(probtype==31))
      adapt_quad_depth=3;
    } else
     BoxLib::Error("dimension bust");

    pp.query("adapt_quad_depth",adapt_quad_depth);

    pp.query("invert_solid_levelset",invert_solid_levelset);
    if (!((invert_solid_levelset==1)||(invert_solid_levelset==0)))
     BoxLib::Error("invert_solid_levelset invalid");

    pp.query("law_of_the_wall",law_of_the_wall);
    if ((law_of_the_wall!=0)&&
        (law_of_the_wall!=1))
     BoxLib::Error("law_of_the_wall invalid");

    FSI_flag.resize(nmat);
    FSI_refine_factor.resize(nmat);
    FSI_bounding_box_ngrow.resize(nmat);

    material_type.resize(nmat);
    pp.getarr("material_type",material_type,0,nmat);
 
    for (int i=0;i<nmat;i++) {
     FSI_flag[i]=0;
     FSI_refine_factor[i]=1;
     FSI_bounding_box_ngrow[i]=3;
    }
    pp.queryarr("FSI_flag",FSI_flag,0,nmat);
    pp.queryarr("FSI_refine_factor",FSI_refine_factor,0,nmat);
    pp.queryarr("FSI_bounding_box_ngrow",FSI_bounding_box_ngrow,0,nmat);

    pp.query("CTML_FSI_numsolids",CTML_FSI_numsolids);
    pp.query("CTML_force_model",CTML_force_model);
    if ((CTML_force_model!=0)&&(CTML_force_model!=1))
     BoxLib::Error("CTML_force_model invalid");

    int nparts=0;
    int CTML_FSI_numsolids_test=0;
    for (int i=0;i<nmat;i++) {

     if (FSI_flag[i]==4)
      CTML_FSI_numsolids_test++;
 
     if (ns_is_rigid(i)==1) {
      nparts++;
     } else if (ns_is_rigid(i)==0) {
      // do nothing
     } else
      BoxLib::Error("ns_is_rigid invalid");
    }  // i=0..nmat-1
    im_solid_map.resize(nparts);

    if (CTML_FSI_numsolids!=CTML_FSI_numsolids_test)
     BoxLib::Error("CTML_FSI_numsolids!=CTML_FSI_numsolids_test");

    nparts=0;
    for (int i=0;i<nmat;i++) {
     if (ns_is_rigid(i)==1) {
      im_solid_map[nparts]=i;
      nparts++;
     } else if (ns_is_rigid(i)==0) {
      // do nothing
     } else
      BoxLib::Error("ns_is_rigid invalid");
    }  // i=0..nmat-1
    if (nparts!=im_solid_map.size())
     BoxLib::Error("nparts!=im_solid_map.size()");

    elastic_viscosity.resize(nmat);
    for (int i=0;i<nmat;i++) {
     elastic_viscosity[i]=0.0;
    }
    pp.queryarr("elastic_viscosity",elastic_viscosity,0,nmat);

    num_materials_viscoelastic=0;
    for (int i=0;i<nmat;i++) {
     if (elastic_viscosity[i]>0.0) {
      num_materials_viscoelastic++;
     } else if (elastic_viscosity[i]==0.0) {
      // do nothing
     } else
      BoxLib::Error("elastic_viscosity invalid");
    } // im=0..nmat-1 
    im_elastic_map.resize(num_materials_viscoelastic);

    num_materials_viscoelastic=0;
    for (int i=0;i<nmat;i++) {
     if (elastic_viscosity[i]>0.0) {
      im_elastic_map[num_materials_viscoelastic]=i;
      num_materials_viscoelastic++;
     } else if (elastic_viscosity[i]==0.0) {
      // do nothing
     } else
      BoxLib::Error("elastic_viscosity invalid");
    } // im=0..nmat-1 

    NUM_STATE_TYPE=DIV_Type+1;

    if (nparts==0) {
     Solid_State_Type=-1;
    } else if ((nparts>=1)&&(nparts<=nmat-1)) {
     Solid_State_Type=NUM_STATE_TYPE;
     NUM_STATE_TYPE++;
    } else
     BoxLib::Error("nparts invalid");
  
    if (num_materials_viscoelastic==0) {
     Tensor_Type=-1;
    } else if ((num_materials_viscoelastic>=1)&&
               (num_materials_viscoelastic<=nmat)) {
     Tensor_Type=NUM_STATE_TYPE;
     NUM_STATE_TYPE++;
    } else
     BoxLib::Error("num_materials_viscoelastic invalid");

    for (int i=0;i<nmat;i++) {
     if (material_type[i]==0) {
      if (ns_is_rigid(i)!=0)
       BoxLib::Error("ns_is_rigid invalid");
     } else if (material_type[i]==999) {
      if (ns_is_rigid(i)!=1)
       BoxLib::Error("ns_is_rigid invalid");
     } else if ((material_type[i]>0)&&
                (material_type[i]<=MAX_NUM_EOS)) {
      if (ns_is_rigid(i)!=0)
       BoxLib::Error("ns_is_rigid invalid");
     } else
      BoxLib::Error("material_type invalid");

    } // i=0..nmat-1

    ParmParse pplp("Lp");
    pplp.query("smooth_type",smooth_type);
    pplp.query("bottom_smooth_type",bottom_smooth_type);
    if (smooth_type!=2)
     BoxLib::Warning("WARNING: ILU smoother is best");
    pplp.query("use_mg_precond_in_mglib",use_mg_precond_in_mglib);
    pplp.query("use_bicgstab_in_mglib_pressure",
               use_bicgstab_in_mglib_pressure);
    pplp.query("use_bicgstab_in_mglib_diffusion",
	       use_bicgstab_in_mglib_diffusion);
    pplp.query("bottom_bottom_tol_factor",bottom_bottom_tol_factor);

    if (ParallelDescriptor::IOProcessor()) {
     std::cout << "smooth_type " << smooth_type << '\n';
     std::cout << "bottom_smooth_type " << bottom_smooth_type << '\n';
     std::cout << "use_mg_precond_in_mglib " <<use_mg_precond_in_mglib<<'\n';
     std::cout << "use_bicgstab_in_mglib_pressure " <<
	     use_bicgstab_in_mglib_pressure<<'\n';
     std::cout << "use_bicgstab_in_mglib_diffusion " <<
	     use_bicgstab_in_mglib_diffusion<<'\n';
     std::cout << "bottom_bottom_tol_factor " <<
       bottom_bottom_tol_factor<<'\n';
     for (int i=0;i<nmat;i++) {
      std::cout << "i= " << i << " FSI_flag " << FSI_flag[i] << '\n';
      std::cout << "i= " << i << " FSI_refine_factor " << 
	     FSI_refine_factor[i] << '\n';
      std::cout << "i= " << i << " FSI_bounding_box_ngrow " << 
	     FSI_bounding_box_ngrow[i] << '\n';
     }
     std::cout << "CTML_FSI_numsolids " << CTML_FSI_numsolids << '\n';

     std::cout << "invert_solid_levelset " << invert_solid_levelset << '\n';
     std::cout << "law_of_the_wall " << law_of_the_wall << '\n';
     std::cout << "adapt_quad_depth= " << adapt_quad_depth << '\n';
    }

    pp.get("adv_dir",adv_dir);
    pp.get("adv_vel",adv_vel);
    pp.get("rgasinlet",rgasinlet);
    pp.query("slipcoeff",slipcoeff);
    pp.get("vinletgas",vinletgas);

    pp.get("twall",twall);

    pp.query("use_StewartLay",use_StewartLay);

    if ((use_StewartLay==0)||(use_StewartLay==1)) {
     // do nothing
    } else
     BoxLib::Error("use_StewartLay invalid");

    pp.query("curv_stencil_height",curv_stencil_height);
    if (curv_stencil_height!=4)
     BoxLib::Error("must have curv_stencil_height==4");


    pp.query("bicgstab_max_num_outer_iter",bicgstab_max_num_outer_iter);
    pp.query("slope_limiter_option",slope_limiter_option);

    pp.query("EILE_flag",EILE_flag);

    if ((EILE_flag==0)|| // Sussman and Puckett
        (EILE_flag==1)|| // EILE
        (EILE_flag==2)|| // EI
        (EILE_flag==3)|| // LE
        (EILE_flag==-1)) { // Weymouth and Yue
     // do nothing
    } else
     BoxLib::Error("EILE_flag invalid");

    pp.query("unsplit_flag",unsplit_flag);

    if ((unsplit_flag==0)|| // directionally split
        (unsplit_flag==1)|| // unsplit everywhere
        (unsplit_flag==2)|| // unsplit in incompressible zones
        (unsplit_flag==3)) {//unsplit fluid cells neighboring prescribed solid 
     // do nothing
    } else
     BoxLib::Error("unsplit_flag invalid");

    num_species_var=0;

    pp.get("num_species_var",num_species_var);
    if ((num_species_var<0)||(num_species_var>MAX_NUM_SPECIES))
     BoxLib::Error("num species var invalid");

    massface_index=facespecies_index+num_species_var;
    vofface_index=massface_index+2*num_materials;
    ncphys=vofface_index+2*num_materials;

    MOFITERMAX=DEFAULT_MOFITERMAX;
    pp.query("MOFITERMAX",MOFITERMAX);
    if ((MOFITERMAX<0)||(MOFITERMAX>50))
     BoxLib::Error("mof iter max invalid in navierstokes");

    MOF_TURN_OFF_LS=0;  
    pp.query("MOF_TURN_OFF_LS",MOF_TURN_OFF_LS);
    if ((MOF_TURN_OFF_LS!=0)&&(MOF_TURN_OFF_LS!=1))
     BoxLib::Error("mof turn off ls invalid in navierstokes");

    MOF_DEBUG_RECON=0; 
    pp.query("MOF_DEBUG_RECON",MOF_DEBUG_RECON);
    if ((MOF_DEBUG_RECON!=0)&&(MOF_DEBUG_RECON!=1)&&
        (MOF_DEBUG_RECON!=2))
     BoxLib::Error("mof debug recon invalid in navierstokes");

    int fortran_max_num_materials=MAX_NUM_MATERIALS;

    num_state_base=SpeciesVar;  // den,Temperature
    num_state_material=SpeciesVar;  // den,Temperature
    num_state_material+=num_species_var;

    pp.query("make_interface_incomp",make_interface_incomp);
    if ((make_interface_incomp!=0)&&
        (make_interface_incomp!=1)&&
        (make_interface_incomp!=2))
     BoxLib::Error("make_interface_incomp invalid");

    advection_order.resize(nmat);
    density_advection_order.resize(nmat);
    for (int i=0;i<nmat;i++) {
     advection_order[i]=1;
     density_advection_order[i]=1;
    }
    pp.queryarr("advection_order",advection_order,0,nmat);
    for (int i=0;i<nmat;i++) {
     density_advection_order[i]=advection_order[i];
     if (!((advection_order[i]==1)||
           (advection_order[i]==2)))
      BoxLib::Error("advection_order invalid");
    }
    pp.queryarr("density_advection_order",density_advection_order,0,nmat);
    for (int i=0;i<nmat;i++) {
     if (!((density_advection_order[i]==1)||
           (density_advection_order[i]==2)))
      BoxLib::Error("density_advection_order invalid");
    }

    stiffPINF.resize(nmat);
    stiffCP.resize(nmat);
    stiffGAMMA.resize(nmat);

    DrhoDT.resize(nmat);
    DrhoDz.resize(nmat);
    override_density.resize(nmat);

    temperature_source_cen.resize(BL_SPACEDIM);
    temperature_source_rad.resize(BL_SPACEDIM);

    tempconst.resize(nmat);
    initial_temperature.resize(nmat);
    tempcutoff.resize(nmat);
    tempcutoffmax.resize(nmat);
    viscconst.resize(nmat);
    viscconst_eddy.resize(nmat);
    viscosity_state_model.resize(nmat);
    viscoelastic_model.resize(nmat);
    les_model.resize(nmat);
    viscconst_interface.resize(nten);
    speciesconst.resize((num_species_var+1)*nmat);
    speciesviscconst.resize((num_species_var+1)*nmat);
    speciesviscconst_interface.resize((num_species_var+1)*nten);
    for (int i=0;i<nten;i++) {
     viscconst_interface[i]=0.0;
     for (int j=0;j<=num_species_var;j++)
      speciesviscconst_interface[j*nten+i]=0.0;
    }
    pp.queryarr("viscconst_interface",viscconst_interface,0,nten);
    if (num_species_var>0)
     pp.queryarr("speciesviscconst_interface",
      speciesviscconst_interface,0,num_species_var*nten);

     // in: read_params

    species_evaporation_density.resize(num_species_var+1);
    for (int i=0;i<num_species_var+1;i++)
     species_evaporation_density[i]=0.0;

    if (num_species_var>0)
     pp.queryarr("species_evaporation_density",species_evaporation_density,
      0,num_species_var);

    spec_material_id.resize(num_species_var+1);
    for (int i=0;i<num_species_var+1;i++)
     spec_material_id[i]=0;

    if (num_species_var>0)
     pp.queryarr("spec_material_id",spec_material_id,0,num_species_var);
    
    vorterr.resize(nmat);
    pressure_error_cutoff.resize(nmat);
    temperature_error_cutoff.resize(nmat);

    recalesce_model_parameters.resize(3*nmat);

    microlayer_substrate.resize(nmat);
    microlayer_angle.resize(nmat);
    microlayer_size.resize(nmat);
    macrolayer_size.resize(nmat);
    max_contact_line_size.resize(nmat);
 
    microlayer_temperature_substrate.resize(nmat);

     // in: read_params
     
    cavitation_pressure.resize(nmat);
    cavitation_vapor_density.resize(nmat);
    cavitation_tension.resize(nmat);
    cavitation_species.resize(nmat);
    cavitation_model.resize(nmat);
    for (int i=0;i<nmat;i++) {
     cavitation_pressure[i]=0.0; 
     cavitation_vapor_density[i]=0.0; 
     cavitation_tension[i]=0.0; 
     cavitation_species[i]=0; // 1..num_species_var 
     cavitation_model[i]=0; 
    }
    pp.queryarr("cavitation_pressure",cavitation_pressure,0,nmat);
    pp.queryarr("cavitation_vapor_density",cavitation_vapor_density,0,nmat);
    pp.queryarr("cavitation_tension",cavitation_tension,0,nmat);
     // 1..num_species_var
    pp.queryarr("cavitation_species",cavitation_species,0,nmat);
    pp.queryarr("cavitation_model",cavitation_model,0,nmat);
 
     // in: read_params

    saturation_temp.resize(2*nten);
    nucleation_temp.resize(2*nten);
    nucleation_pressure.resize(2*nten);
    nucleation_pmg.resize(2*nten);
    nucleation_mach.resize(2*nten);
    latent_heat.resize(2*nten);
    reaction_rate.resize(2*nten);
    freezing_model.resize(2*nten);
    mass_fraction_id.resize(2*nten);
    distribute_from_target.resize(2*nten);
    tension.resize(nten);
    tension_slope.resize(nten);
    tension_T0.resize(nten);
    tension_min.resize(nten);
    prefreeze_tension.resize(nten);

     // (dir,side)  (1,1),(2,1),(3,1),(1,2),(2,2),(3,2)
    outflow_velocity_buffer_size.resize(2*BL_SPACEDIM);

    cap_wave_speed.resize(nten);

    prerecalesce_stiffCP.resize(nmat);
    prerecalesce_viscconst.resize(nmat);
    prerecalesce_heatviscconst.resize(nmat);

    for (int i=0;i<3*nmat;i++) { 
     recalesce_model_parameters[i]=0.0;
    }

    nucleation_period=0.0;
    nucleation_init_time=0.0;

    for (int i=0;i<nmat;i++) {
     microlayer_substrate[i]=0;
     microlayer_angle[i]=0.0;
     microlayer_size[i]=0.0;
     macrolayer_size[i]=0.0;
     max_contact_line_size[i]=0.0;
     microlayer_temperature_substrate[i]=0.0;
    }

    for (int i=0;i<nten;i++) { 
     saturation_temp[i]=0.0;
     saturation_temp[i+nten]=0.0;
     nucleation_temp[i]=0.0;
     nucleation_temp[i+nten]=0.0;
     nucleation_pressure[i]=0.0;
     nucleation_pmg[i]=0.0;
     nucleation_mach[i]=0.0;
     nucleation_pressure[i+nten]=0.0;
     nucleation_pmg[i+nten]=0.0;
     nucleation_mach[i+nten]=0.0;
     latent_heat[i]=0.0;
     reaction_rate[i]=0.0;
     latent_heat[i+nten]=0.0;
     reaction_rate[i+nten]=0.0;
     freezing_model[i]=0;
     freezing_model[i+nten]=0;
     mass_fraction_id[i]=0;
     mass_fraction_id[i+nten]=0;
     distribute_from_target[i]=0;
     distribute_from_target[i+nten]=0;
    } // i=0..nten-1

    density_floor.resize(nmat);
    density_floor_expansion.resize(nmat);
    for (int i=0;i<nmat;i++)
     density_floor[i]=0.0;
    pp.queryarr("density_floor",density_floor,0,nmat);
    density_ceiling.resize(nmat);
    density_ceiling_expansion.resize(nmat);
    for (int i=0;i<nmat;i++)
     density_ceiling[i]=1.0e+20;
    pp.queryarr("density_ceiling",density_ceiling,0,nmat);

    denconst.resize(nmat);
    pp.getarr("denconst",denconst,0,nmat);

    for (int i=0;i<nmat;i++) {
     if (density_ceiling[i]<=0.0) {
      BoxLib::Error("density_ceiling[i]<=0.0");
     } else if (density_ceiling[i]<=denconst[i]) {
      BoxLib::Error("density_ceiling[i]<=denconst[i]");
     }
     if (density_floor[i]<0.0) {
      BoxLib::Error("density_floor[i]<0.0");
     } else if (density_floor[i]==0.0) {
      // do nothing
     } else if (density_floor[i]>=denconst[i]) {
      BoxLib::Error("density_floor[i]>=denconst[i]");
     }
     density_ceiling_expansion[i]=denconst[i];
     density_floor_expansion[i]=denconst[i];
    } // i=0..nmat-1

    pp.queryarr("density_floor_expansion",density_floor_expansion,0,nmat);
    pp.queryarr("density_ceiling_expansion",density_ceiling_expansion,0,nmat);

    for (int i=0;i<nmat;i++) {
     if (density_ceiling_expansion[i]<=0.0) {
      BoxLib::Error("density_ceiling_expansion[i]<=0.0");
     } else if (density_ceiling_expansion[i]<denconst[i]) {
      BoxLib::Error("density_ceiling_expansion[i]<denconst[i]");
     }
     if (density_floor_expansion[i]<=0.0) {
      BoxLib::Error("density_floor_expansion[i]<=0.0");
     } else if (density_floor_expansion[i]>denconst[i]) {
      BoxLib::Error("density_floor_expansion[i]>denconst[i]");
     }
    } // i=0..nmat-1


    denconst_interface.resize(nten);
    for (int i=0;i<nten;i++) 
     denconst_interface[i]=0.0;
    pp.queryarr("denconst_interface",denconst_interface,0,nten);

    denconst_gravity.resize(nmat);
    for (int i=0;i<nmat;i++) 
     denconst_gravity[i]=1.0;
    pp.queryarr("denconst_gravity",denconst_gravity,0,nmat);

    pp.query("stokes_flow",stokes_flow);

    added_weight.resize(nmat);
    for (int i=0;i<nmat;i++) 
     added_weight[i]=1.0;
    pp.queryarr("added_weight",added_weight,0,nmat);

    for (int i=0;i<(num_species_var+1)*nmat;i++) {
     speciesconst[i]=0.0;
     speciesviscconst[i]=0.0;
    }

    for (int i=0;i<nmat;i++) {

     stiffPINF[i]=0.0;
     stiffCP[i]=4.1855e+7;
     stiffGAMMA[i]=0.0;

     tempcutoff[i]=1.0e-8;
     tempcutoffmax[i]=1.0e+99;
     DrhoDT[i]=0.0;
     DrhoDz[i]=0.0;
     override_density[i]=0;
     temperature_error_cutoff[i]=0.0;
    }

    pp.queryarr("tempcutoff",tempcutoff,0,nmat);
    pp.queryarr("tempcutoffmax",tempcutoffmax,0,nmat);

    pp.queryarr("stiffPINF",stiffPINF,0,nmat);
    pp.queryarr("stiffCP",stiffCP,0,nmat);
    pp.queryarr("stiffGAMMA",stiffGAMMA,0,nmat);

    pp.query("angular_velocity",angular_velocity);

    pp.query("constant_viscosity",constant_viscosity);

    pp.queryarr("DrhoDT",DrhoDT,0,nmat);
    pp.queryarr("DrhoDz",DrhoDz,0,nmat);
    pp.queryarr("override_density",override_density,0,nmat);

    pp.getarr("vorterr",vorterr,0,nmat);
    pp.query("pressure_error_flag",pressure_error_flag);
    pp.getarr("pressure_error_cutoff",pressure_error_cutoff,0,nmat);
    pp.queryarr("temperature_error_cutoff",temperature_error_cutoff,0,nmat);

    pp.query("temperature_source",temperature_source);
    pp.queryarr("temperature_source_cen",temperature_source_cen,0,BL_SPACEDIM);
    pp.queryarr("temperature_source_rad",temperature_source_rad,0,BL_SPACEDIM);

    pp.getarr("tempconst",tempconst,0,nmat);
    for (int i=0;i<nmat;i++)
     initial_temperature[i]=tempconst[i]; 
    pp.queryarr("initial_temperature",initial_temperature,0,nmat);
    pp.query("initial_temperature_diffuse_duration",
     initial_temperature_diffuse_duration);
    if (initial_temperature_diffuse_duration<0.0)
     BoxLib::Error("initial_temperature_diffuse_duration<0.0");

    pp.getarr("viscconst",viscconst,0,nmat);

    for (int i=0;i<nmat;i++)
     viscconst_eddy[i]=0.0;
    pp.queryarr("viscconst_eddy",viscconst_eddy,0,nmat);

    for (int i=0;i<nmat;i++)
     viscosity_state_model[i]=0;
    pp.queryarr("viscosity_state_model",viscosity_state_model,0,nmat);
    
    for (int i=0;i<nmat;i++)
     viscoelastic_model[i]=0;
    pp.queryarr("viscoelastic_model",viscoelastic_model,0,nmat);

    for (int i=0;i<nmat;i++)
     les_model[i]=0;
    pp.queryarr("les_model",les_model,0,nmat);

    heatviscconst.resize(nmat);
    heatviscconst_interface.resize(nten);
    pp.getarr("heatviscconst",heatviscconst,0,nmat);
    for (int i=0;i<nten;i++) 
     heatviscconst_interface[i]=0.0;
    pp.queryarr("heatviscconst_interface",heatviscconst_interface,0,nten);
    if (num_species_var>0) {
     pp.queryarr("speciesconst",speciesconst,0,num_species_var*nmat);
     pp.queryarr("speciesviscconst",speciesviscconst,0,num_species_var*nmat);
    }

    for (int i=0;i<nmat;i++) {
     prerecalesce_stiffCP[i]=stiffCP[i];
     prerecalesce_viscconst[i]=viscconst[i];
     prerecalesce_heatviscconst[i]=heatviscconst[i];
    }
    pp.queryarr("prerecalesce_viscconst",prerecalesce_viscconst,0,nmat);
    pp.queryarr("prerecalesce_heatviscconst",prerecalesce_heatviscconst,0,nmat);
    pp.queryarr("prerecalesce_stiffCP",prerecalesce_stiffCP,0,nmat);

    pp.query("conservative_tension_force",conservative_tension_force);

    pp.getarr("tension",tension,0,nten);
    for (int i=0;i<nten;i++) 
     prefreeze_tension[i]=tension[i];
    pp.queryarr("prefreeze_tension",prefreeze_tension,0,nten);
    for (int i=0;i<nten;i++) {
     tension_slope[i]=0.0;
     tension_T0[i]=293.0;
     tension_min[i]=0.0;
     cap_wave_speed[i]=0.0;
    }

    for (int i=0;i<2*BL_SPACEDIM;i++) {
     outflow_velocity_buffer_size[i]=0.0;
    }
    pp.queryarr("outflow_velocity_buffer_size",
      outflow_velocity_buffer_size,0,2*BL_SPACEDIM);

    pp.queryarr("tension_slope",tension_slope,0,nten);
    pp.queryarr("tension_T0",tension_T0,0,nten);
    pp.queryarr("tension_min",tension_min,0,nten);

    pp.queryarr("recalesce_model_parameters",recalesce_model_parameters,
       0,3*nmat);

    pp.queryarr("saturation_temp",saturation_temp,0,2*nten);

    pp.query("nucleation_period",nucleation_period);
    pp.query("nucleation_init_time",nucleation_init_time);

    pp.query("perturbation_on_restart",perturbation_on_restart);
    pp.query("perturbation_mode",perturbation_mode);
    pp.query("perturbation_eps_temp",perturbation_eps_temp);
    pp.query("perturbation_eps_vel",perturbation_eps_vel);
   
    pp.query("solidheat_flag",solidheat_flag);
    if ((solidheat_flag<0)||(solidheat_flag>2))
     BoxLib::Error("solidheat_flag invalid"); 
 
    pp.queryarr("microlayer_substrate",microlayer_substrate,0,nmat);
    pp.queryarr("microlayer_angle",microlayer_angle,0,nmat);
    pp.queryarr("microlayer_size",microlayer_size,0,nmat);
    pp.queryarr("macrolayer_size",macrolayer_size,0,nmat);
    pp.queryarr("max_contact_line_size",
                max_contact_line_size,0,nmat);
    pp.queryarr("microlayer_temperature_substrate",
     microlayer_temperature_substrate,0,nmat);
    for (int i=0;i<nmat;i++) {
     if (microlayer_temperature_substrate[i]<0.0)
      BoxLib::Error("microlayer_temperature_substrate[i]<0.0");
     if ((microlayer_substrate[i]<0)||
         (microlayer_substrate[i]>nmat))
      BoxLib::Error("microlayer_substrate invalid");
     if ((microlayer_angle[i]<0.0)|| 
         (microlayer_angle[i]>=Pi))
      BoxLib::Error("microlayer_angle invalid");
     if ((microlayer_size[i]<0.0)||
         (macrolayer_size[i]<microlayer_size[i]))
      BoxLib::Error("microlayer_size invalid");
     if (max_contact_line_size[i]<0.0)
      BoxLib::Error("max_contact_line_size invalid");
     if ((microlayer_size[i]>0.0)&&(solidheat_flag==0))
      BoxLib::Error("cannot have microlayer_size>0.0&&solidheat_flag==0");
    }  // i=0..nmat-1

    pp.queryarr("nucleation_temp",nucleation_temp,0,2*nten);
    pp.queryarr("nucleation_pressure",nucleation_pressure,0,2*nten);
    pp.queryarr("nucleation_pmg",nucleation_pmg,0,2*nten);
    pp.queryarr("nucleation_mach",nucleation_mach,0,2*nten);
    pp.queryarr("latent_heat",latent_heat,0,2*nten);
    pp.queryarr("reaction_rate",reaction_rate,0,2*nten);
    pp.queryarr("freezing_model",freezing_model,0,2*nten);
    pp.queryarr("mass_fraction_id",mass_fraction_id,0,2*nten);
    pp.queryarr("distribute_from_target",distribute_from_target,0,2*nten);

    for (int im=0;im<nmat;im++) {

     if ((override_density[im]!=0)&&
         (override_density[im]!=1)&&
         (override_density[im]!=2))
      BoxLib::Error("override_density invalid");
     if (DrhoDT[im]>0.0)
      BoxLib::Error("DrhoDT cannot be positive");
     if ((DrhoDz[im]!=-1.0)&&(DrhoDz[im]<0.0))
      BoxLib::Error("DrhoDz invalid"); 

     if (constant_viscosity==0) {
      // do nothing
     } else if (constant_viscosity==1) {
      if (fabs(viscconst[im]-viscconst[0])>1.0e-12)
       BoxLib::Warning("variable visc but constant_viscosity==1");
     } else
      BoxLib::Error("constant_viscosity invalid");

     if (material_type[im]==999) {
      if (viscconst[im]<=0.0)
       BoxLib::Error("solid cannot have 0 viscosity");
      if (ns_is_rigid(im)!=1)
       BoxLib::Error("ns_is_rigid invalid");
     } else if (material_type[im]==0) {
      if (ns_is_rigid(im)!=0)
       BoxLib::Error("ns_is_rigid invalid");
     } else if ((material_type[im]>0)&& 
                (material_type[im]<=MAX_NUM_EOS)) {
      if (ns_is_rigid(im)!=0)
       BoxLib::Error("ns_is_rigid invalid");
     } else {
      BoxLib::Error("material type invalid");
     }

    }  // im=0, im<nmat

    if (num_state_base!=2)
     BoxLib::Error("num_state_base invalid 10");

    for (int i=0;i<nten;i++) {
     if ((freezing_model[i]<0)||(freezing_model[i]>5))
      BoxLib::Error("freezing_model invalid in read_params (i)");
     if ((freezing_model[i+nten]<0)||(freezing_model[i+nten]>5))
      BoxLib::Error("freezing_model invalid in read_params (i+nten)");
     if ((distribute_from_target[i]<0)||(distribute_from_target[i]>1))
      BoxLib::Error("distribute_from_target invalid in read_params (i)");
     if ((distribute_from_target[i+nten]<0)||(distribute_from_target[i+nten]>1))
      BoxLib::Error("distribute_from_target invalid in read_params (i+nten)");
     if (mass_fraction_id[i]<0)
      BoxLib::Error("mass_fraction_id invalid in read_params (i)");
     if (mass_fraction_id[i+nten]<0)
      BoxLib::Error("mass_fraction_id invalid in read_params (i+nten)");
    }  // i=0..nten-1

    shock_timestep.resize(nmat);
    for (int i=0;i<nmat;i++) 
     shock_timestep[i]=0;
    pp.queryarr("shock_timestep",shock_timestep,0,nmat);

    int all_advective=1;

    for (int i=0;i<nmat;i++) {
     if (!((shock_timestep[i]==1)||(shock_timestep[i]==0)||
           (shock_timestep[i]==2)))
      BoxLib::Error("shock_timestep invalid");

     if (shock_timestep[i]==1)
      all_advective=0;
    }
    if ((cfl>1.0)&&(all_advective==1))
     BoxLib::Error("cfl should be less than or equal to 1");

    projection_pressure_scale=1.0;

    if (some_materials_compressible()==1) {
     projection_pressure_scale=1.0e+6;
    }

    pp.query("projection_pressure_scale",projection_pressure_scale);
    if (projection_pressure_scale<=0.0)
     BoxLib::Error("projection pressure scale invalid");

    projection_velocity_scale=sqrt(projection_pressure_scale);

    num_divu_outer_sweeps=1;
    if (some_materials_compressible()==1) {
     num_divu_outer_sweeps=2;
    } else if (some_materials_compressible()==0) {
     // do nothing
    } else {
     BoxLib::Error("some_materials_compressible invalid");
    }
    pp.query("num_divu_outer_sweeps",num_divu_outer_sweeps);
     
    prescribed_solid_scale.resize(nmat);
    for (int i=0;i<nmat;i++) 
     prescribed_solid_scale[i]=0.0;
    pp.queryarr("prescribed_solid_scale",prescribed_solid_scale,0,nmat);
    for (int i=0;i<nmat;i++) {
     if ((prescribed_solid_scale[i]>=0.0)&&
	 (prescribed_solid_scale[i]<0.5)) {
      // do nothing
     } else
      BoxLib::Error("prescribed_solid_scale[i] out of range");
    }
    pp.query("prescribed_solid_method",prescribed_solid_method);
    pp.query("min_prescribed_opt_iter",min_prescribed_opt_iter);

    pp.query("post_init_pressure_solve",post_init_pressure_solve);
    if ((post_init_pressure_solve<0)||(post_init_pressure_solve>1))
     BoxLib::Error("post_init_pressure_solve out of range");

    pp.query("solvability_projection",solvability_projection);
    if ((solvability_projection<0)||(solvability_projection>1))
     BoxLib::Error("solvability_projection out of range");

    pp.query("use_lsa",use_lsa);
    if ((use_lsa<0)||(use_lsa>1))
     BoxLib::Error("use_lsa out of range");
    pp.query("Uref",Uref);
    pp.query("Lref",Lref);
    if (Uref<0.0)
     BoxLib::Error("Uref invalid");
    if (Lref<0.0)
     BoxLib::Error("Lref invalid");

    pp.query("pgrad_dt_factor",pgrad_dt_factor);
    if (pgrad_dt_factor<1.0)
     BoxLib::Error("pgrad_dt_factor too small");

    pp.query("pressure_select_criterion",pressure_select_criterion);
    if ((pressure_select_criterion<0)||
        (pressure_select_criterion>2))
     BoxLib::Error("pressure_select_criterion invalid");

    temperature_primitive_variable.resize(nmat);
    elastic_time.resize(nmat);

    Carreau_alpha.resize(nmat);
    Carreau_beta.resize(nmat);
    Carreau_n.resize(nmat);
    Carreau_mu_inf.resize(nmat);

    polymer_factor.resize(nmat);

    pp.query("face_flag",face_flag);

    if ((face_flag==0)||(face_flag==1)) {
     // do nothing
    } else
     BoxLib::Error("face_flag invalid 1");

    for (int i=0;i<nmat;i++) {
     temperature_primitive_variable[i]=0;
     if (is_ice_matC(i)==1) {
      temperature_primitive_variable[i]=1;
     } else if (is_FSI_rigid_matC(i)==1) {
      temperature_primitive_variable[i]=1;
     } else if (CTML_FSI_matC(i)==1) {
      temperature_primitive_variable[i]=1;
     } else if (ns_is_rigid(i)==1) {
      temperature_primitive_variable[i]=1;
     } else if (visc_coef*viscconst[i]>0.0) {
      temperature_primitive_variable[i]=1;
     } else if (material_type[i]==0) {
      temperature_primitive_variable[i]=1;
     } else if (material_type[i]==999) {
      temperature_primitive_variable[i]=1;
     } else if (ns_is_rigid(i)==0) {
      // do nothing
     } else if (visc_coef*viscconst[i]==0.0) {
      // do nothing
     } else if (material_type[i]>0) {
      // do nothing
     } else {
      BoxLib::Error("parameter bust");
     }

     elastic_time[i]=0.0;

     Carreau_alpha[i]=1.0;
     Carreau_beta[i]=0.0;
     Carreau_n[i]=1.0;
     Carreau_mu_inf[i]=0.0;

     polymer_factor[i]=0.0;
    }  // i=0..nmat-1

    pp.queryarr("temperature_primitive_variable",
     temperature_primitive_variable,0,nmat);

    for (int i=0;i<nmat;i++) {

     if (temperature_primitive_variable[i]==0) {
      if (ns_is_rigid(i)==1) {
       BoxLib::Error("make temperature_primitive_variable=1 for solids");
      } else if (is_ice_matC(i)==1) {
       BoxLib::Error("make temperature_primitive_variable=1 for ice");
      } else if (is_FSI_rigid_matC(i)==1) {
       BoxLib::Error("make temperature_primitive_variable=1 for FSI_rigid");
      } else if (CTML_FSI_matC(i)==1) {
       BoxLib::Error("make temperature_primitive_variable=1 for CTML");
      } else if (visc_coef*viscconst[i]>0.0) {
       BoxLib::Error("make temperature_primitive_variable=1 for visc. fluids");
      } else if (material_type[i]==0) {
       BoxLib::Error("make temperature_primitive_variable=1 for incomp fluids");
      } else if (material_type[i]==999) {
       BoxLib::Error("make temperature_primitive_variable=1 for solids");
      } else if (ns_is_rigid(i)==0) {
       // do nothing
      } else if (visc_coef*viscconst[i]==0.0) {
       // do nothing
      } else if (material_type[i]>0) {
       // do nothing
      } else {
       BoxLib::Error("parameter bust");
      }
     } else if (temperature_primitive_variable[i]==1) {
      if ((ns_is_rigid(i)==0)&& 
          (is_ice_matC(i)==0)&&
          (is_FSI_rigid_matC(i)==0)&&
          (CTML_FSI_matC(i)==0)&&
          (visc_coef*viscconst[i]==0)&& 
          (material_type[i]>=1)&&
          (material_type[i]<=MAX_NUM_EOS)) {
       BoxLib::Error("make temperature_primitive_variable=1 for inv gases");
      }
     } else {
      BoxLib::Error("temperature_primitive_variable[i] invalid");
     }

    }  // i=0..nmat-1

    pp.queryarr("elastic_time",elastic_time,0,nmat);
    pp.queryarr("polymer_factor",polymer_factor,0,nmat);

    pp.queryarr("Carreau_alpha",Carreau_alpha,0,nmat);
    pp.queryarr("Carreau_beta",Carreau_beta,0,nmat);
    pp.queryarr("Carreau_n",Carreau_n,0,nmat);
    pp.queryarr("Carreau_mu_inf",Carreau_mu_inf,0,nmat);

    etaL.resize(nmat);
    etaP.resize(nmat);
    etaS.resize(nmat);
    concentration.resize(nmat);

    for (int i=0;i<nmat;i++) {

     if (Carreau_n[i]>1.0)
      BoxLib::Error("Carreau_n[i] invalid");
     if (Carreau_mu_inf[i]<0.0)
      BoxLib::Error("Carreau_mu_inf[i] invalid");

     if (viscosity_state_model[i]<0)
      BoxLib::Error("viscosity state model invalid");

     if ((viscoelastic_model[i]>=0)&&(viscoelastic_model[i]<=2)) {
      // do nothing
     } else
      BoxLib::Error("viscoelastic_model invalid");

     if (les_model[i]<0)
      BoxLib::Error("les model invalid");

     if ((elastic_time[i]<0.0)||(elastic_viscosity[i]<0.0))
      BoxLib::Error("elastic_time/elastic_viscosity invalid read_params");
     if (polymer_factor[i]<0.0)
      BoxLib::Error("polymer_factor invalid");

     if ((Carreau_beta[i]!=0.0)&&(visc_coef==0.0))
      BoxLib::Error("Cannot have Carreau_beta!=0 and visc_coef==0 ");

      // if first material, Carreau_beta==0 for first material,
      //  probtype==2, axis_dir>0, then 
      //  "call viscosity(axis_dir,visc(i,j),shear)"
      //  VISCOSITY=visc_coef * visc(i,j)
      // otherwise:
      //  if Carreau_beta==0, then
      //   VISCOSITY=visc_coef * viscconst
      //  else if (Carreau_beta>0) then
      //   if visco-elastic then
      //    VISCOSITY=visc_coef * ( etaL0-etaP0+ 
      //     etaP0*(1+(beta*gamma_dot)**alpha)**( (n-1)/alpha )  )
      //    =
      //    VISCOSITY=visc_coef * ( etaS+ 
      //     etaP0*(1+(beta*gamma_dot)**alpha)**( (n-1)/alpha )  )
      //
      //   else
      //    VISCOSITY=visc_coef * (
      //      mu_inf + (etaL0-mu_inf)* 
      //      (1+(beta*gamma_dot)**alpha)**((n-1)/alpha) )
      //
      // my "etaL0" is Mitsuhiro's eta_S(1+c_0)= viscconst
      // my "etaP0" is Mitsuhiro's c_0 eta_S   = elastic_viscosity
      // my "etaS" is Mitsuhiro's eta_S too.  = viscconst-elastic_viscosity
      // The coefficient for the viscoelastic force term is:
      //  visc_coef * 
      //  (VISCOSITY-viscconst+elastic_viscosity)/(elastic_time*(1-tr(A)/L^2))
      // =
      //  visc_coef * 
      //  (VISCOSITY-etaL0+etaP0)/(elastic_time*(1-tr(A)/L^2))
      // =
      //  visc_coef * 
      //  (VISCOSITY-etaS)/(elastic_time*(1-tr(A)/L^2))
      // =  (assume visc_coef==1)
      //  etaP0*
      //  (1+(beta*gamma_dot)**alpha)**((n-1)/alpha) 
      //
     etaL[i]=viscconst[i];  
     etaP[i]=elastic_viscosity[i]; // eta_{P0} in the JNNFM paper and above.
     etaS[i]=etaL[i]-etaP[i];  
     if (ParallelDescriptor::IOProcessor()) {
      std::cout << "for material " << i << '\n';

      std::cout << "etaL0=viscconst[i]=" << etaL[i] << '\n';

      std::cout << "Carreau_alpha=" << Carreau_alpha[i] << '\n';
      std::cout << "Carreau_beta=" << Carreau_beta[i] << '\n';
      std::cout << "Carreau_n=" << Carreau_n[i] << '\n';
      std::cout << "Carreau_mu_inf=" << Carreau_mu_inf[i] << '\n';
     } // io processor

     if ((num_materials_viscoelastic>=1)&&(num_materials_viscoelastic<=nmat)) {

      if (visc_coef<=0.0)
       BoxLib::Error("cannot have no viscosity if viscoelastic");

      if (ParallelDescriptor::IOProcessor()) {
       std::cout << "for material " << i << '\n';
       std::cout << "etaP0=elastic_viscosity=" << etaP[i] << '\n';
       std::cout << "etaS=etaL0-etaP0= " << etaS[i] << '\n';
       std::cout << "elastic_viscosity= " << elastic_viscosity[i] << '\n';
       std::cout << "elastic_time= " << elastic_time[i] << '\n';
      }

       // c0=etaP0/etaS=etaP0/(etaL0-etaP0)
      concentration[i]=0.0;
      if (etaL[i]-etaP[i]>0.0)
       concentration[i]=etaP[i]/(etaL[i]-etaP[i]);  

      if (ParallelDescriptor::IOProcessor()) {
       std::cout << "for material " << i << " c0=" << 
        concentration[i] << '\n';
      }

     } else if (num_materials_viscoelastic==0) {
      // do nothing
     } else
      BoxLib::Error("num_materials_viscoelastic  invalid");

    } // i

    if ((face_flag<0)||(face_flag>1))
     BoxLib::Error("face_flag invalid 2");

    pp.query("wait_time",wait_time);

    pp.get("advbot",advbot);
    pp.query("inflow_pressure",inflow_pressure);
    pp.query("outflow_pressure",outflow_pressure);

    pp.query( "multilevel_maxcycle",multilevel_maxcycle);

    ParmParse ppmac("mac");
    pp.query( "viscous_maxiter",viscous_maxiter);
    if ((viscous_maxiter<1)||(viscous_maxiter>2)) 
     BoxLib::Error("viscous_maxiter should be 1 or 2");

    ppmac.query( "mac_abs_tol",mac_abs_tol);
    ppmac.query( "visc_abs_tol",visc_abs_tol);
    thermal_abs_tol=visc_abs_tol;
    ppmac.query( "thermal_abs_tol",thermal_abs_tol);
    pp.query( "minimum_relative_error",minimum_relative_error);
    pp.query( "diffusion_minimum_relative_error",
      diffusion_minimum_relative_error);
    if (mac_abs_tol<=0.0)
     BoxLib::Error("mac_abs_tol must be positive");
    if (visc_abs_tol<=0.0)
     BoxLib::Error("visc_abs_tol must be positive");
    if (thermal_abs_tol<=0.0)
     BoxLib::Error("thermal_abs_tol must be positive");

    extra_circle_parameters(
       xblob2,yblob2,zblob2,radblob2,
       xblob3,yblob3,zblob3,radblob3,
       xblob4,yblob4,zblob4,radblob4,
       xblob5,yblob5,zblob5,radblob5,
       xblob6,yblob6,zblob6,radblob6,
       xblob7,yblob7,zblob7,radblob7,
       xblob8,yblob8,zblob8,radblob8,
       xblob9,yblob9,zblob9,radblob9,
       xblob10,yblob10,zblob10,radblob10 );

    pp.query("xblob2",xblob2);
    pp.query("yblob2",yblob2);
    pp.query("zblob2",zblob2);
    pp.query("radblob2",radblob2);

    pp.query("xblob3",xblob3);
    pp.query("yblob3",yblob3);
    pp.query("zblob3",zblob3);
    pp.query("radblob3",radblob3);

    pp.query("xblob4",xblob4);
    pp.query("yblob4",yblob4);
    pp.query("zblob4",zblob4);
    pp.query("radblob4",radblob4);

    pp.query("xblob5",xblob5);
    pp.query("yblob5",yblob5);
    pp.query("zblob5",zblob5);
    pp.query("radblob5",radblob5);

    pp.query("xblob6",xblob6);
    pp.query("yblob6",yblob6);
    pp.query("zblob6",zblob6);
    pp.query("radblob6",radblob6);

    pp.query("xblob7",xblob7);
    pp.query("yblob7",yblob7);
    pp.query("zblob7",zblob7);
    pp.query("radblob7",radblob7);

    pp.query("xblob8",xblob8);
    pp.query("yblob8",yblob8);
    pp.query("zblob8",zblob8);
    pp.query("radblob8",radblob8);

    pp.query("xblob9",xblob9);
    pp.query("yblob9",yblob9);
    pp.query("zblob9",zblob9);
    pp.query("radblob9",radblob9);

    pp.query("xblob10",xblob10);
    pp.query("yblob10",yblob10);
    pp.query("zblob10",zblob10);
    pp.query("radblob10",radblob10);

    xactive=0.0;
    yactive=0.0;
    zactive=0.0;
    ractive=0.0;
    ractivex=0.0;
    ractivey=0.0;
    ractivez=0.0;

    pp.query("xactive",xactive);
    pp.query("yactive",yactive);
    pp.query("zactive",zactive);
    pp.query("ractive",ractive);
    if (ractive>0.0) {
     ractivex=ractive;
     ractivey=ractive;
     ractivez=ractive;
    }
    pp.query("ractivex",ractivex);
    pp.query("ractivey",ractivey);
    pp.query("ractivez",ractivez);

     // 0 - MGPCG  1-PCG 2-MG
    pp.query("project_solver_type",project_solver_type);
    pp.query("initial_cg_cycles",initial_cg_cycles);

    pp.query("initial_project_cycles",initial_project_cycles);
    if (initial_project_cycles<1)
     BoxLib::Error("must do at least 1 jacobi sweep at the beginning");
    initial_viscosity_cycles=initial_project_cycles;
    pp.query("initial_viscosity_cycles",initial_viscosity_cycles);
    if (initial_viscosity_cycles<1)
     BoxLib::Error("must do at least 1 jacobi sweep at the beginning (visc)");
    initial_thermal_cycles=initial_viscosity_cycles;
    pp.query("initial_thermal_cycles",initial_thermal_cycles);
    if (initial_thermal_cycles<1)
     BoxLib::Error("must do at least 1 jacobi sweep at the beginning (therm)");

    if ((project_solver_type<0)||(project_solver_type>2))
     BoxLib::Error("project_solver_type invalid");

    prescribe_temperature_outflow=0;
    pp.query("prescribe_temperature_outflow",prescribe_temperature_outflow);
    if ((prescribe_temperature_outflow<0)||
        (prescribe_temperature_outflow>3))
     BoxLib::Error("prescribe_temperature_outflow invalid");

    pp.query("diffusionface_flag",diffusionface_flag);
    if ((diffusionface_flag<0)||(diffusionface_flag>1))
     BoxLib::Error("diffusionface_flag invalid"); 

    pp.query("elasticface_flag",elasticface_flag);
    if ((elasticface_flag<0)||(elasticface_flag>1))
     BoxLib::Error("elasticface_flag invalid"); 

    pp.query("temperatureface_flag",temperatureface_flag);
    if ((temperatureface_flag!=0)&&
        (temperatureface_flag!=1))
     BoxLib::Error("temperatureface_flag invalid"); 

    is_phasechange=0;
    is_cavitation=0;
    is_cavitation_mixture_model=0;
    for (int i=0;i<nmat;i++) {
     int ispec=cavitation_species[i];
     if (ispec==0) {
      // do nothing
     } else if ((ispec>=1)&&(ispec<=num_species_var)) {
      is_cavitation_mixture_model=1;
     } else
      BoxLib::Error("ispec invalid");
    }
    for (int i=0;i<2*nten;i++) {
     if ((nucleation_pressure[i]!=0.0)&&
         (nucleation_pmg[i]!=0.0)&&
         (nucleation_mach[i]!=0.0)) {
      is_cavitation=1;
     }
     if (latent_heat[i]!=0.0) {
      is_phasechange=1;
      if ((freezing_model[i]==0)||   // Stefan model for phase change
          (freezing_model[i]==5)) {  // Stefan model for evap/cond.
       if (temperatureface_flag!=0)
        BoxLib::Error("must have temperatureface_flag==0");
      } else if ((freezing_model[i]==1)||
                 (freezing_model[i]==2)||  // hydrate
                 (freezing_model[i]==3)||
                 (freezing_model[i]==4)) { // Tannasawa model
       if (temperatureface_flag!=1)
        BoxLib::Error("must have temperatureface_flag==1");
      } else
       BoxLib::Error("freezing_model[i] invalid");

     } // latent_heat<>0
    }  // i=0;i<2*nten

    hydrate_flag=0;
    for (int i=0;i<2*nten;i++) {
     if (latent_heat[i]!=0.0)
      if (freezing_model[i]==2)
       hydrate_flag=1;
    } // i

    for (int im=1;im<=nmat;im++) {
     for (int im_opp=im+1;im_opp<=nmat;im_opp++) {
      for (int ireverse=0;ireverse<=1;ireverse++) {
       if ((im>nmat)||(im_opp>nmat))
        BoxLib::Error("im or im_opp bust 200cpp");
       int iten;
       get_iten_cpp(im,im_opp,iten,nmat);
       if ((iten<1)||(iten>nten))
        BoxLib::Error("iten invalid");
       int im_source=im;
       int im_dest=im_opp;
       if (ireverse==1) {
        im_source=im_opp;
        im_dest=im;
       }

       int indexEXP=iten+ireverse*nten-1;

       Real LL=latent_heat[indexEXP];
       if (freezing_model[indexEXP]==5) {
        if (LL!=0.0) {
         int massfrac_id=mass_fraction_id[indexEXP];
         if ((massfrac_id<1)||(massfrac_id>num_species_var))
          BoxLib::Error("massfrac_id invalid");
         if (LL>0.0)  //evaporation
          spec_material_id[massfrac_id-1]=im_source;
         else if (LL<0.0)  // condensation
          spec_material_id[massfrac_id-1]=im_dest;
         else
          BoxLib::Error("LL invalid");
        } // LL!=0.0
       } // if (freezing_model[indexEXP]==5)
      } // ireverse
     } //im_opp
    } // im

    truncate_volume_fractions.resize(nmat);
    for (int i=0;i<nmat;i++) {
     if (FSI_flag[i]==0) // fluid
      truncate_volume_fractions[i]=1;
     else if (is_ice_matC(i)==1) // ice
      truncate_volume_fractions[i]=1;
     else if (FSI_flag[i]==1) // prescribed PROB.F90 solid
      truncate_volume_fractions[i]=0;
     else if (FSI_flag[i]==2) // prescribed sci_clsvof.F90 solid
      truncate_volume_fractions[i]=0;
     else if (FSI_flag[i]==4) // FSI CTML solid
      truncate_volume_fractions[i]=0;
     else if (is_FSI_rigid_matC(i)==1) // FSI rigid solid
      truncate_volume_fractions[i]=0;
     else
      BoxLib::Error("FSI_flag invalid");
    }

    pp.queryarr("truncate_volume_fractions",truncate_volume_fractions,0,nmat);
    for (int i=0;i<nmat;i++) {
     if ((truncate_volume_fractions[i]<0)||
         (truncate_volume_fractions[i]>1))
      BoxLib::Error("truncate_volume_fractions invalid");
    }

    pp.query("truncate_thickness",truncate_thickness);
    if (truncate_thickness<1.0)
     BoxLib::Error("truncate_thickness too small");

    pp.query("normal_probe_size",normal_probe_size);
    if (normal_probe_size!=1)
     BoxLib::Error("normal_probe_size invalid");
   
    pp.query("ngrow_distance",ngrow_distance);
    if (ngrow_distance!=4)
     BoxLib::Error("ngrow_distance invalid");

    pp.query("ngrowFSI",ngrowFSI);
    if (ngrowFSI!=3)
     BoxLib::Error("ngrowFSI invalid");

    pp.query("ngrow_expansion",ngrow_expansion);
    if (ngrow_expansion!=2)
     BoxLib::Error("ngrow_expansion invalid");

    mof_error_ordering=0; 
    pp.query("mof_error_ordering",mof_error_ordering);
    if ((mof_error_ordering!=0)&&
        (mof_error_ordering!=1))
     BoxLib::Error("mof_error_ordering invalid");
    mof_ordering.resize(nmat);

    mof_ordering_override(mof_ordering,
      nmat,probtype,
      axis_dir,radblob3,
      radblob4,radblob7,
      mof_error_ordering,
      FSI_flag);

    pp.queryarr("mof_ordering",mof_ordering,0,nmat);
    for (int i=0;i<nmat;i++) {
     if ((mof_ordering[i]<0)||
         (mof_ordering[i]>nmat+1))
      BoxLib::Error("mof_ordering invalid");
    }


    for (int i=0;i<nmat;i++) {

       if (visc_coef*viscconst[i]<0.0) {
        BoxLib::Error("viscosity coefficients invalid");
       } else if (visc_coef*viscconst[i]==0.0) {
        // do nothing
       } else if (visc_coef*viscconst[i]>0.0) {
        // do nothing
       } else
        BoxLib::Error("viscconst bust");
 
       if (heatviscconst[i]==0.0) {
        // do nothing
       } else if (heatviscconst[i]>0.0) {
        // do nothing
       } else
        BoxLib::Error("heatviscconst invalid");

       for (int imspec=0;imspec<num_species_var;imspec++) {
        if (speciesviscconst[imspec*nmat+i]==0.0) {
         // do nothing
        } else if (speciesviscconst[imspec*nmat+i]>0.0) {
         // do nothing
        } else
         BoxLib::Error("speciesviscconst invalid");
       } // imspec

    } // i=0..nmat-1

    if (ParallelDescriptor::IOProcessor()) {

     std::cout << "temperature_source=" << temperature_source << '\n';
     for (int i=0;i<BL_SPACEDIM;i++) {
      std::cout << "i,temperature_source_cen=" << i << ' ' <<
         temperature_source_cen[i] << '\n';
      std::cout << "i,temperature_source_rad=" << i << ' ' <<
         temperature_source_rad[i] << '\n';
     }
 
     for (int i=0;i<nten;i++) {
      std::cout << "i= " << i << " denconst_interface "  << 
        denconst_interface[i] << '\n';
      std::cout << "i= " << i << " viscconst_interface "  << 
        viscconst_interface[i] << '\n';
      std::cout << "i= " << i << " heatviscconst_interface "  << 
        heatviscconst_interface[i] << '\n';
      for (int j=0;j<num_species_var;j++) {
       std::cout << "i= " << i << " j= " << j << 
         " speciesviscconst_interface "  << 
         speciesviscconst_interface[j*nten+i] << '\n';
      }

     } // i=0 ... nten-1

     for (int j=0;j<num_species_var;j++) {
      std::cout << " j= " << j << 
         " species_evaporation_density "  <<
         species_evaporation_density[j] << '\n';
     }  

     std::cout << "CTML_FSI_numsolids " << CTML_FSI_numsolids << '\n';
     std::cout << "CTML_force_model " << CTML_force_model << '\n';

     std::cout << "mof_error_ordering " << 
      mof_error_ordering << '\n';

     std::cout << "ngrow_make_distance= " << 
      ngrow_make_distance << '\n';
     std::cout << "ngrow_distance= " << 
      ngrow_distance << '\n';
     std::cout << "ngrowFSI= " << 
      ngrowFSI << '\n';
     std::cout << "ngrow_expansion= " << 
      ngrow_expansion << '\n';
     std::cout << "normal_probe_size= " << 
      normal_probe_size << '\n';
     std::cout << "prescribe_temperature_outflow= " << 
      prescribe_temperature_outflow << '\n';
     std::cout << "solidheat_flag= " << solidheat_flag << '\n';
     std::cout << "diffusionface_flag= " << diffusionface_flag << '\n';
     std::cout << "elasticface_flag= " << elasticface_flag << '\n';
     std::cout << "temperatureface_flag= " << temperatureface_flag << '\n';
     std::cout << "truncate_thickness= " << truncate_thickness << '\n';
     std::cout << "face_flag= " << face_flag << '\n';
     std::cout << "nparts (im_solid_map.size())= " << 
      im_solid_map.size() << '\n';
     std::cout << "Solid_State_Type= " << Solid_State_Type << '\n';
     std::cout << "Tensor_Type= " << Tensor_Type << '\n';
     std::cout << "NUM_STATE_TYPE= " << NUM_STATE_TYPE << '\n';

     std::cout << "angular_velocity= " << angular_velocity << '\n';

     std::cout << "constant_viscosity= " << constant_viscosity << '\n';

     std::cout << "pressure_error_flag=" << pressure_error_flag << '\n';

     std::cout << "initial_temperature_diffuse_duration=" << 
      initial_temperature_diffuse_duration << '\n';

     std::cout << "make_interface_incomp " << make_interface_incomp << '\n';
 
     for (int i=0;i<nmat;i++) {
      std::cout << "mof_ordering i= " << i << ' ' <<
        mof_ordering[i] << '\n';
      std::cout << "truncate_volume_fractions i= " << i << ' ' <<
        truncate_volume_fractions[i] << '\n';
      std::cout << "viscosity_state_model i= " << i << ' ' <<
        viscosity_state_model[i] << '\n';
      std::cout << "viscoelastic_model i= " << i << ' ' <<
        viscoelastic_model[i] << '\n';
      std::cout << "les_model i= " << i << ' ' <<
        les_model[i] << '\n';
      std::cout << "temperature_primitive_variable i= " << i << ' ' <<
       temperature_primitive_variable[i] << '\n';
      std::cout << "shock_timestep i=" << i << " " << 
          shock_timestep[i] << '\n';
      std::cout << "material_type i=" << i << " " << material_type[i] << '\n';
      std::cout << "pressure_error_cutoff i=" << i << " " << 
        pressure_error_cutoff[i] << '\n';
      std::cout << "vorterr i=" << i << " " << 
        vorterr[i] << '\n';
      std::cout << "temperature_error_cutoff i=" << i << " " << 
        temperature_error_cutoff[i] << '\n';
      std::cout << "stiffPINF i=" << i << " " << stiffPINF[i] << '\n';
      std::cout << "stiffCP i=" << i << " " << stiffCP[i] << '\n';
      std::cout << "stiffGAMMA i=" << i << " " << stiffGAMMA[i] << '\n';
      std::cout << "added_weight i=" << i << " " << added_weight[i] << '\n';
      std::cout << "denconst_gravity i=" << i << " " << 
         denconst_gravity[i] << '\n';
      std::cout << "denconst i=" << i << " " << denconst[i] << '\n';
      std::cout << "density_floor i=" << i << " " << density_floor[i] << '\n';
      std::cout << "density_ceiling i="<<i<<" "<< density_ceiling[i] << '\n';
      std::cout << "density_floor_expansion i=" << i << " " << 
        density_floor_expansion[i] << '\n';
      std::cout << "density_ceiling_expansion i="<<i<<" "<< 
        density_ceiling_expansion[i] << '\n';
      std::cout << "tempconst i=" << i << " " << tempconst[i] << '\n';
      std::cout << "initial_temperature i=" << i << " " << 
        initial_temperature[i] << '\n';
      std::cout << "tempcutoff i=" << i << " " << tempcutoff[i] << '\n';
      std::cout << "tempcutoffmax i=" << i << " " << tempcutoffmax[i] << '\n';
      std::cout << "DrhoDT i=" << i << " " << DrhoDT[i] << '\n';
      std::cout << "DrhoDz i=" << i << " " << DrhoDz[i] << '\n';
      std::cout << "override_density i=" << i << " " << 
         override_density[i] << '\n';
      std::cout << "viscconst i=" << i << "  " << viscconst[i] << '\n';
      std::cout << "viscconst_eddy i=" <<i<<"  "<<viscconst_eddy[i]<<'\n';
      std::cout << "heatviscconst i=" << i << "  " << 
          heatviscconst[i] << '\n';
      std::cout << "advection_order i=" << i << "  " << 
          advection_order[i] << '\n';
      std::cout << "density_advection_order i=" << i << "  " << 
          density_advection_order[i] << '\n';
      std::cout << "prerecalesce_viscconst i=" << i << "  " << 
         prerecalesce_viscconst[i] << '\n';
      std::cout << "prerecalesce_heatviscconst i=" << i << "  " << 
         prerecalesce_heatviscconst[i] << '\n';
      std::cout << "prerecalesce_stiffCP i=" << i << "  " << 
         prerecalesce_stiffCP[i] << '\n';
     }  // i=0,..,nmat

     for (int i=0;i<num_species_var*nmat;i++) {
      std::cout << "speciesviscconst i=" << i << "  " << 
          speciesviscconst[i] << '\n';
      std::cout << "speciesconst i=" << i << "  " << 
          speciesconst[i] << '\n';
     }

     std::cout << "stokes_flow= " << stokes_flow << '\n';

     std::cout << "is_phasechange= " << is_phasechange << '\n';
     std::cout << "is_cavitation= " << is_cavitation << '\n';

     for (int i=0;i<3*nmat;i++) {
      std::cout << "recalesce_model_parameters i=" << i << "  " << 
       recalesce_model_parameters[i] << '\n';
     }

     std::cout << "perturbation_on_restart " << perturbation_on_restart << '\n';
     std::cout << "perturbation_mode " << perturbation_mode << '\n';
     std::cout << "perturbation_eps_temp " << perturbation_eps_temp << '\n';
     std::cout << "perturbation_eps_vel " << perturbation_eps_vel << '\n';

     std::cout << "custom_nucleation_model " << 
       custom_nucleation_model << '\n';

     std::cout << "conservative_tension_force " << 
       conservative_tension_force << '\n';
     std::cout << "FD_curv_select " << FD_curv_select << '\n';
     std::cout << "FD_curv_interp " << FD_curv_interp << '\n';

     std::cout << "hydrate flag " << hydrate_flag << '\n';
     std::cout << "nucleation_period= " << nucleation_period << '\n';
     std::cout << "nucleation_init_time= " << nucleation_init_time << '\n';
     std::cout << "n_sites= " << n_sites << '\n';
     if (n_sites>0) {
      for (int i=0;i<pos_sites.size();i++) {
       std::cout << "i, pos_sites= " << i << ' ' << pos_sites[i] << '\n';
      }
     }
    
     for (int i=0;i<nmat;i++) {
      std::cout << "microlayer_substrate i=" << i << "  " << 
       microlayer_substrate[i] << '\n';
      std::cout << "microlayer_angle i=" << i << "  " << 
       microlayer_angle[i] << '\n';
      std::cout << "microlayer_size i=" << i << "  " << 
       microlayer_size[i] << '\n';
      std::cout << "macrolayer_size i=" << i << "  " << 
       macrolayer_size[i] << '\n';
      std::cout << "max_contact_line_size i=" << i << "  " << 
       max_contact_line_size[i] << '\n';
      std::cout << "microlayer_temperature_substrate i=" << i << "  " << 
       microlayer_temperature_substrate[i] << '\n';
     } // i=0..nmat-1

     for (int i=0;i<num_species_var;i++)
      std::cout << "spec_material_id i= " << i << " " <<
       spec_material_id[i] << '\n';

     for (int i=0;i<2*BL_SPACEDIM;i++) {
      std::cout << "i= " << i << " outflow_velocity_buffer_size= " <<
       outflow_velocity_buffer_size[i] << '\n';
     }
 
     for (int i=0;i<nten;i++) {
      std::cout << "saturation_temp i=" << i << "  " << 
       saturation_temp[i] << '\n';
      std::cout << "saturation_temp i+nten=" << i+nten << "  " << 
       saturation_temp[i+nten] << '\n';

      std::cout << "nucleation_temp i=" << i << "  " << 
       nucleation_temp[i] << '\n';
      std::cout << "nucleation_temp i+nten=" << i+nten << "  " << 
       nucleation_temp[i+nten] << '\n';

      std::cout << "nucleation_pressure i=" << i << "  " << 
       nucleation_pressure[i] << '\n';
      std::cout << "nucleation_pressure i+nten=" << i+nten << "  " << 
       nucleation_pressure[i+nten] << '\n';

      std::cout << "nucleation_pmg i=" << i << "  " << 
       nucleation_pmg[i] << '\n';
      std::cout << "nucleation_pmg i+nten=" << i+nten << "  " << 
       nucleation_pmg[i+nten] << '\n';

      std::cout << "nucleation_mach i=" << i << "  " << 
       nucleation_mach[i] << '\n';
      std::cout << "nucleation_mach i+nten=" << i+nten << "  " << 
       nucleation_mach[i+nten] << '\n';

      std::cout << "latent_heat i=" << i << "  " << 
       latent_heat[i] << '\n';
      std::cout << "latent_heat i+nten=" << i+nten << "  " << 
       latent_heat[i+nten] << '\n';

      std::cout << "reaction_rate i=" << i << "  " << 
       reaction_rate[i] << '\n';
      std::cout << "reaction_rate i+nten=" << i+nten << "  " << 
       reaction_rate[i+nten] << '\n';

      std::cout << "freezing_model i=" << i << "  " << 
       freezing_model[i] << '\n';
      std::cout << "freezing_model i+nten=" << i+nten << "  " << 
       freezing_model[i+nten] << '\n';
      std::cout << "mass_fraction_id i=" << i << "  " << 
       mass_fraction_id[i] << '\n';
      std::cout << "mass_fraction_id i+nten=" << i+nten << "  " << 
       mass_fraction_id[i+nten] << '\n';
      std::cout << "distribute_from_target i=" << i << "  " << 
       distribute_from_target[i] << '\n';
      std::cout << "distribute_from_target i+nten=" << i+nten << "  " << 
       distribute_from_target[i+nten] << '\n';


      std::cout << "tension i=" << i << "  " << tension[i] << '\n';
      std::cout << "tension_slope i=" << i << "  " << tension_slope[i] << '\n';
      std::cout << "tension_T0 i=" << i << "  " << tension_T0[i] << '\n';
      std::cout << "tension_min i=" << i << "  " << tension_min[i] << '\n';
      std::cout << "initial cap_wave_speed i=" << i << "  " << 
        cap_wave_speed[i] << '\n';
      std::cout << "prefreeze_tension i=" << i << "  " << 
       prefreeze_tension[i] << '\n';
     }  // i=0..nten-1

     for (int i=0;i<nmat;i++) {
      std::cout << "cavitation_pressure i=" << i << "  " << 
       cavitation_pressure[i] << '\n';
      std::cout << "cavitation_vapor_density i=" << i << "  " << 
       cavitation_vapor_density[i] << '\n';
      std::cout << "cavitation_tension i=" << i << "  " << 
       cavitation_tension[i] << '\n';
      std::cout << "cavitation_species i=" << i << "  " << 
       cavitation_species[i] << '\n';
      std::cout << "cavitation_model i=" << i << "  " << 
       cavitation_model[i] << '\n';
     } // i=0..nmat-1

     std::cout << "Uref " << Uref << '\n';
     std::cout << "Lref " << Lref << '\n';
     std::cout << "use_lsa " << use_lsa << '\n';
     std::cout << "pgrad_dt_factor " << pgrad_dt_factor << '\n';
     std::cout << "pressure_select_criterion " << 
       pressure_select_criterion << '\n';

     std::cout << "num_materials_viscoelastic " << 
        num_materials_viscoelastic << '\n';
     std::cout << "num_species_var " << num_species_var << '\n';
     std::cout << "num_materials " << num_materials << '\n';
     std::cout << "num_materials_vel " << num_materials_vel << '\n';
     std::cout << "num_materials_scalar_solve " << 
      num_materials_scalar_solve << '\n';
     std::cout << "fortran_max_num_materials " << 
       fortran_max_num_materials << '\n';
     std::cout << "MOFITERMAX= " << MOFITERMAX << '\n';
     std::cout << "MOF_DEBUG_RECON= " << MOF_DEBUG_RECON << '\n';
     std::cout << "MOF_TURN_OFF_LS= " << MOF_TURN_OFF_LS << '\n';

     std::cout << "post_init_pressure_solve " << 
       post_init_pressure_solve << '\n';

     std::cout << "solvability_projection " << solvability_projection << '\n';

     std::cout << "prescribed_solid_method= " <<
	   prescribed_solid_method << '\n';
     std::cout << "min_prescribed_opt_iter= " <<
	   min_prescribed_opt_iter << '\n';

     for (int i=0;i<nmat;i++) {
      std::cout << "i=" << i << " prescribed_solid_scale= " <<
	   prescribed_solid_scale[i] << '\n';
     }

     std::cout << "curv_stencil_height " << curv_stencil_height << '\n';
     std::cout << "use_StewartLay " << use_StewartLay << '\n';

     std::cout << "projection_pressure_scale " << 
       projection_pressure_scale << '\n';
     std::cout << "projection_velocity_scale " << 
       projection_velocity_scale << '\n';

     int init_snan=FArrayBox::get_init_snan();
     std::cout << "init_snan= " << init_snan << '\n';
     int do_initval=FArrayBox::get_do_initval();
     std::cout << "do_initval= " << do_initval << '\n';
     Real initval=FArrayBox::get_initval();
     std::cout << "initval= " << initval << '\n';


     std::cout << "bicgstab_max_num_outer_iter " << 
       bicgstab_max_num_outer_iter << '\n';
     std::cout << "slope_limiter_option " << slope_limiter_option << '\n';
     std::cout << "slipcoeff " << slipcoeff << '\n';

     std::cout << "EILE_flag " << EILE_flag << '\n';
     std::cout << "unsplit_flag " << unsplit_flag << '\n';

     std::cout << "ractive " << ractive << '\n';
     std::cout << "ractivex " << ractivex << '\n';
     std::cout << "ractivey " << ractivey << '\n';
     std::cout << "ractivez " << ractivez << '\n';
     std::cout << "wait_time " << wait_time << '\n';
     std::cout << "multilevel_maxcycle " << multilevel_maxcycle << '\n';

     std::cout << "mac.mac_abs_tol " <<mac_abs_tol<< '\n';
     std::cout << "mac.visc_abs_tol " <<visc_abs_tol<< '\n';
     std::cout << "mac.thermal_abs_tol " <<thermal_abs_tol<< '\n';
     std::cout << "viscous_maxiter " <<viscous_maxiter<< '\n';
     std::cout << "project_solver_type " <<project_solver_type<< '\n';
     std::cout << "initial_cg_cycles " <<initial_cg_cycles<< '\n';
     std::cout << "initial_project_cycles " <<initial_project_cycles<< '\n';
     std::cout << "initial_viscosity_cycles " <<initial_viscosity_cycles<< '\n';
     std::cout << "initial_thermal_cycles " <<initial_thermal_cycles<< '\n';
     std::cout << "visual_tessellate_vfrac " << visual_tessellate_vfrac << '\n';
     std::cout << "visual_revolve " << visual_revolve << '\n';
     std::cout << "visual_option " << visual_option << '\n';

     std::cout << "visual_compare " << visual_compare << '\n';
     for (int dir=0;dir<BL_SPACEDIM;dir++) {
      std::cout << "dir,visual_ncell " << dir << ' ' << 
       visual_ncell[dir] << '\n';
     }
     std::cout << "change_max=" << change_max << '\n';
     std::cout << "change_max_init=" << change_max_init << '\n';
     std::cout << "fixed_dt=" << fixed_dt << '\n';
     std::cout << "fixed_dt_init=" << fixed_dt_init << '\n';
     std::cout << "fixed_dt_velocity=" << fixed_dt_velocity << '\n';
     std::cout << "min_velocity_for_dt=" << min_velocity_for_dt << '\n';
     std::cout << "dt_max=" << dt_max << '\n';
     std::cout << "minimum_relative_error=" << minimum_relative_error << '\n';
     std::cout << "diffusion_minimum_relative_error=" << 
      diffusion_minimum_relative_error << '\n';
     std::cout << "num_divu_outer_sweeps=" << num_divu_outer_sweeps << '\n';

     std::cout << "ns_max_level= " << ns_max_level << '\n';
     for (int ilev=0;ilev<ns_max_grid_size.size();ilev++) {
      std::cout << "ilev, max_grid_size " << ilev << ' ' <<
       ns_max_grid_size[ilev] << '\n';
     }
     for (int ilev=0;ilev<ns_max_level;ilev++) {
      std::cout << "ilev, ns_n_error_buf " << ilev << ' ' <<
       ns_n_error_buf[ilev] << '\n';
     }
    }  // if IO processor

    if (some_materials_compressible()==1) {
     if (num_divu_outer_sweeps<2)
      BoxLib::Warning("WARNING:divu_outer_sweeps>=2 for comp materials");
     if (face_flag==0) {
      if ((make_interface_incomp==0)||
          (make_interface_incomp==1)||
          (make_interface_incomp==2)) {
       // do nothing
      } else 
       BoxLib::Error("make_interface_incomp invalid 1");
     } else if (face_flag==1) {
      if ((make_interface_incomp==1)||
          (make_interface_incomp==2)) {
       // do nothing
      } else
       BoxLib::Error("make_interface_incomp invalid 2");
     } else
      BoxLib::Error("face_flag invalid");
    } else if (some_materials_compressible()==0) {
     // do nothing
    } else
     BoxLib::Error("compressible flag bust");

} // subroutine read_params()


NavierStokes::NavierStokes ()
{
    Geometry_setup();
}

// constructor
NavierStokes::NavierStokes (Amr&            papa,
                            int             lev,
                            const Geometry& level_geom,
                            const BoxArray& bl,
                            const DistributionMapping& dmap,
                            Real            time)
    :
    AmrLevel(papa,lev,level_geom,bl,dmap,time)
{
    Geometry_setup();
}

NavierStokes::~NavierStokes ()
{
    Geometry_cleanup();

    for (int i=0;i<MAX_NUM_LOCAL_MF;i++)
     if (localMF_grow[i]>=0) {
      std::cout << "i= " << i << " localMF_grow= " <<
       localMF_grow[i] << '\n';
      BoxLib::Error("forgot to delete localMF variables");
     }

}

int NavierStokes::ns_is_rigid(int im) {

 if ((im<0)|(im>=num_materials))
  BoxLib::Error("im invalid50");

 int local_flag=-1;

 if ((FSI_flag[im]==0)|| // fluid
     (is_FSI_rigid_matC(im)==1)|| // FSI PROB.F90 rigid solid
     (is_ice_matC(im)==1)) { 
  local_flag=0;
 } else if ((FSI_flag[im]==1)|| // prescribed PROB.F90 rigid solid
            (FSI_flag[im]==2)|| // prescribed sci_clsvof.F90 rigid solid
            (FSI_flag[im]==4)) { // FSI CTML solid
  local_flag=1;
 } else
  BoxLib::Error("FSI_flag invalid");

 return local_flag;

} // subroutine ns_is_rigid

// getState_list needs scomp,ncomp
void
NavierStokes::get_mm_scomp_solver(
  int num_materials_combine,
  int project_option,
  int& state_index,
  Array<int>& scomp,
  Array<int>& ncomp,
  int& ncomp_check) {

 int nmat=num_materials;

 int nsolve=1;
 int nlist=1;

 if ((project_option==0)||
     (project_option==13)||  // FSI_material_exists (1st project)
     (project_option==1)) { // pressure
  nsolve=1;
  nlist=1;
  if (num_materials_combine!=num_materials_vel)
   BoxLib::Error("num_materials_combine invalid");
 } else if ((project_option==10)||  // divu pressure
            (project_option==11)||  // FSI_material_exists (2nd project)
	    (project_option==12)) { // pressure extension
  nsolve=1;
  nlist=1;
  if (num_materials_combine!=num_materials_vel)
   BoxLib::Error("num_materials_combine invalid");
 } else if (project_option==2) { // temperature
  nsolve=1;
  nlist=num_materials_combine;
 } else if ((project_option>=100)&&
            (project_option<100+num_species_var)) { // species
  nsolve=1;
  nlist=num_materials_combine;
 } else if (project_option==3) { // viscosity
  nsolve=BL_SPACEDIM;
  nlist=1;
  if (num_materials_combine!=num_materials_vel)
   BoxLib::Error("num_materials_combine invalid");
 } else
  BoxLib::Error("project_option invalid1");

 if (num_materials_vel!=1)
  BoxLib::Error("num_materials_vel invalid");

 if ((num_materials_combine!=1)&&
     (num_materials_combine!=nmat)) 
  BoxLib::Error("num_materials_combine invalid");

 scomp.resize(nlist);
 ncomp.resize(nlist);

 int nsolveMM=nsolve*num_materials_combine;

 if ((project_option==0)||
     (project_option==1)||
     (project_option==13)|| //FSI_material_exists (1st project)
     (project_option==12)) { // pressure extrapolation
 
  scomp[0]=num_materials_vel*BL_SPACEDIM;
  ncomp[0]=nsolveMM; 
  state_index=State_Type;

 } else if ((project_option==10)||  // divu pressure
            (project_option==11)) { // FSI_material_exists (2nd project);
	                            // divu pressure is independent var.

  scomp[0]=0;
  ncomp[0]=nsolveMM; 
  state_index=DIV_Type;

 } else if (project_option==2) { // temperature

  // u,v,w,p,den1,T1,...
  for (int im=0;im<nlist;im++) {
   scomp[im]=num_materials_vel*(BL_SPACEDIM+1)+
     im*num_state_material+1;
   ncomp[im]=1;
  }
  state_index=State_Type;

 } else if (project_option==3) { // viscosity

  scomp[0]=0;
  ncomp[0]=nsolveMM; 
  state_index=State_Type;

 } else if ((project_option>=100)&&
            (project_option<100+num_species_var)) { // species

  for (int im=0;im<nlist;im++) {
   scomp[im]=num_materials_vel*(BL_SPACEDIM+1)+
     im*num_state_material+num_state_base+project_option-100;
   ncomp[im]=1;
  }
  state_index=State_Type;

 } else
  BoxLib::Error("project_option invalid get_mm_scomp_solver");

 ncomp_check=0;
 for (int im=0;im<nlist;im++)
  ncomp_check+=ncomp[im];

 if (ncomp_check!=nsolveMM)
  BoxLib::Error("ncomp_check invalid");

} // get_mm_scomp_solver

void
NavierStokes::zero_independent_vel(int project_option,int idx,int nsolve) {

 int finest_level=parent->finestLevel();
 if ((level<0)||(level>finest_level))
  BoxLib::Error("level corrupt");

 int nmat=num_materials;

 if ((nsolve!=1)&&(nsolve!=BL_SPACEDIM))
  BoxLib::Error("nsolve invalid36");

 int num_materials_face=num_materials_vel;

 if ((project_option==0)||
     (project_option==1)||
     (project_option==10)||
     (project_option==11)|| // FSI_material_exists (2nd project)
     (project_option==13)|| // FSI_material_exists (1st project)
     (project_option==12)||
     (project_option==3)) {  // viscosity
  if (num_materials_face!=1)
   BoxLib::Error("num_materials_face invalid");
 } else if ((project_option==2)||  // thermal diffusion
            ((project_option>=100)&&
             (project_option<100+num_species_var))) {
  num_materials_face=num_materials_scalar_solve;
 } else
  BoxLib::Error("project_option invalid2");

 if (num_materials_vel!=1)
  BoxLib::Error("num_materials_vel invalid");

 if ((num_materials_face!=1)&&
     (num_materials_face!=nmat))
  BoxLib::Error("num_materials_face invalid");

 int nsolveMM_FACE=nsolve*num_materials_face;
 if (num_materials_face==1) {
  // do nothing
 } else if (num_materials_face==nmat) { 
  nsolveMM_FACE*=2;
 } else
  BoxLib::Error("num_materials_face invalid");

 for (int dir=0;dir<BL_SPACEDIM;dir++) {
  if (localMF[idx+dir]->nComp()!=nsolveMM_FACE)
   BoxLib::Error("localMF[idx+dir] has invalid ncomp");
  setVal_localMF(idx+dir,0.0,0,nsolveMM_FACE,0);
 } // dir

} // subroutine zero_independent_vel

// u,v,w,p,den1,T1,...,den2,T2,...
void
NavierStokes::zero_independent_variable(int project_option,int nsolve) {

 if (num_state_base!=2) 
  BoxLib::Error("num_state_base invalid");

 if ((nsolve!=1)&&
     (nsolve!=BL_SPACEDIM))
  BoxLib::Error("nsolve invalid36");

 int nmat=num_materials;

 int num_materials_face=num_materials_vel;

 if ((project_option==0)||
     (project_option==1)||
     (project_option==10)||
     (project_option==11)|| // FSI_material_exists (2nd project)
     (project_option==13)|| // FSI_material_exists (1st project)
     (project_option==12)||
     (project_option==3)) {  // viscosity
  if (num_materials_face!=1)
   BoxLib::Error("num_materials_face invalid");
 } else if ((project_option==2)||  // thermal diffusion
            ((project_option>=100)&&
             (project_option<100+num_species_var))) {
  num_materials_face=num_materials_scalar_solve;
 } else
  BoxLib::Error("project_option invalid3");

 if (num_materials_vel!=1)
  BoxLib::Error("num_materials_vel invalid");

 if ((num_materials_face!=1)&&
     (num_materials_face!=nmat))
  BoxLib::Error("num_materials_face invalid");

 int nsolveMM=nsolve*num_materials_face;

 int finest_level=parent->finestLevel();
 if ((level<0)||(level>finest_level))
  BoxLib::Error("level corrupt");

 Array<int> scomp;
 Array<int> ncomp;
 int ncomp_check;
 int state_index;
 get_mm_scomp_solver(
   num_materials_face,
   project_option,
   state_index,
   scomp,
   ncomp,
   ncomp_check);

 if (ncomp_check!=nsolveMM)
  BoxLib::Error("nsolveMM invalid 2732");

 MultiFab& S_new = get_new_data(state_index,slab_step+1);
 for (int icomp=0;icomp<scomp.size();icomp++) 
  S_new.setVal(0.0,scomp[icomp],ncomp[icomp],0);

} // zero_independent_variable

void
NavierStokes::init_regrid_history() {

    is_first_step_after_regrid = 0;
    old_intersect_new          = grids;

}

void
NavierStokes::restart (Amr&          papa,
                       std::istream& is) {

    AmrLevel::restart(papa,is);
    init_regrid_history();

}

//
// new_time=time  old_time=time-dt
//
void
NavierStokes::setTimeLevel (Real time,Real& dt)
{
 int nstate=state.size();
 if (nstate!=NUM_STATE_TYPE)
  BoxLib::Error("nstate invalid");
 for (int k=0;k<nstate;k++) 
  state[k].setTimeLevel(time,dt);
}


void NavierStokes::debug_ngrow(int idxMF,int ngrow,int counter) {

 if ((idxMF<0)||(idxMF>=MAX_NUM_LOCAL_MF))
  BoxLib::Error("idxMF invalid");

 if (1==0) {
  std::cout << "full check of localMF integrity \n";

  for (int i=0;i<MAX_NUM_LOCAL_MF;i++) {
   if (localMF_grow[i]>=0) {
    MultiFab* mf_temp=localMF[i];
    if (! mf_temp->ok()) {
     BoxLib::Error("! mf_temp->ok()");
    }
   } else if (localMF_grow[i]==-1) {
    if (localMF[i]==0) {
     // do nothing
    } else {
     std::cout << "level = " << level << '\n';
     std::cout << "i = " << i << '\n';
     BoxLib::Error("localMF[i] invalid");
    }
   } else {
    BoxLib::Error("localMF_grow[i] invalid");
   }
  } // i=0 ... MAX_NUM_LOCAL_MF-1   
 } // full check of localMF integrity

 MultiFab* mf=localMF[idxMF];
 int mfgrow=localMF_grow[idxMF];

 if (mfgrow<ngrow) {
  std::cout << "counter= " << counter << '\n';
  std::cout << "idxMF= " << idxMF << '\n';
  std::cout << "mfgrow= " << mfgrow << " expected grow= " <<
   ngrow << '\n';
 }

 if (! mf->ok()) {
  std::cout << "counter= " << counter << '\n';
  std::cout << "idxMF= " << idxMF << '\n';
  BoxLib::Error("mf not ok");
 } else if ((mf->nGrow()<ngrow)||(mfgrow<ngrow)) {
  std::cout << "counter= " << counter << '\n';
  std::cout << "idxMF= " << idxMF << '\n';
  std::cout << "mf->ngrow= " << mf->nGrow() << " expected grow= " <<
   ngrow << '\n';
  std::cout << "mfgrow= " << mfgrow << " expected grow= " <<
   ngrow << '\n';
  BoxLib::Error("grow invalid in debug_ngrow");
 }

} // subroutine debug_ngrow

int NavierStokes::some_materials_compressible() {

 int comp_flag=0;
 int nmat=num_materials;
 for (int im=0;im<nmat;im++) {
  int imat=material_type[im];
  if (imat==999) {
    // do nothing
  } else if (imat==0) {
    // do nothing
  } else if ((imat>=1)&&(imat<=MAX_NUM_EOS)) {
   comp_flag=1;
  } else
   BoxLib::Error("material type invalid");
 }
 return comp_flag;
}

// number of levels including the current that are "valid"
int NavierStokes::NSnumLevels() {

 int numLevelsMAX = 1024;

 int lv = numLevelsMAX;
    //
    // The routine `falls through' since coarsening and refining
    // a unit box does not yield the initial box.
    //
 const BoxArray& bs = grids;
 int ng=grids.size();

 for (int i = 0; i < ng; ++i) {
  int llv = 0;
  Box tmp = bs[i];
  for (;;) {
   Box ctmp  = tmp;   ctmp.coarsen(2);
   Box rctmp = ctmp; rctmp.refine(2);
   if (tmp != rctmp || ctmp.numPts() == 1)
    break;
   llv++;
   tmp = ctmp;
  }
  if (lv >= llv)
   lv = llv;
 }

 return lv+1; // Including coarsest.

} // function int NavierStokes::NSnumLevels()

int NavierStokes::do_FSI() {

 int nmat=num_materials;
 int local_do_FSI=0;
 for (int im=0;im<nmat;im++) {
  if ((FSI_flag[im]==2)||   // prescribed sci_clsvof.F90 rigid material 
      (FSI_flag[im]==4)) {  // FSI CTML sci_clsvof.F90 material
   local_do_FSI=1;
  } else if ((FSI_flag[im]==0)|| // fluid
             (FSI_flag[im]==1)|| // prescribed PROB.F90 rigid material
	     (is_FSI_rigid_matC(im)==1)|| // FSI PROB.F90 rigid material
             (is_ice_matC(im)==1)) { // FSI ice rigid material
   // do nothing
  } else
   BoxLib::Error("FSI_flag invalid");
 } // im=0..nmat-1

 return local_do_FSI;

}  // do_FSI()


int NavierStokes::is_ice_matC(int im) {

 int nmat=num_materials;
 int local_is_ice=0;
 if ((im>=0)&&(im<nmat)) {
  if (FSI_flag[im]==3) {  
   local_is_ice=1;
  } else if ((FSI_flag[im]==0)||  // fluid
             (FSI_flag[im]==1)||  // prescribed PROB.F90 rigid material
             (FSI_flag[im]==2)||  // prescribed sci_clsvof.F90 rigid material
             (FSI_flag[im]==4)||  // FSI CTML sci_clsvof.F90 material
	     (FSI_flag[im]==5)) { // FSI PROB.F90 rigid material
   // do nothing
  } else
   BoxLib::Error("FSI_flag invalid");
 } else
  BoxLib::Error("im invalid");

 return local_is_ice;

}  // is_ice_matC()


int NavierStokes::FSI_material_exists() {

 int local_flag=0;
 int nmat=num_materials;

 for (int im=0;im<nmat;im++) {
  if ((is_ice_matC(im)==0)&&
      (is_FSI_rigid_matC(im)==0)) {
   // do nothing
  } else if ((is_ice_matC(im)==1)||
             (is_FSI_rigid_matC(im)==1)) {
   local_flag=1;
  } else
   BoxLib::Error("is_ice_matC or is_FSI_rigid_matC invalid");
 } // im=0..nmat-1
 return local_flag;

}  // FSI_material_exists()


int NavierStokes::is_FSI_rigid_matC(int im) {

 int nmat=num_materials;
 int local_is_FSI_rigid=0;
 if ((im>=0)&&(im<nmat)) {
  if (FSI_flag[im]==5) {  // FSI PROB.F90 rigid material
   local_is_FSI_rigid=1;
  } else if ((FSI_flag[im]==0)||  // fluid
             (FSI_flag[im]==1)||  // prescribed PROB.F90 rigid material
             (FSI_flag[im]==2)||  // prescribed sci_clsvof.F90 rigid material
             (FSI_flag[im]==4)||  // FSI CTML sci_clsvof.F90 material
             (FSI_flag[im]==3)) { // ice
   // do nothing
  } else
   BoxLib::Error("FSI_flag invalid");
 } else
  BoxLib::Error("im invalid");

 return local_is_FSI_rigid;

}  // is_FSI_rigid_matC()

int NavierStokes::is_singular_coeff(int im) {

 int nmat=num_materials;
 int local_is_singular_coeff=0;
 if ((im>=0)&&(im<nmat)) {
  if (FSI_flag[im]==5) {  // FSI PROB.F90 rigid material
   local_is_singular_coeff=1;
  } else if (FSI_flag[im]==1) { // prescribed PROB.F90 rigid material
   local_is_singular_coeff=1;
  } else if (FSI_flag[im]==2) { // prescribed sci_clsvof.F90 rigid material
   local_is_singular_coeff=1;
  } else if (FSI_flag[im]==0) { // fluid
   local_is_singular_coeff=0;
  } else if (FSI_flag[im]==3) { // ice
   local_is_singular_coeff=1;
  } else if (FSI_flag[im]==4) { // FSI CTML sci_clsvof.F90 material
   local_is_singular_coeff=0;
  } else
   BoxLib::Error("FSI_flag invalid");
 } else
  BoxLib::Error("im invalid");

 return local_is_singular_coeff;

}  // is_singular_coeff()


int NavierStokes::CTML_FSI_flagC() {

 int nmat=num_materials;
 int local_CTML_FSI_flag=0;
 for (int im=0;im<nmat;im++) {
  if (FSI_flag[im]==4) {  // FSI CTML sci_clsvof.F90 
#ifdef MVAHABFSI
   local_CTML_FSI_flag=1;
#else
   BoxLib::Error("CTML(C): define MEHDI_VAHAB_FSI in GNUmakefile");
#endif
  } else if ((FSI_flag[im]==0)||  // fluid
             (FSI_flag[im]==1)||  // prescribed PROB.F90 rigid material
             (FSI_flag[im]==2)||  // prescribed sci_clsvof.F90 rigid material
             (is_FSI_rigid_matC(im)==1)||  // FSI PROB.F90 rigid material
             (is_ice_matC(im)==1)) { // FSI ice material
   // do nothing
  } else
   BoxLib::Error("FSI_flag invalid");
 } // im=0..nmat-1

 return local_CTML_FSI_flag;

}  // CTML_FSI_flagC()


int NavierStokes::CTML_FSI_matC(int im) {

 int nmat=num_materials;
 int local_CTML_FSI_flag=0;
 if ((im>=0)&&(im<nmat)) {
  if (FSI_flag[im]==4) {  // FSI CTML sci_clsvof.F90
#ifdef MVAHABFSI
   local_CTML_FSI_flag=1;
#else
   BoxLib::Error("CTML(C): define MEHDI_VAHAB_FSI in GNUmakefile");
#endif
  } else if ((FSI_flag[im]==0)||  // fluid material
             (FSI_flag[im]==1)||  // prescribed PROB.F90 rigid material
             (FSI_flag[im]==2)||  // prescribed sci_clsvof.F90 rigid material
             (is_FSI_rigid_matC(im)==1)||  // FSI PROB.F90 rigid material
             (is_ice_matC(im)==1)) { // FSI ice material
   // do nothing
  } else
   BoxLib::Error("FSI_flag invalid");
 } else
  BoxLib::Error("im invalid51");

 return local_CTML_FSI_flag;

}  // CTML_FSI_matC(int im)



// passes tile information to sci_clsvof.F90 so that Lagrangian
// elements can be distributed amongst the tiles.
// called from FSI_make_distance and ns_header_msg_level 
//  (FSI_operation==4, FSI_sub_operation==0)
// time is used just in case the actual node position depends on time.
// i.e. for finding target of characteristic given the foot.
void NavierStokes::create_fortran_grid_struct(Real time,Real dt) {

 if (do_FSI()!=1)
  BoxLib::Error("do_FSI()!=1");

 const int max_level = parent->maxLevel();
 int finest_level=parent->finestLevel();
 if ((level<0)||(level>max_level))
  BoxLib::Error("level invalid in create_fortran_grid_struct");

 Real problo[BL_SPACEDIM];
 Real probhi[BL_SPACEDIM];
 for (int dir=0;dir<BL_SPACEDIM;dir++) {
  problo[dir]=Geometry::ProbLo(dir);
  probhi[dir]=Geometry::ProbHi(dir);
 }

 bool use_tiling=ns_tiling;

 MultiFab& S_new=get_new_data(State_Type,slab_step+1);

 int nmat=num_materials;
 const Real* dx = geom.CellSize();

 Real dx_maxlevel[BL_SPACEDIM];
 for (int dir=0;dir<BL_SPACEDIM;dir++)
  dx_maxlevel[dir]=dx[dir];
 for (int ilev=level+1;ilev<=max_level;ilev++) 
  for (int dir=0;dir<BL_SPACEDIM;dir++)
   dx_maxlevel[dir]/=2.0;

  // this will store information about the grids stored
  // on this processor.
 Array<int> tilelo_array;
 Array<int> tilehi_array;
 Array<int> gridno_array;
 Array<Real> xlo_array;
 Array< int > num_tiles_on_thread_proc;
 Array< int > num_tiles_on_thread_proc_check;
 int max_num_tiles_on_thread_proc=0;
 int tile_dim=0;

 num_tiles_on_thread_proc.resize(thread_class::nthreads);
 num_tiles_on_thread_proc_check.resize(thread_class::nthreads);
 for (int tid=0;tid<thread_class::nthreads;tid++) {
  num_tiles_on_thread_proc[tid]=0;
  num_tiles_on_thread_proc_check[tid]=0;
 }

 int num_grids_on_level=grids.size();
 int num_grids_on_level_check=0;
 int num_grids_on_level_proc=0;
 for (MFIter mfi(S_new,false); mfi.isValid(); ++mfi) {
  BL_ASSERT(grids[mfi.index()] == mfi.validbox());
  const int gridno = mfi.index();
  if ((gridno>=0)&&(gridno<num_grids_on_level)) {
   num_grids_on_level_proc++;
   num_grids_on_level_check++;
  } else
   BoxLib::Error("gridno invalid");
 } // mfi
 ParallelDescriptor::Barrier();
 ParallelDescriptor::ReduceIntSum(num_grids_on_level_check);

 if ((num_grids_on_level_proc<0)||
     (num_grids_on_level_proc>num_grids_on_level)) {
  std::cout << "num_grids_on_level_proc= " << num_grids_on_level_proc << '\n';
  std::cout << "num_grids_on_level= " << num_grids_on_level << '\n';
  std::cout << "num_grids_on_level_check= " << 
    num_grids_on_level_check << '\n';
  std::cout << "level= " << level << '\n';
  BoxLib::Error("num_grids_on_level_proc invalid");
 }

 if (num_grids_on_level_check!=num_grids_on_level) {
  std::cout << "num_grids_on_level_proc= " << num_grids_on_level_proc << '\n';
  std::cout << "num_grids_on_level= " << num_grids_on_level << '\n';
  std::cout << "num_grids_on_level_check= " << 
    num_grids_on_level_check << '\n';
  std::cout << "level= " << level << '\n';
  BoxLib::Error("num_grids_on_level_check invalid");
 }

 for (int grid_sweep=0;grid_sweep<2;grid_sweep++) {

  if (grid_sweep==0) {
   // do nothing
  } else if (grid_sweep==1) {

   for (int tid=0;tid<thread_class::nthreads;tid++) {

    if (num_tiles_on_thread_proc[tid]!=
	num_tiles_on_thread_proc_check[tid])
     BoxLib::Error("num_tiles_on_thread_proc[tid] failed check");

    if (num_grids_on_level_proc==0) {
     if (num_tiles_on_thread_proc[tid]!=0)
      BoxLib::Error("num_tiles_on_thread_proc[tid] invalid");
     if (max_num_tiles_on_thread_proc!=0)
      BoxLib::Error("max_num_tiles_on_thread_proc invalid");
    } else if (num_grids_on_level_proc>0) {

     if (num_tiles_on_thread_proc[tid]>max_num_tiles_on_thread_proc)
      max_num_tiles_on_thread_proc=num_tiles_on_thread_proc[tid];

    } else
     BoxLib::Error("num_grids_on_level_proc invalid");

   } // tid=0..thread_class::nthreads-1

   tile_dim=thread_class::nthreads*max_num_tiles_on_thread_proc;

   if (num_grids_on_level_proc==0) {
    if (tile_dim!=0)
     BoxLib::Error("tile_dim invalid");
    int tile_dim_virtual=1;
    tilelo_array.resize(tile_dim_virtual*BL_SPACEDIM);   
    tilehi_array.resize(tile_dim_virtual*BL_SPACEDIM);   
    gridno_array.resize(tile_dim_virtual);   // gridno for a given tile
    xlo_array.resize(tile_dim_virtual*BL_SPACEDIM);   
    for (int tid=0;tid<thread_class::nthreads;tid++) {
     if (num_tiles_on_thread_proc[tid]!=0)
      BoxLib::Error("num_tiles_on_thread_proc[tid] invalid");
    }
   } else if (num_grids_on_level_proc>0) {

    if ((tile_dim>=1)&&(tile_dim>=num_grids_on_level_proc)) {
     tilelo_array.resize(tile_dim*BL_SPACEDIM);   
     tilehi_array.resize(tile_dim*BL_SPACEDIM);   
     gridno_array.resize(tile_dim);   // gridno for a given tile
     xlo_array.resize(tile_dim*BL_SPACEDIM);   
    } else
     BoxLib::Error("tile_dim invalid");
   } else
    BoxLib::Error("num_grids_on_level_proc invalid");

  } else
   BoxLib::Error("grid_sweep invalid"); 

  if ((grid_sweep==0)||(grid_sweep==1)) {
   for (int tid=0;tid<thread_class::nthreads;tid++) {
    num_tiles_on_thread_proc[tid]=0;
   }
  } else
   BoxLib::Error("grid_sweep invalid");

#ifdef _OPENMP
#pragma omp parallel
#endif
{
  for (MFIter mfi(S_new,use_tiling); mfi.isValid(); ++mfi) {
   BL_ASSERT(grids[mfi.index()] == mfi.validbox());

   int tid=ns_thread();
   if ((tid<0)||(tid>=thread_class::nthreads))
    BoxLib::Error("tid invalid");
 
   if (grid_sweep==0) {
    // do nothing
   } else if (grid_sweep==1) {
    const int gridno = mfi.index();
    const Box& tilegrid = mfi.tilebox();
    const int* tilelo=tilegrid.loVect();
    const int* tilehi=tilegrid.hiVect();
    const Real* xlo = grid_loc[gridno].lo();

    if (num_grids_on_level_proc==0) {
     // do nothing
    } else if (num_grids_on_level_proc>0) {

     if (max_num_tiles_on_thread_proc>0) {
      int ibase=max_num_tiles_on_thread_proc*BL_SPACEDIM*tid+
       BL_SPACEDIM*num_tiles_on_thread_proc[tid];
      for (int dir=0;dir<BL_SPACEDIM;dir++) {
       tilelo_array[ibase+dir]=tilelo[dir]; 
       tilehi_array[ibase+dir]=tilehi[dir]; 
       xlo_array[ibase+dir]=xlo[dir]; 
      } // dir=0..sdim-1
      ibase=max_num_tiles_on_thread_proc*tid+
       num_tiles_on_thread_proc[tid];

      if ((gridno>=0)&&(gridno<num_grids_on_level)) {
       gridno_array[ibase]=gridno;
      } else
       BoxLib::Error("gridno invalid");
     } else
      BoxLib::Error("max_num_tiles_on_thread_proc invalid");
    } else
     BoxLib::Error("num_grids_on_level_proc invalid");
   } else
    BoxLib::Error("grid_sweep invalid");

   num_tiles_on_thread_proc[tid]++;
   if (grid_sweep==0) {
    // check nothing
   } else if (grid_sweep==1) {
    if (num_tiles_on_thread_proc[tid]>max_num_tiles_on_thread_proc)
     BoxLib::Error("num_tiles_on_thread_proc[tid] invalid");
   } else
    BoxLib::Error("grid_sweep invalid");

  } // mfi
}//omp
  ParallelDescriptor::Barrier();

  for (int tid=0;tid<thread_class::nthreads;tid++) {
   if (grid_sweep==0) {
    num_tiles_on_thread_proc_check[tid]=num_tiles_on_thread_proc[tid];
   } else if (grid_sweep==1) {
    if (num_tiles_on_thread_proc_check[tid]!=
	num_tiles_on_thread_proc[tid])
     BoxLib::Error("num_tiles_on_thread_proc[tid] failed check");
   } else
    BoxLib::Error("grid_sweep invalid");
  }

 } // grid_sweep=0..1

 if (num_grids_on_level_proc==0) {
  if (max_num_tiles_on_thread_proc!=0)
   BoxLib::Error("max_num_tiles_on_thread_proc invalid");
 } else if (num_grids_on_level_proc>0) {
  if (max_num_tiles_on_thread_proc>0) {
   // do nothing
  } else
   BoxLib::Error("max_num_tiles_on_thread_proc invalid");
 } else
  BoxLib::Error("num_grids_on_level_proc invalid"); 

 int nparts=im_solid_map.size();
 if ((nparts<1)||(nparts>=nmat))
  BoxLib::Error("nparts invalid");

 FORT_FILLCONTAINER(
  &level,
  &finest_level,
  &max_level,
  &time,
  &dt,
  tilelo_array.dataPtr(),
  tilehi_array.dataPtr(),
  xlo_array.dataPtr(),
  dx,
  dx_maxlevel,
  &num_grids_on_level,
  &num_grids_on_level_proc,
  gridno_array.dataPtr(),
  num_tiles_on_thread_proc.dataPtr(),
  &thread_class::nthreads,
  &max_num_tiles_on_thread_proc,
  &tile_dim, 
  &nmat,
  &nparts,
  im_solid_map.dataPtr(),
  problo,
  probhi);

} // end subroutine create_fortran_grid_struct

// called from:
//  NavierStokes::prescribe_solid_geometryALL
//  NavierStokes::do_the_advance
//  NavierStokes::MaxAdvectSpeedALL
void NavierStokes::init_FSI_GHOST_MF_ALL(int ngrow) {

 int finest_level=parent->finestLevel();
 if (level!=0)
  BoxLib::Error("level invalid init_FSI_GHOST_MF_ALL");

 if ((ngrow<1)||(ngrow>ngrowFSI))
  BoxLib::Error("ngrow invalid");

 for (int ilev=level;ilev<=finest_level;ilev++) {
  NavierStokes& ns_level=getLevel(ilev);
  ns_level.init_FSI_GHOST_MF(ngrow);
 } // ilev=level...finest_level

} // end subroutine init_FSI_GHOST_MF_ALL

//    create a ghost solid velocity variable:
//    simple method: ghost solid velocity=solid velocity
//    law of wall  : ghost solid velocity in the solid
//                   is some kind of reflection of
//                   the interior fluid velocity.  ghost 
//                   solid velocity in the fluid=fluid velocity.
// initialize Fluid Structure Interaction Ghost Multifab
// multifab = multiple fortran array blocks.
void NavierStokes::init_FSI_GHOST_MF(int ngrow) {

 if ((ngrow<1)||(ngrow>ngrowFSI))
  BoxLib::Error("ngrow invalid");

 int finest_level=parent->finestLevel();
 int nmat=num_materials;
 int nparts=im_solid_map.size();
 bool use_tiling=ns_tiling;

 int nparts_ghost=nparts;
 int ghost_state_type=Solid_State_Type;
 if (nparts==0) {
  nparts_ghost=1;
  ghost_state_type=State_Type;
 } else if ((nparts>=1)&&(nparts<nmat)) {
  // do nothing
 } else {
  BoxLib::Error("nparts invalid");
 }
 
 if (localMF_grow[FSI_GHOST_MF]>=0)
  delete_localMF(FSI_GHOST_MF,1);

 new_localMF(FSI_GHOST_MF,nparts_ghost*BL_SPACEDIM,ngrow,-1);

 MultiFab& S_new=get_new_data(State_Type,slab_step+1);
 int nstate=num_materials_vel*(BL_SPACEDIM+1)+
  nmat*(num_state_material+ngeom_raw)+1;
 if (nstate!=S_new.nComp())
  BoxLib::Error("nstate invalid");

 if (nparts==0) {

  MultiFab::Copy(*localMF[FSI_GHOST_MF],S_new,0,0,BL_SPACEDIM,0);

 } else if ((nparts>=1)&&(nparts<nmat)) {

  if (law_of_the_wall==0) {
   MultiFab& Solid_new=get_new_data(Solid_State_Type,slab_step+1);
   if (nparts*BL_SPACEDIM!=Solid_new.nComp())
    BoxLib::Error("nparts*BL_SPACEDIM!=Solid_new.nComp()");
   MultiFab::Copy(*localMF[FSI_GHOST_MF],Solid_new,0,0,nparts*BL_SPACEDIM,0);
  } else if (law_of_the_wall>0) {
   if (num_materials_vel!=1)
    BoxLib::Error("num_materials_vel invalid");

   int ngrow_law_of_wall=3;
   MultiFab* solid_vel_mf=getStateSolid(ngrow_law_of_wall,0,
    nparts*BL_SPACEDIM,cur_time_slab);
     // velocity and pressure
   MultiFab* fluid_vel_mf=getState(ngrow_law_of_wall,0,BL_SPACEDIM+1,
    cur_time_slab);
     // temperature and density for all of the materials.
   int nden=nmat*num_state_material;
   MultiFab* state_var_mf=getStateDen(ngrow_law_of_wall,cur_time_slab);
   if (state_var_mf->nComp()!=nden)
    BoxLib::Error("state_var_mf->nComp()!=nden");
   MultiFab* LS_mf=getStateDist(ngrow_distance,cur_time_slab,1);
   if (LS_mf->nGrow()!=ngrow_distance)
    BoxLib::Error("LS_mf->nGrow()!=ngrow_distance");
   if (LS_mf->nComp()!=nmat*(BL_SPACEDIM+1))
    BoxLib::Error("LS_mf->nComp()!=nmat*(BL_SPACEDIM+1)");

   const Real* dx = geom.CellSize();

#ifdef _OPENMP
#pragma omp parallel
#endif
{
   for (MFIter mfi(*LS_mf,use_tiling); mfi.isValid(); ++mfi) {
    BL_ASSERT(grids[mfi.index()] == mfi.validbox());
    const int gridno = mfi.index();
    const Box& tilegrid = mfi.tilebox();
    const Box& fabgrid = grids[gridno];
    const int* tilelo=tilegrid.loVect();
    const int* tilehi=tilegrid.hiVect();
    const int* fablo=fabgrid.loVect();
    const int* fabhi=fabgrid.hiVect();
    int bfact=parent->Space_blockingFactor(level);

    const Real* xlo = grid_loc[gridno].lo();

    FArrayBox& lsfab=(*LS_mf)[mfi];
    FArrayBox& statefab=(*state_var_mf)[mfi];
    FArrayBox& fluidvelfab=(*fluid_vel_mf)[mfi]; 
    FArrayBox& solidvelfab=(*solid_vel_mf)[mfi]; 
    FArrayBox& ghostsolidvelfab=(*localMF[FSI_GHOST_MF])[mfi]; 

     // CODY ESTEBE: LAW OF THE WALL
     // fab = fortran array block
    FORT_WALLFUNCTION( 
     im_solid_map.dataPtr(),
     &level,
     &finest_level,
     &ngrow_law_of_wall,
     &ngrow_distance,
     &nmat,&nparts,&nden,
     tilelo,tilehi,
     fablo,fabhi,&bfact,
     xlo,dx,
     &dt_slab,
     lsfab.dataPtr(),ARLIM(lsfab.loVect()),ARLIM(lsfab.hiVect()),
     statefab.dataPtr(),ARLIM(statefab.loVect()),ARLIM(statefab.hiVect()),
     fluidvelfab.dataPtr(),
     ARLIM(fluidvelfab.loVect()),ARLIM(fluidvelfab.hiVect()),
     solidvelfab.dataPtr(),
     ARLIM(solidvelfab.loVect()),ARLIM(solidvelfab.hiVect()),
     ghostsolidvelfab.dataPtr(),
     ARLIM(ghostsolidvelfab.loVect()),ARLIM(ghostsolidvelfab.hiVect()) );
   } // mfi
} // omp
   ParallelDescriptor::Barrier();

   delete LS_mf;
   delete state_var_mf;
   delete fluid_vel_mf;
   delete solid_vel_mf;
  } else
   BoxLib::Error("law_of_the_wall invalid");

 } else {
  BoxLib::Error("nparts invalid");
 }

  // idx,ngrow,scomp,ncomp,index,scompBC_map
  // InterpBordersGHOST is ultimately called.
  // dest_lstGHOST for Solid_State_Type defaults to pc_interp.
  // scompBC_map==0 corresponds to extrap_bc, pc_interp and FORT_EXTRAPFILL
  // scompBC_map==1,2,3 corresponds to x or y or z vel_extrap_bc, pc_interp 
  //   and FORT_EXTRAPFILL
 for (int partid=0;partid<nparts_ghost;partid++) {
  int ibase=partid*BL_SPACEDIM;
  Array<int> scompBC_map;
  scompBC_map.resize(BL_SPACEDIM);
  for (int dir=0;dir<BL_SPACEDIM;dir++)
   scompBC_map[dir]=dir+1;
  PCINTERP_fill_borders(FSI_GHOST_MF,ngrow,ibase,
   BL_SPACEDIM,ghost_state_type,scompBC_map);
 } // partid=0..nparts_ghost-1

} // end subroutine init_FSI_GHOST_MF

void NavierStokes::resize_FSI_GHOST_MF(int ngrow) {

 if ((ngrow<1)||(ngrow>ngrowFSI))
  BoxLib::Error("ngrow invalid");

 int nmat=num_materials;
 int nparts=im_solid_map.size();

 int nparts_ghost=nparts;
 int ghost_state_type=Solid_State_Type;
 if (nparts==0) {
  nparts_ghost=1;
  ghost_state_type=State_Type;
 } else if ((nparts>=1)&&(nparts<nmat)) {
  // do nothing
 } else {
  BoxLib::Error("nparts invalid");
 }

 if (localMF[FSI_GHOST_MF]->nComp()!=nparts_ghost*BL_SPACEDIM)
  BoxLib::Error("localMF[FSI_GHOST_MF]->nComp()!=nparts_ghost*BL_SPACEDIM");

 if (localMF[FSI_GHOST_MF]->nGrow()==ngrow) {
  // do nothing
 } else if (localMF[FSI_GHOST_MF]->nGrow()>=0) {

  MultiFab* save_ghost=
    new MultiFab(grids,nparts_ghost*BL_SPACEDIM,0,dmap,Fab_allocate); 
  MultiFab::Copy(*save_ghost,*localMF[FSI_GHOST_MF],0,0,
       nparts_ghost*BL_SPACEDIM,0);
  delete_localMF(FSI_GHOST_MF,1);
  new_localMF(FSI_GHOST_MF,nparts_ghost*BL_SPACEDIM,ngrow,-1);
  MultiFab::Copy(*localMF[FSI_GHOST_MF],*save_ghost,0,0,
       nparts_ghost*BL_SPACEDIM,0);

  // idx,ngrow,scomp,ncomp,index,scompBC_map
  // InterpBordersGHOST is ultimately called.
  // dest_lstGHOST for Solid_State_Type defaults to pc_interp.
  // scompBC_map==0 corresponds to extrap_bc, pc_interp and FORT_EXTRAPFILL
  // scompBC_map==1,2,3 corresponds to x or y or z vel_extrap_bc, pc_interp 
  //   and FORT_EXTRAPFILL
  for (int partid=0;partid<nparts_ghost;partid++) {
   int ibase=partid*BL_SPACEDIM;
   Array<int> scompBC_map;
   scompBC_map.resize(BL_SPACEDIM);
   for (int dir=0;dir<BL_SPACEDIM;dir++)
    scompBC_map[dir]=dir+1;
   PCINTERP_fill_borders(FSI_GHOST_MF,ngrow,ibase,
    BL_SPACEDIM,ghost_state_type,scompBC_map);
  } // partid=0..nparts_ghost-1

  delete save_ghost;
 } else
  BoxLib::Error("localMF[FSI_GHOST_MF]->nGrow() invalid");

} // end subroutine resize_FSI_GHOST_MF


// get rid of the ghost cells
void NavierStokes::resize_FSI_MF() {

 int nmat=num_materials;
 int nparts=im_solid_map.size();
 if (nparts==0) {
  // do nothing
 } else if ((nparts>=1)&&(nparts<nmat)) {
  if (nFSI_sub!=12)
   BoxLib::Error("nFSI_sub invalid");
  int nFSI=nparts*nFSI_sub;
  if (localMF[FSI_MF]->nComp()!=nFSI)
   BoxLib::Error("localMF[FSI_MF]->nComp()!=nFSI");
  if (localMF[FSI_MF]->nGrow()==0) {
   // do nothing
  } else if (localMF[FSI_MF]->nGrow()>0) {
   MultiFab* save_FSI=
    new MultiFab(grids,nFSI,0,dmap,Fab_allocate); 
   MultiFab::Copy(*save_FSI,*localMF[FSI_MF],0,0,nFSI,0);
   delete_localMF(FSI_MF,1);
   new_localMF(FSI_MF,nFSI,0,-1);
   MultiFab::Copy(*localMF[FSI_MF],*save_FSI,0,0,nFSI,0);
   delete save_FSI;
  } else
   BoxLib::Error("localMF[FSI_MF]->nGrow() invalid");

 } else {
  BoxLib::Error("nparts invalid");
 }

} // end subroutine resize_FSI_MF



// create a distance function (velocity and temperature) on this level.
// calls fill coarse patch if level>0
// called from: NavierStokes::initData ()
//              NavierStokes::nonlinear_advection()
void NavierStokes::FSI_make_distance(Real time,Real dt) {

 int nmat=num_materials;
 int nparts=im_solid_map.size();

 if (nparts==0) {
  // do nothing
 } else if ((nparts>=1)&&(nparts<=nmat-1)) {

  // nmat x (velocity + LS + temperature + flag+stress)   3D
  if (nFSI_sub!=12)
   BoxLib::Error("nFSI_sub invalid");
  int nFSI=nparts*nFSI_sub;
  if (ngrowFSI!=3)
   BoxLib::Error("ngrowFSI invalid");

  if (localMF_grow[FSI_MF]>=0)
   delete_localMF(FSI_MF,1);

  new_localMF(FSI_MF,nFSI,ngrowFSI,-1);

  for (int partid=0;partid<nparts;partid++) {
   int ibase=partid*nFSI_sub;
   setVal_localMF(FSI_MF,0.0,ibase,3,ngrowFSI); // velocity
   setVal_localMF(FSI_MF,-99999.0,ibase+3,1,ngrowFSI); // LS
   setVal_localMF(FSI_MF,0.0,ibase+4,1,ngrowFSI); // temperature
   setVal_localMF(FSI_MF,0.0,ibase+5,1,ngrowFSI); // mask
   setVal_localMF(FSI_MF,0.0,ibase+6,6,ngrowFSI); //stress 
  } // partid=0..nparts-1

  if (do_FSI()==1) {

    // in: NavierStokes::FSI_make_distance
    // 1. create lagrangian container data structure within the 
    //    fortran part that recognizes tiles. (FILLCONTAINER in SOLIDFLUID.F90)
    // 2. fill the containers with the Lagrangian information.
    //    (CLSVOF_FILLCONTAINER called from FILLCONTAINER)
    //    i.e. associate to each tile a set of Lagrangian nodes and elements
    //    that are located in or very near the tile.
   create_fortran_grid_struct(time,dt);

   int iter=0; // touch_flag=0
   int FSI_operation=2;  // make distance in narrow band
   int FSI_sub_operation=0;
   resize_mask_nbr(ngrowFSI);
    // 1.FillCoarsePatch
    // 2.traverse lagrangian elements belonging to each tile and update
    //   cells within "bounding box" of the element.
   ns_header_msg_level(FSI_operation,FSI_sub_operation,time,dt,iter);
  
   do {
 
    FSI_operation=3; // sign update   
    FSI_sub_operation=0;
    ns_header_msg_level(FSI_operation,FSI_sub_operation,time,dt,iter);
    iter++;
  
   } while (FSI_touch_flag[0]==1);

   build_moment_from_FSILS();

  } else if (do_FSI()==0) {
   // do nothing
  } else
   BoxLib::Error("do_FSI invalid");
 
  bool use_tiling=ns_tiling;

  const Real* dx = geom.CellSize();

#ifdef _OPENMP
#pragma omp parallel
#endif
{
  for (MFIter mfi(*localMF[FSI_MF],use_tiling); mfi.isValid(); ++mfi) {
   BL_ASSERT(grids[mfi.index()] == mfi.validbox());
   const int gridno = mfi.index();
   const Box& tilegrid = mfi.tilebox();
   const Box& fabgrid = grids[gridno];
   const int* tilelo=tilegrid.loVect();
   const int* tilehi=tilegrid.hiVect();
   const int* fablo=fabgrid.loVect();
   const int* fabhi=fabgrid.hiVect();
   int bfact=parent->Space_blockingFactor(level);
   const Real* xlo = grid_loc[gridno].lo();
   const Real* xhi = grid_loc[gridno].hi();

   FArrayBox& solidfab=(*localMF[FSI_MF])[mfi];

    // updates FSI_MF for FSI_flag(im)==1 type materials.
   FORT_INITDATASOLID(
     &nmat,
     &nparts,
     &nFSI_sub,
     &nFSI,
     &ngrowFSI,
     im_solid_map.dataPtr(),
     &time,
     tilelo,tilehi,
     fablo,fabhi,
     &bfact,
     solidfab.dataPtr(),
     ARLIM(solidfab.loVect()),ARLIM(solidfab.hiVect()),
     dx,xlo,xhi);  
  } // mfi
} // omp
  ParallelDescriptor::Barrier();

   // Solid velocity
  MultiFab& Solid_new = get_new_data(Solid_State_Type,slab_step+1);
  if (Solid_new.nComp()!=nparts*BL_SPACEDIM)
   BoxLib::Error("Solid_new.nComp()!=nparts*BL_SPACEDIM");
  for (int partid=0;partid<nparts;partid++) {
   int ibase=partid*nFSI_sub;
   MultiFab::Copy(Solid_new,*localMF[FSI_MF],ibase,partid*BL_SPACEDIM,
     BL_SPACEDIM,0);
  } // partid=0..nparts-1

 } else {
  BoxLib::Error("nparts invalid");
 }

}  // subroutine FSI_make_distance

// called from: Transfer_FSI_To_STATE()
// Transfer_FSI_To_STATE() called from: ns_header_msg_level,initData ()
void NavierStokes::copy_velocity_on_sign(int partid) {

 int nmat=num_materials;
 int nparts=im_solid_map.size();
 if ((nparts<1)||(nparts>=nmat))
  BoxLib::Error("nparts invalid");
 if ((partid<0)||(partid>=nparts))
  BoxLib::Error("partid invalid");
 debug_ngrow(FSI_MF,ngrowFSI,1);

 int im_part=im_solid_map[partid];
 if ((im_part<0)||(im_part>=nmat))
  BoxLib::Error("im_part invalid");

 if (FSI_flag[im_part]==2) { //prescribed sci_clsvof.F90 rigid solid 

  MultiFab& S_new=get_new_data(State_Type,slab_step+1);
  int nstate=num_materials_vel*(BL_SPACEDIM+1)+
   nmat*(num_state_material+ngeom_raw)+1;
  if (nstate!=S_new.nComp())
   BoxLib::Error("nstate invalid");

   // nmat x (velocity + LS + temperature + flag)
  if (nFSI_sub!=12)
   BoxLib::Error("nFSI_sub invalid");

  int nFSI=nparts*nFSI_sub;
  if (localMF[FSI_MF]->nComp()!=nFSI)
   BoxLib::Error("localMF[FSI_MF]->nComp()!=nFSI");
  
  bool use_tiling=ns_tiling;

  if (ns_is_rigid(im_part)==1) {

   const Real* dx = geom.CellSize();

#ifdef _OPENMP
#pragma omp parallel
#endif
{
   for (MFIter mfi(S_new,use_tiling); mfi.isValid(); ++mfi) {
    BL_ASSERT(grids[mfi.index()] == mfi.validbox());
    const int gridno = mfi.index();
    const Box& tilegrid = mfi.tilebox();
    const Box& fabgrid = grids[gridno];
    const int* tilelo=tilegrid.loVect();
    const int* tilehi=tilegrid.hiVect();
    const int* fablo=fabgrid.loVect();
    const int* fabhi=fabgrid.hiVect();
    int bfact=parent->Space_blockingFactor(level);

    const Real* xlo = grid_loc[gridno].lo();

    FArrayBox& snewfab=S_new[mfi];
    FArrayBox& fsifab=(*localMF[FSI_MF])[mfi];

    FORT_COPY_VEL_ON_SIGN(
     &im_part, 
     &nparts,
     &partid, 
     &ngrowFSI, 
     &nFSI, 
     &nFSI_sub, 
     xlo,dx,
     snewfab.dataPtr(),ARLIM(snewfab.loVect()),ARLIM(snewfab.hiVect()),
     fsifab.dataPtr(),ARLIM(fsifab.loVect()),ARLIM(fsifab.hiVect()),
     tilelo,tilehi,
     fablo,fabhi,&bfact,
     &nmat,&nstate);
   }  // mfi  
}//omp
   ParallelDescriptor::Barrier(); 

  } else {
   BoxLib::Error("ns_is_rigid invalid");
  }

 } else if (FSI_flag[im_part]==4) { // FSI CTML material
  // do nothing (CTML)
 } else
  BoxLib::Error("FSI_flag[im_part] invalid");

} // subroutine copy_velocity_on_sign

// called from: FSI_make_distance, initData ()
void NavierStokes::build_moment_from_FSILS() {

 int finest_level=parent->finestLevel();
 if ((level<0)||(level>finest_level))
  BoxLib::Error("level invalid 1");

 int nmat=num_materials;

 if (do_FSI()!=1)
  BoxLib::Error("do_FSI invalid");

 MultiFab& S_new=get_new_data(State_Type,slab_step+1);
 MultiFab& LS_new=get_new_data(LS_Type,slab_step+1);
 int nstate=num_materials_vel*(BL_SPACEDIM+1)+
   nmat*(num_state_material+ngeom_raw)+1;
 if (nstate!=S_new.nComp())
  BoxLib::Error("nstate invalid");
 if (LS_new.nComp()!=nmat*(1+BL_SPACEDIM))
  BoxLib::Error("LS_new.nComp()!=nmat*(1+BL_SPACEDIM)");

   // nparts x (velocity + LS + temperature + flag+stress)
 if (nFSI_sub!=12)
  BoxLib::Error("nFSI_sub invalid");
 if (ngrowFSI!=3)
  BoxLib::Error("ngrowFSI!=3");
 int nparts=im_solid_map.size();
 if ((nparts<1)||(nparts>=nmat))
  BoxLib::Error("nparts invalid");
 int nFSI=nparts*nFSI_sub;
 if (localMF[FSI_MF]->nComp()!=nFSI)
  BoxLib::Error("localMF[FSI_MF]->nComp()!=nFSI");
 debug_ngrow(FSI_MF,ngrowFSI,1);
  
 bool use_tiling=ns_tiling;

 const Real* dx = geom.CellSize();

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(S_new,use_tiling); mfi.isValid(); ++mfi) {
  BL_ASSERT(grids[mfi.index()] == mfi.validbox());
  const int gridno = mfi.index();
  const Box& tilegrid = mfi.tilebox();
  const Box& fabgrid = grids[gridno];
  const int* tilelo=tilegrid.loVect();
  const int* tilehi=tilegrid.hiVect();
  const int* fablo=fabgrid.loVect();
  const int* fabhi=fabgrid.hiVect();
  int bfact=parent->Space_blockingFactor(level);

  const Real* xlo = grid_loc[gridno].lo();

  FArrayBox& snewfab=S_new[mfi];
  FArrayBox& lsnewfab=LS_new[mfi];
  FArrayBox& fsifab=(*localMF[FSI_MF])[mfi];

  FORT_BUILD_MOMENT(
    &level,
    &finest_level,
    &nFSI, 
    &nFSI_sub, 
    &nparts,
    &ngrowFSI, 
    im_solid_map.dataPtr(),
    xlo,dx,
    snewfab.dataPtr(),ARLIM(snewfab.loVect()),ARLIM(snewfab.hiVect()),
    lsnewfab.dataPtr(),ARLIM(lsnewfab.loVect()),ARLIM(lsnewfab.hiVect()),
    fsifab.dataPtr(),ARLIM(fsifab.loVect()),ARLIM(fsifab.hiVect()),
    tilelo,tilehi,
    fablo,fabhi,&bfact,
    &nmat,&nstate);
 }  // mfi  
}//omp
 ParallelDescriptor::Barrier(); 

} // subroutine build_moment_from_FSILS

// called from: ns_header_msg_level,initData ()
void NavierStokes::Transfer_FSI_To_STATE() {

 // nparts x (velocity + LS + temperature + flag + stress)
 int nmat=num_materials;
 int nparts=im_solid_map.size();
 if ((nparts<0)||(nparts>=nmat))
  BoxLib::Error("nparts invalid");

 if (do_FSI()==1) {

  if ((nparts<1)||(nparts>=nmat))
   BoxLib::Error("nparts invalid");
  debug_ngrow(FSI_MF,ngrowFSI,1);

  MultiFab& S_new=get_new_data(State_Type,slab_step+1);
  int nstate=num_materials_vel*(BL_SPACEDIM+1)+
   nmat*(num_state_material+ngeom_raw)+1;
  int dencomp=num_materials_vel*(BL_SPACEDIM+1);     
  if (nstate!=S_new.nComp())
   BoxLib::Error("nstate invalid");

  MultiFab& Solid_new = get_new_data(Solid_State_Type,slab_step+1);
  if (Solid_new.nComp()!=nparts*BL_SPACEDIM)
   BoxLib::Error("Solid_new.nComp()!=nparts*BL_SPACEDIM");

  MultiFab& LS_new = get_new_data(LS_Type,slab_step+1);
  if (LS_new.nComp()!=nmat*(BL_SPACEDIM+1))
   BoxLib::Error("LS_new invalid ncomp");
  if (nFSI_sub!=12)
   BoxLib::Error("nFSI_sub invalid");
  int nFSI=nparts*nFSI_sub;
  if (localMF[FSI_MF]->nComp()!=nFSI)
   BoxLib::Error("localMF[FSI_MF]->nComp()!=nFSI");

  for (int partid=0;partid<nparts;partid++) {

   int im_part=im_solid_map[partid];
   if ((im_part<0)||(im_part>=nmat))
    BoxLib::Error("im_part invalid");

   if ((FSI_flag[im_part]==2)|| //prescribed sci_clsvof.F90 rigid solid 
       (FSI_flag[im_part]==4)) { //FSI CTML sci_clsvof.F90 solid
    int ibase=partid*nFSI_sub;
    copy_velocity_on_sign(partid);
     // Solid velocity
     //ngrow=0
    MultiFab::Copy(Solid_new,*localMF[FSI_MF],ibase,partid*BL_SPACEDIM,
     BL_SPACEDIM,0);
     // LS
     //ngrow=0
    MultiFab::Copy(LS_new,*localMF[FSI_MF],ibase+3,im_part,1,0);
     // temperature
    if (solidheat_flag==0) { // diffuse in solid
     // do nothing
    } else if ((solidheat_flag==1)||  //dirichlet
               (solidheat_flag==2)) { //neumann
      //ngrow=0
     MultiFab::Copy(S_new,*localMF[FSI_MF],ibase+4,
      dencomp+im_part*num_state_material+1,1,0);
    } else
     BoxLib::Error("solidheat_flag invalid"); 
   } else if (FSI_flag[im_part]==1) { // prescribed PROB.F90 rigid solid
    // do nothing
   } else
    BoxLib::Error("FSI_flag invalid");

  } // partid=0..nparts-1

 } else if (do_FSI()==0) {
  // do nothing
 } else
  BoxLib::Error("do_FSI invalid");

}  // subroutine Transfer_FSI_To_STATE

//FSI_operation=0  initialize node locations; generate_new_triangles
//FSI_operation=1  update node locations
//FSI_operation=2  make distance in narrow band
//  (nparts x (vel, LS, Temp, flag, stress)
//FSI_operation=3  update the sign.
//FSI_operation=4  copy Eulerian vel. to lag.
//
// note for CTML algorithm:
// 1. copy Eulerian velocity to Lagrangian velocity.
// 2. update node locations
// 3. copy Lagrangian Force to Eulerian Force and update Eulerian velocity.
void NavierStokes::ns_header_msg_level(
 int FSI_operation,int FSI_sub_operation,
 Real time,Real dt,int iter) {

 Array< int > num_tiles_on_thread_proc;
 num_tiles_on_thread_proc.resize(thread_class::nthreads);
 for (int tid=0;tid<thread_class::nthreads;tid++) {
  num_tiles_on_thread_proc[tid]=0;
 }

 if (FSI_operation==0) { //initialize node locations; generate_new_triangles
  if (iter!=0)
   BoxLib::Error("iter invalid");
  if (FSI_sub_operation!=0)
   BoxLib::Error("FSI_sub_operation!=0");
 } else if (FSI_operation==1) { //update node locations
  if (iter!=0)
   BoxLib::Error("iter invalid");
  if (FSI_sub_operation!=0)
   BoxLib::Error("FSI_sub_operation!=0");
  if (CTML_FSI_flagC()==1) {
   if (num_divu_outer_sweeps!=1)
    BoxLib::Error("num_divu_outer_sweeps!=1");
   if (ns_time_order!=1)
    BoxLib::Error("ns_time_order!=1");
  } else if (CTML_FSI_flagC()==0) {
   // do nothing
  } else
   BoxLib::Error("CTML_FSI_flagC() invalid");
 } else if (FSI_operation==2) { //make distance in narrow band
  if (iter!=0)
   BoxLib::Error("iter invalid");
  if (FSI_sub_operation!=0)
   BoxLib::Error("FSI_sub_operation!=0");
 } else if (FSI_operation==3) { //update the sign.
  if (iter<0)
   BoxLib::Error("iter invalid");
  if (FSI_sub_operation!=0)
   BoxLib::Error("FSI_sub_operation!=0");
 } else if (FSI_operation==4) { //copy Eulerian velocity to Lagrangian velocity
  if (iter!=0)
   BoxLib::Error("iter invalid");
  if ((FSI_sub_operation<0)||
      (FSI_sub_operation>2)) 
   BoxLib::Error("FSI_sub_operation invalid");
 } else
  BoxLib::Error("FSI_operation out of range");

 if (iter==0) {
  FSI_touch_flag.resize(thread_class::nthreads);
  for (int tid=0;tid<thread_class::nthreads;tid++) {
   FSI_touch_flag[tid]=0;
  }
 } else if (iter>0) {
  for (int tid=0;tid<thread_class::nthreads;tid++) {
   FSI_touch_flag[tid]=0;
  }
 } else {
  BoxLib::Error("iter invalid");
 }

 int nmat=num_materials;
 int scomp_mofvars=num_materials_vel*(BL_SPACEDIM+1)+
  nmat*num_state_material;
 int dencomp=num_materials_vel*(BL_SPACEDIM+1);     
  
 const int max_level = parent->maxLevel();
 int finest_level=parent->finestLevel();

 if ((level>max_level)||(finest_level>max_level))
  BoxLib::Error("(level>max_level)||(finest_level>max_level)");

 const Real* dx = geom.CellSize();
 Real h_small=dx[0];
 if (h_small>dx[1])
  h_small=dx[1];
 if (h_small>dx[BL_SPACEDIM-1])
  h_small=dx[BL_SPACEDIM-1];
 for (int i=level+1;i<=max_level;i++)
  h_small/=2.0;

 Real dx_maxlevel[BL_SPACEDIM];
 for (int dir=0;dir<BL_SPACEDIM;dir++)
  dx_maxlevel[dir]=dx[dir];
 for (int ilev=level+1;ilev<=max_level;ilev++) 
  for (int dir=0;dir<BL_SPACEDIM;dir++)
   dx_maxlevel[dir]/=2.0;

 if (verbose>0) {
  if (ParallelDescriptor::IOProcessor()) {
   std::cout << "ns_header_msg_level START\n";
   std::cout << "level= " << level << " finest_level= " << finest_level <<
    " max_level= " << max_level << '\n';
   std::cout << "FSI_operation= " << FSI_operation <<
    " time = " << time << " dt= " << dt << " iter = " << iter << '\n';
  }
 } else if (verbose==0) {
  // do nothing
 } else
  BoxLib::Error("verbose invalid");

 int ioproc;
 if (ParallelDescriptor::IOProcessor())
  ioproc=1;
 else
  ioproc=0;

 if (FSI_operation==0) { // init node locations
  if (level==0) {
   elements_generated=0;
  } else {
   elements_generated=1;
  }
 } else if (FSI_operation==1) { // update node locations
  if (level==0) {
   elements_generated=0;
  } else {
   elements_generated=1;
  }
 } else if ((FSI_operation>=2)&&(FSI_operation<=3)) {
  elements_generated=1;
 } else if (FSI_operation==4) { // copy Eul. fluid vel to Lag. fluid vel.
  elements_generated=1;
 } else
  BoxLib::Error("FSI_operation invalid");

 int current_step = nStep();
 int plot_interval=parent->plotInt();

 if (do_FSI()==1) {

   // nparts x (velocity + LS + temperature + flag)
  int nparts=im_solid_map.size();
  if ((nparts<1)||(nparts>=nmat))
   BoxLib::Error("nparts invalid");

  MultiFab& Solid_new=get_new_data(Solid_State_Type,slab_step+1);
  if (Solid_new.nComp()!=nparts*BL_SPACEDIM)
   BoxLib::Error("Solid_new.nComp()!=nparts*BL_SPACEDIM");

  MultiFab& S_new=get_new_data(State_Type,slab_step+1);
  MultiFab& LS_new = get_new_data(LS_Type,slab_step+1);
  if (LS_new.nComp()!=nmat*(BL_SPACEDIM+1))
   BoxLib::Error("LS_new invalid ncomp");

  bool use_tiling=ns_tiling;
  int bfact=parent->Space_blockingFactor(level);

  Real problo[BL_SPACEDIM];
  Real probhi[BL_SPACEDIM];
  Real problen[BL_SPACEDIM];
  for (int dir=0;dir<BL_SPACEDIM;dir++) {
   problo[dir]=Geometry::ProbLo(dir);
   probhi[dir]=Geometry::ProbHi(dir);
   problen[dir]=probhi[dir]-problo[dir];
   if (problen[dir]<=0.0)
    BoxLib::Error("problen[dir]<=0.0");
  }

  if (nFSI_sub!=12)
   BoxLib::Error("nFSI_sub invalid");
  int nFSI=nparts*nFSI_sub;

  if ((FSI_operation==0)||  // initialize nodes
      (FSI_operation==1)) { // update node locations

   if (FSI_sub_operation!=0)
    BoxLib::Error("FSI_sub_operation!=0");

   if (elements_generated==0) {
    int ngrowFSI_fab=0;
    IntVect unitlo(D_DECL(0,0,0));
    IntVect unithi(D_DECL(0,0,0));
     // construct cell-centered type box
    Box unitbox(unitlo,unithi);

    const int* tilelo=unitbox.loVect();
    const int* tilehi=unitbox.hiVect();
    const int* fablo=unitbox.loVect();
    const int* fabhi=unitbox.hiVect();

    FArrayBox FSIfab(unitbox,nFSI);

    if (num_materials_vel!=1)
     BoxLib::Error("num_materials_vel invalid");

    Array<int> velbc;
    velbc.resize(num_materials_vel*BL_SPACEDIM*2*BL_SPACEDIM);
    for (int i=0;i<velbc.size();i++)
     velbc[i]=0;
    Array<int> vofbc;
    vofbc.resize(2*BL_SPACEDIM);
    for (int i=0;i<vofbc.size();i++)
     vofbc[i]=0;

    int tid=0;
    int gridno=0;

    FORT_HEADERMSG(
     &tid,
     &num_tiles_on_thread_proc[tid],
     &gridno,
     &thread_class::nthreads,
     &level,
     &finest_level,
     &max_level,
     &FSI_operation, // 0 or 1 (initialize or update nodes)
     &FSI_sub_operation, // 0
     tilelo,tilehi,
     fablo,fabhi,
     &bfact,
     problo,
     problen, 
     dx_maxlevel, 
     problo,
     probhi, 
     velbc.dataPtr(),  
     vofbc.dataPtr(), 
     FSIfab.dataPtr(), // placeholder
     ARLIM(FSIfab.loVect()),ARLIM(FSIfab.hiVect()),
     FSIfab.dataPtr(), // velfab spot
     ARLIM(FSIfab.loVect()),ARLIM(FSIfab.hiVect()),
     FSIfab.dataPtr(), // mnbrfab spot
     ARLIM(FSIfab.loVect()),ARLIM(FSIfab.hiVect()),
     FSIfab.dataPtr(), // mfiner spot
     ARLIM(FSIfab.loVect()),ARLIM(FSIfab.hiVect()),
     &nFSI,
     &nFSI_sub,
     &ngrowFSI_fab,
     &nparts,
     im_solid_map.dataPtr(),
     &h_small,
     &time, 
     &dt, 
     FSI_refine_factor.dataPtr(),
     FSI_bounding_box_ngrow.dataPtr(),
     &FSI_touch_flag[tid],
     &CTML_FSI_init,
     &CTML_force_model,
     &iter,
     &current_step,
     &plot_interval,
     &ioproc);

    elements_generated=1;
   } else if (elements_generated==1) {
    // do nothing
   } else 
    BoxLib::Error("elements_generated invalid");

   elements_generated=1;

   CTML_FSI_init=1;

  } else if ((FSI_operation==2)||  // make distance in narrow band
             (FSI_operation==3)) { // update the sign

   if (FSI_sub_operation!=0)
    BoxLib::Error("FSI_sub_operation!=0");

   elements_generated=1;

    // FSI_MF allocated in FSI_make_distance
   if (ngrowFSI!=3)
    BoxLib::Error("ngrowFSI invalid");
   debug_ngrow(FSI_MF,ngrowFSI,1);
   if (localMF[FSI_MF]->nComp()!=nFSI)
    BoxLib::Error("localMF[FSI_MF]->nComp() invalid");

   if (FSI_operation==2) { // make distance in narrow band.

    if (num_materials_vel!=1)
     BoxLib::Error("num_materials_vel invalid");

     // fill coarse patch 
    if (level>0) {

      //ngrow=0
     MultiFab* S_new_coarse=new MultiFab(grids,BL_SPACEDIM,0,dmap,
      Fab_allocate);
     int dcomp=0;
     int scomp=0;
     FillCoarsePatch(*S_new_coarse,dcomp,time,State_Type,scomp,BL_SPACEDIM);

     if (verbose>0) {
      if (ParallelDescriptor::IOProcessor()) {
       std::cout << "check_for_NAN(S_new_coarse,200)\n";
       std::fflush(NULL);
      }
      check_for_NAN(S_new_coarse,200);
     }

      //ngrow=0
     MultiFab* Solid_new_coarse=new MultiFab(grids,nparts*BL_SPACEDIM,0,dmap,
      Fab_allocate);
     dcomp=0;
     scomp=0;

     if ((verbose>0)&&(1==0)) {
      if (ParallelDescriptor::IOProcessor()) {
       std::cout << "FillCoarsePatch(*Solid_new_coarse)\n";
       std::fflush(NULL);
      }
     }

     FillCoarsePatch(*Solid_new_coarse,dcomp,time,Solid_State_Type,scomp,
        nparts*BL_SPACEDIM);

     if (verbose>0) {
      if (ParallelDescriptor::IOProcessor()) {
       std::cout << "check_for_NAN(Solid_new_coarse,200)\n";
       std::fflush(NULL);
      }
      check_for_NAN(Solid_new_coarse,201);
     }

      //ngrow=0
     MultiFab* LS_new_coarse=new MultiFab(grids,nmat*(BL_SPACEDIM+1),0,dmap,
      Fab_allocate);
     dcomp=0;
     scomp=0;
     FillCoarsePatch(*LS_new_coarse,dcomp,time,LS_Type,scomp,
        nmat*(BL_SPACEDIM+1));

     if (verbose>0) {
      if (ParallelDescriptor::IOProcessor()) {
       std::cout << "check_for_NAN(LS_new_coarse,200)\n";
       std::fflush(NULL);
      }
      check_for_NAN(LS_new_coarse,202);
     }

     for (int partid=0;partid<nparts;partid++) {

      int im_part=im_solid_map[partid];

      if ((im_part<0)||(im_part>=nmat))
       BoxLib::Error("im_part invalid");
 
      if ((FSI_flag[im_part]==2)|| //prescribed sci_clsvof.F90 rigid solid 
          (FSI_flag[im_part]==4)) { //FSI CTML sci_clsvof.F90 solid

       dcomp=im_part;
       scomp=im_part;
         //ngrow==0 (levelset)
       MultiFab::Copy(LS_new,*LS_new_coarse,scomp,dcomp,1,0);
       dcomp=nmat+im_part*BL_SPACEDIM;
       scomp=dcomp;
         //ngrow==0 (levelset normal)
       MultiFab::Copy(LS_new,*LS_new_coarse,scomp,dcomp,BL_SPACEDIM,0);

       dcomp=partid*BL_SPACEDIM;
       scomp=partid*BL_SPACEDIM;
         //ngrow==0
       MultiFab::Copy(Solid_new,*Solid_new_coarse,scomp,dcomp,BL_SPACEDIM,0);

        //ngrow==0
       MultiFab* new_coarse_thermal=new MultiFab(grids,1,0,dmap,Fab_allocate);
       dcomp=0;
       int scomp_thermal=dencomp+im_part*num_state_material+1;
        //ncomp==1
       FillCoarsePatch(*new_coarse_thermal,dcomp,time,State_Type,
         scomp_thermal,1);

        //ngrow==0
       if (solidheat_flag==0) {  // diffuse in solid
        // do nothing
       } else if ((solidheat_flag==1)||   // dirichlet
                  (solidheat_flag==2)) {  // neumann
         //ngrow==0
        MultiFab::Copy(S_new,*new_coarse_thermal,0,scomp_thermal,1,0);
       } else
        BoxLib::Error("solidheat_flag invalid");

       delete new_coarse_thermal;

      } else if (FSI_flag[im_part]==1) { // prescribed PROB.F90 rigid solid 
       // do nothing
      } else
       BoxLib::Error("FSI_flag invalid");
     } // partid=0..nparts-1

     delete S_new_coarse;
     delete Solid_new_coarse;
     delete LS_new_coarse;

    } else if (level==0) {
     // do nothing
    } else
     BoxLib::Error("level invalid 3");

   } else if (FSI_operation==3) { // update sign
    // do not fill coarse patch.
   } else
    BoxLib::Error("FSI_operation invalid");

   MultiFab* solidmf=getStateSolid(ngrowFSI,0,
     nparts*BL_SPACEDIM,time);
   MultiFab* denmf=getStateDen(ngrowFSI,time);  
   MultiFab* LSMF=getStateDist(ngrowFSI,time,2);
   if (LSMF->nGrow()!=ngrowFSI)
    BoxLib::Error("LSMF->nGrow()!=ngrow_distance");

   // FSI_MF allocated in FSI_make_distance
   // all components of FSI_MF are initialized to zero except for LS.
   // LS component of FSI_MF is init to -99999
   // nparts x (velocity + LS + temperature + flag + stress)
   for (int partid=0;partid<nparts;partid++) {

    int im_part=im_solid_map[partid];
    if ((im_part<0)||(im_part>=nmat))
     BoxLib::Error("im_part invalid");

    int ibase=partid*nFSI_sub;
     // velocity
    MultiFab::Copy(*localMF[FSI_MF],*solidmf,partid*BL_SPACEDIM,
      ibase,BL_SPACEDIM,ngrowFSI);
     // LS  
    MultiFab::Copy(*localMF[FSI_MF],*LSMF,im_part,
      ibase+3,1,ngrowFSI);
     // temperature
    MultiFab::Copy(*localMF[FSI_MF],*denmf,im_part*num_state_material+1,
      ibase+4,1,ngrowFSI);

     // flag (mask)
    if (FSI_operation==2) {

     if ((level>0)||
         ((level==0)&&(time>0.0))) {
      setVal_localMF(FSI_MF,10.0,ibase+5,1,ngrowFSI); 
     } else if ((level==0)&&(time==0.0)) {
      // do nothing
     } else
      BoxLib::Error("level or time invalid");

    } else if (FSI_operation==3) {
     // do nothing
    } else
     BoxLib::Error("FSI_operation invalid");

   } // partid=0..nparts-1

   // (1) =1 interior  =1 fine-fine ghost in domain  =0 otherwise
   // (2) =1 interior  =0 otherwise
   resize_mask_nbr(ngrowFSI);
   debug_ngrow(MASK_NBR_MF,ngrowFSI,2);
 
#ifdef _OPENMP
#pragma omp parallel
#endif
{
   for (MFIter mfi(S_new,use_tiling); mfi.isValid(); ++mfi) {
    BL_ASSERT(grids[mfi.index()] == mfi.validbox());
    const int gridno = mfi.index();
    const Box& tilegrid = mfi.tilebox();
    const Box& fabgrid = grids[gridno];
    const int* tilelo=tilegrid.loVect();
    const int* tilehi=tilegrid.hiVect();
    const int* fablo=fabgrid.loVect();
    const int* fabhi=fabgrid.hiVect();
    const Real* xlo = grid_loc[gridno].lo();
    FArrayBox& FSIfab=(*localMF[FSI_MF])[mfi];
    FArrayBox& mnbrfab=(*localMF[MASK_NBR_MF])[mfi];

    Array<int> velbc=getBCArray(Solid_State_Type,gridno,0,
     nparts*BL_SPACEDIM);
    Array<int> vofbc=getBCArray(State_Type,gridno,scomp_mofvars,1);

    int tid=ns_thread();
    if ((tid<0)||(tid>=thread_class::nthreads))
     BoxLib::Error("tid invalid");

    FORT_HEADERMSG(
     &tid,
     &num_tiles_on_thread_proc[tid],
     &gridno,
     &thread_class::nthreads,
     &level,
     &finest_level,
     &max_level,
     &FSI_operation, // 2 or 3 (make distance or update sign)
     &FSI_sub_operation, // 0
     tilelo,tilehi,
     fablo,fabhi,
     &bfact,
     xlo,
     dx, 
     dx_maxlevel, 
     problo,
     probhi, 
     velbc.dataPtr(),  
     vofbc.dataPtr(), 
     FSIfab.dataPtr(),
     ARLIM(FSIfab.loVect()),ARLIM(FSIfab.hiVect()),
     FSIfab.dataPtr(), // velfab spot
     ARLIM(FSIfab.loVect()),ARLIM(FSIfab.hiVect()),
     mnbrfab.dataPtr(),
     ARLIM(mnbrfab.loVect()),ARLIM(mnbrfab.hiVect()),
     mnbrfab.dataPtr(), // mfiner spot
     ARLIM(mnbrfab.loVect()),ARLIM(mnbrfab.hiVect()),
     &nFSI,
     &nFSI_sub,
     &ngrowFSI,
     &nparts,
     im_solid_map.dataPtr(),
     &h_small,
     &time, 
     &dt, 
     FSI_refine_factor.dataPtr(),
     FSI_bounding_box_ngrow.dataPtr(),
     &FSI_touch_flag[tid],
     &CTML_FSI_init,
     &CTML_force_model,
     &iter,
     &current_step,
     &plot_interval,
     &ioproc);

    num_tiles_on_thread_proc[tid]++;
   } //mfi
}//omp
   for (int tid=1;tid<thread_class::nthreads;tid++) {
    if (FSI_touch_flag[tid]==1) {
     FSI_touch_flag[0]=1;
    } else if (FSI_touch_flag[tid]==0) {
     // do nothing
    } else
     BoxLib::Error("FSI_touch_flag[tid] invalid");
   } 
   ParallelDescriptor::Barrier();
   ParallelDescriptor::ReduceIntMax(FSI_touch_flag[0]);

   if (num_materials_vel!=1)
    BoxLib::Error("num_materials_vel invalid");

   // idx,ngrow,scomp,ncomp,index,scompBC_map
   // InterpBordersGHOST is ultimately called.
   // dest_lstGHOST for Solid_State_Type defaults to pc_interp.
   // scompBC_map==0 corresponds to extrap_bc, pc_interp and FORT_EXTRAPFILL
   // scompBC_map==1,2,3 corresponds to x or y or z vel_extrap_bc, pc_interp 
   //   and FORT_EXTRAPFILL
   // nFSI=nparts * (vel + LS + temp + flag + stress)
   for (int partid=0;partid<nparts;partid++) {
    int ibase=partid*nFSI_sub;
    Array<int> scompBC_map;
    scompBC_map.resize(BL_SPACEDIM); 
    for (int dir=0;dir<BL_SPACEDIM;dir++)
     scompBC_map[dir]=dir+1;

    // This routine interpolates from coarser levels.
    PCINTERP_fill_borders(FSI_MF,ngrowFSI,ibase,
     BL_SPACEDIM,Solid_State_Type,scompBC_map);

    for (int i=BL_SPACEDIM;i<nFSI_sub;i++) {
     scompBC_map.resize(1); 
     scompBC_map[0]=0;
     PCINTERP_fill_borders(FSI_MF,ngrowFSI,ibase+i,
      1,Solid_State_Type,scompBC_map);
    } // i=BL_SPACEDIM  ... nFSI_sub-1
   } // partid=0..nparts-1

    // 1. copy_velocity_on_sign
    // 2. update Solid_new
    // 3. update LS_new
    // 4. update S_new(temperature) (if solidheat_flag==1 or 2)

   Transfer_FSI_To_STATE();

   delete solidmf;
   delete denmf;
   delete LSMF;

  } else if (FSI_operation==4) { // copy Eul. vel to struct vel.

   elements_generated=1;
   if (ngrowFSI!=3)
    BoxLib::Error("ngrowFSI invalid");
   if (num_materials_vel!=1)
    BoxLib::Error("num_materials_vel invalid");
   if ((FSI_sub_operation!=0)&&
       (FSI_sub_operation!=1)&&
       (FSI_sub_operation!=2))
    BoxLib::Error("FSI_sub_operation invalid");

   // (1) =1 interior  =1 fine-fine ghost in domain  =0 otherwise
   // (2) =1 interior  =0 otherwise
   resize_mask_nbr(ngrowFSI);
   debug_ngrow(MASK_NBR_MF,ngrowFSI,2);
   // mask=1 if not covered or if outside the domain.
   // NavierStokes::maskfiner_localMF
   // NavierStokes::maskfiner
   resize_maskfiner(ngrowFSI,MASKCOEF_MF);
   debug_ngrow(MASKCOEF_MF,ngrowFSI,28);

   if ((FSI_sub_operation==0)|| //init VELADVECT_MF, fortran grid structure,...
       (FSI_sub_operation==2)) {//delete VELADVECT_MF

    if (FSI_sub_operation==0) {
     // Two layers of ghost cells are needed if
     // (INTP_CORONA = 1) in UTIL_BOUNDARY_FORCE_FSI.F90
     getState_localMF(VELADVECT_MF,ngrowFSI,0,
      num_materials_vel*BL_SPACEDIM,cur_time_slab); 

      // in: NavierStokes::ns_header_msg_level
     create_fortran_grid_struct(time,dt);
    } else if (FSI_sub_operation==2) {
     delete_localMF(VELADVECT_MF,1);
    } else
     BoxLib::Error("FSI_sub_operation invalid");

    int ngrowFSI_fab=0;
    IntVect unitlo(D_DECL(0,0,0));
    IntVect unithi(D_DECL(0,0,0));
     // construct cell-centered type box
    Box unitbox(unitlo,unithi);

    const int* tilelo=unitbox.loVect();
    const int* tilehi=unitbox.hiVect();
    const int* fablo=unitbox.loVect();
    const int* fabhi=unitbox.hiVect();

    FArrayBox FSIfab(unitbox,nFSI);

    if (num_materials_vel!=1)
     BoxLib::Error("num_materials_vel invalid");

    Array<int> velbc;
    velbc.resize(num_materials_vel*BL_SPACEDIM*2*BL_SPACEDIM);
    for (int i=0;i<velbc.size();i++)
     velbc[i]=0;
    Array<int> vofbc;
    vofbc.resize(2*BL_SPACEDIM);
    for (int i=0;i<vofbc.size();i++)
     vofbc[i]=0;

    int tid=0;
    int gridno=0;

    FORT_HEADERMSG(
     &tid,
     &num_tiles_on_thread_proc[tid],
     &gridno,
     &thread_class::nthreads,
     &level,
     &finest_level,
     &max_level,
     &FSI_operation, // 4
     &FSI_sub_operation, // 0 (clear lag data) or 2 (sync lag data)
     tilelo,tilehi,
     fablo,fabhi,
     &bfact,
     problo,
     problen, 
     dx_maxlevel, 
     problo,
     probhi, 
     velbc.dataPtr(),  
     vofbc.dataPtr(), 
     FSIfab.dataPtr(), // placeholder
     ARLIM(FSIfab.loVect()),ARLIM(FSIfab.hiVect()),
     FSIfab.dataPtr(), // velfab spot
     ARLIM(FSIfab.loVect()),ARLIM(FSIfab.hiVect()),
     FSIfab.dataPtr(), // mnbrfab spot
     ARLIM(FSIfab.loVect()),ARLIM(FSIfab.hiVect()),
     FSIfab.dataPtr(), // mfiner spot
     ARLIM(FSIfab.loVect()),ARLIM(FSIfab.hiVect()),
     &nFSI,
     &nFSI_sub,
     &ngrowFSI_fab,
     &nparts,
     im_solid_map.dataPtr(),
     &h_small,
     &time, 
     &dt, 
     FSI_refine_factor.dataPtr(),
     FSI_bounding_box_ngrow.dataPtr(),
     &FSI_touch_flag[tid],
     &CTML_FSI_init,
     &CTML_force_model,
     &iter,
     &current_step,
     &plot_interval,
     &ioproc);

   } else if (FSI_sub_operation==1) {

#ifdef _OPENMP
#pragma omp parallel
#endif
{
    for (MFIter mfi(*localMF[VELADVECT_MF],use_tiling); mfi.isValid(); ++mfi) {
     BL_ASSERT(grids[mfi.index()] == mfi.validbox());
     const int gridno = mfi.index();
     const Box& tilegrid = mfi.tilebox();
     const Box& fabgrid = grids[gridno];
     const int* tilelo=tilegrid.loVect();
     const int* tilehi=tilegrid.hiVect();
     const int* fablo=fabgrid.loVect();
     const int* fabhi=fabgrid.hiVect();
     const Real* xlo = grid_loc[gridno].lo();
     FArrayBox& FSIfab=(*localMF[VELADVECT_MF])[mfi]; // placeholder
     FArrayBox& velfab=(*localMF[VELADVECT_MF])[mfi]; // ngrowFSI ghost cells
     FArrayBox& mnbrfab=(*localMF[MASK_NBR_MF])[mfi];
     FArrayBox& mfinerfab=(*localMF[MASKCOEF_MF])[mfi];

     Array<int> velbc=getBCArray(State_Type,gridno,0,
      num_materials_vel*BL_SPACEDIM);
     Array<int> vofbc=getBCArray(State_Type,gridno,scomp_mofvars,1);

     int tid=ns_thread();
     if ((tid<0)||(tid>=thread_class::nthreads))
      BoxLib::Error("tid invalid");

     FORT_HEADERMSG(
      &tid,
      &num_tiles_on_thread_proc[tid],
      &gridno,
      &thread_class::nthreads,
      &level,
      &finest_level,
      &max_level,
      &FSI_operation, // 4 (copy eul. fluid vel to lag. solid vel)
      &FSI_sub_operation, // 1 
      tilelo,tilehi,
      fablo,fabhi,
      &bfact,
      xlo,
      dx, 
      dx_maxlevel, 
      problo,
      probhi, 
      velbc.dataPtr(),  
      vofbc.dataPtr(), 
      FSIfab.dataPtr(), // placeholder
      ARLIM(FSIfab.loVect()),ARLIM(FSIfab.hiVect()),
      velfab.dataPtr(), // ngrowFSI ghost cells
      ARLIM(velfab.loVect()),ARLIM(velfab.hiVect()),
      mnbrfab.dataPtr(),
      ARLIM(mnbrfab.loVect()),ARLIM(mnbrfab.hiVect()),
      mfinerfab.dataPtr(),
      ARLIM(mfinerfab.loVect()),ARLIM(mfinerfab.hiVect()),
      &nFSI,
      &nFSI_sub,
      &ngrowFSI,
      &nparts,
      im_solid_map.dataPtr(),
      &h_small,
      &time, 
      &dt, 
      FSI_refine_factor.dataPtr(),
      FSI_bounding_box_ngrow.dataPtr(),
      &FSI_touch_flag[tid],
      &CTML_FSI_init,
      &CTML_force_model,
      &iter,
      &current_step,
      &plot_interval,
      &ioproc);

     num_tiles_on_thread_proc[tid]++;
    } //mfi
}//omp

   } else 
    BoxLib::Error("FSI_sub_operation invalid");

  } else
   BoxLib::Error("FSI_operation invalid");

 } else if (do_FSI()==0) {
  // do nothing
 } else
  BoxLib::Error("do_FSI invalid");

 if (verbose>0) {
  if (ParallelDescriptor::IOProcessor()) {
   std::cout << "ns_header_msg_level FINISH\n";
   std::cout << "level= " << level << " finest_level= " << finest_level <<
    " max_level= " << max_level << '\n';
   std::cout << "FSI_operation= " << FSI_operation <<
    " FSI_sub_operation= " << FSI_sub_operation <<
    " time = " << time << " dt= " << dt << " iter = " << iter << '\n';
  }
 } else if (verbose==0) {
  // do nothing
 } else
  BoxLib::Error("verbose invalid");

} // end subroutine ns_header_msg_level

// called from Amr::restart 
void NavierStokes::post_restart() {

 SDC_setup();
 ns_time_order=parent->Time_blockingFactor();
 slab_step=ns_time_order-1;

 SDC_outer_sweeps=0;
 SDC_setup_step();

 if (verbose>0)
  if (ParallelDescriptor::IOProcessor())
   std::cout << "in post_restart: level " << level << '\n';

 for (int dir=0;dir<BL_SPACEDIM;dir++) {
  std::cout << "dir mfiter_tile_size " << dir << ' ' <<
    FabArrayBase::mfiter_tile_size[dir] << '\n';
 }

 const int max_level = parent->maxLevel();
 const Real* dx = geom.CellSize();
 const Box& domain = geom.Domain();
 const int* domlo = domain.loVect();
 const int* domhi = domain.hiVect();

 Real problo[BL_SPACEDIM];
 Real probhi[BL_SPACEDIM];
 for (int dir=0;dir<BL_SPACEDIM;dir++) {
  problo[dir]=Geometry::ProbLo(dir);
  probhi[dir]=Geometry::ProbHi(dir);
 }

 MultiFab& S_new = get_new_data(State_Type,slab_step+1);
 int nmat=num_materials;
 int nten=( (nmat-1)*(nmat-1)+nmat-1 )/2;
 int nc=S_new.nComp();

 FORT_INITDATA_ALLOC(&nmat,&nten,&nc,
  latent_heat.dataPtr(),
  freezing_model.dataPtr(),
  distribute_from_target.dataPtr(),
  saturation_temp.dataPtr(),
  dx);

 if (level==0) {

  Array<int> bfact_space_level(max_level+1);
  Array<int> bfact_grid_level(max_level+1);
  for (int ilev=0;ilev<=max_level;ilev++) {
   bfact_space_level[ilev]=parent->Space_blockingFactor(ilev);
   bfact_grid_level[ilev]=parent->blockingFactor(ilev);
  }
  FORT_INITGRIDMAP(
    &max_level,
    bfact_space_level.dataPtr(),
    bfact_grid_level.dataPtr(),
    domlo,domhi,
    dx,
    problo,probhi);

 } else if ((level>0)&&(level<=max_level)) {
  // do nothing
 } else {
  BoxLib::Error("level invalid post_restart() ");
 }

 metrics_data(2);  

 Real dt_amr=parent->getDt(); // returns dt_AMR

 int iter=0;
  // in post_restart: initialize node locations; generate_new_triangles
 int FSI_operation=0; 
 int FSI_sub_operation=0; 
 ns_header_msg_level(FSI_operation,FSI_sub_operation,
   upper_slab_time,dt_amr,iter); 

   // inside of post_restart
 if (level==0) {

  int post_init_flag=2; // post_restart
  prepare_post_process(post_init_flag);

  if (sum_interval>0) {
   sum_integrated_quantities(post_init_flag);
  }

 } else if (level>0) {
  // do nothing
 } else {
  BoxLib::Error("level invalid20");
 } 

}  // subroutine post_restart


// This routine might be called twice at level 0 if AMR program
// run on more than one processor.  At level=0, the BoxArray used in
// "defbaselevel" can be different from the boxarray that optimizes
// load balancing.
void
NavierStokes::initData () {

 Real strt_time=0.0;

 int bfact_space=parent->Space_blockingFactor(level);
 int bfact_grid=parent->blockingFactor(level);

 bool use_tiling=ns_tiling;

 if (ParallelDescriptor::IOProcessor()) {
  std::cout << "initData() at level= " << level << '\n';
  std::cout << "amr.space_blocking_factor= " << bfact_space << '\n';
  std::cout << "amr.blocking_factor= " << bfact_grid << '\n';
  for (int dir=0;dir<BL_SPACEDIM;dir++) {
   std::cout << "dir mfiter_tile_size " << dir << ' ' <<
     FabArrayBase::mfiter_tile_size[dir] << '\n';
  }
 }

 SDC_setup();
 ns_time_order=parent->Time_blockingFactor();
 slab_step=ns_time_order-1;

 SDC_outer_sweeps=0;
 SDC_setup_step();

 int nmat=num_materials;
 if (ngeom_raw!=BL_SPACEDIM+1)
  BoxLib::Error("ngeom_raw bust");

 int nten=( (nmat-1)*(nmat-1)+nmat-1 )/2;

 int max_level = parent->maxLevel();

 const Real* dx = geom.CellSize();
 const Box& domain = geom.Domain();
 const int* domlo = domain.loVect();
 const int* domhi = domain.hiVect();
 Real problo[BL_SPACEDIM];
 Real probhi[BL_SPACEDIM];

 for (int dir=0;dir<BL_SPACEDIM;dir++) {
  problo[dir]=Geometry::ProbLo(dir);
  probhi[dir]=Geometry::ProbHi(dir);
 }

 if (upper_slab_time!=0.0)
  BoxLib::Error("upper_slab_time should be zero at the very beginning");

 if (level==0) {

  Array<int> bfact_space_level(max_level+1);
  Array<int> bfact_grid_level(max_level+1);
  for (int ilev=0;ilev<=max_level;ilev++) {
   bfact_space_level[ilev]=parent->Space_blockingFactor(ilev);
   bfact_grid_level[ilev]=parent->blockingFactor(ilev);
  }
  FORT_INITGRIDMAP(
   &max_level,
   bfact_space_level.dataPtr(),
   bfact_grid_level.dataPtr(),
   domlo,domhi,
   dx,
   problo,probhi);

 } else if ((level>0)&&(level<=max_level)) {
  // do nothing
 } else {
  BoxLib::Error("level invalid 4");
 }

 metrics_data(1);

 if (level==0) {

  int at_least_one_ice=0;
  for (int im=1;im<=nmat;im++) {
   if (is_ice_matC(im-1)==1)
    at_least_one_ice=1;
  }

  Array<int> recalesce_material;
  recalesce_material.resize(nmat);

  int at_least_one=0;
  for (int im=1;im<=nmat;im++) {
   recalesce_material[im-1]=parent->AMR_recalesce_flag(im);
   if (parent->AMR_recalesce_flag(im)>0) {
    if (at_least_one_ice!=1)
     BoxLib::Error("expecting FSI_flag==3");
    at_least_one=1;
   }
  }
      
  Array<Real> recalesce_state_old;
  int recalesce_num_state=6;
  recalesce_state_old.resize(recalesce_num_state*nmat);
  if (at_least_one==1) {
   parent->recalesce_init(nmat);
   parent->recalesce_get_state(recalesce_state_old,nmat);
  } else if (at_least_one==0) {
   for (int im=0;im<recalesce_num_state*nmat;im++) {
    recalesce_state_old[im]=-1.0;
   }
  } else
   BoxLib::Error("at_least_one invalid");

    // this must be done before the volume fractions, centroids, and
    // level set function are initialized.
  FORT_INITRECALESCE(
   recalesce_material.dataPtr(),
   recalesce_state_old.dataPtr(),
   &recalesce_num_state,&nmat); 

 } else if ((level>0)&&(level<=max_level)) {
  // do nothing
 } else {
  BoxLib::Error("level invalid 5");
 } 

 Real dt_amr=parent->getDt(); // returns dt_AMR

  // velocity,pres,state x nmat,interface variables x nmat, error ind
 MultiFab& S_new = get_new_data(State_Type,slab_step+1);
 int nc=S_new.nComp();
 int dencomp=num_materials_vel*(BL_SPACEDIM+1);
 int nc_expect=dencomp+nmat*num_state_material+nmat*ngeom_raw+1;
 if (nc!=nc_expect)
  BoxLib::Error("nc invalid in initdata");

 MultiFab& LS_new = get_new_data(LS_Type,slab_step+1);
 if (LS_new.nComp()!=nmat*(BL_SPACEDIM+1))
  BoxLib::Error("LS_new invalid ncomp");

 MultiFab& DIV_new = get_new_data(DIV_Type,slab_step+1);
 if (DIV_new.nComp()!=num_materials_vel)
  BoxLib::Error("DIV_new.nComp()!=num_materials_vel");

 int nparts=im_solid_map.size();

 if ((nparts>=1)&&(nparts<=nmat-1)) {  
  MultiFab& Solid_new = get_new_data(Solid_State_Type,slab_step+1);
  if (Solid_new.nComp()!=nparts*BL_SPACEDIM)
   BoxLib::Error("Solid_new.nComp()!=nparts*BL_SPACEDIM");
  Solid_new.setVal(0.0,0,nparts*BL_SPACEDIM,1);
 } else if (nparts==0) {
  // do nothing
 } else 
  BoxLib::Error("nparts invalid");

 int nparts_tensor=im_elastic_map.size();

 if ((nparts_tensor>=1)&&(nparts_tensor<=nmat)) {  
  MultiFab& Tensor_new = get_new_data(Tensor_Type,slab_step+1);
  if (Tensor_new.nComp()!=nparts_tensor*NUM_TENSOR_TYPE)
   BoxLib::Error("Tensor_new.nComp()!=nparts_tensor*NUM_TENSOR_TYPE");
  Tensor_new.setVal(0.0,0,nparts_tensor*NUM_TENSOR_TYPE,1);
 } else if (nparts_tensor==0) {
  // do nothing
 } else 
  BoxLib::Error("nparts_tensor invalid");

 DIV_new.setVal(0.0);

 S_new.setVal(0.0,0,nc,1);
 LS_new.setVal(-99999.0,0,nmat,1);
 LS_new.setVal(0.0,nmat,nmat*BL_SPACEDIM,1); // slopes

 for (int dir=0;dir<BL_SPACEDIM;dir++) {
  MultiFab& Smac_new = get_new_data(Umac_Type+dir,slab_step+1);

  int nsolve=1;
  int nsolveMM_FACE=nsolve*num_materials_vel;

  if (num_materials_vel!=1)
   BoxLib::Error("num_materials_vel invalid");

  if (Smac_new.nComp()!=nsolveMM_FACE) {
   std::cout << "nmat = " << nmat << '\n';
   std::cout << "num_materials_vel = " << num_materials_vel << '\n';
   std::cout << "nsolveMM_FACE = " << nsolveMM_FACE << '\n';
   BoxLib::Error("Smac_new.nComp() invalid in initData");
  }
  Smac_new.setVal(0.0,0,nsolveMM_FACE,0);
 }  // dir=0..sdim-1

 int iter=0; // =>  FSI_touch_flag[tid]=0
  // in initData: initialize node locations; generate_new_triangles
 int FSI_operation=0; 
 int FSI_sub_operation=0; 
 ns_header_msg_level(FSI_operation,FSI_sub_operation,
   upper_slab_time,dt_amr,iter); 

  // create a distance function (velocity and temperature) on this level.
  // calls ns_header_msg_level with FSI_operation==2,3
  // ns_header_msg_level calls NavierStokes::Transfer_FSI_To_STATE
 prepare_mask_nbr(1);
 FSI_make_distance(upper_slab_time,dt_amr);

 FORT_INITDATA_ALLOC(&nmat,&nten,&nc,
  latent_heat.dataPtr(),
  freezing_model.dataPtr(),
  distribute_from_target.dataPtr(),
  saturation_temp.dataPtr(),
  dx);

 if (verbose>0) {
  if (ParallelDescriptor::IOProcessor()) {
   std::cout << "initdata loop follows->level=" << level << 
     " max_level= " << max_level << '\n';
  }
 }
 
 // initialize one ghost cell of LS_new so that LS_stencil needed
 // by the AMR error indicator can be initialized for those
 // level set components with FSI_flag==2,4.
 MultiFab* lsmf=getStateDist(1,cur_time_slab,101);
 MultiFab::Copy(LS_new,*lsmf,0,0,nmat*(1+BL_SPACEDIM),1);
 delete lsmf;
 
#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(S_new,use_tiling); mfi.isValid(); ++mfi) {
  BL_ASSERT(grids[mfi.index()] == mfi.validbox());
  const int gridno = mfi.index();
  const Box& tilegrid = mfi.tilebox();
  const Box& fabgrid = grids[gridno];
  const int* tilelo=tilegrid.loVect();
  const int* tilehi=tilegrid.hiVect();
  const int* fablo=fabgrid.loVect();
  const int* fabhi=fabgrid.hiVect();
  const Real* xlo = grid_loc[gridno].lo();
  const Real* xhi = grid_loc[gridno].hi();

  int tid=ns_thread();

   // if FSI_flag==2,4, then LS_new is used.
   // if FSI_flag==2,4, then S_new (volume fractions and centroids)
   //  is used.
  FORT_INITDATA(
   &tid,
   &adapt_quad_depth,
   &level,&max_level,
   &upper_slab_time,
   tilelo,tilehi,
   fablo,fabhi,
   &bfact_space,
   &nc,
   &nmat,&nten,
   latent_heat.dataPtr(),
   saturation_temp.dataPtr(),
   radius_cutoff.dataPtr(),
   S_new[mfi].dataPtr(),
   ARLIM(S_new[mfi].loVect()),
   ARLIM(S_new[mfi].hiVect()),
   LS_new[mfi].dataPtr(),
   ARLIM(LS_new[mfi].loVect()),
   ARLIM(LS_new[mfi].hiVect()),
   dx,xlo,xhi);  

  if (1==0) {
   FArrayBox& snewfab=S_new[mfi];
   int interior_only=1;
   tecplot_debug(snewfab,xlo,fablo,fabhi,dx,-1,0,dencomp, 
    nmat*num_state_material,interior_only); 
  }
 } //mfi
}//omp
 ParallelDescriptor::Barrier();

 if (do_FSI()==1)
  build_moment_from_FSILS();

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(S_new,use_tiling); mfi.isValid(); ++mfi) {
  BL_ASSERT(grids[mfi.index()] == mfi.validbox());

  const int gridno = mfi.index();
  const Box& tilegrid = mfi.tilebox();
  const Box& fabgrid = grids[gridno];
  const int* tilelo=tilegrid.loVect();
  const int* tilehi=tilegrid.hiVect();
  const int* fablo=fabgrid.loVect();
  const int* fabhi=fabgrid.hiVect();

  const Real* xlo = grid_loc[gridno].lo();
  const Real* xhi = grid_loc[gridno].hi();

  const int* s_lo = S_new[mfi].loVect();
  const int* s_hi = S_new[mfi].hiVect();

  Real Reynolds=visc_coef*viscconst[0];
  if (Reynolds>0.0) Reynolds=1.0/Reynolds;
  Real Weber=tension[0];
  if (Weber>0.0) Weber=1.0/Weber;
  Real RGASRWATER=probhi[0];
  if (xblob>0.0) RGASRWATER/=xblob;

  if (num_materials_vel!=1)
   BoxLib::Error("num_materials_vel invalid");

  FORT_INITVELOCITY(
   &level,&upper_slab_time,
   tilelo,tilehi,
   fablo,fabhi,&bfact_space,
   S_new[mfi].dataPtr(),
   ARLIM(s_lo),ARLIM(s_hi),
   dx,xlo,xhi,
   &Reynolds,&Weber,&RGASRWATER,&use_lsa );

 } // mfi
}//omp
 ParallelDescriptor::Barrier();

 // for FSI_flag==2 or 4: (prescribed sci_clsvof.F90 rigid material)
 // 1. copy_velocity_on_sign
 // 2. update Solid_new
 // 3. update LS_new
 // 4. update S_new(temperature) (if solidheat_flag==1 or 2)
 Transfer_FSI_To_STATE();

  // if nparts>0,
  //  Initialize FSI_GHOST_MF from Solid_State_Type
  // Otherwise initialize FSI_GHOST_MF with the fluid velocity.
 init_FSI_GHOST_MF(1);
 
 init_regrid_history();
 is_first_step_after_regrid=-1;

 int nstate=state.size();
 if (nstate!=NUM_STATE_TYPE)
  BoxLib::Error("nstate invalid");
 for (int k=0;k<nstate;k++) {
  state[k].CopyNewToOld();  // olddata=newdata 
   // time_array[0]=strt_time-dt_amr  
   // time_array[bfact_time_order]=strt_time
  state[k].setTimeLevel(strt_time,dt_amr); 
 }

}  // subroutine initData

void NavierStokes::init_boundary_list(Array<int> scomp,
  Array<int> ncomp) {

 MultiFab& S_new=get_new_data(State_Type,slab_step+1);

 int ncomp_list=0;
 for (int ilist=0;ilist<scomp.size();ilist++) {
  MultiFab* state_mf=getState(1,scomp[ilist],ncomp[ilist],cur_time_slab);
  MultiFab::Copy(S_new,*state_mf,0,scomp[ilist],ncomp[ilist],1);
  delete state_mf;
  ncomp_list+=ncomp[ilist];
 }
 if (ncomp_list<=0)
  BoxLib::Error("ncomp_list invalid");

} // subroutine init_boundary_list

void NavierStokes::init_boundary() {

 if (num_state_base!=2)
  BoxLib::Error("num_state_base invalid");

 int nstate=state.size();
 if (nstate!=NUM_STATE_TYPE)
  BoxLib::Error("nstate invalid");

 int nmat=num_materials;
 int mofcomp=num_materials_vel*(BL_SPACEDIM+1)+
  nmat*num_state_material;
 int dencomp=num_materials_vel*(BL_SPACEDIM+1);
 int nden=nmat*num_state_material;

 for (int k=0;k<nstate;k++) {

  if (k==State_Type) {
   MultiFab& S_new=get_new_data(State_Type,slab_step+1);
   MultiFab* vofmf=getState(1,mofcomp,nmat*ngeom_raw,cur_time_slab);
   MultiFab::Copy(S_new,*vofmf,0,mofcomp,nmat*ngeom_raw,1);
   delete vofmf;
   MultiFab* velmf=getState(1,0,
    num_materials_vel*(BL_SPACEDIM+1),cur_time_slab);
   MultiFab::Copy(S_new,*velmf,0,0,
    num_materials_vel*(BL_SPACEDIM+1),1);
   delete velmf;
   MultiFab* denmf=getStateDen(1,cur_time_slab);  
   MultiFab::Copy(S_new,*denmf,0,dencomp,nden,1);
   delete denmf;
  } else if (k==Umac_Type) {
   // do nothing
  } else if (k==Vmac_Type) {
   // do nothing
  } else if (k==Wmac_Type) {
   // do nothing
  } else if (k==LS_Type) {
   MultiFab& LS_new=get_new_data(LS_Type,slab_step+1);
   MultiFab* lsmf=getStateDist(1,cur_time_slab,3);  
   MultiFab::Copy(LS_new,*lsmf,0,0,nmat*(1+BL_SPACEDIM),1);
   delete lsmf;
  } else if (k==DIV_Type) {
   // do nothing
  } else if (k==Solid_State_Type) {
   int nparts=im_solid_map.size();
   if ((nparts<1)||(nparts>nmat-1))
    BoxLib::Error("nparts invalid");
   MultiFab& Solid_new=get_new_data(Solid_State_Type,slab_step+1);
   MultiFab* velmf=getStateSolid(1,0,nparts*BL_SPACEDIM,cur_time_slab);
   MultiFab::Copy(Solid_new,*velmf,0,0,nparts*BL_SPACEDIM,1);
   delete velmf;
  } else if (k==Tensor_Type) {
   int nparts=im_elastic_map.size();
   if ((nparts<1)||(nparts>nmat))
    BoxLib::Error("nparts invalid");
   if (nparts!=num_materials_viscoelastic)
    BoxLib::Error("nparts!=num_materials_viscoelastic");
   MultiFab& Tensor_new=get_new_data(Tensor_Type,slab_step+1);
   MultiFab* tensormf=getStateTensor(1,0,nparts*NUM_TENSOR_TYPE,cur_time_slab);
   MultiFab::Copy(Tensor_new,*tensormf,0,0,nparts*NUM_TENSOR_TYPE,1);
   delete tensormf;
  } else 
   BoxLib::Error("k invalid");

 } // k=0..nstate-1

}  // subroutine init_boundary()

void
NavierStokes::init (AmrLevel &old,
  const BoxArray& ba_in,
  const DistributionMapping& dmap_in) {
 
 NavierStokes* oldns     = (NavierStokes*) &old;

 SDC_setup();
 ns_time_order=parent->Time_blockingFactor();
 slab_step=ns_time_order-1;

 SDC_outer_sweeps=0;

 upper_slab_time=oldns->state[State_Type].slabTime(ns_time_order);
 lower_slab_time=oldns->state[State_Type].slabTime(0);
 cur_time_slab=upper_slab_time;
 prev_time_slab=oldns->state[State_Type].slabTime(ns_time_order-1);

  // in: init(&old,ba_in,dmap_in)
 dt_slab=cur_time_slab-prev_time_slab;
 delta_slab_time=upper_slab_time-lower_slab_time;

 if (verbose>0) {
  if (ParallelDescriptor::IOProcessor()) {
   std::cout << "init(old)  level,cur_time,dt " << level << ' ' <<
     upper_slab_time << ' ' << delta_slab_time << '\n';
  }
 }

  // new time: upper_slab_time   old time: upper_slab_time-delta_slab_time
 setTimeLevel(upper_slab_time,delta_slab_time); 

 if (num_state_base!=2)
  BoxLib::Error("num_state_base invalid");

 int nmat=num_materials;
 if (ngeom_raw!=BL_SPACEDIM+1)
  BoxLib::Error("ngeom_raw bust");

 int nstate=state.size();  // cell centered, vel MAC, LS, etc
 if (nstate!=NUM_STATE_TYPE)
  BoxLib::Error("nstate invalid");

 for (int k=0;k<nstate;k++) {

  MultiFab &S_new = get_new_data(k,ns_time_order);
  int ncomp=S_new.nComp();

  int numparts=1;
  int ncomp_part[4];
  int scomp_part[4];
  
  if (k==State_Type) {
   int test_ncomp=0;

   numparts=4;
   scomp_part[0]=0;
   scomp_part[1]=num_materials_vel*(BL_SPACEDIM+1);
   scomp_part[2]=scomp_part[1]+nmat*num_state_material;
   scomp_part[3]=scomp_part[2]+nmat*ngeom_raw;

   ncomp_part[0]=scomp_part[1];
   test_ncomp+=ncomp_part[0];
   ncomp_part[1]=scomp_part[2]-scomp_part[1];
   test_ncomp+=ncomp_part[1];
   ncomp_part[2]=scomp_part[3]-scomp_part[2];
   test_ncomp+=ncomp_part[2];
   ncomp_part[3]=1;
   test_ncomp+=ncomp_part[3];
   if (test_ncomp!=ncomp)
    BoxLib::Error("test ncomp invalid");

  } else {
   numparts=1;
   scomp_part[0]=0;
   ncomp_part[0]=ncomp;
  }
   
  for (int part_iter=0;part_iter<numparts;part_iter++) { 
   FillPatch(old,S_new,scomp_part[part_iter],
      upper_slab_time,k,
      scomp_part[part_iter],
      ncomp_part[part_iter]);
  }
 }  // k

 old_intersect_new = BoxLib::intersect(grids,oldns->boxArray());
 is_first_step_after_regrid = 1;

}  // subroutine init(old)


// init a new level that did not exist on the previous step.
void
NavierStokes::init (const BoxArray& ba_in,
         const DistributionMapping& dmap_in) {

 if (num_state_base!=2)
  BoxLib::Error("num_state_base invalid");

 int nmat=num_materials;
 if (ngeom_raw!=BL_SPACEDIM+1)
  BoxLib::Error("ngeom_raw bust");

 if (level==0)
  BoxLib::Error("this init only called for level>0");

 Real dt_amr=parent->getDt();
 Real dt_new=dt_amr;
 parent->setDt(dt_new);

 NavierStokes& old = getLevel(level-1);

 SDC_setup();
 ns_time_order=parent->Time_blockingFactor();
 slab_step=ns_time_order-1;

 SDC_outer_sweeps=0;

 upper_slab_time = old.state[State_Type].slabTime(ns_time_order);
 lower_slab_time = old.state[State_Type].slabTime(0);
 cur_time_slab=upper_slab_time;
 prev_time_slab=old.state[State_Type].slabTime(ns_time_order-1);

  // in: init (ba_in,dmap_in)
 dt_slab=cur_time_slab-prev_time_slab;
 delta_slab_time=upper_slab_time-lower_slab_time;

 if (verbose>0) {
  if (ParallelDescriptor::IOProcessor()) {
   std::cout << "init()  level,cur_time,dt " << level << ' ' <<
     upper_slab_time << ' ' << delta_slab_time << '\n';
  }
 }
  
  // new time: upper_slab_time   old time: upper_slab_time-delta_slab_time
 setTimeLevel(upper_slab_time,delta_slab_time);

 int nstate=state.size();
 if (nstate!=NUM_STATE_TYPE)
  BoxLib::Error("nstate invalid");

 for (int k=0;k<nstate;k++) {

  MultiFab &S_new = get_new_data(k,ns_time_order);
  int ncomp=S_new.nComp();

  int numparts=1;
  int ncomp_part[4];
  int scomp_part[4];
  
  if (k==State_Type) {
   int test_ncomp=0;

   numparts=4;
   scomp_part[0]=0;
   scomp_part[1]=num_materials_vel*(BL_SPACEDIM+1);
   scomp_part[2]=scomp_part[1]+nmat*num_state_material;
   scomp_part[3]=scomp_part[2]+nmat*ngeom_raw;

   ncomp_part[0]=scomp_part[1];
   test_ncomp+=ncomp_part[0];
   ncomp_part[1]=scomp_part[2]-scomp_part[1];
   test_ncomp+=ncomp_part[1];
   ncomp_part[2]=scomp_part[3]-scomp_part[2];
   test_ncomp+=ncomp_part[2];
   ncomp_part[3]=1;
   test_ncomp+=ncomp_part[3];
   if (test_ncomp!=ncomp)
    BoxLib::Error("test ncomp invalid");

  } else {
   numparts=1;
   scomp_part[0]=0;
   ncomp_part[0]=ncomp;
  }

  for (int part_iter=0;part_iter<numparts;part_iter++) { 
   FillCoarsePatch(S_new,scomp_part[part_iter],upper_slab_time,k,
     scomp_part[part_iter],ncomp_part[part_iter]);
  }
 } // k

 init_regrid_history();
 is_first_step_after_regrid = 2;

}  // subroutine init()

void NavierStokes::CopyNewToOldALL() {

 int finest_level=parent->finestLevel();
 if (level!=0)
  BoxLib::Error("level invalid CopyNewToOldALL");
 for (int ilev=level;ilev<=finest_level;ilev++) {
  NavierStokes& ns_level=getLevel(ilev);
   // oldtime=newtime newtime+=dt
  int nstate=ns_level.state.size();
  if (nstate!=NUM_STATE_TYPE)
   BoxLib::Error("nstate invalid");
  for (int k = 0; k < nstate; k++) {
   ns_level.state[k].CopyNewToOld();  
  }
 }
 int nmat=num_materials;

 int at_least_one=0;
 for (int im=1;im<=nmat;im++) {
  if (parent->AMR_recalesce_flag(im)>0)
   at_least_one=1;
 }
 if (at_least_one==1) {
  parent->recalesce_copy_new_to_old(nmat);
 } else if (at_least_one==0) {
  // do nothing
 } else
  BoxLib::Error("at_least_one invalid");

}  // subroutine CopyNewToOldALL


void NavierStokes::CopyOldToNewALL() {

 int finest_level=parent->finestLevel();
 if (level!=0)
  BoxLib::Error("level invalid CopyOldToNewALL");
 for (int ilev=level;ilev<=finest_level;ilev++) {
  NavierStokes& ns_level=getLevel(ilev);
   // oldtime=newtime=start_time newtime+=dt
  int nstate=ns_level.state.size();
  if (nstate!=NUM_STATE_TYPE)
   BoxLib::Error("nstate invalid");
  for (int k = 0; k < nstate; k++) {
   ns_level.state[k].CopyOldToNew();  
  }
 }

 int nmat=num_materials;

 int at_least_one=0;
 for (int im=1;im<=nmat;im++) {
  if (parent->AMR_recalesce_flag(im)>0)
   at_least_one=1;
 }
 if (at_least_one==1) {
  parent->recalesce_copy_old_to_new(nmat);
 } else if (at_least_one==0) {
  // do nothing
 } else
  BoxLib::Error("at_least_one invalid");

} // subroutine CopyOldToNewALL


void NavierStokes::Number_CellsALL(Real& rcells) {

 int finest_level=parent->finestLevel();
 if (level!=0)
  BoxLib::Error("level invalid Number_CellsALL");
 rcells=0.0;
 for (int ilev=0;ilev<=finest_level;ilev++) {
  NavierStokes& ns_level=getLevel(ilev);
  int ngrids=ns_level.grids.size();
  for (int gridno=0;gridno<ngrids;gridno++) {
   const Box& gridbox=ns_level.grids[gridno];
   rcells+=gridbox.numPts();
  }
 }
   
}  

// called from:
//  NavierStokes::do_the_advance  (near beginning)
//  NavierStokes::prepare_post_process (near beginning)
void NavierStokes::allocate_mdot() {

 if (num_materials_vel!=1)
  BoxLib::Error("num_materials_vel invalid");

 int nsolve=1;

 if (localMF_grow[MDOT_MF]>=0) {
  delete_localMF(MDOT_MF,1);
 } 

  // MDOT has nsolve components.
 new_localMF(MDOT_MF,nsolve,0,-1);
 setVal_localMF(MDOT_MF,0.0,0,nsolve,0); //val,scomp,ncomp,ngrow

} // end subroutine allocate_mdot()

// slab_step and SDC_outer_sweeps are set before calling this routine.
void
NavierStokes::SDC_setup_step() {

 if (num_state_base!=2)
  BoxLib::Error("num_state_base invalid");

 int nmat=num_materials;
 if ((nmat<1)||(nmat>1000))
  BoxLib::Error("nmat out of range");

 nfluxSEM=BL_SPACEDIM+num_state_base;
  // I-scheme,thermal conduction,viscosity,div(up),gp,-force
 nstate_SDC=nfluxSEM+1+BL_SPACEDIM+1+BL_SPACEDIM+BL_SPACEDIM;

 ns_time_order=parent->Time_blockingFactor();

 if ((SDC_outer_sweeps<0)||
     (SDC_outer_sweeps>=ns_time_order))
  BoxLib::Error("SDC_outer_sweeps invalid");

 divu_outer_sweeps=0;

 lower_slab_time=state[State_Type].slabTime(0);
 upper_slab_time=state[State_Type].slabTime(ns_time_order);
 delta_slab_time=upper_slab_time-lower_slab_time;
 if (delta_slab_time<0.0) {
  std::cout << "lower_slab_time= " << lower_slab_time << '\n';
  std::cout << "upper_slab_time= " << upper_slab_time << '\n';
  std::cout << "delta_slab_time= " << delta_slab_time << '\n';
  BoxLib::Error("delta_slab_time invalid");
 }
 if ((delta_slab_time==0.0)&&(upper_slab_time>0.0)) {
  std::cout << "lower_slab_time= " << lower_slab_time << '\n';
  std::cout << "upper_slab_time= " << upper_slab_time << '\n';
  std::cout << "delta_slab_time= " << delta_slab_time << '\n';
  BoxLib::Error("delta_slab_time invalid");
 }

 if ((slab_step>=0)&&(slab_step<ns_time_order)) {

  prev_time_slab=state[State_Type].slabTime(slab_step);
  cur_time_slab=state[State_Type].slabTime(slab_step+1);
  vel_time_slab=cur_time_slab;
  prescribed_vel_time_slab=cur_time_slab;

   // in: NavierStokes::SDC_setup_step()
  dt_slab=cur_time_slab-prev_time_slab;
  if (dt_slab<0.0)
   BoxLib::Error("dt_slab invalid1");
  if ((dt_slab==0.0)&&(cur_time_slab>0.0))
   BoxLib::Error("dt_slab invalid2");

 } else if ((slab_step==-1)&&(ns_time_order>=2)) {
  prev_time_slab=lower_slab_time;
  cur_time_slab=lower_slab_time;
  dt_slab=1.0;
  vel_time_slab=cur_time_slab;
  prescribed_vel_time_slab=cur_time_slab;
 } else if ((slab_step==ns_time_order)&&(ns_time_order>=2)) {
  prev_time_slab=upper_slab_time;
  cur_time_slab=upper_slab_time;
  dt_slab=1.0;
  vel_time_slab=cur_time_slab;
  prescribed_vel_time_slab=cur_time_slab;
 } else
  BoxLib::Error("slab_step invalid"); 

} // SDC_setup_step()


void
NavierStokes::SDC_setup() {

 SDC_outer_sweeps=0;
 divu_outer_sweeps=0;
 prev_time_slab=0.0;
 cur_time_slab=0.0;
 vel_time_slab=0.0;
 prescribed_vel_time_slab=0.0;
 dt_slab=1.0;

 upper_slab_time=0.0;
 lower_slab_time=0.0;
 delta_slab_time=1.0;

} // subroutine SDC_setup()

void 
NavierStokes::Geometry_setup() {

 for (int i=0;i<MAX_NUM_LOCAL_MF;i++) {
  localMF_grow[i]=-1;
  localMF[i]=0;  // null pointer
 }

 SDC_setup();
 ns_time_order=1;
 slab_step=0;

}

void 
NavierStokes::Geometry_cleanup() {

 for (int i=0;i<MAX_NUM_LOCAL_MF;i++) {
  if (localMF_grow[i]>=0) {
   delete localMF[i];
   localMF_grow[i]=-1;
   localMF[i]=0;
  } else if (localMF_grow[i]==-1) {
   if (localMF[i]==0) {
    // do nothing
   } else {
    BoxLib::Error("localMF[i] invalid");
   }
  } else {
   BoxLib::Error("localMF_grow[i] invalid");
  }
 } // i=0 ... MAX_NUM_LOCAL_MF-1   

} // subroutine Geometry_cleanup()

void NavierStokes::SOD_SANITY_CHECK(int id) {

 MultiFab& snew=get_new_data(State_Type,slab_step+1);
 int nc=snew.nComp();

 for (MFIter mfi(snew); mfi.isValid(); ++mfi) {
  BL_ASSERT(grids[mfi.index()] == mfi.validbox());
  const int gridno = mfi.index();
  const Box& fabgrid = grids[gridno];
  const int* fablo=fabgrid.loVect();
  const int* fabhi=fabgrid.hiVect();
  FArrayBox& snewfab=snew[mfi];
  FORT_SOD_SANITY(
    &id,&nc,fablo,fabhi,
    snewfab.dataPtr(),ARLIM(snewfab.loVect()),ARLIM(snewfab.hiVect()));
 }
 ParallelDescriptor::Barrier(); 

} // subroutine SOD_SANITY_CHECK

void NavierStokes::make_viscoelastic_tensor(int im) {

 int nmat=num_materials;
 bool use_tiling=ns_tiling;

 if ((num_materials_viscoelastic>=1)&&(num_materials_viscoelastic<=nmat)) {
  // do nothing
 } else
  BoxLib::Error("num_materials_viscoelastic  invalid");

 if (num_state_base!=2)
  BoxLib::Error("num_state_base invalid");

 if (localMF_grow[VISCOTEN_MF]!=-1)
  delete_localMF(VISCOTEN_MF,1);

 int ngrow=1;  // number of grow cells for the tensor
 for (int dir=0;dir<BL_SPACEDIM;dir++)
  debug_ngrow(FACE_VAR_MF+dir,0,2);
 debug_ngrow(CELL_VISC_MATERIAL_MF,ngrow,3);

 MultiFab& S_new=get_new_data(State_Type,slab_step+1);

 if (localMF[CELL_VISC_MATERIAL_MF]->nComp()<nmat)
  BoxLib::Error("cell_visc_material ncomp invalid");
 if (localMF[CELL_VISC_MATERIAL_MF]->nGrow()<ngrow)
  BoxLib::Error("cell_visc_material ngrow invalid");

 int nstate=num_materials_vel*(BL_SPACEDIM+1)+
   nmat*(num_state_material+ngeom_raw)+1;
 if (nstate!=S_new.nComp())
  BoxLib::Error("nstate invalid");

 if ((im<0)||(im>=nmat))
  BoxLib::Error("im invalid52");

 if (ns_is_rigid(im)==0) {

  if ((elastic_time[im]>0.0)&&(elastic_viscosity[im]>0.0)) {

   int partid=0;
   while ((im_elastic_map[partid]!=im)&&(partid<im_elastic_map.size())) {
    partid++;
   }

   if (partid<im_elastic_map.size()) {

    int scomp_tensor=partid*NUM_TENSOR_TYPE;

    if (NUM_TENSOR_TYPE!=2*BL_SPACEDIM)
     BoxLib::Error("NUM_TENSOR_TYPE invalid");

    getStateTensor_localMF(VISCOTEN_MF,1,scomp_tensor,NUM_TENSOR_TYPE,
     cur_time_slab);

    int rzflag=0;
    if (CoordSys::IsRZ())
     rzflag=1;
    else if (CoordSys::IsCartesian())
     rzflag=0;
    else if (CoordSys::IsCYLINDRICAL())
     rzflag=3;
    else
     BoxLib::Error("CoordSys bust 2");

    const Real* dx = geom.CellSize();

#ifdef _OPENMP
#pragma omp parallel
#endif
{
    for (MFIter mfi(*localMF[VISCOTEN_MF],use_tiling); mfi.isValid(); ++mfi) {
     BL_ASSERT(grids[mfi.index()] == mfi.validbox());
     const int gridno = mfi.index();
     const Box& tilegrid = mfi.tilebox();
     const Box& fabgrid = grids[gridno];
     const int* tilelo=tilegrid.loVect();
     const int* tilehi=tilegrid.hiVect();
     const int* fablo=fabgrid.loVect();
     const int* fabhi=fabgrid.hiVect();
     int bfact=parent->Space_blockingFactor(level);

     const Real* xlo = grid_loc[gridno].lo();

     FArrayBox& tenfab=(*localMF[VISCOTEN_MF])[mfi];
     // 1. maketensor: TQ_{m}=alpha_{m} Q_{m}
     // 2. tensor force: F= div (H_{m} TQ_{m})
     //    H=H(F-1/2) or H=H(phi)

     FArrayBox& viscfab=(*localMF[CELL_VISC_MATERIAL_MF])[mfi];
     int ncomp_visc=viscfab.nComp();

     FORT_MAKETENSOR(
      &ncomp_visc,&im, 
      xlo,dx,
      viscfab.dataPtr(),ARLIM(viscfab.loVect()),ARLIM(viscfab.hiVect()),
      tenfab.dataPtr(),ARLIM(tenfab.loVect()),ARLIM(tenfab.hiVect()),
      tilelo,tilehi,
      fablo,fabhi,&bfact,
      &elastic_viscosity[im],&etaS[im],
      &elastic_time[im],
      &viscoelastic_model[im],
      &polymer_factor[im],
      &rzflag,&ngrow,&nmat);
    }  // mfi  
}//omp
    ParallelDescriptor::Barrier(); 
   } else
    BoxLib::Error("partid could not be found: make_viscoelastic_tensor");

  } else if ((elastic_time[im]==0.0)||
             (elastic_viscosity[im]==0.0)) {

   if (viscoelastic_model[im]!=0)
    BoxLib::Error("viscoelastic_model[im]!=0");

  } else
   BoxLib::Error("elastic_time/elastic_viscosity invalid");


 } else if (ns_is_rigid(im)==1) {
  // do nothing
 } else
  BoxLib::Error("ns_is_rigid invalid");

}  // subroutine make_viscoelastic_tensor


void NavierStokes::make_viscoelastic_heating(int im,int idx) {

 bool use_tiling=ns_tiling;

 int nmat=num_materials;
 int nden=nmat*num_state_material;

 if (num_materials_vel!=1)
  BoxLib::Error("num_materials_vel invalid");

 int ntensor=BL_SPACEDIM*BL_SPACEDIM;
 int ntensorMM=ntensor*num_materials_vel;

 if ((num_materials_viscoelastic>=1)&&(num_materials_viscoelastic<=nmat)) {
  // do nothing
 } else
  BoxLib::Error("num_materials_viscoelastic invalid");

 if (num_state_base!=2)
  BoxLib::Error("num_state_base invalid");

 int ngrow=1;  // number of grow cells for the tensor

 for (int dir=0;dir<BL_SPACEDIM;dir++)
  debug_ngrow(FACE_VAR_MF+dir,0,2);

 debug_ngrow(idx,0,4);
 if (localMF[idx]->nComp()!=num_materials_vel)
  BoxLib::Error("localMF[idx]->nComp() invalid");

 debug_ngrow(CELLTENSOR_MF,1,6);
 if (localMF[CELLTENSOR_MF]->nComp()!=ntensorMM)
  BoxLib::Error("localMF[CELLTENSOR_MF]->nComp() invalid");

 debug_ngrow(CELL_VISC_MATERIAL_MF,ngrow,3);

 debug_ngrow(CELL_DEN_MF,1,28); 
 debug_ngrow(CELL_DEDT_MF,1,28); 

 if (localMF[CELL_DEN_MF]->nComp()!=nmat+1)
  BoxLib::Error("localMF[CELL_DEN_MF]->nComp() invalid");
 if (localMF[CELL_DEDT_MF]->nComp()!=nmat+1)
  BoxLib::Error("localMF[CELL_DEDT_MF]->nComp() invalid");

 // 1. viscosity coefficient - 1..nmat
 // 2. viscoelastic coefficient - 1..nmat
 // 3. relaxation time - 1..nmat
 // the viscous and viscoelastic forces should both be multiplied by
 // visc_coef.  
 if (localMF[CELL_VISC_MATERIAL_MF]->nComp()<nmat)
  BoxLib::Error("cell_visc_material ncomp invalid");
 if (localMF[CELL_VISC_MATERIAL_MF]->nGrow()<ngrow)
  BoxLib::Error("cell_visc_material ngrow invalid");
 MultiFab& S_new=get_new_data(State_Type,slab_step+1);

 int nstate=num_materials_vel*(BL_SPACEDIM+1)+
   nmat*(num_state_material+ngeom_raw)+1;
 if (nstate!=S_new.nComp())
  BoxLib::Error("nstate invalid");

 if ((im<0)||(im>=nmat))
  BoxLib::Error("im invalid53");

 if (ns_is_rigid(im)==0) {

  if ((elastic_time[im]>0.0)&&(elastic_viscosity[im]>0.0)) {

   debug_ngrow(VISCOTEN_MF,1,5);
   if (localMF[VISCOTEN_MF]->nComp()!=NUM_TENSOR_TYPE)
    BoxLib::Error("localMF[VISCOTEN_MF] invalid");

   int ncomp_visc=localMF[CELL_VISC_MATERIAL_MF]->nComp();
   if (ncomp_visc!=3*nmat)
    BoxLib::Error("cell_visc_material ncomp invalid");

   resize_levelsetLO(2,LEVELPC_MF);
   debug_ngrow(LEVELPC_MF,2,5);

   int rzflag=0;
   if (CoordSys::IsRZ())
    rzflag=1;
   else if (CoordSys::IsCartesian())
    rzflag=0;
   else if (CoordSys::IsCYLINDRICAL())
    rzflag=3;
   else
    BoxLib::Error("CoordSys bust 2");

   const Real* dx = geom.CellSize();

#ifdef _OPENMP
#pragma omp parallel
#endif
{
   for (MFIter mfi(*localMF[VISCOTEN_MF],use_tiling); mfi.isValid(); ++mfi) {
    BL_ASSERT(grids[mfi.index()] == mfi.validbox());
    const int gridno = mfi.index();
    const Box& tilegrid = mfi.tilebox();
    const Box& fabgrid = grids[gridno];
    const int* tilelo=tilegrid.loVect();
    const int* tilehi=tilegrid.hiVect();
    const int* fablo=fabgrid.loVect();
    const int* fabhi=fabgrid.hiVect();
    int bfact=parent->Space_blockingFactor(level);

    const Real* xlo = grid_loc[gridno].lo();

    FArrayBox& tenfab=(*localMF[VISCOTEN_MF])[mfi];
    if (tenfab.nComp()!=NUM_TENSOR_TYPE)
     BoxLib::Error("tenfab.nComp invalid");

    FArrayBox& DeDTinversefab=(*localMF[CELL_DEDT_MF])[mfi]; // 1/(rho cv)
    if (DeDTinversefab.nComp()!=nmat+1)
     BoxLib::Error("DeDTinversefab.nComp() invalid");

    FArrayBox& gradufab=(*localMF[CELLTENSOR_MF])[mfi];
    if (gradufab.nComp()!=ntensorMM)
     BoxLib::Error("gradufab.nComp() invalid");

    FArrayBox& heatfab=(*localMF[idx])[mfi];
    if (heatfab.nComp()!=num_materials_vel)
     BoxLib::Error("heatfab.nComp() invalid");

    FArrayBox& lsfab=(*localMF[LEVELPC_MF])[mfi];
    FArrayBox& viscfab=(*localMF[CELL_VISC_MATERIAL_MF])[mfi];
    if (viscfab.nComp()<nmat)
     BoxLib::Error("viscfab.nComp() invalid");

    FArrayBox& xface=(*localMF[FACE_VAR_MF])[mfi];
    FArrayBox& yface=(*localMF[FACE_VAR_MF+1])[mfi];
    FArrayBox& zface=(*localMF[FACE_VAR_MF+BL_SPACEDIM-1])[mfi];

    FORT_TENSORHEAT(
     &elasticface_flag,
     &massface_index,
     &vofface_index,
     &ncphys,
     &ntensor,
     &ntensorMM,
     &nstate,
     xlo,dx,
     xface.dataPtr(),ARLIM(xface.loVect()),ARLIM(xface.hiVect()), 
     yface.dataPtr(),ARLIM(yface.loVect()),ARLIM(yface.hiVect()), 
     zface.dataPtr(),ARLIM(zface.loVect()),ARLIM(zface.hiVect()), 
     lsfab.dataPtr(),ARLIM(lsfab.loVect()),ARLIM(lsfab.hiVect()),
     DeDTinversefab.dataPtr(),
     ARLIM(DeDTinversefab.loVect()),ARLIM(DeDTinversefab.hiVect()),
     heatfab.dataPtr(),
     ARLIM(heatfab.loVect()),ARLIM(heatfab.hiVect()),
     tenfab.dataPtr(),
     ARLIM(tenfab.loVect()),ARLIM(tenfab.hiVect()),
     gradufab.dataPtr(),
     ARLIM(gradufab.loVect()),ARLIM(gradufab.hiVect()),
     tilelo,tilehi,
     fablo,fabhi,&bfact,&level,
     &dt_slab,&rzflag,&im,&nmat,&nden);
   }  // mfi  
}//omp
   ParallelDescriptor::Barrier(); 
  } else if ((elastic_time[im]==0.0)||
             (elastic_viscosity[im]==0.0)) {

   if (viscoelastic_model[im]!=0)
    BoxLib::Error("viscoelastic_model[im]!=0");

  } else
   BoxLib::Error("elastic_time/elastic_viscosity invalid");

 } else if (ns_is_rigid(im)==1) {
  // do nothing
 } else
  BoxLib::Error("ns_is_rigid invalid");

}   // subroutine make_viscoelastic_heating

// TO DO: for elastic materials, the force should be calculated
//  in such a way as to preserve the reversibility property that
//  materials return to their original shape if the external
//  forces are turned off. 
void NavierStokes::make_viscoelastic_force(int im) {

 int nmat=num_materials;
 bool use_tiling=ns_tiling;

 if ((num_materials_viscoelastic>=1)&&(num_materials_viscoelastic<=nmat)) {
  // do nothing
 } else
  BoxLib::Error("num_materials_viscoelastic invalid");

 if (num_state_base!=2)
  BoxLib::Error("num_state_base invalid");

 int ngrow=1;

 for (int dir=0;dir<BL_SPACEDIM;dir++)
  debug_ngrow(FACE_VAR_MF+dir,0,2);

 debug_ngrow(CELL_VISC_MATERIAL_MF,ngrow,3);

 int nden=nmat*num_state_material;

 MultiFab& S_new=get_new_data(State_Type,slab_step+1);

 if (localMF[CELL_VISC_MATERIAL_MF]->nComp()<nmat)
  BoxLib::Error("cell_visc_material ncomp invalid");
 if (localMF[CELL_VISC_MATERIAL_MF]->nGrow()<ngrow)
  BoxLib::Error("cell_visc_material ngrow invalid");

 int nstate=num_materials_vel*(BL_SPACEDIM+1)+
   nmat*(num_state_material+ngeom_raw)+1;
 if (nstate!=S_new.nComp())
  BoxLib::Error("nstate invalid");

 if ((im<0)||(im>=nmat))
  BoxLib::Error("im invalid54");

 if (ns_is_rigid(im)==0) {

  if ((elastic_time[im]>0.0)&&(elastic_viscosity[im]>0.0)) {

   debug_ngrow(VISCOTEN_MF,1,5);
   int ncomp_visc=localMF[CELL_VISC_MATERIAL_MF]->nComp();
   if (ncomp_visc!=3*nmat)
    BoxLib::Error("cell_visc_material ncomp invalid");

   resize_levelsetLO(2,LEVELPC_MF);
   debug_ngrow(LEVELPC_MF,2,5);
   if (localMF[LEVELPC_MF]->nComp()!=nmat*(BL_SPACEDIM+1))
    BoxLib::Error("localMF[LEVELPC_MF]->nComp()!=nmat*(BL_SPACEDIM+1)");

   int rzflag=0;
   if (CoordSys::IsRZ())
    rzflag=1;
   else if (CoordSys::IsCartesian())
    rzflag=0;
   else if (CoordSys::IsCYLINDRICAL())
    rzflag=3;
   else
    BoxLib::Error("CoordSys bust 2");

   const Real* dx = geom.CellSize();

#ifdef _OPENMP
#pragma omp parallel
#endif
{
   for (MFIter mfi(*localMF[VISCOTEN_MF],use_tiling); mfi.isValid(); ++mfi) {
    BL_ASSERT(grids[mfi.index()] == mfi.validbox());
    const int gridno = mfi.index();
    const Box& tilegrid = mfi.tilebox();
    const Box& fabgrid = grids[gridno];
    const int* tilelo=tilegrid.loVect();
    const int* tilehi=tilegrid.hiVect();
    const int* fablo=fabgrid.loVect();
    const int* fabhi=fabgrid.hiVect();
    int bfact=parent->Space_blockingFactor(level);

    const Real* xlo = grid_loc[gridno].lo();

    FArrayBox& tenfab=(*localMF[VISCOTEN_MF])[mfi];
    FArrayBox& rhoinversefab=(*localMF[CELL_DEN_MF])[mfi];

    FArrayBox& lsfab=(*localMF[LEVELPC_MF])[mfi];
    FArrayBox& viscfab=(*localMF[CELL_VISC_MATERIAL_MF])[mfi];
    if (viscfab.nComp()<nmat)
     BoxLib::Error("viscfab.nComp() invalid");

    FArrayBox& xface=(*localMF[FACE_VAR_MF])[mfi];
    FArrayBox& yface=(*localMF[FACE_VAR_MF+1])[mfi];
    FArrayBox& zface=(*localMF[FACE_VAR_MF+BL_SPACEDIM-1])[mfi];

    FORT_TENSORFORCE(
     &elasticface_flag,
     &massface_index,
     &vofface_index,
     &ncphys,
     &nstate,
     xlo,dx,
     xface.dataPtr(),ARLIM(xface.loVect()),ARLIM(xface.hiVect()), 
     yface.dataPtr(),ARLIM(yface.loVect()),ARLIM(yface.hiVect()), 
     zface.dataPtr(),ARLIM(zface.loVect()),ARLIM(zface.hiVect()), 
     lsfab.dataPtr(),ARLIM(lsfab.loVect()),ARLIM(lsfab.hiVect()),
     rhoinversefab.dataPtr(),
     ARLIM(rhoinversefab.loVect()),ARLIM(rhoinversefab.hiVect()),
     S_new[mfi].dataPtr(),
     ARLIM(S_new[mfi].loVect()),ARLIM(S_new[mfi].hiVect()),
     tenfab.dataPtr(),
     ARLIM(tenfab.loVect()),ARLIM(tenfab.hiVect()),
     tilelo,tilehi,
     fablo,fabhi,&bfact,&level,
     &dt_slab,&rzflag,&im,&nmat,&nden);
   }  // mfi  
} // omp
   ParallelDescriptor::Barrier(); 

  } else if ((elastic_time[im]==0.0)||
             (elastic_viscosity[im]==0.0)) {

   if (viscoelastic_model[im]!=0)
    BoxLib::Error("viscoelastic_model[im]!=0");

  } else
   BoxLib::Error("elastic_time/elastic_viscosity invalid");


 } else if (ns_is_rigid(im)==1) {
  // do nothing
 } else
  BoxLib::Error("ns_is_rigid invalid");

}   // subroutine make_viscoelastic_force

void NavierStokes::make_marangoni_force(int isweep) {

 bool use_tiling=ns_tiling;

 int finest_level=parent->finestLevel();

 if ((level<0)||(level>finest_level))
  BoxLib::Error("level invalid make_marangoni_force");

 int nmat=num_materials;

 if (num_state_base!=2)
  BoxLib::Error("num_state_base invalid");
 if (num_materials_vel!=1)
  BoxLib::Error("num_materials_vel invalid");

 resize_levelsetLO(2,LEVELPC_MF);
 debug_ngrow(LEVELPC_MF,2,5);
 if (localMF[LEVELPC_MF]->nComp()!=nmat*(BL_SPACEDIM+1))
  BoxLib::Error("localMF[LEVELPC_MF]->nComp() invalid");

 for (int dir=0;dir<BL_SPACEDIM;dir++)
  debug_ngrow(FACE_VAR_MF+dir,0,2);

 VOF_Recon_resize(2,SLOPE_RECON_MF);
 debug_ngrow(SLOPE_RECON_MF,2,3);
 debug_ngrow(DIST_CURV_MF,1,3);
 debug_ngrow(CELL_DEN_MF,1,5);
 if (localMF[CELL_DEN_MF]->nComp()!=nmat+1)
  BoxLib::Error("localMF[CELL_DEN_MF]->nComp() invalid");

  // mask=1 if not covered or if outside the domain.
  // NavierStokes::maskfiner_localMF
  // NavierStokes::maskfiner
 resize_maskfiner(2,MASKCOEF_MF);
 debug_ngrow(MASKCOEF_MF,2,28);
 resize_mask_nbr(2);
 debug_ngrow(MASK_NBR_MF,2,2);

 resize_metrics(2);

 MultiFab* CL_velocity;

 for (int dir=0;dir<BL_SPACEDIM;dir++) {
  debug_ngrow(CONSERVE_FLUXES_MF+dir,0,7);
  if (localMF[CONSERVE_FLUXES_MF+dir]->nComp()!=BL_SPACEDIM)
   BoxLib::Error("localMF[CONSERVE_FLUXES_MF+dir]->nComp() invalid");
 }   

 if (isweep==0) {
   // second order coarse-fine interp for LS
  getStateDist_localMF(GHOSTDIST_MF,2,cur_time_slab,16);
  getStateDen_localMF(DEN_RECON_MF,2,prev_time_slab);

  CL_velocity=getState(2,0,num_materials_vel*(BL_SPACEDIM+1),prev_time_slab);
  if (CL_velocity->nComp()!=BL_SPACEDIM+1)
   BoxLib::Error("CL_velocity->nComp()!=BL_SPACEDIM+1");

  for (int dir=0;dir<BL_SPACEDIM;dir++) {
   setVal_localMF(CONSERVE_FLUXES_MF+dir,0.0,0,BL_SPACEDIM,0);
   new_localMF(POTENTIAL_EDGE_MF+dir,2*BL_SPACEDIM,0,dir); //Ften-,Ften+
   setVal_localMF(POTENTIAL_EDGE_MF+dir,0.0,0,2*BL_SPACEDIM,0);
  }
 } else if (isweep==1) {
  CL_velocity=localMF[GHOSTDIST_MF];
  if (CL_velocity->nComp()!=localMF[GHOSTDIST_MF]->nComp())
   BoxLib::Error("CL_velocity->nComp()!=localMF[GHOSTDIST_MF]->nComp()");
 } else {
  BoxLib::Error("isweep invalid");
 }
 if (CL_velocity->nGrow()!=2)
  BoxLib::Error("CL_velocity->nGrow()!=2");

 debug_ngrow(GHOSTDIST_MF,2,5);
 if (localMF[GHOSTDIST_MF]->nComp()!=nmat*(BL_SPACEDIM+1))
  BoxLib::Error("localMF[GHOSTDIST_MF]->nComp() invalid");
 debug_ngrow(DEN_RECON_MF,2,5);
 if (localMF[DEN_RECON_MF]->nComp()!=nmat*num_state_material)
  BoxLib::Error("den_recon has invalid ncomp");

 int nden=nmat*num_state_material;
 int nten=( (nmat-1)*(nmat-1)+nmat-1 )/2;

 MultiFab& S_new=get_new_data(State_Type,slab_step+1);

 int nstate=num_materials_vel*(BL_SPACEDIM+1)+
   nmat*(num_state_material+ngeom_raw)+1;
 if (nstate!=S_new.nComp())
  BoxLib::Error("nstate invalid");

 int scomp_mofvars=num_materials_vel*(BL_SPACEDIM+1)+
  nmat*num_state_material;

 const Real* dx = geom.CellSize();

  // height function curvature
  // finite difference curvature
  // pforce
  // marangoni force
  // dir/side flag
  // im3
  // x nten
 int num_curv=nten*(5+BL_SPACEDIM);
 if (localMF[DIST_CURV_MF]->nComp()!=num_curv)
  BoxLib::Error("DIST_CURV invalid ncomp");
  
 MultiFab& Umac_new=get_new_data(Umac_Type,slab_step+1);
 MultiFab& Vmac_new=get_new_data(Umac_Type+1,slab_step+1);
 MultiFab& Wmac_new=get_new_data(Umac_Type+BL_SPACEDIM-1,slab_step+1);

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(*localMF[DIST_CURV_MF],use_tiling); mfi.isValid(); ++mfi) {
  BL_ASSERT(grids[mfi.index()] == mfi.validbox());
  const int gridno = mfi.index();
  const Box& tilegrid = mfi.tilebox();
  const Box& fabgrid = grids[gridno];
  const int* tilelo=tilegrid.loVect();
  const int* tilehi=tilegrid.hiVect();
  const int* fablo=fabgrid.loVect();
  const int* fabhi=fabgrid.hiVect();
  int bfact=parent->Space_blockingFactor(level);
  int bfact_grid=parent->blockingFactor(level);

  const Real* xlo = grid_loc[gridno].lo();

   // mask=tag if not covered by level+1 or outside the domain.
  FArrayBox& maskcov=(*localMF[MASKCOEF_MF])[mfi];

  FArrayBox& curvfab=(*localMF[DIST_CURV_MF])[mfi];
  FArrayBox& voffab=(*localMF[SLOPE_RECON_MF])[mfi];
  FArrayBox& rhoinversefab=(*localMF[CELL_DEN_MF])[mfi];
  FArrayBox& lsfab=(*localMF[LEVELPC_MF])[mfi];
  FArrayBox& lshofab=(*localMF[GHOSTDIST_MF])[mfi];
  FArrayBox& velfab=(*CL_velocity)[mfi];
  FArrayBox& denfab=(*localMF[DEN_RECON_MF])[mfi];
   // mask_nbr:
   // (1) =1 interior  =1 fine-fine ghost in domain  =0 otherwise
   // (2) =1 interior  =0 otherwise
   // (3) =1 interior+ngrow-1  =0 otherwise
   // (4) =1 interior+ngrow    =0 otherwise
  FArrayBox& masknbr=(*localMF[MASK_NBR_MF])[mfi];
  FArrayBox& areax=(*localMF[AREA_MF])[mfi];
  FArrayBox& areay=(*localMF[AREA_MF+1])[mfi];
  FArrayBox& areaz=(*localMF[AREA_MF+BL_SPACEDIM-1])[mfi];
  FArrayBox& xflux=(*localMF[CONSERVE_FLUXES_MF])[mfi];
  FArrayBox& yflux=(*localMF[CONSERVE_FLUXES_MF+1])[mfi];
  FArrayBox& zflux=(*localMF[CONSERVE_FLUXES_MF+BL_SPACEDIM-1])[mfi];
  FArrayBox& volfab=(*localMF[VOLUME_MF])[mfi];
  FArrayBox& xface=(*localMF[FACE_VAR_MF])[mfi];
  FArrayBox& yface=(*localMF[FACE_VAR_MF+1])[mfi];
  FArrayBox& zface=(*localMF[FACE_VAR_MF+BL_SPACEDIM-1])[mfi];
  FArrayBox& xp=(*localMF[POTENTIAL_EDGE_MF])[mfi];
  FArrayBox& yp=(*localMF[POTENTIAL_EDGE_MF+1])[mfi];
  FArrayBox& zp=(*localMF[POTENTIAL_EDGE_MF+BL_SPACEDIM-1])[mfi];

  Array<int> presbc=getBCArray(State_Type,gridno,
   num_materials_vel*BL_SPACEDIM,1);
  Array<int> velbc=getBCArray(State_Type,gridno,0,
   num_materials_vel*BL_SPACEDIM);
  Array<int> vofbc=getBCArray(State_Type,gridno,scomp_mofvars,1);

  // force=dt * div((I-nn^T)(sigma) delta) / rho
  FORT_MARANGONIFORCE(
   &conservative_tension_force,
   &isweep,
   &nstate,
   &nten,
   &num_curv,
   xlo,dx,
   &facecut_index,
   &icefacecut_index,
   &curv_index,
   &pforce_index,
   &faceden_index,
   &icemask_index,
   &massface_index,
   &vofface_index,
   &ncphys,
   xface.dataPtr(),ARLIM(xface.loVect()),ARLIM(xface.hiVect()), 
   yface.dataPtr(),ARLIM(yface.loVect()),ARLIM(yface.hiVect()), 
   zface.dataPtr(),ARLIM(zface.loVect()),ARLIM(zface.hiVect()), 
   xp.dataPtr(),ARLIM(xp.loVect()),ARLIM(xp.hiVect()),
   yp.dataPtr(),ARLIM(yp.loVect()),ARLIM(yp.hiVect()),
   zp.dataPtr(),ARLIM(zp.loVect()),ARLIM(zp.hiVect()),
   maskcov.dataPtr(),
   ARLIM(maskcov.loVect()),ARLIM(maskcov.hiVect()),
   masknbr.dataPtr(),
   ARLIM(masknbr.loVect()),ARLIM(masknbr.hiVect()),
   volfab.dataPtr(),ARLIM(volfab.loVect()),ARLIM(volfab.hiVect()),
   areax.dataPtr(),ARLIM(areax.loVect()),ARLIM(areax.hiVect()),
   areay.dataPtr(),ARLIM(areay.loVect()),ARLIM(areay.hiVect()),
   areaz.dataPtr(),ARLIM(areaz.loVect()),ARLIM(areaz.hiVect()),
   xflux.dataPtr(),ARLIM(xflux.loVect()),ARLIM(xflux.hiVect()),
   yflux.dataPtr(),ARLIM(yflux.loVect()),ARLIM(yflux.hiVect()),
   zflux.dataPtr(),ARLIM(zflux.loVect()),ARLIM(zflux.hiVect()),
   velfab.dataPtr(),
   ARLIM(velfab.loVect()),ARLIM(velfab.hiVect()),
   denfab.dataPtr(),
   ARLIM(denfab.loVect()),ARLIM(denfab.hiVect()),
   lsfab.dataPtr(),ARLIM(lsfab.loVect()),ARLIM(lsfab.hiVect()),
   lshofab.dataPtr(),
   ARLIM(lshofab.loVect()),ARLIM(lshofab.hiVect()),
   rhoinversefab.dataPtr(),
   ARLIM(rhoinversefab.loVect()),ARLIM(rhoinversefab.hiVect()),
   voffab.dataPtr(),ARLIM(voffab.loVect()),ARLIM(voffab.hiVect()),
   curvfab.dataPtr(),ARLIM(curvfab.loVect()),ARLIM(curvfab.hiVect()),
   S_new[mfi].dataPtr(),
   ARLIM(S_new[mfi].loVect()),ARLIM(S_new[mfi].hiVect()),
   Umac_new[mfi].dataPtr(),
   ARLIM(Umac_new[mfi].loVect()),ARLIM(Umac_new[mfi].hiVect()),
   Vmac_new[mfi].dataPtr(),
   ARLIM(Vmac_new[mfi].loVect()),ARLIM(Vmac_new[mfi].hiVect()),
   Wmac_new[mfi].dataPtr(),
   ARLIM(Wmac_new[mfi].loVect()),ARLIM(Wmac_new[mfi].hiVect()),
   tilelo,tilehi,
   fablo,fabhi,
   &bfact,
   &bfact_grid,
   &level,
   &finest_level,
   &dt_slab,
   &cur_time_slab,
   &visc_coef,
   &solvability_projection, 
   presbc.dataPtr(),
   velbc.dataPtr(),
   vofbc.dataPtr(),
   &nmat,&nden);
 }  // mfi  
} // omp
 ParallelDescriptor::Barrier(); 

 if (isweep==0) {
  delete CL_velocity;
  int spectral_override=0;  // always do low order average down
  int ncomp_edge=-1;
  avgDownEdge_localMF(
   CONSERVE_FLUXES_MF,
   0,ncomp_edge,0,BL_SPACEDIM,spectral_override,1);
  avgDownEdge_localMF(
   POTENTIAL_EDGE_MF,
   0,ncomp_edge,0,BL_SPACEDIM,spectral_override,2);
 } else if (isweep==1) {
  delete_localMF(GHOSTDIST_MF,1);
  delete_localMF(DEN_RECON_MF,1);
  delete_localMF(POTENTIAL_EDGE_MF,BL_SPACEDIM);
 } else {
  BoxLib::Error("isweep invalid");
 }

}   // make_marangoni_force

int NavierStokes::ns_thread() {
 
 int tid=0;
#ifdef _OPENMP
 tid = omp_get_thread_num();
#endif
 if ((tid<0)||(tid>=thread_class::nthreads))
  BoxLib::Error("tid invalid");

 return tid;
}

// add correction term to velocity and/or temperature
// called from:
//  NavierStokes::multiphase_project
//  NavierStokes::veldiffuseALL
void NavierStokes::make_SEM_delta_force(int project_option) {

 bool use_tiling=ns_tiling;

 if ((slab_step<0)||(slab_step>=ns_time_order))
  BoxLib::Error("slab_step invalid");

 if (num_materials_vel!=1)
  BoxLib::Error("num_materials_vel invalid");

 if ((SDC_outer_sweeps<=0)||
     (SDC_outer_sweeps>=ns_time_order))
  BoxLib::Error("SDC_outer_sweeps invalid");
 if (ns_time_order<=1)
  BoxLib::Error("ns_time_order invalid");

 if (num_state_base!=2)
  BoxLib::Error("num_state_base invalid");

 int nmat=num_materials;

 for (int dir=0;dir<BL_SPACEDIM;dir++)
  debug_ngrow(FACE_VAR_MF+dir,0,2);

 debug_ngrow(delta_MF,0,3);
 debug_ngrow(MASKSEM_MF,1,28); 
 debug_ngrow(CELL_DEN_MF,1,28); 
 debug_ngrow(CELL_DEDT_MF,1,28); 
 if (localMF[CELL_DEN_MF]->nComp()!=nmat+1)
  BoxLib::Error("localMF[CELL_DEN_MF]->nComp() invalid");
 if (localMF[CELL_DEDT_MF]->nComp()!=nmat+1)
  BoxLib::Error("localMF[CELL_DEDT_MF]->nComp() invalid");

 MultiFab& S_new=get_new_data(State_Type,slab_step+1);

 int nstate=num_materials_vel*(BL_SPACEDIM+1)+
   nmat*(num_state_material+ngeom_raw)+1;
 if (nstate!=S_new.nComp())
  BoxLib::Error("nstate invalid");

 const Real* dx = geom.CellSize();
 int bfact=parent->Space_blockingFactor(level);

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(*localMF[delta_MF],use_tiling); mfi.isValid(); ++mfi) {
  BL_ASSERT(grids[mfi.index()] == mfi.validbox());
  const int gridno = mfi.index();
  const Box& tilegrid = mfi.tilebox();
  const Box& fabgrid = grids[gridno];
  const int* tilelo=tilegrid.loVect();
  const int* tilehi=tilegrid.hiVect();
  const int* fablo=fabgrid.loVect();
  const int* fabhi=fabgrid.hiVect();

  const Real* xlo = grid_loc[gridno].lo();

  // I-scheme,thermal conduction,viscosity,div(up),gp,-force
  FArrayBox& deltafab=(*localMF[delta_MF])[mfi];
  int deltacomp=0;
  if (project_option==3) { // viscosity
   deltacomp=slab_step*nstate_SDC+nfluxSEM+1;
  } else if (project_option==4) { // -momentum force at t^n+1
   deltacomp=slab_step*nstate_SDC+nfluxSEM+1+BL_SPACEDIM+1+BL_SPACEDIM;
  } else if (project_option==2) { // thermal conduction
   deltacomp=slab_step*nstate_SDC+nfluxSEM;
  } else if (project_option==0) { // div up and gp 
     // advection, thermal conduction,viscosity,div(up),gp
   deltacomp=slab_step*nstate_SDC+nfluxSEM+1+BL_SPACEDIM;
  } else
   BoxLib::Error("project_option invalid4");

  FArrayBox& rhoinversefab=(*localMF[CELL_DEN_MF])[mfi];
  FArrayBox& DeDTinversefab=(*localMF[CELL_DEDT_MF])[mfi]; // 1/(rho cv)
  FArrayBox& maskSEMfab=(*localMF[MASKSEM_MF])[mfi];

  FORT_SEMDELTAFORCE(
   &nstate,
   &nfluxSEM,
   &nstate_SDC,
   &nmat,
   &project_option,
   xlo,dx,
   deltafab.dataPtr(deltacomp),
   ARLIM(deltafab.loVect()),ARLIM(deltafab.hiVect()),
   maskSEMfab.dataPtr(),
   ARLIM(maskSEMfab.loVect()),ARLIM(maskSEMfab.hiVect()),
   rhoinversefab.dataPtr(),
   ARLIM(rhoinversefab.loVect()),ARLIM(rhoinversefab.hiVect()),
   DeDTinversefab.dataPtr(),
   ARLIM(DeDTinversefab.loVect()),ARLIM(DeDTinversefab.hiVect()),
   S_new[mfi].dataPtr(),
   ARLIM(S_new[mfi].loVect()),ARLIM(S_new[mfi].hiVect()),
   tilelo,tilehi,
   fablo,fabhi,&bfact,&level,
   &dt_slab);
 }  // mfi  
} // omp
 ParallelDescriptor::Barrier(); 

  // pressure gradient at faces.
 if (project_option==0) {

  for (int dir=0;dir<BL_SPACEDIM;dir++) {

   MultiFab& Umac_new=get_new_data(Umac_Type+dir,slab_step+1);

#ifdef _OPENMP
#pragma omp parallel
#endif
{
   for (MFIter mfi(*localMF[delta_MF],use_tiling); mfi.isValid(); ++mfi) {
    BL_ASSERT(grids[mfi.index()] == mfi.validbox());
    const int gridno = mfi.index();
    const Box& tilegrid = mfi.tilebox();
    const Box& fabgrid = grids[gridno];
    const int* tilelo=tilegrid.loVect();
    const int* tilehi=tilegrid.hiVect();
    const int* fablo=fabgrid.loVect();
    const int* fabhi=fabgrid.hiVect();

    const Real* xlo = grid_loc[gridno].lo();

    FArrayBox& deltafab=(*localMF[delta_GP_MF+dir])[mfi];
    int deltacomp=slab_step;
    FArrayBox& xfacefab=(*localMF[FACE_VAR_MF+dir])[mfi];
    FArrayBox& macfab=Umac_new[mfi];
    FArrayBox& maskSEMfab=(*localMF[MASKSEM_MF])[mfi];

      // faceden_index=1/rho
    FORT_SEMDELTAFORCE_FACE(
     &dir,
     &faceden_index,
     &ncphys,
     xlo,dx,
     deltafab.dataPtr(deltacomp),
     ARLIM(deltafab.loVect()),ARLIM(deltafab.hiVect()),
     maskSEMfab.dataPtr(),
     ARLIM(maskSEMfab.loVect()),ARLIM(maskSEMfab.hiVect()),
     xfacefab.dataPtr(),
     ARLIM(xfacefab.loVect()),ARLIM(xfacefab.hiVect()),
     macfab.dataPtr(),
     ARLIM(macfab.loVect()),ARLIM(macfab.hiVect()),
     tilelo,tilehi,
     fablo,fabhi,&bfact,&level,
     &dt_slab);
   }  // mfi  
} // omp
   ParallelDescriptor::Barrier(); 
  } // dir=0..sdim-1

 } else if ((project_option==2)|| // thermal conduction
	    (project_option==3)|| // viscosity
	    (project_option==4)) { // -momentum force at t^n+1
	 // do nothing
 } else {
  BoxLib::Error("project_option invalid");
 } 

}   // subroutine make_SEM_delta_force


// called from veldiffuseALL in NavierStokes3.cpp
void NavierStokes::make_heat_source() {

 bool use_tiling=ns_tiling;

 int finest_level=parent->finestLevel();
 int nmat=num_materials;

 if ((slab_step<0)||(slab_step>=ns_time_order))
  BoxLib::Error("slab_step invalid");

 if (num_state_base!=2)
  BoxLib::Error("num_state_base invalid");

 for (int dir=0;dir<BL_SPACEDIM;dir++)
  debug_ngrow(FACE_VAR_MF+dir,0,2);

 VOF_Recon_resize(1,SLOPE_RECON_MF);
 debug_ngrow(SLOPE_RECON_MF,1,3);
 resize_levelsetLO(2,LEVELPC_MF);
 debug_ngrow(LEVELPC_MF,2,5);
 if (localMF[LEVELPC_MF]->nComp()!=nmat*(BL_SPACEDIM+1))
  BoxLib::Error("localMF[LEVELPC_MF]->nComp() invalid");

 resize_metrics(1);  
 debug_ngrow(VOLUME_MF,1,28); 

 debug_ngrow(CELL_DEN_MF,1,5);
 if (localMF[CELL_DEN_MF]->nComp()!=nmat+1)
  BoxLib::Error("localMF[CELL_DEN_MF]->nComp() invalid");
 debug_ngrow(CELL_DEDT_MF,1,5);
 if (localMF[CELL_DEDT_MF]->nComp()!=nmat+1)
  BoxLib::Error("localMF[CELL_DEDT_MF]->nComp() invalid");

 MultiFab& S_new=get_new_data(State_Type,slab_step+1);

 int nstate=num_materials_vel*(BL_SPACEDIM+1)+
   nmat*(num_state_material+ngeom_raw)+1;
 if (nstate!=S_new.nComp())
  BoxLib::Error("nstate invalid");

 int dencomp=num_materials_vel*(BL_SPACEDIM+1);
 int nden=nmat*num_state_material;

 const Real* dx = geom.CellSize();
 int bfact=parent->Space_blockingFactor(level);

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(S_new,use_tiling); mfi.isValid(); ++mfi) {
  BL_ASSERT(grids[mfi.index()] == mfi.validbox());
  const int gridno = mfi.index();
  const Box& tilegrid = mfi.tilebox();
  const Box& fabgrid = grids[gridno];
  const int* tilelo=tilegrid.loVect();
  const int* tilehi=tilegrid.hiVect();
  const int* fablo=fabgrid.loVect();
  const int* fabhi=fabgrid.hiVect();

  const Real* xlo = grid_loc[gridno].lo();

  FArrayBox& rhoinversefab=(*localMF[CELL_DEN_MF])[mfi];
  FArrayBox& DeDTinversefab=(*localMF[CELL_DEDT_MF])[mfi]; // 1/(rho cv)
  FArrayBox& lsfab=(*localMF[LEVELPC_MF])[mfi];
  FArrayBox& reconfab=(*localMF[SLOPE_RECON_MF])[mfi];
  FArrayBox& volfab=(*localMF[VOLUME_MF])[mfi];
  FArrayBox& snewfab=S_new[mfi];

   // T^{n+1}=T^{n}+dt * heat_source/(rho cv)
   // MKS units:
   // T: Kelvin
   // rho : kg/m^3
   // cv : J/(kg Kelvin)
   // rho cv : J/(m^3 Kelvin)
   // 1/(rho cv) : (m^3 Kelvin)/J
   // heat_source: J/(m^3 s)
   //  
  FORT_HEATSOURCE(
   &nstate,
   &nmat,
   &nden,
   xlo,dx,
   &temperature_source,
   temperature_source_cen.dataPtr(),
   temperature_source_rad.dataPtr(),
   rhoinversefab.dataPtr(),
   ARLIM(rhoinversefab.loVect()),ARLIM(rhoinversefab.hiVect()),
   DeDTinversefab.dataPtr(),
   ARLIM(DeDTinversefab.loVect()),ARLIM(DeDTinversefab.hiVect()),
   snewfab.dataPtr(dencomp),
   ARLIM(snewfab.loVect()),ARLIM(snewfab.hiVect()),
   lsfab.dataPtr(),
   ARLIM(lsfab.loVect()),ARLIM(lsfab.hiVect()),
   reconfab.dataPtr(),
   ARLIM(reconfab.loVect()),ARLIM(reconfab.hiVect()),
   volfab.dataPtr(),
   ARLIM(volfab.loVect()),ARLIM(volfab.hiVect()),
   tilelo,tilehi,
   fablo,fabhi,&bfact,
   &level,
   &finest_level,
   &dt_slab,  // time step within a slab if SDC, otherwise dt is not SDC.
   &prev_time_slab);
 }  // mfi  
} // omp
 ParallelDescriptor::Barrier(); 

}   // subroutine make_heat_source



void NavierStokes::add_perturbation() {

 bool use_tiling=ns_tiling;

 int finest_level=parent->finestLevel();

 if (slab_step!=ns_time_order-1)
  BoxLib::Error("slab_step invalid");

 if (num_state_base!=2)
  BoxLib::Error("num_state_base invalid");

 MultiFab& S_new=get_new_data(State_Type,slab_step+1);
 MultiFab& LS_new = get_new_data(LS_Type,slab_step+1);

 int nmat=num_materials;

 int nstate=num_materials_vel*(BL_SPACEDIM+1)+
   nmat*(num_state_material+ngeom_raw)+1;
 if (nstate!=S_new.nComp())
  BoxLib::Error("nstate invalid");

 const Real* dx = geom.CellSize();
 int bfact=parent->Space_blockingFactor(level);

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(S_new,use_tiling); mfi.isValid(); ++mfi) {
  BL_ASSERT(grids[mfi.index()] == mfi.validbox());
  const int gridno = mfi.index();
  const Box& tilegrid = mfi.tilebox();
  const Box& fabgrid = grids[gridno];
  const int* tilelo=tilegrid.loVect();
  const int* tilehi=tilegrid.hiVect();
  const int* fablo=fabgrid.loVect();
  const int* fabhi=fabgrid.hiVect();

  const Real* xlo = grid_loc[gridno].lo();

  FArrayBox& snewfab=S_new[mfi];
  FArrayBox& lsnewfab=LS_new[mfi];

  for (int dir=0;dir<BL_SPACEDIM;dir++) {

   MultiFab& Umac_new=get_new_data(Umac_Type+dir,slab_step+1);
   FArrayBox& macfab=Umac_new[mfi];

   FORT_ADDNOISE(
    &dir,
    &angular_velocity,
    &perturbation_mode,
    &perturbation_eps_temp,
    &perturbation_eps_vel,
    &nstate,
    &nmat,
    xlo,dx,
    snewfab.dataPtr(),
    ARLIM(snewfab.loVect()),ARLIM(snewfab.hiVect()),
    lsnewfab.dataPtr(),
    ARLIM(lsnewfab.loVect()),ARLIM(lsnewfab.hiVect()),
    macfab.dataPtr(),
    ARLIM(macfab.loVect()),ARLIM(macfab.hiVect()),
    tilelo,tilehi,
    fablo,fabhi,
    &bfact,
    &level,
    &finest_level);
  }  // dir
 }  // mfi  
} // omp
 ParallelDescriptor::Barrier(); 

}   // subroutine add_perturbation

// called from: update_SEM_forces
// update_SEM_forces is called from: update_SEM_forcesALL
// update_SEM_forcesALL is called from:
//   NavierStokes::do_the_advance
//   NavierStokes::veldiffuseALL
void NavierStokes::update_SEM_delta_force(
 int project_option,
 int idx_gp,int idx_gpmac,int idx_div,
 int update_spectral,int update_stable,
 int nsolve) {

 bool use_tiling=ns_tiling;

 int nmat=num_materials;

 if ((ns_time_order>=2)&&(ns_time_order<=32)) {
  // do nothing
 } else
  BoxLib::Error("ns_time_order invalid");

 if ((enable_spectral==1)||(enable_spectral==3)) {
  // do nothing
 } else {
  std::cout << "ns_time_order= " << ns_time_order << '\n';
  std::cout << "project_option= " << project_option << '\n';
  std::cout << "update_spectral= " << update_spectral << '\n';
  std::cout << "update_stable= " << update_stable << '\n';
  BoxLib::Error("enable_spectral invalid in update_SEM_delta_force");
 }

 if ((nsolve!=1)&&(nsolve!=BL_SPACEDIM))
  BoxLib::Error("nsolve invalid37");

 int num_materials_face=num_materials_vel;

 if (project_option==0) {
  if (num_materials_face!=1)
   BoxLib::Error("num_materials_face invalid");
 } else if (project_option==2) { // thermal diffusion
  num_materials_face=num_materials_scalar_solve;
 } else if (project_option==3) { // viscosity
  if (num_materials_face!=1)
   BoxLib::Error("num_materials_face invalid");
 } else if (project_option==4) { // -momentum force
  if (num_materials_face!=1)
   BoxLib::Error("num_materials_face invalid");
 } else
  BoxLib::Error("project_option invalid5");

 int nsolveMM=nsolve*num_materials_face;

 int nsolveMM_FACE=nsolve*num_materials_face;
 if (num_materials_face==1) {
  // do nothing
 } else if (num_materials_face==nmat) {
  nsolveMM_FACE*=2;
 } else
  BoxLib::Error("num_materials_face invalid");

 if (update_stable==1) {
  if ((slab_step<0)||(slab_step>=ns_time_order))
   BoxLib::Error("slab_step invalid");
 } else if (update_stable==0) {
  // do nothing
 } else
  BoxLib::Error("update_stable invalid update sem delta force");

 if (num_state_base!=2)
  BoxLib::Error("num_state_base invalid");

 debug_ngrow(idx_div,0,3);
 debug_ngrow(idx_gp,0,3);

 int idx_hoop;

 if (project_option==0) { // grad p, div(up)
  idx_hoop=idx_div;
  if (nsolve!=1)
   BoxLib::Error("nsolve invalid");
  if (localMF[idx_gp]->nComp()!=nsolveMM*BL_SPACEDIM)
   BoxLib::Error("localMF[idx_gp]->nComp() invalid");
  if (localMF[idx_div]->nComp()!=nsolveMM)
   BoxLib::Error("localMF[idx_div]->nComp() invalid");
 } else if (project_option==2) { // -div(k grad T)-THERMAL_FORCE_MF
  idx_hoop=THERMAL_FORCE_MF;
  if (nsolve!=1)
   BoxLib::Error("nsolve invalid");
  if (localMF[idx_div]->nComp()!=nsolveMM) {
   std::cout << "project_option = " << project_option << '\n';
   std::cout << "idx_div = " << idx_div << '\n';
   std::cout << "nsolveMM = " << nsolveMM << '\n';
   std::cout << "nsolve = " << nsolve << '\n';
   std::cout << "localMF ncomp= " <<
     localMF[idx_div]->nComp() << '\n';
   BoxLib::Error("localMF[idx_div]->nComp() invalid");
  }
  if (localMF[idx_hoop]->nComp()!=nsolveMM) {
   std::cout << "project_option = " << project_option << '\n';
   std::cout << "idx_hoop = " << idx_hoop << '\n';
   std::cout << "nsolveMM = " << nsolveMM << '\n';
   std::cout << "nsolve = " << nsolve << '\n';
   std::cout << "localMF ncomp= " << 
     localMF[idx_hoop]->nComp() << '\n';
   BoxLib::Error("localMF[idx_hoop]->nComp() invalid");
  }
 } else if (project_option==3) { // -div(2 mu D)-HOOP_FORCE_MARK_MF
  idx_hoop=HOOP_FORCE_MARK_MF;
  if (nsolve!=BL_SPACEDIM)
   BoxLib::Error("nsolve invalid");
  if (localMF[idx_div]->nComp()!=nsolveMM) {
   std::cout << "project_option = " << project_option << '\n';
   std::cout << "idx_div = " << idx_div << '\n';
   std::cout << "nsolveMM = " << nsolveMM << '\n';
   std::cout << "nsolve = " << nsolve << '\n';
   std::cout << "localMF ncomp= " <<
     localMF[idx_div]->nComp() << '\n';
   BoxLib::Error("localMF[idx_div]->nComp() invalid");
  }
  if (localMF[idx_hoop]->nComp()!=nsolveMM) {
   std::cout << "project_option = " << project_option << '\n';
   std::cout << "idx_hoop = " << idx_hoop << '\n';
   std::cout << "nsolveMM = " << nsolveMM << '\n';
   std::cout << "nsolve = " << nsolve << '\n';
   std::cout << "localMF ncomp= " <<
     localMF[idx_hoop]->nComp() << '\n';
   BoxLib::Error("localMF[idx_hoop]->nComp() invalid");
  }
 } else if (project_option==4) { // -momentum force
  idx_hoop=idx_div;
  if (idx_div!=idx_gp)
   BoxLib::Error("expecting idx_div==idx_gp");
  if (nsolve!=BL_SPACEDIM)
   BoxLib::Error("nsolve invalid");
  if (localMF[idx_hoop]->nComp()!=nsolveMM)
   BoxLib::Error("localMF[idx_hoop]->nComp() invalid");
 } else
  BoxLib::Error("project_option invalid6");

 debug_ngrow(idx_hoop,0,3);
 if (localMF[idx_hoop]->nComp()!=localMF[idx_div]->nComp())
  BoxLib::Error("localMF[idx_hoop]->nComp() invalid");

 if ((project_option==0)||
     (project_option==2)||
     (project_option==3)) {

  for (int dir=0;dir<BL_SPACEDIM;dir++) {
   debug_ngrow(idx_gpmac+dir,0,3);

   if (project_option==0) {
    if (localMF[idx_gpmac]->nComp()!=nsolveMM_FACE)
     BoxLib::Error("localMF[idx_gpmac]->nComp() invalid");
   } else if ((project_option==2)||
              (project_option==3)) {
    // do nothing
   } else
    BoxLib::Error("project_option invalid7");
 
   debug_ngrow(spectralF_GP_MF+dir,0,3);
   debug_ngrow(stableF_GP_MF+dir,0,3);
   debug_ngrow(delta_GP_MF+dir,0,3);
  } // dir=0..sdim-1

 } else if (project_option==4) { // -momentum force
  // check nothing
 } else
  BoxLib::Error("project_option invalid8");

 debug_ngrow(spectralF_MF,0,3);
 debug_ngrow(stableF_MF,0,3);
 debug_ngrow(delta_MF,0,3);

 debug_ngrow(MASKSEM_MF,1,28); 

 MultiFab& S_new=get_new_data(State_Type,slab_step+1);

 int nstate=num_materials_vel*(BL_SPACEDIM+1)+
   nmat*(num_state_material+ngeom_raw)+1;
 if (nstate!=S_new.nComp())
  BoxLib::Error("nstate invalid");

 const Real* dx = geom.CellSize();
 int bfact=parent->Space_blockingFactor(level);

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(*localMF[delta_MF],use_tiling); mfi.isValid(); ++mfi) {
  BL_ASSERT(grids[mfi.index()] == mfi.validbox());
  const int gridno = mfi.index();
  const Box& tilegrid = mfi.tilebox();
  const Box& fabgrid = grids[gridno];
  const int* tilelo=tilegrid.loVect();
  const int* tilehi=tilegrid.hiVect();
  const int* fablo=fabgrid.loVect();
  const int* fabhi=fabgrid.hiVect();

  const Real* xlo = grid_loc[gridno].lo();

  FArrayBox& gpfab=(*localMF[idx_gp])[mfi];
  FArrayBox& divfab=(*localMF[idx_div])[mfi];
  FArrayBox& hoopfab=(*localMF[idx_hoop])[mfi];
  FArrayBox& HOfab=(*localMF[spectralF_MF])[mfi];
  FArrayBox& LOfab=(*localMF[stableF_MF])[mfi];
  FArrayBox& maskSEMfab=(*localMF[MASKSEM_MF])[mfi];

  int LOcomp=0;
  int HOcomp=0;
  if (slab_step==-1)
   HOcomp=0;
  else if ((slab_step>=0)&&(slab_step<ns_time_order))
   HOcomp=nstate_SDC*(slab_step+1);
  else
   BoxLib::Error("slab_step invalid");

  if (update_stable==1) {
   if ((slab_step>=0)&&(slab_step<ns_time_order))
    LOcomp=nstate_SDC*slab_step;
   else
    BoxLib::Error("slab_step invalid");
  } else if (update_stable==0) {
   // do nothing
  } else
   BoxLib::Error("update_stable invalid");

  FORT_UPDATESEMFORCE(
   &ns_time_order,
   &slab_step,
   &nsolve,
   &update_spectral,
   &update_stable,
   &nstate,
   &nfluxSEM,
   &nstate_SDC,
   &nmat,
   &project_option,
   xlo,dx,
   gpfab.dataPtr(),
   ARLIM(gpfab.loVect()),ARLIM(gpfab.hiVect()),
   divfab.dataPtr(),
   ARLIM(divfab.loVect()),ARLIM(divfab.hiVect()),
   hoopfab.dataPtr(),
   ARLIM(hoopfab.loVect()),ARLIM(hoopfab.hiVect()),
   HOfab.dataPtr(HOcomp),
   ARLIM(HOfab.loVect()),ARLIM(HOfab.hiVect()),
   LOfab.dataPtr(LOcomp),
   ARLIM(LOfab.loVect()),ARLIM(LOfab.hiVect()),
   maskSEMfab.dataPtr(),
   ARLIM(maskSEMfab.loVect()),ARLIM(maskSEMfab.hiVect()),
   tilelo,tilehi,
   fablo,fabhi,&bfact,&level,
   &dt_slab);
 }  // mfi  
} // omp
 ParallelDescriptor::Barrier(); 

 if (project_option==0) {

  for (int dir=0;dir<BL_SPACEDIM;dir++) {

#ifdef _OPENMP
#pragma omp parallel
#endif
{
   for (MFIter mfi(*localMF[delta_MF],use_tiling); mfi.isValid(); ++mfi) {
    BL_ASSERT(grids[mfi.index()] == mfi.validbox());
    const int gridno = mfi.index();
    const Box& tilegrid = mfi.tilebox();
    const Box& fabgrid = grids[gridno];
    const int* tilelo=tilegrid.loVect();
    const int* tilehi=tilegrid.hiVect();
    const int* fablo=fabgrid.loVect();
    const int* fabhi=fabgrid.hiVect();

    const Real* xlo = grid_loc[gridno].lo();

    FArrayBox& gpfab=(*localMF[idx_gpmac+dir])[mfi];
    FArrayBox& HOfab=(*localMF[spectralF_GP_MF+dir])[mfi];
    FArrayBox& LOfab=(*localMF[stableF_GP_MF+dir])[mfi];
    FArrayBox& maskSEMfab=(*localMF[MASKSEM_MF])[mfi];

    int LOcomp=0;
    int HOcomp=0;
    if (update_spectral==1) {
     if (slab_step==-1)
      HOcomp=0;
     else if ((slab_step>=0)&&(slab_step<ns_time_order))
      HOcomp=(slab_step+1);
     else
      BoxLib::Error("slab_step invalid");
    } else if (update_spectral==0) {
     // do nothing
    } else
     BoxLib::Error("update_spectral invalid");

    if (update_stable==1) {
     if ((slab_step>=0)&&(slab_step<ns_time_order))
      LOcomp=slab_step;
     else
      BoxLib::Error("slab_step invalid");
    } else if (update_stable==0) {
     // do nothing
    } else
     BoxLib::Error("update_stable invalid");

    FORT_UPDATESEMFORCE_FACE(
     &project_option,
     &num_materials_face,
     &nsolveMM_FACE,
     &ns_time_order,
     &dir,
     &slab_step,
     &update_spectral,
     &update_stable,
     &nmat,
     xlo,dx,
     gpfab.dataPtr(),
     ARLIM(gpfab.loVect()),ARLIM(gpfab.hiVect()),
     HOfab.dataPtr(HOcomp),
     ARLIM(HOfab.loVect()),ARLIM(HOfab.hiVect()),
     LOfab.dataPtr(LOcomp),
     ARLIM(LOfab.loVect()),ARLIM(LOfab.hiVect()),
     maskSEMfab.dataPtr(),
     ARLIM(maskSEMfab.loVect()),ARLIM(maskSEMfab.hiVect()),
     tilelo,tilehi,
     fablo,fabhi,&bfact,&level,
     &dt_slab);
   }  // mfi  
} // omp
   ParallelDescriptor::Barrier(); 

  } // dir=0..sdim-1

 } else if ((project_option==2)||
            (project_option==3)||
            (project_option==4)) {
  // do nothing
 } else {
  BoxLib::Error("project_option invalid9");
 } 

}   // subroutine update_SEM_delta_force


void NavierStokes::tensor_advection_update() {

 bool use_tiling=ns_tiling;

 int finest_level=parent->finestLevel();

 int ngrow_zero=0;
 int nmat=num_materials;

 if (num_materials_vel!=1)
  BoxLib::Error("num_materials_vel invalid");

 if ((num_materials_viscoelastic>=1)&&(num_materials_viscoelastic<=nmat)) {
  // do nothing
 } else
  BoxLib::Error("num_materials_viscoelastic invalid");

 if (num_state_base!=2)
  BoxLib::Error("num_state_base invalid");

 for (int dir=0;dir<BL_SPACEDIM;dir++)
  debug_ngrow(FACE_VAR_MF+dir,0,2);

 resize_levelsetLO(2,LEVELPC_MF);
 debug_ngrow(LEVELPC_MF,2,8);
 if (localMF[LEVELPC_MF]->nComp()!=nmat*(BL_SPACEDIM+1))
  BoxLib::Error("(localMF[LEVELPC_MF]->nComp()!=nmat*(BL_SPACEDIM+1))");

 VOF_Recon_resize(1,SLOPE_RECON_MF);
 debug_ngrow(SLOPE_RECON_MF,1,9);
 debug_ngrow(CELLTENSOR_MF,1,9);

 MultiFab& Tensor_new=get_new_data(Tensor_Type,slab_step+1);

 int rzflag=0;
 if (CoordSys::IsRZ())
  rzflag=1;
 else if (CoordSys::IsCartesian())
  rzflag=0;
 else if (CoordSys::IsCYLINDRICAL())
  rzflag=3;
 else
  BoxLib::Error("CoordSys bust 3");

 const Real* dx = geom.CellSize();

 for (int im=0;im<nmat;im++) {

  if (ns_is_rigid(im)==0) {

   if ((elastic_time[im]>0.0)&&(elastic_viscosity[im]>0.0)) {

    int partid=0;
    while ((im_elastic_map[partid]!=im)&&(partid<im_elastic_map.size())) {
     partid++;
    }

    if (partid<im_elastic_map.size()) {

     int scomp_tensor=partid*NUM_TENSOR_TYPE;

     int ncomp_visc=localMF[CELL_VISC_MATERIAL_MF]->nComp();
     if (ncomp_visc!=3*nmat)
      BoxLib::Error("cell_visc_material ncomp invalid");

     MultiFab* tensor_source_mf=
      getStateTensor(0,scomp_tensor,NUM_TENSOR_TYPE,cur_time_slab);

     MultiFab* velmf=getState(1,0,BL_SPACEDIM,cur_time_slab);
   
     MultiFab* tendata_mf=new MultiFab(grids,20,ngrow_zero,dmap,Fab_allocate);
 
#ifdef _OPENMP
#pragma omp parallel
#endif
{
     for (MFIter mfi(*tensor_source_mf,use_tiling); mfi.isValid(); ++mfi) {

      BL_ASSERT(grids[mfi.index()] == mfi.validbox());
      const int gridno = mfi.index();
      const Box& tilegrid = mfi.tilebox();
      const Box& fabgrid = grids[gridno];
      const int* tilelo=tilegrid.loVect();
      const int* tilehi=tilegrid.hiVect();
      const int* fablo=fabgrid.loVect();
      const int* fabhi=fabgrid.hiVect();
      int bfact=parent->Space_blockingFactor(level);

      const Real* xlo = grid_loc[gridno].lo();

      FArrayBox& cellten=(*localMF[CELLTENSOR_MF])[mfi];
      int ntensor=BL_SPACEDIM*BL_SPACEDIM;
      int ntensorMM=ntensor*num_materials_vel;

      if (cellten.nComp()!=ntensorMM)
       BoxLib::Error("cellten invalid ncomp");

      FArrayBox& voffab=(*localMF[SLOPE_RECON_MF])[mfi];
      FArrayBox& viscfab=(*localMF[CELL_VISC_MATERIAL_MF])[mfi];
      if (viscfab.nComp()<nmat)
       BoxLib::Error("viscfab.nComp() invalid");

      FArrayBox& velfab=(*velmf)[mfi];
      FArrayBox& tendata=(*tendata_mf)[mfi];

      Array<int> velbc=getBCArray(State_Type,gridno,0,BL_SPACEDIM);

      // get |grad U|,D,grad U 
      int iproject=0;
      int onlyscalar=0; 

      // 0<=im<=nmat-1
      FORT_GETSHEAR(
       &im,
       &ntensor,
       cellten.dataPtr(),
       ARLIM(cellten.loVect()),ARLIM(cellten.hiVect()),
       voffab.dataPtr(),ARLIM(voffab.loVect()),ARLIM(voffab.hiVect()),
       velfab.dataPtr(),
       ARLIM(velfab.loVect()),ARLIM(velfab.hiVect()),
       dx,xlo,
       tendata.dataPtr(),
       ARLIM(tendata.loVect()),ARLIM(tendata.hiVect()),
       &iproject,&onlyscalar,
       &cur_time_slab,
       tilelo,tilehi,
       fablo,fabhi,
       &bfact, 
       &level, 
       velbc.dataPtr(),
       &ngrow_zero, // 0
       &nmat);
     } // mfi  
} // omp
     ParallelDescriptor::Barrier(); 

#ifdef _OPENMP
#pragma omp parallel
#endif
{
     for (MFIter mfi(*tensor_source_mf,use_tiling); mfi.isValid(); ++mfi) {

      BL_ASSERT(grids[mfi.index()] == mfi.validbox());
      const int gridno = mfi.index();
      const Box& tilegrid = mfi.tilebox();
      const Box& fabgrid = grids[gridno];
      const int* tilelo=tilegrid.loVect();
      const int* tilehi=tilegrid.hiVect();
      const int* fablo=fabgrid.loVect();
      const int* fabhi=fabgrid.hiVect();
      int bfact=parent->Space_blockingFactor(level);

      const Real* xlo = grid_loc[gridno].lo();

      FArrayBox& voffab=(*localMF[SLOPE_RECON_MF])[mfi];
      FArrayBox& viscfab=(*localMF[CELL_VISC_MATERIAL_MF])[mfi];
      FArrayBox& velfab=(*velmf)[mfi];
      FArrayBox& tensor_new_fab=Tensor_new[mfi];
      FArrayBox& tensor_source_mf_fab=(*tensor_source_mf)[mfi];
      FArrayBox& tendata=(*tendata_mf)[mfi];

      Array<int> velbc=getBCArray(State_Type,gridno,0,BL_SPACEDIM);

      int transposegradu=0;

      FORT_UPDATETENSOR(
       &level,
       &finest_level,
       &nmat,&im,&ncomp_visc,
       voffab.dataPtr(),ARLIM(voffab.loVect()),ARLIM(voffab.hiVect()),
       viscfab.dataPtr(),ARLIM(viscfab.loVect()),ARLIM(viscfab.hiVect()),
       tendata.dataPtr(),ARLIM(tendata.loVect()),ARLIM(tendata.hiVect()),
       dx,xlo,
       velfab.dataPtr(),
       ARLIM(velfab.loVect()),ARLIM(velfab.hiVect()),
       tensor_new_fab.dataPtr(scomp_tensor),
       ARLIM(tensor_new_fab.loVect()),ARLIM(tensor_new_fab.hiVect()),
       tensor_source_mf_fab.dataPtr(),
       ARLIM(tensor_source_mf_fab.loVect()),
       ARLIM(tensor_source_mf_fab.hiVect()),
       tilelo,tilehi,
       fablo,fabhi,
       &bfact, 
       &dt_slab,
       &elastic_time[im],
       &viscoelastic_model[im],
       &polymer_factor[im],
       &rzflag,velbc.dataPtr(),&transposegradu);
     }  // mfi
} // omp
     ParallelDescriptor::Barrier(); 

     if ((BL_SPACEDIM==2)&&(rzflag==1)) {

#ifdef _OPENMP
#pragma omp parallel
#endif
{
      for (MFIter mfi(*tensor_source_mf,use_tiling); mfi.isValid(); ++mfi) {

       BL_ASSERT(grids[mfi.index()] == mfi.validbox());
       const int gridno = mfi.index();
       const Box& tilegrid = mfi.tilebox();
       const Box& fabgrid = grids[gridno];
       const int* tilelo=tilegrid.loVect();
       const int* tilehi=tilegrid.hiVect();
       const int* fablo=fabgrid.loVect();
       const int* fabhi=fabgrid.hiVect();
       int bfact=parent->Space_blockingFactor(level);

       const Real* xlo = grid_loc[gridno].lo();

       FArrayBox& voffab=(*localMF[SLOPE_RECON_MF])[mfi];
       FArrayBox& tensor_new_fab=Tensor_new[mfi];

       FORT_FIX_HOOP_TENSOR(
        &level,
        &finest_level,
        &nmat,&im,
        voffab.dataPtr(),ARLIM(voffab.loVect()),ARLIM(voffab.hiVect()),
        dx,xlo,
        tensor_new_fab.dataPtr(scomp_tensor),
        ARLIM(tensor_new_fab.loVect()),ARLIM(tensor_new_fab.hiVect()),
        tilelo,tilehi,
        fablo,fabhi,
        &bfact, 
        &rzflag);
      }  // mfi
} // omp
      ParallelDescriptor::Barrier(); 

     } else if ((BL_SPACEDIM==3)||
                (rzflag==0)||
                (rzflag==3)) {
      // do nothing
     } else
      BoxLib::Error("bl_spacedim or rzflag invalid");
      
     delete tendata_mf;
     delete tensor_source_mf;
     delete velmf;
    } else
     BoxLib::Error("partid could not be found: tensor_advection_update");

   } else if ((elastic_time[im]==0.0)||(elastic_viscosity[im]==0.0)) {
    if (viscoelastic_model[im]!=0)
     BoxLib::Error("viscoelastic_model[im]!=0");
   } else
    BoxLib::Error("viscoelastic parameter bust");

  } else if (ns_is_rigid(im)==1) {

   // do nothing

  } else
   BoxLib::Error("ns_is_rigid invalid");

 } // im


}   // subroutine tensor_advection_update

// extrapolate where the volume fraction is less than 1/2.  
// note: the gradients used to update the tensor are only valid in cells
// where LS(im_viscoelastic)>=0.
void NavierStokes::tensor_extrapolate() {

 int finest_level=parent->finestLevel();

 int nmat=num_materials;

 if ((num_materials_viscoelastic>=1)&&(num_materials_viscoelastic<=nmat)) {
  // do nothing
 } else
  BoxLib::Error("num_materials_viscoelastic invalid");

 if (num_state_base!=2)
  BoxLib::Error("num_state_base invalid");

 int ngrow_extrap=2;

 VOF_Recon_resize(ngrow_extrap,SLOPE_RECON_MF);
 debug_ngrow(SLOPE_RECON_MF,ngrow_extrap,13);

 MultiFab& Tensor_new=get_new_data(Tensor_Type,slab_step+1);

 const Real* dx = geom.CellSize();

 for (int im=0;im<nmat;im++) {

  if (ns_is_rigid(im)==0) {

   if ((elastic_time[im]>0.0)&&(elastic_viscosity[im]>0.0)) {

    int partid=0;
    while ((im_elastic_map[partid]!=im)&&(partid<im_elastic_map.size())) {
     partid++;
    }

    if (partid<im_elastic_map.size()) {

     int scomp_tensor=partid*NUM_TENSOR_TYPE;

     MultiFab* tensor_source_mf= getStateTensor(ngrow_extrap,scomp_tensor,
      NUM_TENSOR_TYPE,cur_time_slab);
   
     //use_tiling==false 
     //for future: have a separate FAB for each thread?
 
#ifdef _OPENMP
#pragma omp parallel
#endif
{
     for (MFIter mfi(*tensor_source_mf); mfi.isValid(); ++mfi) {

      BL_ASSERT(grids[mfi.index()] == mfi.validbox());
      const int gridno = mfi.index();
      const Box& tilegrid = mfi.tilebox();
      const Box& fabgrid = grids[gridno];
      const int* tilelo=tilegrid.loVect();
      const int* tilehi=tilegrid.hiVect();
      const int* fablo=fabgrid.loVect();
      const int* fabhi=fabgrid.hiVect();
      int bfact=parent->Space_blockingFactor(level);

      const Real* xlo = grid_loc[gridno].lo();
 
      FArrayBox& voffab=(*localMF[SLOPE_RECON_MF])[mfi];
      FArrayBox& tensor_new_fab=Tensor_new[mfi];
      FArrayBox& tensor_source_mf_fab=(*tensor_source_mf)[mfi];

      FORT_EXTRAPTENSOR(
       &level,
       &finest_level,
       &nmat,&im,
       &ngrow_extrap,
       voffab.dataPtr(),ARLIM(voffab.loVect()),ARLIM(voffab.hiVect()),
       dx,xlo,
       tensor_new_fab.dataPtr(scomp_tensor),
       ARLIM(tensor_new_fab.loVect()),ARLIM(tensor_new_fab.hiVect()),
       tensor_source_mf_fab.dataPtr(),
       ARLIM(tensor_source_mf_fab.loVect()),
       ARLIM(tensor_source_mf_fab.hiVect()),
       tilelo,tilehi,
       fablo,fabhi,&bfact);
     }  // mfi
} //omp
     ParallelDescriptor::Barrier(); 

     delete tensor_source_mf;

    } else
     BoxLib::Error("partid could not be found: tensor_extrapolate");

   } else if ((elastic_time[im]==0.0)||(elastic_viscosity[im]==0.0)) {

    if (viscoelastic_model[im]!=0)
     BoxLib::Error("viscoelastic_model[im]!=0");

   } else
    BoxLib::Error("viscoelastic parameter bust");
  } else if (ns_is_rigid(im)==1) {
   // do nothing
  } else
   BoxLib::Error("ns_is_rigid bust");
 } // im

}   // subroutine tensor_extrapolate


void 
NavierStokes::correct_density() {

 bool use_tiling=ns_tiling;

 if (num_state_base!=2)
  BoxLib::Error("num_state_base invalid");
 if (dt_slab<=0.0)
  BoxLib::Error("dt_slab invalid3");

 int nmat=num_materials;

 int non_conservative_density=0;

 for (int im=0;im<nmat;im++) {
  if ((DrhoDT[im]!=0.0)&&(override_density[im]==0))
   BoxLib::Error("DrhoDT mismatch"); 
  if ((DrhoDz[im]!=0.0)&&(override_density[im]==0))
   BoxLib::Error("DrhoDz mismatch"); 
  if (override_density[im]==1) {
   non_conservative_density=1;
  } else if (override_density[im]==0) {
   // do nothing
  } else if (override_density[im]==2) {
   // do nothing
  } else
   BoxLib::Error("override density invalid");
 } // im

 if (non_conservative_density==0) {
  // do nothing
 } else if (non_conservative_density==1) {

   int finest_level=parent->finestLevel();

   resize_metrics(1);
   resize_maskfiner(1,MASKCOEF_MF);
   resize_mask_nbr(1);

   debug_ngrow(VOLUME_MF,1,28); 
   debug_ngrow(MASKCOEF_MF,1,28); 
   debug_ngrow(MASK_NBR_MF,1,28); 
   debug_ngrow(MASKSEM_MF,1,28); 
   MultiFab& S_new=get_new_data(State_Type,slab_step+1);
   const Real* dx = geom.CellSize();
 
   int scomp_pres=num_materials_vel*BL_SPACEDIM;

   Real gravity_normalized=fabs(gravity);
   if (invert_gravity==1)
    gravity_normalized=-gravity_normalized;
   else if (invert_gravity==0) {
    // do nothing
   } else
    BoxLib::Error("invert_gravity invalid");
 
#ifdef _OPENMP
#pragma omp parallel
#endif
{
   for (MFIter mfi(S_new,use_tiling); mfi.isValid(); ++mfi) {
    BL_ASSERT(grids[mfi.index()] == mfi.validbox());
    const int gridno = mfi.index();
    const Box& tilegrid = mfi.tilebox();
    const Box& fabgrid = grids[gridno];
    const int* tilelo=tilegrid.loVect();
    const int* tilehi=tilegrid.hiVect();
    const int* fablo=fabgrid.loVect();
    const int* fabhi=fabgrid.hiVect();
    int bfact=parent->Space_blockingFactor(level);

    const Real* xlo = grid_loc[gridno].lo();
    Array<int> presbc=getBCArray(State_Type,gridno,scomp_pres,1);
    FArrayBox& maskfab=(*localMF[MASKCOEF_MF])[mfi];
    FArrayBox& masknbrfab=(*localMF[MASK_NBR_MF])[mfi];
    FArrayBox& volfab=(*localMF[VOLUME_MF])[mfi];
    int dencomp=num_materials_vel*(BL_SPACEDIM+1);

     // if override_density[im]==1, then rho_im=rho(T,z) 
    FORT_DENCOR(
      presbc.dataPtr(),
      tilelo,tilehi,
      fablo,fabhi,&bfact,
      &dt_slab,
      maskfab.dataPtr(),
      ARLIM(maskfab.loVect()),ARLIM(maskfab.hiVect()),
      masknbrfab.dataPtr(),
      ARLIM(masknbrfab.loVect()),ARLIM(masknbrfab.hiVect()),
      volfab.dataPtr(),ARLIM(volfab.loVect()),ARLIM(volfab.hiVect()),
      S_new[mfi].dataPtr(dencomp),
      ARLIM(S_new[mfi].loVect()),ARLIM(S_new[mfi].hiVect()),
      xlo,dx,
      &gravity_normalized,
      DrhoDT.dataPtr(),
      override_density.dataPtr(),
      &nmat,&level,&finest_level);
   }  // mfi
}  // omp
   ParallelDescriptor::Barrier();
  
 } else
   BoxLib::Error("non_conservative_density invalid");

} // subroutine correct_density

// check that fine grids align with coarse elements and that
// fine grid dimensions are perfectly divisible by the fine order.
// fine grid dimensions are perfectly divisible by the blocking factor.
void NavierStokes::check_grid_places() {

 int bfact_SEM_coarse=0;
 int bfact_SEM=parent->Space_blockingFactor(level);
 int bfact_grid=parent->blockingFactor(level);
 if (bfact_grid<4)
  BoxLib::Error("we must have blocking factor at least 4");

 int bfact_fine_min=((bfact_SEM<4) ? 4 : bfact_SEM);
 if (bfact_fine_min<bfact_grid)
  bfact_fine_min=bfact_grid;

  // coarse elements must align with fine grid patches.
 if (level>0) {
  bfact_SEM_coarse=parent->Space_blockingFactor(level-1);
  if (2*bfact_SEM_coarse>bfact_fine_min)
   bfact_fine_min=2*bfact_SEM_coarse;
 }

 if (bfact_fine_min<4)
  BoxLib::Error("bfact_fine_min<4");

 for (int gridno=0;gridno<grids.size();gridno++) {
  const Box& fabgrid = grids[gridno];
  const int* fablo=fabgrid.loVect();
  const int* fabhi=fabgrid.hiVect();
  for (int dir=0;dir<BL_SPACEDIM;dir++) {
   if ((fablo[dir]/bfact_fine_min)*bfact_fine_min!=fablo[dir])
    BoxLib::Error("fablo index failed bfact_fine_min test");
   if ((fablo[dir]/bfact_grid)*bfact_grid!=fablo[dir])
    BoxLib::Error("fablo index failed bfact_grid test");
   int testhi=fabhi[dir]+1;
   if ((testhi/bfact_fine_min)*bfact_fine_min!=testhi)
    BoxLib::Error("fabhi index failed bfact_fine_min test");
   if ((testhi/bfact_grid)*bfact_grid!=testhi)
    BoxLib::Error("fabhi index failed bfact_grid test");
  } // dir=0..sdim-1

 } // gridno

} // subroutine check_grid_places


void 
NavierStokes::prepare_mask_nbr(int ngrow) {

 if (ngrow_distance!=4)
  BoxLib::Error("ngrow_distance invalid");
 if (curv_stencil_height!=ngrow_distance)
  BoxLib::Error("curv_stencil_height invalid");

 if ((ngrow<1)||(ngrow>ngrow_distance))
  BoxLib::Error("ngrow invalid");

 if (localMF_grow[MASK_NBR_MF]>=0) {
  delete_localMF(MASK_NBR_MF,1);
 }

   // mask_nbr:
   // (1) =1 interior  =1 fine-fine ghost in domain  =0 otherwise
   // (2) =1 interior  =0 otherwise
   // (3) =1 interior+ngrow-1  =0 otherwise
   // (4) =1 interior+ngrow    =0 otherwise
 new_localMF(MASK_NBR_MF,4,ngrow,-1);
  // value,scomp,ncomp,ngrow
 localMF[MASK_NBR_MF]->setVal(0.0,0,4,ngrow);
 localMF[MASK_NBR_MF]->setVal(1.0,0,2,0);
 localMF[MASK_NBR_MF]->FillBoundary(0,1,geom.periodicity());  
 localMF[MASK_NBR_MF]->setVal(1.0,2,1,ngrow-1);
 localMF[MASK_NBR_MF]->setVal(1.0,3,1,ngrow);

} // subroutine prepare_mask_nbr()

void 
NavierStokes::prepare_displacement(int mac_grow,int unsplit_displacement) {
 
 bool use_tiling=ns_tiling;

 if (divu_outer_sweeps==0)
  vel_time_slab=prev_time_slab;
 else if (divu_outer_sweeps>0)
  vel_time_slab=cur_time_slab;
 else
  BoxLib::Error("divu_outer_sweeps invalid");

 int mac_grow_expect=1;
 if (face_flag==0) {
  // do nothing
 } else if (face_flag==1) {
  mac_grow_expect++;
 } else
  BoxLib::Error("face_flag invalid 3");

 if (mac_grow!=mac_grow_expect)
  BoxLib::Error("mac_grow invalid in prepare_displacement");

 int finest_level=parent->finestLevel();
 int nmat=num_materials;

 if (num_materials_vel!=1)
  BoxLib::Error("num_materials_vel invalid");

 int nsolveMM_FACE=num_materials_vel;

 getState_localMF(CELL_VELOCITY_MF,mac_grow,0,
  num_materials_vel*BL_SPACEDIM,vel_time_slab);

 for (int normdir=0;normdir<BL_SPACEDIM;normdir++) {

   // mac_grow+1 for finding slopes
  MultiFab* temp_mac_velocity=getStateMAC(mac_grow+1,normdir,
   0,nsolveMM_FACE,vel_time_slab); 

   // mac_grow+1 for finding slopes
   // MAC_VELOCITY_MF deleted towards the end of 
   //   NavierStokes::nonlinear_advection
   // component 1: the velocity
   // component(s) 2..sdim: tangential derivatives
  new_localMF(MAC_VELOCITY_MF+normdir,nsolveMM_FACE*BL_SPACEDIM,
		  mac_grow+1,normdir);

  const Real* dx = geom.CellSize();
  MultiFab& S_new=get_new_data(State_Type,slab_step+1);

  // first sweep: 
  // 1. multiply velocity by dt.  
  // 2. adjust velocity if RZ.  
  // 3. override velocity if it is a passive advection problem.
  // 4. copy into mac_velocity
  // 5. repeat for cell_velocity
  // second sweep:
  // 1. calculate mac velocity slopes tangent to given face.

  for (int isweep=0;isweep<=1;isweep++) {
#ifdef _OPENMP
#pragma omp parallel
#endif
{
   for (MFIter mfi(S_new,use_tiling); mfi.isValid(); ++mfi) {
    BL_ASSERT(grids[mfi.index()] == mfi.validbox());
    const int gridno = mfi.index();
    const Box& tilegrid = mfi.tilebox();
    const Box& fabgrid = grids[gridno];
    const int* tilelo=tilegrid.loVect();
    const int* tilehi=tilegrid.hiVect();
    const int* fablo=fabgrid.loVect();
    const int* fabhi=fabgrid.hiVect();
    int bfact=parent->Space_blockingFactor(level);

    const Real* xlo = grid_loc[gridno].lo();

    Array<int> velbc=getBCArray(State_Type,gridno,normdir,1);

    FArrayBox& unodetemp=(*temp_mac_velocity)[mfi]; // macgrow+1
    FArrayBox& unode=(*localMF[MAC_VELOCITY_MF+normdir])[mfi]; // macgrow+1
    FArrayBox& ucell=(*localMF[CELL_VELOCITY_MF])[mfi];

    prescribed_vel_time_slab=0.5*(prev_time_slab+cur_time_slab);

    FORT_VELMAC_OVERRIDE(
     &isweep,
     &unsplit_displacement,
     &nsolveMM_FACE,
     &nmat,
     tilelo,tilehi,
     fablo,fabhi,&bfact,
     velbc.dataPtr(),
     &dt_slab,
     &prev_time_slab,
     &prescribed_vel_time_slab,
     &vel_time_slab,
     &normdir,
     unodetemp.dataPtr(),ARLIM(unodetemp.loVect()),ARLIM(unodetemp.hiVect()),
     unode.dataPtr(),ARLIM(unode.loVect()),ARLIM(unode.hiVect()),
     ucell.dataPtr(),ARLIM(ucell.loVect()),ARLIM(ucell.hiVect()),
     xlo,dx,
     &mac_grow,
     &map_forward_direct_split[normdir],
     &level,
     &finest_level, 
     &SDC_outer_sweeps,
     &ns_time_order,
     &divu_outer_sweeps,
     &num_divu_outer_sweeps);

   }  // mfi
} // omp
   ParallelDescriptor::Barrier();
  } // isweep=0..1

  delete temp_mac_velocity;

  localMF[MAC_VELOCITY_MF+normdir]->FillBoundary(geom.periodicity());
 } // normdir=0..sdim-1
 localMF[CELL_VELOCITY_MF]->FillBoundary(geom.periodicity());

}  // prepare_displacement

void
NavierStokes::level_phase_change_rate(int get_statistics,
     Array<blobclass> blobdata,int color_count) {

 bool use_tiling=ns_tiling;
 int finest_level=parent->finestLevel();
 if ((level<0)||(level>finest_level))
  BoxLib::Error("level invalid level_phase_change_rate");

 int nmat=num_materials;
 int nten=( (nmat-1)*(nmat-1)+nmat-1 )/2;
 int nden=nmat*num_state_material;
 int nburning=nten*(BL_SPACEDIM+1);
 const Real* dx = geom.CellSize();

  // in: level_phase_change_rate()
 getStateDen_localMF(DEN_RECON_MF,normal_probe_size+3,cur_time_slab);

 if (localMF[DEN_RECON_MF]->nComp()!=nden)
  BoxLib::Error("DEN_RECON_MF invalid ncomp");

 int blob_arraysize=num_elements_blobclass;

 if (get_statistics==1) {
  if (color_count!=blobdata.size())
   BoxLib::Error("color_count!=blobdata.size()");
  blob_arraysize=color_count*num_elements_blobclass;
 } else if (get_statistics==0) {
  // do nothing
 } else
  BoxLib::Error("get_statistics invalid");

 Array<Real> blob_array;
 blob_array.resize(blob_arraysize);

 MultiFab* levelcolor;
 MultiFab* leveltype;

 if (get_statistics==1) {

  int counter=0;
  for (int i=0;i<color_count;i++) {
   copy_from_blobdata(i,counter,blob_array,blobdata);
  } // i=0..color_count-1

  if (counter!=blob_arraysize)
   BoxLib::Error("counter invalid");

  levelcolor=localMF[COLOR_MF];
  leveltype=localMF[TYPE_MF];

  if (levelcolor->nGrow()!=1)
   BoxLib::Error("levelcolor->nGrow()!=1");
  if (leveltype->nGrow()!=1)
   BoxLib::Error("leveltype->nGrow()!=1");

 } else if (get_statistics==0) {
  levelcolor=localMF[DEN_RECON_MF];
  leveltype=localMF[DEN_RECON_MF];
 } else
  BoxLib::Error("get_statistics invalid");

 MultiFab* presmf;
 if (hydrate_flag==1) {
  presmf=getState(normal_probe_size+3,num_materials_vel*BL_SPACEDIM,1,
   cur_time_slab);
 } else if (hydrate_flag==0) {
  presmf=localMF[DEN_RECON_MF];
 } else {
  BoxLib::Error("hydrate_flag invalid");
 }

 resize_maskfiner(1,MASKCOEF_MF);
 debug_ngrow(MASKCOEF_MF,1,28); 

 MultiFab& LS_new = get_new_data(LS_Type,slab_step+1);
 if (LS_new.nComp()!=nmat*(1+BL_SPACEDIM)) 
  BoxLib::Error("LS_new invalid ncomp");

 if (localMF[BURNING_VELOCITY_MF]->nComp()!=nburning)
  BoxLib::Error("localMF[BURNING_VELOCITY_MF] incorrect ncomp");
 if (localMF[BURNING_VELOCITY_MF]->nGrow()!=ngrow_make_distance)
  BoxLib::Error("localMF[BURNING_VELOCITY_MF] incorrect ngrow");

 debug_ngrow(HOLD_LS_DATA_MF,normal_probe_size+3,30);
 if (localMF[HOLD_LS_DATA_MF]->nComp()!=nmat*(1+BL_SPACEDIM)) 
  BoxLib::Error("localMF[HOLD_LS_DATA_MF]->nComp() invalid");

 debug_ngrow(LS_NRM_FD_MF,1,30);
 if (localMF[LS_NRM_FD_MF]->nComp()!=nmat*BL_SPACEDIM) 
  BoxLib::Error("localMF[LS_NRM_FD_MF]->nComp() invalid");

 VOF_Recon_resize(normal_probe_size+3,SLOPE_RECON_MF);

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(LS_new,use_tiling); mfi.isValid(); ++mfi) {
   BL_ASSERT(grids[mfi.index()] == mfi.validbox());
   const int gridno = mfi.index();
   const Box& tilegrid = mfi.tilebox();
   const Box& fabgrid = grids[gridno];
   const int* tilelo=tilegrid.loVect();
   const int* tilehi=tilegrid.hiVect();
   const int* fablo=fabgrid.loVect();
   const int* fabhi=fabgrid.hiVect();
   int bfact=parent->Space_blockingFactor(level);

   const Real* xlo = grid_loc[gridno].lo();

    // mask=tag if not covered by level+1 or outside the domain.
   FArrayBox& maskcov=(*localMF[MASKCOEF_MF])[mfi];

   FArrayBox& lsfab=(*localMF[HOLD_LS_DATA_MF])[mfi];
   FArrayBox& nrmFDfab=(*localMF[LS_NRM_FD_MF])[mfi];

   FArrayBox& burnvelfab=(*localMF[BURNING_VELOCITY_MF])[mfi];
   if (burnvelfab.nComp()!=nburning)
    BoxLib::Error("burnvelfab.nComp() incorrect");

   FArrayBox& lsnewfab=LS_new[mfi];
   FArrayBox& colorfab=(*levelcolor)[mfi];
   FArrayBox& typefab=(*leveltype)[mfi];
   FArrayBox& eosfab=(*localMF[DEN_RECON_MF])[mfi];
   FArrayBox& reconfab=(*localMF[SLOPE_RECON_MF])[mfi]; 
   FArrayBox& presfab=(*presmf)[mfi]; 

     // lsnewfab and burnvelfab are updated.
     // lsfab is not updated.
     // burnvelfab=BURNING_VELOCITY_MF is cell centered velocity.
   int stefan_flag=1;
   Array<int> use_exact_temperature(2*nten);
   for (int im=0;im<2*nten;im++)
    use_exact_temperature[im]=0;

   FORT_RATEMASSCHANGE( 
    &stefan_flag,
    &level,
    &finest_level,
    &normal_probe_size,
    &ngrow_distance,
    &nmat,
    &nten,
    &nburning,
    &nden,
    density_floor_expansion.dataPtr(),
    density_ceiling_expansion.dataPtr(),
    microlayer_substrate.dataPtr(),
    microlayer_angle.dataPtr(),
    microlayer_size.dataPtr(),
    macrolayer_size.dataPtr(),
    max_contact_line_size.dataPtr(),
    latent_heat.dataPtr(),
    use_exact_temperature.dataPtr(),
    reaction_rate.dataPtr(),
    saturation_temp.dataPtr(),
    freezing_model.dataPtr(),
    distribute_from_target.dataPtr(),
    mass_fraction_id.dataPtr(),
    species_evaporation_density.dataPtr(),
    tilelo,tilehi,
    fablo,fabhi,&bfact,
    xlo,dx,
    &prev_time_slab,
    &dt_slab,
    &blob_arraysize,
    blob_array.dataPtr(),
    &num_elements_blobclass,
    &color_count,
    colorfab.dataPtr(),
    ARLIM(colorfab.loVect()),ARLIM(colorfab.hiVect()),
    typefab.dataPtr(),
    ARLIM(typefab.loVect()),ARLIM(typefab.hiVect()),
    maskcov.dataPtr(),
    ARLIM(maskcov.loVect()),ARLIM(maskcov.hiVect()),
    burnvelfab.dataPtr(),
    ARLIM(burnvelfab.loVect()),ARLIM(burnvelfab.hiVect()),
    lsfab.dataPtr(),ARLIM(lsfab.loVect()),ARLIM(lsfab.hiVect()),
    lsnewfab.dataPtr(),ARLIM(lsnewfab.loVect()),ARLIM(lsnewfab.hiVect()),
    nrmFDfab.dataPtr(),ARLIM(nrmFDfab.loVect()),ARLIM(nrmFDfab.hiVect()),
    eosfab.dataPtr(),ARLIM(eosfab.loVect()),ARLIM(eosfab.hiVect()),
    reconfab.dataPtr(),ARLIM(reconfab.loVect()),ARLIM(reconfab.hiVect()),
    presfab.dataPtr(),ARLIM(presfab.loVect()),ARLIM(presfab.hiVect()));
 } // mfi
} // omp
 ParallelDescriptor::Barrier();

 delete_localMF(DEN_RECON_MF,1);

 if (hydrate_flag==1) {
  delete presmf;
 }

} // subroutine level_phase_change_rate



void
NavierStokes::level_phase_change_rate_extend() {

 bool use_tiling=ns_tiling;
 int finest_level=parent->finestLevel();
 if ((level<0)||(level>finest_level))
  BoxLib::Error("level invalid level_phase_change_rate_extend");

 int nmat=num_materials;
 int nten=( (nmat-1)*(nmat-1)+nmat-1 )/2;
 int nburning=nten*(BL_SPACEDIM+1);

 const Real* dx = geom.CellSize();

 MultiFab& LS_new = get_new_data(LS_Type,slab_step+1);
 if (LS_new.nComp()!=nmat*(BL_SPACEDIM+1)) 
  BoxLib::Error("LS_new invalid ncomp");

 if (localMF[BURNING_VELOCITY_MF]->nComp()!=nburning)
  BoxLib::Error("localMF[BURNING_VELOCITY_MF] incorrect ncomp");

 if (ngrow_make_distance!=3)
  BoxLib::Error("expecting ngrow_make_distance==3");
 if (localMF[BURNING_VELOCITY_MF]->nGrow()!=ngrow_make_distance)
  BoxLib::Error("localMF[BURNING_VELOCITY_MF] incorrect ngrow");

 debug_ngrow(HOLD_LS_DATA_MF,normal_probe_size+3,30);
 if (localMF[HOLD_LS_DATA_MF]->nComp()!=nmat*(1+BL_SPACEDIM)) 
  BoxLib::Error("localMF[HOLD_LS_DATA_MF]->nComp() invalid");

 Array<int> scompBC_map;
 scompBC_map.resize(nburning);
  // extrap, u_extrap, v_extrap, w_extrap
  // mof recon extrap
  // maskSEMextrap
 int burnvel_start_pos=1+BL_SPACEDIM+nmat*ngeom_recon+1;

 for (int imdest=0;imdest<nburning;imdest++)
  scompBC_map[imdest]=burnvel_start_pos+imdest;

 PCINTERP_fill_borders(BURNING_VELOCITY_MF,ngrow_make_distance,
  0,nburning,State_Type,scompBC_map);

 if (1==0) {
  int gridno=0;
  const Box& fabgrid = grids[gridno];
  const int* fablo=fabgrid.loVect();
  const int* fabhi=fabgrid.hiVect();
  const Real* xlo = grid_loc[gridno].lo();
  int interior_only=0;
  FArrayBox& burnvelfab=(*localMF[BURNING_VELOCITY_MF])[0];
  const Real* dxplot = geom.CellSize();
  int scomp=0;
  int ncomp=nburning;
  int dirplot=-1;
  int id=0;
  tecplot_debug(burnvelfab,xlo,fablo,fabhi,dxplot,dirplot,id,
     scomp,ncomp,interior_only);
 }

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(LS_new,use_tiling); mfi.isValid(); ++mfi) {
   BL_ASSERT(grids[mfi.index()] == mfi.validbox());
   const int gridno = mfi.index();
   const Box& tilegrid = mfi.tilebox();
   const Box& fabgrid = grids[gridno];
   const int* tilelo=tilegrid.loVect();
   const int* tilehi=tilegrid.hiVect();
   const int* fablo=fabgrid.loVect();
   const int* fabhi=fabgrid.hiVect();
   int bfact=parent->Space_blockingFactor(level);

   const Real* xlo = grid_loc[gridno].lo();
   FArrayBox& lsfab=(*localMF[HOLD_LS_DATA_MF])[mfi];
   FArrayBox& burnvelfab=(*localMF[BURNING_VELOCITY_MF])[mfi];
   if (burnvelfab.nComp()==nburning) {
    // do nothing
   } else {
    BoxLib::Error("burnvelfab.nComp() invalid");
   }

   int ngrow=normal_probe_size+3;
   if (ngrow!=4)
    BoxLib::Error("expecting ngrow==4");

    // burnvelfab=BURNING_VELOCITY_MF is cell centered.
    // sets the burning velocity flag from 0 to 2 if
    // foot of characteristic within range.
   FORT_EXTEND_BURNING_VEL( 
    &level,
    &finest_level,
    xlo,dx,
    &nmat,
    &nten,
    &nburning,
    &ngrow,
    latent_heat.dataPtr(),
    tilelo,tilehi,
    fablo,fabhi,&bfact,
    burnvelfab.dataPtr(),
    ARLIM(burnvelfab.loVect()),ARLIM(burnvelfab.hiVect()),
    lsfab.dataPtr(),ARLIM(lsfab.loVect()),ARLIM(lsfab.hiVect()));
 } // mfi
} // omp
 ParallelDescriptor::Barrier();

 scompBC_map.resize(nburning);

  // state extrap, vel extrap, MOF extrap, mask SEM extrap
 burnvel_start_pos=1+BL_SPACEDIM+nmat*ngeom_recon+1;

 for (int imdest=0;imdest<nburning;imdest++)
  scompBC_map[imdest]=burnvel_start_pos+imdest;

  // first nten components are the status.
 PCINTERP_fill_borders(BURNING_VELOCITY_MF,ngrow_make_distance,
  0,nburning,State_Type,scompBC_map);

 if (1==0) {
  int gridno=0;
  const Box& fabgrid = grids[gridno];
  const int* fablo=fabgrid.loVect();
  const int* fabhi=fabgrid.hiVect();
  const Real* xlo = grid_loc[gridno].lo();
  int interior_only=0;
  FArrayBox& burnvelfab=(*localMF[BURNING_VELOCITY_MF])[0];
  const Real* dxplot = geom.CellSize();
  int scomp=0;
  int ncomp=nburning;
  int dirplot=-1;
  int id=0;
  tecplot_debug(burnvelfab,xlo,fablo,fabhi,dxplot,dirplot,id,
     scomp,ncomp,interior_only);
 }

} // subroutine level_phase_change_rate_extend

// isweep==0:
// 1. initialize node velocity from BURNING_VELOCITY_MF
// 2. unsplit advection of materials changing phase
// 3. determine overall change in volume
// isweep==1:
// 1. scale overall change in volume so that amount evaporated equals the
//    amount condensed if heat pipe problem.
// 2. update volume fractions, jump strength, temperature
void
NavierStokes::level_phase_change_convert(int isweep) {

 bool use_tiling=ns_tiling;
 int finest_level=parent->finestLevel();
 if ((level<0)||(level>finest_level))
  BoxLib::Error("level invalid level_phase_change_convert");

 int nmat=num_materials;
 int nten=( (nmat-1)*(nmat-1)+nmat-1 )/2;
 int nburning=nten*(BL_SPACEDIM+1);
 int nden=nmat*num_state_material;
 int nstate=num_materials_vel*(BL_SPACEDIM+1)+
  nmat*(num_state_material+ngeom_raw)+1;
 int scomp_mofvars=num_materials_vel*(BL_SPACEDIM+1)+
  nmat*num_state_material;

 // mask=1 if not covered or if outside the domain.
 // NavierStokes::maskfiner_localMF
 // NavierStokes::maskfiner
 resize_maskfiner(1,MASKCOEF_MF);
 debug_ngrow(MASKCOEF_MF,1,28); 

 if (localMF[JUMP_STRENGTH_MF]->nGrow()!=ngrow_expansion)
  BoxLib::Error("jump strength invalid ngrow level_phase_change_conv");
 if (localMF[JUMP_STRENGTH_MF]->nComp()!=2*nten)
  BoxLib::Error("localMF[JUMP_STRENGTH_MF]->nComp() invalid");

 if (localMF[nodevel_MF]->nGrow()!=1)
  BoxLib::Error("localMF[nodevel_MF]->nGrow()  invalid");
 if (localMF[nodevel_MF]->nComp()!=nten*BL_SPACEDIM)
  BoxLib::Error("localMF[nodevel_MF]->nComp()  invalid");

 if (localMF[deltaVOF_MF]->nComp()!=nmat)
  BoxLib::Error("localMF[deltaVOF_MF]->nComp()  invalid");

 if (localMF[BURNING_VELOCITY_MF]->nComp()!=nburning)
  BoxLib::Error("burning vel invalid ncomp");

  // in: level_phase_change_convert
  // DEN_RECON_MF is initialized prior to isweep==0
 if (localMF[DEN_RECON_MF]->nComp()!=nden)
  BoxLib::Error("DEN_RECON_MF invalid ncomp");

 debug_ngrow(HOLD_LS_DATA_MF,ngrow_distance,30);
 if (localMF[HOLD_LS_DATA_MF]->nComp()!=nmat*(1+BL_SPACEDIM)) 
  BoxLib::Error("localMF[HOLD_LS_DATA_MF]->nComp() invalid");

 MultiFab& LS_new = get_new_data(LS_Type,slab_step+1);
 if (LS_new.nComp()!=nmat*(BL_SPACEDIM+1)) 
  BoxLib::Error("LS_new invalid ncomp");

 MultiFab& S_new = get_new_data(State_Type,slab_step+1);
 if (nstate!=S_new.nComp())
  BoxLib::Error("nstate invalid");

 const Real* dx = geom.CellSize();

 Array< Array<Real> > DVOF_local;
 DVOF_local.resize(thread_class::nthreads);
 for (int tid=0;tid<thread_class::nthreads;tid++) {
  DVOF_local[tid].resize(nmat);
  for (int im=0;im<nmat;im++)
   DVOF_local[tid][im]=0.0;
 } // tid

 Array< Array<Real> > delta_mass_local;
 delta_mass_local.resize(thread_class::nthreads);
 for (int tid=0;tid<thread_class::nthreads;tid++) {
  delta_mass_local[tid].resize(2*nmat); // source 1..nmat  dest 1..nmat
  for (int im=0;im<2*nmat;im++)
   delta_mass_local[tid][im]=0.0;
 } // tid


 if (isweep==0) {

#ifdef _OPENMP
#pragma omp parallel
#endif
{
  for (MFIter mfi(S_new,use_tiling); mfi.isValid(); ++mfi) {
   BL_ASSERT(grids[mfi.index()] == mfi.validbox());
   const int gridno = mfi.index();
   const Box& tilegrid = mfi.tilebox();
   const Box& fabgrid = grids[gridno];
   const int* tilelo=tilegrid.loVect();
   const int* tilehi=tilegrid.hiVect();
   const int* fablo=fabgrid.loVect();
   const int* fabhi=fabgrid.hiVect();

   const Real* xlo = grid_loc[gridno].lo();
   Array<int> velbc=getBCArray(State_Type,gridno,0,
    num_materials_vel*BL_SPACEDIM);

   FArrayBox& burnvelfab=(*localMF[BURNING_VELOCITY_MF])[mfi];
   FArrayBox& nodevelfab=(*localMF[nodevel_MF])[mfi];
   if (burnvelfab.nComp()==nburning) {
    // do nothing
   } else 
    BoxLib::Error("burnvelfab.nComp() invalid");

   if (nodevelfab.nComp()==nten*BL_SPACEDIM) {
    // do nothing
   } else 
    BoxLib::Error("nodevelfab.nComp() invalid");

   int bfact=parent->Space_blockingFactor(level);

     // burnvelfab=BURNING_VELOCITY_MF is cell centered (interior: lo to hi)
     // nodevelfab=nodevel is at the nodes. (interior: lo to hi+1)
   FORT_NODEDISPLACE(
    &nmat,
    &nten,
    &nburning,
    tilelo,tilehi,
    fablo,fabhi,
    &bfact, 
    velbc.dataPtr(),
    &dt_slab,
    nodevelfab.dataPtr(),
    ARLIM(nodevelfab.loVect()),ARLIM(nodevelfab.hiVect()),
    burnvelfab.dataPtr(),
    ARLIM(burnvelfab.loVect()),ARLIM(burnvelfab.hiVect()),
    xlo,dx, 
    &level,&finest_level);
  } // mfi
} // omp
  ParallelDescriptor::Barrier();

  if (1==0) {
   int gridno=0;
   const Box& fabgrid = grids[gridno];
   const int* fablo=fabgrid.loVect();
   const int* fabhi=fabgrid.hiVect();
   const Real* xlo = grid_loc[gridno].lo();
   int interior_only=0;
   FArrayBox& nodevelfab=(*localMF[nodevel_MF])[0];
   const Real* dxplot = geom.CellSize();
   int scomp=0;
   int ncomp=nten*BL_SPACEDIM;
   int dirplot=-1;
   int id=0;
   std::cout << "dt_slab = " << dt_slab << '\n';
   tecplot_debug(nodevelfab,xlo,fablo,fabhi,dxplot,dirplot,id,
     scomp,ncomp,interior_only);
  }

 } else if (isweep==1) {
  // do nothing
 } else
  BoxLib::Error("isweep invalid");

 if (isweep==0) {
  // do nothing
 } else if (isweep==1) {
  for (int im=0;im<nmat;im++)
   DVOF_local[0][im]=DVOF[0][im];
 } else
  BoxLib::Error("isweep invalid");

 VOF_Recon_resize(normal_probe_size+3,SLOPE_RECON_MF);

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(S_new,use_tiling); mfi.isValid(); ++mfi) {
   BL_ASSERT(grids[mfi.index()] == mfi.validbox());
   const int gridno = mfi.index();
   const Box& tilegrid = mfi.tilebox();
   const Box& fabgrid = grids[gridno];
   const int* tilelo=tilegrid.loVect();
   const int* tilehi=tilegrid.hiVect();
   const int* fablo=fabgrid.loVect();
   const int* fabhi=fabgrid.hiVect();

   const Real* xlo = grid_loc[gridno].lo();

   Array<int> vofbc=getBCArray(State_Type,gridno,scomp_mofvars,1);

    // mask=tag if not covered by level+1 or outside the domain.
   FArrayBox& maskcov=(*localMF[MASKCOEF_MF])[mfi];

   FArrayBox& deltafab=(*localMF[deltaVOF_MF])[mfi];

   FArrayBox& nodevelfab=(*localMF[nodevel_MF])[mfi];
   if (nodevelfab.nComp()==nten*BL_SPACEDIM) {
    // do nothing
   } else 
    BoxLib::Error("nodevelfab.nComp() invalid");

   FArrayBox& olddistfab=(*localMF[HOLD_LS_DATA_MF])[mfi];
   FArrayBox& sweptfab=(*localMF[SWEPT_CROSSING_MF])[mfi];
   FArrayBox& lsnewfab=LS_new[mfi];
   FArrayBox& snewfab=S_new[mfi];

   FArrayBox& reconfab=(*localMF[SLOPE_RECON_MF])[mfi]; 

   FArrayBox& eosfab=(*localMF[DEN_RECON_MF])[mfi];

   FArrayBox& JUMPfab=(*localMF[JUMP_STRENGTH_MF])[mfi];
   int bfact=parent->Space_blockingFactor(level);

   int tid=0;
   if (isweep==0) {
    tid=ns_thread();
   } else if (isweep==1) {
    tid=0; // only use 0th component for 2nd sweep
   } else
    BoxLib::Error("isweep invalid");

   int tid_data=ns_thread();

    // nodevelfab (=nodevel) is node based (interior: lo..hi+1)
   FORT_CONVERTMATERIAL( 
    &tid_data,
    &isweep, 
    &solvability_projection,
    &ngrow_expansion,
    &level,&finest_level,
    &normal_probe_size,
    &nmat,
    &nten,
    &nden,
    &nstate,
    density_floor_expansion.dataPtr(),
    density_ceiling_expansion.dataPtr(),
    latent_heat.dataPtr(),
    saturation_temp.dataPtr(),
    freezing_model.dataPtr(),
    mass_fraction_id.dataPtr(),
    species_evaporation_density.dataPtr(),
    distribute_from_target.dataPtr(),
    tilelo,tilehi,
    fablo,fabhi,
    &bfact, 
    vofbc.dataPtr(),xlo,dx,
    &dt_slab,
    delta_mass_local[tid_data].dataPtr(),
    DVOF_local[tid].dataPtr(),
    maskcov.dataPtr(),
    ARLIM(maskcov.loVect()),ARLIM(maskcov.hiVect()),
    deltafab.dataPtr(),
    ARLIM(deltafab.loVect()),ARLIM(deltafab.hiVect()),
    nodevelfab.dataPtr(),
    ARLIM(nodevelfab.loVect()),ARLIM(nodevelfab.hiVect()),
    JUMPfab.dataPtr(),
    ARLIM(JUMPfab.loVect()),ARLIM(JUMPfab.hiVect()),
    olddistfab.dataPtr(),
    ARLIM(olddistfab.loVect()),ARLIM(olddistfab.hiVect()),
    lsnewfab.dataPtr(),
    ARLIM(lsnewfab.loVect()),ARLIM(lsnewfab.hiVect()),
    reconfab.dataPtr(),
    ARLIM(reconfab.loVect()),ARLIM(reconfab.hiVect()),
    snewfab.dataPtr(),
    ARLIM(snewfab.loVect()),ARLIM(snewfab.hiVect()),
    eosfab.dataPtr(),
    ARLIM(eosfab.loVect()),ARLIM(eosfab.hiVect()),
    sweptfab.dataPtr(),
    ARLIM(sweptfab.loVect()),ARLIM(sweptfab.hiVect()));
 } // mfi
} // omp
 if (isweep==0) {
  for (int tid=1;tid<thread_class::nthreads;tid++) {
   for (int im=0;im<nmat;im++) 
    DVOF_local[0][im]+=DVOF_local[tid][im];
  } // tid
 } else if (isweep==1) {
  for (int tid=1;tid<thread_class::nthreads;tid++) {
   for (int im=0;im<2*nmat;im++) {
    delta_mass_local[0][im]+=delta_mass_local[tid][im];
   }
  } // tid
 } else {
  BoxLib::Error("isweep invalid");
 }

 ParallelDescriptor::Barrier();

 if (isweep==0) {
  for (int im=0;im<nmat;im++) {
   ParallelDescriptor::ReduceRealSum(DVOF_local[0][im]);
   DVOF[0][im]+=DVOF_local[0][im];
  }  // im
 } else if (isweep==1) {
  for (int im=0;im<2*nmat;im++) {
   ParallelDescriptor::ReduceRealSum(delta_mass_local[0][im]);
   delta_mass[0][im]+=delta_mass_local[0][im];
  }
 } else
  BoxLib::Error("isweep invalid");

 localMF[JUMP_STRENGTH_MF]->FillBoundary(geom.periodicity());
 avgDown_localMF(JUMP_STRENGTH_MF,0,2*nten,0);

} // subroutine level_phase_change_convert

void
NavierStokes::phase_change_redistributeALL() {

 if (level!=0)
  BoxLib::Error("level invalid phase_change_redistributeALL");

 int nmat=num_materials;
 int nten=( (nmat-1)*(nmat-1)+nmat-1 )/2;

 int finest_level=parent->finestLevel();
 for (int ilev=finest_level;ilev>=level;ilev--) {
  NavierStokes& ns_level=getLevel(ilev);
  ns_level.getStateDist_localMF(LSNEW_MF,2*ngrow_expansion,cur_time_slab,4);
 }

 mdotplus.resize(thread_class::nthreads);
 mdotminus.resize(thread_class::nthreads);
 mdotcount.resize(thread_class::nthreads);
 mdot_lost.resize(thread_class::nthreads);
 mdot_sum.resize(thread_class::nthreads);
 mdot_sum2.resize(thread_class::nthreads);

 for (int im=1;im<=nmat;im++) {
  for (int im_opp=im+1;im_opp<=nmat;im_opp++) {
   for (int ireverse=0;ireverse<=1;ireverse++) {
    if ((im>nmat)||(im_opp>nmat))
     BoxLib::Error("im or im_opp bust 200cpp");
    int iten;
    get_iten_cpp(im,im_opp,iten,nmat);
    if ((iten<1)||(iten>nten))
     BoxLib::Error("iten invalid");

    int indexEXP=iten+ireverse*nten-1;

    Real LL=latent_heat[indexEXP];
    int distribute_from_targ=distribute_from_target[indexEXP];

    int im_source=-1;
    int im_dest=-1;

    if ((ns_is_rigid(im-1)==1)||
        (ns_is_rigid(im_opp-1)==1)) { 
     // do nothing
    } else if (LL!=0.0) {

     if (ireverse==0) {
      im_source=im;im_dest=im_opp;
     } else if (ireverse==1) {
      im_source=im_opp;im_dest=im;
     } else
      BoxLib::Error("ireverse invalid");

     Real expect_mdot_sign=0.0;
     if ((distribute_from_targ==0)||
         (distribute_from_targ==1)) {
      expect_mdot_sign=( 
       (denconst[im_source-1]>denconst[im_dest-1]) ? 1.0 : -1.0 );
     } else
      BoxLib::Error("distribute_from_targ invalid");

     for (int tid=0;tid<thread_class::nthreads;tid++) {
      mdotplus[tid]=0.0;
      mdotminus[tid]=0.0;
      mdotcount[tid]=0.0;
      mdot_sum[tid]=0.0;
      mdot_sum2[tid]=0.0;
      mdot_lost[tid]=0.0;
     }

     allocate_array(2*ngrow_expansion,1,-1,donorflag_MF);
     setVal_array(2*ngrow_expansion,1,0.0,donorflag_MF);

     for (int isweep=0;isweep<3;isweep++) {

      for (int ilev=finest_level;ilev>=level;ilev--) {
       NavierStokes& ns_level=getLevel(ilev);
       ns_level.level_phase_change_redistribute(
        expect_mdot_sign,im_source,im_dest,indexEXP,
        isweep);
      } // ilev=finest_level ... level

      if (isweep==0) {
       std::cout << "before:imsrc,imdst,mdot_sum " <<
        im_source << ' ' << im_dest << ' ' << mdot_sum[0] << '\n';
      } else if (isweep==1) {
       // do nothing
      } else if (isweep==2) {
       std::cout << "after:imsrc,imdst,mdot_sum2 " <<   
        im_source << ' ' << im_dest << ' ' << mdot_sum2[0] << '\n';
       std::cout << "after:imsrc,imdst,mdot_lost " <<   
        im_source << ' ' << im_dest << ' ' << mdot_lost[0] << '\n';
       std::cout << "imsrc,imdst,mdot_sum2+mdot_lost " <<   
        im_source << ' ' << im_dest << ' ' <<   
        mdot_sum2[0]+mdot_lost[0] << '\n';
      } else
       BoxLib::Error("isweep invalid");

     } // isweep=0,1,2

     delete_array(donorflag_MF);

    } // LL!=0
   } // ireverse
  } // im_opp
 } // im=1..nmat


   // copy contributions from all materials changing phase to a single
   // source term.
 int isweep=3;

 for (int tid=0;tid<thread_class::nthreads;tid++) {
  mdotplus[tid]=0.0;
  mdotminus[tid]=0.0;
  mdotcount[tid]=0.0;
  mdot_sum[tid]=0.0;
  mdot_sum2[tid]=0.0;
  mdot_lost[tid]=0.0;
 }

 Real expect_mdot_sign_filler=0.0;
 int im_source_filler=-1;
 int im_dest_filler=-1;
 int indexEXP_filler=-1;
  
 for (int ilev=finest_level;ilev>=level;ilev--) {
  NavierStokes& ns_level=getLevel(ilev);
  ns_level.level_phase_change_redistribute(
   expect_mdot_sign_filler,
   im_source_filler,im_dest_filler,
   indexEXP_filler,isweep);
 } // ilev=finest_level ... level

 if (verbose>0) {
  if (ParallelDescriptor::IOProcessor()) {
   std::cout << "mdotplus = " << mdotplus[0] << '\n';
   std::cout << "mdotminus = " << mdotminus[0] << '\n';
   std::cout << "mdotcount = " << mdotcount[0] << '\n';
  } // IOProc?
 } // verbose>0

 if (solvability_projection==1) {

  int isweep=4;

  for (int ilev=finest_level;ilev>=level;ilev--) {
   NavierStokes& ns_level=getLevel(ilev);
   ns_level.level_phase_change_redistribute(
    expect_mdot_sign_filler,
    im_source_filler,im_dest_filler,
    indexEXP_filler,isweep);
  } // ilev=finest_level ... level

 } else if (solvability_projection==0) {
  // do nothing
 } else
  BoxLib::Error("solvability_projection invalid");

 delete_array(LSNEW_MF);
 delete_array(HOLD_LS_DATA_MF);

} // subroutine phase_change_redistributeALL

void
NavierStokes::level_phase_change_redistribute(
 Real expect_mdot_sign,
 int im_source,int im_dest,int indexEXP,
 int isweep) {

 bool use_tiling=ns_tiling;
 int finest_level=parent->finestLevel();
 if ((level<0)||(level>finest_level))
  BoxLib::Error("level invalid level_phase_change_redistribute");

 int nmat=num_materials;
 int nten=( (nmat-1)*(nmat-1)+nmat-1 )/2;
 int scomp_mofvars=num_materials_vel*(BL_SPACEDIM+1)+
  nmat*num_state_material;

 debug_ngrow(JUMP_STRENGTH_MF,ngrow_expansion,355);
 if (localMF[JUMP_STRENGTH_MF]->nComp()!=2*nten)
  BoxLib::Error("localMF[JUMP_STRENGTH_MF]->nComp()!=2*nten level_phase ...");

 resize_maskfiner(1,MASKCOEF_MF);
 debug_ngrow(MASKCOEF_MF,1,6001);
 
 if (localMF[LSNEW_MF]->nComp()!=nmat*(1+BL_SPACEDIM))
  BoxLib::Error("localMF[LSNEW_MF]->nComp() invalid");
 debug_ngrow(LSNEW_MF,2*ngrow_expansion,6001);

 const Real* dx = geom.CellSize();

  // tags for redistribution of source term
  // 1=> donor  2=> receiver  0=> neither

 Real LL=0.0;

 if ((isweep==0)||(isweep==1)||(isweep==2)) {
  if (localMF[donorflag_MF]->nGrow()!=2*ngrow_expansion)
   BoxLib::Error("localMF[donorflag_MF]->ngrow() invalid");
  if (localMF[donorflag_MF]->nComp()!=1)
   BoxLib::Error("localMF[donorflag_MF]->nComp() invalid");

  if ((indexEXP>=0)&&(indexEXP<2*nten)) {
   LL=latent_heat[indexEXP];
  } else
   BoxLib::Error("indexEXP invalid");

 } else if ((isweep==3)||(isweep==4)) {

  if (indexEXP==-1) {
   LL=0.0;
  } else
   BoxLib::Error("indexEXP invalid");

 } else
  BoxLib::Error("isweep invalid");

 VOF_Recon_resize(1,SLOPE_RECON_MF);
  
 if (isweep==0) {
 
  if (LL==0.0)
   BoxLib::Error("LL invalid");
  if (fabs(expect_mdot_sign)!=1.0)
   BoxLib::Error("expect_mdot_sign invalid");
  if ((im_source<1)||(im_source>nmat))
   BoxLib::Error("im_source invalid");
  if ((im_dest<1)||(im_dest>nmat))
   BoxLib::Error("im_dest invalid");
  if ((indexEXP<0)||(indexEXP>=2*nten))
   BoxLib::Error("indexEXP invalid");
  
  Array< Real > mdot_sum_local;
  mdot_sum_local.resize(thread_class::nthreads);
  for (int tid=0;tid<thread_class::nthreads;tid++) {
   mdot_sum_local[tid]=0.0;
  }

#ifdef _OPENMP
#pragma omp parallel
#endif
{
  for (MFIter mfi(*localMF[donorflag_MF]); mfi.isValid(); ++mfi) {
   BL_ASSERT(grids[mfi.index()] == mfi.validbox());
   const int gridno = mfi.index();
   const Box& tilegrid = mfi.tilebox();
   const Box& fabgrid = grids[gridno];
   const int* tilelo=tilegrid.loVect();
   const int* tilehi=tilegrid.hiVect();
   const int* fablo=fabgrid.loVect();
   const int* fabhi=fabgrid.hiVect();
   const Real* xlo = grid_loc[gridno].lo();
   Array<int> vofbc=getBCArray(State_Type,gridno,scomp_mofvars,1);

   FArrayBox& maskcov=(*localMF[MASKCOEF_MF])[mfi];

   FArrayBox& donorfab=(*localMF[donorflag_MF])[mfi];
   FArrayBox& JUMPfab=(*localMF[JUMP_STRENGTH_MF])[mfi];
   FArrayBox& reconfab=(*localMF[SLOPE_RECON_MF])[mfi]; 
   FArrayBox& newdistfab=(*localMF[LSNEW_MF])[mfi];
   int bfact=parent->Space_blockingFactor(level);
   int tid=ns_thread();

    // isweep==0 
   FORT_TAGEXPANSION( 
    latent_heat.dataPtr(),
    freezing_model.dataPtr(),
    distribute_from_target.dataPtr(),
    &ngrow_expansion,
    &cur_time_slab,
    vofbc.dataPtr(),
    &expect_mdot_sign,
    &mdot_sum_local[tid],
    &im_source,
    &im_dest,
    &indexEXP,
    &level,&finest_level,
    &nmat,&nten, 
    tilelo,tilehi,
    fablo,fabhi,
    &bfact, 
    xlo,dx,&dt_slab,
    maskcov.dataPtr(),
    ARLIM(maskcov.loVect()),ARLIM(maskcov.hiVect()),
    donorfab.dataPtr(),
    ARLIM(donorfab.loVect()),ARLIM(donorfab.hiVect()),
    JUMPfab.dataPtr(),
    ARLIM(JUMPfab.loVect()),ARLIM(JUMPfab.hiVect()),
    newdistfab.dataPtr(),
    ARLIM(newdistfab.loVect()),ARLIM(newdistfab.hiVect()),
    reconfab.dataPtr(),
    ARLIM(reconfab.loVect()),ARLIM(reconfab.hiVect()));
 
  } // mfi
} // omp
  for (int tid=1;tid<thread_class::nthreads;tid++) {
   mdot_sum_local[0]+=mdot_sum_local[tid];
  }
  ParallelDescriptor::Barrier();
  ParallelDescriptor::ReduceRealSum(mdot_sum_local[0]);
  mdot_sum[0]+=mdot_sum_local[0];

  localMF[donorflag_MF]->FillBoundary(geom.periodicity());
  avgDown_tag_localMF(donorflag_MF);

 } else if (isweep==1) {

   // redistribution.

  if (LL==0.0)
   BoxLib::Error("LL invalid");
  if (fabs(expect_mdot_sign)!=1.0)
   BoxLib::Error("expect_mdot_sign invalid");
  if ((im_source<1)||(im_source>nmat))
   BoxLib::Error("im_source invalid");
  if ((im_dest<1)||(im_dest>nmat))
   BoxLib::Error("im_dest invalid");
  if ((indexEXP<0)||(indexEXP>=2*nten))
   BoxLib::Error("indexEXP invalid");


#ifdef _OPENMP
#pragma omp parallel
#endif
{
  for (MFIter mfi(*localMF[donorflag_MF]); mfi.isValid(); ++mfi) {
    BL_ASSERT(grids[mfi.index()] == mfi.validbox());
    const int gridno = mfi.index();
    const Box& tilegrid = mfi.tilebox();
    const Box& fabgrid = grids[gridno];
    const int* tilelo=tilegrid.loVect();
    const int* tilehi=tilegrid.hiVect();
    const int* fablo=fabgrid.loVect();
    const int* fabhi=fabgrid.hiVect();
    const Real* xlo = grid_loc[gridno].lo();
    Array<int> vofbc=getBCArray(State_Type,gridno,scomp_mofvars,1);

    FArrayBox& maskcov=(*localMF[MASKCOEF_MF])[mfi];
    FArrayBox& donorfab=(*localMF[donorflag_MF])[mfi];
    FArrayBox& JUMPfab=(*localMF[JUMP_STRENGTH_MF])[mfi];
    FArrayBox& newdistfab=(*localMF[LSNEW_MF])[mfi];

    int bfact=parent->Space_blockingFactor(level);

     // isweep==1
    FORT_DISTRIBUTEEXPANSION( 
     &ngrow_expansion,
     &im_source,
     &im_dest,
     &indexEXP,
     &level,&finest_level,
     &nmat,&nten, 
     tilelo,tilehi,
     fablo,fabhi,
     &bfact, 
     xlo,dx,&dt_slab,
     maskcov.dataPtr(),
     ARLIM(maskcov.loVect()),ARLIM(maskcov.hiVect()),
     newdistfab.dataPtr(),
     ARLIM(newdistfab.loVect()),ARLIM(newdistfab.hiVect()),
     donorfab.dataPtr(),
     ARLIM(donorfab.loVect()),ARLIM(donorfab.hiVect()),
     JUMPfab.dataPtr(),
     ARLIM(JUMPfab.loVect()),ARLIM(JUMPfab.hiVect()));
  } // mfi
} //omp
  ParallelDescriptor::Barrier();

 } else if (isweep==2) {

   // clear out mdot in the donor cells.

  if (LL==0.0)
   BoxLib::Error("LL invalid");
  if (fabs(expect_mdot_sign)!=1.0)
   BoxLib::Error("expect_mdot_sign invalid");
  if ((im_source<1)||(im_source>nmat))
   BoxLib::Error("im_source invalid");
  if ((im_dest<1)||(im_dest>nmat))
   BoxLib::Error("im_dest invalid");
  if ((indexEXP<0)||(indexEXP>=2*nten))
   BoxLib::Error("indexEXP invalid");

  Array< Real > mdot_lost_local;
  Array< Real > mdot_sum2_local;
  mdot_lost_local.resize(thread_class::nthreads);
  mdot_sum2_local.resize(thread_class::nthreads);
  for (int tid=0;tid<thread_class::nthreads;tid++) {
   mdot_sum2_local[tid]=0.0;
   mdot_lost_local[tid]=0.0;
  }
#ifdef _OPENMP
#pragma omp parallel
#endif
{
  for (MFIter mfi(*localMF[donorflag_MF],use_tiling);mfi.isValid(); ++mfi) {
    BL_ASSERT(grids[mfi.index()] == mfi.validbox());
    const int gridno = mfi.index();
    const Box& tilegrid = mfi.tilebox();
    const Box& fabgrid = grids[gridno];
    const int* tilelo=tilegrid.loVect();
    const int* tilehi=tilegrid.hiVect();
    const int* fablo=fabgrid.loVect();
    const int* fabhi=fabgrid.hiVect();
    const Real* xlo = grid_loc[gridno].lo();
    Array<int> vofbc=getBCArray(State_Type,gridno,scomp_mofvars,1);

    FArrayBox& maskcov=(*localMF[MASKCOEF_MF])[mfi];
    FArrayBox& donorfab=(*localMF[donorflag_MF])[mfi];
    FArrayBox& JUMPfab=(*localMF[JUMP_STRENGTH_MF])[mfi];

    int bfact=parent->Space_blockingFactor(level);
    int tid=ns_thread();

     // isweep==2
    FORT_CLEAREXPANSION( 
     &ngrow_expansion,
     &mdot_sum2_local[tid],
     &mdot_lost_local[tid],
     &im_source,
     &im_dest,
     &indexEXP,
     &level,&finest_level,
     &nmat,&nten, 
     tilelo,tilehi,
     fablo,fabhi,
     &bfact, 
     xlo,dx,&dt_slab,
     maskcov.dataPtr(),
     ARLIM(maskcov.loVect()),ARLIM(maskcov.hiVect()),
     donorfab.dataPtr(),
     ARLIM(donorfab.loVect()),ARLIM(donorfab.hiVect()),
     JUMPfab.dataPtr(),
     ARLIM(JUMPfab.loVect()),ARLIM(JUMPfab.hiVect()));
  } // mfi
} // omp
  for (int tid=1;tid<thread_class::nthreads;tid++) {
   mdot_sum2_local[0]+=mdot_sum2_local[tid];
   mdot_lost_local[0]+=mdot_lost_local[tid];
  } // tid
  ParallelDescriptor::Barrier();
  ParallelDescriptor::ReduceRealSum(mdot_sum2_local[0]);
  ParallelDescriptor::ReduceRealSum(mdot_lost_local[0]);
  mdot_sum2[0]+=mdot_sum2_local[0];
  mdot_lost[0]+=mdot_lost_local[0];

 } else if (isweep==3) {

  Array<Real> mdotplus_local;
  Array<Real> mdotminus_local;
  Array<Real> mdotcount_local;
  mdotplus_local.resize(thread_class::nthreads);
  mdotminus_local.resize(thread_class::nthreads);
  mdotcount_local.resize(thread_class::nthreads);

  for (int tid=0;tid<thread_class::nthreads;tid++) {
   mdotplus_local[tid]=0.0;
   mdotminus_local[tid]=0.0;
   mdotcount_local[tid]=0.0;
  }
 
#ifdef _OPENMP
#pragma omp parallel 
#endif
{
  for (MFIter mfi(*localMF[MDOT_MF],use_tiling); mfi.isValid(); ++mfi) {
    BL_ASSERT(grids[mfi.index()] == mfi.validbox());
    const int gridno = mfi.index();
    const Box& tilegrid = mfi.tilebox();
    const Box& fabgrid = grids[gridno];
    const int* tilelo=tilegrid.loVect();
    const int* tilehi=tilegrid.hiVect();
    const int* fablo=fabgrid.loVect();
    const int* fabhi=fabgrid.hiVect();
    const Real* xlo = grid_loc[gridno].lo();

    FArrayBox& maskcov=(*localMF[MASKCOEF_MF])[mfi];
    FArrayBox& mdotfab=(*localMF[MDOT_MF])[mfi];
    FArrayBox& JUMPfab=(*localMF[JUMP_STRENGTH_MF])[mfi];
    FArrayBox& newdistfab=(*localMF[LSNEW_MF])[mfi];

    int bfact=parent->Space_blockingFactor(level);
    int tid=ns_thread();

    // NavierStokes::allocate_mdot() called at the beginning of
    //  NavierStokes::do_the_advance
    // mdot initialized in NavierStokes::prelim_alloc()
    // mdot updated in nucleate_bubbles.
    FORT_INITJUMPTERM( 
     &mdotplus_local[tid],
     &mdotminus_local[tid],
     &mdotcount_local[tid],
     &ngrow_expansion,
     &cur_time_slab,
     &level,&finest_level,
     &nmat,&nten,
     latent_heat.dataPtr(),
     saturation_temp.dataPtr(),
     freezing_model.dataPtr(),
     distribute_from_target.dataPtr(),
     tilelo,tilehi,
     fablo,fabhi,
     &bfact, 
     xlo,dx,&dt_slab,
     maskcov.dataPtr(),
     ARLIM(maskcov.loVect()),ARLIM(maskcov.hiVect()),
     JUMPfab.dataPtr(),ARLIM(JUMPfab.loVect()),ARLIM(JUMPfab.hiVect()),
      // mdotfab is incremented.
     mdotfab.dataPtr(),ARLIM(mdotfab.loVect()),ARLIM(mdotfab.hiVect()),
     newdistfab.dataPtr(),
     ARLIM(newdistfab.loVect()),ARLIM(newdistfab.hiVect()));

  } // mfi
} // omp
  for (int tid=1;tid<thread_class::nthreads;tid++) {
   mdotplus_local[0]+=mdotplus_local[tid];
   mdotminus_local[0]+=mdotminus_local[tid];
   mdotcount_local[0]+=mdotcount_local[tid];
  }
  ParallelDescriptor::Barrier();
  ParallelDescriptor::ReduceRealSum(mdotplus_local[0]);
  ParallelDescriptor::ReduceRealSum(mdotminus_local[0]);
  ParallelDescriptor::ReduceRealSum(mdotcount_local[0]);

  mdotplus[0]+=mdotplus_local[0];
  mdotminus[0]+=mdotminus_local[0];
  mdotcount[0]+=mdotcount_local[0];

 } else if (isweep==4) {

  if (solvability_projection!=1)
   BoxLib::Error("solvability_projection!=1");

#ifdef _OPENMP
#pragma omp parallel
#endif
{
  for (MFIter mfi(*localMF[MDOT_MF],use_tiling); mfi.isValid(); ++mfi) {
   BL_ASSERT(grids[mfi.index()] == mfi.validbox());
   const int gridno = mfi.index();
   const Box& tilegrid = mfi.tilebox();
   const Box& fabgrid = grids[gridno];
   const int* tilelo=tilegrid.loVect();
   const int* tilehi=tilegrid.hiVect();
   const int* fablo=fabgrid.loVect();
   const int* fabhi=fabgrid.hiVect();
   const Real* xlo = grid_loc[gridno].lo();

   FArrayBox& maskcov=(*localMF[MASKCOEF_MF])[mfi];
   FArrayBox& mdotfab=(*localMF[MDOT_MF])[mfi];

   int bfact=parent->Space_blockingFactor(level);

    // NavierStokes::allocate_mdot() called at the beginning of
    //  NavierStokes::do_the_advance
    // mdot initialized in NavierStokes::prelim_alloc()
    // mdot updated in nucleate_bubbles.
   FORT_RENORM_MDOT( 
    &mdotplus[0],&mdotminus[0],&mdotcount[0],
    &level,&finest_level,
    &nmat,&nten,
    tilelo,tilehi,
    fablo,fabhi,
    &bfact, 
    xlo,dx,&dt_slab,
    maskcov.dataPtr(),
    ARLIM(maskcov.loVect()),ARLIM(maskcov.hiVect()),
    mdotfab.dataPtr(),
    ARLIM(mdotfab.loVect()),ARLIM(mdotfab.hiVect()));

  } // mfi
} // omp
  ParallelDescriptor::Barrier();

 } else
  BoxLib::Error("isweep invalid");

} // subroutine level_phase_change_redistribute

// called from: NavierStokes::make_physics_varsALL
void
NavierStokes::level_init_icemask() {

 bool use_tiling=ns_tiling;
 int finest_level=parent->finestLevel();
 if ((level<0)||(level>finest_level))
  BoxLib::Error("level invalid level_init_icemask");

 int nmat=num_materials;
 int nten=( (nmat-1)*(nmat-1)+nmat-1 )/2;

 resize_maskfiner(1,MASKCOEF_MF);
 VOF_Recon_resize(1,SLOPE_RECON_MF);

 debug_ngrow(SLOPE_RECON_MF,1,3);

 for (int dir=0;dir<BL_SPACEDIM;dir++)
  debug_ngrow(FACE_VAR_MF+dir,0,2);

 debug_ngrow(MASKCOEF_MF,1,6001);

 getStateDist_localMF(LSNEW_MF,1,cur_time_slab,5);

 debug_ngrow(LSNEW_MF,1,6001);
 if (localMF[LSNEW_MF]->nComp()!=nmat*(1+BL_SPACEDIM))
  BoxLib::Error("localMF[LSNEW_MF]->nComp() invalid");

 const Real* dx = geom.CellSize();

#ifdef _OPENMP
#pragma omp parallel 
#endif
{
  for (MFIter mfi(*localMF[LSNEW_MF],use_tiling); mfi.isValid(); ++mfi) {
   BL_ASSERT(grids[mfi.index()] == mfi.validbox());
   const int gridno = mfi.index();
   const Box& tilegrid = mfi.tilebox();
   const Box& fabgrid = grids[gridno];
   const int* tilelo=tilegrid.loVect();
   const int* tilehi=tilegrid.hiVect();
   const int* fablo=fabgrid.loVect();
   const int* fabhi=fabgrid.hiVect();
   const Real* xlo = grid_loc[gridno].lo();

   FArrayBox& maskcov=(*localMF[MASKCOEF_MF])[mfi];

   FArrayBox& xface=(*localMF[FACE_VAR_MF])[mfi];
   FArrayBox& yface=(*localMF[FACE_VAR_MF+1])[mfi];
   FArrayBox& zface=(*localMF[FACE_VAR_MF+BL_SPACEDIM-1])[mfi];
   FArrayBox& reconfab=(*localMF[SLOPE_RECON_MF])[mfi]; 
   FArrayBox& newdistfab=(*localMF[LSNEW_MF])[mfi];
   int bfact=parent->Space_blockingFactor(level);
   
    // in: GODUNOV_3D.F90
   FORT_INIT_ICEMASK( 
    &cur_time_slab,
    &facecut_index,
    &icefacecut_index,
    &icemask_index,
    &massface_index,
    &vofface_index,
    &ncphys,
    &level,&finest_level,
    &nmat,&nten,
    latent_heat.dataPtr(),
    saturation_temp.dataPtr(),
    freezing_model.dataPtr(),
    distribute_from_target.dataPtr(),
    tilelo,tilehi,
    fablo,fabhi,
    &bfact, 
    xlo,dx,&dt_slab,
    maskcov.dataPtr(),
    ARLIM(maskcov.loVect()),ARLIM(maskcov.hiVect()),
    xface.dataPtr(),ARLIM(xface.loVect()),ARLIM(xface.hiVect()),
    yface.dataPtr(),ARLIM(yface.loVect()),ARLIM(yface.hiVect()),
    zface.dataPtr(),ARLIM(zface.loVect()),ARLIM(zface.hiVect()),
    newdistfab.dataPtr(),
    ARLIM(newdistfab.loVect()),ARLIM(newdistfab.hiVect()),
    reconfab.dataPtr(),ARLIM(reconfab.loVect()),ARLIM(reconfab.hiVect()));

  } // mfi
} // omp
  ParallelDescriptor::Barrier();

  delete_localMF(LSNEW_MF,1);

} // subroutine level_init_icemask

void
NavierStokes::nucleate_bubbles() {

 int finest_level=parent->finestLevel();

 if ((level<0)||(level>finest_level))
  BoxLib::Error("level invalid nucleate_bubbles");

 bool use_tiling=ns_tiling;

 if ((is_phasechange!=1)&&
     (is_cavitation!=1)&&
     (is_cavitation_mixture_model!=1))
  BoxLib::Error("is_phasech, is_cav, or is_cav_mm invalid: nucleate_bubbles");

 if (num_state_base!=2)
  BoxLib::Error("num_state_base invalid");
 if (num_materials_vel!=1)
  BoxLib::Error("num_materials_vel invalid");

 int nmat=num_materials;
 int nten=( (nmat-1)*(nmat-1)+nmat-1 )/2;
 int nstate=num_materials_vel*(BL_SPACEDIM+1)+
  nmat*(num_state_material+ngeom_raw)+1;
 int nden=nmat*num_state_material;
 int scomp_mofvars=num_materials_vel*(BL_SPACEDIM+1)+
  nmat*num_state_material;

 const Real* dx = geom.CellSize();

 Real problo[BL_SPACEDIM];
 Real probhi[BL_SPACEDIM];
 for (int dir=0;dir<BL_SPACEDIM;dir++) {
  problo[dir]=Geometry::ProbLo(dir);
  probhi[dir]=Geometry::ProbHi(dir);
 }

 int rz_flag=0;
 if (CoordSys::IsRZ())
  rz_flag=1;
 else if (CoordSys::IsCartesian())
  rz_flag=0;
 else if (CoordSys::IsCYLINDRICAL())
  rz_flag=3;
 else
  BoxLib::Error("CoordSys bust 1");

 MultiFab* presmf=getState(1,num_materials_vel*BL_SPACEDIM,
   num_materials_vel,cur_time_slab);
 MultiFab* pres_eos_mf=derive_EOS_pressure();
 if (pres_eos_mf->nGrow()!=1)
  BoxLib::Error("pres_eos_mf->nGrow()!=1");

 MultiFab* LSMF=getStateDist(2,cur_time_slab,6);
 MultiFab* EOSMF=getStateDen(1,cur_time_slab);
 if (EOSMF->nComp()!=nmat*num_state_material)
  BoxLib::Error("EOSMF invalid ncomp");
 MultiFab* VOFMF=getState(1,scomp_mofvars,nmat*ngeom_raw,cur_time_slab);

 MultiFab& LS_new = get_new_data(LS_Type,slab_step+1);
 if (LS_new.nComp()!=nmat*(BL_SPACEDIM+1)) 
  BoxLib::Error("LS_new invalid ncomp");
 MultiFab& S_new = get_new_data(State_Type,slab_step+1);
 if (nstate!=S_new.nComp())
  BoxLib::Error("nstate invalid");

 resize_maskfiner(1,MASKCOEF_MF);
 debug_ngrow(MASKCOEF_MF,1,6001);

 if ((prev_time_slab<0.0)||
     (cur_time_slab<=0.0)||
     (cur_time_slab<=prev_time_slab))
  BoxLib::Error("prev_time_slab or cur_time_slab invalid");

 int do_the_nucleate;
 Array<Real> nucleate_pos;
 nucleate_pos.resize(4);
 if (n_sites>0)
  nucleate_pos.resize(4*n_sites);

 do_the_nucleate=0;
 for (int i=0;i<nucleate_pos.size();i++) 
  nucleate_pos[i]=0.0;

 if (n_sites==0) {
  // do nothing
 } else if (n_sites>0) {

  int first_time_nucleate=0;
  if (nucleation_init_time==0.0) {
   if (prev_time_slab==0.0) 
    first_time_nucleate=1;
  } else if (nucleation_init_time>0.0) {
   if ((prev_time_slab<=nucleation_init_time)&&
       (cur_time_slab>nucleation_init_time)) 
    first_time_nucleate=1;
  } else
   BoxLib::Error("nucleation_init_time invalid");
 
  if (nucleation_period==0.0) {
   if (first_time_nucleate==1) {
    do_the_nucleate=1;
    for (int dir=0;dir<4*n_sites;dir++)
     nucleate_pos[dir]=pos_sites[dir]; 
   }
  } else if (nucleation_period>0.0) {
   if (first_time_nucleate==1) {
    do_the_nucleate=1;
    for (int dir=0;dir<4*n_sites;dir++)
     nucleate_pos[dir]=pos_sites[dir]; 
   } else if ((first_time_nucleate==0)&&
              (prev_time_slab>nucleation_init_time)) {
  
    if (level==finest_level) {
 
     int num_periods=0;
     Real mult_period=nucleation_init_time;

     while (mult_period<prev_time_slab) {
      num_periods++;
      mult_period=nucleation_init_time+ 
        num_periods*nucleation_period;
     }

     if (1==0) {
      std::cout << "num_periods= " << num_periods << '\n';
      std::cout << "nucleation_period= " << nucleation_period << '\n';
      std::cout << "prev_time_slab= " << prev_time_slab << '\n';
      std::cout << "cur_time_slab= " << cur_time_slab << '\n';
      std::cout << "mult_period= " << mult_period << '\n';
      std::cout << "nucleation_init_time= " << 
       nucleation_init_time << '\n';
     }

     if (mult_period<cur_time_slab) {

      do_the_nucleate=1;

      for (int nc=0;nc<n_sites;nc++) {

       double rr=pos_sites[nc*4+3];  // radius

       Array<Real> xnucleate(BL_SPACEDIM);
       for (int dir=0;dir<BL_SPACEDIM;dir++) 
        xnucleate[dir]=-1.0e+99;

       if (ParallelDescriptor::IOProcessor()) {
        for (int dir=0;dir<BL_SPACEDIM;dir++) {
         Real save_random=BoxLib::Random();
         if ((save_random<0.0)||(save_random>1.0)) {
          std::cout << "save_random invalid save_random= " << 
           save_random << '\n';
          BoxLib::Error("save_random bust");
         }
         
         xnucleate[dir]=problo[dir]+save_random*(probhi[dir]-problo[dir]);
         if ((rz_flag==1)&&(dir==0)) {
          xnucleate[dir]=0.0;
         } else if (dir==BL_SPACEDIM-1) {
          xnucleate[dir]=pos_sites[nc*4+dir];
         } else {       
          if (xnucleate[dir]-rr<problo[dir])
           xnucleate[dir]=problo[dir]+rr;
          if (xnucleate[dir]+rr>probhi[dir])
           xnucleate[dir]=probhi[dir]-rr;
         }
        } // dir
       } // io proc?

       for (int dir=0;dir<BL_SPACEDIM;dir++) 
        ParallelDescriptor::ReduceRealMax(xnucleate[dir]);

       for (int dir=0;dir<BL_SPACEDIM;dir++) 
        nucleate_pos[nc*4+dir]=xnucleate[dir];
       nucleate_pos[nc*4+3]=rr;  // radius

      } // nc=0..n_sites-1

     } // mult_period<cur_time_slab

    } else if ((level>=0)&&(level<finest_level)) {
     // do nothing
    } else
     BoxLib::Error("level invalid nucleate_bubbles 2");

   } else if (prev_time_slab<=nucleation_init_time) {
    // do nothing
   } else
    BoxLib::Error("prev_time_slab invalid");
  } else 
   BoxLib::Error("nucleation_period invalid");
 } else
  BoxLib::Error("n_sites invalid");

 Array< Array<Real> > delta_mass_local;
 delta_mass_local.resize(thread_class::nthreads);
 for (int tid=0;tid<thread_class::nthreads;tid++) {
  delta_mass_local[tid].resize(2*nmat); // source 1..nmat  dest 1..nmat
  for (int im=0;im<2*nmat;im++)
   delta_mass_local[tid][im]=0.0;
 } // tid

 debug_ngrow(MDOT_MF,0,355);

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(LS_new,use_tiling); mfi.isValid(); ++mfi) {
   BL_ASSERT(grids[mfi.index()] == mfi.validbox());
   const int gridno = mfi.index();
   const Box& tilegrid = mfi.tilebox();
   const Box& fabgrid = grids[gridno];
   const int* tilelo=tilegrid.loVect();
   const int* tilehi=tilegrid.hiVect();
   const int* fablo=fabgrid.loVect();
   const int* fabhi=fabgrid.hiVect();
   int bfact=parent->Space_blockingFactor(level);

   const Real* xlo = grid_loc[gridno].lo();
   Array<int> vofbc=getBCArray(State_Type,gridno,scomp_mofvars,1);
   FArrayBox& lsfab=(*LSMF)[mfi];
   FArrayBox& lsnewfab=LS_new[mfi];
   FArrayBox& eosfab=(*EOSMF)[mfi];
   FArrayBox& presfab=(*presmf)[mfi];
   FArrayBox& preseosfab=(*pres_eos_mf)[mfi];

   FArrayBox& snewfab=S_new[mfi];
   FArrayBox& voffab=(*VOFMF)[mfi]; 

    // mask=tag if not covered by level+1 or outside the domain.
   FArrayBox& maskcov=(*localMF[MASKCOEF_MF])[mfi];

   FArrayBox& mdotfab=(*localMF[MDOT_MF])[mfi];

   int nucleate_pos_size=nucleate_pos.size();

   int tid=ns_thread();

   FORT_NUCLEATE( 
    delta_mass_local[tid].dataPtr(),
    &custom_nucleation_model,
    &prev_time_slab,
    &cur_time_slab,
    &level,&finest_level,
    &nmat,&nten,&nden,&nstate,
    latent_heat.dataPtr(),
    saturation_temp.dataPtr(),
    &do_the_nucleate,
    nucleate_pos.dataPtr(),
    &nucleate_pos_size, 
    nucleation_temp.dataPtr(), 
    nucleation_pressure.dataPtr(), 
    nucleation_pmg.dataPtr(), 
    nucleation_mach.dataPtr(), 
    cavitation_pressure.dataPtr(), 
    cavitation_vapor_density.dataPtr(), 
    cavitation_tension.dataPtr(), 
    cavitation_species.dataPtr(), 
    cavitation_model.dataPtr(), 
    tilelo,tilehi,
    fablo,fabhi,&bfact,
    vofbc.dataPtr(),
    xlo,dx,&dt_slab,
    maskcov.dataPtr(),
    ARLIM(maskcov.loVect()),ARLIM(maskcov.hiVect()),
    lsfab.dataPtr(),ARLIM(lsfab.loVect()),ARLIM(lsfab.hiVect()),
    lsnewfab.dataPtr(),ARLIM(lsnewfab.loVect()),ARLIM(lsnewfab.hiVect()),
    voffab.dataPtr(),ARLIM(voffab.loVect()),ARLIM(voffab.hiVect()),
    snewfab.dataPtr(),ARLIM(snewfab.loVect()),ARLIM(snewfab.hiVect()),
    eosfab.dataPtr(),ARLIM(eosfab.loVect()),ARLIM(eosfab.hiVect()),
    mdotfab.dataPtr(),ARLIM(mdotfab.loVect()),ARLIM(mdotfab.hiVect()),
    presfab.dataPtr(),ARLIM(presfab.loVect()),ARLIM(presfab.hiVect()),
    preseosfab.dataPtr(),
    ARLIM(preseosfab.loVect()),ARLIM(preseosfab.hiVect()) );
 } // mfi
} // omp
 for (int tid=1;tid<thread_class::nthreads;tid++) {
  for (int im=0;im<2*nmat;im++) {
   delta_mass_local[0][im]+=delta_mass_local[tid][im];
  }
 } // tid

 ParallelDescriptor::Barrier();

 for (int im=0;im<2*nmat;im++) {
  ParallelDescriptor::ReduceRealSum(delta_mass_local[0][im]);
  delta_mass[0][im]+=delta_mass_local[0][im];
 }

 delete LSMF;
 delete presmf;
 delete pres_eos_mf;
 delete EOSMF;
 delete VOFMF;

}  // subroutine nucleate_bubbles

// 1. called if freezing_model==0,5.
// 2. multiphase_project->allocate_project_variables->stefan_solver_init
//    (adjust_temperature==1)
//    coeffMF==localMF[OUTER_ITER_PRESSURE_MF]
// 3. multiphase_project->allocate_maccoef->stefan_solver_init
//    update_SEM_forcesALL->allocate_maccoef->stefan_solver_init
//    (adjust_temperature==0)
//    coeffMF==localMF[ALPHANOVOLUME_MF]
// 4. multiphase_project->allocate_FACE_WEIGHT->stefan_solver_init
//    update_SEM_forcesALL->allocate_FACE_WEIGHT->stefan_solver_init
//    diffusion_heatingALL->allocate_FACE_WEIGHT->stefan_solver_init
//    (adjust_temperature==-1)
//    coeffMF==localMF[CELL_DEN_MF]
// if adjust_temperature==1,
//  Snew=(c1 Tn + c2 TSAT)/(c1+c2)
//  coeffMF=(c1 Tn + c2 TSAT)/(c1+c2)
//  c1=rho cv/(dt*sweptfactor)  c2=(1/vol) sum_face Aface k_m/(theta dx)
// else if adjust_temperature==0,
//  coeffMF=c1+c2
// else if adjust_temperature==-1,
//  faceheat_index component of FACE_VAR_MF
void
NavierStokes::stefan_solver_init(MultiFab* coeffMF,int adjust_temperature) {
 
 int finest_level=parent->finestLevel();

 bool use_tiling=ns_tiling;

 if (adjust_temperature==1) {
  // do nothing
 } else if (adjust_temperature==0) {
  // do nothing
 } else if (adjust_temperature==-1) {
  use_tiling=false;
 } else
  BoxLib::Error("adjust_temperature invalid");

 if (num_materials_vel!=1)
  BoxLib::Error("num_materials_vel invalid");

 int nmat=num_materials;
 int nten=( (nmat-1)*(nmat-1)+nmat-1 )/2;
 int nstate=num_materials_vel*(BL_SPACEDIM+1)+
  nmat*(num_state_material+ngeom_raw)+1;

 int nsolve=1;
 int nsolveMM=nsolve*num_materials_scalar_solve;

 resize_metrics(1);
 VOF_Recon_resize(1,SLOPE_RECON_MF);

 debug_ngrow(VOLUME_MF,1,34);
 debug_ngrow(SWEPT_CROSSING_MF,0,34);

 for (int dir=0;dir<BL_SPACEDIM;dir++)
  debug_ngrow(FACE_VAR_MF+dir,0,2);

 debug_ngrow(SLOPE_RECON_MF,1,31);
 if (localMF[SLOPE_RECON_MF]->nComp()!=nmat*ngeom_recon)
  BoxLib::Error("localMF[SLOPE_RECON_MF]->nComp() invalid");

 debug_ngrow(CELL_DEN_MF,1,28); 
 debug_ngrow(CELL_DEDT_MF,1,28); 

 if (localMF[CELL_DEN_MF]->nComp()!=nmat+1)
  BoxLib::Error("localMF[CELL_DEN_MF]->nComp() invalid");
 if (localMF[CELL_DEDT_MF]->nComp()!=nmat+1)
  BoxLib::Error("localMF[CELL_DEDT_MF]->nComp() invalid");


 if (dt_slab<=0.0)
  BoxLib::Error("dt_slab must be positive");
 if (num_state_base!=2)
  BoxLib::Error("num_state_base invalid");

 int GFM_flag=0;
 if (is_phasechange==1) {
  for (int im=0;im<2*nten;im++) {
   if (latent_heat[im]!=0.0)
    if ((freezing_model[im]==0)||
        (freezing_model[im]==5))
     GFM_flag=1;
  }
 } else if (is_phasechange==0) {
  BoxLib::Error("is_phasechange invalid in stefan_solver_init (1)");
 } else
  BoxLib::Error("is_phasechange invalid in stefan_solver_init (2)");

 if (GFM_flag!=1)
  BoxLib::Error("Gibou et al algorithm only used for GFM");

 const Real* dx = geom.CellSize();

 MultiFab* LSmf=getStateDist(1,cur_time_slab,7);  
 if (LSmf->nComp()!=nmat*(1+BL_SPACEDIM))
  BoxLib::Error("LSmf invalid ncomp");
 if (LSmf->nGrow()!=1)
  BoxLib::Error("LSmf->nGrow()!=1");

 MultiFab& S_new = get_new_data(State_Type,slab_step+1);
 if (S_new.nComp()!=nstate)
  BoxLib::Error("S_new invalid ncomp");

 int mm_areafrac_index=FACE_VAR_MF;
 int mm_cell_areafrac_index=SLOPE_RECON_MF;
 if (num_materials_scalar_solve==nmat) {
  mm_areafrac_index=FACEFRAC_SOLVE_MM_MF;
  mm_cell_areafrac_index=CELLFRAC_MM_MF;
 } else if (num_materials_scalar_solve==1) {
  // do nothing
 } else
  BoxLib::Error("num_materials_scalar_solve invalid");

 // (ml,mr,2) frac_pair(ml,mr), dist_pair(ml,mr)  
 int nfacefrac=nmat*nmat*2;
 // im_inside,im_outside,3+sdim -->
 //   area, dist_to_line, dist, line normal.
 int ncellfrac=nmat*nmat*(3+BL_SPACEDIM);

 for (int dir=0;dir<BL_SPACEDIM;dir++)
  debug_ngrow(mm_areafrac_index+dir,0,111);
 debug_ngrow(mm_cell_areafrac_index,0,113);

 int num_materials_combine=nmat;
 int project_option_thermal=2;
 int state_index;
 Array<int> scomp;
 Array<int> ncomp;
 int ncomp_check;
 get_mm_scomp_solver(
   num_materials_combine,
   project_option_thermal,
   state_index,
   scomp,ncomp,ncomp_check);

 if ((ncomp_check!=nmat)||(state_index!=State_Type))
  BoxLib::Error("(ncomp_check!=nmat)||(state_index!=State_Type)");

 MultiFab* thermal_list_mf=getState_list(1,scomp,ncomp,cur_time_slab);
 
#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(*LSmf,use_tiling); mfi.isValid(); ++mfi) {
   BL_ASSERT(grids[mfi.index()] == mfi.validbox());
   const int gridno = mfi.index();
   const Box& tilegrid = mfi.tilebox();
   const Box& fabgrid = grids[gridno];
   const int* tilelo=tilegrid.loVect();
   const int* tilehi=tilegrid.hiVect();
   const int* fablo=fabgrid.loVect();
   const int* fabhi=fabgrid.hiVect();
   int bfact=parent->Space_blockingFactor(level);

   const Real* xlo = grid_loc[gridno].lo();

   FArrayBox& lsfab=(*LSmf)[mfi];
   FArrayBox& thermalfab=(*thermal_list_mf)[mfi];

   FArrayBox& snewfab=S_new[mfi];
   FArrayBox& DeDTfab=(*localMF[CELL_DEDT_MF])[mfi];  // 1/(rho cv)
   FArrayBox& denfab=(*localMF[CELL_DEN_MF])[mfi];  // 1/rho
   FArrayBox& coefffab=(*coeffMF)[mfi];  // either TN or alphanovolume

   if ((adjust_temperature==0)||
       (adjust_temperature==1)) {
    if (coefffab.nComp()!=nsolveMM) {
     std::cout << "coefffab.nComp()= " << coefffab.nComp() << '\n';
     std::cout << "nsolveMM= " << nsolveMM << '\n';
     std::cout << "adjust_temperature= " << adjust_temperature << '\n';
     BoxLib::Error("coefffab.nComp() invalid");
    }
   } else if (adjust_temperature==-1) {
    if (coefffab.nComp()<1) {
     std::cout << "coefffab.nComp()= " << coefffab.nComp() << '\n';
     std::cout << "nsolveMM= " << nsolveMM << '\n';
     std::cout << "adjust_temperature= " << adjust_temperature << '\n';
     BoxLib::Error("coefffab.nComp() invalid");
    }
   } else
    BoxLib::Error("adjust_temperature invalid");

   FArrayBox& heatx=(*localMF[FACE_VAR_MF])[mfi];
   FArrayBox& heaty=(*localMF[FACE_VAR_MF+1])[mfi];
   FArrayBox& heatz=(*localMF[FACE_VAR_MF+BL_SPACEDIM-1])[mfi];
   FArrayBox& areax=(*localMF[AREA_MF])[mfi];
   FArrayBox& areay=(*localMF[AREA_MF+1])[mfi];
   FArrayBox& areaz=(*localMF[AREA_MF+BL_SPACEDIM-1])[mfi];
   FArrayBox& volfab=(*localMF[VOLUME_MF])[mfi];
   FArrayBox& sweptfab = (*localMF[SWEPT_CROSSING_MF])[mfi];

   FArrayBox& xfacemm=(*localMF[mm_areafrac_index])[mfi];
   FArrayBox& yfacemm=(*localMF[mm_areafrac_index+1])[mfi];
   FArrayBox& zfacemm=(*localMF[mm_areafrac_index+BL_SPACEDIM-1])[mfi];
   FArrayBox& cellfracmm=(*localMF[mm_cell_areafrac_index])[mfi];

   FORT_STEFANSOLVER( 
    &solidheat_flag, //0=diffuse in solid 1=dirichlet 2=Neumann
    microlayer_size.dataPtr(), 
    microlayer_substrate.dataPtr(), 
    microlayer_temperature_substrate.dataPtr(), 
    &adjust_temperature,
    &nmat,&nten,&nstate,
    latent_heat.dataPtr(),
    freezing_model.dataPtr(),
    distribute_from_target.dataPtr(),
    saturation_temp.dataPtr(),
    tilelo,tilehi,
    fablo,fabhi,&bfact,
    &level,
    &finest_level,
    &nfacefrac,
    &ncellfrac,
    xlo,dx,
    &dt_slab,
    cellfracmm.dataPtr(),
    ARLIM(cellfracmm.loVect()),ARLIM(cellfracmm.hiVect()),
    xfacemm.dataPtr(),ARLIM(xfacemm.loVect()),ARLIM(xfacemm.hiVect()),
    yfacemm.dataPtr(),ARLIM(yfacemm.loVect()),ARLIM(yfacemm.hiVect()),
    zfacemm.dataPtr(),ARLIM(zfacemm.loVect()),ARLIM(zfacemm.hiVect()),
    sweptfab.dataPtr(),ARLIM(sweptfab.loVect()),ARLIM(sweptfab.hiVect()),
    lsfab.dataPtr(),ARLIM(lsfab.loVect()),ARLIM(lsfab.hiVect()),
    thermalfab.dataPtr(),
    ARLIM(thermalfab.loVect()),ARLIM(thermalfab.hiVect()),
    snewfab.dataPtr(),ARLIM(snewfab.loVect()),ARLIM(snewfab.hiVect()),
    DeDTfab.dataPtr(),ARLIM(DeDTfab.loVect()),ARLIM(DeDTfab.hiVect()),
    denfab.dataPtr(),
    ARLIM(denfab.loVect()),ARLIM(denfab.hiVect()),
    coefffab.dataPtr(),ARLIM(coefffab.loVect()),ARLIM(coefffab.hiVect()),
    volfab.dataPtr(),ARLIM(volfab.loVect()),ARLIM(volfab.hiVect()),
    heatx.dataPtr(faceheat_index),ARLIM(heatx.loVect()),ARLIM(heatx.hiVect()),
    heaty.dataPtr(faceheat_index),ARLIM(heaty.loVect()),ARLIM(heaty.hiVect()),
    heatz.dataPtr(faceheat_index),ARLIM(heatz.loVect()),ARLIM(heatz.hiVect()),
    areax.dataPtr(),ARLIM(areax.loVect()),ARLIM(areax.hiVect()),
    areay.dataPtr(),ARLIM(areay.loVect()),ARLIM(areay.hiVect()),
    areaz.dataPtr(),ARLIM(areaz.loVect()),ARLIM(areaz.hiVect()) );
 } // mfi
} // omp
 ParallelDescriptor::Barrier();

 delete LSmf;
 delete thermal_list_mf;
 
}  // stefan_solver_init

// T^new=T^* += dt A Q/(rho cv V) 
void
NavierStokes::heat_source_term() {
 
 bool use_tiling=ns_tiling;

 resize_metrics(1);

 debug_ngrow(VOLUME_MF,1,34);

 for (int dir=0;dir<BL_SPACEDIM;dir++)
  debug_ngrow(FACE_VAR_MF+dir,0,2);

 if (dt_slab<=0.0)
  BoxLib::Error("dt_slab must be positive");
 if (num_state_base!=2)
  BoxLib::Error("num_state_base invalid");

 int nmat=num_materials;
 int nten=( (nmat-1)*(nmat-1)+nmat-1 )/2;
 int nstate=num_materials_vel*(BL_SPACEDIM+1)+
  nmat*(num_state_material+ngeom_raw)+1;

 const Real* dx = geom.CellSize();

 MultiFab* LSmf=getStateDist(1,cur_time_slab,8);  
 if (LSmf->nComp()!=nmat*(1+BL_SPACEDIM))
  BoxLib::Error("LSmf invalid ncomp");
 if (LSmf->nGrow()!=1)
  BoxLib::Error("LSmf->nGrow()!=1");

 MultiFab& S_new = get_new_data(State_Type,slab_step+1);
 if (S_new.nComp()!=nstate)
  BoxLib::Error("S_new invalid ncomp");

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(*LSmf,use_tiling); mfi.isValid(); ++mfi) {
   BL_ASSERT(grids[mfi.index()] == mfi.validbox());
   const int gridno = mfi.index();
   const Box& tilegrid = mfi.tilebox();
   const Box& fabgrid = grids[gridno];
   const int* tilelo=tilegrid.loVect();
   const int* tilehi=tilegrid.hiVect();
   const int* fablo=fabgrid.loVect();
   const int* fabhi=fabgrid.hiVect();
   int bfact=parent->Space_blockingFactor(level);

   const Real* xlo = grid_loc[gridno].lo();
   FArrayBox& lsfab=(*LSmf)[mfi];
   FArrayBox& snewfab=S_new[mfi];
   FArrayBox& DeDTfab=(*localMF[CELL_DEDT_MF])[mfi];  // 1/(rho cv)
   if (DeDTfab.nComp()!=nmat+1)
    BoxLib::Error("DeDTfab.nComp() invalid");

   FArrayBox& denfab=(*localMF[CELL_DEN_MF])[mfi];  // 1/rho
   if (denfab.nComp()!=nmat+1)
    BoxLib::Error("denfab.nComp() invalid");

   FArrayBox& heatx=(*localMF[FACE_VAR_MF])[mfi];
   FArrayBox& heaty=(*localMF[FACE_VAR_MF+1])[mfi];
   FArrayBox& heatz=(*localMF[FACE_VAR_MF+BL_SPACEDIM-1])[mfi];
   FArrayBox& areax=(*localMF[AREA_MF])[mfi];
   FArrayBox& areay=(*localMF[AREA_MF+1])[mfi];
   FArrayBox& areaz=(*localMF[AREA_MF+BL_SPACEDIM-1])[mfi];
   FArrayBox& volfab=(*localMF[VOLUME_MF])[mfi];

   FORT_HEATSOURCE_FACE( 
    &nmat,&nten,&nstate,
    latent_heat.dataPtr(),
    saturation_temp.dataPtr(),
    tilelo,tilehi,
    fablo,fabhi,&bfact,
    xlo,dx,
    &dt_slab,
    lsfab.dataPtr(),ARLIM(lsfab.loVect()),ARLIM(lsfab.hiVect()),
    snewfab.dataPtr(),ARLIM(snewfab.loVect()),ARLIM(snewfab.hiVect()),
    DeDTfab.dataPtr(),ARLIM(DeDTfab.loVect()),ARLIM(DeDTfab.hiVect()),
    denfab.dataPtr(),
    ARLIM(denfab.loVect()),ARLIM(denfab.hiVect()),
    volfab.dataPtr(),ARLIM(volfab.loVect()),ARLIM(volfab.hiVect()),
    heatx.dataPtr(faceheat_index),ARLIM(heatx.loVect()),ARLIM(heatx.hiVect()),
    heaty.dataPtr(faceheat_index),ARLIM(heaty.loVect()),ARLIM(heaty.hiVect()),
    heatz.dataPtr(faceheat_index),ARLIM(heatz.loVect()),ARLIM(heatz.hiVect()),
    areax.dataPtr(),ARLIM(areax.loVect()),ARLIM(areax.hiVect()),
    areay.dataPtr(),ARLIM(areay.loVect()),ARLIM(areay.hiVect()),
    areaz.dataPtr(),ARLIM(areaz.loVect()),ARLIM(areaz.hiVect()) );
 } // mfi
} // omp
 ParallelDescriptor::Barrier();

 delete LSmf;
 
}  // subroutine heat_source_term

void NavierStokes::show_norm2_id(int mf_id,int id) {

 if (show_norm2_flag==1) {
  int finest_level=parent->finestLevel();
  for (int ilev=finest_level;ilev>=level;ilev--) {
   NavierStokes& ns_level=getLevel(ilev);
   MultiFab* mf=ns_level.localMF[mf_id];
   int scomp=0;
   int ncomp=mf->nComp();
   show_norm2(mf,scomp,ncomp,id);
  } // ilev
 } else if (show_norm2_flag==0) {
  // do nothing
 } else
  BoxLib::Error("show_norm2_flag invalid");
 
} // subroutine show_norm2_id

void NavierStokes::show_norm2(MultiFab* mf,int scomp,int ncomp,int id) {

 if (show_norm2_flag==1) {
  for (int nc=0;nc<ncomp;nc++) {
   Real nm2=MultiFab::Dot(*mf,scomp+nc,*mf,scomp+nc,1,0,0);
   std::cout << "\n norm2 id= " << id << "scomp+nc= " << 
    scomp+nc << " nm2= " << nm2 << '\n';
  } // nc
 } else if (show_norm2_flag==0) {
  // do nothing
 } else
  BoxLib::Error("show_norm2_flag invalid");
  
} // subroutine show_norm2

// datatype=0 scalar or vector
// datatype=1 tensor face
// datatype=2 tensor cell
void NavierStokes::aggressive_debug(
  int datatype,
  int force_check,
  MultiFab* mf,
  int scomp,int ncomp,
  int ngrow,
  int dir,int id,
  Real warning_cutoff) {

 if (((verbose==0)||(verbose==1))&&(force_check==0)) {
  // do nothing
 } else if ((verbose==2)||(force_check==1)) {
  std::fflush(NULL);
  int finest_level=parent->finestLevel();
  const Real* dx = geom.CellSize();
  int ndefined=mf->nComp();
  if (ndefined<scomp+ncomp) 
   BoxLib::Error("scomp,ncomp invalid in aggressive debug");
  if (mf->nGrow()<ngrow)
   BoxLib::Error("ngrow invalid in aggressive debug");

  if (verbose==2) {
   std::cout << "AGGRESSIVE DEBUG scomp= " << scomp << '\n';
   std::cout << "AGGRESSIVE DEBUG ncomp= " << ncomp << '\n';
   std::cout << "AGGRESSIVE DEBUG ngrow= " << ngrow << '\n';
   std::cout << "AGGRESSIVE DEBUG dir= " << dir << '\n';
   std::cout << "AGGRESSIVE DEBUG id= " << id << '\n';
  }

  const BoxArray mfBA=mf->boxArray();
  int ngrid=mfBA.size();

#ifdef _OPENMP
#pragma omp parallel
#endif
{
  for (MFIter mfi(*mf); mfi.isValid(); ++mfi) {
   const int gridno = mfi.index();
   const Box& tilegrid = mfi.tilebox();
   const Box& growntilegrid = mfi.growntileboxTENSOR(datatype,ngrow,dir);
   const Box& fabgrid = mfi.validbox();

   if (fabgrid!=mfBA[gridno])
    BoxLib::Error("fabgrid!=mfBA[gridno]");

   const int* tilelo=tilegrid.loVect();
   const int* tilehi=tilegrid.hiVect();
   const int* growlo=growntilegrid.loVect();
   const int* growhi=growntilegrid.hiVect();
   const int* fablo=fabgrid.loVect();
   const int* fabhi=fabgrid.hiVect();
   int bfact=parent->Space_blockingFactor(level);

   FArrayBox& mffab=(*mf)[mfi];
   FORT_AGGRESSIVE(
    &datatype,
    &warning_cutoff,
    tilelo,tilehi,
    fablo,fabhi,
    growlo,growhi,
    &bfact,
    dx,
    &scomp,
    &ncomp,
    &ndefined,
    &ngrow,&dir,&id,
    &verbose,
    &force_check,
    &gridno,&ngrid,&level,&finest_level,
    mffab.dataPtr(),ARLIM(mffab.loVect()),ARLIM(mffab.hiVect()));
  } // mfi
} // omp
  ParallelDescriptor::Barrier();
  std::fflush(NULL);
 } else
  BoxLib::Error("verbose or force_check invalid");

} // subroutine aggressive_debug

void
NavierStokes::synchronize_flux_register(int operation_flag,
 int spectral_loop) {

 if ((operation_flag<0)||(operation_flag>11))
  BoxLib::Error("operation_flag invalid2");
 
 if (spectral_loop+1==end_spectral_loop()) {
  delete_localMF(SEM_FLUXREG_MF,1);
 } else if (spectral_loop==0) {
  localMF[SEM_FLUXREG_MF]->FillBoundary(geom.periodicity());
 } else
  BoxLib::Error("spectral_loop invalid");

} // subroutine synchronize_flux_register

void 
NavierStokes::allocate_flux_register(int operation_flag) {

 int ncfluxreg=0;

  // unew^{f} = 
  //   (i) unew^{f} in incompressible non-solid regions
  //   (ii) u^{f,save} + (unew^{c}-u^{c,save})^{c->f} in spectral regions or
  //        compressible regions.
  //   (iii) usolid in solid regions
 if (operation_flag==11) {
  ncfluxreg=BL_SPACEDIM;
 } else if (operation_flag==10) { // ucell,umac -> umac
  ncfluxreg=BL_SPACEDIM;
 } else if (operation_flag==9) {  // density CELL -> MAC
  ncfluxreg=BL_SPACEDIM;
 } else if (operation_flag==7) {  // advection
  ncfluxreg=BL_SPACEDIM*nfluxSEM;
 } else if (operation_flag==1) { // interp press from cell to MAC.
  ncfluxreg=BL_SPACEDIM;
 } else if (operation_flag==2) { // ppot cell-> mac  grad ppot
  ncfluxreg=2*BL_SPACEDIM; // (grad ppot)_mac, ppot_mac
 } else if (operation_flag==3) { // ucell -> umac
  ncfluxreg=BL_SPACEDIM;
 } else if (operation_flag==4) { // umac -> umac
  ncfluxreg=BL_SPACEDIM;
 } else if (operation_flag==5) { // umac -> umac+beta F^cell->mac
  ncfluxreg=BL_SPACEDIM;
 } else if (operation_flag==6) { // grad U
  ncfluxreg=BL_SPACEDIM*BL_SPACEDIM;
 } else if (operation_flag==0) { // grad p
  ncfluxreg=BL_SPACEDIM;
 } else if (operation_flag==8) { // grad U (for crossterm)
  ncfluxreg=BL_SPACEDIM*BL_SPACEDIM;
 } else
  BoxLib::Error("operation_flag invalid3");

 new_localMF(SEM_FLUXREG_MF,ncfluxreg,1,-1);
 setVal_localMF(SEM_FLUXREG_MF,0.0,0,ncfluxreg,1); 

} // subroutine allocate_flux_register

int 
NavierStokes::end_spectral_loop() {

 int finest_level=parent->finestLevel();
 if ((level<0)||(level>finest_level))
  BoxLib::Error("level invalid end_spectral_loop");

 int local_end=1;
 int bfact=parent->Space_blockingFactor(level);

 if ((enable_spectral==0)||(enable_spectral==3)) {
  local_end=1;
 } else if ((enable_spectral==1)||(enable_spectral==2)) {
  if (bfact==1)
   local_end=1;
  else if (bfact>=2)
   local_end=2;
  else
   BoxLib::Error("bfact invalid");
 } else
  BoxLib::Error("enable_spectral invalid end_spectral_loop() ");

 return local_end;
 
} // end_spectral_loop

// called after NavierStokes::nonlinear_advection()
// called from  NavierStokes::SEM_advectALL
// for advect_iter=0 ... 1 (source_term==0)
//
//  AMRSYNC_PRES_MF+dir, 
//  CONSERVE_FLUXES_MF+dir, 
//  COARSE_FINE_FLUX_MF+dir init to 1.0E+40
//
//  for spectral_loop=0 ... 1
//  for tileloop=0..1
//  for ilev=finest_level ... level
//   call this routine.
//
//  Average down CONSERVE_FLUXES_MF
//
//  init_fluxes=0
//  for ilev=finest_level ... level
//   call this routine.
// end advect_iter loop
//
// spectral_loop==0 => fine data transferred to coarse in a special way
// spectral_loop==1 => coarse fluxes interpolated to fine level.
//
void 
NavierStokes::SEM_scalar_advection(int init_fluxes,int source_term,
  int spectral_loop,int tileloop) {

 int finest_level=parent->finestLevel();
 
 bool use_tiling=ns_tiling;

 int num_colors=0;
 Array<Real> blob_array;
 blob_array.resize(1);
 int blob_array_size=blob_array.size();

 if (nfluxSEM!=BL_SPACEDIM+num_state_base)
  BoxLib::Error("nfluxSEM!=BL_SPACEDIM+num_state_base");

 if ((SDC_outer_sweeps>=0)&&(SDC_outer_sweeps<ns_time_order)) {
  // do nothing
 } else
  BoxLib::Error("SDC_outer_sweeps invalid");

 if (source_term==1) {

  if (advect_iter!=0)
   BoxLib::Error("advect_iter invalid");

 } else if (source_term==0) {

  // do nothing
  
 } else
  BoxLib::Error("source_term invalid");

 int nmat=num_materials;

 int nsolveMM_FACE=num_materials_vel;

 if (num_materials_vel!=1)
  BoxLib::Error("num_materials_vel invalid");

 if ((ns_time_order==1)&&
     (enable_spectral==3))
  BoxLib::Error("(ns_time_order==1)&&(enable_spectral==3)");
 if ((ns_time_order>=2)&&
     ((enable_spectral==0)||
      (enable_spectral==2)))
  BoxLib::Error("(ns_time_order>=2)&&(enable_spectral==0 or 2)");

 if ((enable_spectral==1)||
     (enable_spectral==2)||
     (ns_time_order>=2)) {

  int nparts=im_solid_map.size();
  if ((nparts<0)||(nparts>=nmat))
   BoxLib::Error("nparts invalid");
  Array<int> im_solid_map_null;
  im_solid_map_null.resize(1);

  int* im_solid_map_ptr;
  int nparts_def=nparts;
  if (nparts==0) {
   im_solid_map_ptr=im_solid_map_null.dataPtr();
   nparts_def=1;
  } else if ((nparts>=1)&&(nparts<=nmat-1)) {
   im_solid_map_ptr=im_solid_map.dataPtr();
  } else
   BoxLib::Error("nparts invalid");

  if (localMF[FSI_GHOST_MF]->nGrow()!=1)
   BoxLib::Error("localMF[FSI_GHOST_MF]->nGrow()!=1");
  if (localMF[FSI_GHOST_MF]->nComp()!=nparts_def*BL_SPACEDIM)
   BoxLib::Error("localMF[FSI_GHOST_MF]->nComp()!=nparts_def*BL_SPACEDIM");

  if (localMF[LEVELPC_MF]->nComp()!=nmat*(1+BL_SPACEDIM))
   BoxLib::Error("localMF[LEVELPC_MF]->nComp()!=nmat*(1+BL_SPACEDIM)");

  debug_ngrow(LEVELPC_MF,1,37);
  debug_ngrow(SLOPE_RECON_MF,1,37);
  debug_ngrow(delta_MF,0,37);
  debug_ngrow(MASKCOEF_MF,1,36);

  int nten=( (nmat-1)*(nmat-1)+nmat-1 )/2;
  int nstate=num_materials_vel*(BL_SPACEDIM+1)+
   nmat*(num_state_material+ngeom_raw)+1;

  MultiFab& S_new_test=get_new_data(State_Type,slab_step+1);
  if (S_new_test.nComp()!=nstate)
   BoxLib::Error("nstate invalid");

  int bfact=parent->Space_blockingFactor(level);
  int bfact_c=bfact;
  int bfact_f=bfact;
  if (level>0)
   bfact_c=parent->Space_blockingFactor(level-1);
  if (level<finest_level)
   bfact_f=parent->Space_blockingFactor(level+1);

  int project_option_visc=3;

  int fluxvel_index=0;
  int fluxden_index=BL_SPACEDIM;

  const Real* dx = geom.CellSize();
  const Box& domain = geom.Domain();
  const int* domlo = domain.loVect(); 
  const int* domhi = domain.hiVect();

  int rzflag=0;
  if (CoordSys::IsRZ())
   rzflag=1;
  else if (CoordSys::IsCartesian())
   rzflag=0;
  else if (CoordSys::IsCYLINDRICAL())
   rzflag=3;
  else
   BoxLib::Error("CoordSys bust 20");

  if (init_fluxes==1) {

   int operation_flag=7;
   if (localMF[SEM_FLUXREG_MF]->nComp()!=BL_SPACEDIM*nfluxSEM)
    BoxLib::Error("localMF[SEM_FLUXREG_MF]->nComp() invalid8");

   int mm_areafrac_index=CONSERVE_FLUXES_MF;
   int mm_cell_areafrac_index=SLOPE_RECON_MF;
    //(ml,mr,2) frac_pair(ml,mr),dist_pair(ml,mr)
   int nfacefrac=nmat*nmat*2; 
    // im_inside,im_outside,3+sdim -->
    //   area, dist_to_line, dist, line normal.
   int ncellfrac=nmat*nmat*(3+BL_SPACEDIM);

   for (int dirloc=0;dirloc<BL_SPACEDIM;dirloc++)
    debug_ngrow(mm_areafrac_index+dirloc,0,111);
   debug_ngrow(mm_cell_areafrac_index,0,113);

// flux variables: average down in the tangential direction 
// to the box face, copy in
// the normal direction.  Since the blocking factor is >=2, it is
// impossible to have a grid box with size of 1 cell width.
   if ((spectral_loop==0)&&(tileloop==0)) {

    if (level<finest_level) {
     avgDown_and_Copy_localMF(
      DEN_RECON_MF,
      VELADVECT_MF,
      AMRSYNC_PRES_MF,
      operation_flag);
    } else if (level==finest_level) {
     // do nothing
    } else
     BoxLib::Error("level invalid21");

    if ((level>=1)&&(level<=finest_level)) {
     interp_and_Copy_localMF(
      DEN_RECON_MF,
      VELADVECT_MF,
      AMRSYNC_PRES_MF,
      operation_flag);
    } else if (level==0) {
     // do nothing
    } else
     BoxLib::Error("level invalid22");

   } else if ((spectral_loop==1)&&(tileloop==0)) {

     // interpolate CONSERVE_FLUXES from the coarse level to the fine level:
     // COARSE_FINE_FLUX
     // Since spectral_loop==1 and the fluxes of interest are not "shared",
     // it means that CONSERVE_FLUXES has already been init during the
     // spectral_loop==0 sweep.
    if ((level>=1)&&(level<=finest_level)) {
     interp_flux_localMF(
      CONSERVE_FLUXES_MF,
      COARSE_FINE_FLUX_MF);
    } else if (level==0) {
     // do nothing
    } else
     BoxLib::Error("level invalid22");

   } else if ((spectral_loop==0)&&(tileloop==1)) {
    // do nothing
   } else if ((spectral_loop==1)&&(tileloop==1)) {
    // do nothing
   } else
    BoxLib::Error("spectral_loop or tileloop invalid");

   for (int dir=0;dir<BL_SPACEDIM;dir++) {

    debug_ngrow(UMAC_MF+dir,0,113);
  
#ifdef _OPENMP
#pragma omp parallel
#endif
{
    for (MFIter mfi(*localMF[DEN_RECON_MF],use_tiling); 
         mfi.isValid(); ++mfi) {
     BL_ASSERT(grids[mfi.index()] == mfi.validbox());
     int gridno=mfi.index();
     const Box& tilegrid = mfi.tilebox();
     const Box& fabgrid = grids[gridno];
     const int* tilelo=tilegrid.loVect();
     const int* tilehi=tilegrid.hiVect();
     const int* fablo=fabgrid.loVect();
     const int* fabhi=fabgrid.hiVect();

     const Real* xlo = grid_loc[gridno].lo();

     FArrayBox& xface=(*localMF[CONSERVE_FLUXES_MF+dir])[mfi];  
     FArrayBox& xp=(*localMF[AMRSYNC_PRES_MF+dir])[mfi];  
     FArrayBox& xgp=(*localMF[COARSE_FINE_FLUX_MF+dir])[mfi];  

     FArrayBox& xfacemm=(*localMF[mm_areafrac_index+dir])[mfi];  
     FArrayBox& xcellmm=(*localMF[mm_cell_areafrac_index])[mfi];  

     FArrayBox& reconfab=(*localMF[SLOPE_RECON_MF])[mfi];  

     FArrayBox& xvel=(*localMF[UMAC_MF+dir])[mfi];  

     FArrayBox& velfab=(*localMF[VELADVECT_MF])[mfi];
     FArrayBox& denfab=(*localMF[DEN_RECON_MF])[mfi];

     FArrayBox& solfab=(*localMF[FSI_GHOST_MF])[mfi];
     FArrayBox& levelpcfab=(*localMF[LEVELPC_MF])[mfi];

     FArrayBox& maskfab=(*localMF[MASK_NBR_MF])[mfi];

     // mask=tag if not covered by level+1 or outside the domain.
     FArrayBox& maskcoeffab=(*localMF[MASKCOEF_MF])[mfi];

     FArrayBox& semfluxfab=(*localMF[SEM_FLUXREG_MF])[mfi];
     int ncfluxreg=semfluxfab.nComp();

     FArrayBox& maskSEMfab=(*localMF[MASKSEM_MF])[mfi];

     Array<int> velbc=getBCArray(State_Type,gridno,0,BL_SPACEDIM);
     int dcomp=num_materials_vel*(BL_SPACEDIM+1);
     Array<int> denbc=getBCArray(State_Type,gridno,dcomp,
      nmat*num_state_material);

     int energyflag=0;
     int local_enable_spectral=enable_spectral;
     int num_materials_face=1;
     int simple_AMR_BC_flag=0;
     int ncomp_xp=nfluxSEM;
     int ncomp_xgp=nfluxSEM;

     // in SEM_scalar_advection
     // advect: rho u, rho, temperature (non conservatively)
     FORT_CELL_TO_MAC(
      &ncomp_xp,
      &ncomp_xgp,
      &simple_AMR_BC_flag,
      &nsolveMM_FACE,
      &num_materials_face,
      &tileloop,
      &dir,
      &operation_flag, // 7
      &energyflag,
      &visc_coef, //beta
      &visc_coef,
      &face_flag,
      temperature_primitive_variable.dataPtr(),
      &local_enable_spectral,
      &fluxvel_index,
      &fluxden_index,
      &facevel_index,
      &facecut_index,
      &icefacecut_index,
      &curv_index,
      &conservative_tension_force,
      &pforce_index,
      &faceden_index,
      &icemask_index,
      &massface_index,
      &vofface_index,
      &nfluxSEM, // ncphys (nflux for advection)
      &make_interface_incomp,
      override_density.dataPtr(),
      &solvability_projection,
      denbc.dataPtr(),  // presbc
      velbc.dataPtr(),  
      &slab_step,
      &dt_slab,  // CELL_TO_MAC
      &prev_time_slab, 
      xlo,dx,
      &spectral_loop,
      &ncfluxreg,
      semfluxfab.dataPtr(),
      ARLIM(semfluxfab.loVect()),ARLIM(semfluxfab.hiVect()),
      maskfab.dataPtr(), // mask=1.0 fine/fine   0.0=coarse/fine
      ARLIM(maskfab.loVect()),ARLIM(maskfab.hiVect()),
      maskcoeffab.dataPtr(), // maskcoef: 1=not covered 0=covered
      ARLIM(maskcoeffab.loVect()),ARLIM(maskcoeffab.hiVect()),
      maskSEMfab.dataPtr(),
      ARLIM(maskSEMfab.loVect()),ARLIM(maskSEMfab.hiVect()),
      levelpcfab.dataPtr(),
      ARLIM(levelpcfab.loVect()),ARLIM(levelpcfab.hiVect()),
      solfab.dataPtr(),
      ARLIM(solfab.loVect()),ARLIM(solfab.hiVect()),
      xface.dataPtr(),ARLIM(xface.loVect()),ARLIM(xface.hiVect()), //xcut
      xface.dataPtr(),ARLIM(xface.loVect()),ARLIM(xface.hiVect()), // xflux
      xfacemm.dataPtr(),ARLIM(xfacemm.loVect()),ARLIM(xfacemm.hiVect()),
      xcellmm.dataPtr(),ARLIM(xcellmm.loVect()),ARLIM(xcellmm.hiVect()),
      reconfab.dataPtr(),ARLIM(reconfab.loVect()),ARLIM(reconfab.hiVect()),
      xgp.dataPtr(),ARLIM(xgp.loVect()),ARLIM(xgp.hiVect()),//holds COARSE_FINE
      xp.dataPtr(),ARLIM(xp.loVect()),ARLIM(xp.hiVect()),//holds AMRSYNC_PRES
      xvel.dataPtr(),ARLIM(xvel.loVect()),ARLIM(xvel.hiVect()), 
      velfab.dataPtr(),
      ARLIM(velfab.loVect()),ARLIM(velfab.hiVect()),
      denfab.dataPtr(),  // pres
      ARLIM(denfab.loVect()),ARLIM(denfab.hiVect()),
      denfab.dataPtr(),  // den
      ARLIM(denfab.loVect()),ARLIM(denfab.hiVect()),
      denfab.dataPtr(),  // mgoni 
      ARLIM(denfab.loVect()),ARLIM(denfab.hiVect()),
      denfab.dataPtr(),  // color
      ARLIM(denfab.loVect()),ARLIM(denfab.hiVect()),
      denfab.dataPtr(),  // type
      ARLIM(denfab.loVect()),ARLIM(denfab.hiVect()),
      tilelo,tilehi,
      fablo,fabhi,
      &bfact,&bfact_c,&bfact_f, 
      &level,&finest_level,
      &rzflag,domlo,domhi, 
      &nmat,
      &nparts,
      &nparts_def,
      im_solid_map_ptr,
      prescribed_solid_scale.dataPtr(),
      added_weight.dataPtr(),
      blob_array.dataPtr(),
      &blob_array_size,
      &num_elements_blobclass,
      &num_colors,
      &nten,
      &nfacefrac,
      &ncellfrac,
      &project_option_visc,
      &SEM_upwind,
      &SEM_advection_algorithm);
    }   // mfi
} // omp
    ParallelDescriptor::Barrier();
   } // dir=0..sdim-1

  } else if (init_fluxes==0) {

   if (bfact>=1) {

    debug_ngrow(UMAC_MF,0,113);
    debug_ngrow(UMAC_MF+1,0,113);
    debug_ngrow(UMAC_MF+BL_SPACEDIM-1,0,113);

    MultiFab& S_new=get_new_data(State_Type,slab_step+1);
    MultiFab& S_old=get_new_data(State_Type,slab_step);

    MultiFab* rhs=new MultiFab(grids,nfluxSEM,0,dmap,Fab_allocate);

#ifdef _OPENMP
#pragma omp parallel
#endif
{
    for (MFIter mfi(S_new,use_tiling); mfi.isValid(); ++mfi) {
     BL_ASSERT(grids[mfi.index()] == mfi.validbox());
     const int gridno = mfi.index();
     const Box& tilegrid = mfi.tilebox();
     const Box& fabgrid = grids[gridno];
     const int* tilelo=tilegrid.loVect();
     const int* tilehi=tilegrid.hiVect();
     const int* fablo=fabgrid.loVect();
     const int* fabhi=fabgrid.hiVect();

     const Real* xlo = grid_loc[gridno].lo();

     FArrayBox& ax = (*localMF[AREA_MF])[mfi];
     FArrayBox& ay = (*localMF[AREA_MF+1])[mfi];
     FArrayBox& az = (*localMF[AREA_MF+BL_SPACEDIM-1])[mfi];

      // in: SEM_scalar_advection
     FArrayBox& xface=(*localMF[CONSERVE_FLUXES_MF])[mfi];  
     FArrayBox& yface=(*localMF[CONSERVE_FLUXES_MF+1])[mfi];  
     FArrayBox& zface=(*localMF[CONSERVE_FLUXES_MF+BL_SPACEDIM-1])[mfi];  

     FArrayBox& xvel=(*localMF[UMAC_MF])[mfi];  
     FArrayBox& yvel=(*localMF[UMAC_MF+1])[mfi];  
     FArrayBox& zvel=(*localMF[UMAC_MF+BL_SPACEDIM-1])[mfi];  

     FArrayBox& vol = (*localMF[VOLUME_MF])[mfi];

     // mask=1 if fine/fine
     FArrayBox& maskfab=(*localMF[MASK_NBR_MF])[mfi];
     // mask=tag if not covered by level+1 or outside the domain.
     FArrayBox& maskcoeffab=(*localMF[MASKCOEF_MF])[mfi];

     FArrayBox& maskSEMfab = (*localMF[MASKSEM_MF])[mfi];
     FArrayBox& rhsfab = (*rhs)[mfi];
     FArrayBox& snewfab = S_new[mfi];
     FArrayBox& S_old_fab = S_old[mfi];

     FArrayBox& solfab=(*localMF[FSI_GHOST_MF])[mfi];
     FArrayBox& levelpcfab=(*localMF[LEVELPC_MF])[mfi];
     FArrayBox& slopefab=(*localMF[SLOPE_RECON_MF])[mfi];

     FArrayBox& deltafab=(*localMF[delta_MF])[mfi];
     int deltacomp=0;
     if (source_term==1) {
      deltacomp=0;
     } else if (source_term==0) {
      if ((slab_step>=0)&&(slab_step<ns_time_order)) {
       deltacomp=slab_step*nstate_SDC;
      } else
       BoxLib::Error("slab_step invalid");
     } else
      BoxLib::Error("source_term invalid");

     Array<int> velbc=getBCArray(State_Type,gridno,0,
      num_materials_vel*BL_SPACEDIM);
     int dcomp=num_materials_vel*(BL_SPACEDIM+1);
     Array<int> denbc=getBCArray(State_Type,gridno,dcomp,
      nmat*num_state_material);

     int operation_flag=6; // advection
     int energyflag=advect_iter;
     int nsolve=nfluxSEM;
     int homflag=source_term;
     int local_enable_spectral=projection_enable_spectral;
     int num_materials_face=1;
     int use_VOF_weight=0;

     FORT_MAC_TO_CELL(
      &nsolveMM_FACE,
      &num_materials_face,
      &ns_time_order,
      &divu_outer_sweeps,
      &num_divu_outer_sweeps,
      &operation_flag, // 6=advection
      &energyflag,
      temperature_primitive_variable.dataPtr(),
      &nmat,
      &nparts,
      &nparts_def,
      im_solid_map_ptr,
      prescribed_solid_scale.dataPtr(),
      added_weight.dataPtr(),
      &nten,
      &level, 
      &finest_level,
      &face_flag,
      &make_interface_incomp,
      &solvability_projection,
      &project_option_visc,
      &local_enable_spectral,
      &fluxvel_index,
      &fluxden_index,
      &facevel_index,
      &facecut_index,
      &icefacecut_index,
      &curv_index,
      &conservative_tension_force,
      &pforce_index,
      &faceden_index,
      &icemask_index,
      &massface_index,
      &vofface_index,
      &nfluxSEM, // ncphys
      velbc.dataPtr(),
      denbc.dataPtr(),  // presbc
      &prev_time_slab, 
      &slab_step,
      &dt_slab,  // MAC_TO_CELL
      xlo,dx,
      tilelo,tilehi,
      fablo,fabhi,
      &bfact,
      xface.dataPtr(),ARLIM(xface.loVect()),ARLIM(xface.hiVect()), //xp
      yface.dataPtr(),ARLIM(yface.loVect()),ARLIM(yface.hiVect()), //yp
      zface.dataPtr(),ARLIM(zface.loVect()),ARLIM(zface.hiVect()), //zp
      xvel.dataPtr(),ARLIM(xvel.loVect()),ARLIM(xvel.hiVect()), //xvel
      yvel.dataPtr(),ARLIM(yvel.loVect()),ARLIM(yvel.hiVect()), //xvel
      zvel.dataPtr(),ARLIM(zvel.loVect()),ARLIM(zvel.hiVect()), //xvel
      xface.dataPtr(),ARLIM(xface.loVect()),ARLIM(xface.hiVect()), //xflux
      yface.dataPtr(),ARLIM(yface.loVect()),ARLIM(yface.hiVect()), //yflux
      zface.dataPtr(),ARLIM(zface.loVect()),ARLIM(zface.hiVect()), //zflux
      ax.dataPtr(),ARLIM(ax.loVect()),ARLIM(ax.hiVect()),
      ay.dataPtr(),ARLIM(ay.loVect()),ARLIM(ay.hiVect()),
      az.dataPtr(),ARLIM(az.loVect()),ARLIM(az.hiVect()),
      vol.dataPtr(),ARLIM(vol.loVect()),ARLIM(vol.hiVect()),
      rhsfab.dataPtr(),ARLIM(rhsfab.loVect()),ARLIM(rhsfab.hiVect()),
      snewfab.dataPtr(), // veldest
      ARLIM(snewfab.loVect()),ARLIM(snewfab.hiVect()),
      snewfab.dataPtr(dcomp), // dendest
      ARLIM(snewfab.loVect()),ARLIM(snewfab.hiVect()),
      maskfab.dataPtr(), // mask=1.0 fine/fine   0.0=coarse/fine
      ARLIM(maskfab.loVect()),ARLIM(maskfab.hiVect()),
      maskcoeffab.dataPtr(), // maskcoef: 1=not covered 0=covered
      ARLIM(maskcoeffab.loVect()),ARLIM(maskcoeffab.hiVect()),
      maskSEMfab.dataPtr(), 
      ARLIM(maskSEMfab.loVect()),ARLIM(maskSEMfab.hiVect()),
      levelpcfab.dataPtr(),
      ARLIM(levelpcfab.loVect()),ARLIM(levelpcfab.hiVect()),
      solfab.dataPtr(),
      ARLIM(solfab.loVect()),ARLIM(solfab.hiVect()),
      deltafab.dataPtr(deltacomp), // cterm
      ARLIM(deltafab.loVect()),ARLIM(deltafab.hiVect()),
      rhsfab.dataPtr(), // pold
      ARLIM(rhsfab.loVect()),ARLIM(rhsfab.hiVect()),
      S_old_fab.dataPtr(dcomp), // denold
      ARLIM(S_old_fab.loVect()),ARLIM(S_old_fab.hiVect()),
      S_old_fab.dataPtr(), // ustar
      ARLIM(S_old_fab.loVect()),ARLIM(S_old_fab.hiVect()),
      slopefab.dataPtr(), // recon
      ARLIM(slopefab.loVect()),ARLIM(slopefab.hiVect()),
      S_old_fab.dataPtr(), // mdot
      ARLIM(S_old_fab.loVect()),ARLIM(S_old_fab.hiVect()),
      S_old_fab.dataPtr(), // maskdivres
      ARLIM(S_old_fab.loVect()),ARLIM(S_old_fab.hiVect()),
      S_old_fab.dataPtr(), // maskres
      ARLIM(S_old_fab.loVect()),ARLIM(S_old_fab.hiVect()),
      &SDC_outer_sweeps,
      &homflag,
      &use_VOF_weight,
      &nsolve,
      &SEM_advection_algorithm);

     if (1==0) {
      std::cout << "SEM_scalar_advect c++ level,finest_level " << 
       level << ' ' << finest_level << '\n';
      int interior_only=1;
      tecplot_debug(snewfab,xlo,fablo,fabhi,dx,-1,0,0,
       BL_SPACEDIM,interior_only);
     }

    } // mfi
}// omp
    ParallelDescriptor::Barrier();

    // rhs=div(uF)  (except for temperature: rhs=u dot grad Theta)
    if (source_term==0) {

     if ((slab_step>=0)&&(slab_step<ns_time_order)) {

      if ((advect_iter==1)&&
          (divu_outer_sweeps+1==num_divu_outer_sweeps)) {
       int deltacomp=slab_step*nstate_SDC;
       MultiFab::Copy(*localMF[stableF_MF],*rhs,0,deltacomp,nfluxSEM,0);
      } else if (advect_iter==0) {
       // do nothing
      } else
       BoxLib::Error("advect_iter invalid");

     } else
      BoxLib::Error("source_term invalid");

    } else if (source_term==1) {

     if ((slab_step==0)&&(SDC_outer_sweeps==0)) {
      // this is ok: F_advect(t^n)
     } else if ((slab_step==0)&&(SDC_outer_sweeps!=0)) {
      BoxLib::Error("F_advect(t^n) already init when SDC_outer_sweeps==0");
     } 

     if ((slab_step>=0)&&(slab_step<=ns_time_order)) {
      int deltacomp=slab_step*nstate_SDC;
      MultiFab::Copy(*localMF[spectralF_MF],*rhs,0,deltacomp,nfluxSEM,0);
     } else
      BoxLib::Error("slab_step invalid");

    } else 
     BoxLib::Error("source_term invalid");
 
    delete rhs;

   } else
    BoxLib::Error("bfact invalid");

  } else
   BoxLib::Error("init_fluxes invalid");

 } else
  BoxLib::Error("enable_specral or ns_time_order invalid");

} // subroutine SEM_scalar_advection

// Lagrangian solid info lives at t=t^n 
// order_direct_split=base_step mod 2
// must go from finest level to coarsest.
void 
NavierStokes::split_scalar_advection() { 
 
 bool use_tiling=ns_tiling;

 int finest_level=parent->finestLevel();
 int nmat=num_materials;
 int nten=( (nmat-1)*(nmat-1)+nmat-1 )/2;
 int normdir_here=normdir_direct_split[dir_absolute_direct_split];
 if ((normdir_here<0)||(normdir_here>=BL_SPACEDIM))
  BoxLib::Error("normdir_here invalid");

 int bfact=parent->Space_blockingFactor(level);
 int bfact_f=bfact;
 if (level<finest_level)
  bfact_f=parent->Space_blockingFactor(level+1);

 if (num_materials_vel!=1)
  BoxLib::Error("num_materials_vel invalid");

 int nsolve=1;
 int nsolveMM=nsolve;
 int nsolveMM_FACE=nsolveMM;

 if (num_state_base!=2)
  BoxLib::Error("num_state_base invalid");

 if ((order_direct_split!=0)&&
     (order_direct_split!=1))
  BoxLib::Error("order_direct_split invalid");

 if ((SDC_outer_sweeps>=0)&&
     (SDC_outer_sweeps<ns_time_order)) {
  // do nothing
 } else {
  std::cout << "SDC_outer_sweeps= " << SDC_outer_sweeps << '\n';
  BoxLib::Error("SDC_outer_sweeps invalid");
 }

 if ((dir_absolute_direct_split<0)||
     (dir_absolute_direct_split>=BL_SPACEDIM))
  BoxLib::Error("dir_absolute_direct_split invalid");

 int ngrow=2;
 int mac_grow=1; 
 int ngrow_mac_old=0;

 if (face_flag==0) {
  // do nothing
 } else if (face_flag==1) {
  ngrow=3;
  ngrow_mac_old=2;
  mac_grow=2;
 } else
  BoxLib::Error("face_flag invalid 4");

  // vof,ref centroid,order,slope,intercept  x nmat
 VOF_Recon_resize(ngrow,SLOPE_RECON_MF);
 debug_ngrow(SLOPE_RECON_MF,ngrow,36);
 resize_maskfiner(ngrow,MASKCOEF_MF);
 debug_ngrow(MASKCOEF_MF,ngrow,36);
 debug_ngrow(VOF_LS_PREV_TIME_MF,1,38);
 if (localMF[VOF_LS_PREV_TIME_MF]->nComp()!=2*nmat)
  BoxLib::Error("vof ls prev time invalid ncomp");

 MultiFab& S_new=get_new_data(State_Type,slab_step+1);
 int ncomp_state=S_new.nComp();
 if (ncomp_state!=num_materials_vel*(BL_SPACEDIM+1)+
     nmat*(num_state_material+ngeom_raw)+1)
  BoxLib::Error("ncomp_state invalid");

 MultiFab& LS_new=get_new_data(LS_Type,slab_step+1);
 if (LS_new.nComp()!=nmat*(BL_SPACEDIM+1))
  BoxLib::Error("LS_new ncomp invalid");

 const Real* dx = geom.CellSize();
 const Box& domain = geom.Domain();
 const int* domlo = domain.loVect();
 const int* domhi = domain.hiVect();

 int scomp_mofvars=num_materials_vel*(BL_SPACEDIM+1)+nmat*num_state_material;
 Array<int> dombc(2*BL_SPACEDIM);
 const BCRec& descbc = get_desc_lst()[State_Type].getBC(scomp_mofvars);
 const int* b_rec=descbc.vect();
 for (int m=0;m<2*BL_SPACEDIM;m++)
  dombc[m]=b_rec[m];

 vel_time_slab=prev_time_slab;
 if (divu_outer_sweeps==0) 
  vel_time_slab=prev_time_slab;
 else if (divu_outer_sweeps>0)
  vel_time_slab=cur_time_slab;
 else
  BoxLib::Error("divu_outer_sweeps invalid");

  // in: split_scalar_advection
 getStateDen_localMF(DEN_RECON_MF,ngrow,advect_time_slab);

 int local_tensor_type=State_Type;
 int local_tensor_mf=DEN_RECON_MF;

 if ((num_materials_viscoelastic>=1)&&(num_materials_viscoelastic<=nmat)) {
  local_tensor_type=Tensor_Type;
  local_tensor_mf=TENSOR_RECON_MF;
  getStateTensor_localMF(TENSOR_RECON_MF,1,0,
   num_materials_viscoelastic*NUM_TENSOR_TYPE,advect_time_slab);
 } else if (num_materials_viscoelastic==0) {
  // do nothing
 } else
  BoxLib::Error("num_materials_viscoelastic invalid");

 MultiFab& Tensor_new=get_new_data(local_tensor_type,slab_step+1);

 getStateDist_localMF(LS_RECON_MF,1,advect_time_slab,10);

   // the pressure from before will be copied to the new pressure.
 getState_localMF(VELADVECT_MF,ngrow,0,
  num_materials_vel*(BL_SPACEDIM+1),
  advect_time_slab); 

   // in: split_scalar_advection
 new_localMF(CONSERVE_FLUXES_MF+normdir_here,nmat,0,normdir_here);
 setVal_localMF(CONSERVE_FLUXES_MF+normdir_here,0.0,0,nmat,0);

  // average down volume fraction fluxes 
 if (level<finest_level) {
  if ((bfact==1)&&(bfact_f==1)) {
   NavierStokes& ns_fine=getLevel(level+1);
   vofflux_sum(normdir_here,
     *localMF[CONSERVE_FLUXES_MF+normdir_here],
     *ns_fine.localMF[CONSERVE_FLUXES_MF+normdir_here]);
  }
 }

 for (int dir=0;dir<BL_SPACEDIM;dir++) {
  getStateMAC_localMF(UMACOLD_MF+dir,ngrow_mac_old,dir,
    0,nsolveMM_FACE,advect_time_slab);
 } // dir

 if ((dir_absolute_direct_split<0)||
     (dir_absolute_direct_split>=BL_SPACEDIM))
  BoxLib::Error("dir_absolute_direct_split invalid");

 if (dir_absolute_direct_split==0) {

  int unsplit_displacement=0;
  prepare_displacement(mac_grow,unsplit_displacement);

 } else if ((dir_absolute_direct_split>=1)&&
            (dir_absolute_direct_split<BL_SPACEDIM)) {
  // do nothing
 } else
  BoxLib::Error("dir_absolute_direct_split invalid");

 int vofrecon_ncomp=localMF[SLOPE_RECON_MF]->nComp();
 if (vofrecon_ncomp!=nmat*ngeom_recon)
   BoxLib::Error("recon ncomp bust");

 int den_recon_ncomp=localMF[DEN_RECON_MF]->nComp();
 if (den_recon_ncomp!=num_state_material*nmat)
   BoxLib::Error("den_recon invalid");

 int LS_recon_ncomp=localMF[LS_RECON_MF]->nComp();
 if (LS_recon_ncomp!=nmat*(1+BL_SPACEDIM))
   BoxLib::Error("LS_recon invalid");

 debug_ngrow(LS_RECON_MF,1,40);

 resize_mask_nbr(ngrow);
 debug_ngrow(MASK_NBR_MF,ngrow,28); 

 MultiFab* umac_new[BL_SPACEDIM];
 for (int dir=0;dir<BL_SPACEDIM;dir++) {
  umac_new[dir]=&get_new_data(Umac_Type+dir,slab_step+1);
 }


 int ngrid=grids.size();

 int nc_conserve=BL_SPACEDIM+nmat*num_state_material;
 MultiFab* conserve=new MultiFab(grids,nc_conserve,ngrow,dmap,Fab_allocate);

 int iden_base=BL_SPACEDIM;
 int itensor_base=iden_base+nmat*num_state_material;
 int imof_base=itensor_base+num_materials_viscoelastic*NUM_TENSOR_TYPE;
 int iLS_base=imof_base+nmat*ngeom_raw;
 int iFtarget_base=iLS_base+nmat;
 int iden_mom_base=iFtarget_base+nmat;
 int nc_bucket=iden_mom_base+nmat;

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(*localMF[SLOPE_RECON_MF],use_tiling); mfi.isValid(); ++mfi) {
  BL_ASSERT(grids[mfi.index()] == mfi.validbox());
  const int gridno = mfi.index();
  const Box& tilegrid = mfi.tilebox();
  const Box& fabgrid = grids[gridno];
  const int* tilelo=tilegrid.loVect();
  const int* tilehi=tilegrid.hiVect();
  const int* fablo=fabgrid.loVect();
  const int* fabhi=fabgrid.hiVect();

  FArrayBox& consfab=(*conserve)[mfi];
  FArrayBox& denfab=(*localMF[DEN_RECON_MF])[mfi];
  FArrayBox& velfab=(*localMF[VELADVECT_MF])[mfi];
  FORT_BUILD_CONSERVE( 
   &iden_base,
   override_density.dataPtr(),
   temperature_primitive_variable.dataPtr(),
   consfab.dataPtr(),ARLIM(consfab.loVect()),ARLIM(consfab.hiVect()),
   denfab.dataPtr(),ARLIM(denfab.loVect()),ARLIM(denfab.hiVect()),
   velfab.dataPtr(),ARLIM(velfab.loVect()),ARLIM(velfab.hiVect()),
   tilelo,tilehi,
   fablo,fabhi,&bfact,
   &nmat,&ngrow,
   &normdir_here,
   &nc_conserve,
   &den_recon_ncomp);
 }  // mfi
} // omp
 ParallelDescriptor::Barrier();

 MultiFab* momslope=new MultiFab(grids,nc_conserve,1,dmap,Fab_allocate);

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(*localMF[SLOPE_RECON_MF],use_tiling); mfi.isValid(); ++mfi) {
  BL_ASSERT(grids[mfi.index()] == mfi.validbox());
  const int gridno = mfi.index();
  const Box& tilegrid = mfi.tilebox();
  const Box& fabgrid = grids[gridno];
  const int* tilelo=tilegrid.loVect();
  const int* tilehi=tilegrid.hiVect();
  const int* fablo=fabgrid.loVect();
  const int* fabhi=fabgrid.hiVect();

  const Real* xlo = grid_loc[gridno].lo();

  FArrayBox& reconfab=(*localMF[SLOPE_RECON_MF])[mfi];
  FArrayBox& consfab=(*conserve)[mfi];
  FArrayBox& slopefab=(*momslope)[mfi];
  FArrayBox& masknbrfab=(*localMF[MASK_NBR_MF])[mfi];

  Array<int> velbc=getBCArray(State_Type,gridno,normdir_here,1);

  FORT_BUILD_SLOPES( 
   masknbrfab.dataPtr(),
   ARLIM(masknbrfab.loVect()),ARLIM(masknbrfab.hiVect()),
   reconfab.dataPtr(),ARLIM(reconfab.loVect()),ARLIM(reconfab.hiVect()),
   consfab.dataPtr(),ARLIM(consfab.loVect()),ARLIM(consfab.hiVect()),
   slopefab.dataPtr(),
   ARLIM(slopefab.loVect()),ARLIM(slopefab.hiVect()),
   &nc_conserve,
   &nmat, 
   tilelo,tilehi,
   fablo,fabhi,&bfact, // slopes conserved vars
   &level,
   &finest_level,
   velbc.dataPtr(),
   xlo,dx,
   &normdir_here,
   &ngrow,
   advection_order.dataPtr(), 
   density_advection_order.dataPtr(), 
   &slope_limiter_option);

 }  // mfi
}// omp
 ParallelDescriptor::Barrier();

 MultiFab* xvof[BL_SPACEDIM];
 MultiFab* xvel[BL_SPACEDIM]; // xvel
 MultiFab* xvelslope[BL_SPACEDIM]; // xvelslope,xcen
 MultiFab* side_bucket_mom[BL_SPACEDIM];
 MultiFab* side_bucket_mass[BL_SPACEDIM];

  // (dir-1)*2*nmat + (side-1)*nmat + im
 int nrefine_vof=2*nmat*BL_SPACEDIM;

  // (veldir-1)*2*nmat*sdim + (side-1)*nmat*sdim + (im-1)*sdim+dir
 int nrefine_cen=2*nmat*BL_SPACEDIM*BL_SPACEDIM;

 if (face_flag==0) {

  for (int dir=0;dir<BL_SPACEDIM;dir++) {
   xvof[dir]=localMF[SLOPE_RECON_MF];
   xvel[dir]=localMF[SLOPE_RECON_MF];
   xvelslope[dir]=localMF[SLOPE_RECON_MF];
   side_bucket_mom[dir]=localMF[SLOPE_RECON_MF];
   side_bucket_mass[dir]=localMF[SLOPE_RECON_MF];
  }

 } else if (face_flag==1) {

  MultiFab* vofF=new MultiFab(grids,nrefine_vof,ngrow,dmap,Fab_allocate);

   // linear expansion: 
   // 1. find slopes u_x
   // 2. (rho u)(x)_m = rho_m (u + u_x (x - x_m_centroid))
  MultiFab* cenF=new MultiFab(grids,nrefine_cen,ngrow,dmap,Fab_allocate);
  MultiFab* massF=new MultiFab(grids,nrefine_vof,ngrow,dmap,Fab_allocate);

#ifdef _OPENMP
#pragma omp parallel
#endif
{
  for (MFIter mfi(*vofF,use_tiling); mfi.isValid(); ++mfi) {
   BL_ASSERT(grids[mfi.index()] == mfi.validbox());
   const int gridno = mfi.index();
   const Box& tilegrid = mfi.tilebox();
   const Box& fabgrid = grids[gridno];
   const int* tilelo=tilegrid.loVect();
   const int* tilehi=tilegrid.hiVect();
   const int* fablo=fabgrid.loVect();
   const int* fabhi=fabgrid.hiVect();

   const Real* xlo = grid_loc[gridno].lo();

   FArrayBox& slopefab=(*localMF[SLOPE_RECON_MF])[mfi];
   FArrayBox& denstatefab=(*localMF[DEN_RECON_MF])[mfi];

   FArrayBox& vofFfab=(*vofF)[mfi];
   FArrayBox& cenFfab=(*cenF)[mfi];
   FArrayBox& massFfab=(*massF)[mfi];

   int tessellate=0;

   int tid=ns_thread();

    // centroid in absolute coordinates.
   FORT_BUILD_SEMIREFINEVOF(
    &tid,
    &tessellate,
    &ngrow,
    &nrefine_vof,
    &nrefine_cen,
    &nten,
    spec_material_id.dataPtr(),
    mass_fraction_id.dataPtr(),
    species_evaporation_density.dataPtr(),
    cavitation_vapor_density.dataPtr(),
    cavitation_species.dataPtr(),
    override_density.dataPtr(),
    xlo,dx,
    slopefab.dataPtr(),
    ARLIM(slopefab.loVect()),ARLIM(slopefab.hiVect()),
    denstatefab.dataPtr(),
    ARLIM(denstatefab.loVect()),ARLIM(denstatefab.hiVect()),
    vofFfab.dataPtr(),ARLIM(vofFfab.loVect()),ARLIM(vofFfab.hiVect()),
    cenFfab.dataPtr(),ARLIM(cenFfab.loVect()),ARLIM(cenFfab.hiVect()),
    massFfab.dataPtr(),ARLIM(massFfab.loVect()),ARLIM(massFfab.hiVect()),
    tilelo,tilehi,
    fablo,fabhi,
    &bfact,
    &nmat,
    &level,&finest_level);
  }  // mfi
}// omp
  ParallelDescriptor::Barrier();

  for (int dir=0;dir<BL_SPACEDIM;dir++) {
   xvof[dir]=new MultiFab(state[Umac_Type+dir].boxArray(),nmat,
     ngrow_mac_old,dmap,Fab_allocate);

   xvel[dir]=new MultiFab(state[Umac_Type+dir].boxArray(),1,
     ngrow_mac_old,dmap,Fab_allocate);
    // xvelslope,xcen
   xvelslope[dir]=new MultiFab(state[Umac_Type+dir].boxArray(),1+nmat,
     ngrow_mac_old,dmap,Fab_allocate);

    //ncomp=2 ngrow=1
   side_bucket_mom[dir]=new MultiFab(grids,2,1,dmap,Fab_allocate);
    //scomp=0 ncomp=2 ngrow=1
   side_bucket_mom[dir]->setVal(0.0,0,2,1);

    //ncomp=2 ngrow=1
   side_bucket_mass[dir]=new MultiFab(grids,2,1,dmap,Fab_allocate);
    //scomp=0 ncomp=2 ngrow=1
   side_bucket_mass[dir]->setVal(0.0,0,2,1);
  }  // dir 

  for (int veldir=1;veldir<=BL_SPACEDIM;veldir++) {

#ifdef _OPENMP
#pragma omp parallel
#endif
{
   for (MFIter mfi(*localMF[SLOPE_RECON_MF],use_tiling); 
        mfi.isValid(); ++mfi) {
    BL_ASSERT(grids[mfi.index()] == mfi.validbox());
    const int gridno = mfi.index();
    const Box& tilegrid = mfi.tilebox();
    const Box& fabgrid = grids[gridno];
    const int* tilelo=tilegrid.loVect();
    const int* tilehi=tilegrid.hiVect();
    const int* fablo=fabgrid.loVect();
    const int* fabhi=fabgrid.hiVect();

    const Real* xlo = grid_loc[gridno].lo();

    FArrayBox& xmac_old=(*localMF[UMACOLD_MF+veldir-1])[mfi];
    FArrayBox& xvoffab=(*xvof[veldir-1])[mfi];
    FArrayBox& xvelfab=(*xvel[veldir-1])[mfi]; // xvelleft,xvelright
    FArrayBox& xvelslopefab=(*xvelslope[veldir-1])[mfi]; // xvelslope,xcen
    FArrayBox& vofFfab=(*vofF)[mfi];
    FArrayBox& cenFfab=(*cenF)[mfi];
    int unsplit_advection=0;

    FORT_BUILD_MACVOF( 
     &unsplit_advection,
     &nsolveMM_FACE,
     &level,
     &finest_level,
     &normdir_here,
     &nrefine_vof,
     &nrefine_cen,
     vofFfab.dataPtr(),ARLIM(vofFfab.loVect()),ARLIM(vofFfab.hiVect()),
     cenFfab.dataPtr(),ARLIM(cenFfab.loVect()),ARLIM(cenFfab.hiVect()),
     xmac_old.dataPtr(),ARLIM(xmac_old.loVect()),ARLIM(xmac_old.hiVect()),
     xvoffab.dataPtr(),ARLIM(xvoffab.loVect()),ARLIM(xvoffab.hiVect()),
     xvelfab.dataPtr(),ARLIM(xvelfab.loVect()),ARLIM(xvelfab.hiVect()),
     xvelslopefab.dataPtr(),
     ARLIM(xvelslopefab.loVect()),ARLIM(xvelslopefab.hiVect()),
     xlo,dx,
     tilelo,tilehi,
     fablo,fabhi,
     &bfact,
     &nmat,&ngrow, 
     &ngrow_mac_old,&veldir);
   }  // mfi
}// omp
   ParallelDescriptor::Barrier();


#ifdef _OPENMP
#pragma omp parallel
#endif
{
   for (MFIter mfi(*localMF[SLOPE_RECON_MF],use_tiling); 
        mfi.isValid(); ++mfi) {
    BL_ASSERT(grids[mfi.index()] == mfi.validbox());
    const int gridno = mfi.index();
    const Box& tilegrid = mfi.tilebox();
    const Box& fabgrid = grids[gridno];
    const int* tilelo=tilegrid.loVect();
    const int* tilehi=tilegrid.hiVect();
    const int* fablo=fabgrid.loVect();
    const int* fabhi=fabgrid.hiVect();

    const Real* xlo = grid_loc[gridno].lo();

    FArrayBox& xvoffab=(*xvof[veldir-1])[mfi];
    FArrayBox& xvelfab=(*xvel[veldir-1])[mfi]; 
    FArrayBox& xvelslopefab=(*xvelslope[veldir-1])[mfi]; //xvelslope,xcen
    FArrayBox& masknbrfab=(*localMF[MASK_NBR_MF])[mfi];

    Array<int> velbc=getBCArray(State_Type,gridno,normdir_here,1);
    int slopedir=veldir-1;

    FORT_BUILD_SLOPES_FACE( 
     masknbrfab.dataPtr(),
     ARLIM(masknbrfab.loVect()),ARLIM(masknbrfab.hiVect()),
      // "vfrac"
     xvoffab.dataPtr(),ARLIM(xvoffab.loVect()),ARLIM(xvoffab.hiVect()),
      // "slsrc"
     xvelfab.dataPtr(),ARLIM(xvelfab.loVect()),ARLIM(xvelfab.hiVect()),
      // "sldst"
     xvelslopefab.dataPtr(),
     ARLIM(xvelslopefab.loVect()),ARLIM(xvelslopefab.hiVect()),
     &nmat, 
     tilelo,tilehi,
     fablo,fabhi,&bfact,
     &level,
     &finest_level,
     velbc.dataPtr(),
     xlo,dx,
     &normdir_here,
     &slopedir,
     &ngrow_mac_old,
     advection_order.dataPtr(), 
     &slope_limiter_option);

   }  // mfi
} // omp
   ParallelDescriptor::Barrier();

  } // veldir=1..sdim

  delete vofF;
  delete cenF;
  delete massF;

 } else
  BoxLib::Error("face_flag invalid 5");

 Array<int> nprocessed;
 nprocessed.resize(thread_class::nthreads);
 for (int tid=0;tid<thread_class::nthreads;tid++) {
  nprocessed[tid]=0.0;
 }


 Real profile_time_start=0.0;
 if (profile_debug==1) {
  profile_time_start=ParallelDescriptor::second();
 }

  // in: split_scalar_advection
  // initialize selected state variables 

  // velocity and pressure
 int scomp_init=0;
 int ncomp_init=num_materials_vel*(BL_SPACEDIM+1); 
 S_new.setVal(0.0,scomp_init,ncomp_init,1);

 for (int im=0;im<nmat;im++) {
  if (ns_is_rigid(im)==0) {
   scomp_init=num_materials_vel*(BL_SPACEDIM+1)+im*num_state_material;
   ncomp_init=num_state_material;
   S_new.setVal(0.0,scomp_init,ncomp_init,1);
   scomp_init=num_materials_vel*(BL_SPACEDIM+1)+nmat*num_state_material+
     im*ngeom_raw;
   ncomp_init=ngeom_raw;
   S_new.setVal(0.0,scomp_init,ncomp_init,1);
   LS_new.setVal(0.0,im,1,1);
  } else if (ns_is_rigid(im)==1) {
   if (solidheat_flag==0) {  // thermal diffuse in solid (default)
    scomp_init=num_materials_vel*(BL_SPACEDIM+1)+im*num_state_material+1;
    ncomp_init=1;
    S_new.setVal(0.0,scomp_init,ncomp_init,1);
   } else if (solidheat_flag==2) { // Neumann
    // do nothing
   } else if (solidheat_flag==1) { // dirichlet
    // do nothing
   } else
    BoxLib::Error("solidheat_flag invalid");
  } else
   BoxLib::Error("ns_is_rigid(im) invalid");
 } // im=0..nmat-1

 if ((num_materials_viscoelastic>=1)&&(num_materials_viscoelastic<=nmat)) {
  Tensor_new.setVal(0.0,0,num_materials_viscoelastic*NUM_TENSOR_TYPE,1);
 } else if (num_materials_viscoelastic==0) {
  // do nothing
 } else
  BoxLib::Error("num_materials_viscoelastic invalid");

 int ntensor=Tensor_new.nComp();

 MultiFab* cons_cor;
 if (EILE_flag==0) {  // Sussman and Puckett
  cons_cor=localMF[CONS_COR_MF];
 } else if ((EILE_flag==-1)||  // Weymouth and Yue
            (EILE_flag==1)||   // EI-LE
            (EILE_flag==2)||   // always EI
            (EILE_flag==3)) {  // always LE
  cons_cor=localMF[DEN_RECON_MF];
 } else
  BoxLib::Error("EILE_flag invalid");

 if (dir_absolute_direct_split==0) {
   // initialize the error indicator to be 0.0
  S_new.setVal(0.0,ncomp_state-1,1,1);

  if (EILE_flag==0) {
   if (cons_cor->nComp()!=nmat)
    BoxLib::Error("cons_cor->nComp()!=nmat");
   if (cons_cor->nGrow()!=1)
    BoxLib::Error("cons_cor->nGrow()!=1");
   cons_cor->setVal(0.0,0,nmat,1);
  } else if ((EILE_flag==-1)||
             (EILE_flag==1)||
             (EILE_flag==2)||
             (EILE_flag==3)) {
   // do nothing
  } else
   BoxLib::Error("EILE_flag invalid");
    
 } else if ((dir_absolute_direct_split==1)||
            (dir_absolute_direct_split==BL_SPACEDIM-1)) {
  // do nothing
 } else {
  BoxLib::Error("dir_absolute_direct_split invalid");
 }

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(S_new,use_tiling); mfi.isValid(); ++mfi) {
  BL_ASSERT(grids[mfi.index()] == mfi.validbox());

  const int gridno = mfi.index();
  const Box& tilegrid = mfi.tilebox();
  const Box& fabgrid = grids[gridno];
  const int* tilelo=tilegrid.loVect();
  const int* tilehi=tilegrid.hiVect();
  const int* fablo=fabgrid.loVect();
  const int* fabhi=fabgrid.hiVect();

  const Real* xlo = grid_loc[gridno].lo();

    // mask=tag if not covered by level+1 or outside the domain.
    // mask=1-tag if covered by level+1 and inside the domain.
    // NavierStokes::maskfiner  (clear_phys_boundary==0)
  FArrayBox& maskfab=(*localMF[MASKCOEF_MF])[mfi];
   // mask_nbr:
   // (1) =1 interior  =1 fine-fine ghost in domain  =0 otherwise
   // (2) =1 interior  =0 otherwise
   // (3) =1 interior+ngrow-1  =0 otherwise
   // (4) =1 interior+ngrow    =0 otherwise
  FArrayBox& masknbrfab=(*localMF[MASK_NBR_MF])[mfi];

  FArrayBox& unode=(*localMF[MAC_VELOCITY_MF+normdir_here])[mfi];
  if (unode.nComp()!=nsolveMM_FACE*BL_SPACEDIM)
   BoxLib::Error("unode has invalid ncomp");

    // this is the original data
  FArrayBox& LSfab=(*localMF[LS_RECON_MF])[mfi];
  FArrayBox& denfab=(*localMF[DEN_RECON_MF])[mfi];
  FArrayBox& tenfab=(*localMF[local_tensor_mf])[mfi];
  FArrayBox& velfab=(*localMF[VELADVECT_MF])[mfi];

    // this is the slope data
  FArrayBox& vofslopefab=(*localMF[SLOPE_RECON_MF])[mfi];

  FArrayBox& vofls0fab=(*localMF[VOF_LS_PREV_TIME_MF])[mfi];
  FArrayBox& corfab=(*cons_cor)[mfi];

  int dencomp=num_materials_vel*(BL_SPACEDIM+1);
  int mofcomp=dencomp+nmat*num_state_material;
  int errcomp=mofcomp+nmat*ngeom_raw;

  Array<int> velbc=getBCArray(State_Type,gridno,normdir_here,1);

     // this is the result
  FArrayBox& destfab=S_new[mfi];
  FArrayBox& tennewfab=Tensor_new[mfi];
  FArrayBox& LSdestfab=LS_new[mfi];

  FArrayBox& consfab=(*conserve)[mfi];

  FArrayBox& xvelfab=(*xvel[0])[mfi]; 
  FArrayBox& yvelfab=(*xvel[1])[mfi];
  FArrayBox& zvelfab=(*xvel[BL_SPACEDIM-1])[mfi];

  FArrayBox& xvelslopefab=(*xvelslope[0])[mfi];  // xvelslope,xcen
  FArrayBox& yvelslopefab=(*xvelslope[1])[mfi];
  FArrayBox& zvelslopefab=(*xvelslope[BL_SPACEDIM-1])[mfi];

  FArrayBox& slopefab=(*momslope)[mfi];

  FArrayBox& xmomside=(*side_bucket_mom[0])[mfi];
  FArrayBox& ymomside=(*side_bucket_mom[1])[mfi];
  FArrayBox& zmomside=(*side_bucket_mom[BL_SPACEDIM-1])[mfi];

  FArrayBox& xmassside=(*side_bucket_mass[0])[mfi];
  FArrayBox& ymassside=(*side_bucket_mass[1])[mfi];
  FArrayBox& zmassside=(*side_bucket_mass[BL_SPACEDIM-1])[mfi];

  FArrayBox& ucellfab=(*localMF[CELL_VELOCITY_MF])[mfi];

  FArrayBox& vofflux=(*localMF[CONSERVE_FLUXES_MF+normdir_here])[mfi];

  prescribed_vel_time_slab=0.5*(prev_time_slab+cur_time_slab);

  int tid=ns_thread();

   // solid distance function and solid moments are not modified.
   // solid temperature is modified only if solidheat_flag==0.
  FORT_VFRAC_SPLIT(
   &nsolveMM_FACE,
   &nprocessed[tid],
   &tid,
   &make_interface_incomp,
   added_weight.dataPtr(),
   density_floor.dataPtr(),
   density_ceiling.dataPtr(),
   &solidheat_flag, //0==diffuse in solid 1==dirichlet 2==neumann
   temperature_primitive_variable.dataPtr(),
   &dencomp,&mofcomp,&errcomp,
   latent_heat.dataPtr(),
   freezing_model.dataPtr(),
   distribute_from_target.dataPtr(),
   &nten,
   &face_flag,
   override_density.dataPtr(),
   velbc.dataPtr(),
   &EILE_flag,
   &dir_absolute_direct_split,
   &normdir_here,
   tilelo,tilehi,
   fablo,fabhi,
   &bfact,
   &bfact_f,
   &dt_slab, // VFRAC_SPLIT
   &prev_time_slab,
   &prescribed_vel_time_slab,
   vofflux.dataPtr(),
   ARLIM(vofflux.loVect()),ARLIM(vofflux.hiVect()),
     // this is the original data
   LSfab.dataPtr(),
   ARLIM(LSfab.loVect()),ARLIM(LSfab.hiVect()),
   denfab.dataPtr(),
   ARLIM(denfab.loVect()),ARLIM(denfab.hiVect()),
   tenfab.dataPtr(),
   ARLIM(tenfab.loVect()),ARLIM(tenfab.hiVect()),
   velfab.dataPtr(),
   ARLIM(velfab.loVect()),ARLIM(velfab.hiVect()),
     // slope data
   vofslopefab.dataPtr(),
   ARLIM(vofslopefab.loVect()),ARLIM(vofslopefab.hiVect()),
     // this is the result
   destfab.dataPtr(),
   ARLIM(destfab.loVect()),ARLIM(destfab.hiVect()),
   tennewfab.dataPtr(),
   ARLIM(tennewfab.loVect()),ARLIM(tennewfab.hiVect()),
   LSdestfab.dataPtr(),
   ARLIM(LSdestfab.loVect()),ARLIM(LSdestfab.hiVect()),
    // other vars.
   ucellfab.dataPtr(),ARLIM(ucellfab.loVect()),ARLIM(ucellfab.hiVect()),
   vofls0fab.dataPtr(),ARLIM(vofls0fab.loVect()),ARLIM(vofls0fab.hiVect()),
   corfab.dataPtr(),ARLIM(corfab.loVect()),ARLIM(corfab.hiVect()),
   maskfab.dataPtr(),ARLIM(maskfab.loVect()),ARLIM(maskfab.hiVect()),
   masknbrfab.dataPtr(),
   ARLIM(masknbrfab.loVect()),ARLIM(masknbrfab.hiVect()),
   unode.dataPtr(),ARLIM(unode.loVect()),ARLIM(unode.hiVect()),
   xlo,dx,
    // local variables
   consfab.dataPtr(),ARLIM(consfab.loVect()),ARLIM(consfab.hiVect()),
    // xvelleft,xvelright
   xvelfab.dataPtr(),ARLIM(xvelfab.loVect()),ARLIM(xvelfab.hiVect()),
   yvelfab.dataPtr(),ARLIM(yvelfab.loVect()),ARLIM(yvelfab.hiVect()),
   zvelfab.dataPtr(),ARLIM(zvelfab.loVect()),ARLIM(zvelfab.hiVect()),
    // xvelslope,xcen
   xvelslopefab.dataPtr(),
   ARLIM(xvelslopefab.loVect()),ARLIM(xvelslopefab.hiVect()),
   yvelslopefab.dataPtr(),
   ARLIM(yvelslopefab.loVect()),ARLIM(yvelslopefab.hiVect()),
   zvelslopefab.dataPtr(),
   ARLIM(zvelslopefab.loVect()),ARLIM(zvelslopefab.hiVect()),
   slopefab.dataPtr(),ARLIM(slopefab.loVect()),ARLIM(slopefab.hiVect()),
   xmomside.dataPtr(),ARLIM(xmomside.loVect()),ARLIM(xmomside.hiVect()),
   ymomside.dataPtr(),ARLIM(ymomside.loVect()),ARLIM(ymomside.hiVect()),
   zmomside.dataPtr(),ARLIM(zmomside.loVect()),ARLIM(zmomside.hiVect()),
   xmassside.dataPtr(),ARLIM(xmassside.loVect()),ARLIM(xmassside.hiVect()),
   ymassside.dataPtr(),ARLIM(ymassside.loVect()),ARLIM(ymassside.hiVect()),
   zmassside.dataPtr(),ARLIM(zmassside.loVect()),ARLIM(zmassside.hiVect()),
   &ngrow,
   &ngrow_mac_old,
   &nc_conserve,
   &iden_base,
   &nmat,
   &map_forward_direct_split[normdir_here],
   &vofrecon_ncomp,
   &den_recon_ncomp,
   &ncomp_state,
   &ntensor,
   &nc_bucket,
   &nrefine_vof,
   &verbose,
   &gridno,&ngrid,
   &level,
   &finest_level,
   dombc.dataPtr(), 
   domlo,domhi);

 }  // mfi
} // omp
 for (int tid=1;tid<thread_class::nthreads;tid++) {
  nprocessed[0]+=nprocessed[tid];
 }
 ParallelDescriptor::Barrier();
 ParallelDescriptor::ReduceIntSum(nprocessed[0]);

 if (profile_debug==1) {
  Real profile_time_end=ParallelDescriptor::second();
  if (ParallelDescriptor::IOProcessor()) {
   std::cout << "level= " << level << '\n';
   std::cout << "nprocessed= " << nprocessed[0] << '\n';
   std::cout << "profile VFRAC_SPLIT time = " << 
     profile_time_end-profile_time_start << '\n';
  }
 }

 if (face_flag==0) {
  // do nothing
 } else if (face_flag==1) {

  MultiFab* mask_unsplit=new MultiFab(grids,1,1,dmap,Fab_allocate);
  mask_unsplit->setVal(1.0);

#ifdef _OPENMP
#pragma omp parallel
#endif
{
  for (MFIter mfi(S_new,use_tiling); mfi.isValid(); ++mfi) {
   BL_ASSERT(grids[mfi.index()] == mfi.validbox());

   const int gridno = mfi.index();
   const Box& tilegrid = mfi.tilebox();
   const Box& fabgrid = grids[gridno];
   const int* tilelo=tilegrid.loVect();
   const int* tilehi=tilegrid.hiVect();
   const int* fablo=fabgrid.loVect();
   const int* fabhi=fabgrid.hiVect();

   const Real* xlo = grid_loc[gridno].lo();

    // mask=tag if not covered by level+1 or outside the domain.
   FArrayBox& maskfab=(*localMF[MASKCOEF_MF])[mfi];
   FArrayBox& maskunsplitfab=(*mask_unsplit)[mfi];

   FArrayBox& xmomside=(*side_bucket_mom[0])[mfi];
   FArrayBox& ymomside=(*side_bucket_mom[1])[mfi];
   FArrayBox& zmomside=(*side_bucket_mom[BL_SPACEDIM-1])[mfi];

   FArrayBox& xmassside=(*side_bucket_mass[0])[mfi];
   FArrayBox& ymassside=(*side_bucket_mass[1])[mfi];
   FArrayBox& zmassside=(*side_bucket_mass[BL_SPACEDIM-1])[mfi];

   FArrayBox& xmac_new=(*umac_new[0])[mfi];
   FArrayBox& ymac_new=(*umac_new[1])[mfi];
   FArrayBox& zmac_new=(*umac_new[BL_SPACEDIM-1])[mfi];

   FORT_BUILD_NEWMAC(
    &nsolveMM_FACE,
    &normdir_here,
    tilelo,tilehi,
    fablo,fabhi,&bfact,
    xmomside.dataPtr(),ARLIM(xmomside.loVect()),ARLIM(xmomside.hiVect()),
    ymomside.dataPtr(),ARLIM(ymomside.loVect()),ARLIM(ymomside.hiVect()),
    zmomside.dataPtr(),ARLIM(zmomside.loVect()),ARLIM(zmomside.hiVect()),
    xmassside.dataPtr(),ARLIM(xmassside.loVect()),ARLIM(xmassside.hiVect()),
    ymassside.dataPtr(),ARLIM(ymassside.loVect()),ARLIM(ymassside.hiVect()),
    zmassside.dataPtr(),ARLIM(zmassside.loVect()),ARLIM(zmassside.hiVect()),
    xmac_new.dataPtr(),ARLIM(xmac_new.loVect()),ARLIM(xmac_new.hiVect()),
    ymac_new.dataPtr(),ARLIM(ymac_new.loVect()),ARLIM(ymac_new.hiVect()),
    zmac_new.dataPtr(),ARLIM(zmac_new.loVect()),ARLIM(zmac_new.hiVect()),
    maskfab.dataPtr(),ARLIM(maskfab.loVect()),ARLIM(maskfab.hiVect()),
    maskunsplitfab.dataPtr(),
    ARLIM(maskunsplitfab.loVect()),ARLIM(maskunsplitfab.hiVect()),
    xlo,dx,
    &nmat,
    &level,
    &finest_level);

  }  // mfi
} // omp
  ParallelDescriptor::Barrier();

  delete mask_unsplit;
 } else
  BoxLib::Error("face_flag invalid 6");

 delete conserve;
 delete momslope;
 
 if (face_flag==0) {
  // do nothing
 } else if (face_flag==1) {
  for (int dir=0;dir<BL_SPACEDIM;dir++) {
   delete xvof[dir];
   delete xvel[dir];
   delete xvelslope[dir];
   delete side_bucket_mom[dir];
   delete side_bucket_mass[dir];
  }
 } else
  BoxLib::Error("face_flag invalid 7");

 delete_localMF(VELADVECT_MF,1);
 delete_localMF(DEN_RECON_MF,1);

 if ((num_materials_viscoelastic>=1)&&(num_materials_viscoelastic<=nmat)) {
  delete_localMF(TENSOR_RECON_MF,1);
 } else if (num_materials_viscoelastic==0) {
  // do nothing
 } else
  BoxLib::Error("num_materials_viscoelastic invalid");
 
 delete_localMF(LS_RECON_MF,1);
 
 if (stokes_flow==0) {
  // do nothing
 } else if (stokes_flow==1) {
  MultiFab& S_old=get_new_data(State_Type,slab_step);
  MultiFab::Copy(S_new,S_old,0,0,BL_SPACEDIM,1);
  for (int dir=0;dir<BL_SPACEDIM;dir++) {
   MultiFab::Copy(*umac_new[dir],*localMF[UMACOLD_MF+dir],0,0,1,0);
  }
 } else
  BoxLib::Error("stokes_flow invalid");

 delete_localMF(UMACOLD_MF,BL_SPACEDIM);
 
 if ((level>=0)&&(level<finest_level)) {

  int spectral_override=1;

  if (face_flag==1) {
   avgDownMacState(spectral_override);
  } else if (face_flag==0) {
   // do nothing
  } else
   BoxLib::Error("face_flag invalid 8");
 
  avgDown(LS_Type,0,nmat,0);
  MOFavgDown();
     // velocity and pressure
  avgDown(State_Type,0,num_materials_vel*(BL_SPACEDIM+1),1);
  int scomp_den=num_materials_vel*(BL_SPACEDIM+1);
  avgDown(State_Type,scomp_den,num_state_material*nmat,1);
  if ((num_materials_viscoelastic>=1)&&(num_materials_viscoelastic<=nmat)) {
    // spectral_override==0 => always low order
   avgDown(Tensor_Type,0,num_materials_viscoelastic*NUM_TENSOR_TYPE,0);
  } else if (num_materials_viscoelastic==0) {
   // do nothing
  } else
   BoxLib::Error("num_materials_viscoelastic invalid");

 } else if (level==finest_level) {
  // do nothing
 } else
  BoxLib::Error("level invalid23");

}  // subroutine split_scalar_advection



// Lagrangian solid info lives at t=t^n 
// order_direct_split=base_step mod 2
// must go from finest level to coarsest.
void 
NavierStokes::unsplit_scalar_advection() { 
 
 bool use_tiling=ns_tiling;

 int finest_level=parent->finestLevel();
 int nmat=num_materials;
 int nten=( (nmat-1)*(nmat-1)+nmat-1 )/2;

 int bfact=parent->Space_blockingFactor(level);
 int bfact_f=bfact;
 if (level<finest_level)
  bfact_f=parent->Space_blockingFactor(level+1);

 if (num_materials_vel!=1)
  BoxLib::Error("num_materials_vel invalid");

 int nsolve=1;
 int nsolveMM=nsolve;
 int nsolveMM_FACE=nsolveMM;

 if (num_state_base!=2)
  BoxLib::Error("num_state_base invalid");

 if ((order_direct_split==0)||
     (order_direct_split==1)) {
  // do nothing
 } else
  BoxLib::Error("order_direct_split invalid");

 if ((SDC_outer_sweeps>=0)&&
     (SDC_outer_sweeps<ns_time_order)) {
  // do nothing
 } else {
  std::cout << "SDC_outer_sweeps= " << SDC_outer_sweeps << '\n';
  BoxLib::Error("SDC_outer_sweeps invalid");
 }

 if (dir_absolute_direct_split==0) {
  // do nothing
 } else
  BoxLib::Error("dir_absolute_direct_split invalid");

 int ngrow=1;
 int mac_grow=1; 
 int ngrow_mac_old=0;

 if (face_flag==0) {
  // do nothing
 } else if (face_flag==1) {
  ngrow++;
  mac_grow++;
  ngrow_mac_old++;
 } else
  BoxLib::Error("face_flag invalid 4");

  // vof,ref centroid,order,slope,intercept  x nmat
 VOF_Recon_resize(ngrow,SLOPE_RECON_MF);
 debug_ngrow(SLOPE_RECON_MF,ngrow,36);
 resize_maskfiner(ngrow,MASKCOEF_MF);
 debug_ngrow(MASKCOEF_MF,ngrow,36);
 debug_ngrow(VOF_LS_PREV_TIME_MF,1,38);
 if (localMF[VOF_LS_PREV_TIME_MF]->nComp()!=2*nmat)
  BoxLib::Error("vof ls prev time invalid ncomp");

 MultiFab& S_new=get_new_data(State_Type,slab_step+1);
 int ncomp_state=S_new.nComp();
 if (ncomp_state!=num_materials_vel*(BL_SPACEDIM+1)+
     nmat*(num_state_material+ngeom_raw)+1)
  BoxLib::Error("ncomp_state invalid");

 MultiFab& LS_new=get_new_data(LS_Type,slab_step+1);
 if (LS_new.nComp()!=nmat*(BL_SPACEDIM+1))
  BoxLib::Error("LS_new ncomp invalid");

 const Real* dx = geom.CellSize();
 const Box& domain = geom.Domain();
 const int* domlo = domain.loVect();
 const int* domhi = domain.hiVect();

 int scomp_mofvars=num_materials_vel*(BL_SPACEDIM+1)+nmat*num_state_material;
 Array<int> dombc(2*BL_SPACEDIM);
 const BCRec& descbc = get_desc_lst()[State_Type].getBC(scomp_mofvars);
 const int* b_rec=descbc.vect();
 for (int m=0;m<2*BL_SPACEDIM;m++)
  dombc[m]=b_rec[m];

 vel_time_slab=prev_time_slab;
 if (divu_outer_sweeps==0) 
  vel_time_slab=prev_time_slab;
 else if (divu_outer_sweeps>0)
  vel_time_slab=cur_time_slab;
 else
  BoxLib::Error("divu_outer_sweeps invalid");

  // in: unsplit_scalar_advection
 getStateDen_localMF(DEN_RECON_MF,ngrow,advect_time_slab);

 int local_tensor_type=State_Type;
 int local_tensor_mf=DEN_RECON_MF;

 if ((num_materials_viscoelastic>=1)&&(num_materials_viscoelastic<=nmat)) {
  local_tensor_type=Tensor_Type;
  local_tensor_mf=TENSOR_RECON_MF;
  getStateTensor_localMF(TENSOR_RECON_MF,1,0,
   num_materials_viscoelastic*NUM_TENSOR_TYPE,advect_time_slab);
 } else if (num_materials_viscoelastic==0) {
  // do nothing
 } else
  BoxLib::Error("num_materials_viscoelastic invalid");

 MultiFab& Tensor_new=get_new_data(local_tensor_type,slab_step+1);

 getStateDist_localMF(LS_RECON_MF,1,advect_time_slab,10);

   // the pressure from before will be copied to the new pressure.
   // VELADVECT_MF and UMACOLD_MF deleted at the end of this routine.
 getState_localMF(VELADVECT_MF,ngrow,0,
  num_materials_vel*(BL_SPACEDIM+1),
  advect_time_slab); 

 for (int dir=0;dir<BL_SPACEDIM;dir++) {
  getStateMAC_localMF(UMACOLD_MF+dir,ngrow_mac_old,dir,
    0,nsolveMM_FACE,advect_time_slab);
 } // dir

 int unsplit_displacement=1;
  // MAC_VELOCITY_MF deleted towards the end of 
  //   NavierStokes::nonlinear_advection
 prepare_displacement(mac_grow,unsplit_displacement);

 int vofrecon_ncomp=localMF[SLOPE_RECON_MF]->nComp();
 if (vofrecon_ncomp!=nmat*ngeom_recon)
   BoxLib::Error("recon ncomp bust");

 int den_recon_ncomp=localMF[DEN_RECON_MF]->nComp();
 if (den_recon_ncomp!=num_state_material*nmat)
   BoxLib::Error("den_recon invalid");

 int LS_recon_ncomp=localMF[LS_RECON_MF]->nComp();
 if (LS_recon_ncomp!=nmat*(1+BL_SPACEDIM))
   BoxLib::Error("LS_recon invalid");

 debug_ngrow(LS_RECON_MF,1,40);

 resize_mask_nbr(ngrow);
 debug_ngrow(MASK_NBR_MF,ngrow,28); 

 MultiFab* umac_new[BL_SPACEDIM];
 for (int dir=0;dir<BL_SPACEDIM;dir++) {
  umac_new[dir]=&get_new_data(Umac_Type+dir,slab_step+1);
 }

 int ngrid=grids.size();

 int nc_conserve=BL_SPACEDIM+nmat*num_state_material;
 MultiFab* conserve=new MultiFab(grids,nc_conserve,ngrow,dmap,Fab_allocate);

 MultiFab* mask_unsplit=new MultiFab(grids,1,1,dmap,Fab_allocate);

 int iden_base=BL_SPACEDIM;
 int itensor_base=iden_base+nmat*num_state_material;
 int imof_base=itensor_base+num_materials_viscoelastic*NUM_TENSOR_TYPE;
 int iLS_base=imof_base+nmat*ngeom_raw;
 int iFtarget_base=iLS_base+nmat;
 int iden_mom_base=iFtarget_base+nmat;
 int nc_bucket=iden_mom_base+nmat;

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(*localMF[SLOPE_RECON_MF],use_tiling); mfi.isValid(); ++mfi) {
  BL_ASSERT(grids[mfi.index()] == mfi.validbox());
  const int gridno = mfi.index();
  const Box& tilegrid = mfi.tilebox();
  const Box& fabgrid = grids[gridno];
  const int* tilelo=tilegrid.loVect();
  const int* tilehi=tilegrid.hiVect();
  const int* fablo=fabgrid.loVect();
  const int* fabhi=fabgrid.hiVect();

  FArrayBox& consfab=(*conserve)[mfi];
  FArrayBox& denfab=(*localMF[DEN_RECON_MF])[mfi];
  FArrayBox& velfab=(*localMF[VELADVECT_MF])[mfi];

  int normdir_unsplit=0; // not used

  FORT_BUILD_CONSERVE( 
   &iden_base,
   override_density.dataPtr(),
   temperature_primitive_variable.dataPtr(),
   consfab.dataPtr(),ARLIM(consfab.loVect()),ARLIM(consfab.hiVect()),
   denfab.dataPtr(),ARLIM(denfab.loVect()),ARLIM(denfab.hiVect()),
   velfab.dataPtr(),ARLIM(velfab.loVect()),ARLIM(velfab.hiVect()),
   tilelo,tilehi,
   fablo,fabhi,&bfact,
   &nmat,&ngrow,
   &normdir_unsplit,
   &nc_conserve,
   &den_recon_ncomp);
 }  // mfi
} // omp
 ParallelDescriptor::Barrier();

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(*localMF[SLOPE_RECON_MF],use_tiling); mfi.isValid(); ++mfi) {
  BL_ASSERT(grids[mfi.index()] == mfi.validbox());
  const int gridno = mfi.index();
  const Box& tilegrid = mfi.tilebox();
  const Box& fabgrid = grids[gridno];
  const int* tilelo=tilegrid.loVect();
  const int* tilehi=tilegrid.hiVect();
  const int* fablo=fabgrid.loVect();
  const int* fabhi=fabgrid.hiVect();

  const Real* xlo = grid_loc[gridno].lo();

  FArrayBox& maskfab=(*mask_unsplit)[mfi];
  FArrayBox& vofls0fab=(*localMF[VOF_LS_PREV_TIME_MF])[mfi];

  // in: LEVELSET_3D.F90
  // 0=directionally split
  // 1=unsplit everywhere
  // 2=unsplit in incompressible zones
  // 3=unsplit in fluid cells that neighbor a prescribed solid cell
  FORT_BUILD_MASK_UNSPLIT( 
   &unsplit_flag,
   &make_interface_incomp,
   xlo,dx,
   maskfab.dataPtr(),ARLIM(maskfab.loVect()),ARLIM(maskfab.hiVect()),
   vofls0fab.dataPtr(),ARLIM(vofls0fab.loVect()),ARLIM(vofls0fab.hiVect()),
   tilelo,tilehi,
   fablo,fabhi,
   &bfact,
   &nmat,
   &level,&finest_level);
 }  // mfi
} // omp
 ParallelDescriptor::Barrier();

 MultiFab* xvof[BL_SPACEDIM];
 MultiFab* xvel[BL_SPACEDIM]; // xvel
 MultiFab* side_bucket_mom[BL_SPACEDIM];
 MultiFab* side_bucket_mass[BL_SPACEDIM];

  // (dir-1)*2*nmat + (side-1)*nmat + im
 int nrefine_vof=2*nmat*BL_SPACEDIM;

  // (veldir-1)*2*nmat*sdim + (side-1)*nmat*sdim + (im-1)*sdim+dir
 int nrefine_cen=2*nmat*BL_SPACEDIM*BL_SPACEDIM;

 if (face_flag==0) {

  for (int dir=0;dir<BL_SPACEDIM;dir++) {
   xvof[dir]=localMF[SLOPE_RECON_MF];
   xvel[dir]=localMF[SLOPE_RECON_MF];
   side_bucket_mom[dir]=localMF[SLOPE_RECON_MF];
   side_bucket_mass[dir]=localMF[SLOPE_RECON_MF];
  }

 } else if (face_flag==1) {

  MultiFab* vofF=new MultiFab(grids,nrefine_vof,ngrow,dmap,Fab_allocate);

   // linear expansion: 
   // 1. find slopes u_x
   // 2. (rho u)(x)_m = rho_m (u + u_x (x - x_m_centroid))
  MultiFab* cenF=new MultiFab(grids,nrefine_cen,ngrow,dmap,Fab_allocate);
  MultiFab* massF=new MultiFab(grids,nrefine_vof,ngrow,dmap,Fab_allocate);

#ifdef _OPENMP
#pragma omp parallel
#endif
{
  for (MFIter mfi(*vofF,use_tiling); mfi.isValid(); ++mfi) {
   BL_ASSERT(grids[mfi.index()] == mfi.validbox());
   const int gridno = mfi.index();
   const Box& tilegrid = mfi.tilebox();
   const Box& fabgrid = grids[gridno];
   const int* tilelo=tilegrid.loVect();
   const int* tilehi=tilegrid.hiVect();
   const int* fablo=fabgrid.loVect();
   const int* fabhi=fabgrid.hiVect();

   const Real* xlo = grid_loc[gridno].lo();

   FArrayBox& slopefab=(*localMF[SLOPE_RECON_MF])[mfi];
   FArrayBox& denstatefab=(*localMF[DEN_RECON_MF])[mfi];

   FArrayBox& vofFfab=(*vofF)[mfi];
   FArrayBox& cenFfab=(*cenF)[mfi];
   FArrayBox& massFfab=(*massF)[mfi];

   int tessellate=0;

   int tid=ns_thread();

    // centroid in absolute coordinates.
   FORT_BUILD_SEMIREFINEVOF(
    &tid,
    &tessellate,
    &ngrow,
    &nrefine_vof,
    &nrefine_cen,
    &nten,
    spec_material_id.dataPtr(),
    mass_fraction_id.dataPtr(),
    species_evaporation_density.dataPtr(),
    cavitation_vapor_density.dataPtr(),
    cavitation_species.dataPtr(),
    override_density.dataPtr(),
    xlo,dx,
    slopefab.dataPtr(),
    ARLIM(slopefab.loVect()),ARLIM(slopefab.hiVect()),
    denstatefab.dataPtr(),
    ARLIM(denstatefab.loVect()),ARLIM(denstatefab.hiVect()),
    vofFfab.dataPtr(),ARLIM(vofFfab.loVect()),ARLIM(vofFfab.hiVect()),
    cenFfab.dataPtr(),ARLIM(cenFfab.loVect()),ARLIM(cenFfab.hiVect()),
    massFfab.dataPtr(),ARLIM(massFfab.loVect()),ARLIM(massFfab.hiVect()),
    tilelo,tilehi,
    fablo,fabhi,
    &bfact,
    &nmat,
    &level,&finest_level);
  }  // mfi
}// omp
  ParallelDescriptor::Barrier();

  for (int dir=0;dir<BL_SPACEDIM;dir++) {
   xvof[dir]=new MultiFab(state[Umac_Type+dir].boxArray(),nmat,
     ngrow_mac_old,dmap,Fab_allocate);

   xvel[dir]=new MultiFab(state[Umac_Type+dir].boxArray(),1,
     ngrow_mac_old,dmap,Fab_allocate);

    //ncomp=2 ngrow=1
   side_bucket_mom[dir]=new MultiFab(grids,2,1,dmap,Fab_allocate);
    //scomp=0 ncomp=2 ngrow=1
   side_bucket_mom[dir]->setVal(0.0,0,2,1);

    //ncomp=2 ngrow=1
   side_bucket_mass[dir]=new MultiFab(grids,2,1,dmap,Fab_allocate);
    //scomp=0 ncomp=2 ngrow=1
   side_bucket_mass[dir]->setVal(0.0,0,2,1);
  }  // dir 

  for (int veldir=1;veldir<=BL_SPACEDIM;veldir++) {

#ifdef _OPENMP
#pragma omp parallel
#endif
{
   for (MFIter mfi(*localMF[SLOPE_RECON_MF],use_tiling); 
        mfi.isValid(); ++mfi) {
    BL_ASSERT(grids[mfi.index()] == mfi.validbox());
    const int gridno = mfi.index();
    const Box& tilegrid = mfi.tilebox();
    const Box& fabgrid = grids[gridno];
    const int* tilelo=tilegrid.loVect();
    const int* tilehi=tilegrid.hiVect();
    const int* fablo=fabgrid.loVect();
    const int* fabhi=fabgrid.hiVect();

    const Real* xlo = grid_loc[gridno].lo();

    FArrayBox& xmac_old=(*localMF[UMACOLD_MF+veldir-1])[mfi];
    FArrayBox& xvoffab=(*xvof[veldir-1])[mfi];
    FArrayBox& xvelfab=(*xvel[veldir-1])[mfi]; // xvelleft,xvelright
    FArrayBox& vofFfab=(*vofF)[mfi];
    FArrayBox& cenFfab=(*cenF)[mfi];
    int unsplit_advection=1;
    int normdir_unsplit=0; // not used

    FORT_BUILD_MACVOF( 
     &unsplit_advection,
     &nsolveMM_FACE,
     &level,
     &finest_level,
     &normdir_unsplit,
     &nrefine_vof,
     &nrefine_cen,
     vofFfab.dataPtr(),ARLIM(vofFfab.loVect()),ARLIM(vofFfab.hiVect()),
     cenFfab.dataPtr(),ARLIM(cenFfab.loVect()),ARLIM(cenFfab.hiVect()),
     xmac_old.dataPtr(),ARLIM(xmac_old.loVect()),ARLIM(xmac_old.hiVect()),
     xvoffab.dataPtr(),ARLIM(xvoffab.loVect()),ARLIM(xvoffab.hiVect()),
     xvelfab.dataPtr(),
     ARLIM(xvelfab.loVect()),ARLIM(xvelfab.hiVect()),
     xvelfab.dataPtr(),  // xvelslopefab
     ARLIM(xvelfab.loVect()),ARLIM(xvelfab.hiVect()),
     xlo,dx,
     tilelo,tilehi,
     fablo,fabhi,
     &bfact,
     &nmat,&ngrow, 
     &ngrow_mac_old,&veldir);
   }  // mfi
}// omp
   ParallelDescriptor::Barrier();

  } // veldir=1..sdim

  delete vofF;
  delete cenF;
  delete massF;

 } else
  BoxLib::Error("face_flag invalid 5");

 Array<int> nprocessed;
 nprocessed.resize(thread_class::nthreads);
 for (int tid=0;tid<thread_class::nthreads;tid++) {
  nprocessed[tid]=0.0;
 }

 Real profile_time_start=0.0;
 if (profile_debug==1) {
  profile_time_start=ParallelDescriptor::second();
 }

 int ntensor=Tensor_new.nComp();

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(S_new,use_tiling); mfi.isValid(); ++mfi) {
  BL_ASSERT(grids[mfi.index()] == mfi.validbox());

  const int gridno = mfi.index();
  const Box& tilegrid = mfi.tilebox();
  const Box& fabgrid = grids[gridno];
  const int* tilelo=tilegrid.loVect();
  const int* tilehi=tilegrid.hiVect();
  const int* fablo=fabgrid.loVect();
  const int* fabhi=fabgrid.hiVect();

  const Real* xlo = grid_loc[gridno].lo();

    // mask=tag if not covered by level+1 or outside the domain.
    // mask=1-tag if covered by level+1 and inside the domain.
    // NavierStokes::maskfiner  (clear_phys_boundary==0)
  FArrayBox& maskfab=(*localMF[MASKCOEF_MF])[mfi];
   // mask_nbr:
   // (1) =1 interior  =1 fine-fine ghost in domain  =0 otherwise
   // (2) =1 interior  =0 otherwise
   // (3) =1 interior+ngrow-1  =0 otherwise
   // (4) =1 interior+ngrow    =0 otherwise
  FArrayBox& masknbrfab=(*localMF[MASK_NBR_MF])[mfi];

  FArrayBox& maskunsplitfab=(*mask_unsplit)[mfi];

  FArrayBox& unode=(*localMF[MAC_VELOCITY_MF])[mfi];
  if (unode.nComp()!=nsolveMM_FACE*BL_SPACEDIM)
   BoxLib::Error("unode has invalid ncomp");
  FArrayBox& vnode=(*localMF[MAC_VELOCITY_MF+1])[mfi];
  if (vnode.nComp()!=nsolveMM_FACE*BL_SPACEDIM)
   BoxLib::Error("vnode has invalid ncomp");
  FArrayBox& wnode=(*localMF[MAC_VELOCITY_MF+BL_SPACEDIM-1])[mfi];
  if (wnode.nComp()!=nsolveMM_FACE*BL_SPACEDIM)
   BoxLib::Error("wnode has invalid ncomp");

    // this is the original data
  FArrayBox& LSfab=(*localMF[LS_RECON_MF])[mfi];
  FArrayBox& denfab=(*localMF[DEN_RECON_MF])[mfi];
  FArrayBox& tenfab=(*localMF[local_tensor_mf])[mfi];
  FArrayBox& velfab=(*localMF[VELADVECT_MF])[mfi];

    // this is the slope data
  FArrayBox& vofslopefab=(*localMF[SLOPE_RECON_MF])[mfi];

  FArrayBox& vofls0fab=(*localMF[VOF_LS_PREV_TIME_MF])[mfi];

  int dencomp=num_materials_vel*(BL_SPACEDIM+1);
  int mofcomp=dencomp+nmat*num_state_material;
  int errcomp=mofcomp+nmat*ngeom_raw;

  Array<int> velbc=getBCArray(State_Type,gridno,0,BL_SPACEDIM);

     // this is the result
  FArrayBox& destfab=S_new[mfi];
  FArrayBox& tennewfab=Tensor_new[mfi];
  FArrayBox& LSdestfab=LS_new[mfi];

  FArrayBox& consfab=(*conserve)[mfi];

  FArrayBox& xvelfab=(*xvel[0])[mfi]; 
  FArrayBox& yvelfab=(*xvel[1])[mfi];
  FArrayBox& zvelfab=(*xvel[BL_SPACEDIM-1])[mfi];

  FArrayBox& xmomside=(*side_bucket_mom[0])[mfi];
  FArrayBox& ymomside=(*side_bucket_mom[1])[mfi];
  FArrayBox& zmomside=(*side_bucket_mom[BL_SPACEDIM-1])[mfi];

  FArrayBox& xmassside=(*side_bucket_mass[0])[mfi];
  FArrayBox& ymassside=(*side_bucket_mass[1])[mfi];
  FArrayBox& zmassside=(*side_bucket_mass[BL_SPACEDIM-1])[mfi];

  FArrayBox& ucellfab=(*localMF[CELL_VELOCITY_MF])[mfi];

  prescribed_vel_time_slab=0.5*(prev_time_slab+cur_time_slab);

  int tid=ns_thread();

   // solid distance function and solid moments are not modified.
   // solid temperature is modified only if solidheat_flag==0.
  FORT_VFRAC_UNSPLIT(
   &unsplit_flag,  
   &nsolveMM_FACE,
   &nprocessed[tid],
   &tid,
   &make_interface_incomp,
   added_weight.dataPtr(),
   density_floor.dataPtr(),
   density_ceiling.dataPtr(),
   &solidheat_flag, //0==diffuse in solid 1==dirichlet 2==neumann
   temperature_primitive_variable.dataPtr(),
   &dencomp,&mofcomp,&errcomp,
   latent_heat.dataPtr(),
   freezing_model.dataPtr(),
   distribute_from_target.dataPtr(),
   &nten,
   &face_flag,
   override_density.dataPtr(),
   velbc.dataPtr(),
   tilelo,tilehi,
   fablo,fabhi,
   &bfact,
   &bfact_f,
   &dt_slab, // VFRAC_UNSPLIT
   &prev_time_slab,
   &prescribed_vel_time_slab,
     // this is the original data
   LSfab.dataPtr(),
   ARLIM(LSfab.loVect()),ARLIM(LSfab.hiVect()),
   denfab.dataPtr(),
   ARLIM(denfab.loVect()),ARLIM(denfab.hiVect()),
   tenfab.dataPtr(),
   ARLIM(tenfab.loVect()),ARLIM(tenfab.hiVect()),
   velfab.dataPtr(),
   ARLIM(velfab.loVect()),ARLIM(velfab.hiVect()),
     // slope data
   vofslopefab.dataPtr(),
   ARLIM(vofslopefab.loVect()),ARLIM(vofslopefab.hiVect()),
     // this is the result
   destfab.dataPtr(),
   ARLIM(destfab.loVect()),ARLIM(destfab.hiVect()),
   tennewfab.dataPtr(),
   ARLIM(tennewfab.loVect()),ARLIM(tennewfab.hiVect()),
   LSdestfab.dataPtr(),
   ARLIM(LSdestfab.loVect()),ARLIM(LSdestfab.hiVect()),
    // other vars.
   ucellfab.dataPtr(),ARLIM(ucellfab.loVect()),ARLIM(ucellfab.hiVect()),
   vofls0fab.dataPtr(),ARLIM(vofls0fab.loVect()),ARLIM(vofls0fab.hiVect()),
   maskfab.dataPtr(),ARLIM(maskfab.loVect()),ARLIM(maskfab.hiVect()),
   masknbrfab.dataPtr(),
   ARLIM(masknbrfab.loVect()),ARLIM(masknbrfab.hiVect()),
   maskunsplitfab.dataPtr(),
   ARLIM(maskunsplitfab.loVect()),ARLIM(maskunsplitfab.hiVect()),
   unode.dataPtr(),ARLIM(unode.loVect()),ARLIM(unode.hiVect()),
   vnode.dataPtr(),ARLIM(vnode.loVect()),ARLIM(vnode.hiVect()),
   wnode.dataPtr(),ARLIM(wnode.loVect()),ARLIM(wnode.hiVect()),
   xlo,dx,
    // local variables
   consfab.dataPtr(),ARLIM(consfab.loVect()),ARLIM(consfab.hiVect()),
    // xvelleft,xvelright
   xvelfab.dataPtr(),ARLIM(xvelfab.loVect()),ARLIM(xvelfab.hiVect()),
   yvelfab.dataPtr(),ARLIM(yvelfab.loVect()),ARLIM(yvelfab.hiVect()),
   zvelfab.dataPtr(),ARLIM(zvelfab.loVect()),ARLIM(zvelfab.hiVect()),
   xmomside.dataPtr(),ARLIM(xmomside.loVect()),ARLIM(xmomside.hiVect()),
   ymomside.dataPtr(),ARLIM(ymomside.loVect()),ARLIM(ymomside.hiVect()),
   zmomside.dataPtr(),ARLIM(zmomside.loVect()),ARLIM(zmomside.hiVect()),
   xmassside.dataPtr(),ARLIM(xmassside.loVect()),ARLIM(xmassside.hiVect()),
   ymassside.dataPtr(),ARLIM(ymassside.loVect()),ARLIM(ymassside.hiVect()),
   zmassside.dataPtr(),ARLIM(zmassside.loVect()),ARLIM(zmassside.hiVect()),
   &ngrow,
   &ngrow_mac_old,
   &nc_conserve,
   &iden_base,
   &nmat,
   &vofrecon_ncomp,
   &den_recon_ncomp,
   &ncomp_state,
   &ntensor,
   &nc_bucket,
   &nrefine_vof,
   &verbose,
   &gridno,&ngrid,
   &level,
   &finest_level,
   dombc.dataPtr(), 
   domlo,domhi);

 }  // mfi
} // omp
 for (int tid=1;tid<thread_class::nthreads;tid++) {
  nprocessed[0]+=nprocessed[tid];
 }
 ParallelDescriptor::Barrier();
 ParallelDescriptor::ReduceIntSum(nprocessed[0]);

 if (profile_debug==1) {
  Real profile_time_end=ParallelDescriptor::second();
  if (ParallelDescriptor::IOProcessor()) {
   std::cout << "level= " << level << '\n';
   std::cout << "nprocessed= " << nprocessed[0] << '\n';
   std::cout << "profile VFRAC_SPLIT time = " << 
     profile_time_end-profile_time_start << '\n';
  }
 }

 if (face_flag==0) {
  // do nothing
 } else if (face_flag==1) {


#ifdef _OPENMP
#pragma omp parallel
#endif
{
  for (MFIter mfi(S_new,use_tiling); mfi.isValid(); ++mfi) {
   BL_ASSERT(grids[mfi.index()] == mfi.validbox());

   const int gridno = mfi.index();
   const Box& tilegrid = mfi.tilebox();
   const Box& fabgrid = grids[gridno];
   const int* tilelo=tilegrid.loVect();
   const int* tilehi=tilegrid.hiVect();
   const int* fablo=fabgrid.loVect();
   const int* fabhi=fabgrid.hiVect();

   const Real* xlo = grid_loc[gridno].lo();

    // mask=tag if not covered by level+1 or outside the domain.
   FArrayBox& maskfab=(*localMF[MASKCOEF_MF])[mfi];

   FArrayBox& xmomside=(*side_bucket_mom[0])[mfi];
   FArrayBox& ymomside=(*side_bucket_mom[1])[mfi];
   FArrayBox& zmomside=(*side_bucket_mom[BL_SPACEDIM-1])[mfi];

   FArrayBox& xmassside=(*side_bucket_mass[0])[mfi];
   FArrayBox& ymassside=(*side_bucket_mass[1])[mfi];
   FArrayBox& zmassside=(*side_bucket_mass[BL_SPACEDIM-1])[mfi];

   FArrayBox& xmac_new=(*umac_new[0])[mfi];
   FArrayBox& ymac_new=(*umac_new[1])[mfi];
   FArrayBox& zmac_new=(*umac_new[BL_SPACEDIM-1])[mfi];

   FArrayBox& maskunsplitfab=(*mask_unsplit)[mfi];

   int normdir_unsplit=0; // not used

   FORT_BUILD_NEWMAC(
    &nsolveMM_FACE,
    &normdir_unsplit,
    tilelo,tilehi,
    fablo,fabhi,&bfact,
    xmomside.dataPtr(),ARLIM(xmomside.loVect()),ARLIM(xmomside.hiVect()),
    ymomside.dataPtr(),ARLIM(ymomside.loVect()),ARLIM(ymomside.hiVect()),
    zmomside.dataPtr(),ARLIM(zmomside.loVect()),ARLIM(zmomside.hiVect()),
    xmassside.dataPtr(),ARLIM(xmassside.loVect()),ARLIM(xmassside.hiVect()),
    ymassside.dataPtr(),ARLIM(ymassside.loVect()),ARLIM(ymassside.hiVect()),
    zmassside.dataPtr(),ARLIM(zmassside.loVect()),ARLIM(zmassside.hiVect()),
    xmac_new.dataPtr(),ARLIM(xmac_new.loVect()),ARLIM(xmac_new.hiVect()),
    ymac_new.dataPtr(),ARLIM(ymac_new.loVect()),ARLIM(ymac_new.hiVect()),
    zmac_new.dataPtr(),ARLIM(zmac_new.loVect()),ARLIM(zmac_new.hiVect()),
    maskfab.dataPtr(),ARLIM(maskfab.loVect()),ARLIM(maskfab.hiVect()),
    maskunsplitfab.dataPtr(),
    ARLIM(maskunsplitfab.loVect()),ARLIM(maskunsplitfab.hiVect()),
    xlo,dx,
    &nmat,
    &level,
    &finest_level);

  }  // mfi
} // omp
  ParallelDescriptor::Barrier();
 } else
  BoxLib::Error("face_flag invalid 6");

 delete conserve;
 delete mask_unsplit;
 
 if (face_flag==0) {
  // do nothing
 } else if (face_flag==1) {
  for (int dir=0;dir<BL_SPACEDIM;dir++) {
   delete xvof[dir];
   delete xvel[dir];
   delete side_bucket_mom[dir];
   delete side_bucket_mass[dir];
  }
 } else
  BoxLib::Error("face_flag invalid 7");

 delete_localMF(VELADVECT_MF,1);
 delete_localMF(DEN_RECON_MF,1);

 if ((num_materials_viscoelastic>=1)&&(num_materials_viscoelastic<=nmat)) {
  delete_localMF(TENSOR_RECON_MF,1);
 } else if (num_materials_viscoelastic==0) {
  // do nothing
 } else
  BoxLib::Error("num_materials_viscoelastic invalid");
 
 delete_localMF(LS_RECON_MF,1);
 
 if (stokes_flow==0) {
  // do nothing
 } else if (stokes_flow==1) {
  MultiFab& S_old=get_new_data(State_Type,slab_step);
  MultiFab::Copy(S_new,S_old,0,0,BL_SPACEDIM,1);
  for (int dir=0;dir<BL_SPACEDIM;dir++) {
   MultiFab::Copy(*umac_new[dir],*localMF[UMACOLD_MF+dir],0,0,1,0);
  }
 } else
  BoxLib::Error("stokes_flow invalid");

 delete_localMF(UMACOLD_MF,BL_SPACEDIM);
 
 if ((level>=0)&&(level<finest_level)) {

  int spectral_override=1;

  if (face_flag==1) {
   avgDownMacState(spectral_override);
  } else if (face_flag==0) {
   // do nothing
  } else
   BoxLib::Error("face_flag invalid 8");
 
  avgDown(LS_Type,0,nmat,0);
  MOFavgDown();
     // velocity and pressure
  avgDown(State_Type,0,num_materials_vel*(BL_SPACEDIM+1),1);
  int scomp_den=num_materials_vel*(BL_SPACEDIM+1);
  avgDown(State_Type,scomp_den,num_state_material*nmat,1);
  if ((num_materials_viscoelastic>=1)&&(num_materials_viscoelastic<=nmat)) {
    // spectral_override==0 => always low order
   avgDown(Tensor_Type,0,num_materials_viscoelastic*NUM_TENSOR_TYPE,0);
  } else if (num_materials_viscoelastic==0) {
   // do nothing
  } else
   BoxLib::Error("num_materials_viscoelastic invalid");

 } else if (level==finest_level) {
  // do nothing
 } else
  BoxLib::Error("level invalid23");

}  // subroutine unsplit_scalar_advection


void
NavierStokes::errorEst (TagBoxArray& tags,int clearval,int tagval,
 int n_error_buf,int ngrow)
{
 
 const int max_level = parent->maxLevel();
 if (level>=max_level)
  BoxLib::Error("level too big in errorEst");

 int bfact=parent->Space_blockingFactor(level);

 if (n_error_buf<2)
  BoxLib::Error("amr.n_error_buf<2");

 const int*  domain_lo = geom.Domain().loVect();
 const int*  domain_hi = geom.Domain().hiVect();
 const Real* dx        = geom.CellSize();
 const Real* prob_lo   = geom.ProbLo();

 MultiFab* mf = derive("mag_error",0); // the last component of state.

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(*mf); mfi.isValid(); ++mfi) {
  BL_ASSERT(grids[mfi.index()] == mfi.validbox());
  const int gridno = mfi.index();
  const Box& tilegrid = mfi.tilebox();
  const Box& fabgrid = grids[gridno];
  const int* tilelo=tilegrid.loVect(); 
  const int* tilehi=tilegrid.hiVect();
  const int* fablo=fabgrid.loVect();
  const int* fabhi=fabgrid.hiVect();
  const Real* xlo = grid_loc[gridno].lo();

   // itags: Array<int> ("domain"=box dimensions (not whole domain))
   // itags=TagBox::CLEAR in "tags"
   // then itags = older tag values.
  Array<int>  itags;
  itags             = tags[mfi.index()].tags();
  int*        tptr  = itags.dataPtr();
  const int*  tlo   = tags[mfi.index()].box().loVect();
  const int*  thi   = tags[mfi.index()].box().hiVect();
  Real*       dat   = (*mf)[mfi].dataPtr();
  const int*  dlo   = (*mf)[mfi].box().loVect();
  const int*  dhi   = (*mf)[mfi].box().hiVect();
  const int   ncomp = (*mf)[mfi].nComp();

  FORT_VFRACERROR(
    tptr, ARLIM(tlo), ARLIM(thi), 
    &tagval, &clearval, 
    dat, ARLIM(dlo), ARLIM(dhi),
    tilelo,tilehi,
    fablo,fabhi,&bfact,
    &ncomp, 
    domain_lo, domain_hi,
    dx, xlo, prob_lo, 
    &upper_slab_time, 
    &level,
    &max_level,
    &max_level_two_materials,
    &nblocks,
    xblocks.dataPtr(),yblocks.dataPtr(),zblocks.dataPtr(),
    rxblocks.dataPtr(),ryblocks.dataPtr(),rzblocks.dataPtr(),
    &ncoarseblocks,
    xcoarseblocks.dataPtr(),ycoarseblocks.dataPtr(),
    zcoarseblocks.dataPtr(),
    rxcoarseblocks.dataPtr(),rycoarseblocks.dataPtr(),
    rzcoarseblocks.dataPtr());
  tags[mfi.index()].tags(itags);
 } // mfi
} // omp
 ParallelDescriptor::Barrier();
 delete mf;
} // subroutine errorEst

// called from volWgtSumALL
//
// compute (-pI+2\mu D)dot n_solid for cut cells.
//
// F=mA  rho du/dt = div sigma + rho g
// integral rho du/dt = integral (div sigma + rho g)
// du/dt=(integral_boundary sigma dot n)/mass  + g
// torque_axis=r_axis x F=r_axis x (m alpha_axis x r_axis)   
// alpha=angular acceleration  (1/s^2)
// torque_axis=I_axis alpha_axis  
// I_axis=integral rho r_axis x (e_axis x r_axis)   I=moment of inertia
// I_axis=integral rho r_axis^{2}
// r_axis=(x-x_{COM})_{axis}  y_axis=y-(y dot e_axis)e_axis
// torque_axis=integral r_axis x (div sigma + rho g)=
//   integral_boundary r_axis x sigma dot n + integral r_axis x rho g
// integrated_quantities:
//  1..3  : F=integral_boundary sigma dot n + integral rho g
//  4..6  : torque=integral_boundary r x sigma dot n + integral r x rho g  
//  7..9  : moments of inertia
//  10..12: integral rho x
//  13    : integral rho
void 
NavierStokes::GetDragALL(Array<Real>& integrated_quantities) {

 int finest_level=parent->finestLevel();

 if ((SDC_outer_sweeps>=0)&&
     (SDC_outer_sweeps<ns_time_order)) {
  // do nothing
 } else
  BoxLib::Error("SDC_outer_sweeps invalid");

 if (integrated_quantities.size()!=13)
  BoxLib::Error("invalid size for integrated_quantities");

 for (int iq=0;iq<13;iq++)
  integrated_quantities[iq]=0.0;

  // in: NavierStokes::GetDragALL
 allocate_levelsetLO_ALL(2,LEVELPC_MF);

 int do_alloc=1;
 init_gradu_tensorALL(HOLD_VELOCITY_DATA_MF,do_alloc,CELLTENSOR_MF,
  FACETENSOR_MF);

 for (int isweep=0;isweep<2;isweep++) {
  for (int ilev=level;ilev<=finest_level;ilev++) {
   NavierStokes& ns_level=getLevel(ilev);
   ns_level.GetDrag(integrated_quantities,isweep);
  }
 }

 delete_array(CELLTENSOR_MF);
 delete_array(FACETENSOR_MF);

  // isweep
 if (verbose>0) {
  if (ParallelDescriptor::IOProcessor()) {
   for (int iq=0;iq<13;iq++)
    std::cout << "GetDrag  iq= " << iq << " integrated_quantities= " <<
     integrated_quantities[iq] << '\n';
   Real mass=integrated_quantities[12];
   if (mass>0.0) {
    for (int iq=9;iq<9+BL_SPACEDIM;iq++)
     std::cout << "COM iq= " << iq << " COM= " <<
      integrated_quantities[iq]/mass << '\n';
   }

  }
 }

} // GetDragALL

// sweep=0: integral (rho x), integral (rho) 
// sweep=1: find force, torque, moments of inertia, center of mass,mass
void
NavierStokes::GetDrag(Array<Real>& integrated_quantities,int isweep) {

 int bfact=parent->Space_blockingFactor(level);

 int nmat=num_materials;
 MultiFab& S_new=get_new_data(State_Type,slab_step+1);

 int rzflag=0;
 if (CoordSys::IsRZ())
  rzflag=1;
 else if (CoordSys::IsCartesian())
  rzflag=0;
 else if (CoordSys::IsCYLINDRICAL())
  rzflag=3;
 else
  BoxLib::Error("CoordSys bust 4");

 if (num_state_base!=2)
  BoxLib::Error("num_state_base invalid");

 debug_ngrow(CELLTENSOR_MF,1,45);
 debug_ngrow(FACETENSOR_MF,1,45);
 resize_levelsetLO(2,LEVELPC_MF);
 debug_ngrow(LEVELPC_MF,2,45);
 if (localMF[LEVELPC_MF]->nComp()!=nmat*(1+BL_SPACEDIM))
  BoxLib::Error("levelpc mf has incorrect ncomp");
 VOF_Recon_resize(1,SLOPE_RECON_MF);
 debug_ngrow(SLOPE_RECON_MF,1,46);
 debug_ngrow(CELL_VISC_MATERIAL_MF,0,47);
 debug_ngrow(CELL_VISC_MF,1,47);
 resize_metrics(1);
 debug_ngrow(VOLUME_MF,1,48);
 debug_ngrow(DRAG_MF,0,50);

 for (int dir=0;dir<BL_SPACEDIM;dir++)
  debug_ngrow(FACE_VAR_MF+dir,0,2);

 int nparts=im_solid_map.size();
 if ((nparts<0)||(nparts>=nmat))
  BoxLib::Error("nparts invalid");
 Array<int> im_solid_map_null;
 im_solid_map_null.resize(1);

 int* im_solid_map_ptr;
 int nparts_def=nparts;
 if (nparts==0) {
  im_solid_map_ptr=im_solid_map_null.dataPtr();
  nparts_def=1;
 } else if ((nparts>=1)&&(nparts<=nmat-1)) {
  im_solid_map_ptr=im_solid_map.dataPtr();
 } else
  BoxLib::Error("nparts invalid");

 int ntensor=BL_SPACEDIM*BL_SPACEDIM;
 int ntensorMM=ntensor*num_materials_vel;

 if (localMF[CELLTENSOR_MF]->nComp()!=ntensorMM)
  BoxLib::Error("localMF[CELLTENSOR_MF]->nComp() invalid");
 if (localMF[FACETENSOR_MF]->nComp()!=ntensorMM)
  BoxLib::Error("localMF[FACETENSOR_MF]->nComp() invalid");

 int nstate=num_materials_vel*(BL_SPACEDIM+1)+
  nmat*(num_state_material+ngeom_raw)+1;
 if (nstate!=S_new.nComp())
  BoxLib::Error("nstate invalid");

 if (integrated_quantities.size()!=13)
  BoxLib::Error("integrated_quantities invalid size");
 
 Array< Array<Real> > local_integrated_quantities;
 local_integrated_quantities.resize(thread_class::nthreads);

 for (int tid=0;tid<thread_class::nthreads;tid++) {
  local_integrated_quantities[tid].resize(13);
  for (int iq=0;iq<13;iq++)
   local_integrated_quantities[tid][iq]=0.0;
 } // tid

// gear problem: probtype=563, axis_dir=2, 3D
// scale torque by 2 pi vinletgas/60


 if (localMF[DRAG_MF]->nComp()!=4*BL_SPACEDIM+1)
  BoxLib::Error("drag ncomp invalid");

 const Real* dx = geom.CellSize();

 int project_option_combine=3;  // velocity in GetDrag
 int combine_flag=2;
 int hflag=0;
 int combine_idx=-1;  // update state variables
 int update_flux=0;
 int prescribed_noslip=1;
 combine_state_variable(
  prescribed_noslip,
  project_option_combine,
  combine_idx,combine_flag,hflag,update_flux);
 project_option_combine=0; // mac velocity
 prescribed_noslip=0;
 update_flux=1;
 combine_state_variable(
  prescribed_noslip,
  project_option_combine,
  combine_idx,combine_flag,hflag,update_flux);

  // p(rho,T) in compressible parts
  // projection pressure in incompressible parts.
 MultiFab* pres=getStatePres(1,cur_time_slab); 

 if (pres->nComp()!=num_materials_vel)
  BoxLib::Error("pres->nComp() invalid");
 
 MultiFab* vel=getState(2,0,BL_SPACEDIM*num_materials_vel,cur_time_slab);
  // mask=tag if not covered by level+1 or outside the domain.
 int ngrowmask=2;
 int clear_phys_boundary=0;
 Real tag=1.0;
 MultiFab* mask=maskfiner(ngrowmask,tag,clear_phys_boundary);  
 MultiFab* den_recon=getStateDen(1,cur_time_slab);  
 clear_phys_boundary=3;
  // mask=tag at exterior fine/fine border.
  // mask=1-tag at other exterior boundaries.
 MultiFab* mask3=maskfiner(ngrowmask,tag,clear_phys_boundary);

 MultiFab* elastic_tensor_mf=den_recon;
 int elastic_ntensor=den_recon->nComp();

 resize_FSI_GHOST_MF(2);
 if (localMF[FSI_GHOST_MF]->nGrow()!=2)
  BoxLib::Error("localMF[FSI_GHOST_MF]->nGrow()!=ngrowFSI");
 if (localMF[FSI_GHOST_MF]->nComp()!=nparts_def*BL_SPACEDIM)
  BoxLib::Error("localMF[FSI_GHOST_MF]->nComp()!=nparts_def*BL_SPACEDIM");

 if ((num_materials_viscoelastic>=1)&&
     (num_materials_viscoelastic<=nmat)) {

  elastic_ntensor=num_materials_viscoelastic*NUM_TENSOR_TYPE;
  elastic_tensor_mf=
   new MultiFab(grids,elastic_ntensor,1,dmap,Fab_allocate);
  elastic_tensor_mf->setVal(0.0);
    
  for (int im=0;im<nmat;im++) {

   if (ns_is_rigid(im)==0) {

    if ((elastic_time[im]>0.0)&& 
        (elastic_viscosity[im]>0.0)) {

     int partid=0;
     while ((im_elastic_map[partid]!=im)&&(partid<im_elastic_map.size())) {
      partid++;
     }

     if (partid<im_elastic_map.size()) {
      make_viscoelastic_tensor(im);
      MultiFab::Copy(*elastic_tensor_mf,*localMF[VISCOTEN_MF],0,
       partid*NUM_TENSOR_TYPE,NUM_TENSOR_TYPE,1);
     } else
      BoxLib::Error("partid could not be found: GetDrag");
    } else if ((elastic_time[im]==0.0)||
               (elastic_viscosity[im]==0.0)) {

     if (viscoelastic_model[im]!=0)
      BoxLib::Error("viscoelastic_model[im]!=0");

    } else
     BoxLib::Error("elastic_time/elastic_viscosity invalid");

   } else if (ns_is_rigid(im)==1) {
    // do nothing
   } else
    BoxLib::Error("ns_is_rigid invalid");

  } // im=0..nmat-1
 } else if (num_materials_viscoelastic==0) {
  // do nothing
 } else
  BoxLib::Error("num_materials_viscoelastic invalid");

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(*localMF[DRAG_MF]); mfi.isValid(); ++mfi) {
  BL_ASSERT(grids[mfi.index()] == mfi.validbox());
  const int gridno = mfi.index();
  const Box& tilegrid = mfi.tilebox();
  const Box& fabgrid = grids[gridno];
  const int* tilelo=tilegrid.loVect();
  const int* tilehi=tilegrid.hiVect();
  const int* fablo=fabgrid.loVect();
  const int* fabhi=fabgrid.hiVect();

  const Real* xlo = grid_loc[gridno].lo();
  Array<int> velbc=getBCArray(State_Type,gridno,0,BL_SPACEDIM);

  FArrayBox& maskfab=(*mask)[mfi];
  FArrayBox& volfab=(*localMF[VOLUME_MF])[mfi];
  FArrayBox& areax=(*localMF[AREA_MF])[mfi];
  FArrayBox& areay=(*localMF[AREA_MF+1])[mfi];
  FArrayBox& areaz=(*localMF[AREA_MF+BL_SPACEDIM-1])[mfi];

  FArrayBox& mufab=(*localMF[CELL_VISC_MF])[mfi];
  FArrayBox& xface=(*localMF[FACE_VAR_MF])[mfi];
  FArrayBox& yface=(*localMF[FACE_VAR_MF+1])[mfi];
  FArrayBox& zface=(*localMF[FACE_VAR_MF+BL_SPACEDIM-1])[mfi];

  FArrayBox& solidfab=(*localMF[FSI_GHOST_MF])[mfi]; 

  FArrayBox& denfab=(*den_recon)[mfi];
  FArrayBox& presfab=(*pres)[mfi];
  FArrayBox& velfab=(*vel)[mfi];
  FArrayBox& dragfab=(*localMF[DRAG_MF])[mfi];

  FArrayBox& voffab=(*localMF[SLOPE_RECON_MF])[mfi];
  FArrayBox& levelpcfab=(*localMF[LEVELPC_MF])[mfi];

  FArrayBox& tensor_data=(*localMF[CELLTENSOR_MF])[mfi];
  FArrayBox& elastic_tensor_data=(*elastic_tensor_mf)[mfi];

  Real gravity_normalized=fabs(gravity);
  if (invert_gravity==1)
   gravity_normalized=-gravity_normalized;
  else if (invert_gravity==0) {
   // do nothing
  } else
   BoxLib::Error("invert_gravity invalid");

  int tid=ns_thread();

   // hoop stress, centripetal force, coriolis effect still not
   // considered.
  FORT_GETDRAG(
   &isweep,
   integrated_quantities.dataPtr(),
   local_integrated_quantities[tid].dataPtr(),
   &gravity_normalized,
   &gravity_dir,
   &elastic_ntensor,
   tensor_data.dataPtr(),
   ARLIM(tensor_data.loVect()),ARLIM(tensor_data.hiVect()),
   elastic_tensor_data.dataPtr(),
   ARLIM(elastic_tensor_data.loVect()),
   ARLIM(elastic_tensor_data.hiVect()),
   denfab.dataPtr(),ARLIM(denfab.loVect()),ARLIM(denfab.hiVect()),
   maskfab.dataPtr(),ARLIM(maskfab.loVect()),ARLIM(maskfab.hiVect()),
   voffab.dataPtr(),ARLIM(voffab.loVect()),ARLIM(voffab.hiVect()),
   levelpcfab.dataPtr(),
   ARLIM(levelpcfab.loVect()),ARLIM(levelpcfab.hiVect()),
   volfab.dataPtr(),ARLIM(volfab.loVect()),ARLIM(volfab.hiVect()),
   areax.dataPtr(),ARLIM(areax.loVect()),ARLIM(areax.hiVect()),
   areay.dataPtr(),ARLIM(areay.loVect()),ARLIM(areay.hiVect()),
   areaz.dataPtr(),ARLIM(areaz.loVect()),ARLIM(areaz.hiVect()),
   xface.dataPtr(),ARLIM(xface.loVect()),ARLIM(xface.hiVect()),
   yface.dataPtr(),ARLIM(yface.loVect()),ARLIM(yface.hiVect()),
   zface.dataPtr(),ARLIM(zface.loVect()),ARLIM(zface.hiVect()),
   mufab.dataPtr(),ARLIM(mufab.loVect()),ARLIM(mufab.hiVect()),
   &facevisc_index,
   &faceheat_index,
   &ncphys,
   xlo,dx,
   solidfab.dataPtr(),
   ARLIM(solidfab.loVect()),ARLIM(solidfab.hiVect()),
   presfab.dataPtr(),ARLIM(presfab.loVect()),ARLIM(presfab.hiVect()),
   velfab.dataPtr(),ARLIM(velfab.loVect()),ARLIM(velfab.hiVect()),
   dragfab.dataPtr(),ARLIM(dragfab.loVect()),ARLIM(dragfab.hiVect()),
   tilelo,tilehi,
   fablo,fabhi,&bfact,
   &rzflag,velbc.dataPtr(),&cur_time_slab,
   &visc_coef,
   &ntensor,
   &ntensorMM,
   &nmat,
   &nparts,
   &nparts_def,
   im_solid_map_ptr);

 } // mfi
} // omp

 int iqstart=0;
 int iqend=13;
 if (isweep==0) 
  iqstart=9;
 else if (isweep==1)
  iqend=9;
 else
  BoxLib::Error("isweep invalid");

 for (int tid=1;tid<thread_class::nthreads;tid++) {
  for (int iq=iqstart;iq<iqend;iq++) {
   local_integrated_quantities[0][iq]+=local_integrated_quantities[tid][iq];
  }
 } // tid

 ParallelDescriptor::Barrier();

 for (int iq=iqstart;iq<iqend;iq++) {
  ParallelDescriptor::ReduceRealSum(local_integrated_quantities[0][iq]);
  integrated_quantities[iq]+=local_integrated_quantities[0][iq];
 }

 project_option_combine=3; // velocity in GetDrag
 combine_flag=2;
 hflag=0;
 combine_idx=-1; 
 update_flux=0;
 prescribed_noslip=1;
 combine_state_variable(
  prescribed_noslip,
  project_option_combine,
  combine_idx,combine_flag,hflag,update_flux);
 project_option_combine=0; // mac velocity
 prescribed_noslip=0;
 update_flux=1;
 combine_state_variable(
  prescribed_noslip,
  project_option_combine,
  combine_idx,combine_flag,hflag,update_flux);
 
 if ((num_materials_viscoelastic>=1)&&
     (num_materials_viscoelastic<=nmat)) {
  delete elastic_tensor_mf;
 } else if (num_materials_viscoelastic==0) {
  // do nothing
 } else
  BoxLib::Error("num_materials_viscoelastic invalid");

 delete pres; 
 delete vel; 
 delete den_recon; 
 delete mask; 
 delete mask3; 

} // subroutine GetDrag

void NavierStokes::project_right_hand_side(
  int index_MF,int project_option) {


 if ((project_option==0)||
     (project_option==1)||
     (project_option==10)||  // sync project due to advection
     (project_option==11)||  // FSI_material_exists (2nd project)
     (project_option==13)||  // FSI_material_exists (1st project)
     (project_option==12)) { // pressure extension

  int finest_level=parent->finestLevel();
  if (finest_level>=0) {

   if (level==0) {

    if (ones_sum_global>=1.0) {

     if (local_solvability_projection==0) {
      if (singular_possible==1) {
       zap_resid_where_singular(index_MF);
      } else if (singular_possible==0) {
       // do nothing
      } else
       BoxLib::Error("singular_possible invalid"); 
     } else if (local_solvability_projection==1) {

      if (singular_possible==1) {
       // rhsnew=rhs H-alpha H
       // 0 =sum rhs H-alpha sum H
       // alpha=sum rhs H / sum H
       zap_resid_where_singular(index_MF);
       Real coef;
       dot_productALL_ones(project_option,index_MF,coef);
       coef=-coef/ones_sum_global;
       mf_combine_ones(project_option,index_MF,coef);
       zap_resid_where_singular(index_MF);
      } else
       BoxLib::Error("singular_possible invalid"); 

     } else
      BoxLib::Error("local_solvability_projection invalid");

    } else {
     std::cout << "index_MF = " << index_MF << '\n';
     std::cout << "project_option = " << project_option << '\n';
     std::cout << "ones_sum_global = " << ones_sum_global << '\n';
     BoxLib::Error("ones_sum_global invalid");
    }
   } else
    BoxLib::Error("level invalid");
  } else
   BoxLib::Error("finest_level invalid");

 } else if (project_option==2) {
  if (singular_possible==0) {
   // do nothing
  } else
   BoxLib::Error("singular_possible invalid");
 } else if (project_option==3) {
  if (singular_possible==0) {
   // do nothing
  } else
   BoxLib::Error("singular_possible invalid");
 } else if ((project_option>=100)&&(project_option<100+num_species_var)) {
  if (singular_possible==0) {
   // do nothing
  } else
   BoxLib::Error("singular_possible invalid");
 } else
  BoxLib::Error("project_option invalid project_right_hand_side");

} // subroutine project_right_hand_side

void NavierStokes::dot_productALL_ones(int project_option,
   int index_MF,Real& result) {

 int finest_level=parent->finestLevel();
 if (level!=0)
  BoxLib::Error("level=0 in dot_productALL_ones");

 Real tempsum=0.0;
 Real total_sum=0.0;
 int nsolve=1;

 for (int k = 0; k <= finest_level; k++) {
  NavierStokes& ns_level = getLevel(k);
  ns_level.dotSum(
    project_option,
    ns_level.localMF[index_MF],
    ns_level.localMF[ONES_MF], tempsum,nsolve);

  total_sum+=tempsum;
 }

 result=total_sum;
}  // subroutine dot_projectALL_ones


void NavierStokes::zap_resid_where_singular(int index_MF) {

 int finest_level=parent->finestLevel();
 if (level!=0)
  BoxLib::Error("level=0 in dot_productALL_ones");

 for (int k = 0; k <= finest_level; k++) {
  NavierStokes& ns_level = getLevel(k);
  MultiFab::Multiply(*ns_level.localMF[index_MF],
	*ns_level.localMF[ONES_MF],0,0,1,0);
 }  // k=0..finest_level

}  // subroutine zap_resid_where_singular

void NavierStokes::dot_productALL_ones_size(int project_option,Real& result) {

 int finest_level=parent->finestLevel();
 if (level!=0)
  BoxLib::Error("level=0 in dot_productALL_ones");

 Real tempsum=0.0;
 Real total_sum=0.0;
 int nsolve=1;

 for (int k = 0; k <= finest_level; k++) {
  NavierStokes& ns_level = getLevel(k);
  ns_level.dotSum(
   project_option,
   ns_level.localMF[ONES_MF], 
   ns_level.localMF[ONES_MF], tempsum, nsolve);

  total_sum+=tempsum;
 }

 result=total_sum;

} // subroutine dot_productALL_ones_size




void NavierStokes::mf_combine_ones(
 int project_option,
 int index_MF,Real& Beta) {

  int nsolve=1;

    // amf_x=amf_x+Beta * ones_mf  (where mask=1)
  int finest_level=parent->finestLevel();
  for (int k = 0; k <= finest_level; ++k) {
    NavierStokes& ns_level = getLevel(k);
    ns_level.levelCombine(
     project_option,
     ns_level.localMF[index_MF],
     ns_level.localMF[ONES_MF],
     ns_level.localMF[index_MF], Beta,nsolve);
  }

}


void NavierStokes::dot_productALL(int project_option,
 int index1_MF,int index2_MF,
 Real& result,int nsolve) {

 int finest_level=parent->finestLevel();
 if (level!=0)
  BoxLib::Error("level=0 in dot_productALL");

 Real tempsum=0.0;
 Real total_sum=0.0;

 for (int k = 0; k <= finest_level; k++) {
  NavierStokes& ns_level = getLevel(k);
  ns_level.dotSum( 
     project_option,
     ns_level.localMF[index1_MF],
     ns_level.localMF[index2_MF], tempsum,nsolve);
  total_sum+=tempsum;
 }

 result=total_sum;
}

void
NavierStokes::dotSum(int project_option,
  MultiFab* mf1, MultiFab* mf2, Real& result,int nsolve) {
 
 bool use_tiling=ns_tiling;

 int num_materials_face=num_materials_vel;

 if ((nsolve!=1)&&(nsolve!=BL_SPACEDIM))
  BoxLib::Error("nsolve invalid");

 if ((project_option==0)||
     (project_option==1)||
     (project_option==10)||
     (project_option==11)|| //FSI_material_exists (2nd project)
     (project_option==13)|| //FSI_material_exists (1st project)
     (project_option==12)||
     (project_option==3)) {  // viscosity
  if (num_materials_face!=1)
   BoxLib::Error("num_materials_face invalid");
 } else if ((project_option==2)||  // thermal diffusion
            ((project_option>=100)&&
             (project_option<100+num_species_var))) {
  num_materials_face=num_materials_scalar_solve;
 } else
  BoxLib::Error("project_option invalid2");

 if (num_materials_vel!=1)
  BoxLib::Error("num_materials_vel invalid");

 if ((num_materials_face!=1)&&
     (num_materials_face!=num_materials))
  BoxLib::Error("num_materials_face invalid");

 for (int dir=0;dir<BL_SPACEDIM;dir++)
  debug_ngrow(FACE_VAR_MF+dir,0,2);

 resize_maskfiner(1,MASKCOEF_MF);
 debug_ngrow(MASKCOEF_MF,0,51);
 debug_ngrow(DOTMASK_MF,0,51);
 if (localMF[DOTMASK_MF]->nComp()!=num_materials_face)
  BoxLib::Error("localMF[DOTMASK_MF]->nComp()!=num_materials_face");

 int nsolveMM=nsolve*num_materials_face;

 if (mf1->nComp()!=nsolveMM) {
  std::cout << "mf1_ncomp nsolveMM " << mf1->nComp() << ' ' << nsolveMM << '\n';
  BoxLib::Error("dotSum: mf1 invalid ncomp");
 }
 if (mf2->nComp()!=nsolveMM)
  BoxLib::Error("mf2 invalid ncomp");

 Array<Real> sum;
 sum.resize(thread_class::nthreads);
 for (int tid=0;tid<thread_class::nthreads;tid++) {
  sum[tid]=0.0;
 }

 int finest_level=parent->finestLevel();
 if (level>finest_level)
  BoxLib::Error("level too big");

#ifdef _OPENMP
#pragma omp parallel 
#endif
{
 for (MFIter mfi(*mf1,use_tiling); mfi.isValid(); ++mfi) {
  BL_ASSERT(grids[mfi.index()] == mfi.validbox());

  const int gridno = mfi.index();
  const Box& tilegrid = mfi.tilebox();
  const Box& fabgrid = grids[gridno];
  const int* tilelo=tilegrid.loVect();
  const int* tilehi=tilegrid.hiVect();
  const int* fablo=fabgrid.loVect();
  const int* fabhi=fabgrid.hiVect();
  int bfact=parent->Space_blockingFactor(level);

  FArrayBox& fab = (*mf1)[mfi];
  FArrayBox& fab2 = (*mf2)[mfi];

  Real tsum=0.0;
  FArrayBox& mfab=(*localMF[MASKCOEF_MF])[mfi];
  FArrayBox& dotfab=(*localMF[DOTMASK_MF])[mfi];
  int tid=ns_thread();

   // in: NAVIERSTOKES_3D.F90
  FORT_SUMDOT(&tsum,
    fab.dataPtr(),ARLIM(fab.loVect()), ARLIM(fab.hiVect()),
    fab2.dataPtr(),ARLIM(fab2.loVect()), ARLIM(fab2.hiVect()),
    dotfab.dataPtr(),ARLIM(dotfab.loVect()),ARLIM(dotfab.hiVect()),
    mfab.dataPtr(),ARLIM(mfab.loVect()),ARLIM(mfab.hiVect()),
    tilelo,tilehi,
    fablo,fabhi,&bfact,
    &debug_dot_product,
    &level,&gridno,
    &nsolve, 
    &nsolveMM, 
    &num_materials_face);
  sum[tid] += tsum;
 } // mfi1
} // omp
 for (int tid=1;tid<thread_class::nthreads;tid++) {
  sum[0]+=sum[tid];
 }
 ParallelDescriptor::Barrier();
 ParallelDescriptor::ReduceRealSum(sum[0]);

 result=sum[0];
}


void NavierStokes::mf_combine(
  int project_option,
  int index_x_MF,
  int index_y_MF,
  Real Beta,
  int index_z_MF,int nsolve) {
  // amf_z = amf_x + Beta amf_y

  int finest_level=parent->finestLevel();
  for (int k = 0; k <= finest_level; ++k) {
    NavierStokes& ns_level = getLevel(k);
    ns_level.levelCombine(
     project_option,
     ns_level.localMF[index_x_MF],
     ns_level.localMF[index_y_MF],
     ns_level.localMF[index_z_MF], Beta,nsolve);
  }

}

  // mfz = mfx + Beta mfy
void NavierStokes::levelCombine(
 int project_option,
 MultiFab* mfx, MultiFab* mfy, MultiFab* mfz,
 Real beta,int nsolve) {
 
 bool use_tiling=ns_tiling;

 int num_materials_face=num_materials_vel;

 if ((nsolve!=1)&&(nsolve!=BL_SPACEDIM))
  BoxLib::Error("nsolve invalid");

 if ((project_option==0)||
     (project_option==1)||
     (project_option==10)||
     (project_option==11)|| //FSI_material_exists (2nd project)
     (project_option==13)|| //FSI_material_exists (1st project)
     (project_option==12)||
     (project_option==3)) {  // viscosity
  if (num_materials_face!=1)
   BoxLib::Error("num_materials_face invalid");
 } else if ((project_option==2)||  // thermal diffusion
            ((project_option>=100)&&
             (project_option<100+num_species_var))) {
  num_materials_face=num_materials_scalar_solve;
 } else
  BoxLib::Error("project_option invalid2");

 if (num_materials_vel!=1)
  BoxLib::Error("num_materials_vel invalid");

 if ((num_materials_face!=1)&&
     (num_materials_face!=num_materials))
  BoxLib::Error("num_materials_face invalid");

 int finest_level=parent->finestLevel();
 if (level > finest_level)
  BoxLib::Error("level too big");

 for (int dir=0;dir<BL_SPACEDIM;dir++)
  debug_ngrow(FACE_VAR_MF+dir,0,2);

 resize_maskfiner(1,MASKCOEF_MF);
 debug_ngrow(MASKCOEF_MF,0,51);
 debug_ngrow(DOTMASK_MF,0,51);
 if (localMF[DOTMASK_MF]->nComp()!=num_materials_face)
  BoxLib::Error("localMF[DOTMASK_MF]->nComp()!=num_materials_face");

 int nsolveMM=nsolve*num_materials_face;

 if (mfx->nComp()!= nsolveMM)
  BoxLib::Error("mfx invalid ncomp");
 if (mfy->nComp()!= nsolveMM)
  BoxLib::Error("mfy invalid ncomp");
 if (mfz->nComp()!= nsolveMM)
  BoxLib::Error("mfz invalid ncomp");

#ifdef _OPENMP
#pragma omp parallel 
#endif
{
 for (MFIter mfi(*mfx,use_tiling); mfi.isValid(); ++mfi) {
  const int gridno = mfi.index();
  const Box& tilegrid = mfi.tilebox();
  const Box& fabgrid = grids[gridno];
  const int* tilelo=tilegrid.loVect();
  const int* tilehi=tilegrid.hiVect();
  const int* fablo=fabgrid.loVect();
  const int* fabhi=fabgrid.hiVect();
  int bfact=parent->Space_blockingFactor(level);

  FArrayBox& fabx = (*mfx)[mfi];
  FArrayBox& faby = (*mfy)[mfi];
  FArrayBox& mfab = (*localMF[MASKCOEF_MF])[mfi];
  FArrayBox& dotfab=(*localMF[DOTMASK_MF])[mfi];
  FArrayBox& fabz = (*mfz)[mfi];

  //fabz = fabx + beta * faby
  //in: NAVIERSTOKES_3D.F90
  FORT_FABCOM(
   fabx.dataPtr(),ARLIM(fabx.loVect()),ARLIM(fabx.hiVect()),
   faby.dataPtr(),ARLIM(faby.loVect()),ARLIM(faby.hiVect()),
   dotfab.dataPtr(),ARLIM(dotfab.loVect()),ARLIM(dotfab.hiVect()),
   mfab.dataPtr(),ARLIM(mfab.loVect()),ARLIM(mfab.hiVect()),
   fabz.dataPtr(),ARLIM(fabz.loVect()),ARLIM(fabz.hiVect()),
   &beta,
   tilelo,tilehi,
   fablo,fabhi,&bfact,
   &nsolve,
   &nsolveMM,
   &num_materials_face);
 } // mfi
} // omp

 ParallelDescriptor::Barrier();
} // subroutine levelCombine



void NavierStokes::volWgtSum(
  Array<Real>& result,
  Array<int>& sumdata_type,
  Array<int>& sumdata_sweep,
  Array<Real>& ZZ,Array<Real>& FF,
  int dirx,int diry,int cut_flag,
  MultiFab* dragmf,int isweep) {

 
 bool use_tiling=ns_tiling;

 int nmat=num_materials;
 int ntensor=BL_SPACEDIM*BL_SPACEDIM;
 int ntensorMM=ntensor*num_materials_vel;

  // 0 empty
  // F,E  2 x nmat
  // drag (3 comp)
  // min interface location 3 x nmat  (x1,y1,z1   x2,y2,z2  ...)
  // max interface location 3 x nmat  (x1,y1,z1   x2,y2,z2  ...)
  // pressure drag (3 comp)
  // min den,denA 2 x nmat
  // max den,denA 2 x nmat
  // x=0 amplitude
  // centroid 3 x nmat (x1,y1,z1  x2,y2,z2  ... )
  // min dist from centroid  nmat
  // max dist from centroid  nmat
  // mass      nmat
  // momentum  3 x nmat
  // energy    nmat
  // left pressure,right pressure, left wt,right wt
  // kinetic energy derived  nmat
  // LS F  nmat
  // LS centroid 3 x nmat (x1,y1,z1  x2,y2,z2 ... )
  // torque (3 comp)
  // pressure torque (3 comp)
  // perimeter (rasterized) (1 comp)
  // min interface extent on slice (nmat comp)
  // max interface extent on slice (nmat comp)
  // integral of vorticity (3 comp)
  // vort_error (1 comp)
  // vel_error (1 comp)
  // energy_moment (1 comp)

 int filler_comp=0;
 int FE_sum_comp=filler_comp+1;
 int drag_sum_comp=FE_sum_comp+2*nmat;
 int minint_sum_comp=drag_sum_comp+3;
 int maxint_sum_comp=minint_sum_comp+3*nmat;
 int pdrag_sum_comp=maxint_sum_comp+3*nmat;
 int minden_sum_comp=pdrag_sum_comp+3;
 int maxden_sum_comp=minden_sum_comp+2*nmat;
 int xnot_amp_sum_comp=maxden_sum_comp+2*nmat;
 int cen_sum_comp=xnot_amp_sum_comp+1;
 int mincen_sum_comp=cen_sum_comp+3*nmat;
 int maxcen_sum_comp=mincen_sum_comp+nmat;
 int mass_sum_comp=maxcen_sum_comp+nmat;
 int mom_sum_comp=mass_sum_comp+nmat;
 int energy_sum_comp=mom_sum_comp+3*nmat;
 int left_pressure_sum=energy_sum_comp+nmat;
 int kinetic_energy_sum_comp=left_pressure_sum+4;
 int LS_F_sum_comp=kinetic_energy_sum_comp+nmat;
 int LS_cen_sum_comp=LS_F_sum_comp+nmat;
 int torque_sum_comp=LS_cen_sum_comp+3*nmat;
 int ptorque_sum_comp=torque_sum_comp+3;
 int step_perim_sum_comp=ptorque_sum_comp+3;
 int minint_slice=step_perim_sum_comp+1;
 int maxint_slice=minint_slice+nmat;
 int vort_sum_comp=maxint_slice+nmat;
 int vort_error=vort_sum_comp+3; 
 int vel_error=vort_error+1; 
 int energy_moment=vel_error+1; 
 int enstrophy=energy_moment+1; // integral of w dot w
 int total_comp=enstrophy+nmat; 

 if (total_comp!=result.size())
  BoxLib::Error("result size invalid");
 if (total_comp!=sumdata_type.size())
  BoxLib::Error("sumdata_type size invalid");
 if (total_comp!=sumdata_sweep.size())
  BoxLib::Error("sumdata_sweep size invalid");

 int finest_level=parent->finestLevel();

 if (num_state_base!=2)
  BoxLib::Error("num_state_base invalid");

 if (localMF[CELLTENSOR_MF]->nComp()!=ntensorMM)
  BoxLib::Error("localMF[CELLTENSOR_MF]->nComp() invalid");

 MultiFab* den_recon=getStateDen(1,upper_slab_time);  
 int den_ncomp=den_recon->nComp();

 MultiFab* error_heat_map_mf=new MultiFab(grids,nmat,0,dmap,Fab_allocate);
 error_heat_map_mf -> setVal(0.0);

 int project_option_combine=3; // velocity in volWgtSum
 int prescribed_noslip=1;
 int combine_flag=2;
 int hflag=0;
 int combine_idx=-1;  // update state variables
 int update_flux=0;
 combine_state_variable(
  prescribed_noslip,
  project_option_combine,
  combine_idx,combine_flag,hflag,update_flux);
 project_option_combine=0; // mac velocity
 prescribed_noslip=0;
 update_flux=1;
 combine_state_variable(
  prescribed_noslip,
  project_option_combine,
  combine_idx,combine_flag,hflag,update_flux);

 resize_maskfiner(2,MASKCOEF_MF);
 debug_ngrow(MASKCOEF_MF,2,53);

 for (int dir=0;dir<BL_SPACEDIM;dir++)
  debug_ngrow(FACE_VAR_MF+dir,0,2);

 VOF_Recon_resize(2,SLOPE_RECON_MF);
 debug_ngrow(SLOPE_RECON_MF,2,54);
 debug_ngrow(CELLTENSOR_MF,1,54);

  // velocity and pressure
 MultiFab* vel=getState(1,0,num_materials_vel*(BL_SPACEDIM+1),
   upper_slab_time);

 const Real* dx = geom.CellSize();

 int resultsize=result.size();
 if (resultsize!=total_comp)
  BoxLib::Error("resultsize invalid");

 int NN=ZZ.size()-1;
 if (NN!=FF.size()-1)
  BoxLib::Error("FF and ZZ fail size sanity check");

 Array< Array<Real> > local_result;
 Array< Array<Real> > local_ZZ;
 Array< Array<Real> > local_FF;

 local_result.resize(thread_class::nthreads);
 local_ZZ.resize(thread_class::nthreads);
 local_FF.resize(thread_class::nthreads);

 for (int tid=0;tid<thread_class::nthreads;tid++) {
  local_result[tid].resize(resultsize);

  for (int isum=0;isum<resultsize;isum++) {
   local_result[tid][isum]=0.0;
   if (sumdata_type[isum]==2) // min
    local_result[tid][isum]=1.0E+15;
   else if (sumdata_type[isum]==3)  // max
    local_result[tid][isum]=-1.0E+15;
   else if (sumdata_type[isum]==1)
    local_result[tid][isum]=0.0;
   else
    BoxLib::Error("sumdata_type invalid");
  } // isum

  local_ZZ[tid].resize(NN+1);
  local_FF[tid].resize(NN+1);
  for (int iz=0;iz<=NN;iz++) {
   local_ZZ[tid][iz]=0.0;
   local_FF[tid][iz]=0.0;
  }
 }  // tid

 MultiFab* lsmf=getStateDist(2,upper_slab_time,11);

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(*localMF[MASKCOEF_MF],use_tiling); mfi.isValid(); ++mfi) {
   BL_ASSERT(grids[mfi.index()] == mfi.validbox());
   int gridno=mfi.index();
   const Box& tilegrid = mfi.tilebox();
   const Box& fabgrid = grids[gridno];
   const int* tilelo=tilegrid.loVect();
   const int* tilehi=tilegrid.hiVect();
   const int* fablo=fabgrid.loVect();
   const int* fabhi=fabgrid.hiVect();

   const Real* xlo = grid_loc[gridno].lo();

   FArrayBox& mfab=(*localMF[MASKCOEF_MF])[mfi];
   FArrayBox& maskSEMfab=(*localMF[MASKSEM_MF])[mfi];
   FArrayBox& reconfab=(*localMF[SLOPE_RECON_MF])[mfi];
   FArrayBox& denfab=(*den_recon)[mfi];
   FArrayBox& velfab=(*vel)[mfi];

   FArrayBox& cellten=(*localMF[CELLTENSOR_MF])[mfi];
   if (cellten.nComp()!=ntensorMM)
    BoxLib::Error("cellten invalid ncomp");

   FArrayBox& dragfab=(*dragmf)[mfi];
   Real problo[BL_SPACEDIM];
   Real probhi[BL_SPACEDIM];
   for (int dir=0;dir<BL_SPACEDIM;dir++) {
    problo[dir]=Geometry::ProbLo(dir);
    probhi[dir]=Geometry::ProbHi(dir);
   }
   FArrayBox& lsfab=(*lsmf)[mfi];
   int bfact=parent->Space_blockingFactor(level);

   int tid=ns_thread();

   FORT_SUMMASS(
    &tid,
    &adapt_quad_depth,
    &slice_dir,
    xslice.dataPtr(),
    problo,probhi, 
    xlo,dx,
    cellten.dataPtr(),ARLIM(cellten.loVect()),ARLIM(cellten.hiVect()),
    lsfab.dataPtr(),ARLIM(lsfab.loVect()),ARLIM(lsfab.hiVect()),
    maskSEMfab.dataPtr(),
    ARLIM(maskSEMfab.loVect()),ARLIM(maskSEMfab.hiVect()),
    mfab.dataPtr(),ARLIM(mfab.loVect()),ARLIM(mfab.hiVect()),
    dragfab.dataPtr(),ARLIM(dragfab.loVect()),ARLIM(dragfab.hiVect()),
    reconfab.dataPtr(),ARLIM(reconfab.loVect()),ARLIM(reconfab.hiVect()),
    denfab.dataPtr(),ARLIM(denfab.loVect()),ARLIM(denfab.hiVect()),
    velfab.dataPtr(),ARLIM(velfab.loVect()),ARLIM(velfab.hiVect()),
    tilelo,tilehi,
    fablo,fabhi,
    &bfact,
    &upper_slab_time,
    local_result[tid].dataPtr(),
    result.dataPtr(),
    sumdata_type.dataPtr(),
    sumdata_sweep.dataPtr(),
    &resultsize,
    &NN,
    local_ZZ[tid].dataPtr(),
    local_FF[tid].dataPtr(),
    &dirx,&diry,&cut_flag,
    &nmat,
    &ntensorMM,
    &den_ncomp,
    &isweep);

 } // mfi
} // omp

 for (int tid=1;tid<thread_class::nthreads;tid++) {

  for (int idest=1;idest<total_comp;idest++) {

   if (((sumdata_sweep[idest]==0)&&(isweep==0))||
       ((sumdata_sweep[idest]==1)&&(isweep==1))) { 

    if (sumdata_type[idest]==1) { // reduce real sum
     local_result[0][idest]+=local_result[tid][idest];
    } else if (sumdata_type[idest]==2) { // reduce real min
     if (local_result[tid][idest]<local_result[0][idest])
      local_result[0][idest]=local_result[tid][idest];
    } else if (sumdata_type[idest]==3) { // reduce real max
     if (local_result[tid][idest]>local_result[0][idest])
      local_result[0][idest]=local_result[tid][idest];
    } else
     BoxLib::Error("sumdata_type invalid");

   } else if (sumdata_sweep[idest]==0) {
    // do nothing
   } else if (sumdata_sweep[idest]==1) {
    // do nothing
   } else
    BoxLib::Error("sumdata_sweep invalid");

  } // idest
 
  if (isweep==0) { 
   for (int iz=0;iz<=NN;iz++) {
    if (local_ZZ[tid][iz]>local_ZZ[0][iz])
     local_ZZ[0][iz]=local_ZZ[tid][iz];
    if (local_FF[tid][iz]>local_FF[0][iz])
     local_FF[0][iz]=local_FF[tid][iz];
   } // iz
  }  // isweep==0
 } // tid

 ParallelDescriptor::Barrier();

 project_option_combine=3; // velocity in volWgtSum
 prescribed_noslip=1;
 combine_flag=2;
 hflag=0;
 combine_idx=-1;  // update state variables
 update_flux=0;
 combine_state_variable(
  prescribed_noslip,
  project_option_combine,
  combine_idx,combine_flag,hflag,update_flux);
 project_option_combine=0; // mac velocity
 prescribed_noslip=0;
 update_flux=1;
 combine_state_variable(
  prescribed_noslip,
  project_option_combine,
  combine_idx,combine_flag,hflag,update_flux);

 result[filler_comp]=0.0;

 for (int idest=1;idest<total_comp;idest++) {

  if (((sumdata_sweep[idest]==0)&&(isweep==0))||
      ((sumdata_sweep[idest]==1)&&(isweep==1))) { 

   if (sumdata_type[idest]==1) { // reduce real sum
    ParallelDescriptor::ReduceRealSum(local_result[0][idest]);
    result[idest]=result[idest]+local_result[0][idest];
   } else if (sumdata_type[idest]==2) { // reduce real min
    ParallelDescriptor::ReduceRealMin(local_result[0][idest]);
    if (local_result[0][idest]<result[idest])
     result[idest]=local_result[0][idest];
   } else if (sumdata_type[idest]==3) { // reduce real max
    ParallelDescriptor::ReduceRealMax(local_result[0][idest]);
    if (local_result[0][idest]>result[idest])
     result[idest]=local_result[0][idest];
   } else
    BoxLib::Error("sumdata_type invalid");

  } else if (sumdata_sweep[idest]==0) {
   // do nothing
  } else if (sumdata_sweep[idest]==1) {
   // do nothing
  } else
   BoxLib::Error("sumdata_sweep invalid");

 } // idest
 
 if (isweep==0) { 
  for (int iz=0;iz<=NN;iz++) {
   ParallelDescriptor::ReduceRealMax(local_ZZ[0][iz]);
   ParallelDescriptor::ReduceRealMax(local_FF[0][iz]);
   if (local_ZZ[0][iz]>ZZ[iz])
    ZZ[iz]=local_ZZ[0][iz];
   if (local_FF[0][iz]>FF[iz])
    FF[iz]=local_FF[0][iz];
  }
 }  // isweep==0

 if ((fab_verbose==2)||(fab_verbose==3)) {
  std::cout << "c++ level,finest_level " << level << ' ' <<
    finest_level << '\n';

  for (MFIter mfi(*error_heat_map_mf); mfi.isValid(); ++mfi) {
   BL_ASSERT(grids[mfi.index()] == mfi.validbox());
   const int gridno = mfi.index();
   const Box& fabgrid = grids[gridno];
   const int* fablo=fabgrid.loVect();
   const int* fabhi=fabgrid.hiVect();
   const Real* xlo = grid_loc[gridno].lo();
   std::cout << "gridno= " << gridno << '\n';
   std::cout << "output of error heatmap " << '\n';
   int interior_only=0;
   FArrayBox& errfab=(*error_heat_map_mf)[mfi];
   tecplot_debug(errfab,xlo,fablo,fabhi,dx,-1,0,0,
     nmat,interior_only);
  }// mfi
 } // verbose

 delete error_heat_map_mf;
 delete lsmf;
 delete den_recon;
 delete vel;
}  // subroutine volWgtSum


void
NavierStokes::setPlotVariables()
{
  AmrLevel::setPlotVariables();
}

std::string
NavierStokes::thePlotFileType () const
{
    //
    // Increment this whenever the writePlotFile() format changes.
    //
    static const std::string the_plot_file_type("NavierStokes-V1.1");

    return the_plot_file_type;
}

void 
NavierStokes::debug_memory() {

 if (level!=0)
  BoxLib::Error("level invalid debug_memory");

 if (show_mem==1) {
  std::fflush(NULL);
  ParallelDescriptor::Barrier();
  int proc=ParallelDescriptor::MyProc();
  std::cout << "calling memory status on processor=" << proc << '\n';
  FORT_MEMSTATUS(&proc);
  std::cout << "after calling memory status on processor=" << proc << '\n';
  std::fflush(NULL);
  ParallelDescriptor::Barrier();
 } else if (show_mem!=0)
  BoxLib::Error("show_mem invalid");

}

void NavierStokes::writeInterfaceReconstruction() {

 debug_ngrow(SLOPE_RECON_MF,1,55);
 if (level!=0)
  BoxLib::Error("level should be zero");

 int finest_level = parent->finestLevel();
 Array<int> grids_per_level;
 grids_per_level.resize(finest_level+1);
 for (int ilev=finest_level;ilev>=0;ilev--) {
  NavierStokes& ns_level=getLevel(ilev);
  grids_per_level[ilev]=ns_level.grids.size();
  ns_level.output_triangles();  // all nmat materials at once
 }
 ParallelDescriptor::Barrier();
 if (ParallelDescriptor::IOProcessor()) {
  int nsteps=parent->levelSteps(0);
  int arrdim=finest_level+1;

  int plotint=parent->plotInt();
  int nmat=num_materials;
  for (int im=1;im<=nmat;im++) {
   FORT_COMBINETRIANGLES(grids_per_level.dataPtr(),
    &finest_level,
    &nsteps,
    &im,
    &arrdim,
    &cur_time_slab,
    &plotint);
  }
 }
 ParallelDescriptor::Barrier();

}  // writeInterfaceReconstruction


// VOF_Recon_ALL called before this routine is called.
// init_FSI_GHOST_MF() called for all relevant levels prior to this routine.
void NavierStokes::writeTECPLOT_File(int do_plot,int do_slice) {

 if (level!=0)
  BoxLib::Error("level invalid writeTECPLOT_File");

 if (num_materials_vel!=1)
  BoxLib::Error("num_materials_vel != 1");

 if ((SDC_outer_sweeps>=0)&&
     (SDC_outer_sweeps<ns_time_order)) {
  // do nothing
 } else
  BoxLib::Error("SDC_outer_sweeps invalid");

 int nsteps=parent->levelSteps(0);

 int nmat=num_materials;
 int ntensor=BL_SPACEDIM*BL_SPACEDIM;
 int ntensorMM=ntensor*num_materials_vel;

 int finest_level = parent->finestLevel();

 ParallelDescriptor::Barrier();

 debug_ngrow(SLOPE_RECON_MF,1,65);
 if (localMF[SLOPE_RECON_MF]->nComp()!=nmat*ngeom_recon)
  BoxLib::Error("localMF[SLOPE_RECON_MF]->nComp() invalid");

  // uses "slope_recon" 
 if (do_plot==1) {
  writeInterfaceReconstruction();
 } else if (do_plot==0) {
  // do nothing
 } else
  BoxLib::Error("do_plot invalid");

 Array<int> grids_per_level_array;
 grids_per_level_array.resize(finest_level+1);
 Array<BoxArray> cgrids_minusBA_array;
 cgrids_minusBA_array.resize(finest_level+1);

 Array<Real> slice_data;
 NavierStokes& ns_finest=getLevel(finest_level);
 const Box& domain_finest = ns_finest.geom.Domain();
 const int* domlo_finest = domain_finest.loVect();
 const int* domhi_finest = domain_finest.hiVect();

  // in order to get domain dimensions:
  //Geometry::ProbLo(dir);
  //Geometry::ProbHi(dir);
 
 int nslice=domhi_finest[slice_dir]-domlo_finest[slice_dir]+3;
    // x,y,z,xvel,yvel,zvel,PMG,PEOS,DIV,den,Temp,KE
    // (value of material with LS>0)
 int nstate_slice=BL_SPACEDIM+BL_SPACEDIM+6;
 slice_data.resize(nslice*nstate_slice);
 for (int i=0;i<nslice*nstate_slice;i++)
  slice_data[i]=-1.0e+30;

  // in: NavierStokes::writeTECPLOT_File
 allocate_levelsetLO_ALL(1,LEVELPC_MF);

// HOLD_VELOCITY_DATA_MF not already allocated,
// so "init_gradu_tensorALL" needs to allocate HOLD_VELOCITY_DATA_MF
// internally for its' own uses then delete it after this call.
 if (localMF_grow[HOLD_VELOCITY_DATA_MF]!=-1)
  BoxLib::Error("localMF_grow[HOLD_VELOCITY_DATA_MF] invalid");

 int do_alloc=1; 
 init_gradu_tensorALL(HOLD_VELOCITY_DATA_MF,do_alloc,
   CELLTENSOR_MF,FACETENSOR_MF);

 if (localMF_grow[HOLD_VELOCITY_DATA_MF]!=-1)
  BoxLib::Error("localMF_grow[HOLD_VELOCITY_DATA_MF] invalid");

 if (localMF[CELLTENSOR_MF]->nComp()!=ntensorMM)
  BoxLib::Error("localMF[CELLTENSOR_MF]->nComp() invalid");
 if (localMF[FACETENSOR_MF]->nComp()!=ntensorMM)
  BoxLib::Error("localMF[FACETENSOR_MF]->nComp() invalid");

 getStateVISC_ALL(CELL_VISC_MATERIAL_MF,1);
 if (localMF[CELL_VISC_MATERIAL_MF]->nComp()<nmat)
  BoxLib::Error("viscmf invalid ncomp");

 getStateDIV_ALL(MACDIV_MF,1);
 if (localMF[MACDIV_MF]->nComp()!=num_materials_vel)
  BoxLib::Error("localMF[MACDIV_MF]->nComp() invalid");

   // if FENE-CR+Carreau,
   // liquid viscosity=etaS+etaP ( 1+ (beta gamma_dot)^alpha )^((n-1)/alpha)
   //
   // for each material, there are 5 components:
   // 1. \dot{gamma}
   // 2. Tr(A) if viscoelastic
   //    \dot{gamma} o.t.
   // 3. Tr(A) (liquid viscosity - etaS)/etaP  if FENE-CR+Carreau
   //    Tr(A) if FENE-CR
   //    \dot{gamma} o.t.
   // 4. (3) * f(A)  if viscoelastic
   //    \dot{gamma} o.t.
   // 5. vorticity magnitude.

   // calls GETSHEAR and DERMAGTRACE
 int ntrace=5*nmat;
 getState_tracemag_ALL(MAGTRACE_MF,1);
 if (localMF[MAGTRACE_MF]->nComp()!=ntrace)
  BoxLib::Error("localMF[MAGTRACE_MF]->nComp() invalid");

  // idx,ngrow,scomp,ncomp,index,scompBC_map
 Array<int> scompBC_map;
 scompBC_map.resize(num_materials_vel);
 for (int i=0;i<num_materials_vel;i++)
  scompBC_map[i]=0;
 PCINTERP_fill_bordersALL(MACDIV_MF,1,0,
   num_materials_vel,State_Type,scompBC_map);

 for (int i=0;i<ntrace;i++) {
  scompBC_map.resize(1);
  scompBC_map[0]=0;
  PCINTERP_fill_bordersALL(MAGTRACE_MF,1,i,1,State_Type,scompBC_map);
 }
 
 int output_MAC_vel=0;
 if (face_flag==1)
  output_MAC_vel=1;

 if (output_MAC_vel==1) {
  getStateALL(1,cur_time_slab,0,
    num_materials_vel*BL_SPACEDIM,HOLD_VELOCITY_DATA_MF);
  for (int ilev=finest_level;ilev>=0;ilev--) {
   NavierStokes& ns_level=getLevel(ilev);
   int use_VOF_weight=1;
   int prescribed_noslip=0;
   ns_level.VELMAC_TO_CELL(prescribed_noslip,use_VOF_weight);
  }
 }

 int tecplot_finest_level=finest_level;
 if (tecplot_max_level<tecplot_finest_level)
  tecplot_finest_level=tecplot_max_level;

 IntVect visual_fab_lo(IntVect::TheZeroVector()); 
 IntVect visual_fab_hi(visual_ncell); 
 Box visual_node_box(visual_fab_lo,visual_fab_hi);
 visual_fab_hi-=IntVect::TheUnitVector();
 Box visual_domain(visual_fab_lo,visual_fab_hi);
 int visual_ncomp=2*BL_SPACEDIM+1+nmat;  // x,u,mag vort,LS
 FArrayBox visual_fab_output(visual_node_box,visual_ncomp);
 FArrayBox visual_fab_input(visual_node_box,visual_ncomp); 

 visual_fab_output.setVal(-1.0e+20);
 visual_fab_input.setVal(-1.0e+20);

 Array<int> gridlo(3);
 Array<int> gridhi(3);
 for (int dir=0;dir<3;dir++) {
  gridlo[dir]=0;
  gridhi[dir]=0;
 }
 for (int dir=0;dir<BL_SPACEDIM;dir++) {
  gridlo[dir]=0;
  gridhi[dir]=visual_ncell[dir];
 }

 if (visual_compare==1) {

  if (ParallelDescriptor::IOProcessor()) {
   int do_input=1;
   FORT_IO_COMPARE(
    &nmat,
    &nsteps,
    &do_input,
    &visual_compare,
    &cur_time_slab,
    visual_fab_input.dataPtr(),
    ARLIM(visual_fab_input.loVect()), 
    ARLIM(visual_fab_input.hiVect()), 
    visual_fab_output.dataPtr(),
    ARLIM(visual_fab_output.loVect()), 
    ARLIM(visual_fab_output.hiVect()), 
    visual_domain.loVect(),
    visual_domain.hiVect(),
    &visual_ncomp);
  }
  ParallelDescriptor::Barrier();

   // communicate visual_fab_input from the IO proc to all of the
   // other processors.
  for (int i=gridlo[0];i<=gridhi[0];i++) {
   for (int j=gridlo[1];j<=gridhi[1];j++) {
    for (int k=gridlo[2];k<=gridhi[2];k++) {
     for (int n=0;n<visual_ncomp;n++) {
      Array<int> arr_index(BL_SPACEDIM);
      arr_index[0]=i;
      arr_index[1]=j;
      if (BL_SPACEDIM==3) {
       arr_index[BL_SPACEDIM-1]=k;
      }
      IntVect p(arr_index);
      Real local_data=visual_fab_input(p,n); 
      ParallelDescriptor::ReduceRealMax(local_data);
      ParallelDescriptor::Barrier();
      if (1==0) {
       Box pbox(p,p);
       visual_fab_input.setVal(local_data,pbox,n);
      } else {
       visual_fab_input(p,n)=local_data;
      }
      ParallelDescriptor::Barrier();
     } // n
    } // k
   } // j
  } // i 

 } else if (visual_compare==0) {
  // do nothing
 } else
  BoxLib::Error("visual_compare invalid");

 for (int ilev=tecplot_finest_level;ilev>=0;ilev--) {
  NavierStokes& ns_level=getLevel(ilev);

  ns_level.debug_ngrow(MACDIV_MF,1,250);

  MultiFab* velmf=ns_level.getState(1,0,
    num_materials_vel*(BL_SPACEDIM+1),cur_time_slab);
  MultiFab* presmf=ns_level.derive_EOS_pressure(); 
  if (presmf->nComp()!=num_materials_vel)
   BoxLib::Error("presmf has invalid ncomp");

  MultiFab* denmf=ns_level.getStateDen(1,cur_time_slab); 
  MultiFab* lsdist=ns_level.getStateDist(1,cur_time_slab,12);
  MultiFab* div_data=ns_level.getStateDIV_DATA(1,0,num_materials_vel,
    cur_time_slab);
  if (1==0) {
   std::cout << "level= " << ilev << " div_data norm0= " << 
    div_data->norm0() << '\n'; 
   std::cout << "level= " << ilev << " div_datanorm0+1grow= " << 
    div_data->norm0(0,1) << '\n'; 
  }
  MultiFab* viscoelasticmf=denmf;
  if ((num_materials_viscoelastic>=1)&&(num_materials_viscoelastic<=nmat)) {
   viscoelasticmf=ns_level.getStateTensor(1,0,
     num_materials_viscoelastic*NUM_TENSOR_TYPE,cur_time_slab);
  } else if (num_materials_viscoelastic==0) {
   // do nothing
  } else
   BoxLib::Error("num_materials_viscoelastic invalid");

  ns_level.output_zones(
   visual_fab_output,
   visual_domain,
   visual_ncomp,
   velmf,
   presmf,
   ns_level.localMF[MACDIV_MF],
   div_data,
   denmf,
   viscoelasticmf,
   lsdist,
   ns_level.localMF[CELL_VISC_MATERIAL_MF],
   ns_level.localMF[MAGTRACE_MF],
   grids_per_level_array[ilev],
   cgrids_minusBA_array[ilev],
   slice_data.dataPtr(), 
   do_plot,do_slice);

  if ((slice_dir>=0)&&(slice_dir<BL_SPACEDIM)) {
   for (int i=0;i<nslice*nstate_slice;i++)
    ParallelDescriptor::ReduceRealMax(slice_data[i]);
  } else
   BoxLib::Error("slice_dir invalid");

  if ((num_materials_viscoelastic>=1)&&(num_materials_viscoelastic<=nmat)) {
   delete viscoelasticmf;
  } else if (num_materials_viscoelastic==0) {
   // do nothing
  } else
   BoxLib::Error("num_materials_viscoelastic invalid");

  delete div_data;
  delete velmf;
  delete denmf;
  delete presmf;
  delete lsdist;
 }  // ilev=tecplot_finest_level ... 0

 ParallelDescriptor::Barrier();

 for (int i=gridlo[0];i<=gridhi[0];i++) {
  for (int j=gridlo[1];j<=gridhi[1];j++) {
   for (int k=gridlo[2];k<=gridhi[2];k++) {
    for (int n=0;n<visual_ncomp;n++) {
     Array<int> arr_index(BL_SPACEDIM);
     arr_index[0]=i;
     arr_index[1]=j;
     if (BL_SPACEDIM==3) {
      arr_index[BL_SPACEDIM-1]=k;
     }
     IntVect p(arr_index);
     Real local_data=visual_fab_output(p,n); 
     ParallelDescriptor::ReduceRealMax(local_data);
     ParallelDescriptor::Barrier();
     if (1==0) {
      Box pbox(p,p);
      visual_fab_output.setVal(local_data,pbox,n);
     } else {
      visual_fab_output(p,n)=local_data;
     }
     ParallelDescriptor::Barrier();
    } // n
   } // k
  } // j
 } // i 

 if (ParallelDescriptor::IOProcessor()) {

  int total_number_grids=0;
  for (int ilev=0;ilev<=tecplot_finest_level;ilev++) {
   total_number_grids+=grids_per_level_array[ilev];
  }

  Array<int> levels_array(total_number_grids);
  Array<int> bfact_array(total_number_grids);
  Array<int> gridno_array(total_number_grids);
  Array<int> gridlo_array(BL_SPACEDIM*total_number_grids);
  Array<int> gridhi_array(BL_SPACEDIM*total_number_grids);

  int temp_number_grids=0;
  for (int ilev=0;ilev<=tecplot_finest_level;ilev++) {
   BoxArray cgrids_minusBA;
   cgrids_minusBA=cgrids_minusBA_array[ilev];
   int bfact=parent->Space_blockingFactor(ilev);
   for (int igrid=0;igrid<cgrids_minusBA.size();igrid++) {
    levels_array[temp_number_grids]=ilev; 
    bfact_array[temp_number_grids]=bfact; 
    gridno_array[temp_number_grids]=igrid; 
    const Box& fabgrid = cgrids_minusBA[igrid];
    const int* lo=fabgrid.loVect();
    const int* hi=fabgrid.hiVect();
    for (int dir=0;dir<BL_SPACEDIM;dir++) {
     gridlo_array[BL_SPACEDIM*temp_number_grids+dir]=lo[dir];
     gridhi_array[BL_SPACEDIM*temp_number_grids+dir]=hi[dir];
    }
    temp_number_grids++;
   } // igrid
  } // ilev

  if (temp_number_grids!=total_number_grids)
   BoxLib::Error("temp_number_grids invalid");
  
  int num_levels=tecplot_finest_level+1;
  int plotint=parent->plotInt();
  int sliceint=parent->sliceInt();

  int nparts=im_solid_map.size();
  if ((nparts<0)||(nparts>=nmat))
   BoxLib::Error("nparts invalid");
  Array<int> im_solid_map_null;
  im_solid_map_null.resize(1);
  im_solid_map_null[0]=0;

  int* im_solid_map_ptr;
  int nparts_def=nparts;
  if (nparts==0) {
   im_solid_map_ptr=im_solid_map_null.dataPtr();
   nparts_def=1;
  } else if ((nparts>=1)&&(nparts<=nmat-1)) {
   im_solid_map_ptr=im_solid_map.dataPtr();
  } else
   BoxLib::Error("nparts invalid");

  if (do_plot==1) {
   FORT_COMBINEZONES(
    &total_number_grids,
    grids_per_level_array.dataPtr(),
    levels_array.dataPtr(),
    bfact_array.dataPtr(),
    gridno_array.dataPtr(),
    gridlo_array.dataPtr(),
    gridhi_array.dataPtr(),
    &tecplot_finest_level,
    &nsteps,
    &num_levels,
    &cur_time_slab,
    &visual_option,
    &visual_revolve,
    &plotint,
    &nmat, 
    &nparts,
    &nparts_def,
    im_solid_map_ptr);
  } else if (do_plot==0) {
   // do nothing
  } else
   BoxLib::Error("do_plot invalid");

  if (do_slice==1) {
   if ((slice_dir>=0)&&(slice_dir<BL_SPACEDIM)) {
    FORT_OUTPUTSLICE(&cur_time_slab,&nsteps,&sliceint,
     slice_data.dataPtr(),&nslice,&nstate_slice,
     &visual_option);
   } else
    BoxLib::Error("slice_dir invalid");
  } else if (do_slice==0) {
   // do nothing
  } else
   BoxLib::Error("do_slice invalid");

  int do_input=0;
  FORT_IO_COMPARE(
   &nmat,
   &nsteps,
   &do_input,
   &visual_compare,
   &cur_time_slab,
   visual_fab_input.dataPtr(),
   ARLIM(visual_fab_input.loVect()), 
   ARLIM(visual_fab_input.hiVect()), 
   visual_fab_output.dataPtr(),
   ARLIM(visual_fab_output.loVect()), 
   ARLIM(visual_fab_output.hiVect()), 
   visual_domain.loVect(),
   visual_domain.hiVect(),
   &visual_ncomp);

 } else if (!ParallelDescriptor::IOProcessor()) {
  // do nothing
 } else
  BoxLib::Error("ParallelDescriptor::IOProcessor() corrupt");

 ParallelDescriptor::Barrier();

 if (output_MAC_vel==1) {
  for (int ilev=finest_level;ilev>=0;ilev--) {
   NavierStokes& ns_level=getLevel(ilev);
   MultiFab& S_new=ns_level.get_new_data(State_Type,slab_step+1);
   MultiFab::Copy(S_new,*ns_level.localMF[HOLD_VELOCITY_DATA_MF],
     0,0,num_materials_vel*BL_SPACEDIM,1);
   ns_level.delete_localMF(HOLD_VELOCITY_DATA_MF,1);
  }  // ilev
 }

 delete_array(MACDIV_MF);
 delete_array(MAGTRACE_MF); 
 delete_array(CELLTENSOR_MF);
 delete_array(FACETENSOR_MF);

} // subroutine writeTECPLOT_File

void
NavierStokes::writePlotFile (
  const std::string& dir,
  std::ostream& os,
  int do_plot,int do_slice,
  int SDC_outer_sweeps_in,int slab_step_in) {

 SDC_setup();
 ns_time_order=parent->Time_blockingFactor();

 SDC_outer_sweeps=SDC_outer_sweeps_in;
 if ((SDC_outer_sweeps>=0)&&
     (SDC_outer_sweeps<ns_time_order)) {
  // do nothing
 } else
  BoxLib::Error("SDC_outer_sweeps invalid");

 slab_step=slab_step_in; 

 SDC_setup_step();

 int i, n;

 int f_lev = parent->finestLevel();

  // metrics_dataALL
  // MASKCOEF_MF
  // MASK_NBR_MF
  // init_FSI_GHOST_MF_ALL
  // LEVELPC_MF
  // MASKSEM_MF
  // VOF_Recon_ALL
  // make_physics_varsALL
 if (level==0) {
  int post_init_flag=0; // in: writePlotFile
  prepare_post_process(post_init_flag);
 } // level==0

  // output tecplot zonal files  x,y,z,u,v,w,phi,psi
 if (visual_option==-2) {

  if (level==0) {
   writeTECPLOT_File(do_plot,do_slice);
  }

  // output isosurface for LevelVapor and LevelSolid
  // output plot files
 } else if (visual_option==-1) {

  if (level==0) { 
   if (do_slice==1) {
    int do_plot_kluge=0;
    writeTECPLOT_File(do_plot_kluge,do_slice);
   } 
  }

  ParallelDescriptor::Barrier();

  if (do_plot==1) {

   if (level == 0) {
    writeInterfaceReconstruction();
   } // level==0

   //
   // The list of indices of State to write to plotfile.
   // first component of pair is state_type,
   // second component of pair is component # within the state_type
   //

   std::vector<std::pair<int,int> > plot_var_map;

   for (int typ=State_Type;typ<NUM_STATE_TYPE;typ++) {
    for (int comp = 0; comp < desc_lst[typ].nComp();comp++) {
     if ((parent->isStatePlotVar(desc_lst[typ].name(comp))) &&
         (desc_lst[typ].getType() == IndexType::TheCellType())) {
      plot_var_map.push_back(std::pair<int,int>(typ,comp));
     }
    }  // comp
   } // typ

   int num_derive = 0;
   std::list<std::string> derive_names;
   const std::list<DeriveRec>& dlist = derive_lst.dlist();

   for (std::list<DeriveRec>::const_iterator it = dlist.begin();
        it != dlist.end();
        ++it) {
     if (parent->isDerivePlotVar(it->name())) {
         derive_names.push_back(it->name());
         num_derive += it->numDerive();
     }
   }

   int n_data_items = plot_var_map.size() + num_derive;

   if (level == 0 && ParallelDescriptor::IOProcessor()) {
     //
     // The first thing we write out is the plotfile type.
     //
     os << thePlotFileType() << '\n';

     if (n_data_items == 0)
         BoxLib::Error("Must specify at least one valid data item to plot");

     os << n_data_items << '\n';

     //
     // Names of variables -- first state, then derived
     //
     for (i =0; i < plot_var_map.size(); i++) {
         int typ  = plot_var_map[i].first;
         int comp = plot_var_map[i].second;
         os << desc_lst[typ].name(comp) << '\n';
     }

     for (std::list<std::string>::const_iterator it = derive_names.begin();
          it != derive_names.end();
          ++it) {
         const DeriveRec* rec = derive_lst.get(*it);
         for (i = 0; i < rec->numDerive(); i++)
             os << rec->variableName(i) << '\n';
     }
     os << BL_SPACEDIM << '\n';
     os << parent->cumTime() << '\n';
     os << f_lev << '\n';
     for (i = 0; i < BL_SPACEDIM; i++)
         os << Geometry::ProbLo(i) << ' ';
     os << '\n';
     for (i = 0; i < BL_SPACEDIM; i++)
         os << Geometry::ProbHi(i) << ' ';
     os << '\n';
     for (i = 0; i < f_lev; i++)
         os << 2 << ' ';
     os << '\n';
     for (i = 0; i <= f_lev; i++)
         os << parent->Geom(i).Domain() << ' ';
     os << '\n';
     for (i = 0; i <= f_lev; i++)
         os << parent->levelSteps(i) << ' ';
     os << '\n';
     for (i = 0; i <= f_lev; i++) {
         for (int k = 0; k < BL_SPACEDIM; k++)
             os << parent->Geom(i).CellSize()[k] << ' ';
         os << '\n';
     }
     os << (int) CoordSys::Coord() << '\n';
     os << "0\n"; // Write bndry data.
   }
   // Build the directory to hold the MultiFab at this level.
   // The name is relative to the directory containing the Header file.
   //
   static const std::string BaseName = "/Cell";
   char buf[64];
   sprintf(buf, "Level_%d", level);
   std::string Level = buf;
   //
   // Now for the full pathname of that directory.
   //
   std::string FullPath = dir;
   if (!FullPath.empty() && FullPath[FullPath.length()-1] != '/')
     FullPath += '/';
   FullPath += Level;
   //
   // Only the I/O processor makes the directory if it doesn't already exist.
   //
   if (ParallelDescriptor::IOProcessor())
     if (!BoxLib::UtilCreateDirectory(FullPath, 0755))
         BoxLib::CreateDirectoryFailed(FullPath);
   //
   // Force other processors to wait till directory is built.
   //
   ParallelDescriptor::Barrier();

   if (ParallelDescriptor::IOProcessor()) {
     os << level << ' ' << grids.size() << ' ' << cur_time_slab << '\n';
     os << parent->levelSteps(level) << '\n';

     for (i = 0; i < grids.size(); ++i) {
         for (n = 0; n < BL_SPACEDIM; n++)
             os << grid_loc[i].lo(n) << ' ' << grid_loc[i].hi(n) << '\n';
     }
     //
     // The full relative pathname of the MultiFabs at this level.
     // The name is relative to the Header file containing this name.
     // It's the name that gets written into the Header.
     //
     if (n_data_items > 0) {
         std::string PathNameInHeader = Level;
         PathNameInHeader += BaseName;
         os << PathNameInHeader << '\n';
     }
   } // if IOProc

   //
   // We combine all of the multifabs -- state, derived, etc -- into one
   // multifab -- plotMF.
   // Each state variable has one component.
   // The VOF variables have to be obtained together with Centroid vars.
   // A derived variable is allowed to have multiple components.
   int       cnt   = 0;
   int       ncomp = 1;
   const int nGrow = 0;
   MultiFab  plotMF(grids,n_data_items,nGrow,dmap,Fab_allocate);
   //
   // Cull data from state variables 
   //
   for (i = 0; i < plot_var_map.size(); i++) {
     int typ  = plot_var_map[i].first;
     int comp = plot_var_map[i].second;
     MultiFab* this_dat;
     ncomp=1;

     if (typ==State_Type) {
      int nmat=num_materials;
      int scomp_mofvars=num_materials_vel*(BL_SPACEDIM+1)+
       nmat*num_state_material;
      if (comp==scomp_mofvars) {
       ncomp=nmat*ngeom_raw;
      }
      this_dat=getState(nGrow,comp,ncomp,cur_time_slab);
     } else if ((typ==Solid_State_Type)&&(im_solid_map.size()!=0)) {

      if (comp!=0) {
       std::cout << "comp=" << comp << " ncomp= " <<
        desc_lst[typ].nComp() << '\n';
       BoxLib::Error("comp invalid for Solid_State_Type");
      }
      int nmat=num_materials;
      int nparts=im_solid_map.size();
      if ((nparts<1)||(nparts>=nmat))
       BoxLib::Error("nparts invalid");
      if (comp==0) {
       ncomp=nparts*BL_SPACEDIM;
      } else
       BoxLib::Error("comp invalid");

      this_dat=getStateSolid(nGrow,comp,ncomp,cur_time_slab);

      if (this_dat->nComp()!=ncomp)
       BoxLib::Error("this_dat->nComp() invalid");

     } else if ((typ==Tensor_Type)&&(im_elastic_map.size()!=0)) {

      if (comp!=0) {
       std::cout << "comp=" << comp << " ncomp= " <<
        desc_lst[typ].nComp() << '\n';
       BoxLib::Error("comp invalid for Tensor_Type");
      }
      int nmat=num_materials;
      int nparts=im_elastic_map.size();
      if ((nparts<1)||(nparts>nmat)||
          (nparts!=num_materials_viscoelastic))
       BoxLib::Error("nparts invalid");
      if (comp==0) {
       ncomp=nparts*NUM_TENSOR_TYPE;
      } else
       BoxLib::Error("comp invalid");

      this_dat=getStateTensor(nGrow,comp,ncomp,cur_time_slab);

      if (this_dat->nComp()!=ncomp) 
       BoxLib::Error("this_dat->nComp() invalid");

     } else if (typ==LS_Type) {
      if (comp!=0) {
       std::cout << "comp=" << comp << " ncomp= " <<
        desc_lst[typ].nComp() << '\n';
       BoxLib::Error("comp invalid for LS_Type");
      }

      int nmat=num_materials;
      if (comp==0) {
       ncomp=nmat*(BL_SPACEDIM+1);
      } else
       BoxLib::Error("comp invalid");

      this_dat=getStateDist(nGrow,cur_time_slab,13);
      if (this_dat->nComp()!=ncomp)
       BoxLib::Error("this_dat->nComp() invalid");

     } else if (typ==DIV_Type) {
      if (comp!=0)
       BoxLib::Error("comp invalid for DIV_Type");
      ncomp=num_materials_vel;
      this_dat=getStateDIV_DATA(1,comp,ncomp,cur_time_slab);
     } else
      BoxLib::Error("typ invalid");

     MultiFab::Copy(plotMF,*this_dat,0,cnt,ncomp,nGrow);
     delete this_dat;
     cnt+= ncomp;
     i+=(ncomp-1);
   }  // i
   //
   // Cull data from derived variables.
   // 

   if (derive_names.size() > 0) {
    for (std::list<std::string>::const_iterator it = derive_names.begin();
         it != derive_names.end(); ++it) {
     const DeriveRec* rec = derive_lst.get(*it);
     ncomp = rec->numDerive();
     MultiFab* derive_dat = derive(*it,nGrow);
     MultiFab::Copy(plotMF,*derive_dat,0,cnt,ncomp,nGrow);
     delete derive_dat;
     cnt += ncomp;
    }
   }
   //
   // Use the Full pathname when naming the MultiFab.
   //
   std::string TheFullPath = FullPath;
   TheFullPath += BaseName;
   VisMF::Write(plotMF,TheFullPath);
   ParallelDescriptor::Barrier();
  } else if (do_plot==0) {
   // do nothing
  } else
   BoxLib::Error("do_plot invalid");

 } else
  BoxLib::Error("visual_option invalid try -1 or -2");

}

void NavierStokes::DumpProcNum() {

 
 bool use_tiling=ns_tiling;
 MultiFab& unew = get_new_data(State_Type,slab_step+1);
#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(unew,use_tiling); mfi.isValid(); ++mfi) {
  BL_ASSERT(grids[mfi.index()] == mfi.validbox());
  const int i = mfi.index();
  if (verbose>0)
   std::cout << "level=" << level << " grid=" << i << " proc=" <<
    ParallelDescriptor::MyProc() << " thread= " << ns_thread() << "\n";
 }// mfi
}// omp
 ParallelDescriptor::Barrier();
}

// called from: estTimeStep
void NavierStokes::MaxAdvectSpeedALL(Real& dt_min,
  Real* vel_max,Real* vel_max_estdt) {

 int finest_level=parent->finestLevel();
 if (level!=0)
  BoxLib::Error("level invalid MaxAdvectSpeedALL");

 last_finest_level=finest_level; 
 
 Real local_vel_max[BL_SPACEDIM+1];  // last component is max|c|^2
 Real local_vel_max_estdt[BL_SPACEDIM+1];  // last component is max|c|^2
 Real local_dt_min;

 for (int dir=0;dir<BL_SPACEDIM+1;dir++) {
  vel_max[dir]=0.0;
  vel_max_estdt[dir]=0.0;
 }
 dt_min=1.0E+30;
 if (dt_min<dt_max) 
  dt_min=dt_max;

 if (localMF_grow[FSI_GHOST_MF]<1) {

  init_FSI_GHOST_MF_ALL(1);

 }

 for (int ilev=finest_level;ilev>=0;ilev--) {
  NavierStokes& ns_level=getLevel(ilev);
  for (int dir=0;dir<BL_SPACEDIM+1;dir++) {
   local_vel_max[dir]=0.0;
   local_vel_max_estdt[dir]=0.0;
  }
  local_dt_min=1.0E+25;
  if (local_dt_min<dt_max) 
   local_dt_min=dt_max;

  ns_level.MaxAdvectSpeed(local_dt_min,local_vel_max,local_vel_max_estdt); 
  for (int dir=0;dir<BL_SPACEDIM+1;dir++) {
   vel_max[dir] = std::max(vel_max[dir],local_vel_max[dir]);
   vel_max_estdt[dir] = std::max(vel_max_estdt[dir],local_vel_max_estdt[dir]);
  }
  dt_min=std::min(dt_min,local_dt_min);
 } // ilev 
} // end subroutine MaxAdvectSpeedALL

// vel_max[0,1,2]= max vel in direction  vel_max[sdim]=max c^2
void NavierStokes::MaxAdvectSpeed(Real& dt_min,Real* vel_max,
 Real* vel_max_estdt) {

 int finest_level=parent->finestLevel();
 int nmat=num_materials;
 int scomp_mofvars=num_materials_vel*(BL_SPACEDIM+1)+
  nmat*num_state_material;
 int nten=( (nmat-1)*(nmat-1)+nmat-1 )/2;
 int nsolve=1;
 int nsolveMM_FACE=nsolve*num_materials_vel;

 if (num_materials_vel!=1)
  BoxLib::Error("num_materials_vel invalid");

 int nparts=im_solid_map.size();
 if ((nparts<0)||(nparts>=nmat))
  BoxLib::Error("nparts invalid");
 Array<int> im_solid_map_null;
 im_solid_map_null.resize(1);

 int* im_solid_map_ptr;
 int nparts_def=nparts;
 if (nparts==0) {
  im_solid_map_ptr=im_solid_map_null.dataPtr();
  nparts_def=1;
 } else if ((nparts>=1)&&(nparts<=nmat-1)) {
  im_solid_map_ptr=im_solid_map.dataPtr();
 } else
  BoxLib::Error("nparts invalid");

 resize_FSI_GHOST_MF(1);
 if (localMF[FSI_GHOST_MF]->nGrow()!=1)
  BoxLib::Error("localMF[FSI_GHOST_MF]->nGrow()!=1");
 if (localMF[FSI_GHOST_MF]->nComp()!=nparts_def*BL_SPACEDIM)
  BoxLib::Error("localMF[FSI_GHOST_MF]->nComp()!=nparts_def*BL_SPACEDIM");

 MultiFab* distmf=getStateDist(2,cur_time_slab,14);
 MultiFab* denmf=getStateDen(1,cur_time_slab);  // nmat*num_state_material
 MultiFab* vofmf=getState(1,scomp_mofvars,nmat*ngeom_raw,cur_time_slab);

 int rzflag=0;
 if (CoordSys::IsRZ())
  rzflag=1;
 else if (CoordSys::IsCartesian())
  rzflag=0;
 else if (CoordSys::IsCYLINDRICAL())
  rzflag=3;
 else
  BoxLib::Error("CoordSys bust 5");

 const Real* dx = geom.CellSize();

 dt_min=1.0E+25;
 if (dt_min<dt_max) 
  dt_min=dt_max;

 for (int dir=0;dir<BL_SPACEDIM+1;dir++) {
  vel_max[dir]=0.0;
  vel_max_estdt[dir]=0.0;
 }

 Array< Array<Real> > local_cap_wave_speed;
 Array< Array<Real> > local_vel_max;
 Array< Array<Real> > local_vel_max_estdt;
 Array< Real > local_dt_min;
 local_cap_wave_speed.resize(thread_class::nthreads);
 local_vel_max.resize(thread_class::nthreads);
 local_vel_max_estdt.resize(thread_class::nthreads);
 local_dt_min.resize(thread_class::nthreads);
 for (int tid=0;tid<thread_class::nthreads;tid++) {

  local_cap_wave_speed[tid].resize(nten); 
  for (int iten=0;iten<nten;iten++)
   local_cap_wave_speed[tid][iten]=cap_wave_speed[iten];

  local_vel_max[tid].resize(BL_SPACEDIM+1); // last component is max|c|^2
  local_vel_max_estdt[tid].resize(BL_SPACEDIM+1); // last component is max|c|^2
  for (int dir=0;dir<BL_SPACEDIM+1;dir++) {
   local_vel_max[tid][dir]=0.0;
   local_vel_max_estdt[tid][dir]=0.0;
  }
  local_dt_min[tid]=1.0E+25;
  if (local_dt_min[tid]<dt_max) 
   local_dt_min[tid]=dt_max;
 }  // tid 

 MultiFab* velcell=getState(1,0,num_materials_vel*BL_SPACEDIM,cur_time_slab);
  
 for (int dir=0;dir<BL_SPACEDIM;dir++) {
 
  MultiFab* velmac=getStateMAC(0,dir,0,nsolveMM_FACE,cur_time_slab);


#ifdef _OPENMP
#pragma omp parallel
#endif
{
  for (MFIter mfi(*denmf); mfi.isValid(); ++mfi) {
   BL_ASSERT(grids[mfi.index()] == mfi.validbox());
   const int gridno = mfi.index();
   const Box& tilegrid = mfi.tilebox();
   const Box& fabgrid = grids[gridno];
   const int* tilelo=tilegrid.loVect();
   const int* tilehi=tilegrid.hiVect();
   const int* fablo=fabgrid.loVect();
   const int* fabhi=fabgrid.hiVect();
   int bfact=parent->Space_blockingFactor(level);

   const Real* xlo = grid_loc[gridno].lo();
   FArrayBox& Umac=(*velmac)[mfi];
   FArrayBox& Ucell=(*velcell)[mfi];
   FArrayBox& distfab=(*distmf)[mfi];
   FArrayBox& voffab=(*vofmf)[mfi];
   FArrayBox& denfab=(*denmf)[mfi];
   FArrayBox& solidfab=(*localMF[FSI_GHOST_MF])[mfi];

   int tid=ns_thread();
   int local_enable_spectral=enable_spectral;

    // in: GODUNOV_3D.F90
   FORT_ESTDT(
    &nsolveMM_FACE,
    &local_enable_spectral,
    microlayer_substrate.dataPtr(),
    microlayer_angle.dataPtr(),
    microlayer_size.dataPtr(),
    macrolayer_size.dataPtr(),
    latent_heat.dataPtr(),
    reaction_rate.dataPtr(),
    freezing_model.dataPtr(),
    distribute_from_target.dataPtr(),
    saturation_temp.dataPtr(),
    mass_fraction_id.dataPtr(),
    species_evaporation_density.dataPtr(),
    Umac.dataPtr(),ARLIM(Umac.loVect()),ARLIM(Umac.hiVect()),
    Ucell.dataPtr(),ARLIM(Ucell.loVect()),ARLIM(Ucell.hiVect()),
    solidfab.dataPtr(),ARLIM(solidfab.loVect()),ARLIM(solidfab.hiVect()),
    denfab.dataPtr(),ARLIM(denfab.loVect()),ARLIM(denfab.hiVect()),
    voffab.dataPtr(),ARLIM(voffab.loVect()),ARLIM(voffab.hiVect()),
    distfab.dataPtr(),ARLIM(distfab.loVect()),ARLIM(distfab.hiVect()),
    xlo,dx,
    tilelo,tilehi,
    fablo,fabhi,
    &bfact,
    local_cap_wave_speed[tid].dataPtr(),
    local_vel_max[tid].dataPtr(),
    local_vel_max_estdt[tid].dataPtr(),
    &local_dt_min[tid],
    &rzflag,
    &Uref,&Lref,
    &nten,
    &use_lsa,
    denconst.dataPtr(),
    denconst_gravity.dataPtr(),
    &visc_coef,
    &gravity,
    &terminal_velocity_dt,
    &dir,
    &nmat,
    &nparts,
    &nparts_def,
    im_solid_map_ptr,
    material_type.dataPtr(),
    &cur_time_slab,
    shock_timestep.dataPtr(),
    &cfl,
    &EILE_flag, 
    &level,
    &finest_level);
  }  //mfi
} // omp

  for (int tid=1;tid<thread_class::nthreads;tid++) {

   for (int iten=0;iten<nten;iten++)
    if (local_cap_wave_speed[tid][iten]>local_cap_wave_speed[0][iten])
     local_cap_wave_speed[0][iten]=local_cap_wave_speed[tid][iten];

   if (local_dt_min[tid]<local_dt_min[0])
    local_dt_min[0]=local_dt_min[tid];

   if (local_vel_max[tid][dir]>local_vel_max[0][dir])
    local_vel_max[0][dir]=local_vel_max[tid][dir];
   if (local_vel_max[tid][BL_SPACEDIM]>local_vel_max[0][BL_SPACEDIM])
    local_vel_max[0][BL_SPACEDIM]=local_vel_max[tid][BL_SPACEDIM];

   if (local_vel_max_estdt[tid][dir]>local_vel_max_estdt[0][dir])
    local_vel_max_estdt[0][dir]=local_vel_max_estdt[tid][dir];
   if (local_vel_max_estdt[tid][BL_SPACEDIM]>
       local_vel_max_estdt[0][BL_SPACEDIM])
    local_vel_max_estdt[0][BL_SPACEDIM]=local_vel_max_estdt[tid][BL_SPACEDIM];

  } // tid

  ParallelDescriptor::Barrier();
  for (int iten=0;iten<nten;iten++)
   ParallelDescriptor::ReduceRealMax(local_cap_wave_speed[0][iten]);

  ParallelDescriptor::ReduceRealMax(local_vel_max[0][dir]);
  ParallelDescriptor::ReduceRealMax(local_vel_max[0][BL_SPACEDIM]);
  ParallelDescriptor::ReduceRealMax(local_vel_max_estdt[0][dir]);
  ParallelDescriptor::ReduceRealMax(local_vel_max_estdt[0][BL_SPACEDIM]);
  ParallelDescriptor::ReduceRealMin(local_dt_min[0]);

  delete velmac;
 }  // dir=0..sdim-1

 delete velcell;

 for (int iten=0;iten<nten;iten++)
  cap_wave_speed[iten]=local_cap_wave_speed[0][iten];

 for (int dir=0;dir<=BL_SPACEDIM;dir++) {
  vel_max[dir]=local_vel_max[0][dir];
  vel_max_estdt[dir]=local_vel_max_estdt[0][dir];
 }
 dt_min=local_dt_min[0];

 delete denmf;
 delete vofmf;
 delete distmf;

} // subroutine MaxAdvectSpeed

// called from: computeNewDt, computeInitialDt
Real NavierStokes::estTimeStep (Real local_fixed_dt) {

 Real return_dt=0.0;

 if (level!=0)
  BoxLib::Error("estTimeStep only called at level=0");

 const int finest_level = parent->finestLevel();
 NavierStokes& ns_fine = getLevel(finest_level);
 const Real* dxfine = ns_fine.geom.CellSize();
 Real smallest_dx=dxfine[0];
 for (int dir=1;dir<BL_SPACEDIM;dir++) {
  if (smallest_dx>dxfine[dir])
   smallest_dx=dxfine[dir];
 }

 if (local_fixed_dt>0.0) {

  return_dt=local_fixed_dt;

 } else if (local_fixed_dt==0.0) {

  if (fixed_dt_velocity > 0.0) {

   return_dt=smallest_dx/fixed_dt_velocity;

  } else if (fixed_dt_velocity==0.0) {

   Real dt_min;
   Real u_max[BL_SPACEDIM+1];  // last component is max|c|^2
   Real u_max_estdt[BL_SPACEDIM+1];  // last component is max|c|^2

   MaxAdvectSpeedALL(dt_min,u_max,u_max_estdt);
   if (min_velocity_for_dt>0.0) {
    Real local_dt_max=smallest_dx/min_velocity_for_dt;
    if (dt_min>local_dt_max)
     dt_min=local_dt_max;
   }

   return_dt=cfl*dt_min;

  } else {

   BoxLib::Error("fixed_dt_velocity invalid");

  }

  Array<Real> time_array;
  time_array.resize(ns_time_order+1);
  Real slablow=0.0;
  Real slabhigh=1.0;
  int slab_dt_type=parent->get_slab_dt_type();
  FORT_GL_SLAB(time_array.dataPtr(),
               &slab_dt_type,
               &ns_time_order,
               &slablow,&slabhigh);
  Real max_sub_dt=0.0;
  for (int i=0;i<ns_time_order;i++) {
   Real test_sub_dt=time_array[i+1]-time_array[i];
   if (test_sub_dt<=0.0)
    BoxLib::Error("test_sub_dt invalid");
   if (test_sub_dt>max_sub_dt)
    max_sub_dt=test_sub_dt;
  } // i
  if ((max_sub_dt<=0.0)||(max_sub_dt>1.0))
   BoxLib::Error("max_sub_dt invalid");

  if (verbose>0)
   if (ParallelDescriptor::IOProcessor())
    std::cout << "ns_time_order= " << ns_time_order << 
     " time slab factor= " << 1.0/max_sub_dt << '\n';

  return_dt=return_dt/max_sub_dt;

 } else {
  BoxLib::Error("local_fixed_dt invalid");
 }

 if (return_dt>dt_max)
  return_dt=dt_max;

 return return_dt;

} // subroutine estTimeStep

void NavierStokes::post_regrid (int lbase,int new_finest,Real time) {

  Real dt_amr=parent->getDt(); // returns dt_AMR
  int nstate=state.size();
  if (nstate!=NUM_STATE_TYPE)
   BoxLib::Error("nstate invalid");
   // olddata=newdata  
  for (int k=0;k<nstate;k++) {
   state[k].CopyNewToOld(); 
   state[k].setTimeLevel(time,dt_amr);
  }
}

void NavierStokes::computeNewDt (int finest_level,
  Real& dt,Real stop_time,int post_regrid_flag) {

 int nsteps=parent->levelSteps(0);

 if (nsteps>0) {

  Real local_fixed_dt;
  Real local_change_max;
  if (nsteps==1) {
   local_fixed_dt=fixed_dt;
   local_change_max=change_max_init;
  } else if (nsteps>1) {
   local_fixed_dt=fixed_dt;
   local_change_max=change_max;
  } else
   BoxLib::Error("nsteps invalid");
   
  if (verbose>0) {
   if (ParallelDescriptor::IOProcessor()) {
    std::cout << "start: computeNewDt nsteps=" << nsteps << 
     " local_fixed_dt= " << local_fixed_dt << " local_change_max= " <<
     local_change_max << '\n';
   }
  }

  int max_level = parent->maxLevel();

  if (level==0) {

   Real newdt=estTimeStep(local_fixed_dt);

   if ((local_fixed_dt==0.0)&&(fixed_dt_velocity==0.0)) {
    if  (newdt>local_change_max*dt)
     newdt=local_change_max*dt;
   } else if ((local_fixed_dt>0.0)||(fixed_dt_velocity>0.0)) {
    // do nothing
   } else {
    BoxLib::Error("local_fixed_dt or fixed_dt_velocity invalid");
   }

   Real dt_0=newdt;

   const Real eps      = 0.0001*dt_0;
   const Real eps2     = 0.000001*dt_0;
   upper_slab_time = state[State_Type].slabTime(ns_time_order);
   if (stop_time >= 0.0) {
      if ((upper_slab_time + dt_0) > (stop_time - eps))
          dt_0 = stop_time - upper_slab_time + eps2;
   }

   const Real check_per = parent->checkPer();
   if (check_per > 0.0) {
      int a = int((upper_slab_time + eps ) / check_per);
      int b = int((upper_slab_time + dt_0) / check_per);
      if (a != b)
          dt_0 = b * check_per - upper_slab_time;
   }

   const Real plot_per = parent->plotPer();
   if (plot_per > 0.0) {
      int a = int((upper_slab_time + eps ) / plot_per);
      int b = int((upper_slab_time + dt_0) / plot_per);
      if (a != b)
          dt_0 = b * plot_per - upper_slab_time;
   }

   dt=dt_0;
  } else if ((level>0)&&(level<=max_level)) {
   // do nothing
  } else
   BoxLib::Error("level invalid computeNewDt");

  if (verbose>0) {
   if (ParallelDescriptor::IOProcessor()) {
    std::cout << "end: computeNewDt \n";
   }
  }

 } else
  BoxLib::Error("nsteps invalid");

}

void NavierStokes::computeInitialDt (int finest_level,
   Real& dt,Real stop_time) {

 if (verbose>0) {
  if (ParallelDescriptor::IOProcessor()) {
   std::cout << "start: computeInitialDt \n";
  }
 }

 if (level!=0)
  BoxLib::Error("level invalid computeInitialDt");

  // The first time step does not have grad p initialized, so we
  // must take a very small initial time step in order to maintain
  // spectral accuracy.
 Real shrink_factor=1.0;
 if ((ns_time_order>=2)&&(ns_time_order<=32)) {
  for (int i=0;i<ns_time_order;i++)
   shrink_factor*=2.0;
 } else if (ns_time_order==1) {
  // do nothing
 } else {
  BoxLib::Error("ns_time_order invalid");
 }

 Real newdt=init_shrink*estTimeStep(fixed_dt_init)/shrink_factor;

 Real dt_0 = newdt;

 const Real eps      = 0.0001*dt_0;
 upper_slab_time = state[State_Type].slabTime(ns_time_order);
 if (stop_time >= 0.0) {
     if ((upper_slab_time + dt_0) > (stop_time - eps))
         dt_0 = stop_time - upper_slab_time;
 }

 dt=dt_0;

 if (verbose>0) {
  if (ParallelDescriptor::IOProcessor()) {
   std::cout << "end: computeInitialDt \n";
  }
 }

} // subroutine computeInitialDt

//
// Fills in amrLevel okToContinue.
//

int
NavierStokes::okToContinue ()
{
    return true;
}


void
NavierStokes::post_timestep (Real stop_time) {

 SDC_outer_sweeps=0;
 slab_step=ns_time_order-1;
 SDC_setup_step();

 if (level==0) {
  if (sum_interval>0) {
   if ( (parent->levelSteps(0)%sum_interval == 0)||
        (stop_time-upper_slab_time<1.0E-8) ) {
    int post_init_flag=0;
    sum_integrated_quantities(post_init_flag);
   }
  }
 } 

 init_regrid_history();
}

//
// Ensure state, and pressure are consistent.
//

void
NavierStokes::post_init (Real stop_time)
{

 if (verbose>0) {
  if (ParallelDescriptor::IOProcessor()) {
   std::cout << "start: post_init \n";
  }
 }

 SDC_setup();    
 ns_time_order=parent->Time_blockingFactor();
 slab_step=ns_time_order-1;

 if (level==0) {

  const int finest_level = parent->finestLevel();

   // in post_init: delete FSI_MF used by initData
  for (int ilev = 0; ilev <= finest_level; ilev++) {
   NavierStokes& ns_level = getLevel(ilev);
   if (ns_level.localMF_grow[FSI_MF]>=0)
    ns_level.delete_localMF(FSI_MF,1);
  }

  Real strt_time = state[State_Type].slabTime(ns_time_order);

   // bogus value for dt to use while doing initial project - 
   // differentiate between prev_time and cur_time.
   // newtime=strt_time, oldtime=strt_time-dt_save

  Real dt_save=1.0;

  for (int ilev = 0; ilev <= finest_level; ilev++) {
   NavierStokes& ns_level = getLevel(ilev);
   ns_level.setTimeLevel(strt_time,dt_save);
  }

  parent->setDt(dt_save);

  // Ensure state is consistent, i.e. velocity field is non-divergent,
  // Coarse levels are fine level averages, 
  //
  post_init_state();

  //
  // Re-Estimate the initial timestepping. 

  computeInitialDt(finest_level,dt_save,stop_time);

   // newtime=strt_time, oldtime=strt_time-dt_save
  for (int ilev = 0; ilev <= finest_level; ilev++) {
   NavierStokes& ns_level = getLevel(ilev);
   ns_level.setTimeLevel(strt_time,dt_save);
  }

  parent->setDt(dt_save);

  //
  // Compute the initial estimate of conservation.
  //
  int sum_interval_local=sum_interval;

  if (sum_interval_local > 0) {
     int post_init_flag=1;
     sum_integrated_quantities(post_init_flag);
  }

  ParallelDescriptor::Barrier();
 } // level=0

 if (verbose>0) {
  if (ParallelDescriptor::IOProcessor()) {
   std::cout << "end: post_init \n";
  }
 }

}  // post_init

// status==1 success
// status==0 failure
void matrix_solveCPP(Real** AA,Real* xx,Real* bb,
 int& status,int numelem) {
        
 Real alpha,holdvalue;
 int i,j,k,holdj;

 status=1;
 for (i=1;i<=numelem-1;i++) {
  holdj=i;
  holdvalue=fabs(AA[i-1][i-1]);
  for (j=i+1;j<=numelem;j++) {
   if (fabs(AA[j-1][i-1])>holdvalue) {
    holdj=j;
    holdvalue=fabs(AA[j-1][i-1]);
   }
  }
  if (holdj!=i) {
   for (j=i;j<=numelem;j++) {
    holdvalue=AA[i-1][j-1];
    AA[i-1][j-1]=AA[holdj-1][j-1];
    AA[holdj-1][j-1]=holdvalue;
   }
  }
  holdvalue=bb[i-1];
  bb[i-1]=bb[holdj-1];
  bb[holdj-1]=holdvalue;
  if (fabs(AA[i-1][i-1])<1.0E-32) 
   status=0;
  else {
   for (j=i+1;j<=numelem;j++) {
    alpha=AA[j-1][i-1]/AA[i-1][i-1];
    for (k=i;k<=numelem;k++)
     AA[j-1][k-1]=AA[j-1][k-1]-alpha*AA[i-1][k-1];
    bb[j-1]=bb[j-1]-alpha*bb[i-1];
   }
  }
 }

 for (i=numelem;i>=1;i--) {
  if (status!=0) {
   holdvalue=bb[i-1];
   for (j=i+1;j<=numelem;j++)
    holdvalue=holdvalue-AA[i-1][j-1]*xx[j-1];
   if (fabs(AA[i-1][i-1])<1.0E-32) 
    status=0;
   else
    xx[i-1]=holdvalue/AA[i-1][i-1];
  }
 }

} // matrix_solveCPP


void
NavierStokes::volWgtSumALL(
 int post_init_flag,
 Array<Real>& result,
 Array<int>& sumdata_type,
 Array<int>& sumdata_sweep,
 Array<Real>& ZZ,Array<Real>& FF,
 int dirx,int diry,int cut_flag,
 int isweep) {

 int finest_level=parent->finestLevel();
 if (level!=0)
  BoxLib::Error("level=0 in volWgtSumALL");

 if ((SDC_outer_sweeps>=0)&&
     (SDC_outer_sweeps<ns_time_order)) {
  // do nothing
 } else
  BoxLib::Error("SDC_outer_sweeps invalid");

    // vof,ref cen, order,slope,int
 int update_flag=0;  // do not update the error
 int init_vof_ls_prev_time=0;
 VOF_Recon_ALL(1,cur_time_slab,update_flag,
   init_vof_ls_prev_time,SLOPE_RECON_MF); 

 int project_option=1;  // initial project
  // need to initialize viscosity and density temporary 
  // variables.
  // in: volWgtSumALL
 int post_restart_flag=0;
 if ((post_init_flag==0)||(post_init_flag==1)) {
  // do nothing
 } else if (post_init_flag==2) {
  post_restart_flag=1;
 } else
  BoxLib::Error("post_init_flag invalid");

 make_physics_varsALL(project_option,post_restart_flag);

  // force,torque,moment of inertia,COM,mass
 allocate_array(0,4*BL_SPACEDIM+1,-1,DRAG_MF);
 Array<Real> integrated_quantities;
 integrated_quantities.resize(13); 
 GetDragALL(integrated_quantities);

 int do_alloc=1;
 init_gradu_tensorALL(HOLD_VELOCITY_DATA_MF,do_alloc,
   CELLTENSOR_MF,FACETENSOR_MF);

 for (int ilev = 0; ilev <= finest_level; ilev++) {

  NavierStokes& ns_level = getLevel(ilev);
  ns_level.volWgtSum(
    result,
    sumdata_type,
    sumdata_sweep,
    ZZ,FF,
    dirx,diry,cut_flag,
    ns_level.localMF[DRAG_MF],isweep);

 }  // ilev 

 delete_array(DRAG_MF);
 delete_array(CELLTENSOR_MF);
 delete_array(FACETENSOR_MF);

}  // subroutine volWgtSumALL

void
NavierStokes::MaxPressureVelocityALL(
   Real& minpres,Real& maxpres,Real& maxvel) {

 int finest_level=parent->finestLevel();
 if (level!=0)
  BoxLib::Error("level=0 in MaxPressureVelocityALL");

 Real local_minpres;
 Real local_maxpres;
 Real local_maxvel;
 maxpres=-1.0e+99;
 minpres=1.0e+99;
 maxvel=0.0;
 for (int k = 0; k <= finest_level; k++) {
  NavierStokes& ns_level = getLevel(k);
  ns_level.MaxPressureVelocity(local_minpres,local_maxpres,local_maxvel);
  if (local_minpres<minpres)
   minpres=local_minpres;
  if (local_maxpres>maxpres)
   maxpres=local_maxpres;
  if (local_maxvel>maxvel)
   maxvel=local_maxvel;
 }

} // end subroutine MaxPressureVelocityALL

void 
NavierStokes::MaxPressureVelocity(Real& minpres,Real& maxpres,Real& maxvel) {
 
 bool use_tiling=ns_tiling;

  // ngrow=0  
 MultiFab* vel=getState(0,0,
   num_materials_vel*(BL_SPACEDIM+1),cur_time_slab);
  // mask=tag if not covered by level+1 
  // ngrow=0 so no need to worry about ghost cells.
 int ngrowmask=0;
 Real tag=1.0;
 int clear_phys_boundary=2;
 MultiFab* mask=maskfiner(ngrowmask,tag,clear_phys_boundary);

 const Real* dx = geom.CellSize();
 maxpres=-1.0e+99;
 minpres=1.0e+99;
 maxvel=0.0;

 Array<Real> minpresA;
 Array<Real> maxpresA;
 Array<Real> maxvelA;
 minpresA.resize(thread_class::nthreads);
 maxpresA.resize(thread_class::nthreads);
 maxvelA.resize(thread_class::nthreads);
 for (int tid=0;tid<thread_class::nthreads;tid++) {
  minpresA[tid]=1.0e+99;
  maxpresA[tid]=-1.0e+99;
  maxvelA[tid]=0.0;
 }

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(*mask,use_tiling); mfi.isValid(); ++mfi) {
  BL_ASSERT(grids[mfi.index()] == mfi.validbox());
  const int gridno = mfi.index();
  const Box& tilegrid = mfi.tilebox();
  const Box& fabgrid = grids[gridno];
  const int* tilelo=tilegrid.loVect();
  const int* tilehi=tilegrid.hiVect();
  const int* fablo=fabgrid.loVect();
  const int* fabhi=fabgrid.hiVect();
  int bfact=parent->Space_blockingFactor(level);

  const Real* xlo = grid_loc[gridno].lo();
  FArrayBox& maskfab=(*mask)[mfi];
  FArrayBox& velfab=(*vel)[mfi];
  int tid=ns_thread();

  FORT_MAXPRESVEL(
   &minpresA[tid],
   &maxpresA[tid],
   &maxvelA[tid],
   xlo,dx,
   maskfab.dataPtr(),ARLIM(maskfab.loVect()),ARLIM(maskfab.hiVect()),
   velfab.dataPtr(),ARLIM(velfab.loVect()),ARLIM(velfab.hiVect()),
   tilelo,tilehi,
   fablo,fabhi,&bfact);
 } // mfi
} // omp
 for (int tid=0;tid<thread_class::nthreads;tid++) {
  if (minpres>minpresA[tid])
   minpres=minpresA[tid];
  if (maxpres<maxpresA[tid])
   maxpres=maxpresA[tid];
  if (maxvel<maxvelA[tid])
   maxvel=maxvelA[tid];
 }
 ParallelDescriptor::Barrier();
 ParallelDescriptor::ReduceRealMin(minpres);
 ParallelDescriptor::ReduceRealMax(maxpres);
 ParallelDescriptor::ReduceRealMax(maxvel);

 delete mask;
 delete vel;
} // subroutine MaxPressureVelocity

// called from 
// 1. writePlotFile   (post_init_flag=0)
// 2. post_init_state (post_init_flag=1)
// 3. post_restart    (post_init_flag=2)
void
NavierStokes::prepare_post_process(int post_init_flag) {

 if (level!=0)
  BoxLib::Error("level invalid prepare_post_process");

 const int finest_level = parent->finestLevel();

  // init VOLUME_MF and AREA_MF
 metrics_dataALL(1);

 for (int ilev=level;ilev<=finest_level;ilev++) {
  NavierStokes& ns_level=getLevel(ilev);

  ns_level.allocate_mdot();

    // mask=tag if not covered by level+1 or outside the domain.
  Real tag=1.0;
  int clearbdry=0; 
   // ngrow=1
  ns_level.maskfiner_localMF(MASKCOEF_MF,1,tag,clearbdry);
  ns_level.prepare_mask_nbr(1);

  ns_level.init_FSI_GHOST_MF(1);

  if (post_init_flag==1) { // called from post_init_state
   // do nothing
  } else if (post_init_flag==2) { // called from post_restart
   // do nothing
  } else if (post_init_flag==0) { // called from writePlotFile
    // in: NavierStokes::prepare_post_process
   ns_level.allocate_levelsetLO(1,LEVELPC_MF);
  } else
   BoxLib::Error("post_init_flag invalid");
   
 } // ilev

 build_masksemALL();

 for (int ilev=finest_level;ilev>=level;ilev--) {
  NavierStokes& ns_level=getLevel(ilev);
  if (ilev<finest_level) {
   ns_level.MOFavgDown();
  }
 } // ilev=finest_level ... level

   // in prepare_post_process:
   // post_init_flag==0 called from writePlotFile
   // post_init_flag==1 called from post_init_state
   // post_init_flag==2 called from post_restart
   //
 int init_vof_ls_prev_time=0;
 int error_update_flag=0;
 int renormalize_only=0; // init:solid TEMP,VEL,LS,extend LSfluid into solid.
 int local_truncate=0; // do not force removal of flotsam.

 if (post_init_flag==0) { // called from writePlotFile
  // do nothing
 } else if (post_init_flag==1) {
  error_update_flag=1;  // called from post_init_state, update S_old
 } else if (post_init_flag==2) {
  error_update_flag=1;  // called from post_restart, update S_old
 } else
  BoxLib::Error("post_init_flag invalid");
	
 if (post_init_flag==1) { // called from post_init_state
  VOF_Recon_ALL(1,cur_time_slab,error_update_flag,
   init_vof_ls_prev_time,SLOPE_RECON_MF);
  makeStateDistALL();
  prescribe_solid_geometryALL(cur_time_slab,renormalize_only,local_truncate);
  int project_option=1;  // initial project
  int post_restart_flag=0;
  make_physics_varsALL(project_option,post_restart_flag);
 } else if (post_init_flag==0) { // called from writePlotFile
  if (1==1) {
   VOF_Recon_ALL(1,cur_time_slab,error_update_flag,
    init_vof_ls_prev_time,SLOPE_RECON_MF);
   int project_option=1;  // initial project
   int post_restart_flag=0;
   make_physics_varsALL(project_option,post_restart_flag);
  }
 } else if (post_init_flag==2) { // called from post_restart
  VOF_Recon_ALL(1,cur_time_slab,error_update_flag,
    init_vof_ls_prev_time,SLOPE_RECON_MF);
  if (1==0) {
   makeStateDistALL();
   prescribe_solid_geometryALL(cur_time_slab,renormalize_only,local_truncate);
  }
  int project_option=1;  // initial project
  int post_restart_flag=1;
  make_physics_varsALL(project_option,post_restart_flag);
 } else
  BoxLib::Error("post_init_flag invalid");

}  // subroutine prepare_post_process


 
// should be cur_time=0 and prev_time=-1
// called from post_init
void
NavierStokes::post_init_state () {
    
 if (level>0)
  BoxLib::Error("level>0 in post_init_state");

 SDC_setup();
 ns_time_order=parent->Time_blockingFactor();
 slab_step=ns_time_order-1; 

 SDC_outer_sweeps=0;
 SDC_setup_step();
 dt_slab=1.0;
 delta_slab_time=1.0;

 int project_option_combine=-1;
 int prescribed_noslip=1;
 int combine_flag=2;  
 int hflag=0;
 int combine_idx=-1;
 int update_flux=0;

 int nmat=num_materials;

 int project_option=1;  // initial project

 const int finest_level = parent->finestLevel();

   // inside of post_init_state

   // metrics_data
   // allocate_mdot
   // MASKCOEF
   // init_FSI_GHOST_MF
   // VOF_Recon_ALL (update_flag==1)
   // makeStateDistALL
   // prescribe_solid_geometryALL
   // make_physics_varsALL
 int post_init_flag=1; // in: post_init_state
 prepare_post_process(post_init_flag);

 for (int ilev=finest_level;ilev>=level;ilev--) {
  NavierStokes& ns_level=getLevel(ilev);

  project_option_combine=2;  // temperature in post_init_state
  prescribed_noslip=1;
  combine_flag=2;  
  hflag=0;
  combine_idx=-1;
  update_flux=0;

  ns_level.combine_state_variable(
    prescribed_noslip,
    project_option_combine,
    combine_idx,combine_flag,hflag,update_flux); 

  for (int ns=0;ns<num_species_var;ns++) {
   project_option_combine=100+ns; // species in post_init_state
   ns_level.combine_state_variable(
    prescribed_noslip,
    project_option_combine,
    combine_idx,combine_flag,hflag,update_flux); 
  }

 } // ilev=finest_level ... level

 Array<blobclass> blobdata;

 int color_count;
 int coarsest_level=0;

  // type_flag[im]=1 if material im exists in the domain.
  // type_mf(i,j,k)=im if material im dominates cell (i,j,k)
 Array<int> type_flag;
  // updates one ghost cell of TYPE_MF
  // fluid(s) and solid(s) tessellate the domain.
 TypeALL(TYPE_MF,type_flag);
  // color_count=number of colors
  // ngrow=1, FORT_EXTRAPFILL, pc_interp for COLOR_MF
 color_variable(coarsest_level, 
   COLOR_MF,TYPE_MF,&color_count,type_flag);
  // tessellate==1
 ColorSumALL(coarsest_level,color_count,TYPE_MF,COLOR_MF,blobdata);
 if (color_count!=blobdata.size())
  BoxLib::Error("color_count!=blobdata.size()");

 if (is_zalesak()) {
  for (int ilev=finest_level;ilev>=level;ilev--) {
   NavierStokes& ns_level=getLevel(ilev);
   // init cell center gas/liquid velocity for all materials.
   ns_level.zalesakVEL(); 
  }
 } else if (! is_zalesak()) {
   // do nothing
 } else
  BoxLib::Error("is_zalesak() invalid");

  // unew^{f} = unew^{c->f}
  // in post_init_state (project_option==1)
  // if project_option==1, then the velocity in the ice
  // is overwritten with a projected rigid body velocity.
 int interp_option=0;
 int idx_velcell=-1;
 Real beta=0.0;
 prescribed_noslip=1;

 increment_face_velocityALL(
   prescribed_noslip,
   interp_option,project_option,
   idx_velcell,beta,blobdata); 

 delete_array(TYPE_MF);
 delete_array(COLOR_MF);

 if ((post_init_pressure_solve==1)&&
     (!is_zalesak())) { 

  Real start_pressure_solve = ParallelDescriptor::second();

   // U^CELL and U^MAC; assimilates the solid velocity (MAC and CELL)
   // and  ice velocity (MAC)
  for (int ilev=finest_level;ilev>=level;ilev--) {
   NavierStokes& ns_level=getLevel(ilev);
   project_option_combine=3; // velocity in post_init_state
   prescribed_noslip=1;
   combine_flag=2;
   hflag=0;
   combine_idx=-1;
   update_flux=0;
   ns_level.combine_state_variable(
    prescribed_noslip,
    project_option_combine,
    combine_idx,combine_flag,hflag,update_flux);
   project_option_combine=0; // mac velocity
   update_flux=1;
   ns_level.combine_state_variable(
    prescribed_noslip,
    project_option_combine,
    combine_idx,combine_flag,hflag,update_flux);

   ns_level.make_MAC_velocity_consistent();
  }

  multiphase_project(project_option); // initial project

   // U^CELL and U^MAC
  for (int ilev=finest_level;ilev>=level;ilev--) {
   NavierStokes& ns_level=getLevel(ilev);

   project_option_combine=3; // velocity in post_init_state
   prescribed_noslip=1;
   combine_flag=2;
   hflag=0;
   combine_idx=-1;
   update_flux=0;
   ns_level.combine_state_variable(
    prescribed_noslip,
    project_option_combine,
    combine_idx,combine_flag,hflag,update_flux);
   project_option_combine=0; // mac velocity
   prescribed_noslip=0;
   update_flux=1;
   ns_level.combine_state_variable(
    prescribed_noslip,
    project_option_combine,
    combine_idx,combine_flag,hflag,update_flux);

   ns_level.make_MAC_velocity_consistent();
  }

  Real end_pressure_solve = ParallelDescriptor::second();
  if (verbose>0)
   if (ParallelDescriptor::IOProcessor())
    std::cout << "initial pressure solve time " << end_pressure_solve-
       start_pressure_solve << '\n';

 } else if ((post_init_pressure_solve==0)||(is_zalesak())) {
  // do nothing
 } else {
  BoxLib::Error("post_init_pressure_solve or is_zalesak() invalid");
 } 

 int scomp_pres=num_materials_vel*BL_SPACEDIM;
 int scomp_den=num_materials_vel*(BL_SPACEDIM+1);
 for (int ilev=finest_level;ilev>=0;ilev--) {
  NavierStokes& ns_level=getLevel(ilev);
  ns_level.avgDown(State_Type,scomp_pres,num_materials_vel,1);
  ns_level.avgDown(State_Type,scomp_den,num_state_material*nmat,1);
 } // ilev

 delete_array(MASKCOEF_MF);
 delete_array(MASK_NBR_MF);

}  // subroutine post_init_state

void
NavierStokes::level_avgDown_tag(MultiFab& S_crse,MultiFab& S_fine) {

 int scomp=0;
 int ncomp=1;

 int finest_level=parent->finestLevel();
 if (level>=finest_level)
  BoxLib::Error("level invalid in level_avgDown_tag");

 int f_level=level+1;
 NavierStokes&   fine_lev = getLevel(f_level);
 const BoxArray& fgrids=fine_lev.grids;

 if (grids!=S_crse.boxArray())
  BoxLib::Error("S_crse invalid");
 if (fgrids!=S_fine.boxArray())
  BoxLib::Error("S_fine invalid");
 if (S_crse.nComp()!=S_fine.nComp())
  BoxLib::Error("nComp mismatch");
 if (S_crse.nComp()<scomp+ncomp)
  BoxLib::Error("S_crse.nComp() invalid level_avgDown_tag");

 BoxArray crse_S_fine_BA(fgrids.size());
 for (int i = 0; i < fgrids.size(); ++i) {
  crse_S_fine_BA.set(i,BoxLib::coarsen(fgrids[i],2));
 }
 MultiFab crse_S_fine(crse_S_fine_BA,ncomp,0,Fab_allocate);

 ParallelDescriptor::Barrier();

 const Real* dx = geom.CellSize();
 const Real* dxf = fine_lev.geom.CellSize();
 const Real* prob_lo   = geom.ProbLo();

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(S_fine); mfi.isValid(); ++mfi) {
  BL_ASSERT(fgrids[mfi.index()] == mfi.validbox());
  const int gridno = mfi.index();
  const Real* xlo_fine = fine_lev.grid_loc[gridno].lo();
  const Box& ovgrid = crse_S_fine_BA[gridno];
  const int* ovlo=ovgrid.loVect();
  const int* ovhi=ovgrid.hiVect();

  FArrayBox& fine_fab=S_fine[gridno];
  const Box& fgrid=fine_fab.box();
  const int* flo=fgrid.loVect();
  const int* fhi=fgrid.hiVect();
  const Real* f_dat=fine_fab.dataPtr(scomp);

  FArrayBox& coarse_fab=crse_S_fine[gridno];
  const Box& cgrid = coarse_fab.box();
  const int* clo=cgrid.loVect();
  const int* chi=cgrid.hiVect();
  const Real* c_dat=coarse_fab.dataPtr();

  int bfact_c=parent->Space_blockingFactor(level);
  int bfact_f=parent->Space_blockingFactor(f_level);

  const Box& fine_fabgrid = fine_lev.grids[gridno];
  const int* fine_fablo=fine_fabgrid.loVect();
  const int* fine_fabhi=fine_fabgrid.hiVect();

  FORT_AVGDOWN_TAG(
   prob_lo,
   dxf,
   &level,&f_level,
   &bfact_c,&bfact_f,
   xlo_fine,dx,
   &ncomp,
   c_dat,ARLIM(clo),ARLIM(chi),
   f_dat,ARLIM(flo),ARLIM(fhi),
   ovlo,ovhi,
   fine_fablo,fine_fabhi);

 }// mfi
} //omp
 ParallelDescriptor::Barrier();
 S_crse.copy(crse_S_fine,0,scomp,ncomp);
 ParallelDescriptor::Barrier();
} // subroutine level_avgDown_tag


void
NavierStokes::level_avgDownBURNING(MultiFab& S_crse,MultiFab& S_fine) {

 int nmat=num_materials;
 int nten=( (nmat-1)*(nmat-1)+nmat-1 )/2;
 int scomp=0;
 int ncomp=nten*(BL_SPACEDIM+1);

 int finest_level=parent->finestLevel();
 if (level>=finest_level)
  BoxLib::Error("level invalid in level_avgDownBURNING");

 int f_level=level+1;
 NavierStokes&   fine_lev = getLevel(f_level);
 const BoxArray& fgrids=fine_lev.grids;

 if (grids!=S_crse.boxArray())
  BoxLib::Error("S_crse invalid");
 if (fgrids!=S_fine.boxArray())
  BoxLib::Error("S_fine invalid");
 if (S_crse.nComp()!=S_fine.nComp())
  BoxLib::Error("nComp mismatch");
 if (S_crse.nComp()!=scomp+ncomp)
  BoxLib::Error("S_crse.nComp() invalid level_avgDownBurning");

 BoxArray crse_S_fine_BA(fgrids.size());
 for (int i = 0; i < fgrids.size(); ++i) {
  crse_S_fine_BA.set(i,BoxLib::coarsen(fgrids[i],2));
 }
 MultiFab crse_S_fine(crse_S_fine_BA,ncomp,0,Fab_allocate);

 ParallelDescriptor::Barrier();

 const Real* dx = geom.CellSize();
 const Real* dxf = fine_lev.geom.CellSize();
 const Real* prob_lo   = geom.ProbLo();

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(S_fine); mfi.isValid(); ++mfi) {
  BL_ASSERT(fgrids[mfi.index()] == mfi.validbox());
  const int gridno = mfi.index();
  const Real* xlo_fine = fine_lev.grid_loc[gridno].lo();
  const Box& ovgrid = crse_S_fine_BA[gridno];
  const int* ovlo=ovgrid.loVect();
  const int* ovhi=ovgrid.hiVect();

  FArrayBox& fine_fab=S_fine[gridno];
  const Box& fgrid=fine_fab.box();
  const int* flo=fgrid.loVect();
  const int* fhi=fgrid.hiVect();
  const Real* f_dat=fine_fab.dataPtr(scomp);

  FArrayBox& coarse_fab=crse_S_fine[gridno];
  const Box& cgrid = coarse_fab.box();
  const int* clo=cgrid.loVect();
  const int* chi=cgrid.hiVect();
  const Real* c_dat=coarse_fab.dataPtr();

  int bfact_c=parent->Space_blockingFactor(level);
  int bfact_f=parent->Space_blockingFactor(f_level);

  const Box& fine_fabgrid = fine_lev.grids[gridno];
  const int* fine_fablo=fine_fabgrid.loVect();
  const int* fine_fabhi=fine_fabgrid.hiVect();

  FORT_AVGDOWN_BURNING(
   prob_lo,
   dxf,
   &level,&f_level,
   &bfact_c,&bfact_f,
   xlo_fine,dx,
   &ncomp,
   &nmat,
   &nten,
   c_dat,ARLIM(clo),ARLIM(chi),
   f_dat,ARLIM(flo),ARLIM(fhi),
   ovlo,ovhi,
   fine_fablo,fine_fabhi);

 }// mfi
} //omp
 ParallelDescriptor::Barrier();
 S_crse.copy(crse_S_fine,0,scomp,ncomp);
 ParallelDescriptor::Barrier();
} // subroutine level_avgDownBURNING


void
NavierStokes::level_avgDownCURV(MultiFab& S_crse,MultiFab& S_fine) {

 int nmat=num_materials;
 int nten=( (nmat-1)*(nmat-1)+nmat-1 )/2;

 int scomp=0;
 int ncomp=nten*(BL_SPACEDIM+5);

 int finest_level=parent->finestLevel();
 if (level>=finest_level)
  BoxLib::Error("level invalid in level_avgDownCURV");

 int f_level=level+1;
 NavierStokes&   fine_lev = getLevel(f_level);
 const BoxArray& fgrids=fine_lev.grids;

 if (grids!=S_crse.boxArray())
  BoxLib::Error("S_crse invalid");
 if (fgrids!=S_fine.boxArray())
  BoxLib::Error("S_fine invalid");
 if (S_crse.nComp()!=S_fine.nComp())
  BoxLib::Error("nComp mismatch");
 if (S_crse.nComp()!=scomp+ncomp)
  BoxLib::Error("S_crse.nComp() invalid level_avgDownCURV");

 BoxArray crse_S_fine_BA(fgrids.size());
 for (int i = 0; i < fgrids.size(); ++i) {
  crse_S_fine_BA.set(i,BoxLib::coarsen(fgrids[i],2));
 }
 MultiFab crse_S_fine(crse_S_fine_BA,ncomp,0,Fab_allocate);

 ParallelDescriptor::Barrier();

 const Real* dx = geom.CellSize();
 const Real* dxf = fine_lev.geom.CellSize();
 const Real* prob_lo   = geom.ProbLo();

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(S_fine); mfi.isValid(); ++mfi) {
  BL_ASSERT(fgrids[mfi.index()] == mfi.validbox());
  const int gridno = mfi.index();
  const Real* xlo_fine = fine_lev.grid_loc[gridno].lo();
  const Box& ovgrid = crse_S_fine_BA[gridno];
  const int* ovlo=ovgrid.loVect();
  const int* ovhi=ovgrid.hiVect();

  FArrayBox& fine_fab=S_fine[gridno];
  const Box& fgrid=fine_fab.box();
  const int* flo=fgrid.loVect();
  const int* fhi=fgrid.hiVect();
  const Real* f_dat=fine_fab.dataPtr(scomp);

  FArrayBox& coarse_fab=crse_S_fine[gridno];
  const Box& cgrid = coarse_fab.box();
  const int* clo=cgrid.loVect();
  const int* chi=cgrid.hiVect();
  const Real* c_dat=coarse_fab.dataPtr();

  int bfact_c=parent->Space_blockingFactor(level);
  int bfact_f=parent->Space_blockingFactor(f_level);

  const Box& fine_fabgrid = fine_lev.grids[gridno];
  const int* fine_fablo=fine_fabgrid.loVect();
  const int* fine_fabhi=fine_fabgrid.hiVect();

  FORT_AVGDOWN_CURV(
   prob_lo,
   dxf,
   &level,&f_level,
   &bfact_c,&bfact_f,
   xlo_fine,dx,
   &ncomp,&nmat,&nten,
   c_dat,ARLIM(clo),ARLIM(chi),
   f_dat,ARLIM(flo),ARLIM(fhi),
   ovlo,ovhi,
   fine_fablo,fine_fabhi);

 }// mfi
} //omp
 ParallelDescriptor::Barrier();
 S_crse.copy(crse_S_fine,0,scomp,ncomp);
 ParallelDescriptor::Barrier();

} // subroutine level_avgDownCURV


// spectral_override==0 => always low order.
void
NavierStokes::avgDown(MultiFab& S_crse,MultiFab& S_fine,
  int scomp,int ncomp,int spectral_override) {

 int finest_level=parent->finestLevel();
 if (level>=finest_level)
  BoxLib::Error("level invalid in avgDown");

 int f_level=level+1;
 NavierStokes&   fine_lev = getLevel(f_level);
 const BoxArray& fgrids=fine_lev.grids;

 if (grids!=S_crse.boxArray())
  BoxLib::Error("S_crse invalid");
 if (fgrids!=S_fine.boxArray())
  BoxLib::Error("S_fine invalid");
 if (S_crse.nComp()!=S_fine.nComp())
  BoxLib::Error("nComp mismatch");

 BoxArray crse_S_fine_BA(fgrids.size());
 for (int i = 0; i < fgrids.size(); ++i) {
  crse_S_fine_BA.set(i,BoxLib::coarsen(fgrids[i],2));
 }
 MultiFab crse_S_fine(crse_S_fine_BA,ncomp,0,Fab_allocate);

 ParallelDescriptor::Barrier();

 const Real* dx = geom.CellSize();
 const Real* dxf = fine_lev.geom.CellSize();
 const Real* prob_lo   = geom.ProbLo();

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(S_fine); mfi.isValid(); ++mfi) {
  BL_ASSERT(fgrids[mfi.index()] == mfi.validbox());
  const int gridno = mfi.index();
  const Real* xlo_fine = fine_lev.grid_loc[gridno].lo();
  const Box& ovgrid = crse_S_fine_BA[gridno];
  const int* ovlo=ovgrid.loVect();
  const int* ovhi=ovgrid.hiVect();

  FArrayBox& fine_fab=S_fine[gridno];
  const Box& fgrid=fine_fab.box();
  const int* flo=fgrid.loVect();
  const int* fhi=fgrid.hiVect();
  const Real* f_dat=fine_fab.dataPtr(scomp);

  FArrayBox& coarse_fab=crse_S_fine[gridno];
  const Box& cgrid = coarse_fab.box();
  const int* clo=cgrid.loVect();
  const int* chi=cgrid.hiVect();
  const Real* c_dat=coarse_fab.dataPtr();

  int bfact_c=parent->Space_blockingFactor(level);
  int bfact_f=parent->Space_blockingFactor(f_level);

  const Box& fine_fabgrid = fine_lev.grids[gridno];
  const int* fine_fablo=fine_fabgrid.loVect();
  const int* fine_fabhi=fine_fabgrid.hiVect();

  if (spectral_override==1) {

   FArrayBox& maskfab=(*fine_lev.localMF[MASKSEM_MF])[mfi];
   FORT_AVGDOWN( 
    &enable_spectral,
    &finest_level,
    &spectral_override,
    prob_lo,
    dxf,
    &level,&f_level,
    &bfact_c,&bfact_f,     
    xlo_fine,dx,
    &ncomp,
    c_dat,ARLIM(clo),ARLIM(chi),
    f_dat,ARLIM(flo),ARLIM(fhi),
    maskfab.dataPtr(),
    ARLIM(maskfab.loVect()),ARLIM(maskfab.hiVect()),
    ovlo,ovhi, 
    fine_fablo,fine_fabhi);

  } else if (spectral_override==0) {

   FORT_AVGDOWN_LOW(
    prob_lo,
    dxf,
    &level,&f_level,
    &bfact_c,&bfact_f,
    xlo_fine,dx,
    &ncomp,
    c_dat,ARLIM(clo),ARLIM(chi),
    f_dat,ARLIM(flo),ARLIM(fhi),
    ovlo,ovhi,
    fine_fablo,fine_fabhi);

  } else
   BoxLib::Error("spectral_override invalid");

 }// mfi
} //omp
 ParallelDescriptor::Barrier();
 S_crse.copy(crse_S_fine,0,scomp,ncomp);
 ParallelDescriptor::Barrier();
}  // subroutine avgDown

// spectral_override==1 => order derived from "enable_spectral"
// spectral_override==0 => always low order.
void 
NavierStokes::avgDown_list(int stateidx,Array<int> scomp,
   Array<int> ncomp,int spectral_override) {

 int ncomp_list=0;
 for (int ilist=0;ilist<scomp.size();ilist++) {
  avgDown(stateidx,scomp[ilist],ncomp[ilist],spectral_override);
  ncomp_list+=ncomp[ilist];
 }
 if (ncomp_list<=0)
  BoxLib::Error("ncomp_list invalid");

} // subroutine avgDown_list

// spectral_override==1 => order derived from "enable_spectral"
// spectral_override==0 => always low order.
void
NavierStokes::avgDown(int stateidx,int startcomp,int numcomp,
 int spectral_override) {

 if ((stateidx!=LS_Type)&&
     (stateidx!=State_Type)&&
     (stateidx!=DIV_Type)&&
     (stateidx!=Tensor_Type)) {
  std::cout << "stateidx= " << stateidx << '\n';
  std::cout << "startcomp= " << startcomp << '\n';
  std::cout << "numcomp= " << numcomp << '\n';
  BoxLib::Error("stateidx invalid");
 }

 int finest_level=parent->finestLevel();

 if (level==finest_level) {
  // do nothing
 } else if ((level>=0)&&(level<finest_level)) {

  NavierStokes&   fine_lev = getLevel(level+1);
  MultiFab& S_crse = get_new_data(stateidx,slab_step+1);
  MultiFab& S_fine = fine_lev.get_new_data(stateidx,slab_step+1);
  avgDown(S_crse,S_fine,startcomp,numcomp,spectral_override);

 } else
  BoxLib::Error("level invalid");

} // subroutine avgDown



void NavierStokes::MOFavgDown() {

 int finest_level=parent->finestLevel();

 if (level == finest_level)
  return;

 int f_level=level+1;
 NavierStokes&   fine_lev = getLevel(f_level);
 const BoxArray& fgrids=fine_lev.grids;
 resize_metrics(1);
 debug_ngrow(VOLUME_MF,0,80);
 fine_lev.resize_metrics(1);
 fine_lev.debug_ngrow(VOLUME_MF,1,81);

 MultiFab& S_fine=fine_lev.get_new_data(State_Type,slab_step+1);
 MultiFab& S_crse = get_new_data(State_Type,slab_step+1);

 const Real* dxf = fine_lev.geom.CellSize();
 const Real* dxc = geom.CellSize();
 const Real* prob_lo   = geom.ProbLo();

 if (grids!=S_crse.boxArray())
  BoxLib::Error("S_crse invalid");
 if (fgrids!=S_fine.boxArray())
  BoxLib::Error("S_fine invalid");
 if (S_crse.nComp()!=S_fine.nComp())
  BoxLib::Error("nComp mismatch");

 BoxArray crse_S_fine_BA(fgrids.size());
 for (int i = 0; i < fgrids.size(); ++i) {
  crse_S_fine_BA.set(i,BoxLib::coarsen(fgrids[i],2));
 }

 int nmat=num_materials;
 int scomp_mofvars=num_materials_vel*(BL_SPACEDIM+1)+
   nmat*num_state_material;

 MultiFab crse_S_fine(crse_S_fine_BA,nmat*ngeom_raw,0,Fab_allocate);

 ParallelDescriptor::Barrier();

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(S_fine); mfi.isValid(); ++mfi) {
  BL_ASSERT(fgrids[mfi.index()] == mfi.validbox());
  const int gridno = mfi.index();
  const Box& ovgrid = crse_S_fine_BA[gridno];
  const int* ovlo=ovgrid.loVect();
  const int* ovhi=ovgrid.hiVect();
  
  FArrayBox& finefab=S_fine[gridno];
  const Box& fgrid=finefab.box();
  const int* flo=fgrid.loVect();
  const int* fhi=fgrid.hiVect();
  const Real* f_dat=finefab.dataPtr(scomp_mofvars);

  FArrayBox& coarsefab=crse_S_fine[gridno];
  const Box& cgrid = coarsefab.box();
  const int* clo=cgrid.loVect();
  const int* chi=cgrid.hiVect();
  const Real* c_dat=coarsefab.dataPtr();

  int bfact_c=parent->Space_blockingFactor(level);
  int bfact_f=parent->Space_blockingFactor(f_level);

  FORT_MOFAVGDOWN(
   &cur_time_slab,
   prob_lo,
   dxc,
   dxf,
   &bfact_c,&bfact_f,
   c_dat,ARLIM(clo),ARLIM(chi),
   f_dat,ARLIM(flo),ARLIM(fhi),
   ovlo,ovhi,&nmat);
 } // mfi
} //omp
 ParallelDescriptor::Barrier();
 S_crse.copy(crse_S_fine,0,scomp_mofvars,nmat*ngeom_raw);
 ParallelDescriptor::Barrier();
}


void NavierStokes::avgDownError() {

 int finest_level=parent->finestLevel();

 if (level == finest_level)
  return;

 int f_level=level+1;
 NavierStokes&   fine_lev = getLevel(f_level);
 const BoxArray& fgrids=fine_lev.grids;
 resize_metrics(1);
 debug_ngrow(VOLUME_MF,0,80);
 fine_lev.resize_metrics(1);
 fine_lev.debug_ngrow(VOLUME_MF,1,81);

 MultiFab& S_fine=fine_lev.get_new_data(State_Type,slab_step+1);
 MultiFab& S_crse = get_new_data(State_Type,slab_step+1);

 const Real* dxf = fine_lev.geom.CellSize();
 const Real* prob_lo   = geom.ProbLo();

 if (grids!=S_crse.boxArray())
  BoxLib::Error("S_crse invalid");
 if (fgrids!=S_fine.boxArray())
  BoxLib::Error("S_fine invalid");
 if (S_crse.nComp()!=S_fine.nComp())
  BoxLib::Error("nComp mismatch");

 BoxArray crse_S_fine_BA(fgrids.size());
 for (int i = 0; i < fgrids.size(); ++i) {
  crse_S_fine_BA.set(i,BoxLib::coarsen(fgrids[i],2));
 }

 int nmat=num_materials;
 int scomp_error=num_materials_vel*(BL_SPACEDIM+1)+
   nmat*num_state_material+nmat*ngeom_raw;
 if (S_crse.nComp()!=scomp_error+1)
  BoxLib::Error("scomp_error invalid");

 MultiFab crse_S_fine(crse_S_fine_BA,1,0,Fab_allocate);

 ParallelDescriptor::Barrier();

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(S_fine); mfi.isValid(); ++mfi) {
  BL_ASSERT(fgrids[mfi.index()] == mfi.validbox());
  const int gridno = mfi.index();
  const Box& ovgrid = crse_S_fine_BA[gridno];
  const int* ovlo=ovgrid.loVect();
  const int* ovhi=ovgrid.hiVect();
  
  FArrayBox& finefab=S_fine[gridno];
  const Box& fgrid=finefab.box();
  const int* flo=fgrid.loVect();
  const int* fhi=fgrid.hiVect();
  const Real* f_dat=finefab.dataPtr(scomp_error);

  FArrayBox& coarsefab=crse_S_fine[gridno];
  const Box& cgrid = coarsefab.box();
  const int* clo=cgrid.loVect();
  const int* chi=cgrid.hiVect();
  const Real* c_dat=coarsefab.dataPtr();

  int bfact_c=parent->Space_blockingFactor(level);
  int bfact_f=parent->Space_blockingFactor(f_level);

  FORT_ERRORAVGDOWN(
   prob_lo,
   dxf,
   &bfact_c,&bfact_f,
   c_dat,ARLIM(clo),ARLIM(chi),
   f_dat,ARLIM(flo),ARLIM(fhi),
   ovlo,ovhi);
 } // mfi
} //omp
 ParallelDescriptor::Barrier();
 S_crse.copy(crse_S_fine,0,scomp_error,1);
 ParallelDescriptor::Barrier();
} // subroutine avgDownError


void NavierStokes::getBCArray_list(Array<int>& listbc,int state_index,
     int gridno,Array<int> scomp,Array<int> ncomp) {

 int ncomp_list=0;
 for (int ilist=0;ilist<scomp.size();ilist++)
  ncomp_list+=ncomp[ilist];
 if (ncomp_list<=0)
  BoxLib::Error("ncomp_list invalid");

 listbc.resize(ncomp_list*BL_SPACEDIM*2);

 int dcomp=0;
 for (int ilist=0;ilist<scomp.size();ilist++) {
  Array<int> bc_single=getBCArray(state_index,gridno,
    scomp[ilist],ncomp[ilist]);

  int scomp=0;
  for (int nn=0;nn<ncomp[ilist];nn++) {
   for (int side=0;side<=1;side++) {
    for (int dir=0;dir<BL_SPACEDIM;dir++) {
     listbc[dcomp]=bc_single[scomp];
     scomp++;
     dcomp++;
    } // dir
   } // side
  } // nn
 } // ilist
 if (dcomp!=ncomp_list*BL_SPACEDIM*2)
  BoxLib::Error("dcomp invalid");

}  // subroutine getBCArray_list

MultiFab* NavierStokes::getState_list(
 int ngrow,Array<int> scomp,Array<int> ncomp,
 Real time) {

 int ncomp_list=0;
 for (int ilist=0;ilist<scomp.size();ilist++)
  ncomp_list+=ncomp[ilist];

 if (ncomp_list<=0)
  BoxLib::Error("ncomp_list invalid");
 
 MultiFab* mf = new MultiFab(state[State_Type].boxArray(),ncomp_list,
   ngrow,dmap,Fab_allocate);

 int dcomp=0;
 for (int ilist=0;ilist<scomp.size();ilist++) {
  MultiFab* mf_single=getState(ngrow,scomp[ilist],ncomp[ilist],time);
  MultiFab::Copy(*mf,*mf_single,0,dcomp,ncomp[ilist],ngrow); 
  dcomp+=ncomp[ilist];
  delete mf_single;
 }
 if (dcomp!=ncomp_list)
  BoxLib::Error("dcomp invalid");

 return mf;

} // subroutine getState_list


MultiFab* NavierStokes::getState (
  int ngrow, int  scomp,
  int ncomp, Real time) {

 int nmat=num_materials;
 int scomp_mofvars=num_materials_vel*(BL_SPACEDIM+1)+
  nmat*num_state_material;
 int scomp_error=scomp_mofvars+nmat*ngeom_raw;

 if ((scomp<scomp_error)&&(scomp+ncomp-1>=scomp_mofvars)) {

  if ((scomp<=scomp_mofvars)&&(scomp+ncomp>=scomp_error)) {
   // do nothing
  } else {
   std::cout << "VOF: scomp,ncomp " << scomp << ' ' << ncomp << '\n';
   BoxLib::Error("must get all vof data at once getState");
  }

 }

 MultiFab& S_new=get_new_data(State_Type,slab_step+1);
 int ntotal=S_new.nComp();
 if (scomp<0)
  BoxLib::Error("scomp invalid getState"); 
 if (ncomp<=0)
  BoxLib::Error("ncomp invalid in get state"); 
 if (scomp+ncomp>ntotal)
  BoxLib::Error("scomp,ncomp invalid");

 MultiFab* mf = new MultiFab(state[State_Type].boxArray(),ncomp,
   ngrow,dmap,Fab_allocate);

 FillPatch(*this,*mf,0,time,State_Type,scomp,ncomp);

 ParallelDescriptor::Barrier();

 return mf;
} // end subroutine getState

MultiFab* NavierStokes::getStateSolid (
  int ngrow, int  scomp,
  int ncomp, Real time) {

 int nmat=num_materials;

  // nparts x (velocity + LS + temperature + flag)
 int nparts=im_solid_map.size();
 if ((nparts<1)||(nparts>=nmat))
  BoxLib::Error("nparts invalid");
 if (ncomp%BL_SPACEDIM!=0)
  BoxLib::Error("ncomp invalid");
 int partid=scomp/BL_SPACEDIM;
 if ((partid<0)||(partid>=nparts))
  BoxLib::Error("partid invalid");

 MultiFab& Solid_new=get_new_data(Solid_State_Type,slab_step+1);
 int ntotal=Solid_new.nComp();
 if (scomp<0)
  BoxLib::Error("scomp invalid getStateSolid"); 
 if (ncomp<=0)
  BoxLib::Error("ncomp invalid in getstateSolid"); 
 if (scomp+ncomp>ntotal)
  BoxLib::Error("scomp,ncomp invalid");

 MultiFab* mf = new MultiFab(state[Solid_State_Type].boxArray(),ncomp,
   ngrow,dmap,Fab_allocate);

 FillPatch(*this,*mf,0,time,Solid_State_Type,scomp,ncomp);

 ParallelDescriptor::Barrier();

 return mf;
} // end subroutine getStateSolid



MultiFab* NavierStokes::getStateTensor (
  int ngrow, int  scomp,
  int ncomp, Real time) {

 int nmat=num_materials;

  // nparts x NUM_TENSOR_TYPE
 int nparts=im_elastic_map.size();
 if ((nparts<1)||(nparts>nmat))
  BoxLib::Error("nparts invalid");
 if (ncomp%NUM_TENSOR_TYPE!=0)
  BoxLib::Error("ncomp invalid");
 int partid=scomp/NUM_TENSOR_TYPE;
 if ((partid<0)||(partid>=nparts))
  BoxLib::Error("partid invalid");

 MultiFab& Tensor_new=get_new_data(Tensor_Type,slab_step+1);
 int ntotal=Tensor_new.nComp();
 if (scomp<0)
  BoxLib::Error("scomp invalid getStateTensor"); 
 if (ncomp<=0)
  BoxLib::Error("ncomp invalid in getstateTensor"); 
 if (scomp+ncomp>ntotal)
  BoxLib::Error("scomp,ncomp invalid");

 MultiFab* mf = new MultiFab(state[Tensor_Type].boxArray(),ncomp,
   ngrow,dmap,Fab_allocate);

 FillPatch(*this,*mf,0,time,Tensor_Type,scomp,ncomp);

 ParallelDescriptor::Barrier();

 return mf;
} // end subroutine getStateTensor


MultiFab* NavierStokes::getStateDist (int ngrow,Real time,int caller_id) {

 if (verbose>0)
  if (ParallelDescriptor::IOProcessor())
   std::cout << "getStateDist: time,caller_id " << time << ' ' << 
     caller_id <<'\n';

 if ((ngrow<0)||(ngrow>ngrow_distance))
  BoxLib::Error("ngrow invalid");

 int nmat=num_materials;
 
 MultiFab& S_new=get_new_data(LS_Type,slab_step+1);
 int ntotal=S_new.nComp();
 if (ntotal!=nmat*(BL_SPACEDIM+1))
  BoxLib::Error("ntotal invalid");

 MultiFab* mf = new MultiFab(state[State_Type].boxArray(),
   nmat*(BL_SPACEDIM+1),
   ngrow,dmap,Fab_allocate);

  // scomp=0
 FillPatch(*this,*mf,0,time,LS_Type,0,nmat*(BL_SPACEDIM+1));

 ParallelDescriptor::Barrier();

 return mf;
} // subroutine getStateDist


MultiFab* NavierStokes::getStateDIV_DATA(int ngrow,
		int scomp,int ncomp,Real time) {

 if (num_materials_vel!=1)
  BoxLib::Error("num_materials_vel!=1");

 int project_option=10;
 int save_bc_status=override_bc_to_homogeneous;
 override_bc_to_homogeneous=1;
 FORT_OVERRIDEPBC(&override_bc_to_homogeneous,&project_option);

 MultiFab& S_new=get_new_data(DIV_Type,slab_step+1);
 int ntotal=S_new.nComp();
 if (ntotal!=num_materials_vel)
  BoxLib::Error("ntotal invalid");

 MultiFab* mf = new MultiFab(state[DIV_Type].boxArray(),ncomp,
   ngrow,dmap,Fab_allocate);

 FillPatch(*this,*mf,0,time,DIV_Type,scomp,ncomp);

 ParallelDescriptor::Barrier();

 override_bc_to_homogeneous=save_bc_status;
 FORT_OVERRIDEPBC(&override_bc_to_homogeneous,&project_option);

 return mf;
} // subroutine getStateDIV_DATA


// called from:
// NavierStokes::prepare_post_process  (post_init_flag==1)
// NavierStokes::do_the_advance
void
NavierStokes::makeStateDistALL() {

 if (level!=0)
  BoxLib::Error("level invalid in makeStateDistALL");

 int finest_level=parent->finestLevel();

 Real problo[BL_SPACEDIM];
 Real probhi[BL_SPACEDIM];
 Real problen[BL_SPACEDIM];
 max_problen=0.0;
 for (int dir=0;dir<BL_SPACEDIM;dir++) {
  problo[dir]=Geometry::ProbLo(dir);
  probhi[dir]=Geometry::ProbHi(dir);
  problen[dir]=probhi[dir]-problo[dir];
  if (problen[dir]>0.0) {
   max_problen+=(problen[dir]*problen[dir]);
  } else
   BoxLib::Error("problen[dir] invalid");
 }
 max_problen=sqrt(max_problen);
 if (max_problen>0.0) {
  // do nothing
 } else
  BoxLib::Error("max_problen invalid");

 int nmat=num_materials;
 minLS.resize(thread_class::nthreads);
 maxLS.resize(thread_class::nthreads);
 for (int tid=0;tid<thread_class::nthreads;tid++) {
  minLS[tid].resize(nmat);
  maxLS[tid].resize(nmat);
  for (int im=0;im<nmat;im++) {
   minLS[tid][im]=max_problen;
   maxLS[tid][im]=-max_problen;
  } // tid
 }

  // traverse from coarsest to finest so that
  // coarse data normals will be available for filling in
  // ghost values.
  // Also do a fillCoarsePatch for the level set functions and DIST_TOUCH_MF
  // (using piecewise constant interp) in order to init. some level set
  // function values.
 for (int ilev=level;ilev<=finest_level;ilev++) {
  NavierStokes& ns_level=getLevel(ilev);
  ns_level.makeStateDist();
 }
  // CORRECT_UNINIT is in MOF_REDIST_3D.F90
 for (int ilev=level;ilev<=finest_level;ilev++) {
  NavierStokes& ns_level=getLevel(ilev);
  ns_level.correct_dist_uninit();
 }

 for (int ilev=level;ilev<=finest_level;ilev++) {
  NavierStokes& ns_level=getLevel(ilev);
  ns_level.delete_localMF(FACEFRAC_MF,1);
  ns_level.delete_localMF(FACETEST_MF,1);
  ns_level.delete_localMF(ORIGDIST_MF,1);
  ns_level.delete_localMF(STENCIL_MF,1);
  ns_level.delete_localMF(DIST_TOUCH_MF,1);
 }


 if (verbose>0)
  if (ParallelDescriptor::IOProcessor())
   for (int im=0;im<nmat;im++) {
    std::cout << "im= " << im << '\n';
    std::cout << "minLS = " << minLS[0][im] << '\n';
    std::cout << "maxLS = " << maxLS[0][im] << '\n';
   }

} // subroutine makeStateDistALL()

void 
NavierStokes::build_NRM_FD_MF(int fd_mf,int ls_mf,int ngrow) {

 int nmat=num_materials;
 bool use_tiling=ns_tiling;
 int finest_level=parent->finestLevel();
 const Real* dx = geom.CellSize();

 if (ngrow>=0) {
  if ((localMF[fd_mf]->nGrow()>=ngrow)&&
      (localMF[ls_mf]->nGrow()>=ngrow)) {
   if ((localMF[fd_mf]->nComp()==nmat*BL_SPACEDIM)&&
       (localMF[ls_mf]->nComp()==nmat*(BL_SPACEDIM+1))) {
    MultiFab::Copy(*localMF[fd_mf],*localMF[ls_mf],nmat,0,nmat*BL_SPACEDIM,1);

#ifdef _OPENMP
#pragma omp parallel
#endif
{
    for (MFIter mfi(*localMF[fd_mf],use_tiling); mfi.isValid(); ++mfi) {
     BL_ASSERT(grids[mfi.index()] == mfi.validbox());
     const int gridno = mfi.index();
     const Box& tilegrid = mfi.tilebox();
     const Box& fabgrid = grids[gridno];
     const int* tilelo=tilegrid.loVect();
     const int* tilehi=tilegrid.hiVect();
     const int* fablo=fabgrid.loVect();
     const int* fabhi=fabgrid.hiVect();
     int bfact=parent->Space_blockingFactor(level);

     const Real* xlo = grid_loc[gridno].lo();

     FArrayBox& lsfab=(*localMF[ls_mf])[mfi];
     FArrayBox& nrmfab=(*localMF[fd_mf])[mfi];

      // in: MOF_REDIST_3D.F90
     FORT_FD_NORMAL( 
      &level,
      &finest_level,
      lsfab.dataPtr(),
      ARLIM(lsfab.loVect()),ARLIM(lsfab.hiVect()),
      nrmfab.dataPtr(),
      ARLIM(nrmfab.loVect()),ARLIM(nrmfab.hiVect()),
      tilelo,tilehi,
      fablo,fabhi,
      &bfact,
      xlo,dx,
      &nmat);
    } // mfi
} // omp

    ParallelDescriptor::Barrier();

   } else
    BoxLib::Error("fd_mf or ls_mf nComp() invalid");
  } else
   BoxLib::Error("fd_mf or ls_mf nGrow() invalid");
 } else
  BoxLib::Error("ngrow invalid");
			 
} // subroutine build_NRM_FD_MF

void
NavierStokes::makeStateDist() {

 bool use_tiling=ns_tiling;

 int finest_level=parent->finestLevel();

 int profile_dist=0;

 int dist_timings=1;

 Real before_dist;
 Real after_dist;
 Real before_profile;
 Real after_profile;

 if (dist_timings==1)
  before_dist = ParallelDescriptor::second();

 int rzflag=0;
 if (CoordSys::IsRZ())
  rzflag=1;
 else if (CoordSys::IsCartesian())
  rzflag=0;
 else if (CoordSys::IsCYLINDRICAL())
  rzflag=3;
 else
  BoxLib::Error("CoordSys bust 6");

 const Real* dx = geom.CellSize();
 int nmat=num_materials;
 int scomp_mofvars=num_materials_vel*(BL_SPACEDIM+1)+
  nmat*num_state_material;
 int nten=( (nmat-1)*(nmat-1)+nmat-1 )/2;

 MultiFab& LS_new = get_new_data(LS_Type,slab_step+1);

 getStateDist_localMF(ORIGDIST_MF,ngrow_distance,cur_time_slab,15);

 resize_mask_nbr(ngrow_distance);
 debug_ngrow(MASK_NBR_MF,ngrow_distance,90);
 if (localMF[MASK_NBR_MF]->nComp()!=4)
  BoxLib::Error("invalid ncomp for mask nbr");
 VOF_Recon_resize(ngrow_distance,SLOPE_RECON_MF);
 debug_ngrow(SLOPE_RECON_MF,ngrow_distance,90);
 debug_ngrow(ORIGDIST_MF,ngrow_distance,90);
 if (localMF[ORIGDIST_MF]->nComp()!=nmat*(1+BL_SPACEDIM))
  BoxLib::Error("invalid ncomp for origdist");

 if (profile_dist==1)
  before_profile = ParallelDescriptor::second();

 new_localMF(DIST_TOUCH_MF,nmat,0,-1);
 localMF[DIST_TOUCH_MF]->setVal(0.0);

 MultiFab* dist_coarse_mf;
 MultiFab* dist_touch_coarse_mf;

 if ((level>0)&&(level<=finest_level)) {
  dist_coarse_mf=new MultiFab(grids,nmat*(BL_SPACEDIM+1),0,dmap,Fab_allocate);
  int dcomp=0;
  int scomp=0;
  FillCoarsePatch(*dist_coarse_mf,dcomp,cur_time_slab,LS_Type,scomp,
        nmat*(BL_SPACEDIM+1));

  // idx,scomp,ncomp,index,scompBC_map
  // FillCoarsePatchGHOST is ultimately called.
  // dest_lstGHOST for State_Type defaults to pc_interp.
  // scompBC_map==0 corresponds to pc_interp and FORT_EXTRAPFILL
  dist_touch_coarse_mf=
   new MultiFab(grids,nmat,0,dmap,Fab_allocate);

  for (int i=0;i<nmat;i++) {
   Array<int> scompBC_map;
   scompBC_map.resize(1);
   scompBC_map[0]=0;
   PCINTERP_fill_coarse_patch(DIST_TOUCH_MF,i,
     1,State_Type,scompBC_map);
  } // i=0..nmat-1
  MultiFab::Copy(*dist_touch_coarse_mf,*localMF[DIST_TOUCH_MF],0,0,nmat,0);
  localMF[DIST_TOUCH_MF]->setVal(0.0);

 } else if (level==0) {

  dist_coarse_mf=localMF[ORIGDIST_MF];
  dist_touch_coarse_mf=localMF[DIST_TOUCH_MF];

 } else
  BoxLib::Error("level invalid 6"); 

 if (profile_dist==1) {
  after_profile = ParallelDescriptor::second();
  if (ParallelDescriptor::IOProcessor()) {
   std::cout << "level= " << level << '\n';
   std::cout << "coarse dist time " << after_profile-before_profile << '\n';
  }
 }

 if (profile_dist==1)
  before_profile = ParallelDescriptor::second();

 int nstar=9;
 if (BL_SPACEDIM==3)
  nstar*=3;
 int nface=nmat*BL_SPACEDIM*2*(1+BL_SPACEDIM); 

 new_localMF(STENCIL_MF,nstar,ngrow_distance,-1);
 localMF[STENCIL_MF]->setVal(0.0);

 int do_face_decomp=0;
 int tessellate=0;
  // FACEINIT is in: MOF_REDIST_3D.F90
 makeFaceFrac(tessellate,ngrow_distance,FACEFRAC_MF,do_face_decomp);

 if (profile_dist==1) {
  after_profile = ParallelDescriptor::second();
  if (ParallelDescriptor::IOProcessor()) {
   std::cout << "level= " << level << '\n';
   std::cout << "makeFaceFrac time " << after_profile-before_profile << '\n';
  }
 }

 if (profile_dist==1)
  before_profile = ParallelDescriptor::second();

  // FACEINITTEST is in MOF_REDIST_3D.F90
  // FACETEST_MF has nmat * sdim components.
 makeFaceTest(tessellate,ngrow_distance,FACETEST_MF);

 if (profile_dist==1) {
  after_profile = ParallelDescriptor::second();
  if (ParallelDescriptor::IOProcessor()) {
   std::cout << "level= " << level << '\n';
   std::cout << "makeFaceTest time " << after_profile-before_profile << '\n';
  }
 }

 if (profile_dist==1)
  before_profile = ParallelDescriptor::second();

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(LS_new,use_tiling); mfi.isValid(); ++mfi) {
   BL_ASSERT(grids[mfi.index()] == mfi.validbox());
   const int gridno = mfi.index();
   const Box& tilegrid = mfi.tilebox();
   const Box& fabgrid = grids[gridno];
   const int* tilelo=tilegrid.loVect();
   const int* tilehi=tilegrid.hiVect();
   const int* fablo=fabgrid.loVect();
   const int* fabhi=fabgrid.hiVect();
   int bfact=parent->Space_blockingFactor(level);

   const Real* xlo = grid_loc[gridno].lo();

   FArrayBox& maskfab=(*localMF[MASK_NBR_MF])[mfi];
   FArrayBox& voffab=(*localMF[SLOPE_RECON_MF])[mfi];
   FArrayBox& stencilfab=(*localMF[STENCIL_MF])[mfi];

    // in: MOF_REDIST_3D.F90
    // internally sets tessellate=0
    // 3x3 stencil for each cell in 2D
    // 3x3x3 stencil for each cell in 3D
    // fluid material id for each cell edge point is initialized.
   FORT_STENINIT( 
    &level,
    &finest_level,
    stencilfab.dataPtr(),
    ARLIM(stencilfab.loVect()),ARLIM(stencilfab.hiVect()),
    maskfab.dataPtr(),
    ARLIM(maskfab.loVect()),ARLIM(maskfab.hiVect()),
    voffab.dataPtr(),
    ARLIM(voffab.loVect()),ARLIM(voffab.hiVect()),
    tilelo,tilehi,
    fablo,fabhi,&bfact,
    &rzflag,
    xlo,dx,
    &cur_time_slab,
    &ngrow_distance,
    &nmat,&nstar);
 } // mfi
} // omp
 ParallelDescriptor::Barrier();

 localMF[STENCIL_MF]->FillBoundary(geom.periodicity());

 if (profile_dist==1) {
  after_profile = ParallelDescriptor::second();
  if (ParallelDescriptor::IOProcessor()) {
   std::cout << "level= " << level << '\n';
   std::cout << "STENINIT time " << after_profile-before_profile << '\n';
  }
 }

 debug_ngrow(FACETEST_MF,ngrow_distance,90);
 if (localMF[FACETEST_MF]->nComp()!=nmat*BL_SPACEDIM)
  BoxLib::Error("localMF[FACETEST_MF]->nComp() invalid");

 debug_ngrow(FACEFRAC_MF,ngrow_distance,90);
 if (localMF[FACEFRAC_MF]->nComp()!=nface)
  BoxLib::Error("localMF[FACEFRAC_MF]->nComp() invalid");

 debug_ngrow(STENCIL_MF,ngrow_distance,90);

 Array<int> nprocessed;
 nprocessed.resize(thread_class::nthreads);
 for (int tid=0;tid<thread_class::nthreads;tid++) {
  nprocessed[tid]=0.0;
 }

 Real profile_time_start=0.0;
 if ((profile_debug==1)||(profile_dist==1)) {
  profile_time_start=ParallelDescriptor::second();
 }

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(LS_new,use_tiling); mfi.isValid(); ++mfi) {
   BL_ASSERT(grids[mfi.index()] == mfi.validbox());
   const int gridno = mfi.index();
   const Box& tilegrid = mfi.tilebox();
   const Box& fabgrid = grids[gridno];
   const int* tilelo=tilegrid.loVect();
   const int* tilehi=tilegrid.hiVect();
   const int* fablo=fabgrid.loVect();
   const int* fabhi=fabgrid.hiVect();
   int bfact=parent->Space_blockingFactor(level);

   const Real* xlo = grid_loc[gridno].lo();

   FArrayBox& voffab=(*localMF[SLOPE_RECON_MF])[mfi];
   FArrayBox& lsfab=LS_new[mfi];
   FArrayBox& origdist=(*localMF[ORIGDIST_MF])[mfi];

   FArrayBox& stencilfab=(*localMF[STENCIL_MF])[mfi];
   FArrayBox& facefracfab=(*localMF[FACEFRAC_MF])[mfi];
   FArrayBox& facetestfab=(*localMF[FACETEST_MF])[mfi];
   FArrayBox& maskfab=(*localMF[MASK_NBR_MF])[mfi];

   FArrayBox& touchfab=(*localMF[DIST_TOUCH_MF])[mfi];
   FArrayBox& crsetouch=(*dist_touch_coarse_mf)[mfi];
   FArrayBox& crsedist=(*dist_coarse_mf)[mfi];

   int vofcomp=scomp_mofvars;
   Array<int> vofbc=getBCArray(State_Type,gridno,vofcomp,1);

   int tid=ns_thread();

    // in: MOF_REDIST_3D.F90
   FORT_LEVELSTRIP( 
    &nprocessed[tid],
    minLS[tid].dataPtr(),
    maxLS[tid].dataPtr(),
    &max_problen,
    &level,
    &finest_level,
    latent_heat.dataPtr(),
    maskfab.dataPtr(),
    ARLIM(maskfab.loVect()),ARLIM(maskfab.hiVect()),
    facefracfab.dataPtr(),
    ARLIM(facefracfab.loVect()),ARLIM(facefracfab.hiVect()),
    facetestfab.dataPtr(),
    ARLIM(facetestfab.loVect()),ARLIM(facetestfab.hiVect()),
    stencilfab.dataPtr(),
    ARLIM(stencilfab.loVect()),ARLIM(stencilfab.hiVect()),
    voffab.dataPtr(),
    ARLIM(voffab.loVect()),ARLIM(voffab.hiVect()),
    origdist.dataPtr(),
    ARLIM(origdist.loVect()),ARLIM(origdist.hiVect()),
    lsfab.dataPtr(),
    ARLIM(lsfab.loVect()),ARLIM(lsfab.hiVect()),
    touchfab.dataPtr(),
    ARLIM(touchfab.loVect()),ARLIM(touchfab.hiVect()),
    crsetouch.dataPtr(),
    ARLIM(crsetouch.loVect()),ARLIM(crsetouch.hiVect()),
    crsedist.dataPtr(),
    ARLIM(crsedist.loVect()),ARLIM(crsedist.hiVect()),
    tilelo,tilehi,
    fablo,fabhi,&bfact,
    vofbc.dataPtr(),
    &rzflag,
    xlo,dx,
    &cur_time_slab,
    &ngrow_distance,
    &nmat,&nten,&nstar,&nface);
 } // mfi
} // omp

 ParallelDescriptor::Barrier();

 for (int tid=1;tid<thread_class::nthreads;tid++) {
  for (int im=0;im<nmat;im++) {
   if (minLS[tid][im]<minLS[0][im])
    minLS[0][im]=minLS[tid][im];
   if (maxLS[tid][im]>maxLS[0][im])
    maxLS[0][im]=maxLS[tid][im];
  } // im=0..nmat-1
  nprocessed[0]+=nprocessed[tid];
 }
 ParallelDescriptor::Barrier();
 ParallelDescriptor::ReduceIntSum(nprocessed[0]);
 for (int im=0;im<nmat;im++) {
  ParallelDescriptor::ReduceRealMax(maxLS[0][im]);
  ParallelDescriptor::ReduceRealMin(minLS[0][im]);
 }

 if ((level>0)&&(level<=finest_level)) {
  delete dist_coarse_mf;
  delete dist_touch_coarse_mf;
 } else if (level==0) {
  // do nothing
 } else
  BoxLib::Error("level invalid 7");
 
 if ((profile_debug==1)||(profile_dist==1)) {
  Real profile_time_end=ParallelDescriptor::second();
  if (ParallelDescriptor::IOProcessor()) {
   std::cout << "level= " << level << '\n';
   std::cout << "nprocessed= " << nprocessed[0] << '\n';
   std::cout << "profile LEVELSTRIP time = " << 
    profile_time_end-profile_time_start << '\n';
  }
 }

 if (dist_timings==1) {
  after_dist = ParallelDescriptor::second();
  if (ParallelDescriptor::IOProcessor()) {
   std::cout << "level= " << level << '\n';
   std::cout << "dist time " << after_dist-before_dist << '\n';
  }
 }

} // subroutine makeStateDist


void
NavierStokes::correct_dist_uninit() {

 bool use_tiling=ns_tiling;

 int finest_level=parent->finestLevel();

 const Real* dx = geom.CellSize();
 int nmat=num_materials;

 MultiFab& LS_new = get_new_data(LS_Type,slab_step+1);
 if (localMF[DIST_TOUCH_MF]->nComp()!=nmat)
  BoxLib::Error("localMF[DIST_TOUCH_MF]->nComp()!=nmat");

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(LS_new,use_tiling); mfi.isValid(); ++mfi) {
   BL_ASSERT(grids[mfi.index()] == mfi.validbox());
   const int gridno = mfi.index();
   const Box& tilegrid = mfi.tilebox();
   const Box& fabgrid = grids[gridno];
   const int* tilelo=tilegrid.loVect();
   const int* tilehi=tilegrid.hiVect();
   const int* fablo=fabgrid.loVect();
   const int* fabhi=fabgrid.hiVect();
   int bfact=parent->Space_blockingFactor(level);

   const Real* xlo = grid_loc[gridno].lo();

   FArrayBox& lsfab=LS_new[mfi];
   FArrayBox& touchfab=(*localMF[DIST_TOUCH_MF])[mfi];

   FORT_CORRECT_UNINIT( 
    minLS[0].dataPtr(),
    maxLS[0].dataPtr(),
    &max_problen,
    &level,
    &finest_level,
    lsfab.dataPtr(),
    ARLIM(lsfab.loVect()),ARLIM(lsfab.hiVect()),
    touchfab.dataPtr(),
    ARLIM(touchfab.loVect()),ARLIM(touchfab.hiVect()),
    tilelo,tilehi,
    fablo,fabhi,&bfact,
    xlo,dx,
    &cur_time_slab,
    &nmat);
 } // mfi
} // omp

 ParallelDescriptor::Barrier();

} // subroutine correct_dist_uninit


// WARNING:  allocates, but does not delete.
void
NavierStokes::ProcessFaceFrac(int tessellate,int idxsrc,int idxdst) {
  
 bool use_tiling=ns_tiling;

 int nmat=num_materials;
  // (nmat,sdim,2,sdim+1) area+centroid on each face of a cell.
 int nface_src=nmat*BL_SPACEDIM*2*(1+BL_SPACEDIM); 
  // (nmat,nmat,2)  left material, right material, frac_pair+dist_pair
 int nface_dst=nmat*nmat*2;

 int finest_level=parent->finestLevel();

 if ((tessellate!=0)&&(tessellate!=1))
  BoxLib::Error("tessellate invalid");

 if ((level<0)||(level>finest_level))
  BoxLib::Error("level invalid ProcessFaceFrac");

 for (int dir=0;dir<BL_SPACEDIM;dir++) {
  if (localMF_grow[idxdst+dir]>=0) {
   delete_localMF(idxdst+dir,1);
  }
  new_localMF(idxdst+dir,nface_dst,0,dir);
  localMF[idxdst+dir]->setVal(0.0);
 }

 debug_ngrow(idxsrc,1,90);
 if (localMF[idxsrc]->nComp()!=nface_src)
  BoxLib::Error("idxsrc has invalid ncomp");

 VOF_Recon_resize(1,SLOPE_RECON_MF);
 debug_ngrow(SLOPE_RECON_MF,1,90);
 resize_mask_nbr(1);
 debug_ngrow(MASK_NBR_MF,1,90);

 int rzflag=0;
 if (CoordSys::IsRZ())
  rzflag=1;
 else if (CoordSys::IsCartesian())
  rzflag=0;
 else if (CoordSys::IsCYLINDRICAL())
  rzflag=3;
 else
  BoxLib::Error("CoordSys bust 7");

 const Real* dx = geom.CellSize();

 for (int dir=0;dir<BL_SPACEDIM;dir++) {

#ifdef _OPENMP
#pragma omp parallel
#endif
{
  for (MFIter mfi(*localMF[idxsrc],use_tiling); mfi.isValid(); ++mfi) {
   BL_ASSERT(grids[mfi.index()] == mfi.validbox());
   const int gridno = mfi.index();
   const Box& tilegrid = mfi.tilebox();
   const Box& fabgrid = grids[gridno];
   const int* tilelo=tilegrid.loVect();
   const int* tilehi=tilegrid.hiVect();
   const int* fablo=fabgrid.loVect();
   const int* fabhi=fabgrid.hiVect();
   int bfact=parent->Space_blockingFactor(level);

   const Real* xlo = grid_loc[gridno].lo();

   FArrayBox& voffab=(*localMF[SLOPE_RECON_MF])[mfi];
   FArrayBox& facefab=(*localMF[idxsrc])[mfi];
   FArrayBox& dstfab=(*localMF[idxdst+dir])[mfi];

   int tid=ns_thread();

   FORT_FACEPROCESS( 
    &tid,
    &dir,
    &tessellate,
    &level,
    &finest_level,
    dstfab.dataPtr(),
    ARLIM(dstfab.loVect()),ARLIM(dstfab.hiVect()),
    facefab.dataPtr(),
    ARLIM(facefab.loVect()),ARLIM(facefab.hiVect()),
    voffab.dataPtr(),
    ARLIM(voffab.loVect()),ARLIM(voffab.hiVect()),
    tilelo,tilehi,
    fablo,fabhi,&bfact,
    &rzflag,
    xlo,dx,
    &cur_time_slab,
    &nmat,&nface_src,&nface_dst);
  } // mfi
} // omp
  ParallelDescriptor::Barrier();

  localMF[idxdst+dir]->FillBoundary(geom.periodicity());
 } //dir

} // subroutine ProcessFaceFrac



// WARNING: makeFaceFrac allocates, but does not delete.
void
NavierStokes::makeFaceFrac(
 int tessellate,int ngrow,int idx,int do_face_decomp) {

 
 bool use_tiling=ns_tiling;

 int nmat=num_materials;
 int nten=( (nmat-1)*(nmat-1)+nmat-1 )/2;

  // (nmat,sdim,2,sdim+1) area+centroid on each face of a cell.
 int nface=nmat*BL_SPACEDIM*2*(1+BL_SPACEDIM); 

  // inside,outside,area+centroid,dir,side)
  // (nmat,nmat,sdim+1,sdim,2)
 int nface_decomp=0;
 if (do_face_decomp==1) {
  nface_decomp=nmat*nmat*(BL_SPACEDIM+1)*BL_SPACEDIM*2;
 } else if (do_face_decomp==0) {
  // do nothing
 } else
  BoxLib::Error("do_face_decomp invalid"); 

 int finest_level=parent->finestLevel();

 if (localMF_grow[idx]>=0) {
  delete_localMF(idx,1);
 }

 new_localMF(idx,nface+nface_decomp,ngrow,-1);
 localMF[idx]->setVal(0.0);
 VOF_Recon_resize(ngrow,SLOPE_RECON_MF);
 debug_ngrow(SLOPE_RECON_MF,ngrow,90);
 resize_mask_nbr(ngrow);
 debug_ngrow(MASK_NBR_MF,ngrow,90);

 int rzflag=0;
 if (CoordSys::IsRZ())
  rzflag=1;
 else if (CoordSys::IsCartesian())
  rzflag=0;
 else if (CoordSys::IsCYLINDRICAL())
  rzflag=3;
 else
  BoxLib::Error("CoordSys bust 7");

 const Real* dx = geom.CellSize();

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(*localMF[idx],use_tiling); mfi.isValid(); ++mfi) {
   BL_ASSERT(grids[mfi.index()] == mfi.validbox());
   const int gridno = mfi.index();
   const Box& tilegrid = mfi.tilebox();
   const Box& fabgrid = grids[gridno];
   const int* tilelo=tilegrid.loVect();
   const int* tilehi=tilegrid.hiVect();
   const int* fablo=fabgrid.loVect();
   const int* fabhi=fabgrid.hiVect();
   int bfact=parent->Space_blockingFactor(level);

   const Real* xlo = grid_loc[gridno].lo();

   FArrayBox& voffab=(*localMF[SLOPE_RECON_MF])[mfi];
   FArrayBox& maskfab=(*localMF[MASK_NBR_MF])[mfi];
   FArrayBox& facefab=(*localMF[idx])[mfi];

   int tid=ns_thread();

   FORT_FACEINIT( 
    &tid,
    &tessellate,
    &nten,
    &level,
    &finest_level,
    facefab.dataPtr(),
    ARLIM(facefab.loVect()),ARLIM(facefab.hiVect()),
    maskfab.dataPtr(),
    ARLIM(maskfab.loVect()),ARLIM(maskfab.hiVect()),
    voffab.dataPtr(),
    ARLIM(voffab.loVect()),ARLIM(voffab.hiVect()),
    tilelo,tilehi,
    fablo,fabhi,&bfact,
    &rzflag,
    xlo,dx,
    &cur_time_slab,
    &ngrow,
    &nmat,
    &nface,
    &nface_decomp);
 } // mfi
} // omp
 ParallelDescriptor::Barrier();

 localMF[idx]->FillBoundary(geom.periodicity());

} // subroutine makeFaceFrac


// WARNING: makeFaceTest allocates, but does not delete.
void
NavierStokes::makeFaceTest(int tessellate,int ngrow,int idx) {

 
 bool use_tiling=ns_tiling;

 int nmat=num_materials;
  // (im,dir,side,dir2)  (dir2==1 => area  dir2==2..sdim+1 => cen)
 int nface=nmat*BL_SPACEDIM*2*(1+BL_SPACEDIM); 

 int finest_level=parent->finestLevel();

 if ((tessellate!=0)&&(tessellate!=1))
  BoxLib::Error("tessellate invalid");

 if (localMF_grow[idx]>=0)
  BoxLib::Error("makeFaceTest: forgot to delete");

 new_localMF(idx,nmat*BL_SPACEDIM,ngrow,-1);
 localMF[idx]->setVal(0.0);

 VOF_Recon_resize(ngrow,SLOPE_RECON_MF);
 debug_ngrow(SLOPE_RECON_MF,ngrow,90);
 debug_ngrow(FACEFRAC_MF,ngrow,90);
 resize_mask_nbr(ngrow);
 debug_ngrow(MASK_NBR_MF,ngrow,90);

 if (localMF[FACEFRAC_MF]->nComp()!=nface)
  BoxLib::Error("localMF[FACEFRAC_MF]->nComp() invalid");

 int rzflag=0;
 if (CoordSys::IsRZ())
  rzflag=1;
 else if (CoordSys::IsCartesian())
  rzflag=0;
 else if (CoordSys::IsCYLINDRICAL())
  rzflag=3;
 else
  BoxLib::Error("CoordSys bust 7");

 const Real* dx = geom.CellSize();

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(*localMF[idx],use_tiling); mfi.isValid(); ++mfi) {
   BL_ASSERT(grids[mfi.index()] == mfi.validbox());
   const int gridno = mfi.index();
   const Box& tilegrid = mfi.tilebox();
   const Box& fabgrid = grids[gridno];
   const int* tilelo=tilegrid.loVect();
   const int* tilehi=tilegrid.hiVect();
   const int* fablo=fabgrid.loVect();
   const int* fabhi=fabgrid.hiVect();
   int bfact=parent->Space_blockingFactor(level);

   const Real* xlo = grid_loc[gridno].lo();

   FArrayBox& voffab=(*localMF[SLOPE_RECON_MF])[mfi];
   FArrayBox& facefab=(*localMF[FACEFRAC_MF])[mfi];
   FArrayBox& maskfab=(*localMF[MASK_NBR_MF])[mfi];
   FArrayBox& facetest=(*localMF[idx])[mfi];

   int tid=ns_thread();

   FORT_FACEINITTEST( 
    &tid,
    &tessellate,
    &level,
    &finest_level,
    facefab.dataPtr(),
    ARLIM(facefab.loVect()),ARLIM(facefab.hiVect()),
    facetest.dataPtr(),
    ARLIM(facetest.loVect()),ARLIM(facetest.hiVect()),
    maskfab.dataPtr(),
    ARLIM(maskfab.loVect()),ARLIM(maskfab.hiVect()),
    voffab.dataPtr(),
    ARLIM(voffab.loVect()),ARLIM(voffab.hiVect()),
    tilelo,tilehi,
    fablo,fabhi,&bfact,
    &rzflag,
    xlo,dx,
    &cur_time_slab,
    &ngrow,
    &nmat,
    &nface);
 } // mfi
} // omp
 ParallelDescriptor::Barrier();

} // subroutine makeFaceTest


void
NavierStokes::makeDotMask(int nsolve,int project_option) {
 
 bool use_tiling=ns_tiling;

 int nmat=num_materials;

 int finest_level=parent->finestLevel();

 if (localMF_grow[DOTMASK_MF]>=0)
  BoxLib::Error("makeDotMask: forgot to delete");

 int num_materials_face=num_materials_vel;
 if ((project_option==0)||
     (project_option==1)||
     (project_option==10)||
     (project_option==11)||  // FSI_material_exists (2nd project)
     (project_option==13)||  // FSI_material_exists (1st project)
     (project_option==12)||  // pressure extension
     (project_option==3)) {  // viscosity
  if (num_materials_face!=1)
   BoxLib::Error("num_materials_face invalid");
 } else if ((project_option==2)||  // thermal diffusion
            ((project_option>=100)&&
             (project_option<100+num_species_var))) {
  num_materials_face=num_materials_scalar_solve;
 } else
  BoxLib::Error("project_option invalid9");

 new_localMF(DOTMASK_MF,num_materials_face,0,-1);
 setVal_localMF(DOTMASK_MF,1.0,0,num_materials_face,0);

 VOF_Recon_resize(1,SLOPE_RECON_MF);
 debug_ngrow(SLOPE_RECON_MF,1,90);
 resize_mask_nbr(1);
 debug_ngrow(MASK_NBR_MF,1,90);
 resize_maskfiner(1,MASKCOEF_MF);
 debug_ngrow(MASKCOEF_MF,1,90);

 const Real* dx = geom.CellSize();

 if (num_materials_face==nmat) {

#ifdef _OPENMP
#pragma omp parallel
#endif
{
  for (MFIter mfi(*localMF[DOTMASK_MF],use_tiling); mfi.isValid(); ++mfi) {
   BL_ASSERT(grids[mfi.index()] == mfi.validbox());
   const int gridno = mfi.index();
   const Box& tilegrid = mfi.tilebox();
   const Box& fabgrid = grids[gridno];
   const int* tilelo=tilegrid.loVect();
   const int* tilehi=tilegrid.hiVect();
   const int* fablo=fabgrid.loVect();
   const int* fabhi=fabgrid.hiVect();
   int bfact=parent->Space_blockingFactor(level);

   const Real* xlo = grid_loc[gridno].lo();

   FArrayBox& voffab=(*localMF[SLOPE_RECON_MF])[mfi];

     // mask=tag if not covered by level+1 or outside the domain.
   FArrayBox& maskfab=(*localMF[MASKCOEF_MF])[mfi];
   FArrayBox& dotmaskfab=(*localMF[DOTMASK_MF])[mfi];

     // in: LEVELSET_3D.F90
   FORT_DOTMASK_BUILD( 
    &num_materials_face,
    &level,
    &finest_level,
    dotmaskfab.dataPtr(),
    ARLIM(dotmaskfab.loVect()),ARLIM(dotmaskfab.hiVect()),
    maskfab.dataPtr(),
    ARLIM(maskfab.loVect()),ARLIM(maskfab.hiVect()),
    voffab.dataPtr(),
    ARLIM(voffab.loVect()),ARLIM(voffab.hiVect()),
    tilelo,tilehi,
    fablo,fabhi,&bfact,
    xlo,dx,
    &cur_time_slab,
    &nmat);
  } // mfi
} // omp
  ParallelDescriptor::Barrier();

 } else if (num_materials_face==1) {
  // do nothing
 } else
  BoxLib::Error("num_materials_face invalid");

} // subroutine makeDotMask


// WARNING: makeCellFrac allocates, but does not delete.
void
NavierStokes::makeCellFrac(int tessellate,int ngrow,int idx) {
 
 bool use_tiling=ns_tiling;

 int nmat=num_materials;
 int nten=( (nmat-1)*(nmat-1)+nmat-1 )/2;
  // (nmat,nmat,3+sdim)
  // im_inside,im_outside,3+sdim --> area, dist_to_line, dist, line normal.
 int ncellfrac=nmat*nmat*(3+BL_SPACEDIM); 
 int finest_level=parent->finestLevel();

 if (localMF_grow[idx]>=0) {
  delete_localMF(idx,1);
 }

 new_localMF(idx,ncellfrac,ngrow,-1);
 localMF[idx]->setVal(0.0);

 int ngrow_resize=ngrow;
 if (ngrow_resize==0)
  ngrow_resize++;

 VOF_Recon_resize(ngrow_resize,SLOPE_RECON_MF);
 debug_ngrow(SLOPE_RECON_MF,ngrow_resize,90);
 resize_mask_nbr(ngrow_resize);
 debug_ngrow(MASK_NBR_MF,ngrow_resize,90);

 int rzflag=0;
 if (CoordSys::IsRZ())
  rzflag=1;
 else if (CoordSys::IsCartesian())
  rzflag=0;
 else if (CoordSys::IsCYLINDRICAL())
  rzflag=3;
 else
  BoxLib::Error("CoordSys bust 8");

 const Real* dx = geom.CellSize();

#ifdef _OPENMP
#pragma omp parallel
#endif
{
 for (MFIter mfi(*localMF[idx],use_tiling); mfi.isValid(); ++mfi) {
   BL_ASSERT(grids[mfi.index()] == mfi.validbox());
   const int gridno = mfi.index();
   const Box& tilegrid = mfi.tilebox();
   const Box& fabgrid = grids[gridno];
   const int* tilelo=tilegrid.loVect();
   const int* tilehi=tilegrid.hiVect();
   const int* fablo=fabgrid.loVect();
   const int* fabhi=fabgrid.hiVect();
   int bfact=parent->Space_blockingFactor(level);

   const Real* xlo = grid_loc[gridno].lo();

   FArrayBox& voffab=(*localMF[SLOPE_RECON_MF])[mfi];
   FArrayBox& maskfab=(*localMF[MASK_NBR_MF])[mfi];
   FArrayBox& facefab=(*localMF[idx])[mfi];

   int tid=ns_thread();

   FORT_CELLFACEINIT( 
    &tid,
    &tessellate,
    &nten,
    &level,
    &finest_level,
    facefab.dataPtr(),
    ARLIM(facefab.loVect()),ARLIM(facefab.hiVect()),
    maskfab.dataPtr(),
    ARLIM(maskfab.loVect()),ARLIM(maskfab.hiVect()),
    voffab.dataPtr(),
    ARLIM(voffab.loVect()),ARLIM(voffab.hiVect()),
    tilelo,tilehi,
    fablo,fabhi,&bfact,
    &rzflag,
    xlo,dx,
    &cur_time_slab,
    &ngrow,
    &nmat,
    &ncellfrac);
 } // mfi
} // omp
 ParallelDescriptor::Barrier();

 localMF[idx]->FillBoundary(geom.periodicity());

} // makeCellFrac


// called from make_physics_varsALL
void
NavierStokes::makeStateCurv(int project_option,int post_restart_flag) {
 
 bool use_tiling=ns_tiling;

 int finest_level=parent->finestLevel();
 if ((level<0)||(level>finest_level))
  BoxLib::Error("level invalid makeStateCurv");

 int rzflag=0;
 if (CoordSys::IsRZ())
  rzflag=1;
 else if (CoordSys::IsCartesian())
  rzflag=0;
 else if (CoordSys::IsCYLINDRICAL())
  rzflag=3;
 else
  BoxLib::Error("CoordSys bust 9");

 if (curv_stencil_height!=4)
  BoxLib::Error("curv_stencil_height invalid");

 if (ngrow_distance!=4)
  BoxLib::Error("ngrow_distance invalid");

 int nmat=num_materials;
 int nten=( (nmat-1)*(nmat-1)+nmat-1 )/2;

 Array< Real > curv_min_local;
 Array< Real > curv_max_local;
 curv_min_local.resize(thread_class::nthreads);
 curv_max_local.resize(thread_class::nthreads);

 for (int tid=0;tid<thread_class::nthreads;tid++) {
  curv_min_local[tid]=1.0e+99;
  curv_max_local[tid]=-1.0e+99;
 } // tid

  // height function curvature
  // finite difference curvature
  // pforce
  // marangoni force
  // dir/side flag
  // im3
  // x nten
 int num_curv=nten*(BL_SPACEDIM+5); 

 resize_metrics(1);

 resize_levelsetLO(ngrow_distance,LEVELPC_MF);

 if (localMF[LEVELPC_MF]->nComp()!=nmat*(1+BL_SPACEDIM))
  BoxLib::Error("localMF[LEVELPC_MF]->nComp() invalid");
 if (localMF[LEVELPC_MF]->nGrow()!=ngrow_distance)
  BoxLib::Error("localMF[LEVELPC_MF]->nGrow() invalid");

 if (localMF_grow[DIST_CURV_MF]>=0) {
  delete_localMF(DIST_CURV_MF,1);
 }
 new_localMF(DIST_CURV_MF,num_curv,1,-1);
 localMF[DIST_CURV_MF]->setVal(0.0);

 getStateDist_localMF(GHOSTDIST_MF,ngrow_distance,cur_time_slab,16);
 debug_ngrow(GHOSTDIST_MF,ngrow_distance,906);
 if (localMF[GHOSTDIST_MF]->nComp()!=(1+BL_SPACEDIM)*nmat)
  BoxLib::Error("localMF[GHOSTDIST_MF]->nComp() invalid");

 if ((project_option==0)||
     (project_option==1)) {

  const Real* dx = geom.CellSize();

  Real cl_time=prev_time_slab;
  if (project_option==0)  // regular project
   cl_time=prev_time_slab;
  else if (project_option==1) // initial project
   cl_time=cur_time_slab;
  else if (project_option==10) // sync project
   cl_time=prev_time_slab;
  else
   BoxLib::Error("project_option invalid makeStateCurv");

  if (num_materials_vel!=1)
   BoxLib::Error("num_materials_vel invalid");

  MultiFab* CL_velocity=getState(2,0,
    num_materials_vel*(BL_SPACEDIM+1),cl_time);
  MultiFab* den=getStateDen(2,cl_time);
  if (den->nComp()!=nmat*num_state_material)
   BoxLib::Error("invalid ncomp for den");

   // mask=1 if not covered or if outside the domain.
   // NavierStokes::maskfiner_localMF
   // NavierStokes::maskfiner
  resize_maskfiner(1,MASKCOEF_MF);
  debug_ngrow(MASKCOEF_MF,1,28); 
  resize_mask_nbr(1);
  debug_ngrow(MASK_NBR_MF,1,2);

#ifdef _OPENMP
#pragma omp parallel
#endif
{
  for (MFIter mfi(*CL_velocity,use_tiling); mfi.isValid(); ++mfi) {
    BL_ASSERT(grids[mfi.index()] == mfi.validbox());
    const int gridno = mfi.index();
    const Box& tilegrid = mfi.tilebox();
    const Box& fabgrid = grids[gridno];
    const int* tilelo=tilegrid.loVect();
    const int* tilehi=tilegrid.hiVect();
    const int* fablo=fabgrid.loVect();
    const int* fabhi=fabgrid.hiVect();
    int bfact_space=parent->Space_blockingFactor(level);
    int bfact_grid=parent->blockingFactor(level);

    const Real* xlo = grid_loc[gridno].lo();

    // mask=tag if not covered by level+1 or outside the domain.
    FArrayBox& maskcov=(*localMF[MASKCOEF_MF])[mfi];

    FArrayBox& lsfab=(*localMF[LEVELPC_MF])[mfi];
    FArrayBox& lshofab=(*localMF[GHOSTDIST_MF])[mfi];

    FArrayBox& curvfab=(*localMF[DIST_CURV_MF])[mfi];
    FArrayBox& velfab=(*CL_velocity)[mfi];
    FArrayBox& denfab=(*den)[mfi];
    FArrayBox& maskfab=(*localMF[MASK_NBR_MF])[mfi];

    FArrayBox& areax=(*localMF[AREA_MF])[mfi];
    FArrayBox& areay=(*localMF[AREA_MF+1])[mfi];
    FArrayBox& areaz=(*localMF[AREA_MF+BL_SPACEDIM-1])[mfi];
    FArrayBox& volfab=(*localMF[VOLUME_MF])[mfi];

    int tid=ns_thread();

    FORT_CURVSTRIP(
     &post_restart_flag,
     &conservative_tension_force,
     &level,
     &finest_level,
     &curv_min_local[tid],
     &curv_max_local[tid],
     maskcov.dataPtr(),
     ARLIM(maskcov.loVect()),ARLIM(maskcov.hiVect()),
     volfab.dataPtr(),ARLIM(volfab.loVect()),ARLIM(volfab.hiVect()),
     areax.dataPtr(),ARLIM(areax.loVect()),ARLIM(areax.hiVect()),
     areay.dataPtr(),ARLIM(areay.loVect()),ARLIM(areay.hiVect()),
     areaz.dataPtr(),ARLIM(areaz.loVect()),ARLIM(areaz.hiVect()),
     maskfab.dataPtr(),
     ARLIM(maskfab.loVect()),ARLIM(maskfab.hiVect()),
     lsfab.dataPtr(),
     ARLIM(lsfab.loVect()),ARLIM(lsfab.hiVect()),
     lshofab.dataPtr(),
     ARLIM(lshofab.loVect()),ARLIM(lshofab.hiVect()),
     curvfab.dataPtr(),
     ARLIM(curvfab.loVect()),ARLIM(curvfab.hiVect()),
     velfab.dataPtr(),
     ARLIM(velfab.loVect()),ARLIM(velfab.hiVect()),
     denfab.dataPtr(),
     ARLIM(denfab.loVect()),ARLIM(denfab.hiVect()),
     tilelo,tilehi,
     fablo,fabhi, 
     &bfact_space,
     &bfact_grid,
     &rzflag,
     xlo,dx,
     &cur_time_slab,
     &visc_coef,
     &nmat,&nten,
     &num_curv,
     &ngrow_distance,
     &curv_stencil_height);
  } // mfi
} //omp
  ParallelDescriptor::Barrier();

  for (int tid=1;tid<thread_class::nthreads;tid++) {
    if (curv_min_local[tid]<curv_min_local[0])
     curv_min_local[0]=curv_min_local[tid];
    if (curv_max_local[tid]>curv_max_local[0])
     curv_max_local[0]=curv_max_local[tid];
  } // tid
  ParallelDescriptor::Barrier();

  ParallelDescriptor::ReduceRealMin(curv_min_local[0]);
  ParallelDescriptor::ReduceRealMax(curv_max_local[0]);
  if (curv_min_local[0]<curv_min[0])
    curv_min[0]=curv_min_local[0];
  if (curv_max_local[0]>curv_max[0])
    curv_max[0]=curv_max_local[0];

  localMF[DIST_CURV_MF]->FillBoundary(geom.periodicity());

  if ((fab_verbose==1)||(fab_verbose==3)) {

    std::cout << "c++ level,finest_level " << level << ' ' <<
     finest_level << '\n';
    std::cout << "c++ ngrow,csten " << ngrow_distance << ' ' <<
     curv_stencil_height << ' ' << '\n';

    std::cout << "curv_min_local(HT)= " << curv_min_local[0] << '\n';
    std::cout << "curv_max_local(HT)= " << curv_max_local[0] << '\n';

    for (MFIter mfi(*CL_velocity); mfi.isValid(); ++mfi) {
     BL_ASSERT(grids[mfi.index()] == mfi.validbox());
     const int gridno = mfi.index();
     const Box& fabgrid = grids[gridno];
     const int* fablo=fabgrid.loVect();
     const int* fabhi=fabgrid.hiVect();
     const Real* xlo = grid_loc[gridno].lo();
     std::cout << "gridno= " << gridno << '\n';
     int interior_only=0;

     std::cout << "output of curvfab" << '\n';
     FArrayBox& curvfab=(*localMF[DIST_CURV_MF])[mfi];
     tecplot_debug(curvfab,xlo,fablo,fabhi,dx,-1,0,0,num_curv,interior_only);

     std::cout << "output of lsfab (LEVELPC)" << '\n';
     FArrayBox& lsfab=(*localMF[LEVELPC_MF])[mfi];
     tecplot_debug(lsfab,xlo,fablo,fabhi,dx,-1,0,0,nmat,interior_only);

     std::cout << "output of lshofab (GHOSTDIST)" << '\n';
     FArrayBox& lshofab=(*localMF[GHOSTDIST_MF])[mfi];
     tecplot_debug(lshofab,xlo,fablo,fabhi,dx,-1,0,0,nmat,interior_only);

    } // mfi

  } // ((fab_verbose==1)||(fab_verbose==3))

  delete CL_velocity;
  delete den;

 } else if (project_option==10) {

   // do nothing

 } else
   BoxLib::Error("project_option invalid10");

 delete_localMF(GHOSTDIST_MF,1);

}  // subroutine makeStateCurv


MultiFab* NavierStokes::getStateMAC(int ngrow,int dir,
 int scomp,int ncomp,Real time) {

 if (num_materials_vel!=1)
  BoxLib::Error("num_materials_vel invalid");

 int nsolve=1;
 int nsolveMM_FACE=nsolve*num_materials_vel;
 
 if ((dir<0)||(dir>=BL_SPACEDIM))
  BoxLib::Error("dir invalid get state mac");

 MultiFab& S_new=get_new_data(Umac_Type+dir,slab_step+1);
 int ntotal=S_new.nComp();
 if (ntotal!=nsolveMM_FACE)
  BoxLib::Error("ntotal bust");
 if (scomp+ncomp>ntotal)
  BoxLib::Error("scomp invalid getStateMAC");

 MultiFab* mf = new MultiFab(state[Umac_Type+dir].boxArray(),ncomp,
   ngrow,dmap,Fab_allocate);

 FillPatch(*this,*mf,0,time,Umac_Type+dir,scomp,ncomp);

 ParallelDescriptor::Barrier();

 return mf;

}  // subroutine getStateMAC



void
NavierStokes::ctml_fsi_transfer_force() {
	
 if (ParallelDescriptor::IOProcessor() && verbose)
  std::cout << "in NavierStokes::ctml_fsi_transfer_force() \n";

 bool use_tiling=ns_tiling;

 MultiFab& S_new=get_new_data(State_Type,slab_step+1);

 int nmat=num_materials;

  // nparts x (velocity + LS + temperature + flag + stress)
 int nparts=im_solid_map.size();
 if ((nparts<1)||(nparts>=nmat))
  BoxLib::Error("nparts invalid");

 if (nFSI_sub!=12)
  BoxLib::Error("nFSI_sub invalid");
 if (ngrowFSI!=3)
  BoxLib::Error("ngrowFSI invalid");
 int nFSI=nparts*nFSI_sub;

 debug_ngrow(FSI_MF,0,1);
 if (localMF[FSI_MF]->nComp()!=nFSI)
  BoxLib::Error("localMF[FSI_MF]->nComp() invalid");

 for (int partid=0;partid<nparts;partid++) {

  int im_part=im_solid_map[partid];

  if (ns_is_rigid(im_part)==1) {

   if (CTML_FSI_matC(im_part)==1) {

#ifdef _OPENMP
#pragma omp parallel
#endif
{
    for (MFIter mfi(S_new,use_tiling); mfi.isValid(); ++mfi) {
     BL_ASSERT(grids[mfi.index()] == mfi.validbox());
#ifdef MVAHABFSI
     const int gridno = mfi.index();
     const Box& tilegrid = mfi.tilebox();
     const Box& fabgrid = grids[gridno];
     const int* tilelo=tilegrid.loVect();
     const int* tilehi=tilegrid.hiVect();
     const int* fablo=fabgrid.loVect();
     const int* fabhi=fabgrid.hiVect();

     FArrayBox& snewfab=S_new[mfi];
     FArrayBox& forcefab=(*localMF[FSI_MF])[mfi];
      // nparts x (velocity + LS + temperature + flag + stress)
     int ibase=partid*nFSI_sub+6;

     FORT_CTMLTRANSFERFORCE(
      tilelo, tilehi, 
      fablo, fabhi, 
      snewfab.dataPtr(), 
      ARLIM(snewfab.loVect()), ARLIM(snewfab.hiVect()), 
      forcefab.dataPtr(ibase), 
      ARLIM(forcefab.loVect()), ARLIM(forcefab.hiVect()));
#else
     BoxLib::Error("CTML(C): define MEHDI_VAHAB_FSI in GNUmakefile");
#endif
    }  // mfi  
} // omp
    ParallelDescriptor::Barrier();

   } else if (CTML_FSI_matC(im_part)==0) {
    // do nothing
   } else 
    BoxLib::Error("CTML_FSI_matC(im_part) invalid");

  } else
   BoxLib::Error("ns_is_rigid(im_part) invalid");

 } // partid=0 ... nparts-1

} // subroutine ctml_fsi_transfer_force()