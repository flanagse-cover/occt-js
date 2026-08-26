#include "extruded-shape.hpp"

#include "importer-utils.hpp"
#include "profile-2d.hpp"

#include <BRepCheck_Analyzer.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GC_MakeSegment.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <exception>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

using emscripten::val;

namespace {

constexpr double kPointTolerance = 1e-9;
constexpr double kPi = 3.14159265358979323846;

struct ExtrudedProfileEdgeSource {
    TopoDS_Edge edge;
    int segmentIndex = -1;
    bool hasSegmentIndex = false;
    std::string segmentId;
    bool hasSegmentId = false;
    std::string segmentTag;
    bool hasSegmentTag = false;
};

struct ExtrudedProfileWireBuild {
    TopoDS_Wire wire;
    std::vector<ExtrudedProfileEdgeSource> edgeSources;
};

bool IsObject(const val& jsValue)
{
    return jsValue.typeOf().as<std::string>() == "object" && !jsValue.isNull();
}

bool IsNumber(const val& jsValue)
{
    return jsValue.typeOf().as<std::string>() == "number";
}

bool IsString(const val& jsValue)
{
    return jsValue.typeOf().as<std::string>() == "string";
}

bool TryGetOwnProperty(const val& jsObject, const char* key, val& out)
{
    if (!IsObject(jsObject) || !jsObject.hasOwnProperty(key)) {
        return false;
    }
    out = jsObject[key];
    return true;
}

OcctExtrudedShapeDiagnostic MakeDiagnostic(
    const std::string& code,
    const std::string& message,
    const std::string& path = std::string(),
    int segmentIndex = -1)
{
    OcctExtrudedShapeDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.message = message;
    if (!path.empty()) {
        diagnostic.path = path;
        diagnostic.hasPath = true;
    }
    if (segmentIndex >= 0) {
        diagnostic.segmentIndex = segmentIndex;
        diagnostic.hasSegmentIndex = true;
    }
    return diagnostic;
}

void AddDiagnostic(
    OcctExtrudedShapeValidationResult& result,
    const std::string& code,
    const std::string& message,
    const std::string& path = std::string(),
    int segmentIndex = -1)
{
    result.diagnostics.push_back(MakeDiagnostic(code, message, path, segmentIndex));
}

void AddDiagnostic(
    OcctExtrudedShapeBuildResult& result,
    const std::string& code,
    const std::string& message,
    const std::string& path = std::string(),
    int segmentIndex = -1)
{
    result.diagnostics.push_back(MakeDiagnostic(code, message, path, segmentIndex));
}

void ParseVersion(const val& jsSpec, OcctExtrudedShapeValidationResult& result)
{
    val jsVersion = val::undefined();
    if (!TryGetOwnProperty(jsSpec, "version", jsVersion)) {
        return;
    }

    if (!IsNumber(jsVersion)) {
        AddDiagnostic(result, "invalid-type", "Spec.version must be the number 1.", "version");
        return;
    }

    const double version = jsVersion.as<double>();
    if (!std::isfinite(version) || std::floor(version) != version || static_cast<int>(version) != 1) {
        AddDiagnostic(result, "unsupported-version", "Only generated extruded shape spec version 1 is supported.", "version");
        return;
    }

    result.spec.version = static_cast<int>(version);
}

void ParseUnits(const val& jsSpec, OcctExtrudedShapeValidationResult& result)
{
    val jsUnits = val::undefined();
    if (!TryGetOwnProperty(jsSpec, "units", jsUnits)) {
        AddDiagnostic(result, "missing-field", "Spec.units is required.", "units");
        return;
    }
    if (!IsString(jsUnits)) {
        AddDiagnostic(result, "invalid-type", "Spec.units must be a string.", "units");
        return;
    }

    result.spec.units = jsUnits.as<std::string>();
    if (result.spec.units != "mm" && result.spec.units != "inch") {
        AddDiagnostic(result, "unsupported-unit", "Supported units are mm and inch.", "units");
    }
}

void ParseProfile(const val& jsSpec, OcctExtrudedShapeValidationResult& result)
{
    val jsProfile = val::undefined();
    if (!TryGetOwnProperty(jsSpec, "profile", jsProfile)) {
        AddDiagnostic(result, "missing-field", "Spec.profile is required.", "profile");
        return;
    }
    if (!IsObject(jsProfile)) {
        AddDiagnostic(result, "invalid-type", "Spec.profile must be an object.", "profile");
        return;
    }

    OcctProfile2DValidationOptions options;
    options.rootPath = "profile";
    options.requireClosed = true;
    const OcctProfile2DValidationResult profile = ParseAndValidateProfile2DSpec(jsProfile, options);
    for (const auto& diagnostic : profile.diagnostics) {
        result.diagnostics.push_back(diagnostic);
    }
    if (profile.ok) {
        result.spec.profile = profile.spec;
    }
}

void ParseExtrusion(const val& jsSpec, OcctExtrudedShapeValidationResult& result)
{
    val jsExtrusion = val::undefined();
    if (!TryGetOwnProperty(jsSpec, "extrusion", jsExtrusion)) {
        AddDiagnostic(result, "missing-field", "Spec.extrusion is required.", "extrusion");
        return;
    }
    if (!IsObject(jsExtrusion)) {
        AddDiagnostic(result, "invalid-type", "Spec.extrusion must be an object.", "extrusion");
        return;
    }

    val jsDepth = val::undefined();
    if (!TryGetOwnProperty(jsExtrusion, "depth", jsDepth) || !IsNumber(jsDepth)) {
        AddDiagnostic(result, "invalid-extrusion-depth", "Extrusion depth must be a positive finite number.", "extrusion.depth");
        return;
    }

    const double depth = jsDepth.as<double>();
    result.spec.depth = depth;
    if (!std::isfinite(depth) || depth <= 0.0) {
        AddDiagnostic(result, "invalid-extrusion-depth", "Extrusion depth must be a positive finite number.", "extrusion.depth");
    }
}

double SquaredDistance(const std::array<double, 2>& left, const std::array<double, 2>& right)
{
    const double dx = left[0] - right[0];
    const double dy = left[1] - right[1];
    return dx * dx + dy * dy;
}

gp_Pnt ProfilePointToPnt(const std::array<double, 2>& point)
{
    return gp_Pnt(point[0], point[1], 0.0);
}

OcctColor NeutralShapeColor()
{
    return OcctColor(0.84, 0.86, 0.89);
}

OcctColor MakeSemanticColor(double r, double g, double b)
{
    return OcctColor(r, g, b);
}

std::string ToLowerAscii(std::string value)
{
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
    return value;
}

uint32_t StableHash32(const std::string& text)
{
    uint32_t hash = 2166136261u;
    for (unsigned char ch : text) {
        hash ^= static_cast<uint32_t>(ch);
        hash *= 16777619u;
    }
    return hash;
}

OcctColor HashedWallColor(const std::string& semanticKey)
{
    static const std::array<OcctColor, 6> kWallPalette = {
        MakeSemanticColor(0.20, 0.57, 0.72),
        MakeSemanticColor(0.73, 0.45, 0.24),
        MakeSemanticColor(0.35, 0.60, 0.47),
        MakeSemanticColor(0.58, 0.52, 0.73),
        MakeSemanticColor(0.77, 0.56, 0.20),
        MakeSemanticColor(0.48, 0.62, 0.69),
    };

    const uint32_t hash = StableHash32(semanticKey.empty() ? "wall" : semanticKey);
    return kWallPalette[hash % kWallPalette.size()];
}

OcctColor ResolveWallAppearanceColor(const OcctGeneratedShapeFaceBinding& binding)
{
    if (binding.hasSegmentTag) {
        const std::string tag = ToLowerAscii(binding.segmentTag);
        if (tag == "wall") {
            return MakeSemanticColor(0.20, 0.57, 0.72);
        }
        if (tag == "curved") {
            return MakeSemanticColor(0.73, 0.45, 0.24);
        }
        if (tag == "base") {
            return MakeSemanticColor(0.35, 0.60, 0.47);
        }
        return HashedWallColor(tag);
    }

    if (binding.hasSegmentId) {
        return HashedWallColor(ToLowerAscii(binding.segmentId));
    }

    if (binding.hasSegmentIndex) {
        return HashedWallColor("segment:" + std::to_string(binding.segmentIndex));
    }

    return MakeSemanticColor(0.63, 0.66, 0.71);
}

OcctColor ResolveExtrudedFaceColor(const OcctGeneratedShapeFaceBinding& binding)
{
    const std::string role = ToLowerAscii(binding.systemRole);
    if (role == "start_cap") {
        return MakeSemanticColor(0.88, 0.67, 0.25);
    }
    if (role == "end_cap") {
        return MakeSemanticColor(0.69, 0.39, 0.24);
    }
    return ResolveWallAppearanceColor(binding);
}

ImportParams ParseBuildParams(const val& jsOptions, const OcctExtrudedShapeSpec& spec)
{
    ImportParams params;
    params.linearUnit =
        spec.units == "inch"
            ? ImportParams::LinearUnit::Inch
            : ImportParams::LinearUnit::Millimeter;
    params.readNames = true;
    params.readColors = false;
    params.appearanceMode = ImportParams::AppearanceMode::DefaultColor;
    params.defaultColor = NeutralShapeColor();
    params.defaultOpacity = 1.0;
    params.hasDefaultOpacity = true;

    if (!IsObject(jsOptions)) {
        return params;
    }

    val jsLinearDeflectionType = val::undefined();
    if (TryGetOwnProperty(jsOptions, "linearDeflectionType", jsLinearDeflectionType) && IsString(jsLinearDeflectionType)) {
        const std::string linearDeflectionType = jsLinearDeflectionType.as<std::string>();
        if (linearDeflectionType == "absolute_value") {
            params.linearDeflectionType = ImportParams::LinearDeflectionType::AbsoluteValue;
        } else if (linearDeflectionType == "bounding_box_ratio") {
            params.linearDeflectionType = ImportParams::LinearDeflectionType::BoundingBoxRatio;
        }
    }

    val jsLinearDeflection = val::undefined();
    if (TryGetOwnProperty(jsOptions, "linearDeflection", jsLinearDeflection) && IsNumber(jsLinearDeflection)) {
        const double linearDeflection = jsLinearDeflection.as<double>();
        if (std::isfinite(linearDeflection) && linearDeflection > 0.0) {
            params.linearDeflection = linearDeflection;
        }
    }

    val jsAngularDeflection = val::undefined();
    if (TryGetOwnProperty(jsOptions, "angularDeflection", jsAngularDeflection) && IsNumber(jsAngularDeflection)) {
        const double angularDeflection = jsAngularDeflection.as<double>();
        if (std::isfinite(angularDeflection) && angularDeflection > 0.0) {
            params.angularDeflection = angularDeflection;
        }
    }

    return params;
}

bool TryMakeLineEdge(const gp_Pnt& startPoint, const gp_Pnt& endPoint, TopoDS_Edge& edge)
{
    GC_MakeSegment makeSegment(startPoint, endPoint);
    if (!makeSegment.IsDone()) {
        return false;
    }

    BRepBuilderAPI_MakeEdge edgeBuilder(makeSegment.Value());
    if (!edgeBuilder.IsDone()) {
        return false;
    }

    edge = edgeBuilder.Edge();
    return !edge.IsNull();
}

bool TryMakeArcCenterEdge(
    const std::array<double, 2>& start,
    const OcctExtrudedShapeSegment& segment,
    TopoDS_Edge& edge)
{
    const double radius = std::sqrt(SquaredDistance(start, segment.center));
    const double startAngle = std::atan2(start[1] - segment.center[1], start[0] - segment.center[0]);
    const double endAngle = std::atan2(
        segment.end[1] - segment.center[1],
        segment.end[0] - segment.center[0]);
    double delta = std::remainder(endAngle - startAngle, 2.0 * kPi);
    const double cross2d =
        (start[0] - segment.center[0]) * (segment.end[1] - segment.center[1]) -
        (start[1] - segment.center[1]) * (segment.end[0] - segment.center[0]);

    if (std::abs(std::abs(delta) - kPi) <= 1e-12) {
        delta = cross2d >= 0.0 ? kPi : -kPi;
    } else if (cross2d > 0.0 && delta < 0.0) {
        delta += 2.0 * kPi;
    } else if (cross2d < 0.0 && delta > 0.0) {
        delta -= 2.0 * kPi;
    }

    const double throughAngle = startAngle + (delta * 0.5);
    const std::array<double, 2> through = {
        segment.center[0] + (radius * std::cos(throughAngle)),
        segment.center[1] + (radius * std::sin(throughAngle))
    };

    GC_MakeArcOfCircle makeArc(
        ProfilePointToPnt(start),
        ProfilePointToPnt(through),
        ProfilePointToPnt(segment.end));
    if (!makeArc.IsDone()) {
        return false;
    }

    BRepBuilderAPI_MakeEdge edgeBuilder(makeArc.Value());
    if (!edgeBuilder.IsDone()) {
        return false;
    }

    edge = edgeBuilder.Edge();
    return !edge.IsNull();
}

bool TryMakeArc3PointEdge(
    const std::array<double, 2>& start,
    const OcctExtrudedShapeSegment& segment,
    TopoDS_Edge& edge)
{
    GC_MakeArcOfCircle makeArc(
        ProfilePointToPnt(start),
        ProfilePointToPnt(segment.through),
        ProfilePointToPnt(segment.end));
    if (!makeArc.IsDone()) {
        return false;
    }

    BRepBuilderAPI_MakeEdge edgeBuilder(makeArc.Value());
    if (!edgeBuilder.IsDone()) {
        return false;
    }

    edge = edgeBuilder.Edge();
    return !edge.IsNull();
}

bool TryBuildProfileWire(const OcctProfile2DSpec& profile, ExtrudedProfileWireBuild& build)
{
    BRepBuilderAPI_MakeWire wireBuilder;
    std::array<double, 2> currentPoint = profile.start;

    for (size_t segmentIndex = 0; segmentIndex < profile.segments.size(); ++segmentIndex) {
        const auto& segment = profile.segments[segmentIndex];
        const std::array<double, 2> segmentStart = segment.hasStart ? segment.start : currentPoint;
        TopoDS_Edge edge;
        bool edgeBuilt = false;

        if (segment.kind == "line") {
            edgeBuilt = TryMakeLineEdge(ProfilePointToPnt(segmentStart), ProfilePointToPnt(segment.end), edge);
        } else if (segment.kind == "arc_center") {
            edgeBuilt = TryMakeArcCenterEdge(segmentStart, segment, edge);
        } else if (segment.kind == "arc_3pt") {
            edgeBuilt = TryMakeArc3PointEdge(segmentStart, segment, edge);
        }

        if (!edgeBuilt) {
            return false;
        }

        wireBuilder.Add(edge);
        if (!wireBuilder.IsDone()) {
            return false;
        }

        ExtrudedProfileEdgeSource source;
        source.edge = edge;
        source.segmentIndex = static_cast<int>(segmentIndex);
        source.hasSegmentIndex = true;
        if (segment.hasId) {
            source.segmentId = segment.id;
            source.hasSegmentId = true;
        }
        if (segment.hasTag) {
            source.segmentTag = segment.tag;
            source.hasSegmentTag = true;
        }
        build.edgeSources.push_back(std::move(source));

        currentPoint = segment.end;
    }

    if (!wireBuilder.IsDone()) {
        return false;
    }

    build.wire = wireBuilder.Wire();
    return !build.wire.IsNull();
}

double ComputeApproximateProfileArea(const OcctProfile2DSpec& profile)
{
    std::vector<std::array<double, 2>> points;
    points.push_back(profile.start);
    for (const auto& segment : profile.segments) {
        points.push_back(segment.end);
    }

    if (points.size() < 3) {
        return 0.0;
    }

    double twiceArea = 0.0;
    for (size_t index = 0; index < points.size(); ++index) {
        const auto& current = points[index];
        const auto& next = points[(index + 1) % points.size()];
        twiceArea += current[0] * next[1] - next[0] * current[1];
    }

    return 0.5 * twiceArea;
}

std::string ShapeTypeToString(const TopAbs_ShapeEnum shapeType)
{
    switch (shapeType) {
        case TopAbs_COMPOUND:
            return "compound";
        case TopAbs_COMPSOLID:
            return "compsolid";
        case TopAbs_SOLID:
            return "solid";
        case TopAbs_SHELL:
            return "shell";
        case TopAbs_FACE:
            return "face";
        case TopAbs_WIRE:
            return "wire";
        case TopAbs_EDGE:
            return "edge";
        case TopAbs_VERTEX:
            return "vertex";
        case TopAbs_SHAPE:
            return "shape";
        default:
            return "unknown";
    }
}

OcctGeneratedShapeMeshValidation AnalyzeGeneratedShapeMesh(const OcctMeshData& mesh)
{
    constexpr double kMeshWeldTolerance = 1e-6;

    OcctGeneratedShapeMeshValidation validation;
    const size_t vertexCount = mesh.positions.size() / 3;
    std::vector<int> weldedVertexIds(vertexCount, -1);
    std::unordered_map<std::string, int> weldedIdsByKey;
    weldedIdsByKey.reserve(vertexCount);

    for (size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
        const size_t offset = vertexIndex * 3;
        const long long qx = std::llround(static_cast<double>(mesh.positions[offset]) / kMeshWeldTolerance);
        const long long qy = std::llround(static_cast<double>(mesh.positions[offset + 1]) / kMeshWeldTolerance);
        const long long qz = std::llround(static_cast<double>(mesh.positions[offset + 2]) / kMeshWeldTolerance);
        const std::string key = std::to_string(qx) + ":" + std::to_string(qy) + ":" + std::to_string(qz);

        auto [iterator, inserted] = weldedIdsByKey.emplace(key, static_cast<int>(weldedIdsByKey.size()));
        weldedVertexIds[vertexIndex] = iterator->second;
        if (inserted) {
            validation.weldedVertexCount += 1;
        }
    }

    std::unordered_map<std::string, int> edgeUseCounts;
    edgeUseCounts.reserve(mesh.indices.size());
    for (size_t indexOffset = 0; (indexOffset + 2) < mesh.indices.size(); indexOffset += 3) {
        const int triangle[3] = {
            weldedVertexIds[mesh.indices[indexOffset]],
            weldedVertexIds[mesh.indices[indexOffset + 1]],
            weldedVertexIds[mesh.indices[indexOffset + 2]]
        };

        if (triangle[0] == triangle[1]
            || triangle[1] == triangle[2]
            || triangle[2] == triangle[0]) {
            continue;
        }

        for (int edgeIndex = 0; edgeIndex < 3; ++edgeIndex) {
            const int left = triangle[edgeIndex];
            const int right = triangle[(edgeIndex + 1) % 3];
            const int minIndex = std::min(left, right);
            const int maxIndex = std::max(left, right);
            const std::string edgeKey = std::to_string(minIndex) + ":" + std::to_string(maxIndex);
            edgeUseCounts[edgeKey] += 1;
        }
    }

    for (const auto& [edgeKey, count] : edgeUseCounts) {
        (void)edgeKey;
        if (count == 1) {
            validation.boundaryEdgeCount += 1;
        } else if (count > 2) {
            validation.nonManifoldEdgeCount += 1;
        }
    }

    validation.isManifold = validation.nonManifoldEdgeCount == 0;
    validation.isWatertight = validation.boundaryEdgeCount == 0 && validation.isManifold;
    return validation;
}

OcctGeneratedShapeShapeValidation BuildGeneratedShapeValidation(
    const TopoDS_Shape& shape,
    const OcctMeshData& mesh)
{
    OcctGeneratedShapeShapeValidation validation;
    validation.exact.shapeType = ShapeTypeToString(shape.ShapeType());
    validation.exact.isValid = BRepCheck_Analyzer(shape).IsValid();

    TopTools_IndexedMapOfShape solidMap;
    TopTools_IndexedMapOfShape shellMap;
    TopTools_IndexedMapOfShape faceMap;
    TopTools_IndexedMapOfShape edgeMap;
    TopTools_IndexedMapOfShape vertexMap;
    TopExp::MapShapes(shape, TopAbs_SOLID, solidMap);
    TopExp::MapShapes(shape, TopAbs_SHELL, shellMap);
    TopExp::MapShapes(shape, TopAbs_FACE, faceMap);
    TopExp::MapShapes(shape, TopAbs_EDGE, edgeMap);
    TopExp::MapShapes(shape, TopAbs_VERTEX, vertexMap);

    validation.exact.solidCount = solidMap.Extent();
    validation.exact.shellCount = shellMap.Extent();
    validation.exact.faceCount = faceMap.Extent();
    validation.exact.edgeCount = edgeMap.Extent();
    validation.exact.vertexCount = vertexMap.Extent();
    validation.exact.isSolid = validation.exact.solidCount > 0;

    bool hasClosableTopology = false;
    bool allClosed = true;
    for (TopExp_Explorer explorer(shape, TopAbs_SOLID); explorer.More(); explorer.Next()) {
        hasClosableTopology = true;
        bool solidHasShell = false;
        for (TopExp_Explorer shellExplorer(explorer.Current(), TopAbs_SHELL); shellExplorer.More(); shellExplorer.Next()) {
            solidHasShell = true;
            if (!BRep_Tool::IsClosed(shellExplorer.Current())) {
                allClosed = false;
            }
        }
        if (!solidHasShell) {
            allClosed = false;
        }
    }
    for (TopExp_Explorer explorer(shape, TopAbs_SHELL, TopAbs_SOLID); explorer.More(); explorer.Next()) {
        hasClosableTopology = true;
        if (!BRep_Tool::IsClosed(explorer.Current())) {
            allClosed = false;
        }
    }
    validation.exact.isClosed = hasClosableTopology && allClosed;
    validation.mesh = AnalyzeGeneratedShapeMesh(mesh);
    return validation;
}

int FindFaceId(const TopoDS_Shape& shape, const TopTools_IndexedMapOfShape& faceMap)
{
    if (shape.IsNull() || shape.ShapeType() != TopAbs_FACE) {
        return 0;
    }

    int faceId = faceMap.FindIndex(shape);
    if (faceId > 0) {
        return faceId;
    }

    faceId = faceMap.FindIndex(shape.Oriented(TopAbs_FORWARD));
    if (faceId > 0) {
        return faceId;
    }

    return faceMap.FindIndex(shape.Oriented(TopAbs_REVERSED));
}

bool FaceBindingMatches(
    const OcctGeneratedShapeFaceBinding& binding,
    const OcctGeneratedShapeFaceBinding& candidate)
{
    return binding.geometryIndex == candidate.geometryIndex
        && binding.faceId == candidate.faceId
        && binding.systemRole == candidate.systemRole
        && binding.hasSegmentIndex == candidate.hasSegmentIndex
        && binding.segmentIndex == candidate.segmentIndex
        && binding.hasSegmentId == candidate.hasSegmentId
        && binding.segmentId == candidate.segmentId
        && binding.hasSegmentTag == candidate.hasSegmentTag
        && binding.segmentTag == candidate.segmentTag;
}

void TryAppendResolvedFaceBinding(
    std::vector<OcctGeneratedShapeFaceBinding>& bindings,
    const OcctGeneratedShapeFaceBinding& binding)
{
    const auto duplicate = std::find_if(
        bindings.begin(),
        bindings.end(),
        [&](const OcctGeneratedShapeFaceBinding& existing) {
            return FaceBindingMatches(existing, binding);
        });
    if (duplicate == bindings.end()) {
        bindings.push_back(binding);
    }
}

void TryAppendFaceBinding(
    std::vector<OcctGeneratedShapeFaceBinding>& bindings,
    int geometryIndex,
    int faceId,
    const std::string& systemRole,
    const ExtrudedProfileEdgeSource* source = nullptr)
{
    if (faceId <= 0) {
        return;
    }

    OcctGeneratedShapeFaceBinding binding;
    binding.geometryIndex = geometryIndex;
    binding.faceId = faceId;
    binding.systemRole = systemRole;
    if (source != nullptr && source->hasSegmentIndex) {
        binding.segmentIndex = source->segmentIndex;
        binding.hasSegmentIndex = true;
    }
    if (source != nullptr && source->hasSegmentId) {
        binding.segmentId = source->segmentId;
        binding.hasSegmentId = true;
    }
    if (source != nullptr && source->hasSegmentTag) {
        binding.segmentTag = source->segmentTag;
        binding.hasSegmentTag = true;
    }

    TryAppendResolvedFaceBinding(bindings, binding);
}

std::vector<int> CollectGeneratedFaceIds(
    const TopTools_ListOfShape& generatedShapes,
    const TopTools_IndexedMapOfShape& faceMap)
{
    std::vector<int> faceIds;
    for (TopTools_ListIteratorOfListOfShape it(generatedShapes); it.More(); it.Next()) {
        const int faceId = FindFaceId(it.Value(), faceMap);
        if (faceId > 0
            && std::find(faceIds.begin(), faceIds.end(), faceId) == faceIds.end()) {
            faceIds.push_back(faceId);
        }
    }
    return faceIds;
}

bool FaceIdCollectionContains(const std::vector<int>& faceIds, int faceId)
{
    return std::find(faceIds.begin(), faceIds.end(), faceId) != faceIds.end();
}

OcctGeneratedShapeFaceBinding BuildCollapsedWallBinding(
    int geometryIndex,
    int faceId,
    const std::vector<const ExtrudedProfileEdgeSource*>& sources)
{
    OcctGeneratedShapeFaceBinding binding;
    binding.geometryIndex = geometryIndex;
    binding.faceId = faceId;
    binding.systemRole = "wall";
    if (sources.size() == 1) {
        const ExtrudedProfileEdgeSource& source = *sources.front();
        if (source.hasSegmentIndex) {
            binding.segmentIndex = source.segmentIndex;
            binding.hasSegmentIndex = true;
        }
        if (source.hasSegmentId) {
            binding.segmentId = source.segmentId;
            binding.hasSegmentId = true;
        }
        if (source.hasSegmentTag) {
            binding.segmentTag = source.segmentTag;
            binding.hasSegmentTag = true;
        }
    }
    return binding;
}

std::vector<OcctGeneratedShapeFaceBinding> BuildExtrudedFaceBindings(
    const TopoDS_Shape& extrudedShape,
    const TopoDS_Face& profileFace,
    BRepPrimAPI_MakePrism& makePrism,
    const ExtrudedProfileWireBuild& wireBuild)
{
    std::vector<OcctGeneratedShapeFaceBinding> bindings;
    TopTools_IndexedMapOfShape faceMap;
    TopTools_IndexedMapOfShape profileEdgeMap;
    TopExp::MapShapes(extrudedShape, TopAbs_FACE, faceMap);
    TopExp::MapShapes(profileFace, TopAbs_EDGE, profileEdgeMap);

    const int startCapFaceId = FindFaceId(makePrism.FirstShape(profileFace), faceMap);
    const int endCapFaceId = FindFaceId(makePrism.LastShape(profileFace), faceMap);

    std::map<int, std::vector<const ExtrudedProfileEdgeSource*>> sourcesByFaceId;
    const int mappedEdgeCount = std::min(profileEdgeMap.Extent(), static_cast<int>(wireBuild.edgeSources.size()));
    for (int index = 0; index < mappedEdgeCount; ++index) {
        std::vector<int> faceIds = CollectGeneratedFaceIds(makePrism.Generated(profileEdgeMap(index + 1)), faceMap);
        const std::vector<int> directFaceIds = CollectGeneratedFaceIds(makePrism.Generated(wireBuild.edgeSources[static_cast<size_t>(index)].edge), faceMap);
        for (int faceId : directFaceIds) {
            if (!FaceIdCollectionContains(faceIds, faceId)) {
                faceIds.push_back(faceId);
            }
        }

        for (int faceId : faceIds) {
            auto& sources = sourcesByFaceId[faceId];
            const ExtrudedProfileEdgeSource* source = &wireBuild.edgeSources[static_cast<size_t>(index)];
            if (std::find(sources.begin(), sources.end(), source) == sources.end()) {
                sources.push_back(source);
            }
        }
    }

    for (const auto& [faceId, sources] : sourcesByFaceId) {
        if (faceId == startCapFaceId || faceId == endCapFaceId) {
            continue;
        }
        TryAppendResolvedFaceBinding(bindings, BuildCollapsedWallBinding(0, faceId, sources));
    }

    TryAppendFaceBinding(bindings, 0, startCapFaceId, "start_cap");
    TryAppendFaceBinding(bindings, 0, endCapFaceId, "end_cap");
    return bindings;
}

void ApplyGeneratedExtrudedFaceColors(
    OcctMeshData& mesh,
    const std::vector<OcctGeneratedShapeFaceBinding>& faceBindings)
{
    std::vector<bool> assigned(mesh.faces.size(), false);
    for (const auto& binding : faceBindings) {
        if (binding.faceId <= 0 || binding.faceId > static_cast<int>(mesh.faces.size())) {
            continue;
        }

        const size_t faceIndex = static_cast<size_t>(binding.faceId - 1);
        if (assigned[faceIndex]) {
            continue;
        }

        mesh.faces[faceIndex].color = ResolveExtrudedFaceColor(binding);
        assigned[faceIndex] = mesh.faces[faceIndex].color.isValid;
    }

    for (size_t faceIndex = 0; faceIndex < mesh.faces.size(); ++faceIndex) {
        if (!assigned[faceIndex]) {
            mesh.faces[faceIndex].color = NeutralShapeColor();
        }
    }
}

OcctGeneratedExtrudedShapeMetadata BuildExtrudedShapeMetadata(const OcctExtrudedShapeSpec& spec)
{
    OcctGeneratedExtrudedShapeMetadata metadata;
    metadata.version = spec.version;
    metadata.units = spec.units;
    metadata.depth = spec.depth;
    metadata.segmentCount = static_cast<int>(spec.profile.segments.size());

    for (size_t index = 0; index < spec.profile.segments.size(); ++index) {
        const auto& segment = spec.profile.segments[index];
        OcctGeneratedShapeSegmentDescriptor descriptor;
        descriptor.index = static_cast<int>(index);
        descriptor.kind = segment.kind;
        if (segment.hasId) {
            descriptor.id = segment.id;
            descriptor.hasId = true;
        }
        if (segment.hasTag) {
            descriptor.tag = segment.tag;
            descriptor.hasTag = true;
        }
        metadata.segments.push_back(descriptor);
    }

    return metadata;
}

void PopulateGeneratedScene(
    const OcctMeshData& mesh,
    const OcctExtrudedShapeSpec& spec,
    OcctSceneData& scene)
{
    scene.success = true;
    scene.sourceUnit = spec.units == "inch" ? "INCH" : "MM";
    scene.unitScaleToMeters = spec.units == "inch" ? 0.0254 : 0.001;

    scene.meshes.push_back(mesh);

    OcctNodeData rootNode;
    rootNode.id = "generated-extruded-shape-root";
    rootNode.name = "Generated Extruded Shape";
    rootNode.isAssembly = false;
    rootNode.transform = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    rootNode.meshIndices.push_back(0);

    scene.nodes.push_back(rootNode);
    scene.rootNodeIndices.push_back(0);
}

void FinalizeBuildFailure(OcctExtrudedShapeBuildResult& result, const std::string& error)
{
    result.success = false;
    result.error = error;
    result.scene.success = false;
    result.scene.error = error;
}

} // anonymous namespace

OcctExtrudedShapeValidationResult ValidateExtrudedShapeSpec(const val& jsSpec)
{
    OcctExtrudedShapeValidationResult result;
    if (!IsObject(jsSpec)) {
        AddDiagnostic(result, "invalid-spec", "Extruded shape spec must be an object.");
        return result;
    }

    ParseVersion(jsSpec, result);
    ParseUnits(jsSpec, result);
    ParseProfile(jsSpec, result);
    ParseExtrusion(jsSpec, result);

    result.ok = result.diagnostics.empty();
    result.hasSpec = result.ok;
    return result;
}

OcctExtrudedShapeBuildResult BuildExtrudedShape(const val& jsSpec, const val& jsOptions)
{
    OcctExtrudedShapeBuildResult result;
    const OcctExtrudedShapeValidationResult validation = ValidateExtrudedShapeSpec(jsSpec);
    if (!validation.ok) {
        result.diagnostics = validation.diagnostics;
        FinalizeBuildFailure(
            result,
            validation.diagnostics.empty()
                ? "Invalid extruded shape spec."
                : validation.diagnostics.front().message);
        return result;
    }

    const OcctExtrudedShapeSpec& spec = validation.spec;
    result.extrudedShape = BuildExtrudedShapeMetadata(spec);
    result.hasExtrudedShape = true;

    try {
        if (std::abs(ComputeApproximateProfileArea(spec.profile)) <= kPointTolerance) {
            AddDiagnostic(result, "build-failed", "Generated extruded shape profile encloses zero area.", "profile");
            FinalizeBuildFailure(result, "Generated extruded shape profile encloses zero area.");
            return result;
        }

        ExtrudedProfileWireBuild wireBuild;
        if (!TryBuildProfileWire(spec.profile, wireBuild)) {
            AddDiagnostic(result, "build-failed", "Failed to build a connected extruded shape profile wire.", "profile");
            FinalizeBuildFailure(result, "Failed to build a connected extruded shape profile wire.");
            return result;
        }

        BRepBuilderAPI_MakeFace faceBuilder(wireBuild.wire, Standard_True);
        if (!faceBuilder.IsDone()) {
            AddDiagnostic(result, "build-failed", "Failed to create a planar profile face from the extruded shape wire.", "profile");
            FinalizeBuildFailure(result, "Failed to create a planar profile face from the extruded shape wire.");
            return result;
        }

        const TopoDS_Face face = faceBuilder.Face();
        BRepPrimAPI_MakePrism makePrism(face, gp_Vec(0.0, 0.0, spec.depth), Standard_False, Standard_True);
        makePrism.Build();
        if (!makePrism.IsDone()) {
            AddDiagnostic(result, "build-failed", "OCCT failed to extrude the generated profile.", "extrusion");
            FinalizeBuildFailure(result, "OCCT failed to extrude the generated profile.");
            return result;
        }

        TopoDS_Shape extrudedShape = makePrism.Shape();
        if (extrudedShape.IsNull()) {
            AddDiagnostic(result, "build-failed", "OCCT produced a null extruded shape.", "extrusion");
            FinalizeBuildFailure(result, "OCCT produced a null extruded shape.");
            return result;
        }

        const std::vector<OcctGeneratedShapeFaceBinding> faceBindings =
            BuildExtrudedFaceBindings(extrudedShape, face, makePrism, wireBuild);

        ImportParams buildParams = ParseBuildParams(jsOptions, spec);
        if (!TriangulateShape(extrudedShape, buildParams)) {
            AddDiagnostic(result, "build-failed", "Failed to triangulate the generated extruded shape.", "mesh");
            FinalizeBuildFailure(result, "Failed to triangulate the generated extruded shape.");
            return result;
        }

        OcctMeshData mesh = ExtractMeshFromShape(extrudedShape);
        if (mesh.indices.empty() || mesh.faces.empty()) {
            AddDiagnostic(result, "build-failed", "Generated extruded shape triangulation produced no renderable faces.", "mesh");
            FinalizeBuildFailure(result, "Generated extruded shape triangulation produced no renderable faces.");
            return result;
        }

        mesh.name = "Generated Extruded Shape";
        mesh.color = NeutralShapeColor();
        ApplyGeneratedExtrudedFaceColors(mesh, faceBindings);
        result.extrudedShape.shapeValidation = BuildGeneratedShapeValidation(extrudedShape, mesh);
        result.extrudedShape.hasShapeValidation = true;
        result.extrudedShape.faceBindings = faceBindings;
        result.extrudedShape.hasStableFaceBindings = true;

        PopulateGeneratedScene(mesh, spec, result.scene);
        result.exactShape = extrudedShape;
        result.exactGeometryShapes = { extrudedShape };
        result.success = true;
        result.error.clear();
        return result;
    } catch (const Standard_Failure& failure) {
        const std::string message =
            failure.GetMessageString() != nullptr
                ? std::string(failure.GetMessageString())
                : std::string("OCCT threw an exception while building the generated extruded shape.");
        AddDiagnostic(result, "build-failed", message, "extrusion");
        FinalizeBuildFailure(result, message);
        return result;
    } catch (const std::exception& exception) {
        AddDiagnostic(result, "build-failed", exception.what(), "extrusion");
        FinalizeBuildFailure(result, exception.what());
        return result;
    }
}
