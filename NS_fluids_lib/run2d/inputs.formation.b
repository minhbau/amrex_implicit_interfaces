# ------------------  INPUTS TO MAIN PROGRAM  -------------------
# CHANGES FROM PAST VERSION:
# smg.useCG becomes mg.usecg, no more smg.eps
# no more raster stuff (hdf), add mac.mac_abs_tol
# blob.* now becomes ns.*
#
max_step  = 99999    # maximum timestep
#max_step  =  2    # maximum timestep
stop_time =  3000  # maximum problem time
thickness = 0.1

# ------------------  INPUTS TO CLASS AMR ---------------------
# set up for bubble
geometry.coord_sys      = 1        # 0 => cart, 1 => RZ
geometry.prob_lo   =  0.0 0.0
geometry.prob_hi   =  16.0 64.0

# multigrid class
#mg.verbose = 2
#cg.verbose = 2
# set above to 2 for maximum verbosity
mg.nu_f = 40
mg.nu_0 = 1   # 1 - v-cycle 2 - w-cycle
cg.maxiter = 200
mg.usecg=1
mg.bot_atol = 1.0e-10
mg.rtol_b = -0.01
#Lp.v = 1
Lp.harmavg = 0
ns.be_cn_theta=1.0
ns.rk_theta=1.0
diffuse.max_order=2

amr.n_cell    = 32 128
amr.max_level = 2
# 0- 1 level 1- 2 levels  2- 3 levels
amr.regrid_int      = 1       # how often to regrid
amr.n_error_buf     = 8 8 8 8 8    # number of buffer cells in error est
amr.grid_eff        = 0.45   # what constitutes an efficient grid
# above was .55 (smaller=> less boxes)
amr.blocking_factor = 8       # block factor in grid generation
amr.check_int       = 100      # number of timesteps between checkpoints
amr.check_file      = chk     # root name of checkpoint file
amr.plot_int        = 0
amr.plot_file       = plt 
amr.grid_log        = grdlog  # name of grid logging file
amr.max_grid_size   = 512
#amr.restart         = chk2900
#amr.trace   =1

# ------------------  INPUTS TO PHYSICS CLASS -------------------
ns.dt_cutoff      = 0.000005  # level 0 timestep below which we halt

mac.mac_tol        = 1.0e-8   # tolerence for mac projections
mac.mac_abs_tol    = 1.0e-8
mac.use_cg_solve   = 1

ns.pcav=0.0
ns.dencav=0.0
ns.pchopp=0.0
ns.denchopp=0.0
ns.soundchopp=100.0
ns.bubble_pressure=1.0
ns.bubble_density=0.001                                                                                
ns.hydrostatic_pressure=1.013
ns.visual_option=3
ns.visual_lo = 0 0
ns.visual_hi = 512 512

ns.visual_side_max = 512

ns.overlap=2.0
ns.is_twophase=1
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
ns.stable_interface_update=0

ns.cfl            = 3.0      # cfl number for hyperbolic system
ns.init_shrink    = 1.0      # scale back initial timestep
ns.change_max     = 1.1      # scale back initial timestep
ns.visc_coef      = 0.0138 0.0138 0 0 0 0 0 0 0  # coef of viscosity
mac.visc_abs_tol   = 1.0e-6
ns.init_iter      = 2        # number of init iters to def pressure
ns.gravity        = -1.07e-4     # body force  (gravity in MKS units)
ns.gravityangle   = 0.0
ns.tension        = 0.000817 # interfacial tension force
#ns.fixed_dt	  = 0.0025     # hardwire dt
ns.sum_interval   = 1        # timesteps between computing mass 
ns.usekluge       = 0
ns.RUNGAKUTTA = 0
ns.centerpressure=1
ns.visctimestep=1

ns.axis_dir=11
ns.vorterr=999999.0
ns.rgasinlet=1.57
ns.vinletgas=0.0
ns.twall=0.0
ns.advbot=0.0
ns.adv_vel=0.0
ns.adv_dir=1
ns.viscunburn=1.0
ns.viscburn=1.44444E-4
ns.viscvapor=1.44444E-4
ns.tcenter=-1.0
ns.denspread=2.0
ns.denwater=1.0
ns.denair=0.000985
ns.denvapor=0.000985
ns.xblob=0.0
ns.yblob=0.0
ns.zblob=5.0
ns.radblob=1.0
ns.denfact=1.0
ns.velfact=0.0
ns.probtype=25

proj.bogus_value = 5.0e+5

#ns.mem_debug = 1
#ns.v = 1
#ns.d = 1

# ----------------  PROBLEM DEPENDENT INPUTS
ns.lo_bc          = 3 1 4
ns.hi_bc          = 4 2 4

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
#               3 => same as 2 except show Up and not Ud
# win_siz     = number of pixels in max window direction
#
#             file_name  var_name   device  freq  n_cont  sho_grd win_siz sval sdir sstr
contour.verbose = 1
contour.plot =  triple    triple       1     1     -1       2      400    -1   -1   0
#contour.plot =  triple    triple       2  1000     -1       2      400    -1   -1   0
#contour.plot = presliquid presliquid  1     1     20       2      400    -1   -1   0
#contour.plot = presliquid presliquid  2  1000     20       2      400    -1   -1   0

