

# ------------------  INPUTS TO MAIN PROGRAM  -------------------
# CHANGES FROM PAST VERSION:
# smg.useCG becomes mg.usecg, no more smg.eps
# no more raster stuff (hdf), add mac.mac_abs_tol
# blob.* now becomes ns.*
#
max_step  =  999999   # maximum timestep
#max_step  =  2    # maximum timestep
stop_time =  2000.0  # maximum problem time
thickness = 0.1

# ------------------  INPUTS TO CLASS AMR ---------------------
# set up for bubble
geometry.coord_sys      = 0        # 0 => cart, 1 => RZ
geometry.prob_lo   =  0.0   0.0  0.0
geometry.prob_hi   =  10.0  10.0 5.0

# multigrid class
mg.verbose = 0
cg.verbose = 0
# set above to 2 for maximum verbosity
mg.nu_f = 40
mg.nu_0 = 1   # 1 - v-cycle 2 - w-cycle
cg.maxiter = 9000
mg.usecg=0
mg.bot_atol = 1.0e-8
mg.rtol_b = -0.01
Lp.v = 0
Lp.harmavg = 0
ns.be_cn_theta=1.0
ns.rk_theta=1.0
diffuse.max_order=2

amr.n_cell    = 32 32 16
amr.max_level =  1
# 0- 1 level 1- 2 levels  2- 3 levels
amr.regrid_int      = 1       # how often to regrid
amr.n_error_buf     = 2 2 2 2 2   # number of bu0ffer cells in error est
amr.grid_eff        = 1.0  # what constitutes an efficient grid
# above was .55 (smaller=> less boxes)
amr.blocking_factor = 4    # block factor in grid generation MS
amr.check_int       = 20     # number of timesteps between checkpoints
amr.check_file      = chk     # root name of checkpoint file
amr.plot_int        = -1       # MS (was 5)
amr.plot_file       = plt 
amr.grid_log        = grdlog  # name of grid logging file
amr.max_grid_size   = 512
#amr.restart         = chk1800
#amr.trace   =1

# ------------------  INPUTS TO PHYSICS CLASS -------------------
ns.dt_cutoff      = 0.000005  # level 0 timestep below which we halt

mac.mac_tol        = 1.0e-6  # tolerence for mac projections
mac.mac_abs_tol    = 1.0e-6
mac.use_cg_solve   = 1

ns.pcav=0.0
ns.dencav=0.0
ns.pchopp=0.0
ns.denchopp=0.0
ns.soundchopp=100.0
ns.bubble_pressure=1.0
ns.bubble_density=0.001                                                                                
ns.hydrostatic_pressure=0.5 #was 1 VM
ns.visual_option=7
ns.visual_lo =-1.0 -1.0 -1.0  # MS
ns.visual_hi =-1.0 -1.0 -1.0  # MS
ns.visual_lo_phys =0.0 0.0 0.0  # MS  (always assumes 0,0,0 is smallest coord)
ns.visual_hi_phys =10.0 10.0 10.0  # MS
ns.visual_level_max = 3 # MS

ns.overlap=2.0
ns.is_twophase=0
ns.is_compressible=0
ns.shock_capture=0
ns.shock_eta=1.0
ns.shock_timestep=0
ns.limitfactor=2.0
ns.clipfactor=2.0
ns.enoextrap=1
# 0 cell 1 edge 2 conserve
ns.edgeextraptype=0
ns.redist_on_init=0
ns.tvdrk=0
ns.conserve_vof=0

ns.sharp_solid=1
ns.cfl            = 0.5      # MS cfl number for hyperbolic system
ns.init_shrink    = 1.0      # scale back initial timestep
ns.change_max     = 1.1      # scale back initial timestep
ns.visc_coef      = 0.0      # coef of viscosity
mac.visc_abs_tol   = 1.0e-7
ns.init_iter      = 0      # number of init iters to def pressure
ns.gravity        = -9.8 # body force  (gravity in MKS units)
ns.gravityangle = 0.0
ns.tension        = 0.005
ns.fixed_dt	  = 0.005 # hardwire dt MS
ns.sum_interval   = 1        # timesteps between computing mass 
ns.usekluge       = 0
ns.RUNGAKUTTA = 0
ns.centerpressure=1
ns.visctimestep=1
ns.axis_dir=0
ns.vorterr=999999.0
ns.rgasinlet=1.570
ns.vinletgas=0.0
ns.twall=5.0
ns.advbot=0.0
ns.adv_vel=0.0
ns.adv_dir=2
ns.viscunburn=1.0
ns.viscburn=0.01
ns.viscvapor=0.01


ns.tcenter=1.0  # MS
ns.solidradius=1.0 # MS
ns.denspread=1.0  # MS
ns.denwater=1.0
ns.denair=1.0
ns.denvapor=0.001
ns.xblob=16.0
ns.yblob=7.0   
ns.zblob=12.0   
ns.radblob=0.2
ns.radblob2=0.6
ns.radblob4 = 4.9 #radius of solid cylinder
ns.radblob5 = 1.0 #bottom water rotational speed 
ns.xblob2=0.9  # 45 degrees tilt
ns.zblob2=27    # length of straw
ns.radblob3=0.0  # inflow velocity
ns.zblob3=0.5    # depth of water
ns.denfact=1.0
ns.velfact=0.0
ns.probtype=65

#ns.mem_debug = 1
ns.v = 1  # MS
#ns.d = 1

# ----------------  PROBLEM DEPENDENT INPUTS
ns.lo_bc          = 4 4 2
ns.hi_bc          = 4 4 1

# >>>>>>>>>>>>>  BC FLAGS <<<<<<<<<<<<<<<<
# 0 = Interior           3 = Symmetry
# 1 = Inflow             4 = SlipWall
# 2 = Outflow            5 = NoSlipWall

# turn any of these on to generate run-time timing stats
RunStats.statvar = godunov_box level_project sync_project


# select single or double precision of FAB output data
#        default is whatever precision code is compiled with.
#fab.precision = FLOAT     # output in FLOAT or DOUBLE
fab.precision = DOUBLE    # output in FLOAT or DOUBLE

# --------------------------------------------------------------------
# -----       CONTOUR PLOTTING ONLY AVAILABLE IN 2-D           -------
# --------------------------------------------------------------------
# uncomment the next line to set a default level for contour plotting
# contour.level = 1
#
# These variables control interactive contour plotting on UNIX systems
# file_name   = root name of postscript file (will be appended with ".ps")
# var_name    = name of thermodynamic variable to plot
# device      = 1  => XWINDOW, 2 = POSTSCRIPT, 3 = both
# freq        = intervals between plots (-1 = off)
# n_cont      = number of contour lines per plot
# sho_grd     = 0 => don't show grid placement, 1 => show grid placement
#               2 => show grid placement and overlay velocity vector plot
#               3 => same as 2 except show Umac and not Ucell
# win_siz     = number of pixels in max window direction
#
#file_name  var_name   device  freq  n_cont  sho_grd win_siz sval sdir sstr
contour.verbose = 0
contour.plot = triple triple 1   1     -1       2      400    7.0  1   xz
contour.plot = triple triple 1   1     -1       2      400    0.0  2   xy
contour.plot = triple triple 1   1     -1       2      400    5.0  0   yz
#contour.plot = triple triple 2   5     -1       2      400    5.0  1   xz
#contour.plot = triple triple 2   5     -1       2      400    8.0  2   xy
#contour.plot = triple triple 2   5     -1       2      400    7.0  0   yz
