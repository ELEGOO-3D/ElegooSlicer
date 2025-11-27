cmake_minimum_required(VERSION 3.13)
project(libslic3r)

include(PrecompiledHeader)

string(TIMESTAMP COMPILE_TIME %Y%m%d-%H%M%S)
set(SLIC3R_BUILD_TIME ${COMPILE_TIME})

if(NOT DEFINED ORCA_CHECK_GCODE_PLACEHOLDERS)
    set(ORCA_CHECK_GCODE_PLACEHOLDERS "0")
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/libslic3r_version.h.in ${CMAKE_CURRENT_BINARY_DIR}/libslic3r_version.h @ONLY)

if (MINGW)
    add_compile_options(-Wa,-mbig-obj)
endif ()

set(OpenVDBUtils_SOURCES "")
if (TARGET OpenVDB::openvdb)
    set(OpenVDBUtils_SOURCES OpenVDBUtils.cpp OpenVDBUtils.hpp)
endif()

set(lisbslic3r_sources
    ArcFitter.cpp
    ArcFitter.hpp
    pchheader.cpp
    pchheader.hpp
    AABBTreeIndirect.hpp
    AABBTreeLines.hpp
    AABBMesh.hpp
    AABBMesh.cpp
    Algorithm/LineSplit.hpp
    Algorithm/LineSplit.cpp
    Algorithm/PathSorting.hpp
    Algorithm/RegionExpansion.hpp
    Algorithm/RegionExpansion.cpp
    AnyPtr.hpp
    BoundingBox.cpp
    BoundingBox.hpp
    BridgeDetector.cpp
    BridgeDetector.hpp
    FaceDetector.cpp
    FaceDetector.hpp
    Brim.cpp
    Brim.hpp
    BuildVolume.cpp
    BuildVolume.hpp
    Circle.cpp
    Circle.hpp
    clipper.cpp
    clipper.hpp
    ClipperUtils.cpp
    ClipperUtils.hpp
    Clipper2Utils.cpp
    Clipper2Utils.hpp
    ClipperZUtils.hpp
    Color.cpp
    Color.hpp
    Config.cpp
    Config.hpp
    CurveAnalyzer.cpp
    CurveAnalyzer.hpp
    EdgeGrid.cpp
    EdgeGrid.hpp
    ElephantFootCompensation.cpp
    ElephantFootCompensation.hpp
    Emboss.cpp
    Emboss.hpp
    EmbossShape.hpp
    enum_bitmask.hpp
    ExPolygon.cpp
    ExPolygon.hpp
    ExPolygonSerialize.hpp
    ExPolygonsIndex.cpp
    ExPolygonsIndex.hpp
    Extruder.cpp
    Extruder.hpp
    ExtrusionEntity.cpp
    ExtrusionEntity.hpp
    ExtrusionEntityCollection.cpp
    ExtrusionEntityCollection.hpp
    ExtrusionSimulator.cpp
    ExtrusionSimulator.hpp
    FileParserError.hpp
    Fill/Fill.cpp
    Fill/Fill.hpp
    Fill/Fill3DHoneycomb.cpp
    Fill/Fill3DHoneycomb.hpp
    Fill/FillAdaptive.cpp
    Fill/FillAdaptive.hpp
    Fill/FillBase.cpp
    Fill/FillBase.hpp
    Fill/FillConcentric.cpp
    Fill/FillConcentric.hpp
    Fill/FillConcentricInternal.cpp
    Fill/FillConcentricInternal.hpp
    Fill/FillCrossHatch.cpp
    Fill/FillCrossHatch.hpp
    Fill/FillHoneycomb.cpp
    Fill/FillHoneycomb.hpp
    Fill/FillGyroid.cpp
    Fill/FillGyroid.hpp
    Fill/FillPlanePath.cpp
    Fill/FillPlanePath.hpp
    Fill/FillLine.cpp
    Fill/FillLine.hpp
    Fill/FillLightning.cpp
    Fill/FillLightning.hpp
    Fill/Lightning/DistanceField.cpp
    Fill/Lightning/DistanceField.hpp
    Fill/Lightning/Generator.cpp
    Fill/Lightning/Generator.hpp
    Fill/Lightning/Layer.cpp
    Fill/Lightning/Layer.hpp
    Fill/Lightning/TreeNode.cpp
    Fill/Lightning/TreeNode.hpp
    Fill/FillRectilinear.cpp
    Fill/FillRectilinear.hpp
    Flow.cpp
    Flow.hpp
    FlushVolCalc.cpp
    FlushVolCalc.hpp
    format.hpp
    Format/3mf.cpp
    Format/3mf.hpp
    Format/bbs_3mf.cpp
    Format/bbs_3mf.hpp
    Format/AMF.cpp
    Format/AMF.hpp
    Format/OBJ.cpp
    Format/OBJ.hpp
    Format/objparser.cpp
    Format/objparser.hpp
    Format/STEP.cpp
    Format/STEP.hpp
    Format/STL.cpp
    Format/STL.hpp
    Format/SL1.hpp
    Format/SL1.cpp
	Format/svg.hpp
    Format/svg.cpp
    Format/ZipperArchiveImport.hpp
    Format/ZipperArchiveImport.cpp
    GCode/ThumbnailData.cpp
    GCode/ThumbnailData.hpp
    GCode/CoolingBuffer.cpp
    GCode/CoolingBuffer.hpp
	GCode/FanMover.cpp
    GCode/FanMover.hpp
    GCode/PostProcessor.cpp
    GCode/PostProcessor.hpp
    GCode/PressureEqualizer.cpp
    GCode/PressureEqualizer.hpp
    GCode/PrintExtents.cpp
    GCode/PrintExtents.hpp
    GCode/RetractWhenCrossingPerimeters.cpp
    GCode/RetractWhenCrossingPerimeters.hpp
    GCode/SmallAreaInfillFlowCompensator.cpp
    GCode/SmallAreaInfillFlowCompensator.hpp
    GCode/PchipInterpolatorHelper.cpp
    GCode/PchipInterpolatorHelper.hpp
    GCode/AdaptivePAInterpolator.cpp
    GCode/AdaptivePAInterpolator.hpp
    GCode/AdaptivePAProcessor.cpp
    GCode/AdaptivePAProcessor.hpp
    GCode/SpiralVase.cpp
    GCode/SpiralVase.hpp
    GCode/SeamPlacer.cpp
    GCode/SeamPlacer.hpp
    GCode/ToolOrdering.cpp
    GCode/ToolOrdering.hpp
    GCode/WipeTower.cpp
    GCode/WipeTower.hpp
    GCode/WipeTower2.cpp
    GCode/WipeTower2.hpp
    GCode/GCodeProcessor.cpp
    GCode/GCodeProcessor.hpp
    GCode/AvoidCrossingPerimeters.cpp
    GCode/AvoidCrossingPerimeters.hpp
    GCode/ExtrusionProcessor.hpp
    GCode/ConflictChecker.cpp
    GCode/ConflictChecker.hpp
    GCode.cpp
    GCode.hpp
    GCodeReader.cpp
    GCodeReader.hpp
    # GCodeSender.cpp
    # GCodeSender.hpp
    GCodeWriter.cpp
    GCodeWriter.hpp
    Geometry.cpp
    Geometry.hpp
    Geometry/Bicubic.hpp
    Geometry/Circle.cpp
    Geometry/Circle.hpp
    Geometry/ConvexHull.cpp
    Geometry/ConvexHull.hpp
    Geometry/Curves.hpp
    Geometry/MedialAxis.cpp
    Geometry/MedialAxis.hpp
	Geometry/Voronoi.cpp
    Geometry/Voronoi.hpp
    Geometry/VoronoiOffset.cpp
    Geometry/VoronoiOffset.hpp
	Geometry/VoronoiUtils.hpp
    Geometry/VoronoiUtils.cpp
    Geometry/VoronoiUtilsCgal.cpp
    Geometry/VoronoiUtilsCgal.hpp
    Geometry/VoronoiVisualUtils.hpp
    Int128.hpp
    KDTreeIndirect.hpp
    Layer.cpp
    Layer.hpp
    LayerRegion.cpp
    libslic3r.h
    libslic3r.cpp
    Line.cpp
    Line.hpp
    BlacklistedLibraryCheck.cpp
    BlacklistedLibraryCheck.hpp
    LocalesUtils.cpp
    LocalesUtils.hpp
    CutUtils.cpp
    CutUtils.hpp
    Model.cpp
    Model.hpp
    ModelArrange.hpp
    ModelArrange.cpp
    MultiMaterialSegmentation.cpp
    MultiMaterialSegmentation.hpp
    Measure.hpp
    Measure.cpp
    MeasureUtils.hpp
    CustomGCode.cpp
    CustomGCode.hpp
    Arrange.hpp
    Arrange.cpp
    NormalUtils.cpp
    NormalUtils.hpp
    ObjColorUtils.hpp
    Orient.hpp
    Orient.cpp
    MultiPoint.cpp
    MultiPoint.hpp
    MutablePriorityQueue.hpp
    NSVGUtils.cpp
    NSVGUtils.hpp
    ObjectID.cpp
    ObjectID.hpp
	ParameterUtils.cpp
    ParameterUtils.hpp
    PerimeterGenerator.cpp
    PerimeterGenerator.hpp
    PlaceholderParser.cpp
    PlaceholderParser.hpp
    Platform.cpp
    Platform.hpp
    Point.cpp
    Point.hpp
    Polygon.cpp
    Polygon.hpp
    MutablePolygon.cpp
    MutablePolygon.hpp
    PolygonTrimmer.cpp
    PolygonTrimmer.hpp
    Polyline.cpp
    Polyline.hpp
    Preset.cpp
    Preset.hpp
    PresetBundle.cpp
    PresetBundle.hpp
    ProjectTask.cpp
    ProjectTask.hpp
    PrincipalComponents2D.hpp
    PrincipalComponents2D.cpp
    AppConfig.cpp
    AppConfig.hpp
    Print.cpp
    Print.hpp
    PrintApply.cpp
    PrintBase.cpp
    PrintBase.hpp
    PrintConfig.cpp
    PrintConfig.hpp
    PrintObject.cpp
    PrintObjectSlice.cpp
    PrintRegion.cpp
    PNGReadWrite.hpp
    PNGReadWrite.cpp
    QuadricEdgeCollapse.cpp
    QuadricEdgeCollapse.hpp
    Semver.cpp
    ShortEdgeCollapse.cpp
    ShortEdgeCollapse.hpp
    ShortestPath.cpp
    ShortestPath.hpp
    SLAPrint.cpp
    SLAPrintSteps.cpp
    SLAPrintSteps.hpp
    SLAPrint.hpp
    Slicing.cpp
    Slicing.hpp
    SlicesToTriangleMesh.hpp
    SlicesToTriangleMesh.cpp
    SlicingAdaptive.cpp
    SlicingAdaptive.hpp
    Support/SupportCommon.cpp
    Support/SupportCommon.hpp
    Support/SupportLayer.hpp
    Support/SupportMaterial.cpp
    Support/SupportMaterial.hpp
    Support/SupportParameters.hpp
    Support/SupportSpotsGenerator.cpp
    Support/SupportSpotsGenerator.hpp
    Support/TreeSupport.hpp
    Support/TreeSupport.cpp
    Support/TreeSupport3D.cpp
    Support/TreeSupport3D.hpp
    Support/TreeSupportCommon.hpp
    Support/TreeModelVolumes.cpp
    Support/TreeModelVolumes.hpp
    PrincipalComponents2D.cpp
    PrincipalComponents2D.hpp
    MinimumSpanningTree.hpp
    MinimumSpanningTree.cpp
    Surface.cpp
    Surface.hpp
    SurfaceCollection.cpp
    SurfaceCollection.hpp
    SurfaceMesh.hpp
    SVG.cpp
    SVG.hpp
    Technologies.hpp
    Tesselate.cpp
    Tesselate.hpp
    TextConfiguration.hpp
    TriangleMesh.cpp
    TriangleMesh.hpp
    TriangleMeshSlicer.cpp
    TriangleMeshSlicer.hpp
    MeshSplitImpl.hpp
    TriangulateWall.hpp
    TriangulateWall.cpp
    utils.cpp
    Utils.hpp
    Time.cpp
    Time.hpp
    Timer.cpp
    Timer.hpp
    Thread.cpp
    Thread.hpp
    TriangleSelector.cpp
    TriangleSelector.hpp
    TriangleSetSampling.cpp
    TriangleSetSampling.hpp
    MTUtils.hpp
    VariableWidth.cpp
    VariableWidth.hpp
    Zipper.hpp
    Zipper.cpp
    MinAreaBoundingBox.hpp
    MinAreaBoundingBox.cpp
    miniz_extension.hpp
    miniz_extension.cpp
    MarchingSquares.hpp
    Execution/Execution.hpp
    Execution/ExecutionSeq.hpp
    Execution/ExecutionTBB.hpp
    Optimize/Optimizer.hpp
    Optimize/NLoptOptimizer.hpp
    Optimize/BruteforceOptimizer.hpp
    SLA/Pad.hpp
    SLA/Pad.cpp
    SLA/SupportTreeBuilder.hpp
    SLA/SupportTreeMesher.hpp
    SLA/SupportTreeMesher.cpp
    SLA/SupportTreeBuildsteps.hpp
    SLA/SupportTreeBuildsteps.cpp
    SLA/SupportTreeBuilder.cpp
    SLA/Concurrency.hpp
    SLA/SupportTree.hpp
    SLA/SupportTree.cpp
#    SLA/SupportTreeIGL.cpp
    SLA/Rotfinder.hpp
    SLA/Rotfinder.cpp
    SLA/BoostAdapter.hpp
    SLA/SpatIndex.hpp
    SLA/SpatIndex.cpp
    SLA/RasterBase.hpp
    SLA/RasterBase.cpp
    SLA/AGGRaster.hpp
    SLA/RasterToPolygons.hpp
    SLA/RasterToPolygons.cpp
    SLA/ConcaveHull.hpp
    SLA/ConcaveHull.cpp
    SLA/Hollowing.hpp
    SLA/Hollowing.cpp
    SLA/JobController.hpp
    SLA/SupportPoint.hpp
    SLA/SupportPointGenerator.hpp
    SLA/SupportPointGenerator.cpp
    SLA/IndexedMesh.hpp
    SLA/IndexedMesh.cpp
    SLA/Clustering.hpp
    SLA/Clustering.cpp
    SLA/ReprojectPointsOnMesh.hpp
    Arachne/BeadingStrategy/BeadingStrategy.hpp
    Arachne/BeadingStrategy/BeadingStrategy.cpp
    Arachne/BeadingStrategy/BeadingStrategyFactory.hpp
    Arachne/BeadingStrategy/BeadingStrategyFactory.cpp
    Arachne/BeadingStrategy/DistributedBeadingStrategy.hpp
    Arachne/BeadingStrategy/DistributedBeadingStrategy.cpp
    Arachne/BeadingStrategy/LimitedBeadingStrategy.hpp
    Arachne/BeadingStrategy/LimitedBeadingStrategy.cpp
    Arachne/BeadingStrategy/OuterWallInsetBeadingStrategy.hpp
    Arachne/BeadingStrategy/OuterWallInsetBeadingStrategy.cpp
    Arachne/BeadingStrategy/RedistributeBeadingStrategy.hpp
    Arachne/BeadingStrategy/RedistributeBeadingStrategy.cpp
    Arachne/BeadingStrategy/WideningBeadingStrategy.hpp
    Arachne/BeadingStrategy/WideningBeadingStrategy.cpp
    Arachne/utils/ExtrusionJunction.hpp
    Arachne/utils/ExtrusionJunction.cpp
    Arachne/utils/ExtrusionLine.hpp
    Arachne/utils/ExtrusionLine.cpp
    Arachne/utils/HalfEdge.hpp
    Arachne/utils/HalfEdgeGraph.hpp
    Arachne/utils/HalfEdgeNode.hpp
    Arachne/utils/SparseGrid.hpp
    Arachne/utils/SparsePointGrid.hpp
    Arachne/utils/SparseLineGrid.hpp
    Arachne/utils/SquareGrid.hpp
    Arachne/utils/SquareGrid.cpp
    Arachne/utils/PolygonsPointIndex.hpp
    Arachne/utils/PolygonsSegmentIndex.hpp
    Arachne/utils/PolylineStitcher.hpp
    Arachne/utils/PolylineStitcher.cpp
    Arachne/SkeletalTrapezoidation.hpp
    Arachne/SkeletalTrapezoidation.cpp
    Arachne/SkeletalTrapezoidationEdge.hpp
    Arachne/SkeletalTrapezoidationGraph.hpp
    Arachne/SkeletalTrapezoidationGraph.cpp
    Arachne/SkeletalTrapezoidationJoint.hpp
    Arachne/WallToolPaths.hpp
    Arachne/WallToolPaths.cpp
    Shape/TextShape.hpp
    Shape/TextShape.cpp
    calib.hpp
    calib.cpp
    GCode/Thumbnails.cpp
    GCode/Thumbnails.hpp
    Interlocking/InterlockingGenerator.hpp
    Interlocking/InterlockingGenerator.cpp
    Interlocking/VoxelUtils.hpp
    Interlocking/VoxelUtils.cpp
)

if (APPLE)
    list(APPEND lisbslic3r_sources
			MacUtils.mm
            Format/ModelIO.hpp
            Format/ModelIO.mm
        )
endif ()

add_library(libslic3r STATIC ${lisbslic3r_sources} 
    "${CMAKE_CURRENT_BINARY_DIR}/libslic3r_version.h"
    ${OpenVDBUtils_SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${lisbslic3r_sources})

if (SLIC3R_STATIC)
    set(CGAL_Boost_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)
endif ()
set(CGAL_DO_NOT_WARN_ABOUT_CMAKE_BUILD_TYPE ON CACHE BOOL "" FORCE)

cmake_policy(PUSH)
cmake_policy(SET CMP0011 NEW)

# Find and configure CGAL
if(USE_SYSTEM_DEPS)
    find_package(CGAL)
    if(NOT CGAL_FOUND)
        message(STATUS "CGAL not found in system, falling back to external project.")
    endif()
else()
    message(STATUS "USE_SYSTEM_DEPS is OFF, implicitly using external CGAL project.")
endif()
if(NOT USE_SYSTEM_DEPS OR NOT CGAL_FOUND)
    add_dependencies(libslic3r dep_CGAL) # Add dependency if not found
endif()
# Find and configure OpenCV
if(USE_SYSTEM_DEPS)
    find_package(OpenCV core)
    if(NOT OpenCV_FOUND)
        message(STATUS "OpenCV not found in system, falling back to external project.")
    endif()
else()
    message(STATUS "USE_SYSTEM_DEPS is OFF, implicitly using external OpenCV project.")
endif()
if(NOT USE_SYSTEM_DEPS OR NOT OpenCV_FOUND)
    add_dependencies(libslic3r dep_OpenCV) # Add dependency if not found
endif()
cmake_policy(POP)

add_library(libslic3r_cgal STATIC 
    CutSurface.hpp CutSurface.cpp
    IntersectionPoints.hpp IntersectionPoints.cpp
    MeshBoolean.hpp MeshBoolean.cpp
    TryCatchSignal.hpp TryCatchSignal.cpp
    Triangulation.hpp Triangulation.cpp
)
target_include_directories(libslic3r_cgal PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# Reset compile options of libslic3r_cgal. Despite it being linked privately, CGAL options
# (-frounding-math) still propagate to dependent libs which is not desired.
get_target_property(_cgal_tgt CGAL::CGAL ALIASED_TARGET)
if (NOT TARGET ${_cgal_tgt})
    set (_cgal_tgt CGAL::CGAL)
endif ()
get_target_property(_opts ${_cgal_tgt} INTERFACE_COMPILE_OPTIONS)
if (_opts)
    set(_opts_bad "${_opts}")
    set(_opts_good "${_opts}")
    list(FILTER _opts_bad INCLUDE REGEX frounding-math)
    list(FILTER _opts_good EXCLUDE REGEX frounding-math)
    set_target_properties(${_cgal_tgt} PROPERTIES INTERFACE_COMPILE_OPTIONS "${_opts_good}")
    target_compile_options(libslic3r_cgal PRIVATE "${_opts_bad}")
endif()

target_link_libraries(libslic3r_cgal PRIVATE ${_cgal_tgt} libigl mcut)

if (MSVC AND "${CMAKE_SIZEOF_VOID_P}" STREQUAL "4") # 32 bit MSVC workaround
    target_compile_definitions(libslic3r_cgal PRIVATE CGAL_DO_NOT_USE_MPZF)
endif ()

encoding_check(libslic3r)

target_compile_definitions(libslic3r PUBLIC -DUSE_TBB -DTBB_USE_CAPTURED_EXCEPTION=0)
target_include_directories(libslic3r PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(libslic3r PUBLIC ${EXPAT_INCLUDE_DIRS})

# Find the OCCT and related libraries
set(OpenCASCADE_DIR "${CMAKE_PREFIX_PATH}/lib/cmake/occt")
# Find and configure OpenCASCADE
if(USE_SYSTEM_DEPS)
    find_package(OpenCASCADE COMPONENTS TKernel TKBRep TKGeomBase)
    if(NOT OpenCASCADE_FOUND)
        message(STATUS "OpenCASCADE not found in system, falling back to external project.")
    endif()
else()
    message(STATUS "USE_SYSTEM_DEPS is OFF, implicitly using external OpenCASCADE project.")
endif()
if(NOT USE_SYSTEM_DEPS OR NOT OpenCASCADE_FOUND)
    add_dependencies(libslic3r dep_OCCT) # Add dependency if not found
    # Need to correctly set include directories for the external project.
    # For now, rely on the external project's install step to populate DESTDIR.
else()
    target_include_directories(libslic3r PUBLIC ${OpenCASCADE_INCLUDE_DIR})
endif()

# Find and configure JPEG
if(USE_SYSTEM_DEPS)
    find_package(JPEG)
    if(NOT JPEG_FOUND)
        message(STATUS "JPEG not found in system, falling back to external project.")
    endif()
else()
    message(STATUS "USE_SYSTEM_DEPS is OFF, implicitly using external JPEG project.")
endif()
if(NOT USE_SYSTEM_DEPS OR NOT JPEG_FOUND)
    add_dependencies(libslic3r dep_JPEG) # Add dependency if not found
endif()

set(OCCT_LIBS
    TKXDESTEP
    TKSTEP
    TKSTEP209
    TKSTEPAttr
    TKSTEPBase
    TKXCAF
    TKXSBase
    TKVCAF
    TKCAF
    TKLCAF
    TKCDF
    TKV3d
    TKService
    TKMesh
    TKBO
    TKPrim
    TKHLR
    TKShHealing
    TKTopAlgo
    TKGeomAlgo
    TKBRep
    TKGeomBase
    TKG3d
    TKG2d
    TKMath
    TKernel
)


    )

# Constructing the library list for libslic3r
set(_libslic3r_LIBS "")

# Add external project dependencies first if they are not found as system libraries
if(NOT USE_SYSTEM_DEPS OR NOT Boost_FOUND)
    list(APPEND _libslic3r_LIBS dep_Boost)
else()
    list(APPEND _libslic3r_LIBS Boost::system Boost::filesystem Boost::thread Boost::log Boost::locale Boost::regex Boost::chrono Boost::atomic Boost::date_time Boost::iostreams Boost::program_options)
endif()

if(NOT USE_SYSTEM_DEPS OR NOT TBB_FOUND)
    list(APPEND _libslic3r_LIBS dep_TBB)
else()
    list(APPEND _libslic3r_LIBS TBB::tbb TBB::tbbmalloc)
endif()

if(NOT USE_SYSTEM_DEPS OR NOT OpenSSL_FOUND)
    list(APPEND _libslic3r_LIBS dep_OpenSSL)
else()
    list(APPEND _libslic3r_LIBS OpenSSL::SSL OpenSSL::Crypto)
endif()

if(NOT USE_SYSTEM_DEPS OR NOT CURL_FOUND)
    list(APPEND _libslic3r_LIBS dep_CURL)
else()
    list(APPEND _libslic3r_LIBS CURL::libcurl)
endif()

if(NOT USE_SYSTEM_DEPS OR NOT ZLIB_FOUND)
    list(APPEND _libslic3r_LIBS dep_ZLIB)
else()
    list(APPEND _libslic3r_LIBS ZLIB::ZLIB)
endif()

if(NOT USE_SYSTEM_DEPS OR NOT PNG_FOUND)
    list(APPEND _libslic3r_LIBS dep_PNG)
else()
    list(APPEND _libslic3r_LIBS PNG::PNG)
endif()

if(NOT USE_SYSTEM_DEPS OR NOT EXPAT_FOUND)
    list(APPEND _libslic3r_LIBS dep_EXPAT)
else()
    list(APPEND _libslic3r_LIBS ${EXPAT_LIBRARIES}) # EXPAT_LIBRARIES is set by find_package(EXPAT)
endif()

if(NOT USE_SYSTEM_DEPS OR NOT GLEW_FOUND)
    list(APPEND _libslic3r_LIBS dep_GLEW)
else()
    list(APPEND _libslic3r_LIBS GLEW::GLEW) # Assuming GLEW exports a target
endif()

if(NOT USE_SYSTEM_DEPS OR NOT glfw3_FOUND)
    list(APPEND _libslic3r_LIBS dep_GLFW)
else()
    list(APPEND _libslic3r_LIBS glfw) # Assuming glfw3 exports 'glfw' target
endif()

if(NOT USE_SYSTEM_DEPS OR NOT cereal_FOUND)
    list(APPEND _libslic3r_LIBS dep_Cereal)
    # The cereal library is header-only, so no explicit link needed to dep_Cereal target
    # The dep_Cereal adds it to include paths, so its fine.
else()
    list(APPEND _libslic3r_LIBS cereal::cereal)
endif()

if(NOT USE_SYSTEM_DEPS OR NOT NLopt_FOUND)
    list(APPEND _libslic3r_LIBS dep_NLopt)
else()
    list(APPEND _libslic3r_LIBS NLopt::nlopt) # Assuming NLopt exports a target
endif()

if(NOT USE_SYSTEM_DEPS OR NOT OpenVDB_FOUND)
    list(APPEND _libslic3r_LIBS dep_OpenVDB)
else()
    list(APPEND _libslic3r_LIBS OpenVDB::openvdb)
endif()

if(NOT USE_SYSTEM_DEPS OR NOT GMP_FOUND)
    list(APPEND _libslic3r_LIBS dep_GMP)
else()
    list(APPEND _libslic3r_LIBS GMP::GMP) # Assuming GMP exports a target
endif()

if(NOT USE_SYSTEM_DEPS OR NOT MPFR_FOUND)
    list(APPEND _libslic3r_LIBS dep_MPFR)
else()
    list(APPEND _libslic3r_LIBS MPFR::MPFR) # Assuming MPFR exports a target
endif()

if(NOT USE_SYSTEM_DEPS OR NOT CGAL_FOUND)
    list(APPEND _libslic3r_LIBS dep_CGAL)
else()
    list(APPEND _libslic3r_LIBS CGAL::CGAL)
endif()

if(NOT USE_SYSTEM_DEPS OR NOT OpenCASCADE_FOUND)
    list(APPEND _libslic3r_LIBS dep_OCCT)
else()
    list(APPEND _libslic3r_LIBS ${OCCT_LIBS}) # OCCT_LIBS contains a list of individual OCCT libraries
endif()

if(NOT USE_SYSTEM_DEPS OR NOT OpenCV_FOUND)
    list(APPEND _libslic3r_LIBS dep_OpenCV)
else()
    list(APPEND _libslic3r_LIBS OpenCV::core OpenCV::imgcodecs OpenCV::imgproc OpenCV::world)
endif()

if(NOT USE_SYSTEM_DEPS OR NOT Freetype_FOUND)
    list(APPEND _libslic3r_LIBS dep_FREETYPE)
else()
    list(APPEND _libslic3r_LIBS Freetype::Freetype) # Assuming Freetype exports a target
endif()

# OpenCSG is always an external project for now
list(APPEND _libslic3r_LIBS dep_OpenCSG)

target_link_libraries(libslic3r PUBLIC
    libnest2d
    admesh
    libigl
    miniz
    clipper
    nowide
    glu-libtess
    qhull
    semver
    libslic3r_cgal
    ${CMAKE_DL_LIBS}
    Clipper2
    mcut
    qoi
    opencv_world
    ${_libslic3r_LIBS} # Add all collected dependencies here
)

# Existing conditional linking logic
if(NOT SLIC3R_STATIC)
    target_compile_definitions(libslic3r PUBLIC BOOST_ALL_DYN_LINK)
endif()

if(NOT WIN32)
    target_link_libraries(libslic3r freetype) # freetype is handled above
    if (NOT APPLE)
        target_link_libraries(libslic3r fontconfig)
    endif()
endif()

if (APPLE)
    find_library(FOUNDATION Foundation REQUIRED)
    find_library(MODELIO ModelIO REQUIRED)
    target_link_libraries(libslic3r ${FOUNDATION} ${MODELIO})
endif ()

if(WIN32)
    target_link_libraries(libslic3r Psapi.lib)
endif()

if(SLIC3R_PROFILE)
    target_link_libraries(libslic3r Shiny)
endif()

if (SLIC3R_PCH AND NOT SLIC3R_SYNTAXONLY)
    add_precompiled_header(libslic3r pchheader.hpp FORCEINCLUDE)
endif ()
