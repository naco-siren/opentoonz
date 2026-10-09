# Builds the vendored SuperLU 4.1 (double-precision subset) from source.
#
# Apple builds used to link thirdparty/superlu/libsuperlu_4.1.a, a universal
# archive that only carries i386 and x86_64 slices, so Apple Silicon builds
# had no SuperLU at all. Compiling the same 4.1 sources here gives arm64 and
# x86_64 macOS identical numerics without depending on Homebrew's SuperLU 7,
# whose headers differ from the 4.1 ones that tlin_superlu_wrap.cpp targets.
#
# Only the files that the "d" (double) solver needs are compiled: ALLAUX and
# DLUSRC from thirdparty/superlu/SuperLU_4.1/SRC/Makefile. BLAS comes from
# Accelerate on Apple.

set(SLU41_SRC_DIR ${SDKROOT}/superlu/SuperLU_4.1/SRC)

set(SLU41_ALLAUX
    superlu_timer.c util.c memory.c get_perm_c.c mmd.c
    sp_coletree.c sp_preorder.c sp_ienv.c relax_snode.c
    heap_relax_snode.c colamd.c
    ilu_relax_snode.c ilu_heap_relax_snode.c mark_relax.c
    mc64ad.c qselect.c
)

set(SLU41_DLUSRC
    dgssv.c dgssvx.c
    dsp_blas2.c dsp_blas3.c dgscon.c
    dlangs.c dgsequ.c dlaqgs.c dpivotgrowth.c
    dgsrfs.c dgstrf.c dgstrs.c dcopy_to_ucol.c
    dsnode_dfs.c dsnode_bmod.c dpanel_dfs.c dpanel_bmod.c
    dreadhb.c dreadrb.c dreadtriple.c
    dcolumn_dfs.c dcolumn_bmod.c dpivotL.c dpruneL.c
    dmemory.c dutil.c dmyblas2.c
    dgsisx.c dgsitrf.c dldperm.c
    ilu_ddrop_row.c ilu_dsnode_dfs.c
    ilu_dcolumn_dfs.c ilu_dpanel_dfs.c ilu_dcopy_to_ucol.c
    ilu_dpivotL.c ddiagonal.c
)

set(SLU41_SOURCES)
foreach(_f ${SLU41_ALLAUX} ${SLU41_DLUSRC})
    list(APPEND SLU41_SOURCES ${SLU41_SRC_DIR}/${_f})
endforeach()

add_library(superlu41 STATIC ${SLU41_SOURCES})

target_include_directories(superlu41 SYSTEM PUBLIC ${SLU41_SRC_DIR})
target_compile_definitions(superlu41 PRIVATE PRNTlevel=0 NDEBUG)

# SuperLU 4.1 is pre-C99 code: it relies on implicit function declarations
# and implicit int, which Clang 16+ and GCC 14+ reject in C99 mode. Compile
# it as gnu90, where those are still only warnings, and silence the noise.
set_target_properties(superlu41 PROPERTIES
    C_STANDARD 90
    C_EXTENSIONS ON
    POSITION_INDEPENDENT_CODE ON
)
if(NOT MSVC)
    target_compile_options(superlu41 PRIVATE -w)
endif()

if(APPLE)
    find_library(SLU41_ACCELERATE Accelerate)
    target_link_libraries(superlu41 PUBLIC ${SLU41_ACCELERATE})
endif()
