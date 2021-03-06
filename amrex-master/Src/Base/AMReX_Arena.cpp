
#include <AMReX_Arena.H>
#include <AMReX_BArena.H>
#include <AMReX_CArena.H>
#include <AMReX_DArena.H>
#include <AMReX_EArena.H>

#include <AMReX.H>
#include <AMReX_Print.H>
#include <AMReX_ParallelDescriptor.H>
#include <AMReX_ParmParse.H>
#include <AMReX_Gpu.H>

namespace amrex {

namespace {
    bool initialized = false;

    Arena* the_arena = nullptr;
    Arena* the_device_arena = nullptr;
    Arena* the_managed_arena = nullptr;
    Arena* the_pinned_arena = nullptr;
    Arena* the_cpu_arena = nullptr;

    bool use_buddy_allocator = false;
    long buddy_allocator_size = 0L;
    long the_arena_init_size = 0L;
    bool abort_on_out_of_gpu_memory = false;
}

const unsigned int Arena::align_size;

Arena::~Arena () {}

std::size_t
Arena::align (std::size_t s)
{
    std::size_t x = s + (align_size-1);
    x -= x & (align_size-1);
    return x;
}

void*
Arena::allocate_system (std::size_t nbytes)
{
#ifdef AMREX_USE_GPU
    void * p;
    if (arena_info.device_use_hostalloc)
    {
        AMREX_HIP_OR_CUDA(
            AMREX_HIP_SAFE_CALL ( hipHostAlloc(&p, nbytes, hipHostMallocMapped));,
            AMREX_CUDA_SAFE_CALL(cudaHostAlloc(&p, nbytes, cudaHostAllocMapped)););
    }
    else
    {
        if (abort_on_out_of_gpu_memory) {
            std::size_t free_mem_avail = Gpu::Device::freeMemAvailable();
            if (nbytes >= free_mem_avail) {
                amrex::Abort("Out of gpu memory. Free: " + std::to_string(free_mem_avail)
                             + " Asked: " + std::to_string(nbytes));
            }
        }

        if (arena_info.device_use_managed_memory)
        {
#if defined(__CUDACC__)
            AMREX_CUDA_SAFE_CALL(cudaMallocManaged(&p, nbytes));
#else
            AMREX_HIP_SAFE_CALL(hipMalloc(&p, nbytes));
#endif
            if (arena_info.device_set_readonly)
            {
                Gpu::Device::mem_advise_set_readonly(p, nbytes);
            }
            if (arena_info.device_set_preferred)
            {
                const int device = Gpu::Device::deviceId();
                Gpu::Device::mem_advise_set_preferred(p, nbytes, device);
            }
        }
        else
        {
            AMREX_HIP_OR_CUDA(AMREX_HIP_SAFE_CALL ( hipMalloc(&p, nbytes));,
                              AMREX_CUDA_SAFE_CALL(cudaMalloc(&p, nbytes)););
        }
    }
    return p;
#else
    return std::malloc(nbytes);
#endif
}

void
Arena::deallocate_system (void* p)
{
#ifdef AMREX_USE_GPU
    if (arena_info.device_use_hostalloc)
    {
        AMREX_HIP_OR_CUDA(AMREX_HIP_SAFE_CALL ( hipFreeHost(p));,
                          AMREX_CUDA_SAFE_CALL(cudaFreeHost(p)););
    }
    else
    {
        AMREX_HIP_OR_CUDA(AMREX_HIP_SAFE_CALL ( hipFree(p));,
                          AMREX_CUDA_SAFE_CALL(cudaFree(p)););
    }
#else
    std::free(p);
#endif
}

void
Arena::Initialize ()
{
    if (initialized) return;
    initialized = true;

    BL_ASSERT(the_arena == nullptr);
    BL_ASSERT(the_device_arena == nullptr);
    BL_ASSERT(the_managed_arena == nullptr);
    BL_ASSERT(the_pinned_arena == nullptr);
    BL_ASSERT(the_cpu_arena == nullptr);

    ParmParse pp("amrex");
    pp.query("use_buddy_allocator", use_buddy_allocator);
    pp.query("buddy_allocator_size", buddy_allocator_size);
    pp.query("the_arena_init_size", the_arena_init_size);
    pp.query("abort_on_out_of_gpu_memory", abort_on_out_of_gpu_memory);

#ifdef AMREX_USE_GPU
    if (use_buddy_allocator)
    {
        if (buddy_allocator_size <= 0) {
            buddy_allocator_size = Gpu::Device::totalGlobalMem() / 4 * 3;
        }
        std::size_t chunk = 512*1024*1024;
        buddy_allocator_size = (buddy_allocator_size/chunk) * chunk;
        the_arena = new DArena(buddy_allocator_size, 512, ArenaInfo().SetPreferred());
    }
    else
#endif
    {
#if defined(BL_COALESCE_FABS) || defined(AMREX_USE_GPU)
        the_arena = new CArena(0, ArenaInfo().SetPreferred());
#ifdef AMREX_USE_GPU
        if (the_arena_init_size <= 0) {
            the_arena_init_size = Gpu::Device::totalGlobalMem() / 4L * 3L;
        }
        void *p = the_arena->alloc(static_cast<std::size_t>(the_arena_init_size));
        the_arena->free(p);
#endif
#else
        the_arena = new BArena;
#endif
    }

#ifdef AMREX_USE_GPU
    the_device_arena = new CArena(0, ArenaInfo().SetDeviceMemory());
#else
    the_device_arena = new BArena;
#endif

#if defined(AMREX_USE_GPU)
    the_managed_arena = new CArena;
#else
    the_managed_arena = new BArena;
#endif

#if defined(AMREX_USE_GPU)
//    const std::size_t hunk_size = 64 * 1024;
//    the_pinned_arena = new CArena(hunk_size);
    the_pinned_arena = new CArena(0, ArenaInfo().SetHostAlloc());
#else
    the_pinned_arena = new BArena;
#endif

    std::size_t N = 1024UL*1024UL*8UL;

    void *p = the_device_arena->alloc(N);
    the_device_arena->free(p);

    p = the_managed_arena->alloc(N);
    the_managed_arena->free(p);

    p = the_pinned_arena->alloc(N);
    the_pinned_arena->free(p);

    the_cpu_arena = new BArena;
}

void
Arena::PrintUsage ()
{
    const int IOProc = ParallelDescriptor::IOProcessorNumber();
#ifdef AMREX_USE_GPU
    {
        long min_megabytes = Gpu::Device::totalGlobalMem() / (1024*1024);
        long max_megabytes = min_megabytes;
        ParallelDescriptor::ReduceLongMin(min_megabytes, IOProc);
        ParallelDescriptor::ReduceLongMax(max_megabytes, IOProc);
#ifdef AMREX_USE_MPI
        amrex::Print() << "Total GPU global memory (MB) spread across MPI: ["
                       << min_megabytes << " ... " << max_megabytes << "]\n";
#else
        amrex::Print() << "Total GPU global memory (MB): " << min_megabytes << "\n";
#endif
    }
    {
        long min_megabytes = Gpu::Device::freeMemAvailable() / (1024*1024);
        long max_megabytes = min_megabytes;
        ParallelDescriptor::ReduceLongMin(min_megabytes, IOProc);
        ParallelDescriptor::ReduceLongMax(max_megabytes, IOProc);
#ifdef AMREX_USE_MPI
        amrex::Print() << "Free  GPU global memory (MB) spread across MPI: ["
                       << min_megabytes << " ... " << max_megabytes << "]\n";
#else
        amrex::Print() << "Free  GPU global memory (MB): " << min_megabytes << "\n";
#endif
    }
#endif
    if (The_Arena()) {
        CArena* p = dynamic_cast<CArena*>(The_Arena());
        if (p) {
            long min_megabytes = p->heap_space_used() / (1024*1024);
            long max_megabytes = min_megabytes;
            ParallelDescriptor::ReduceLongMin(min_megabytes, IOProc);
            ParallelDescriptor::ReduceLongMax(max_megabytes, IOProc);
#ifdef AMREX_USE_MPI
            amrex::Print() << "[The         Arena] space (MB) used spread across MPI: ["
                           << min_megabytes << " ... " << max_megabytes << "]\n";
#else
            amrex::Print() << "[The         Arena] space (MB): " << min_megabytes << "\n";
#endif
        }
    }
    if (The_Device_Arena()) {
        CArena* p = dynamic_cast<CArena*>(The_Device_Arena());
        if (p) {
            long min_megabytes = p->heap_space_used() / (1024*1024);
            long max_megabytes = min_megabytes;
            ParallelDescriptor::ReduceLongMin(min_megabytes, IOProc);
            ParallelDescriptor::ReduceLongMax(max_megabytes, IOProc);
#ifdef AMREX_USE_MPI
            amrex::Print() << "[The  Device Arena] space (MB) used spread across MPI: ["
                           << min_megabytes << " ... " << max_megabytes << "]\n";
#else
            amrex::Print() << "[The  Device Arena] space (MB): " << min_megabytes << "\n";
#endif
        }
    }
    if (The_Managed_Arena()) {
        CArena* p = dynamic_cast<CArena*>(The_Managed_Arena());
        if (p) {
            long min_megabytes = p->heap_space_used() / (1024*1024);
            long max_megabytes = min_megabytes;
            ParallelDescriptor::ReduceLongMin(min_megabytes, IOProc);
            ParallelDescriptor::ReduceLongMax(max_megabytes, IOProc);
#ifdef AMREX_USE_MPI
            amrex::Print() << "[The Managed Arena] space (MB) used spread across MPI: ["
                           << min_megabytes << " ... " << max_megabytes << "]\n";
#else
            amrex::Print() << "[The Managed Arena] space (MB): " << min_megabytes << "\n";
#endif
        }
    }
    if (The_Pinned_Arena()) {
        CArena* p = dynamic_cast<CArena*>(The_Pinned_Arena());
        if (p) {
            long min_megabytes = p->heap_space_used() / (1024*1024);
            long max_megabytes = min_megabytes;
            ParallelDescriptor::ReduceLongMin(min_megabytes, IOProc);
            ParallelDescriptor::ReduceLongMax(max_megabytes, IOProc);
#ifdef AMREX_USE_MPI
            amrex::Print() << "[The  Pinned Arena] space (MB) used spread across MPI: ["
                           << min_megabytes << " ... " << max_megabytes << "]\n";
#else
            amrex::Print() << "[The  Pinned Arena] space (MB): " << min_megabytes << "\n";
#endif
        }
    }
}
    
void
Arena::Finalize ()
{
    if (amrex::Verbose() > 0) {
        PrintUsage();
    }
    
    initialized = false;
    
    delete the_arena;
    the_arena = nullptr;
    
    delete the_device_arena;
    the_device_arena = nullptr;
    
    delete the_managed_arena;
    the_managed_arena = nullptr;
    
    delete the_pinned_arena;
    the_pinned_arena = nullptr;

    delete the_cpu_arena;
    the_cpu_arena = nullptr;
}
    
Arena*
The_Arena ()
{
    BL_ASSERT(the_arena != nullptr);
    return the_arena;
}

Arena*
The_Device_Arena ()
{
    BL_ASSERT(the_device_arena != nullptr);
    return the_device_arena;
}

Arena*
The_Managed_Arena ()
{
    BL_ASSERT(the_managed_arena != nullptr);
    return the_managed_arena;
}

Arena*
The_Pinned_Arena ()
{
    BL_ASSERT(the_pinned_arena != nullptr);
    return the_pinned_arena;
}

Arena*
The_Cpu_Arena ()
{
    BL_ASSERT(the_cpu_arena != nullptr);
    return the_cpu_arena;
}

}
