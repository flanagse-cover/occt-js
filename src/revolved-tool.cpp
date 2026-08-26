#include "revolved-tool.hpp"

#include "importer-utils.hpp"
#include "profile-2d.hpp"

#include <BRepCheck_Analyzer.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GC_MakeSegment.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <GeomAbs_SurfaceType.hxx>
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
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Cone.hxx>
#include <gp_Circ.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pln.hxx>
#include <gp_Sphere.hxx>
#include <gp_Torus.hxx>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

using emscripten::val;

namespace {

constexpr double kPointTolerance = 1e-9;
constexpr double kPi = 3.14159265358979323846;

enum class RevolvedFaceSupportKind {
    None,
    Plane,
    Cylinder,
    Cone,
    Sphere,
    Torus,
    Other
};

struct RevolvedFaceSupport {
    RevolvedFaceSupportKind kind = RevolvedFaceSupportKind::None;
    bool generatesFace = false;
    double zValue = 0.0;
    double zMin = 0.0;
    double zMax = 0.0;
    double radialMin = 0.0;
    double radialMax = 0.0;
    double radius = 0.0;
    double centerZ = 0.0;
    double majorRadius = 0.0;
    double minorRadius = 0.0;
    double apexZ = 0.0;
};

struct RevolvedFaceDescriptor {
    int faceId = 0;
    RevolvedFaceSupport support;
    bool isHorizontalPlane = false;
    bool isCapPlane = false;
};

struct RevolvedProfileEdgeSource {
    TopoDS_Edge edge;
    std::string systemRole;
    std::array<double, 2> start = { 0.0, 0.0 };
    std::array<double, 2> end = { 0.0, 0.0 };
    int segmentIndex = -1;
    bool hasSegmentIndex = false;
    std::string segmentId;
    bool hasSegmentId = false;
    std::string segmentTag;
    bool hasSegmentTag = false;
    RevolvedFaceSupport faceSupport;
};

struct RevolvedProfileWireBuild {
    TopoDS_Wire wire;
    std::vector<RevolvedProfileEdgeSource> edgeSources;
};

struct ResolvedRevolvedProfileSegment {
    OcctRevolvedToolSegment segment;
    std::string systemRole;
    int segmentIndex = -1;
    bool hasSegmentIndex = false;
};

struct ResolvedRevolvedProfileLoop {
    OcctProfile2DSpec profile;
    std::vector<ResolvedRevolvedProfileSegment> segments;
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

OcctRevolvedToolDiagnostic MakeDiagnostic(
    const std::string& code,
    const std::string& message,
    const std::string& path = std::string(),
    int segmentIndex = -1)
{
    OcctRevolvedToolDiagnostic diagnostic;
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
    OcctRevolvedToolValidationResult& result,
    const std::string& code,
    const std::string& message,
    const std::string& path = std::string(),
    int segmentIndex = -1)
{
    result.diagnostics.push_back(MakeDiagnostic(code, message, path, segmentIndex));
}

void AddDiagnostic(
    OcctRevolvedToolBuildResult& result,
    const std::string& code,
    const std::string& message,
    const std::string& path = std::string(),
    int segmentIndex = -1)
{
    result.diagnostics.push_back(MakeDiagnostic(code, message, path, segmentIndex));
}

bool TryGetOwnProperty(const val& jsObject, const char* key, val& out)
{
    if (!IsObject(jsObject) || !jsObject.hasOwnProperty(key)) {
        return false;
    }
    out = jsObject[key];
    return true;
}

bool TryParseStringProperty(
    const val& jsObject,
    const char* key,
    std::string& out,
    OcctRevolvedToolValidationResult& result,
    const std::string& path,
    int segmentIndex = -1)
{
    val jsValue = val::undefined();
    if (!TryGetOwnProperty(jsObject, key, jsValue)) {
        return false;
    }
    if (!IsString(jsValue)) {
        AddDiagnostic(result, "invalid-type", "Expected a string value.", path, segmentIndex);
        return false;
    }
    out = jsValue.as<std::string>();
    return true;
}

double SquaredDistance(const std::array<double, 2>& left, const std::array<double, 2>& right)
{
    const double dx = left[0] - right[0];
    const double dz = left[1] - right[1];
    return dx * dx + dz * dz;
}

bool PointsCoincident(const std::array<double, 2>& left, const std::array<double, 2>& right)
{
    return SquaredDistance(left, right) <= (kPointTolerance * kPointTolerance);
}

void ParseProfile(const val& jsProfile, OcctRevolvedToolValidationResult& result)
{
    if (!IsObject(jsProfile)) {
        AddDiagnostic(result, "missing-field", "Spec.profile must be an object.", "profile");
        return;
    }

    std::string plane = "XZ";
    TryParseStringProperty(jsProfile, "plane", plane, result, "profile.plane");
    if (plane != "XZ") {
        AddDiagnostic(result, "unsupported-plane", "Only the XZ revolve profile plane is supported in v1.", "profile.plane");
    }
    result.spec.profile.plane = plane;

    val jsClosure = val::undefined();
    if (!TryGetOwnProperty(jsProfile, "closure", jsClosure)) {
        AddDiagnostic(result, "missing-field", "Spec.profile.closure is required.", "profile.closure");
    } else if (!IsString(jsClosure)) {
        AddDiagnostic(result, "invalid-type", "Expected a string value.", "profile.closure");
    } else {
        result.spec.profile.closure = jsClosure.as<std::string>();
        result.spec.profile.hasClosure = true;
        if (result.spec.profile.closure != "explicit" && result.spec.profile.closure != "auto_axis") {
            AddDiagnostic(result, "invalid-closure", "Supported closure values are explicit and auto_axis.", "profile.closure");
        }
    }

    OcctProfile2DValidationOptions profileOptions;
    profileOptions.rootPath = "profile";
    profileOptions.requireClosed = result.spec.profile.closure != "auto_axis";
    const OcctProfile2DValidationResult profileResult = ParseAndValidateProfile2DSpec(jsProfile, profileOptions);

    result.spec.profile.hasStart = profileResult.spec.hasStart;
    result.spec.profile.start = profileResult.spec.start;
    result.spec.profile.segments = profileResult.spec.segments;

    for (const auto& diagnostic : profileResult.diagnostics) {
        result.diagnostics.push_back(diagnostic);
    }

    if (result.spec.profile.hasStart && result.spec.profile.start[0] < 0.0) {
        AddDiagnostic(result, "negative-radius", "Radius coordinates must be >= 0.", "profile.start[0]");
    }
    for (size_t segmentIndex = 0; segmentIndex < result.spec.profile.segments.size(); ++segmentIndex) {
        const OcctRevolvedToolSegment& segment = result.spec.profile.segments[segmentIndex];
        const std::string segmentPath = "profile.segments[" + std::to_string(segmentIndex) + "]";
        if (segment.hasEnd && segment.end[0] < 0.0) {
            AddDiagnostic(result, "negative-radius", "Radius coordinates must be >= 0.", segmentPath + ".end[0]", static_cast<int>(segmentIndex));
        }
        if (segment.hasCenter && segment.center[0] < 0.0) {
            AddDiagnostic(result, "negative-radius", "Radius coordinates must be >= 0.", segmentPath + ".center[0]", static_cast<int>(segmentIndex));
        }
        if (segment.hasThrough && segment.through[0] < 0.0) {
            AddDiagnostic(result, "negative-radius", "Radius coordinates must be >= 0.", segmentPath + ".through[0]", static_cast<int>(segmentIndex));
        }
    }
}

void ParseUnits(const val& jsSpec, OcctRevolvedToolValidationResult& result)
{
    val jsUnits = val::undefined();
    if (!TryGetOwnProperty(jsSpec, "units", jsUnits)) {
        AddDiagnostic(result, "missing-field", "Spec.units is required.", "units");
        return;
    }
    if (!IsString(jsUnits)) {
        AddDiagnostic(result, "invalid-type", "Expected a string value.", "units");
        return;
    }
    result.spec.units = jsUnits.as<std::string>();

    if (result.spec.units != "mm" && result.spec.units != "inch") {
        AddDiagnostic(result, "unsupported-unit", "Supported units are mm and inch.", "units");
    }
}

void ParseVersion(const val& jsSpec, OcctRevolvedToolValidationResult& result)
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
        AddDiagnostic(result, "unsupported-version", "Only revolved shape spec version 1 is supported.", "version");
        return;
    }

    result.spec.version = static_cast<int>(version);
}

void ParseRevolve(const val& jsSpec, OcctRevolvedToolValidationResult& result)
{
    val jsRevolve = val::undefined();
    if (!TryGetOwnProperty(jsSpec, "revolve", jsRevolve)) {
        return;
    }
    if (!IsObject(jsRevolve)) {
        AddDiagnostic(result, "invalid-type", "Spec.revolve must be an object.", "revolve");
        return;
    }

    val jsAngleDeg = val::undefined();
    if (!TryGetOwnProperty(jsRevolve, "angleDeg", jsAngleDeg)) {
        return;
    }
    if (!IsNumber(jsAngleDeg)) {
        AddDiagnostic(result, "invalid-type", "Spec.revolve.angleDeg must be a number.", "revolve.angleDeg");
        return;
    }

    const double angleDeg = jsAngleDeg.as<double>();
    result.spec.angleDeg = angleDeg;
    if (!std::isfinite(angleDeg) || angleDeg <= 0.0 || angleDeg > 360.0) {
        AddDiagnostic(result, "invalid-revolve-angle", "Revolve angle must be > 0 and <= 360 degrees.", "revolve.angleDeg");
    }
}

double ProfileRadius(const gp_Pnt& point)
{
    return std::sqrt((point.X() * point.X()) + (point.Y() * point.Y()));
}

double ScaledTolerance(double left, double right, double multiplier = 1e-6, double floor = 1e-6)
{
    return std::max({ floor, std::abs(left) * multiplier, std::abs(right) * multiplier });
}

bool NearlyEqual(double left, double right, double multiplier = 1e-6, double floor = 1e-6)
{
    return std::abs(left - right) <= ScaledTolerance(left, right, multiplier, floor);
}

double NormalizeAnglePositive(double angle)
{
    angle = std::fmod(angle, 2.0 * kPi);
    if (angle < 0.0) {
        angle += 2.0 * kPi;
    }
    return angle;
}

double PositiveAngularDistance(double start, double end)
{
    return NormalizeAnglePositive(end - start);
}

bool AngleLiesOnArcSweep(double startAngle, double throughAngle, double endAngle, double testAngle)
{
    const double ccwSpan = PositiveAngularDistance(startAngle, endAngle);
    const double ccwThrough = PositiveAngularDistance(startAngle, throughAngle);
    const bool isCcw = ccwThrough <= (ccwSpan + 1e-9);

    if (isCcw) {
        return PositiveAngularDistance(startAngle, testAngle) <= (ccwSpan + 1e-9);
    }

    return PositiveAngularDistance(testAngle, startAngle) <= (PositiveAngularDistance(endAngle, startAngle) + 1e-9);
}

bool TryComputeCircleFromThreePoints(
    const std::array<double, 2>& pointA,
    const std::array<double, 2>& pointB,
    const std::array<double, 2>& pointC,
    std::array<double, 2>& center,
    double& radius)
{
    const double ax = pointA[0];
    const double az = pointA[1];
    const double bx = pointB[0];
    const double bz = pointB[1];
    const double cx = pointC[0];
    const double cz = pointC[1];

    const double determinant = 2.0 * ((ax * (bz - cz)) + (bx * (cz - az)) + (cx * (az - bz)));
    if (std::abs(determinant) <= kPointTolerance) {
        return false;
    }

    const double aSquared = (ax * ax) + (az * az);
    const double bSquared = (bx * bx) + (bz * bz);
    const double cSquared = (cx * cx) + (cz * cz);

    center[0] = ((aSquared * (bz - cz)) + (bSquared * (cz - az)) + (cSquared * (az - bz))) / determinant;
    center[1] = ((aSquared * (cx - bx)) + (bSquared * (ax - cx)) + (cSquared * (bx - ax))) / determinant;
    radius = std::sqrt(SquaredDistance(pointA, center));
    return std::isfinite(radius) && radius > kPointTolerance;
}

void IncludeProfilePointInSupport(RevolvedFaceSupport& support, const std::array<double, 2>& point, bool& hasBounds)
{
    const double radial = std::max(0.0, point[0]);
    const double z = point[1];
    if (!hasBounds) {
        support.radialMin = radial;
        support.radialMax = radial;
        support.zMin = z;
        support.zMax = z;
        hasBounds = true;
        return;
    }

    support.radialMin = std::min(support.radialMin, radial);
    support.radialMax = std::max(support.radialMax, radial);
    support.zMin = std::min(support.zMin, z);
    support.zMax = std::max(support.zMax, z);
}

RevolvedFaceSupport BuildLineFaceSupport(const std::array<double, 2>& start, const std::array<double, 2>& end)
{
    RevolvedFaceSupport support;
    const double startRadius = start[0];
    const double endRadius = end[0];
    const double startZ = start[1];
    const double endZ = end[1];

    if (std::abs(startZ - endZ) <= kPointTolerance) {
        if (std::max(startRadius, endRadius) <= kPointTolerance) {
            return support;
        }
        support.kind = RevolvedFaceSupportKind::Plane;
        support.generatesFace = true;
        support.zValue = startZ;
        support.radialMin = std::max(0.0, std::min(startRadius, endRadius));
        support.radialMax = std::max(startRadius, endRadius);
        support.zMin = startZ;
        support.zMax = startZ;
        return support;
    }

    if (std::abs(startRadius - endRadius) <= kPointTolerance) {
        if (std::max(startRadius, endRadius) <= kPointTolerance) {
            return support;
        }
        support.kind = RevolvedFaceSupportKind::Cylinder;
        support.generatesFace = true;
        support.radius = std::max(startRadius, endRadius);
        support.radialMin = support.radius;
        support.radialMax = support.radius;
        support.zMin = std::min(startZ, endZ);
        support.zMax = std::max(startZ, endZ);
        return support;
    }

    support.kind = RevolvedFaceSupportKind::Cone;
    support.generatesFace = true;
    support.radialMin = std::max(0.0, std::min(startRadius, endRadius));
    support.radialMax = std::max(startRadius, endRadius);
    support.zMin = std::min(startZ, endZ);
    support.zMax = std::max(startZ, endZ);
    support.apexZ = startZ - (startRadius * (endZ - startZ) / (endRadius - startRadius));
    return support;
}

RevolvedFaceSupport BuildArcFaceSupport(
    const std::array<double, 2>& start,
    const OcctRevolvedToolSegment& segment)
{
    RevolvedFaceSupport support;
    std::array<double, 2> center = { 0.0, 0.0 };
    double radius = 0.0;
    double startAngle = 0.0;
    double throughAngle = 0.0;
    double endAngle = 0.0;

    if (segment.kind == "arc_center") {
        center = segment.center;
        radius = std::sqrt(SquaredDistance(start, center));
        startAngle = std::atan2(start[1] - center[1], start[0] - center[0]);
        endAngle = std::atan2(segment.end[1] - center[1], segment.end[0] - center[0]);

        double delta = std::remainder(endAngle - startAngle, 2.0 * kPi);
        const double cross2d =
            (start[0] - center[0]) * (segment.end[1] - center[1]) -
            (start[1] - center[1]) * (segment.end[0] - center[0]);

        if (std::abs(std::abs(delta) - kPi) <= 1e-12) {
            delta = cross2d >= 0.0 ? kPi : -kPi;
        } else if (cross2d > 0.0 && delta < 0.0) {
            delta += 2.0 * kPi;
        } else if (cross2d < 0.0 && delta > 0.0) {
            delta -= 2.0 * kPi;
        }
        throughAngle = startAngle + (delta * 0.5);
    } else if (segment.kind == "arc_3pt") {
        if (!TryComputeCircleFromThreePoints(start, segment.through, segment.end, center, radius)) {
            return support;
        }
        startAngle = std::atan2(start[1] - center[1], start[0] - center[0]);
        throughAngle = std::atan2(segment.through[1] - center[1], segment.through[0] - center[0]);
        endAngle = std::atan2(segment.end[1] - center[1], segment.end[0] - center[0]);
    } else {
        return support;
    }

    support.generatesFace = true;
    if (std::abs(center[0]) <= kPointTolerance) {
        support.kind = RevolvedFaceSupportKind::Sphere;
        support.radius = radius;
        support.centerZ = center[1];
    } else {
        support.kind = RevolvedFaceSupportKind::Torus;
        support.majorRadius = center[0];
        support.minorRadius = radius;
        support.centerZ = center[1];
    }

    bool hasBounds = false;
    const std::array<double, 4> cardinalAngles = { 0.0, 0.5 * kPi, kPi, 1.5 * kPi };
    auto includeAngle = [&](double angle) {
        const std::array<double, 2> point = {
            center[0] + (radius * std::cos(angle)),
            center[1] + (radius * std::sin(angle))
        };
        IncludeProfilePointInSupport(support, point, hasBounds);
    };

    includeAngle(startAngle);
    includeAngle(endAngle);
    for (double angle : cardinalAngles) {
        if (AngleLiesOnArcSweep(startAngle, throughAngle, endAngle, angle)) {
            includeAngle(angle);
        }
    }

    return support;
}

RevolvedFaceSupport BuildExpectedFaceSupport(
    const std::array<double, 2>& start,
    const std::array<double, 2>& end,
    const OcctRevolvedToolSegment* segment = nullptr)
{
    if (segment == nullptr || segment->kind == "line") {
        return BuildLineFaceSupport(start, end);
    }

    return BuildArcFaceSupport(start, *segment);
}

void AppendProfileSample(std::vector<std::array<double, 2>>& samples, const gp_Pnt& point)
{
    samples.push_back({ std::max(0.0, ProfileRadius(point)), point.Z() });
}

std::vector<std::array<double, 2>> CollectFaceBoundaryProfileSamples(const TopoDS_Face& face)
{
    std::vector<std::array<double, 2>> samples;

    for (TopExp_Explorer explorer(face, TopAbs_VERTEX); explorer.More(); explorer.Next()) {
        AppendProfileSample(samples, BRep_Tool::Pnt(TopoDS::Vertex(explorer.Current())));
    }

    for (TopExp_Explorer explorer(face, TopAbs_EDGE); explorer.More(); explorer.Next()) {
        const TopoDS_Edge edge = TopoDS::Edge(explorer.Current());
        if (edge.IsNull() || BRep_Tool::Degenerated(edge)) {
            continue;
        }

        BRepAdaptor_Curve curve(edge);
        const double first = curve.FirstParameter();
        const double last = curve.LastParameter();
        if (!std::isfinite(first) || !std::isfinite(last)) {
            continue;
        }

        AppendProfileSample(samples, curve.Value(first));
        AppendProfileSample(samples, curve.Value(0.5 * (first + last)));
        AppendProfileSample(samples, curve.Value(last));
    }

    return samples;
}

void ApplyProfileBoundsToSupport(const std::vector<std::array<double, 2>>& samples, RevolvedFaceSupport& support)
{
    bool hasBounds = false;
    for (const auto& sample : samples) {
        IncludeProfilePointInSupport(support, sample, hasBounds);
    }
}

bool FaceContainsAxisPoint(const TopoDS_Face& face, double zValue)
{
    const BRepClass_FaceClassifier classifier(face, gp_Pnt(0.0, 0.0, zValue), 1e-7);
    const TopAbs_State state = classifier.State();
    return state == TopAbs_IN || state == TopAbs_ON;
}

RevolvedFaceDescriptor DescribeRevolvedFace(const TopoDS_Face& face, int faceId)
{
    RevolvedFaceDescriptor descriptor;
    descriptor.faceId = faceId;

    BRepAdaptor_Surface surface(face, false);
    const GeomAbs_SurfaceType surfaceType = surface.GetType();
    std::vector<std::array<double, 2>> samples = CollectFaceBoundaryProfileSamples(face);

    if (surfaceType != GeomAbs_Plane) {
        double uFirst = 0.0;
        double uLast = 0.0;
        double vFirst = 0.0;
        double vLast = 0.0;
        BRepTools::UVBounds(face, uFirst, uLast, vFirst, vLast);
        for (double uFactor : { 0.0, 0.5, 1.0 }) {
            for (double vFactor : { 0.0, 0.5, 1.0 }) {
                AppendProfileSample(
                    samples,
                    surface.Value(
                        uFirst + ((uLast - uFirst) * uFactor),
                        vFirst + ((vLast - vFirst) * vFactor)));
            }
        }
    }

    switch (surfaceType) {
        case GeomAbs_Plane: {
            const gp_Pln plane = surface.Plane();
            const gp_Dir normal = plane.Axis().Direction();
            descriptor.isHorizontalPlane = std::abs(std::abs(normal.Z()) - 1.0) <= 1e-6;
            descriptor.isCapPlane = !descriptor.isHorizontalPlane;
            if (!descriptor.isHorizontalPlane) {
                descriptor.support.kind = RevolvedFaceSupportKind::Other;
                break;
            }

            descriptor.support.kind = RevolvedFaceSupportKind::Plane;
            descriptor.support.generatesFace = true;
            descriptor.support.zValue = plane.Location().Z();
            ApplyProfileBoundsToSupport(samples, descriptor.support);
            if (FaceContainsAxisPoint(face, descriptor.support.zValue)) {
                descriptor.support.radialMin = 0.0;
            }
            descriptor.support.zMin = descriptor.support.zValue;
            descriptor.support.zMax = descriptor.support.zValue;
            break;
        }
        case GeomAbs_Cylinder: {
            const gp_Cylinder cylinder = surface.Cylinder();
            if (std::abs(std::abs(cylinder.Axis().Direction().Z()) - 1.0) > 1e-6) {
                descriptor.support.kind = RevolvedFaceSupportKind::Other;
                break;
            }

            descriptor.support.kind = RevolvedFaceSupportKind::Cylinder;
            descriptor.support.generatesFace = true;
            descriptor.support.radius = cylinder.Radius();
            ApplyProfileBoundsToSupport(samples, descriptor.support);
            break;
        }
        case GeomAbs_Cone: {
            const gp_Cone cone = surface.Cone();
            if (std::abs(std::abs(cone.Axis().Direction().Z()) - 1.0) > 1e-6) {
                descriptor.support.kind = RevolvedFaceSupportKind::Other;
                break;
            }

            descriptor.support.kind = RevolvedFaceSupportKind::Cone;
            descriptor.support.generatesFace = true;
            descriptor.support.apexZ = cone.Apex().Z();
            ApplyProfileBoundsToSupport(samples, descriptor.support);
            break;
        }
        case GeomAbs_Sphere: {
            const gp_Sphere sphere = surface.Sphere();
            descriptor.support.kind = RevolvedFaceSupportKind::Sphere;
            descriptor.support.generatesFace = true;
            descriptor.support.radius = sphere.Radius();
            descriptor.support.centerZ = sphere.Location().Z();
            ApplyProfileBoundsToSupport(samples, descriptor.support);
            break;
        }
        case GeomAbs_Torus: {
            const gp_Torus torus = surface.Torus();
            if (std::abs(std::abs(torus.Axis().Direction().Z()) - 1.0) > 1e-6) {
                descriptor.support.kind = RevolvedFaceSupportKind::Other;
                break;
            }

            descriptor.support.kind = RevolvedFaceSupportKind::Torus;
            descriptor.support.generatesFace = true;
            descriptor.support.majorRadius = torus.MajorRadius();
            descriptor.support.minorRadius = torus.MinorRadius();
            descriptor.support.centerZ = torus.Location().Z();
            ApplyProfileBoundsToSupport(samples, descriptor.support);
            break;
        }
        default:
            descriptor.support.kind = RevolvedFaceSupportKind::Other;
            break;
    }

    return descriptor;
}

bool RangeCovers(double outerMin, double outerMax, double innerMin, double innerMax)
{
    const double minTolerance = ScaledTolerance(outerMin, innerMin);
    const double maxTolerance = ScaledTolerance(outerMax, innerMax);
    return outerMin <= (innerMin + minTolerance) && outerMax >= (innerMax - maxTolerance);
}

double CoverageScore(double outerMin, double outerMax, double innerMin, double innerMax)
{
    return std::abs(outerMin - innerMin) + std::abs(outerMax - innerMax);
}

bool TryMatchSourceToFace(
    const RevolvedProfileEdgeSource& source,
    const RevolvedFaceDescriptor& face,
    double& score)
{
    score = 0.0;
    if (!source.faceSupport.generatesFace || !face.support.generatesFace) {
        return false;
    }

    if (source.faceSupport.kind != face.support.kind) {
        return false;
    }

    switch (source.faceSupport.kind) {
        case RevolvedFaceSupportKind::Plane:
            if (!face.isHorizontalPlane
                || !NearlyEqual(face.support.zValue, source.faceSupport.zValue)
                || !RangeCovers(face.support.radialMin, face.support.radialMax, source.faceSupport.radialMin, source.faceSupport.radialMax)) {
                return false;
            }
            score = CoverageScore(
                face.support.radialMin,
                face.support.radialMax,
                source.faceSupport.radialMin,
                source.faceSupport.radialMax);
            return true;

        case RevolvedFaceSupportKind::Cylinder:
            if (!NearlyEqual(face.support.radius, source.faceSupport.radius)
                || !RangeCovers(face.support.zMin, face.support.zMax, source.faceSupport.zMin, source.faceSupport.zMax)) {
                return false;
            }
            score = CoverageScore(face.support.zMin, face.support.zMax, source.faceSupport.zMin, source.faceSupport.zMax);
            return true;

        case RevolvedFaceSupportKind::Cone:
            if (!NearlyEqual(face.support.apexZ, source.faceSupport.apexZ)
                || !RangeCovers(face.support.zMin, face.support.zMax, source.faceSupport.zMin, source.faceSupport.zMax)
                || !RangeCovers(face.support.radialMin, face.support.radialMax, source.faceSupport.radialMin, source.faceSupport.radialMax)) {
                return false;
            }
            score =
                CoverageScore(face.support.zMin, face.support.zMax, source.faceSupport.zMin, source.faceSupport.zMax) +
                CoverageScore(face.support.radialMin, face.support.radialMax, source.faceSupport.radialMin, source.faceSupport.radialMax);
            return true;

        case RevolvedFaceSupportKind::Sphere:
            if (!NearlyEqual(face.support.radius, source.faceSupport.radius)
                || !NearlyEqual(face.support.centerZ, source.faceSupport.centerZ)
                || !RangeCovers(face.support.zMin, face.support.zMax, source.faceSupport.zMin, source.faceSupport.zMax)
                || !RangeCovers(face.support.radialMin, face.support.radialMax, source.faceSupport.radialMin, source.faceSupport.radialMax)) {
                return false;
            }
            score =
                CoverageScore(face.support.zMin, face.support.zMax, source.faceSupport.zMin, source.faceSupport.zMax) +
                CoverageScore(face.support.radialMin, face.support.radialMax, source.faceSupport.radialMin, source.faceSupport.radialMax);
            return true;

        case RevolvedFaceSupportKind::Torus:
            if (!NearlyEqual(face.support.majorRadius, source.faceSupport.majorRadius)
                || !NearlyEqual(face.support.minorRadius, source.faceSupport.minorRadius)
                || !NearlyEqual(face.support.centerZ, source.faceSupport.centerZ)
                || !RangeCovers(face.support.zMin, face.support.zMax, source.faceSupport.zMin, source.faceSupport.zMax)
                || !RangeCovers(face.support.radialMin, face.support.radialMax, source.faceSupport.radialMin, source.faceSupport.radialMax)) {
                return false;
            }
            score =
                CoverageScore(face.support.zMin, face.support.zMax, source.faceSupport.zMin, source.faceSupport.zMax) +
                CoverageScore(face.support.radialMin, face.support.radialMax, source.faceSupport.radialMin, source.faceSupport.radialMax);
            return true;

        default:
            return false;
    }
}

gp_Pnt ProfilePointToPnt(const std::array<double, 2>& point)
{
    return gp_Pnt(point[0], 0.0, point[1]);
}

OcctColor NeutralToolColor()
{
    return OcctColor(0.78, 0.80, 0.84);
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

OcctColor HashedProfileColor(const std::string& semanticKey)
{
    static const std::array<OcctColor, 6> kProfilePalette = {
        MakeSemanticColor(0.86, 0.55, 0.22),
        MakeSemanticColor(0.14, 0.61, 0.72),
        MakeSemanticColor(0.79, 0.42, 0.24),
        MakeSemanticColor(0.39, 0.58, 0.49),
        MakeSemanticColor(0.50, 0.58, 0.70),
        MakeSemanticColor(0.61, 0.44, 0.69),
    };

    const uint32_t hash = StableHash32(semanticKey.empty() ? "profile" : semanticKey);
    return kProfilePalette[hash % kProfilePalette.size()];
}

OcctColor ResolveProfileAppearanceColor(const OcctGeneratedToolFaceBinding& binding)
{
    if (binding.hasSegmentTag) {
        const std::string tag = ToLowerAscii(binding.segmentTag);
        if (tag == "tip") {
            return MakeSemanticColor(0.86, 0.55, 0.22);
        }
        if (tag == "cutting") {
            return MakeSemanticColor(0.14, 0.61, 0.72);
        }
        if (tag == "corner") {
            return MakeSemanticColor(0.79, 0.42, 0.24);
        }
        if (tag == "neck") {
            return MakeSemanticColor(0.39, 0.58, 0.49);
        }
        if (tag == "shank") {
            return MakeSemanticColor(0.50, 0.58, 0.70);
        }
        return HashedProfileColor(tag);
    }

    if (binding.hasSegmentId) {
        return HashedProfileColor(ToLowerAscii(binding.segmentId));
    }

    if (binding.hasSegmentIndex) {
        return HashedProfileColor("segment:" + std::to_string(binding.segmentIndex));
    }

    return MakeSemanticColor(0.63, 0.66, 0.71);
}

OcctColor ResolveGeneratedFaceColor(const OcctGeneratedToolFaceBinding& binding)
{
    const std::string role = ToLowerAscii(binding.systemRole);
    if (role == "closure") {
        return MakeSemanticColor(0.45, 0.49, 0.54);
    }
    if (role == "axis") {
        return MakeSemanticColor(0.29, 0.32, 0.37);
    }
    if (role == "start_cap") {
        return MakeSemanticColor(0.89, 0.67, 0.25);
    }
    if (role == "end_cap") {
        return MakeSemanticColor(0.69, 0.39, 0.24);
    }
    if (role == "degenerated") {
        return MakeSemanticColor(0.65, 0.27, 0.58);
    }

    return ResolveProfileAppearanceColor(binding);
}

ImportParams ParseBuildParams(const val& jsOptions, const OcctRevolvedToolSpec& spec)
{
    ImportParams params;
    params.linearUnit = spec.units == "inch"
        ? ImportParams::LinearUnit::Inch
        : ImportParams::LinearUnit::Millimeter;

    if (!IsObject(jsOptions)) {
        return params;
    }

    val jsLinearDeflectionType = val::undefined();
    if (TryGetOwnProperty(jsOptions, "linearDeflectionType", jsLinearDeflectionType)
        && IsString(jsLinearDeflectionType)) {
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
    const OcctRevolvedToolSegment& segment,
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
    const OcctRevolvedToolSegment& segment,
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

bool TryAppendEdge(
    BRepBuilderAPI_MakeWire& wireBuilder,
    const TopoDS_Edge& edge)
{
    wireBuilder.Add(edge);
    return wireBuilder.IsDone();
}

bool IsAxisPoint(const std::array<double, 2>& point)
{
    return std::abs(point[0]) <= kPointTolerance;
}

std::string InferSegmentSystemRole(
    const std::array<double, 2>& start,
    const std::array<double, 2>& end,
    const OcctRevolvedToolSegment* segment = nullptr)
{
    if (segment != nullptr && segment->hasTag) {
        const std::string tag = ToLowerAscii(segment->tag);
        if (tag == "closure") {
            return "closure";
        }
        if (tag == "axis") {
            return "axis";
        }
        return "profile";
    }

    const bool startOnAxis = IsAxisPoint(start);
    const bool endOnAxis = IsAxisPoint(end);
    if (startOnAxis && endOnAxis) {
        return "axis";
    }
    if (startOnAxis || endOnAxis) {
        return "closure";
    }
    return "profile";
}

bool TryAppendTrackedEdge(
    BRepBuilderAPI_MakeWire& wireBuilder,
    const TopoDS_Edge& edge,
    const std::array<double, 2>& start,
    const std::array<double, 2>& end,
    const std::string& systemRole,
    std::vector<RevolvedProfileEdgeSource>& edgeSources,
    const OcctRevolvedToolSegment* segment = nullptr,
    int segmentIndex = -1)
{
    if (!TryAppendEdge(wireBuilder, edge)) {
        return false;
    }

    RevolvedProfileEdgeSource source;
    source.edge = edge;
    source.systemRole = systemRole;
    source.start = start;
    source.end = end;
    source.faceSupport = BuildExpectedFaceSupport(start, end, segment);
    if (segment != nullptr && segmentIndex >= 0) {
        source.segmentIndex = segmentIndex;
        source.hasSegmentIndex = true;
        if (segment->hasId) {
            source.segmentId = segment->id;
            source.hasSegmentId = true;
        }
        if (segment->hasTag) {
            source.segmentTag = segment->tag;
            source.hasSegmentTag = true;
        }
    }

    edgeSources.push_back(std::move(source));
    return true;
}

void AppendResolvedProfileSegment(
    ResolvedRevolvedProfileLoop& loop,
    const OcctRevolvedToolSegment& segment,
    const std::string& systemRole,
    int segmentIndex = -1)
{
    ResolvedRevolvedProfileSegment resolved;
    resolved.segment = segment;
    resolved.systemRole = systemRole;
    if (segmentIndex >= 0) {
        resolved.segmentIndex = segmentIndex;
        resolved.hasSegmentIndex = true;
    }
    loop.profile.segments.push_back(resolved.segment);
    loop.segments.push_back(std::move(resolved));
}

ResolvedRevolvedProfileLoop ResolveRevolvedProfileLoop(const OcctRevolvedToolSpec& spec)
{
    ResolvedRevolvedProfileLoop loop;
    loop.profile.version = spec.version;
    loop.profile.start = spec.profile.start;
    loop.profile.hasStart = spec.profile.hasStart;

    std::array<double, 2> currentPoint = spec.profile.start;
    for (size_t segmentIndex = 0; segmentIndex < spec.profile.segments.size(); ++segmentIndex) {
        OcctRevolvedToolSegment resolvedSegment = spec.profile.segments[segmentIndex];
        if (!resolvedSegment.hasStart) {
            resolvedSegment.start = currentPoint;
            resolvedSegment.hasStart = true;
        }
        AppendResolvedProfileSegment(
            loop,
            resolvedSegment,
            InferSegmentSystemRole(resolvedSegment.start, resolvedSegment.end, &resolvedSegment),
            static_cast<int>(segmentIndex));
        currentPoint = resolvedSegment.end;
    }

    if (spec.profile.closure == "auto_axis") {
        const std::array<double, 2> axisCurrent = { 0.0, currentPoint[1] };
        const std::array<double, 2> axisStart = { 0.0, spec.profile.start[1] };

        auto appendSyntheticLine = [&](const std::array<double, 2>& start, const std::array<double, 2>& end, const std::string& systemRole) {
            if (PointsCoincident(start, end)) {
                return;
            }

            OcctRevolvedToolSegment syntheticSegment;
            syntheticSegment.kind = "line";
            syntheticSegment.start = start;
            syntheticSegment.hasStart = true;
            syntheticSegment.end = end;
            syntheticSegment.hasEnd = true;
            AppendResolvedProfileSegment(loop, syntheticSegment, systemRole);
        };

        appendSyntheticLine(currentPoint, axisCurrent, "closure");
        appendSyntheticLine(axisCurrent, axisStart, "axis");
        appendSyntheticLine(axisStart, spec.profile.start, "closure");
    }

    return loop;
}

bool TryResolveValidatedProfileLoop(
    const OcctRevolvedToolSpec& spec,
    ResolvedRevolvedProfileLoop& loop,
    OcctRevolvedToolBuildResult& result)
{
    loop = ResolveRevolvedProfileLoop(spec);

    OcctProfile2DValidationOptions options;
    options.rootPath = "profile";
    options.requireClosed = true;
    const OcctProfile2DValidationResult normalized = ValidateNormalizedProfile2DSpec(loop.profile, options);
    if (!normalized.ok) {
        for (const auto& diagnostic : normalized.diagnostics) {
            result.diagnostics.push_back(diagnostic);
        }
        return false;
    }

    loop.profile = normalized.spec;
    const size_t syncedSegmentCount = std::min(loop.segments.size(), loop.profile.segments.size());
    for (size_t index = 0; index < syncedSegmentCount; ++index) {
        loop.segments[index].segment = loop.profile.segments[index];
    }
    return true;
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

bool TryBuildProfileWire(const ResolvedRevolvedProfileLoop& loop, RevolvedProfileWireBuild& build)
{
    BRepBuilderAPI_MakeWire wireBuilder;
    std::array<double, 2> currentPoint = loop.profile.start;

    for (const auto& resolvedSegment : loop.segments) {
        const auto& segment = resolvedSegment.segment;
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

        if (!edgeBuilt
            || !TryAppendTrackedEdge(
                wireBuilder,
                edge,
                segmentStart,
                segment.end,
                resolvedSegment.systemRole,
                build.edgeSources,
                &segment,
                resolvedSegment.hasSegmentIndex ? resolvedSegment.segmentIndex : -1)) {
            return false;
        }

        currentPoint = segment.end;
    }

    if (!wireBuilder.IsDone()) {
        return false;
    }

    build.wire = wireBuilder.Wire();
    return !build.wire.IsNull();
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
    const OcctGeneratedToolFaceBinding& binding,
    const OcctGeneratedToolFaceBinding& candidate)
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

void TryAppendFaceBinding(
    std::vector<OcctGeneratedToolFaceBinding>& bindings,
    int geometryIndex,
    int faceId,
    const std::string& systemRole,
    const RevolvedProfileEdgeSource* source = nullptr)
{
    if (faceId <= 0) {
        return;
    }

    OcctGeneratedToolFaceBinding binding;
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

    const auto duplicate = std::find_if(
        bindings.begin(),
        bindings.end(),
        [&](const OcctGeneratedToolFaceBinding& existing) {
            return FaceBindingMatches(existing, binding);
        });
    if (duplicate == bindings.end()) {
        bindings.push_back(std::move(binding));
    }
}

struct ResolvedFaceBindingCandidate {
    const RevolvedProfileEdgeSource* source = nullptr;
    double score = 0.0;
    bool hinted = false;
};

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

OcctGeneratedToolFaceBinding BuildCollapsedFaceBinding(
    int geometryIndex,
    int faceId,
    const std::vector<const RevolvedProfileEdgeSource*>& sources)
{
    OcctGeneratedToolFaceBinding binding;
    binding.geometryIndex = geometryIndex;
    binding.faceId = faceId;
    if (sources.empty()) {
        binding.systemRole = "profile";
        return binding;
    }

    const RevolvedProfileEdgeSource& first = *sources.front();
    const bool sameRole = std::all_of(
        sources.begin(),
        sources.end(),
        [&](const RevolvedProfileEdgeSource* source) {
            return source != nullptr && source->systemRole == first.systemRole;
        });
    binding.systemRole = sameRole ? first.systemRole : "profile";

    if (sources.size() == 1) {
        const RevolvedProfileEdgeSource& only = *sources.front();
        if (only.hasSegmentIndex) {
            binding.segmentIndex = only.segmentIndex;
            binding.hasSegmentIndex = true;
        }
        if (only.hasSegmentId) {
            binding.segmentId = only.segmentId;
            binding.hasSegmentId = true;
        }
        if (only.hasSegmentTag) {
            binding.segmentTag = only.segmentTag;
            binding.hasSegmentTag = true;
        }
    }

    return binding;
}

void TryAppendResolvedFaceBinding(
    std::vector<OcctGeneratedToolFaceBinding>& bindings,
    const OcctGeneratedToolFaceBinding& binding)
{
    const auto duplicate = std::find_if(
        bindings.begin(),
        bindings.end(),
        [&](const OcctGeneratedToolFaceBinding& existing) {
            return FaceBindingMatches(existing, binding);
        });
    if (duplicate == bindings.end()) {
        bindings.push_back(binding);
    }
}

std::vector<OcctGeneratedToolFaceBinding> BuildGeneratedToolFaceBindings(
    const TopoDS_Shape& revolvedShape,
    const TopoDS_Face& profileFace,
    BRepPrimAPI_MakeRevol& makeRevol,
    const RevolvedProfileWireBuild& wireBuild,
    const OcctRevolvedToolSpec& spec)
{
    std::vector<OcctGeneratedToolFaceBinding> bindings;
    TopTools_IndexedMapOfShape faceMap;
    TopTools_IndexedMapOfShape profileEdgeMap;
    TopExp::MapShapes(revolvedShape, TopAbs_FACE, faceMap);
    TopExp::MapShapes(profileFace, TopAbs_EDGE, profileEdgeMap);

    const int startCapFaceId =
        std::abs(spec.angleDeg - 360.0) > 1e-9
            ? FindFaceId(makeRevol.FirstShape(profileFace), faceMap)
            : 0;
    const int endCapFaceId =
        std::abs(spec.angleDeg - 360.0) > 1e-9
            ? FindFaceId(makeRevol.LastShape(profileFace), faceMap)
            : 0;

    std::vector<RevolvedFaceDescriptor> faceDescriptors;
    faceDescriptors.reserve(static_cast<size_t>(faceMap.Extent()));
    for (int faceId = 1; faceId <= faceMap.Extent(); ++faceId) {
        faceDescriptors.push_back(DescribeRevolvedFace(TopoDS::Face(faceMap(faceId)), faceId));
    }

    std::vector<std::vector<int>> sourceHintFaceIds(wireBuild.edgeSources.size());
    const int mappedEdgeCount = std::min(profileEdgeMap.Extent(), static_cast<int>(wireBuild.edgeSources.size()));
    for (int index = 0; index < mappedEdgeCount; ++index) {
        sourceHintFaceIds[static_cast<size_t>(index)] =
            CollectGeneratedFaceIds(makeRevol.Generated(profileEdgeMap(index + 1)), faceMap);
    }
    for (size_t index = 0; index < wireBuild.edgeSources.size(); ++index) {
        std::vector<int> directHintFaceIds = CollectGeneratedFaceIds(makeRevol.Generated(wireBuild.edgeSources[index].edge), faceMap);
        for (int faceId : directHintFaceIds) {
            if (!FaceIdCollectionContains(sourceHintFaceIds[index], faceId)) {
                sourceHintFaceIds[index].push_back(faceId);
            }
        }
    }

    for (const auto& faceDescriptor : faceDescriptors) {
        if (faceDescriptor.faceId == startCapFaceId || faceDescriptor.faceId == endCapFaceId) {
            continue;
        }

        std::vector<ResolvedFaceBindingCandidate> candidates;
        for (size_t sourceIndex = 0; sourceIndex < wireBuild.edgeSources.size(); ++sourceIndex) {
            const RevolvedProfileEdgeSource& source = wireBuild.edgeSources[sourceIndex];
            double score = 0.0;
            if (!TryMatchSourceToFace(source, faceDescriptor, score)) {
                continue;
            }

            ResolvedFaceBindingCandidate candidate;
            candidate.source = &source;
            candidate.score = score;
            candidate.hinted = FaceIdCollectionContains(sourceHintFaceIds[sourceIndex], faceDescriptor.faceId);
            candidates.push_back(candidate);
        }

        if (candidates.empty()) {
            std::vector<const RevolvedProfileEdgeSource*> hintedSources;
            for (size_t sourceIndex = 0; sourceIndex < wireBuild.edgeSources.size(); ++sourceIndex) {
                if (FaceIdCollectionContains(sourceHintFaceIds[sourceIndex], faceDescriptor.faceId)) {
                    hintedSources.push_back(&wireBuild.edgeSources[sourceIndex]);
                }
            }
            if (hintedSources.empty()) {
                continue;
            }

            TryAppendResolvedFaceBinding(bindings, BuildCollapsedFaceBinding(0, faceDescriptor.faceId, hintedSources));
            continue;
        }

        std::vector<const RevolvedProfileEdgeSource*> resolvedSources;
        if (candidates.size() == 1) {
            resolvedSources.push_back(candidates.front().source);
        } else {
            std::vector<const RevolvedProfileEdgeSource*> hintedSources;
            for (const auto& candidate : candidates) {
                if (candidate.hinted) {
                    hintedSources.push_back(candidate.source);
                }
            }

            if (hintedSources.size() == 1) {
                resolvedSources = std::move(hintedSources);
            } else if (!hintedSources.empty()) {
                resolvedSources = std::move(hintedSources);
            } else {
                for (const auto& candidate : candidates) {
                    resolvedSources.push_back(candidate.source);
                }
            }
        }

        TryAppendResolvedFaceBinding(bindings, BuildCollapsedFaceBinding(0, faceDescriptor.faceId, resolvedSources));
    }

    if (std::abs(spec.angleDeg - 360.0) > 1e-9) {
        TryAppendFaceBinding(bindings, 0, startCapFaceId, "start_cap");
        TryAppendFaceBinding(bindings, 0, endCapFaceId, "end_cap");
    }

    return bindings;
}

void ApplyGeneratedToolFaceColors(
    OcctMeshData& mesh,
    const std::vector<OcctGeneratedToolFaceBinding>& faceBindings)
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

        mesh.faces[faceIndex].color = ResolveGeneratedFaceColor(binding);
        assigned[faceIndex] = mesh.faces[faceIndex].color.isValid;
    }

    for (size_t faceIndex = 0; faceIndex < mesh.faces.size(); ++faceIndex) {
        if (!assigned[faceIndex]) {
            mesh.faces[faceIndex].color = NeutralToolColor();
        }
    }
}

OcctGeneratedToolMetadata BuildGeneratedToolMetadata(const OcctRevolvedToolSpec& spec)
{
    OcctGeneratedToolMetadata metadata;
    metadata.version = spec.version;
    metadata.units = spec.units;
    metadata.plane = spec.profile.plane;
    metadata.closure = spec.profile.closure;
    metadata.angleDeg = spec.angleDeg;
    metadata.segmentCount = static_cast<int>(spec.profile.segments.size());

    for (size_t index = 0; index < spec.profile.segments.size(); ++index) {
        const auto& segment = spec.profile.segments[index];
        OcctGeneratedToolSegmentDescriptor descriptor;
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

OcctGeneratedToolMeshValidation AnalyzeGeneratedToolMesh(const OcctMeshData& mesh)
{
    constexpr double kMeshWeldTolerance = 1e-6;

    OcctGeneratedToolMeshValidation validation;
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

OcctGeneratedToolShapeValidation BuildGeneratedToolShapeValidation(
    const TopoDS_Shape& shape,
    const OcctMeshData& mesh)
{
    OcctGeneratedToolShapeValidation validation;
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

    validation.mesh = AnalyzeGeneratedToolMesh(mesh);
    return validation;
}

void PopulateGeneratedScene(
    const OcctMeshData& mesh,
    const OcctRevolvedToolSpec& spec,
    OcctSceneData& scene)
{
    scene.success = true;
    scene.sourceUnit = spec.units == "inch" ? "INCH" : "MM";
    scene.unitScaleToMeters = spec.units == "inch" ? 0.0254 : 0.001;

    scene.meshes.push_back(mesh);

    OcctNodeData rootNode;
    rootNode.id = "generated-revolved-shape-root";
    rootNode.name = "Generated Revolved Shape";
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

void FinalizeBuildFailure(OcctRevolvedToolBuildResult& result, const std::string& error)
{
    result.success = false;
    result.error = error;
    result.scene.success = false;
    result.scene.error = error;
}

} // anonymous namespace

OcctRevolvedToolValidationResult ValidateRevolvedShapeSpec(const val& jsSpec)
{
    OcctRevolvedToolValidationResult result;
    if (!IsObject(jsSpec)) {
        AddDiagnostic(result, "invalid-spec", "Revolved shape spec must be an object.");
        return result;
    }

    ParseVersion(jsSpec, result);
    ParseUnits(jsSpec, result);

    val jsProfile = val::undefined();
    if (TryGetOwnProperty(jsSpec, "profile", jsProfile)) {
        ParseProfile(jsProfile, result);
    } else {
        AddDiagnostic(result, "missing-field", "Spec.profile is required.", "profile");
    }

    ParseRevolve(jsSpec, result);

    result.ok = result.diagnostics.empty();
    result.hasSpec = result.ok;
    return result;
}

OcctRevolvedToolBuildResult BuildRevolvedShape(const val& jsSpec, const val& jsOptions)
{
    OcctRevolvedToolBuildResult result;
    const OcctRevolvedToolValidationResult validation = ValidateRevolvedShapeSpec(jsSpec);
    if (!validation.ok) {
        result.diagnostics = validation.diagnostics;
        FinalizeBuildFailure(
            result,
            validation.diagnostics.empty()
                ? "Invalid revolved shape spec."
                : validation.diagnostics.front().message);
        return result;
    }

    const OcctRevolvedToolSpec& spec = validation.spec;
    result.revolvedShape = BuildGeneratedToolMetadata(spec);
    result.hasRevolvedShape = true;

    try {
        ResolvedRevolvedProfileLoop resolvedProfileLoop;
        if (!TryResolveValidatedProfileLoop(spec, resolvedProfileLoop, result)) {
            FinalizeBuildFailure(
                result,
                result.diagnostics.empty()
                    ? "Failed to normalize a closed revolved shape profile."
                    : result.diagnostics.front().message);
            return result;
        }

        if (std::abs(ComputeApproximateProfileArea(resolvedProfileLoop.profile)) <= kPointTolerance) {
            AddDiagnostic(result, "build-failed", "Generated revolved shape profile encloses zero area.", "profile");
            FinalizeBuildFailure(result, "Generated revolved shape profile encloses zero area.");
            return result;
        }

        RevolvedProfileWireBuild wireBuild;
        if (!TryBuildProfileWire(resolvedProfileLoop, wireBuild)) {
            AddDiagnostic(result, "build-failed", "Failed to build a connected revolved shape profile wire.", "profile");
            FinalizeBuildFailure(result, "Failed to build a connected revolved shape profile wire.");
            return result;
        }

        BRepBuilderAPI_MakeFace faceBuilder(wireBuild.wire, Standard_True);
        if (!faceBuilder.IsDone()) {
            AddDiagnostic(result, "build-failed", "Failed to create a planar profile face from the revolved shape wire.", "profile");
            FinalizeBuildFailure(result, "Failed to create a planar profile face from the revolved shape wire.");
            return result;
        }

        const TopoDS_Face face = faceBuilder.Face();
        const gp_Ax1 axis(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0));
        const double angleRad = spec.angleDeg * kPi / 180.0;

        TopoDS_Shape revolvedShape;
        std::vector<OcctGeneratedToolFaceBinding> faceBindings;
        if (std::abs(spec.angleDeg - 360.0) <= 1e-9) {
            BRepPrimAPI_MakeRevol makeRevol(face, axis, 2.0 * kPi);
            makeRevol.Build();
            if (!makeRevol.IsDone()) {
                AddDiagnostic(result, "build-failed", "OCCT failed to revolve the generated profile.", "revolve");
                FinalizeBuildFailure(result, "OCCT failed to revolve the generated profile.");
                return result;
            }
            revolvedShape = makeRevol.Shape();
            faceBindings = BuildGeneratedToolFaceBindings(revolvedShape, face, makeRevol, wireBuild, spec);
        } else {
            BRepPrimAPI_MakeRevol makeRevol(face, axis, angleRad);
            makeRevol.Build();
            if (!makeRevol.IsDone()) {
                AddDiagnostic(result, "build-failed", "OCCT failed to revolve the generated profile.", "revolve");
                FinalizeBuildFailure(result, "OCCT failed to revolve the generated profile.");
                return result;
            }
            revolvedShape = makeRevol.Shape();
            faceBindings = BuildGeneratedToolFaceBindings(revolvedShape, face, makeRevol, wireBuild, spec);
        }

        if (revolvedShape.IsNull()) {
            AddDiagnostic(result, "build-failed", "OCCT produced a null revolved shape.", "revolve");
            FinalizeBuildFailure(result, "OCCT produced a null revolved shape.");
            return result;
        }

        ImportParams buildParams = ParseBuildParams(jsOptions, spec);
        if (!TriangulateShape(revolvedShape, buildParams)) {
            AddDiagnostic(result, "build-failed", "Failed to triangulate the generated revolved shape.", "mesh");
            FinalizeBuildFailure(result, "Failed to triangulate the generated revolved shape.");
            return result;
        }

        OcctMeshData mesh = ExtractMeshFromShape(revolvedShape);
        if (mesh.indices.empty() || mesh.faces.empty()) {
            AddDiagnostic(result, "build-failed", "Generated revolved shape triangulation produced no renderable faces.", "mesh");
            FinalizeBuildFailure(result, "Generated revolved shape triangulation produced no renderable faces.");
            return result;
        }

        mesh.name = "Generated Revolved Shape";
        mesh.color = NeutralToolColor();
        ApplyGeneratedToolFaceColors(mesh, faceBindings);
        result.revolvedShape.shapeValidation = BuildGeneratedToolShapeValidation(revolvedShape, mesh);
        result.revolvedShape.hasShapeValidation = true;

        PopulateGeneratedScene(mesh, spec, result.scene);
        result.exactShape = revolvedShape;
        result.exactGeometryShapes = { revolvedShape };
        result.revolvedShape.faceBindings = std::move(faceBindings);
        result.revolvedShape.hasStableFaceBindings = true;
        result.success = true;
        result.error.clear();
        return result;
    } catch (const Standard_Failure& failure) {
        const std::string message =
            failure.GetMessageString() != nullptr
                ? std::string(failure.GetMessageString())
                : std::string("OCCT threw an exception while building the generated revolved shape.");
        AddDiagnostic(result, "build-failed", message, "revolve");
        FinalizeBuildFailure(result, message);
        return result;
    } catch (...) {
        AddDiagnostic(result, "build-failed", "Unknown exception while building the generated revolved shape.", "revolve");
        FinalizeBuildFailure(result, "Unknown exception while building the generated revolved shape.");
        return result;
    }
}
