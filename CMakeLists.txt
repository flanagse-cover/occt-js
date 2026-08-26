cmake_minimum_required(VERSION 3.20)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_CONFIGURATION_TYPES Debug Release RelWithDebInfo MinSizeRel)

add_definitions(-DUNICODE -D_UNICODE)

project(OcctJS)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(OcctGeneratedIncludeDir "${CMAKE_CURRENT_BINARY_DIR}/inc")
file(MAKE_DIRECTORY "${OcctGeneratedIncludeDir}")

include("${CMAKE_CURRENT_SOURCE_DIR}/occt/adm/cmake/version.cmake")

string(TIMESTAMP OCCT_VERSION_DATE "%Y-%m-%d" UTC)
set(SET_OCC_VERSION_DEVELOPMENT "")
if (NOT "${OCC_VERSION_DEVELOPMENT}" STREQUAL ""
    AND NOT "${OCC_VERSION_DEVELOPMENT}" STREQUAL "OCC_VERSION_DEVELOPMENT")
    set(SET_OCC_VERSION_DEVELOPMENT "#define OCC_VERSION_DEVELOPMENT \"${OCC_VERSION_DEVELOPMENT}\"")
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/occt/adm/templates/Standard_Version.hxx.in"
    "${OcctGeneratedIncludeDir}/Standard_Version.hxx"
    @ONLY
)

# ---------------------------------------------------------------------------
# Macro: AddOcctModule
#   Collects all .cxx/.c sources from occt/src/<Module>/<Toolkit>/<Package>
#   and adds the directory to the include path list.  A package named here
#   and absent from the checkout is always a bug, so it is fatal.
# ---------------------------------------------------------------------------
set(OcctSourceFolders)
set(OcctIncludeDirs "${OcctGeneratedIncludeDir}")

function(ResolveOcctPackage moduleName outVar)
    file(GLOB _modDirs "${CMAKE_CURRENT_SOURCE_DIR}/occt/src/*/*/${moduleName}")
    list(LENGTH _modDirs _count)
    if(NOT _count EQUAL 1)
        message(FATAL_ERROR "OCCT package '${moduleName}' resolved to ${_count} directories under occt/src/*/*/ (expected 1).")
    endif()
    list(GET _modDirs 0 _modDir)
    set(${outVar} "${_modDir}" PARENT_SCOPE)
endfunction()

function(AddOcctModule moduleName)
    ResolveOcctPackage("${moduleName}" _modDir)
    list(APPEND OcctSourceFolders "${_modDir}/*.c*")
    list(APPEND OcctIncludeDirs   "${_modDir}")
    set(OcctSourceFolders ${OcctSourceFolders} PARENT_SCOPE)
    set(OcctIncludeDirs   ${OcctIncludeDirs}   PARENT_SCOPE)
endfunction()

# Header-only: add include path but do NOT compile .cxx files.
# Use for modules needed only for type definitions referenced by other headers.
function(AddOcctHeaders moduleName)
    ResolveOcctPackage("${moduleName}" _modDir)
    list(APPEND OcctIncludeDirs "${_modDir}")
    set(OcctIncludeDirs ${OcctIncludeDirs} PARENT_SCOPE)
endfunction()

# 8.0 relocated the TColgp/TColStd/TopTools/TDF collection typedefs here.
list(APPEND OcctIncludeDirs "${CMAKE_CURRENT_SOURCE_DIR}/occt/src/Deprecated/NCollectionAliases")

# ===========================================================================
#  OCCT Modules
# ===========================================================================

# --- Kernel / Foundation ---
AddOcctModule(Standard)
AddOcctModule(NCollection)
AddOcctModule(TCollection)
AddOcctModule(TColStd)
AddOcctModule(OSD)
AddOcctModule(Message)
AddOcctModule(Plugin)
AddOcctModule(Resource)
AddOcctModule(Units)
AddOcctModule(UnitsAPI)
AddOcctModule(UnitsMethods)
AddOcctModule(Storage)
AddOcctModule(FSD)
AddOcctModule(LDOM)
AddOcctModule(UTL)
AddOcctModule(StdFail)
AddOcctModule(Quantity)
AddOcctModule(TShort)

# --- Math / Geometry primitives ---
AddOcctModule(gp)
AddOcctModule(gce)
AddOcctModule(math)
AddOcctModule(MathRoot)
AddOcctModule(MathSys)
AddOcctModule(MathLin)
AddOcctModule(MathPoly)
AddOcctModule(MathUtils)
AddOcctModule(MathInteg)
AddOcctModule(MathOpt)
AddOcctModule(Precision)
AddOcctModule(ElCLib)
AddOcctModule(ElSLib)
AddOcctModule(BSplCLib)
AddOcctModule(BSplSLib)
AddOcctModule(PLib)
AddOcctModule(Geom)
AddOcctModule(Geom2d)
AddOcctModule(GeomAbs)
AddOcctModule(GeomAdaptor)
AddOcctModule(GeomAPI)
AddOcctModule(GeomConvert)
AddOcctModule(GeomEval)
AddOcctModule(GeomGridEval)
AddOcctModule(GeomHash)
AddOcctModule(GeomFill)
AddOcctModule(GeomInt)
AddOcctModule(GeomLib)
AddOcctModule(GeomLProp)
AddOcctModule(GeomPlate)
AddOcctModule(GeomProjLib)
AddOcctModule(GeomTools)
AddOcctModule(GeomToStep)
AddOcctModule(Adaptor2d)
AddOcctModule(Adaptor3d)
AddOcctModule(Geom2dAdaptor)
AddOcctModule(Geom2dAPI)
AddOcctModule(Geom2dConvert)
AddOcctModule(Geom2dEval)
AddOcctModule(Geom2dGridEval)
AddOcctModule(Geom2dHash)
AddOcctModule(Geom2dGcc)
AddOcctModule(Geom2dHatch)
AddOcctModule(Geom2dInt)
AddOcctModule(GC)
AddOcctModule(GCE2d)
AddOcctModule(GCPnts)
AddOcctModule(GProp)
AddOcctModule(Convert)
AddOcctModule(CPnts)
AddOcctModule(CSLib)
AddOcctModule(Approx)
AddOcctModule(ApproxInt)
AddOcctModule(AppCont)
AddOcctModule(AppDef)
AddOcctModule(AppParCurves)
AddOcctModule(AdvApp2Var)
AddOcctModule(AdvApprox)
AddOcctModule(Plate)
AddOcctModule(NLPlate)
AddOcctModule(Bnd)
AddOcctModule(BndLib)
AddOcctModule(GeomBndLib)
AddOcctModule(BVH)
AddOcctModule(FEmTool)
AddOcctModule(LProp)
AddOcctModule(ProjLib)
AddOcctModule(Hermit)
AddOcctModule(Law)
AddOcctModule(Intrv)
AddOcctModule(LocalAnalysis)
AddOcctModule(Extrema)
AddOcctModule(ExtremaPC)

# --- Intersections ---
AddOcctModule(IntAna)
AddOcctModule(IntAna2d)
AddOcctModule(IntCurve)
AddOcctModule(IntCurveSurface)
AddOcctModule(IntCurvesFace)
AddOcctModule(IntRes2d)
AddOcctModule(IntSurf)
AddOcctModule(IntImp)
AddOcctModule(IntImpParGen)
AddOcctModule(IntPatch)
AddOcctModule(IntPolyh)
AddOcctModule(IntStart)
AddOcctModule(IntWalk)
AddOcctModule(Intf)
AddOcctModule(Contap)
AddOcctModule(Hatch)
AddOcctModule(HatchGen)

# --- GCC solver ---
AddOcctModule(GccAna)
AddOcctModule(GccEnt)
AddOcctModule(GccInt)

# --- Topology ---
AddOcctModule(TopoDS)
AddOcctModule(TopAbs)
AddOcctModule(TopExp)
AddOcctModule(TopLoc)
AddOcctModule(TopTools)
AddOcctModule(TopBas)
AddOcctModule(TopClass)
AddOcctModule(TopCnx)
AddOcctModule(TopTrans)
AddOcctModule(BRep)
AddOcctModule(BRepTools)
AddOcctModule(BRepBuilderAPI)
AddOcctModule(BRepAdaptor)
AddOcctModule(BRepAlgo)
AddOcctModule(BRepAlgoAPI)
AddOcctModule(BRepApprox)
AddOcctModule(BRepBlend)
AddOcctModule(BRepBndLib)
AddOcctModule(BRepCheck)
AddOcctModule(BRepClass)
AddOcctModule(BRepClass3d)
AddOcctModule(BRepExtrema)
AddOcctModule(BRepFill)
AddOcctModule(BRepGProp)
AddOcctModule(BRepIntCurveSurface)
AddOcctModule(BRepLib)
AddOcctModule(BRepLProp)
AddOcctModule(BRepMAT2d)
AddOcctModule(BRepMesh)
AddOcctModule(BRepMeshData)
AddOcctModule(BRepOffset)
AddOcctModule(BRepOffsetAPI)
AddOcctModule(BRepPrim)
AddOcctModule(BRepPrimAPI)
AddOcctModule(BRepSweep)
AddOcctModule(BRepTopAdaptor)
AddOcctModule(BRepFeat)
AddOcctModule(BRepFilletAPI)
AddOcctModule(BRepProj)
AddOcctModule(Sweep)
AddOcctModule(Bisector)
AddOcctModule(MAT)
AddOcctModule(MAT2d)
AddOcctModule(AppBlend)
AddOcctModule(Blend)
AddOcctModule(BlendFunc)
AddOcctModule(ChFi2d)
AddOcctModule(ChFi3d)
AddOcctModule(ChFiDS)
AddOcctModule(ChFiKPart)
AddOcctModule(Draft)
AddOcctModule(FilletSurf)
AddOcctModule(BiTgte)
AddOcctModule(LocOpe)

# --- Boolean operations ---
AddOcctModule(BOPAlgo)
AddOcctModule(BOPDS)
AddOcctModule(BOPTools)
AddOcctModule(IntTools)

# --- TopOpeBRep (legacy but still needed by some modules) ---
AddOcctModule(TopOpeBRep)
AddOcctModule(TopOpeBRepBuild)
AddOcctModule(TopOpeBRepDS)
AddOcctModule(TopOpeBRepTool)

# --- HLR (headers only, see visualization section) ---

# --- Shape Healing ---
AddOcctModule(ShapeAlgo)
AddOcctModule(ShapeAnalysis)
AddOcctModule(ShapeBuild)
AddOcctModule(ShapeConstruct)
AddOcctModule(ShapeCustom)
AddOcctModule(ShapeExtend)
AddOcctModule(ShapeFix)
AddOcctModule(ShapeProcess)
AddOcctModule(ShapeProcessAPI)
AddOcctModule(ShapeUpgrade)

# --- Meshing ---
AddOcctModule(IMeshData)
AddOcctModule(IMeshTools)
AddOcctModule(Poly)

# --- STEP exchange ---
AddOcctModule(STEPControl)
AddOcctModule(STEPCAFControl)
AddOcctModule(STEPConstruct)
AddOcctModule(STEPEdit)
AddOcctModule(STEPSelections)
AddOcctModule(StepData)
AddOcctModule(StepFile)
AddOcctModule(StepBasic)
AddOcctModule(StepGeom)
AddOcctModule(StepShape)
AddOcctModule(StepRepr)
AddOcctModule(StepVisual)
AddOcctModule(StepAP203)
AddOcctModule(StepAP209)
AddOcctModule(StepAP214)
AddOcctModule(StepAP242)
AddOcctModule(StepDimTol)
AddOcctModule(StepElement)
AddOcctModule(StepFEA)
AddOcctModule(StepKinematics)
AddOcctModule(StepSelect)
AddOcctModule(StepToGeom)
AddOcctModule(StepToTopoDS)
AddOcctModule(StepTidy)
AddOcctModule(TopoDSToStep)
# GeomToIGES / Geom2dToIGES: headers only (see IGES section)

# --- RWStep readers ---
AddOcctModule(RWHeaderSection)
AddOcctModule(RWStepAP203)
AddOcctModule(RWStepAP214)
AddOcctModule(RWStepAP242)
AddOcctModule(RWStepBasic)
AddOcctModule(RWStepDimTol)
AddOcctModule(RWStepElement)
AddOcctModule(RWStepFEA)
AddOcctModule(RWStepGeom)
AddOcctModule(RWStepKinematics)
AddOcctModule(RWStepRepr)
AddOcctModule(RWStepShape)
AddOcctModule(RWStepVisual)
AddOcctModule(HeaderSection)
AddOcctModule(APIHeaderSection)

# --- IGES exchange ---
AddOcctModule(IGESAppli)
AddOcctModule(IGESBasic)
AddOcctModule(IGESCAFControl)
AddOcctModule(IGESControl)
AddOcctModule(IGESConvGeom)
AddOcctModule(IGESData)
AddOcctModule(IGESDefs)
AddOcctModule(IGESDimen)
AddOcctModule(IGESDraw)
AddOcctModule(IGESFile)
AddOcctModule(IGESGeom)
AddOcctModule(IGESGraph)
AddOcctModule(IGESSelect)
AddOcctModule(IGESSolid)
AddOcctModule(IGESToBRep)
AddOcctModule(BRepToIGES)
AddOcctModule(BRepToIGESBRep)
AddOcctModule(GeomToIGES)
AddOcctModule(Geom2dToIGES)

# --- Exchange infrastructure ---
AddOcctModule(Interface)
AddOcctModule(Transfer)
AddOcctModule(TransferBRep)
AddOcctModule(XSControl)
AddOcctModule(XSAlgo)
AddOcctModule(IFSelect)
AddOcctModule(IFGraph)
AddOcctModule(MoniTool)
AddOcctModule(LibCtl)

# --- TObj persistence ---
AddOcctModule(TObj)
AddOcctModule(BinTObjDrivers)
AddOcctModule(XmlTObjDrivers)

# --- XML persistence ---
AddOcctModule(XmlDrivers)
AddOcctModule(XmlLDrivers)
AddOcctModule(XmlMDF)
AddOcctModule(XmlMDataStd)
AddOcctModule(XmlMDataXtd)
AddOcctModule(XmlMDocStd)
AddOcctModule(XmlMFunction)
AddOcctModule(XmlMNaming)
AddOcctModule(XmlMXCAFDoc)
AddOcctModule(XmlObjMgt)
AddOcctModule(XmlXCAFDrivers)

# --- Standard persistence ---
AddOcctModule(StdObject)
AddOcctModule(StdStorage)

# --- DE framework (V7.8+ data exchange plugin system) ---
AddOcctModule(DE)
AddOcctModule(DEBREP)
# --- Non-STEP DE plugins (headers only) ---
AddOcctModule(DEIGES)
AddOcctHeaders(DEGLTF)
AddOcctHeaders(DEOBJ)
AddOcctHeaders(DEPLY)
AddOcctHeaders(DESTL)
AddOcctHeaders(DEVRML)

# --- Non-STEP RW/API modules (headers only) ---
AddOcctHeaders(RWGltf)
AddOcctHeaders(RWObj)
AddOcctHeaders(RWPly)
AddOcctHeaders(RWStl)
AddOcctHeaders(RWMesh)
AddOcctHeaders(StlAPI)
AddOcctHeaders(Vrml)
AddOcctHeaders(VrmlAPI)
AddOcctHeaders(VrmlConverter)
AddOcctHeaders(VrmlData)
# XBRepMesh excluded: DISCRETALGO symbol conflicts with BRepMesh_IncrementalMesh
AddOcctModule(DEBRepCascade)
AddOcctModule(DESTEP)
AddOcctModule(DEXCAF)
AddOcctModule(DEXCAFCascade)

# --- XDE / XCAF framework ---
AddOcctModule(XCAFApp)
AddOcctModule(XCAFDoc)
AddOcctModule(XCAFPrs)
AddOcctModule(XCAFDimTolObjects)
AddOcctModule(XCAFNoteObjects)
AddOcctModule(XCAFView)
AddOcctModule(TDocStd)
AddOcctModule(TDF)
AddOcctModule(TDataStd)
AddOcctModule(TDataXtd)
AddOcctModule(TFunction)
AddOcctModule(TNaming)
AddOcctModule(TPrsStd)
AddOcctModule(CDF)
AddOcctModule(CDM)
AddOcctModule(PCDM)

# --- Persistence / serialization ---
AddOcctModule(BinDrivers)
AddOcctModule(BinLDrivers)
AddOcctModule(BinMDF)
AddOcctModule(BinMDataStd)
AddOcctModule(BinMDataXtd)
AddOcctModule(BinMDocStd)
AddOcctModule(BinMFunction)
AddOcctModule(BinMNaming)
AddOcctModule(BinMXCAFDoc)
AddOcctModule(BinObjMgt)
AddOcctModule(BinTools)
AddOcctModule(BinXCAFDrivers)
AddOcctModule(StdDrivers)
AddOcctModule(StdLDrivers)
AddOcctModule(StdObjMgt)
AddOcctModule(StdLPersistent)
AddOcctModule(StdPersistent)
AddOcctModule(ShapePersistent)
AddOcctModule(AppStd)
AddOcctModule(AppStdL)

# --- Visualization (header-only) ---
# XCAFDoc/XCAFPrs reference visualization types in their headers.
# We only need the headers to compile; at link time these symbols are
# not called on our STEP-import code path, so we skip .cxx compilation.
AddOcctModule(AIS)
AddOcctModule(Prs3d)
AddOcctModule(PrsMgr)
AddOcctModule(StdPrs)
AddOcctModule(StdSelect)
AddOcctModule(Select3D)
AddOcctModule(SelectBasics)
AddOcctModule(SelectMgr)
AddOcctModule(V3d)
AddOcctModule(Graphic3d)
AddOcctModule(Aspect)
AddOcctModule(Image)  # BinMXCAFDoc_VisMaterialDriver needs Image_Texture
AddOcctModule(Font)
AddOcctModule(Media)
AddOcctModule(PrsDim)
AddOcctModule(DsgPrs)
AddOcctModule(WNT)
AddOcctModule(HLRAlgo)
AddOcctModule(HLRBRep)
AddOcctModule(HLRTopoBRep)
AddOcctModule(FairCurve)

# --- Misc expression / mesh utilities ---
AddOcctModule(Expr)
AddOcctModule(ExprIntrp)
AddOcctModule(FlexLexer)
# FairCurve: headers only (see visualization section)

# --- Resource files (message/step) ---
AddOcctModule(SHMessage)
AddOcctModule(XSMessage)

# ===========================================================================
#  Glob all collected source files
# ===========================================================================
file(GLOB OcctSourceFiles ${OcctSourceFolders})

# ===========================================================================
#  Project sources
# ===========================================================================
set(OcctJSSrcDir "${CMAKE_CURRENT_SOURCE_DIR}/src")
file(GLOB OcctJSSourceFiles CONFIGURE_DEPENDS
    ${OcctJSSrcDir}/*.hpp
    ${OcctJSSrcDir}/*.cpp
)
source_group("Sources" FILES ${OcctJSSourceFiles})
source_group("OCCT"    FILES ${OcctSourceFiles})

# ===========================================================================
#  Target
# ===========================================================================
# The kernel is an archive so the linker pulls only the members that resolve
# an undefined symbol, instead of linking all ~3000 OCCT objects.
add_library(occt-kernel STATIC ${OcctSourceFiles})
target_compile_options(occt-kernel PUBLIC -DOCCT_NO_PLUGINS -DOCCT_NO_DEPRECATED)
target_include_directories(occt-kernel PUBLIC ${OcctIncludeDirs})

if(${EMSCRIPTEN})
    add_executable(occt-js ${OcctJSSourceFiles})

    target_compile_options(occt-kernel PUBLIC "$<$<CONFIG:Debug>:-gsource-map>")
    target_compile_options(occt-kernel PUBLIC "$<$<CONFIG:Release>:-Oz>")
    target_compile_options(occt-kernel PUBLIC -fwasm-exceptions)

    target_link_options(occt-js PUBLIC -sMODULARIZE=1)
    target_link_options(occt-js PUBLIC -sEXPORT_NAME='OcctJS')
    target_link_options(occt-js PUBLIC -sSTACK_SIZE=10MB)
    target_link_options(occt-js PUBLIC -sALLOW_MEMORY_GROWTH=1)
    target_link_options(occt-js PUBLIC -fwasm-exceptions)
    target_link_options(occt-js PUBLIC --bind)
else()
    add_library(occt-js ${OcctJSSourceFiles})
endif()

target_link_libraries(occt-js PRIVATE occt-kernel)
target_include_directories(occt-js PUBLIC ${OcctJSSrcDir})

# Output into dist/
set_target_properties(occt-js PROPERTIES
    OUTPUT_NAME "occt-js"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist"
)
