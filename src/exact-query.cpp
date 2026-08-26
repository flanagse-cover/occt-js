#include "exact-query.hpp"

#include "exact-model-store.hpp"

#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepLProp_SLProps.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <GProp_GProps.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_State.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Circ.hxx>
#include <gp_Cone.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Dir.hxx>
#include <gp_Lin.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pln.hxx>
#include <gp_Sphere.hxx>
#include <gp_Torus.hxx>
#include <gp_Vec.hxx>
#include <GeomAbs_CurveType.hxx>
#include <GeomAbs_SurfaceType.hxx>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace {

template <typename TResult>
TResult MakeFailure(const std::string& code, const std::string& message)
{
    TResult result;
    result.ok = false;
    result.code = code;
    result.message = message;
    return result;
}

constexpr const char* kCompoundHoleUnsupportedMessage =
    "Exact compound-hole helper only supports selected refs that normalize into supported counterbore or countersink cavities.";

std::string NormalizeKind(const std::string& kind)
{
    std::string normalized = kind;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return normalized;
}

std::array<double, 3> ToArray(const gp_Pnt& point)
{
    return { point.X(), point.Y(), point.Z() };
}

std::array<double, 3> ToArray(const gp_Dir& direction)
{
    return { direction.X(), direction.Y(), direction.Z() };
}

std::array<double, 3> ToArray(const gp_Vec& vector)
{
    return { vector.X(), vector.Y(), vector.Z() };
}

gp_Pnt Midpoint(const gp_Pnt& left, const gp_Pnt& right)
{
    return gp_Pnt(
        0.5 * (left.X() + right.X()),
        0.5 * (left.Y() + right.Y()),
        0.5 * (left.Z() + right.Z())
    );
}

double AxisParameterOnLine(const gp_Pnt& axisOrigin, const gp_Dir& axisDirection, const gp_Pnt& point)
{
    return gp_Vec(axisOrigin, point).Dot(gp_Vec(axisDirection));
}

gp_Pnt ToPoint(const std::array<double, 3>& point)
{
    return gp_Pnt(point[0], point[1], point[2]);
}

template <typename TResult>
TResult ConvertFailure(const OcctLifecycleResult& lifecycle)
{
    return MakeFailure<TResult>(lifecycle.code, lifecycle.message);
}

OcctLifecycleResult LookupGeometryShape(
    int exactModelId,
    int exactShapeHandle,
    TopoDS_Shape& geometryShape)
{
    return ExactModelStore::Instance().GetGeometryShape(exactModelId, exactShapeHandle, geometryShape);
}

OcctLifecycleResult ResolveFace(
    int exactModelId,
    int exactShapeHandle,
    int elementId,
    TopoDS_Face& face)
{
    TopoDS_Shape geometryShape;
    OcctLifecycleResult lifecycle = LookupGeometryShape(exactModelId, exactShapeHandle, geometryShape);
    if (!lifecycle.ok) {
        return lifecycle;
    }

    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(geometryShape, TopAbs_FACE, faces);
    if (elementId <= 0 || elementId > faces.Extent()) {
        return MakeFailure<OcctLifecycleResult>("invalid-id", "Requested face id is out of range for this exact geometry.");
    }

    face = TopoDS::Face(faces(elementId));
    if (face.IsNull()) {
        return MakeFailure<OcctLifecycleResult>("invalid-id", "Requested face id did not resolve to a retained exact face.");
    }

    OcctLifecycleResult ok;
    ok.ok = true;
    return ok;
}

OcctLifecycleResult ResolveEdge(
    int exactModelId,
    int exactShapeHandle,
    int elementId,
    TopoDS_Edge& edge)
{
    TopoDS_Shape geometryShape;
    OcctLifecycleResult lifecycle = LookupGeometryShape(exactModelId, exactShapeHandle, geometryShape);
    if (!lifecycle.ok) {
        return lifecycle;
    }

    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(geometryShape, TopAbs_EDGE, edges);
    if (elementId <= 0 || elementId > edges.Extent()) {
        return MakeFailure<OcctLifecycleResult>("invalid-id", "Requested edge id is out of range for this exact geometry.");
    }

    edge = TopoDS::Edge(edges(elementId));
    if (edge.IsNull()) {
        return MakeFailure<OcctLifecycleResult>("invalid-id", "Requested edge id did not resolve to a retained exact edge.");
    }

    OcctLifecycleResult ok;
    ok.ok = true;
    return ok;
}

OcctLifecycleResult ResolveVertex(
    int exactModelId,
    int exactShapeHandle,
    int elementId,
    TopoDS_Vertex& vertex)
{
    TopoDS_Shape geometryShape;
    OcctLifecycleResult lifecycle = LookupGeometryShape(exactModelId, exactShapeHandle, geometryShape);
    if (!lifecycle.ok) {
        return lifecycle;
    }

    TopTools_IndexedMapOfShape vertices;
    TopExp::MapShapes(geometryShape, TopAbs_VERTEX, vertices);
    if (elementId <= 0 || elementId > vertices.Extent()) {
        return MakeFailure<OcctLifecycleResult>("invalid-id", "Requested vertex id is out of range for this exact geometry.");
    }

    vertex = TopoDS::Vertex(vertices(elementId));
    if (vertex.IsNull()) {
        return MakeFailure<OcctLifecycleResult>("invalid-id", "Requested vertex id did not resolve to a retained exact vertex.");
    }

    OcctLifecycleResult ok;
    ok.ok = true;
    return ok;
}

OcctLifecycleResult ResolveElementShape(
    int exactModelId,
    int exactShapeHandle,
    const std::string& kind,
    int elementId,
    TopoDS_Shape& shape)
{
    const std::string normalizedKind = NormalizeKind(kind);
    if (normalizedKind == "face") {
        TopoDS_Face face;
        OcctLifecycleResult lifecycle = ResolveFace(exactModelId, exactShapeHandle, elementId, face);
        if (!lifecycle.ok) {
            return lifecycle;
        }
        shape = face;
        OcctLifecycleResult ok;
        ok.ok = true;
        return ok;
    }

    if (normalizedKind == "edge") {
        TopoDS_Edge edge;
        OcctLifecycleResult lifecycle = ResolveEdge(exactModelId, exactShapeHandle, elementId, edge);
        if (!lifecycle.ok) {
            return lifecycle;
        }
        shape = edge;
        OcctLifecycleResult ok;
        ok.ok = true;
        return ok;
    }

    if (normalizedKind == "vertex") {
        TopoDS_Vertex vertex;
        OcctLifecycleResult lifecycle = ResolveVertex(exactModelId, exactShapeHandle, elementId, vertex);
        if (!lifecycle.ok) {
            return lifecycle;
        }
        shape = vertex;
        OcctLifecycleResult ok;
        ok.ok = true;
        return ok;
    }

    return MakeFailure<OcctLifecycleResult>("unsupported-geometry", "Exact pairwise queries only support face, edge, or vertex refs.");
}

TopoDS_Shape ApplyOccurrenceTransform(const TopoDS_Shape& shape, const gp_Trsf& transform)
{
    if (shape.IsNull()) {
        return shape;
    }
    return shape.Moved(TopLoc_Location(transform));
}

OcctLifecycleResult MeasureMinimumDistance(
    const TopoDS_Shape& shapeA,
    const TopoDS_Shape& shapeB,
    double& value,
    gp_Pnt& pointA,
    gp_Pnt& pointB)
{
    BRepExtrema_DistShapeShape distance(shapeA, shapeB, Precision::Confusion());
    if (!distance.IsDone()) {
        return MakeFailure<OcctLifecycleResult>("internal-error", "OCCT exact distance query did not complete.");
    }
    if (distance.NbSolution() <= 0) {
        return MakeFailure<OcctLifecycleResult>("internal-error", "OCCT exact distance query returned no solutions.");
    }

    value = distance.Value();
    pointA = distance.PointOnShape1(1);
    pointB = distance.PointOnShape2(1);

    OcctLifecycleResult ok;
    ok.ok = true;
    return ok;
}

bool TryMakeDirection(const gp_Pnt& from, const gp_Pnt& to, gp_Dir& direction)
{
    gp_Vec delta(from, to);
    if (delta.Magnitude() <= Precision::Confusion()) {
        return false;
    }
    direction = gp_Dir(delta);
    return true;
}

bool TryMakeDirection(const std::array<double, 3>& vector, gp_Dir& direction)
{
    gp_Vec delta(vector[0], vector[1], vector[2]);
    if (delta.Magnitude() <= Precision::Confusion()) {
        return false;
    }
    direction = gp_Dir(delta);
    return true;
}

double CanonicalAngle(const double radians)
{
    constexpr double halfPi = 1.5707963267948966;
    return radians > halfPi ? 3.14159265358979323846 - radians : radians;
}

std::string FamilyFromSurfaceType(const GeomAbs_SurfaceType type)
{
    switch (type) {
        case GeomAbs_Plane:
            return "plane";
        case GeomAbs_Cylinder:
            return "cylinder";
        case GeomAbs_Cone:
            return "cone";
        case GeomAbs_Sphere:
            return "sphere";
        case GeomAbs_Torus:
            return "torus";
        default:
            return "other";
    }
}

std::string FamilyFromCurveType(const GeomAbs_CurveType type)
{
    switch (type) {
        case GeomAbs_Line:
            return "line";
        case GeomAbs_Circle:
            return "circle";
        default:
            return "other";
    }
}

gp_Pnt SampleFacePoint(const TopoDS_Face& face, BRepAdaptor_Surface& surface)
{
    double uFirst = 0.0;
    double uLast = 0.0;
    double vFirst = 0.0;
    double vLast = 0.0;
    BRepTools::UVBounds(face, uFirst, uLast, vFirst, vLast);
    return surface.Value(0.5 * (uFirst + uLast), 0.5 * (vFirst + vLast));
}

gp_Dir ChooseStableReferenceDirection(const gp_Dir& axis)
{
    const double absX = std::abs(axis.X());
    const double absY = std::abs(axis.Y());
    const double absZ = std::abs(axis.Z());

    if (absX <= absY && absX <= absZ) {
        return gp_Dir(1.0, 0.0, 0.0);
    }
    if (absY <= absZ) {
        return gp_Dir(0.0, 1.0, 0.0);
    }
    return gp_Dir(0.0, 0.0, 1.0);
}

bool TryProjectDirectionOntoPlane(const gp_Dir& candidate, const gp_Dir& planeNormal, gp_Dir& projected)
{
    gp_Vec candidateVector(candidate.XYZ());
    const gp_Vec normalVector(planeNormal.XYZ());
    candidateVector -= normalVector * candidateVector.Dot(normalVector);
    if (candidateVector.Magnitude() <= Precision::Confusion()) {
        return false;
    }

    projected = gp_Dir(candidateVector);
    return true;
}

bool BuildFrameFromNormalAndX(
    const gp_Pnt& origin,
    const gp_Dir& normal,
    const gp_Dir& preferredX,
    OcctExactPlacementFrame& frame)
{
    gp_Dir xDir;
    if (!TryProjectDirectionOntoPlane(preferredX, normal, xDir)) {
        const gp_Dir fallback = ChooseStableReferenceDirection(normal);
        if (!TryProjectDirectionOntoPlane(fallback, normal, xDir)) {
            return false;
        }
    }

    gp_Vec yVector(normal.XYZ().Crossed(xDir.XYZ()));
    if (yVector.Magnitude() <= Precision::Confusion()) {
        return false;
    }

    const gp_Dir yDir(yVector);

    frame.origin = ToArray(origin);
    frame.normal = ToArray(normal);
    frame.xDir = ToArray(xDir);
    frame.yDir = ToArray(yDir);
    return true;
}

bool BuildFrameFromXAxis(
    const gp_Pnt& origin,
    const gp_Dir& xAxis,
    OcctExactPlacementFrame& frame)
{
    const gp_Dir reference = ChooseStableReferenceDirection(xAxis);
    gp_Vec normalVector(xAxis.XYZ().Crossed(reference.XYZ()));
    if (normalVector.Magnitude() <= Precision::Confusion()) {
        return false;
    }

    const gp_Dir normal(normalVector);
    return BuildFrameFromNormalAndX(origin, normal, xAxis, frame);
}

void AddPlacementAnchor(OcctExactPlacementResult& result, const std::string& role, const gp_Pnt& point)
{
    OcctExactPlacementAnchor anchor;
    anchor.role = role;
    anchor.point = ToArray(point);
    result.anchors.push_back(anchor);
}

void AddRelationAnchor(OcctExactRelationResult& result, const std::string& role, const gp_Pnt& point)
{
    OcctExactPlacementAnchor anchor;
    anchor.role = role;
    anchor.point = ToArray(point);
    result.anchors.push_back(anchor);
}

void AddHoleAnchor(OcctExactHoleResult& result, const std::string& role, const gp_Pnt& point)
{
    OcctExactPlacementAnchor anchor;
    anchor.role = role;
    anchor.point = ToArray(point);
    result.anchors.push_back(anchor);
}

void AddChamferAnchor(OcctExactChamferResult& result, const std::string& role, const gp_Pnt& point)
{
    OcctExactPlacementAnchor anchor;
    anchor.role = role;
    anchor.point = ToArray(point);
    result.anchors.push_back(anchor);
}

void AddCompoundHoleAnchor(OcctExactCompoundHoleResult& result, const std::string& role, const gp_Pnt& point)
{
    OcctExactPlacementAnchor anchor;
    anchor.role = role;
    anchor.point = ToArray(point);
    result.anchors.push_back(anchor);
}

bool IsOutsideState(const TopAbs_State state)
{
    return state == TopAbs_OUT || state == TopAbs_ON;
}

OcctLifecycleResult ClassifyPointInShape(
    const TopoDS_Shape& shape,
    const gp_Pnt& point,
    TopAbs_State& state)
{
    BRepClass3d_SolidClassifier classifier(shape, point, Precision::Confusion());
    state = classifier.State();
    if (state == TopAbs_UNKNOWN) {
        return MakeFailure<OcctLifecycleResult>(
            "internal-error",
            "OCCT exact hole classification could not determine whether a probe point was inside or outside the retained geometry."
        );
    }

    OcctLifecycleResult ok;
    ok.ok = true;
    return ok;
}

OcctLifecycleResult ResolveHoleCandidateFace(
    int exactModelId,
    int exactShapeHandle,
    const std::string& kind,
    int elementId,
    TopoDS_Shape& geometryShape,
    TopoDS_Face& holeFace)
{
    OcctLifecycleResult lifecycle = LookupGeometryShape(exactModelId, exactShapeHandle, geometryShape);
    if (!lifecycle.ok) {
        return lifecycle;
    }

    const std::string normalizedKind = NormalizeKind(kind);
    if (normalizedKind == "face") {
        lifecycle = ResolveFace(exactModelId, exactShapeHandle, elementId, holeFace);
        if (!lifecycle.ok) {
            return lifecycle;
        }

        BRepAdaptor_Surface surface(holeFace, false);
        if (surface.GetType() != GeomAbs_Cylinder) {
            return MakeFailure<OcctLifecycleResult>(
                "unsupported-geometry",
                "Exact hole helper only supports cylindrical face refs or circular edge refs adjacent to one cylindrical face."
            );
        }

        OcctLifecycleResult ok;
        ok.ok = true;
        return ok;
    }

    if (normalizedKind != "edge") {
        return MakeFailure<OcctLifecycleResult>(
            "unsupported-kind",
            "Exact hole helper only supports edge or face refs."
        );
    }

    TopoDS_Edge edge;
    lifecycle = ResolveEdge(exactModelId, exactShapeHandle, elementId, edge);
    if (!lifecycle.ok) {
        return lifecycle;
    }

    BRepAdaptor_Curve curve(edge);
    if (curve.GetType() != GeomAbs_Circle) {
        return MakeFailure<OcctLifecycleResult>(
            "unsupported-geometry",
            "Exact hole helper only supports cylindrical face refs or circular edge refs adjacent to one cylindrical face."
        );
    }

    TopTools_IndexedDataMapOfShapeListOfShape edgeToFaces;
    TopExp::MapShapesAndAncestors(geometryShape, TopAbs_EDGE, TopAbs_FACE, edgeToFaces);
    if (!edgeToFaces.Contains(edge)) {
        return MakeFailure<OcctLifecycleResult>(
            "invalid-id",
            "Requested edge id did not resolve to a retained exact hole candidate."
        );
    }

    int cylinderFaceCount = 0;
    for (TopTools_ListIteratorOfListOfShape it(edgeToFaces.FindFromKey(edge)); it.More(); it.Next()) {
        const TopoDS_Face candidateFace = TopoDS::Face(it.Value());
        if (candidateFace.IsNull()) {
            continue;
        }

        BRepAdaptor_Surface surface(candidateFace, false);
        if (surface.GetType() != GeomAbs_Cylinder) {
            continue;
        }

        holeFace = candidateFace;
        cylinderFaceCount += 1;
    }

    if (cylinderFaceCount != 1 || holeFace.IsNull()) {
        return MakeFailure<OcctLifecycleResult>(
            "unsupported-geometry",
            "Exact hole helper only supports circular edge refs adjacent to exactly one cylindrical face."
        );
    }

    OcctLifecycleResult ok;
    ok.ok = true;
    return ok;
}

struct CircularBoundaryInfo {
    TopoDS_Edge edge;
    gp_Pnt center;
    double axisParameter = 0.0;
    double radius = 0.0;
    bool isOpen = false;
};

struct HoleCylinderDescriptor {
    TopoDS_Face face;
    gp_Cylinder cylinder;
    gp_Dir axisDirection;
    gp_Dir radialDirection;
    std::vector<CircularBoundaryInfo> boundaries;
};

OcctLifecycleResult CollectCoaxialCircularBoundaryInfo(
    const TopoDS_Face& face,
    const gp_Pnt& axisOrigin,
    const gp_Dir& axisDirection,
    std::vector<CircularBoundaryInfo>& boundaries,
    const double expectedRadius = -1.0)
{
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(face, TopAbs_EDGE, edges);
    const gp_Lin axisLine(axisOrigin, axisDirection);
    const double tolerance = Precision::Confusion();

    for (int edgeIndex = 1; edgeIndex <= edges.Extent(); edgeIndex += 1) {
        const TopoDS_Edge edge = TopoDS::Edge(edges(edgeIndex));
        if (edge.IsNull()) {
            continue;
        }

        BRepAdaptor_Curve curve(edge);
        if (curve.GetType() != GeomAbs_Circle) {
            continue;
        }

        const gp_Circ circle = curve.Circle();
        if (!circle.Axis().Direction().IsParallel(axisDirection, Precision::Angular())) {
            continue;
        }
        if (axisLine.Distance(circle.Location()) > tolerance) {
            continue;
        }
        if (expectedRadius >= 0.0 && std::abs(circle.Radius() - expectedRadius) > tolerance) {
            continue;
        }

        bool duplicate = false;
        for (const auto& boundary : boundaries) {
            if (boundary.center.Distance(circle.Location()) <= tolerance
                && std::abs(boundary.radius - circle.Radius()) <= tolerance) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }

        CircularBoundaryInfo boundary;
        boundary.edge = edge;
        boundary.center = circle.Location();
        boundary.axisParameter = AxisParameterOnLine(axisOrigin, axisDirection, boundary.center);
        boundary.radius = circle.Radius();
        boundaries.push_back(boundary);
    }

    std::sort(boundaries.begin(), boundaries.end(), [](const CircularBoundaryInfo& left, const CircularBoundaryInfo& right) {
        return left.axisParameter < right.axisParameter;
    });

    OcctLifecycleResult ok;
    ok.ok = true;
    return ok;
}

OcctLifecycleResult CollectHoleBoundaryInfo(
    const TopoDS_Face& holeFace,
    const gp_Cylinder& cylinder,
    const gp_Dir& axisDirection,
    std::vector<CircularBoundaryInfo>& boundaries)
{
    OcctLifecycleResult lifecycle = CollectCoaxialCircularBoundaryInfo(
        holeFace,
        cylinder.Location(),
        axisDirection,
        boundaries,
        cylinder.Radius()
    );
    if (!lifecycle.ok) {
        return lifecycle;
    }

    if (boundaries.size() != 2) {
        return MakeFailure<OcctLifecycleResult>(
            "unsupported-geometry",
            "Exact hole helper only supports cylindrical holes with exactly two circular boundary loops."
        );
    }

    OcctLifecycleResult ok;
    ok.ok = true;
    return ok;
}

OcctLifecycleResult ResolveBoundaryOpenStates(
    const TopoDS_Shape& geometryShape,
    const gp_Dir& axisDirection,
    std::vector<CircularBoundaryInfo>& boundaries)
{
    if (boundaries.size() < 2) {
        return MakeFailure<OcctLifecycleResult>(
            "unsupported-geometry",
            "Exact hole helper requires at least two circular boundaries."
        );
    }

    const double depth = std::abs(boundaries[1].axisParameter - boundaries[0].axisParameter);
    const double probeDistance = std::max(Precision::Confusion() * 100.0, std::min(depth * 0.1, 0.5));
    const double midpoint = 0.5 * (boundaries[0].axisParameter + boundaries[1].axisParameter);

    for (auto& boundary : boundaries) {
        const double direction = boundary.axisParameter >= midpoint ? 1.0 : -1.0;
        const gp_Pnt probePoint = boundary.center.Translated(gp_Vec(axisDirection) * (direction * probeDistance));
        TopAbs_State state = TopAbs_UNKNOWN;
        OcctLifecycleResult lifecycle = ClassifyPointInShape(geometryShape, probePoint, state);
        if (!lifecycle.ok) {
            return lifecycle;
        }
        boundary.isOpen = IsOutsideState(state);
    }

    OcctLifecycleResult ok;
    ok.ok = true;
    return ok;
}

bool CircularBoundaryMatches(
    const CircularBoundaryInfo& boundary,
    const gp_Pnt& center,
    const double radius,
    const double tolerance = Precision::Confusion())
{
    return boundary.center.Distance(center) <= tolerance
        && std::abs(boundary.radius - radius) <= tolerance;
}

const CircularBoundaryInfo* FindMatchingBoundary(
    const std::vector<CircularBoundaryInfo>& boundaries,
    const gp_Pnt& center,
    const double radius)
{
    for (const auto& boundary : boundaries) {
        if (CircularBoundaryMatches(boundary, center, radius)) {
            return &boundary;
        }
    }
    return nullptr;
}

const CircularBoundaryInfo* FindOtherBoundary(
    const std::vector<CircularBoundaryInfo>& boundaries,
    const gp_Pnt& center,
    const double radius)
{
    for (const auto& boundary : boundaries) {
        if (!CircularBoundaryMatches(boundary, center, radius)) {
            return &boundary;
        }
    }
    return nullptr;
}

void CollectAdjacentFaces(
    const TopTools_IndexedDataMapOfShapeListOfShape& edgeToFaces,
    const TopoDS_Edge& edge,
    std::vector<TopoDS_Face>& ownerFaces,
    const TopoDS_Face* excludeA = nullptr,
    const TopoDS_Face* excludeB = nullptr)
{
    if (!edgeToFaces.Contains(edge)) {
        return;
    }

    for (TopTools_ListIteratorOfListOfShape it(edgeToFaces.FindFromKey(edge)); it.More(); it.Next()) {
        const TopoDS_Face face = TopoDS::Face(it.Value());
        if (face.IsNull()) {
            continue;
        }
        if ((excludeA != nullptr && face.IsSame(*excludeA))
            || (excludeB != nullptr && face.IsSame(*excludeB))) {
            continue;
        }

        bool duplicate = false;
        for (const auto& existing : ownerFaces) {
            if (existing.IsSame(face)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            ownerFaces.push_back(face);
        }
    }
}

bool IsSameAxis(
    const gp_Pnt& originA,
    const gp_Dir& directionA,
    const gp_Pnt& originB,
    const gp_Dir& directionB)
{
    return directionA.IsParallel(directionB, Precision::Angular())
        && gp_Lin(originA, directionA).Distance(originB) <= Precision::Confusion();
}

OcctLifecycleResult ResolveHoleCylinderDescriptor(
    const TopoDS_Shape& geometryShape,
    const TopoDS_Face& holeFace,
    HoleCylinderDescriptor& descriptor)
{
    BRepAdaptor_Surface surface(holeFace, false);
    if (surface.GetType() != GeomAbs_Cylinder) {
        return MakeFailure<OcctLifecycleResult>(
            "unsupported-geometry",
            "Exact hole helper only supports cylindrical face refs or circular edge refs adjacent to one cylindrical face."
        );
    }

    descriptor.face = holeFace;
    descriptor.cylinder = surface.Cylinder();
    descriptor.axisDirection = descriptor.cylinder.Axis().Direction();

    const gp_Pnt samplePoint = SampleFacePoint(holeFace, surface);
    const double sampleParameter = AxisParameterOnLine(
        descriptor.cylinder.Location(),
        descriptor.axisDirection,
        samplePoint
    );
    const gp_Pnt axisPoint = descriptor.cylinder.Location().Translated(gp_Vec(descriptor.axisDirection) * sampleParameter);
    if (!TryMakeDirection(axisPoint, samplePoint, descriptor.radialDirection)) {
        return MakeFailure<OcctLifecycleResult>(
            "unsupported-geometry",
            "Exact hole helper requires cylindrical geometry with a stable radial direction."
        );
    }

    const double radialProbeDistance = std::max(
        Precision::Confusion() * 100.0,
        std::min(descriptor.cylinder.Radius() * 0.25, 0.5)
    );
    const gp_Pnt inwardProbe = samplePoint.Translated(gp_Vec(descriptor.radialDirection) * (-radialProbeDistance));
    TopAbs_State inwardState = TopAbs_UNKNOWN;
    OcctLifecycleResult lifecycle = ClassifyPointInShape(geometryShape, inwardProbe, inwardState);
    if (!lifecycle.ok) {
        return lifecycle;
    }
    if (!IsOutsideState(inwardState)) {
        return MakeFailure<OcctLifecycleResult>(
            "unsupported-geometry",
            "Selected cylindrical geometry does not bound a supported hole cavity."
        );
    }

    descriptor.boundaries.clear();
    lifecycle = CollectHoleBoundaryInfo(holeFace, descriptor.cylinder, descriptor.axisDirection, descriptor.boundaries);
    if (!lifecycle.ok) {
        return lifecycle;
    }

    lifecycle = ResolveBoundaryOpenStates(geometryShape, descriptor.axisDirection, descriptor.boundaries);
    if (!lifecycle.ok) {
        return lifecycle;
    }

    if (!descriptor.boundaries[0].isOpen && !descriptor.boundaries[1].isOpen) {
        return MakeFailure<OcctLifecycleResult>(
            "unsupported-geometry",
            "Exact hole helper requires at least one open circular boundary."
        );
    }

    OcctLifecycleResult ok;
    ok.ok = true;
    return ok;
}

OcctExactHoleResult BuildHoleResult(const HoleCylinderDescriptor& descriptor)
{
    const gp_Pnt centerPoint = Midpoint(descriptor.boundaries[0].center, descriptor.boundaries[1].center);
    const double depth = std::abs(descriptor.boundaries[1].axisParameter - descriptor.boundaries[0].axisParameter);

    OcctExactHoleResult result;
    result.ok = true;
    result.kind = "hole";
    result.profile = "cylindrical";
    result.radius = descriptor.cylinder.Radius();
    result.diameter = result.radius * 2.0;
    result.hasAxisDirection = true;
    result.axisDirection = ToArray(descriptor.axisDirection);
    result.hasDepth = true;
    result.depth = depth;
    result.hasIsThrough = true;
    result.isThrough = descriptor.boundaries[0].isOpen && descriptor.boundaries[1].isOpen;
    if (!BuildFrameFromNormalAndX(centerPoint, descriptor.axisDirection, descriptor.radialDirection, result.frame)) {
        return MakeFailure<OcctExactHoleResult>(
            "unsupported-geometry",
            "Exact hole helper requires a stable working frame."
        );
    }
    result.hasFrame = true;

    AddHoleAnchor(result, "center", centerPoint);
    if (result.isThrough) {
        AddHoleAnchor(result, "entry", descriptor.boundaries[0].center);
        AddHoleAnchor(result, "exit", descriptor.boundaries[1].center);
    } else if (descriptor.boundaries[0].isOpen) {
        AddHoleAnchor(result, "entry", descriptor.boundaries[0].center);
        AddHoleAnchor(result, "bottom", descriptor.boundaries[1].center);
    } else {
        AddHoleAnchor(result, "entry", descriptor.boundaries[1].center);
        AddHoleAnchor(result, "bottom", descriptor.boundaries[0].center);
    }

    return result;
}

OcctLifecycleResult TryDescribeExactCounterboreFromCylinder(
    const TopoDS_Shape& geometryShape,
    const TopoDS_Face& selectedCylinderFace,
    OcctExactCompoundHoleResult& result)
{
    HoleCylinderDescriptor primary;
    OcctLifecycleResult lifecycle = ResolveHoleCylinderDescriptor(geometryShape, selectedCylinderFace, primary);
    if (!lifecycle.ok) {
        return lifecycle;
    }

    TopTools_IndexedDataMapOfShapeListOfShape edgeToFaces;
    TopExp::MapShapesAndAncestors(geometryShape, TopAbs_EDGE, TopAbs_FACE, edgeToFaces);

    const gp_Pnt axisOrigin = primary.cylinder.Location();
    const gp_Dir axisDirection = primary.axisDirection;

    for (const auto& primaryBoundary : primary.boundaries) {
        std::vector<TopoDS_Face> planeFaces;
        CollectAdjacentFaces(edgeToFaces, primaryBoundary.edge, planeFaces, &primary.face);

        for (const auto& planeFace : planeFaces) {
            BRepAdaptor_Surface planeSurface(planeFace, false);
            if (planeSurface.GetType() != GeomAbs_Plane) {
                continue;
            }

            const gp_Pln plane = planeSurface.Plane();
            if (!plane.Axis().Direction().IsParallel(axisDirection, Precision::Angular())) {
                continue;
            }

            std::vector<CircularBoundaryInfo> planeBoundaries;
            lifecycle = CollectCoaxialCircularBoundaryInfo(planeFace, axisOrigin, axisDirection, planeBoundaries);
            if (!lifecycle.ok || planeBoundaries.size() < 2) {
                continue;
            }

            if (FindMatchingBoundary(planeBoundaries, primaryBoundary.center, primaryBoundary.radius) == nullptr) {
                continue;
            }

            for (const auto& transitionBoundary : planeBoundaries) {
                if (CircularBoundaryMatches(transitionBoundary, primaryBoundary.center, primaryBoundary.radius)) {
                    continue;
                }
                if (std::abs(transitionBoundary.radius - primaryBoundary.radius) <= Precision::Confusion()) {
                    continue;
                }

                std::vector<TopoDS_Face> cylinderFaces;
                CollectAdjacentFaces(edgeToFaces, transitionBoundary.edge, cylinderFaces, &planeFace);

                for (const auto& candidateCylinderFace : cylinderFaces) {
                    if (candidateCylinderFace.IsSame(primary.face)) {
                        continue;
                    }

                    BRepAdaptor_Surface candidateSurface(candidateCylinderFace, false);
                    if (candidateSurface.GetType() != GeomAbs_Cylinder) {
                        continue;
                    }

                    HoleCylinderDescriptor secondary;
                    lifecycle = ResolveHoleCylinderDescriptor(geometryShape, candidateCylinderFace, secondary);
                    if (!lifecycle.ok) {
                        continue;
                    }

                    if (!IsSameAxis(
                            primary.cylinder.Location(),
                            primary.axisDirection,
                            secondary.cylinder.Location(),
                            secondary.axisDirection)) {
                        continue;
                    }

                    const CircularBoundaryInfo* secondaryTransition = FindMatchingBoundary(
                        secondary.boundaries,
                        transitionBoundary.center,
                        transitionBoundary.radius
                    );
                    if (secondaryTransition == nullptr) {
                        continue;
                    }

                    const CircularBoundaryInfo* primaryFar = FindOtherBoundary(
                        primary.boundaries,
                        primaryBoundary.center,
                        primaryBoundary.radius
                    );
                    const CircularBoundaryInfo* secondaryFar = FindOtherBoundary(
                        secondary.boundaries,
                        secondaryTransition->center,
                        secondaryTransition->radius
                    );
                    if (primaryFar == nullptr || secondaryFar == nullptr) {
                        continue;
                    }

                    const bool primaryIsLarger = primary.cylinder.Radius() > secondary.cylinder.Radius();
                    if (std::abs(primary.cylinder.Radius() - secondary.cylinder.Radius()) <= Precision::Confusion()) {
                        continue;
                    }

                    const HoleCylinderDescriptor& larger = primaryIsLarger ? primary : secondary;
                    const HoleCylinderDescriptor& smaller = primaryIsLarger ? secondary : primary;
                    const CircularBoundaryInfo& largerTransition = primaryIsLarger ? primaryBoundary : *secondaryTransition;
                    const CircularBoundaryInfo& smallerTransition = primaryIsLarger ? *secondaryTransition : primaryBoundary;
                    const CircularBoundaryInfo& largerFar = primaryIsLarger ? *primaryFar : *secondaryFar;
                    const CircularBoundaryInfo& smallerFar = primaryIsLarger ? *secondaryFar : *primaryFar;

                    const double transitionParameter = AxisParameterOnLine(axisOrigin, axisDirection, largerTransition.center);
                    const double largerFarParameter = AxisParameterOnLine(axisOrigin, axisDirection, largerFar.center);
                    const double smallerFarParameter = AxisParameterOnLine(axisOrigin, axisDirection, smallerFar.center);
                    if ((largerFarParameter - transitionParameter) * (smallerFarParameter - transitionParameter) >= 0.0) {
                        continue;
                    }
                    if (!largerFar.isOpen) {
                        continue;
                    }

                    gp_Dir semanticAxis = axisDirection;
                    TryMakeDirection(largerFar.center, smallerFar.center, semanticAxis);

                    const gp_Dir preferredX = primaryIsLarger ? primary.radialDirection : secondary.radialDirection;
                    const gp_Pnt origin = Midpoint(largerFar.center, smallerFar.center);

                    OcctExactCompoundHoleResult candidate;
                    candidate.ok = true;
                    candidate.kind = "compound-hole";
                    candidate.family = "counterbore";
                    candidate.holeDiameter = smaller.cylinder.Radius() * 2.0;
                    candidate.hasHoleDepth = true;
                    candidate.holeDepth = std::abs(largerFarParameter - smallerFarParameter);
                    candidate.hasIsThrough = true;
                    candidate.isThrough = smallerFar.isOpen;
                    candidate.hasAxisDirection = true;
                    candidate.axisDirection = ToArray(semanticAxis);
                    candidate.hasCounterboreDiameter = true;
                    candidate.counterboreDiameter = larger.cylinder.Radius() * 2.0;
                    candidate.hasCounterboreDepth = true;
                    candidate.counterboreDepth = std::abs(largerFarParameter - transitionParameter);
                    if (!BuildFrameFromNormalAndX(origin, semanticAxis, preferredX, candidate.frame)) {
                        continue;
                    }
                    candidate.hasFrame = true;
                    AddCompoundHoleAnchor(candidate, "entry", largerFar.center);
                    AddCompoundHoleAnchor(candidate, "anchor", smallerTransition.center);
                    AddCompoundHoleAnchor(candidate, candidate.isThrough ? "exit" : "bottom", smallerFar.center);
                    result = candidate;

                    OcctLifecycleResult ok;
                    ok.ok = true;
                    return ok;
                }
            }
        }
    }

    return MakeFailure<OcctLifecycleResult>("unsupported-geometry", kCompoundHoleUnsupportedMessage);
}

OcctLifecycleResult TryDescribeExactCountersinkFromCone(
    const TopoDS_Shape& geometryShape,
    const TopoDS_Face& coneFace,
    OcctExactCompoundHoleResult& result)
{
    BRepAdaptor_Surface coneSurface(coneFace, false);
    if (coneSurface.GetType() != GeomAbs_Cone) {
        return MakeFailure<OcctLifecycleResult>("unsupported-geometry", kCompoundHoleUnsupportedMessage);
    }

    const gp_Cone cone = coneSurface.Cone();
    const gp_Dir axisDirection = cone.Axis().Direction();
    std::vector<CircularBoundaryInfo> coneBoundaries;
    OcctLifecycleResult lifecycle = CollectCoaxialCircularBoundaryInfo(
        coneFace,
        cone.Apex(),
        axisDirection,
        coneBoundaries
    );
    if (!lifecycle.ok || coneBoundaries.size() != 2) {
        return MakeFailure<OcctLifecycleResult>("unsupported-geometry", kCompoundHoleUnsupportedMessage);
    }

    lifecycle = ResolveBoundaryOpenStates(geometryShape, axisDirection, coneBoundaries);
    if (!lifecycle.ok) {
        return lifecycle;
    }

    const CircularBoundaryInfo& largerBoundary =
        coneBoundaries[0].radius >= coneBoundaries[1].radius ? coneBoundaries[0] : coneBoundaries[1];
    const CircularBoundaryInfo& smallerBoundary =
        coneBoundaries[0].radius >= coneBoundaries[1].radius ? coneBoundaries[1] : coneBoundaries[0];
    if (std::abs(largerBoundary.radius - smallerBoundary.radius) <= Precision::Confusion()) {
        return MakeFailure<OcctLifecycleResult>("unsupported-geometry", kCompoundHoleUnsupportedMessage);
    }
    if (!largerBoundary.isOpen) {
        return MakeFailure<OcctLifecycleResult>("unsupported-geometry", kCompoundHoleUnsupportedMessage);
    }

    TopTools_IndexedDataMapOfShapeListOfShape edgeToFaces;
    TopExp::MapShapesAndAncestors(geometryShape, TopAbs_EDGE, TopAbs_FACE, edgeToFaces);

    std::vector<TopoDS_Face> cylinderFaces;
    CollectAdjacentFaces(edgeToFaces, smallerBoundary.edge, cylinderFaces, &coneFace);
    for (const auto& cylinderFace : cylinderFaces) {
        BRepAdaptor_Surface cylinderSurface(cylinderFace, false);
        if (cylinderSurface.GetType() != GeomAbs_Cylinder) {
            continue;
        }

        HoleCylinderDescriptor cylinder;
        lifecycle = ResolveHoleCylinderDescriptor(geometryShape, cylinderFace, cylinder);
        if (!lifecycle.ok) {
            continue;
        }

        if (!IsSameAxis(cone.Apex(), axisDirection, cylinder.cylinder.Location(), cylinder.axisDirection)) {
            continue;
        }

        const CircularBoundaryInfo* sharedBoundary = FindMatchingBoundary(
            cylinder.boundaries,
            smallerBoundary.center,
            smallerBoundary.radius
        );
        const CircularBoundaryInfo* farBoundary = FindOtherBoundary(
            cylinder.boundaries,
            smallerBoundary.center,
            smallerBoundary.radius
        );
        if (sharedBoundary == nullptr || farBoundary == nullptr) {
            continue;
        }

        gp_Dir semanticAxis = axisDirection;
        TryMakeDirection(largerBoundary.center, farBoundary->center, semanticAxis);

        const gp_Pnt samplePoint = SampleFacePoint(coneFace, coneSurface);
        const double sampleParameter = AxisParameterOnLine(cone.Apex(), axisDirection, samplePoint);
        const gp_Pnt axisPoint = cone.Apex().Translated(gp_Vec(axisDirection) * sampleParameter);
        gp_Dir radialDirection;
        if (!TryMakeDirection(axisPoint, samplePoint, radialDirection)) {
            continue;
        }

        OcctExactCompoundHoleResult candidate;
        candidate.ok = true;
        candidate.kind = "compound-hole";
        candidate.family = "countersink";
        candidate.holeDiameter = cylinder.cylinder.Radius() * 2.0;
        candidate.hasHoleDepth = true;
        candidate.holeDepth = std::abs(
            AxisParameterOnLine(cone.Apex(), semanticAxis, largerBoundary.center)
            - AxisParameterOnLine(cone.Apex(), semanticAxis, farBoundary->center)
        );
        candidate.hasIsThrough = true;
        candidate.isThrough = farBoundary->isOpen;
        candidate.hasAxisDirection = true;
        candidate.axisDirection = ToArray(semanticAxis);
        candidate.hasCountersinkDiameter = true;
        candidate.countersinkDiameter = largerBoundary.radius * 2.0;
        const double countersinkAngle = 2.0 * std::abs(cone.SemiAngle());
        if (countersinkAngle <= Precision::Angular()) {
            continue;
        }
        candidate.hasCountersinkAngle = true;
        candidate.countersinkAngle = countersinkAngle;
        const gp_Pnt origin = Midpoint(largerBoundary.center, farBoundary->center);
        if (!BuildFrameFromNormalAndX(origin, semanticAxis, radialDirection, candidate.frame)) {
            continue;
        }
        candidate.hasFrame = true;
        AddCompoundHoleAnchor(candidate, "entry", largerBoundary.center);
        AddCompoundHoleAnchor(candidate, "anchor", smallerBoundary.center);
        AddCompoundHoleAnchor(candidate, candidate.isThrough ? "exit" : "bottom", farBoundary->center);
        result = candidate;

        OcctLifecycleResult ok;
        ok.ok = true;
        return ok;
    }

    return MakeFailure<OcctLifecycleResult>("unsupported-geometry", kCompoundHoleUnsupportedMessage);
}

OcctLifecycleResult TryDescribeExactCountersinkFromCylinder(
    const TopoDS_Shape& geometryShape,
    const TopoDS_Face& cylinderFace,
    OcctExactCompoundHoleResult& result)
{
    HoleCylinderDescriptor cylinder;
    OcctLifecycleResult lifecycle = ResolveHoleCylinderDescriptor(geometryShape, cylinderFace, cylinder);
    if (!lifecycle.ok) {
        return lifecycle;
    }

    TopTools_IndexedDataMapOfShapeListOfShape edgeToFaces;
    TopExp::MapShapesAndAncestors(geometryShape, TopAbs_EDGE, TopAbs_FACE, edgeToFaces);

    for (const auto& boundary : cylinder.boundaries) {
        std::vector<TopoDS_Face> adjacentFaces;
        CollectAdjacentFaces(edgeToFaces, boundary.edge, adjacentFaces, &cylinder.face);
        for (const auto& adjacentFace : adjacentFaces) {
            BRepAdaptor_Surface adjacentSurface(adjacentFace, false);
            if (adjacentSurface.GetType() != GeomAbs_Cone) {
                continue;
            }

            lifecycle = TryDescribeExactCountersinkFromCone(geometryShape, adjacentFace, result);
            if (lifecycle.ok) {
                return lifecycle;
            }
            if (lifecycle.code != "unsupported-geometry") {
                return lifecycle;
            }
        }
    }

    return MakeFailure<OcctLifecycleResult>("unsupported-geometry", kCompoundHoleUnsupportedMessage);
}

OcctLifecycleResult CollectCompoundHoleCandidateFaces(
    int exactModelId,
    int exactShapeHandle,
    const std::string& kind,
    int elementId,
    TopoDS_Shape& geometryShape,
    std::vector<TopoDS_Face>& candidateFaces)
{
    candidateFaces.clear();

    OcctLifecycleResult lifecycle = LookupGeometryShape(exactModelId, exactShapeHandle, geometryShape);
    if (!lifecycle.ok) {
        return lifecycle;
    }

    const std::string normalizedKind = NormalizeKind(kind);
    if (normalizedKind == "face") {
        TopoDS_Face face;
        lifecycle = ResolveFace(exactModelId, exactShapeHandle, elementId, face);
        if (!lifecycle.ok) {
            return lifecycle;
        }

        BRepAdaptor_Surface surface(face, false);
        if (surface.GetType() == GeomAbs_Cylinder || surface.GetType() == GeomAbs_Cone) {
            candidateFaces.push_back(face);
            OcctLifecycleResult ok;
            ok.ok = true;
            return ok;
        }

        return MakeFailure<OcctLifecycleResult>("unsupported-geometry", kCompoundHoleUnsupportedMessage);
    }

    if (normalizedKind != "edge") {
        return MakeFailure<OcctLifecycleResult>("unsupported-kind", "Exact compound-hole helper only supports edge or face refs.");
    }

    TopoDS_Edge edge;
    lifecycle = ResolveEdge(exactModelId, exactShapeHandle, elementId, edge);
    if (!lifecycle.ok) {
        return lifecycle;
    }

    BRepAdaptor_Curve curve(edge);
    if (curve.GetType() != GeomAbs_Circle) {
        return MakeFailure<OcctLifecycleResult>("unsupported-geometry", kCompoundHoleUnsupportedMessage);
    }

    TopTools_IndexedDataMapOfShapeListOfShape edgeToFaces;
    TopExp::MapShapesAndAncestors(geometryShape, TopAbs_EDGE, TopAbs_FACE, edgeToFaces);
    CollectAdjacentFaces(edgeToFaces, edge, candidateFaces);
    candidateFaces.erase(
        std::remove_if(candidateFaces.begin(), candidateFaces.end(), [](const TopoDS_Face& face) {
            BRepAdaptor_Surface surface(face, false);
            return surface.GetType() != GeomAbs_Cylinder && surface.GetType() != GeomAbs_Cone;
        }),
        candidateFaces.end()
    );
    if (candidateFaces.empty()) {
        return MakeFailure<OcctLifecycleResult>("unsupported-geometry", kCompoundHoleUnsupportedMessage);
    }

    OcctLifecycleResult ok;
    ok.ok = true;
    return ok;
}

gp_Pnt ProjectPointOntoLine(const gp_Lin& line, const gp_Pnt& point)
{
    const gp_Vec offset(line.Location(), point);
    const double parameter = offset.Dot(gp_Vec(line.Direction()));
    return line.Location().Translated(gp_Vec(line.Direction()) * parameter);
}

bool TryIntersectPlanes(const gp_Pln& planeA, const gp_Pln& planeB, gp_Lin& intersection)
{
    const gp_Vec normalA(planeA.Axis().Direction().XYZ());
    const gp_Vec normalB(planeB.Axis().Direction().XYZ());
    const gp_Vec directionVector(normalA.Crossed(normalB));
    const double denominator = directionVector.SquareMagnitude();
    if (denominator <= Precision::Confusion()) {
        return false;
    }

    const double constantA = normalA.Dot(gp_Vec(planeA.Location().XYZ()));
    const double constantB = normalB.Dot(gp_Vec(planeB.Location().XYZ()));
    const gp_Vec pointVector =
        normalB.Crossed(directionVector) * (constantA / denominator)
        + directionVector.Crossed(normalA) * (constantB / denominator);

    intersection = gp_Lin(
        gp_Pnt(pointVector.X(), pointVector.Y(), pointVector.Z()),
        gp_Dir(directionVector)
    );
    return true;
}

struct ChamferSupportCandidate {
    TopoDS_Face face;
    TopoDS_Edge edge;
    gp_Pln plane;
    gp_Dir normal;
    gp_Lin boundaryLine;
};

OcctLifecycleResult ResolveChamferCandidateFace(
    int exactModelId,
    int exactShapeHandle,
    const std::string& kind,
    int elementId,
    TopoDS_Shape& geometryShape,
    TopoDS_Face& chamferFace)
{
    OcctLifecycleResult lifecycle = LookupGeometryShape(exactModelId, exactShapeHandle, geometryShape);
    if (!lifecycle.ok) {
        return lifecycle;
    }

    if (NormalizeKind(kind) != "face") {
        return MakeFailure<OcctLifecycleResult>(
            "unsupported-kind",
            "Exact chamfer helper only supports face refs."
        );
    }

    lifecycle = ResolveFace(exactModelId, exactShapeHandle, elementId, chamferFace);
    if (!lifecycle.ok) {
        return lifecycle;
    }

    BRepAdaptor_Surface surface(chamferFace, false);
    if (surface.GetType() != GeomAbs_Plane) {
        return MakeFailure<OcctLifecycleResult>(
            "unsupported-geometry",
            "Exact chamfer helper only supports planar chamfer face refs."
        );
    }

    OcctLifecycleResult ok;
    ok.ok = true;
    return ok;
}

OcctLifecycleResult CollectChamferSupportCandidates(
    const TopoDS_Shape& geometryShape,
    const TopoDS_Face& chamferFace,
    const gp_Pln& chamferPlane,
    std::vector<ChamferSupportCandidate>& supports)
{
    TopTools_IndexedDataMapOfShapeListOfShape edgeToFaces;
    TopExp::MapShapesAndAncestors(geometryShape, TopAbs_EDGE, TopAbs_FACE, edgeToFaces);

    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(chamferFace, TopAbs_EDGE, edges);

    const gp_Dir chamferNormal = chamferPlane.Axis().Direction();
    for (int edgeIndex = 1; edgeIndex <= edges.Extent(); edgeIndex += 1) {
        const TopoDS_Edge edge = TopoDS::Edge(edges(edgeIndex));
        if (edge.IsNull() || !edgeToFaces.Contains(edge)) {
            continue;
        }

        BRepAdaptor_Curve boundaryCurve(edge);
        if (boundaryCurve.GetType() != GeomAbs_Line) {
            continue;
        }

        for (TopTools_ListIteratorOfListOfShape it(edgeToFaces.FindFromKey(edge)); it.More(); it.Next()) {
            const TopoDS_Face candidateFace = TopoDS::Face(it.Value());
            if (candidateFace.IsNull() || candidateFace.IsSame(chamferFace)) {
                continue;
            }

            BRepAdaptor_Surface supportSurface(candidateFace, false);
            if (supportSurface.GetType() != GeomAbs_Plane) {
                continue;
            }

            const gp_Pln supportPlane = supportSurface.Plane();
            const gp_Dir supportNormal = supportPlane.Axis().Direction();
            if (supportNormal.IsParallel(chamferNormal, Precision::Angular())
                || std::abs(CanonicalAngle(supportNormal.Angle(chamferNormal)) - 1.5707963267948966) <= Precision::Angular()) {
                continue;
            }

            bool duplicate = false;
            for (const auto& existing : supports) {
                if (existing.face.IsSame(candidateFace)) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                continue;
            }

            ChamferSupportCandidate candidate;
            candidate.face = candidateFace;
            candidate.edge = edge;
            candidate.plane = supportPlane;
            candidate.normal = supportNormal;
            candidate.boundaryLine = boundaryCurve.Line();
            supports.push_back(candidate);
        }
    }

    if (supports.size() != 2) {
        return MakeFailure<OcctLifecycleResult>(
            "unsupported-geometry",
            "Exact chamfer helper requires one planar chamfer face with exactly two oblique planar support faces."
        );
    }

    if (supports[0].normal.IsParallel(supports[1].normal, Precision::Angular())) {
        return MakeFailure<OcctLifecycleResult>(
            "unsupported-geometry",
            "Exact chamfer helper requires two non-parallel planar support faces."
        );
    }

    OcctLifecycleResult ok;
    ok.ok = true;
    return ok;
}

template <typename TFailure>
OcctExactPlacementResult ConvertPlacementFailure(const TFailure& failure)
{
    return MakeFailure<OcctExactPlacementResult>(failure.code, failure.message);
}

OcctExactRelationResult MakeRelationSuccess(const std::string& kind)
{
    OcctExactRelationResult result;
    result.ok = true;
    result.kind = kind;
    return result;
}

bool SetRelationFrameFromNormalAndX(
    OcctExactRelationResult& result,
    const gp_Pnt& origin,
    const gp_Dir& normal,
    const gp_Dir& preferredX)
{
    OcctExactPlacementFrame frame;
    if (!BuildFrameFromNormalAndX(origin, normal, preferredX, frame)) {
        return false;
    }
    result.frame = frame;
    result.hasFrame = true;
    return true;
}

bool SetRelationFrameFromXAxis(
    OcctExactRelationResult& result,
    const gp_Pnt& origin,
    const gp_Dir& xAxis)
{
    OcctExactPlacementFrame frame;
    if (!BuildFrameFromXAxis(origin, xAxis, frame)) {
        return false;
    }
    result.frame = frame;
    result.hasFrame = true;
    return true;
}

void SetRelationDirections(OcctExactRelationResult& result, const gp_Dir& directionA, const gp_Dir& directionB)
{
    result.directionA = ToArray(directionA);
    result.directionB = ToArray(directionB);
    result.hasDirectionA = true;
    result.hasDirectionB = true;
}

void SetRelationAxis(OcctExactRelationResult& result, const gp_Dir& axisDirection)
{
    result.axisDirection = ToArray(axisDirection);
    result.hasAxisDirection = true;
}

void SetRelationCenter(OcctExactRelationResult& result, const gp_Pnt& centerPoint)
{
    result.center = ToArray(centerPoint);
    result.hasCenter = true;
}

void SetRelationTangentPoint(OcctExactRelationResult& result, const gp_Pnt& tangentPoint)
{
    result.tangentPoint = ToArray(tangentPoint);
    result.hasTangentPoint = true;
}

template <typename TResult>
TResult MakeUnsupportedKind(const std::string& message)
{
    return MakeFailure<TResult>("unsupported-geometry", message);
}

template <typename TResult>
TResult MakePairwiseUnsupported()
{
    return MakeFailure<TResult>(
        "unsupported-geometry",
        "Exact thickness only supports parallel planar face pairs."
    );
}

OcctLifecycleResult ResolveFaceProjection(
    const TopoDS_Face& face,
    const gp_Pnt& queryPoint,
    gp_Pnt& projectedPoint,
    double& u,
    double& v)
{
    TopLoc_Location surfaceLocation;
    Handle(Geom_Surface) surface = BRep_Tool::Surface(face, surfaceLocation);
    if (surface.IsNull()) {
        return MakeFailure<OcctLifecycleResult>("unsupported-geometry", "Exact face normal is not supported for faces without an evaluable surface.");
    }

    double uFirst = 0.0;
    double uLast = 0.0;
    double vFirst = 0.0;
    double vLast = 0.0;
    BRepTools::UVBounds(face, uFirst, uLast, vFirst, vLast);

    GeomAPI_ProjectPointOnSurf projector(
        queryPoint,
        surface,
        uFirst,
        uLast,
        vFirst,
        vLast,
        Precision::Confusion()
    );
    if (!projector.IsDone() || projector.NbPoints() <= 0) {
        return MakeFailure<OcctLifecycleResult>("query-out-of-range", "Exact face normal query point does not project onto the trimmed face.");
    }

    projectedPoint = projector.NearestPoint();
    projector.LowerDistanceParameters(u, v);

    BRepClass_FaceClassifier classifier(face, projectedPoint, Precision::Confusion());
    if (classifier.State() != TopAbs_IN && classifier.State() != TopAbs_ON) {
        return MakeFailure<OcctLifecycleResult>("query-out-of-range", "Exact face normal query point does not project onto the trimmed face.");
    }

    OcctLifecycleResult ok;
    ok.ok = true;
    return ok;
}

OcctLifecycleResult ResolveCircularPlacementSupport(
    int exactModelId,
    int exactShapeHandle,
    const std::string& kind,
    int elementId,
    double& radiusValue,
    gp_Pnt& centerPoint,
    gp_Pnt& anchorPoint,
    gp_Dir& axisDirection,
    gp_Dir& radialDirection)
{
    const OcctExactRadiusResult radius = MeasureExactRadius(exactModelId, exactShapeHandle, kind, elementId);
    if (!radius.ok) {
        return MakeFailure<OcctLifecycleResult>(radius.code, radius.message);
    }

    if (radius.family != "circle" && radius.family != "cylinder") {
        return MakeFailure<OcctLifecycleResult>(
            "unsupported-geometry",
            "Exact circular placement is only supported for circle or cylinder geometry."
        );
    }

    if (!TryMakeDirection(radius.localAxisDirection, axisDirection)) {
        return MakeFailure<OcctLifecycleResult>(
            "unsupported-geometry",
            "Exact circular placement requires geometry with a stable axis."
        );
    }

    const gp_Pnt rawCenter = ToPoint(radius.localCenter);
    const gp_Pnt rawAnchor = ToPoint(radius.localAnchorPoint);

    centerPoint = rawCenter;
    if (radius.family == "cylinder") {
        const gp_Vec centerToAnchor(rawCenter, rawAnchor);
        centerPoint = rawCenter.Translated(gp_Vec(axisDirection) * centerToAnchor.Dot(gp_Vec(axisDirection)));
    }

    if (!TryMakeDirection(centerPoint, rawAnchor, radialDirection)) {
        return MakeFailure<OcctLifecycleResult>(
            "unsupported-geometry",
            "Exact circular placement requires geometry with a stable radial direction."
        );
    }

    radiusValue = radius.radius;
    anchorPoint = rawAnchor;

    OcctLifecycleResult ok;
    ok.ok = true;
    return ok;
}

gp_Pnt SampleEdgePoint(BRepAdaptor_Curve& curve)
{
    return curve.Value(0.5 * (curve.FirstParameter() + curve.LastParameter()));
}

bool IsPerpendicular(const gp_Dir& left, const gp_Dir& right)
{
    return std::abs(CanonicalAngle(left.Angle(right)) - 1.5707963267948966) <= Precision::Angular();
}

OcctExactRelationResult ClassifyLineLineRelation(const TopoDS_Edge& edgeA, const TopoDS_Edge& edgeB)
{
    BRepAdaptor_Curve curveA(edgeA);
    BRepAdaptor_Curve curveB(edgeB);
    if (curveA.GetType() != GeomAbs_Line || curveB.GetType() != GeomAbs_Line) {
        return MakeFailure<OcctExactRelationResult>(
            "unsupported-geometry",
            "Exact relation classification only supports line/line, plane/plane, circle/circle, and circle/cylinder analytic pairs."
        );
    }

    const gp_Lin lineA = curveA.Line();
    const gp_Lin lineB = curveB.Line();
    const gp_Dir directionA = lineA.Direction();
    const gp_Dir directionB = lineB.Direction();

    double distanceValue = 0.0;
    gp_Pnt pointA;
    gp_Pnt pointB;
    OcctLifecycleResult lifecycle = MeasureMinimumDistance(edgeA, edgeB, distanceValue, pointA, pointB);
    if (!lifecycle.ok) {
        return ConvertFailure<OcctExactRelationResult>(lifecycle);
    }

    if (directionA.IsParallel(directionB, Precision::Angular())) {
        OcctExactRelationResult result = MakeRelationSuccess("parallel");
        SetRelationDirections(result, directionA, directionB);
        const gp_Pnt origin = Midpoint(pointA, pointB);
        gp_Dir frameNormal;
        if (distanceValue > Precision::Confusion()
            && TryMakeDirection(pointA, pointB, frameNormal)
            && SetRelationFrameFromNormalAndX(result, origin, frameNormal, directionA)) {
            // Stable separated parallel frame established.
        } else if (!SetRelationFrameFromXAxis(result, origin, directionA)) {
            return MakeFailure<OcctExactRelationResult>(
                "insufficient-precision",
                "Exact parallel relation requires a stable working frame."
            );
        }
        AddRelationAnchor(result, "attach", pointA);
        AddRelationAnchor(result, "attach", pointB);
        return result;
    }

    if (IsPerpendicular(directionA, directionB)) {
        const gp_Vec normalVector(directionA.XYZ().Crossed(directionB.XYZ()));
        if (normalVector.Magnitude() <= Precision::Confusion()) {
            return MakeFailure<OcctExactRelationResult>(
                "insufficient-precision",
                "Exact perpendicular relation requires a stable working frame."
            );
        }

        OcctExactRelationResult result = MakeRelationSuccess("perpendicular");
        SetRelationDirections(result, directionA, directionB);
        if (!SetRelationFrameFromNormalAndX(result, Midpoint(pointA, pointB), gp_Dir(normalVector), directionA)) {
            return MakeFailure<OcctExactRelationResult>(
                "insufficient-precision",
                "Exact perpendicular relation requires a stable working frame."
            );
        }
        AddRelationAnchor(result, "attach", pointA);
        AddRelationAnchor(result, "attach", pointB);
        return result;
    }

    return MakeRelationSuccess("none");
}

OcctExactRelationResult ClassifyPlanePlaneRelation(const TopoDS_Face& faceA, const TopoDS_Face& faceB)
{
    BRepAdaptor_Surface surfaceA(faceA, false);
    BRepAdaptor_Surface surfaceB(faceB, false);
    if (surfaceA.GetType() != GeomAbs_Plane || surfaceB.GetType() != GeomAbs_Plane) {
        return MakeFailure<OcctExactRelationResult>(
            "unsupported-geometry",
            "Exact relation classification only supports line/line, plane/plane, circle/circle, and circle/cylinder analytic pairs."
        );
    }

    const gp_Pln planeA = surfaceA.Plane();
    const gp_Pln planeB = surfaceB.Plane();
    const gp_Dir directionA = planeA.Axis().Direction();
    const gp_Dir directionB = planeB.Axis().Direction();

    double distanceValue = 0.0;
    gp_Pnt pointA;
    gp_Pnt pointB;
    OcctLifecycleResult lifecycle = MeasureMinimumDistance(faceA, faceB, distanceValue, pointA, pointB);
    if (!lifecycle.ok) {
        return ConvertFailure<OcctExactRelationResult>(lifecycle);
    }

    if (directionA.IsParallel(directionB, Precision::Angular())) {
        OcctExactRelationResult result = MakeRelationSuccess("parallel");
        SetRelationDirections(result, directionA, directionB);
        if (!SetRelationFrameFromNormalAndX(result, Midpoint(pointA, pointB), directionA, ChooseStableReferenceDirection(directionA))) {
            return MakeFailure<OcctExactRelationResult>(
                "insufficient-precision",
                "Exact parallel relation requires a stable working frame."
            );
        }
        AddRelationAnchor(result, "attach", pointA);
        AddRelationAnchor(result, "attach", pointB);
        return result;
    }

    if (IsPerpendicular(directionA, directionB)) {
        const gp_Vec normalVector(directionA.XYZ().Crossed(directionB.XYZ()));
        if (normalVector.Magnitude() <= Precision::Confusion()) {
            return MakeFailure<OcctExactRelationResult>(
                "insufficient-precision",
                "Exact perpendicular relation requires a stable working frame."
            );
        }

        OcctExactRelationResult result = MakeRelationSuccess("perpendicular");
        SetRelationDirections(result, directionA, directionB);
        if (!SetRelationFrameFromNormalAndX(result, Midpoint(pointA, pointB), gp_Dir(normalVector), directionA)) {
            return MakeFailure<OcctExactRelationResult>(
                "insufficient-precision",
                "Exact perpendicular relation requires a stable working frame."
            );
        }
        AddRelationAnchor(result, "attach", pointA);
        AddRelationAnchor(result, "attach", pointB);
        return result;
    }

    return MakeRelationSuccess("none");
}

OcctExactRelationResult ClassifyCircleCircleRelation(const TopoDS_Edge& edgeA, const TopoDS_Edge& edgeB)
{
    BRepAdaptor_Curve curveA(edgeA);
    BRepAdaptor_Curve curveB(edgeB);
    if (curveA.GetType() != GeomAbs_Circle || curveB.GetType() != GeomAbs_Circle) {
        return MakeFailure<OcctExactRelationResult>(
            "unsupported-geometry",
            "Exact relation classification only supports line/line, plane/plane, circle/circle, and circle/cylinder analytic pairs."
        );
    }

    const gp_Circ circleA = curveA.Circle();
    const gp_Circ circleB = curveB.Circle();
    const gp_Pnt centerA = circleA.Location();
    const gp_Pnt centerB = circleB.Location();
    const gp_Dir axisA = circleA.Axis().Direction();
    const gp_Dir axisB = circleB.Axis().Direction();
    const gp_Pnt anchorA = SampleEdgePoint(curveA);
    const gp_Pnt anchorB = SampleEdgePoint(curveB);
    const double radiusA = circleA.Radius();
    const double radiusB = circleB.Radius();

    if (!axisA.IsParallel(axisB, Precision::Angular())) {
        return MakeRelationSuccess("none");
    }

    const gp_Vec centerDelta(centerA, centerB);
    const double axialOffset = std::abs(centerDelta.Dot(gp_Vec(axisA)));
    gp_Vec planarDelta = centerDelta - gp_Vec(axisA) * centerDelta.Dot(gp_Vec(axisA));
    const double planarDistance = planarDelta.Magnitude();
    const double tolerance = Precision::Confusion();

    if (centerA.Distance(centerB) <= tolerance) {
        gp_Dir preferredX = ChooseStableReferenceDirection(axisA);
        TryMakeDirection(centerA, anchorA, preferredX);

        OcctExactRelationResult result = MakeRelationSuccess("concentric");
        SetRelationCenter(result, centerA);
        SetRelationAxis(result, axisA);
        if (!SetRelationFrameFromNormalAndX(result, centerA, axisA, preferredX)) {
            return MakeFailure<OcctExactRelationResult>(
                "insufficient-precision",
                "Exact concentric relation requires a stable working frame."
            );
        }
        AddRelationAnchor(result, "center", centerA);
        AddRelationAnchor(result, "anchor", anchorA);
        AddRelationAnchor(result, "anchor", anchorB);
        return result;
    }

    if (axialOffset <= tolerance && planarDistance > tolerance) {
        const gp_Dir centerDirection(planarDelta);
        const double externalTangentDelta = std::abs(planarDistance - (radiusA + radiusB));
        const double internalTangentDelta = std::abs(planarDistance - std::abs(radiusA - radiusB));
        if (externalTangentDelta <= tolerance || internalTangentDelta <= tolerance) {
            gp_Dir tangentRadial = centerDirection;
            gp_Pnt tangentPoint = centerA.Translated(gp_Vec(centerDirection) * radiusA);
            if (internalTangentDelta <= tolerance && radiusB > radiusA) {
                tangentRadial.Reverse();
                tangentPoint = centerA.Translated(gp_Vec(tangentRadial) * radiusA);
            }

            OcctExactRelationResult result = MakeRelationSuccess("tangent");
            SetRelationAxis(result, axisA);
            SetRelationTangentPoint(result, tangentPoint);
            if (!SetRelationFrameFromNormalAndX(result, tangentPoint, axisA, tangentRadial)) {
                return MakeFailure<OcctExactRelationResult>(
                    "insufficient-precision",
                    "Exact tangent relation requires a stable working frame."
                );
            }
            AddRelationAnchor(result, "center", centerA);
            AddRelationAnchor(result, "center", centerB);
            AddRelationAnchor(result, "anchor", tangentPoint);
            return result;
        }
    }

    return MakeRelationSuccess("none");
}

OcctExactRelationResult ClassifyCircleCylinderRelation(const TopoDS_Edge& edge, const TopoDS_Face& face)
{
    BRepAdaptor_Curve curve(edge);
    BRepAdaptor_Surface surface(face, false);
    if (curve.GetType() != GeomAbs_Circle || surface.GetType() != GeomAbs_Cylinder) {
        return MakeFailure<OcctExactRelationResult>(
            "unsupported-geometry",
            "Exact relation classification only supports line/line, plane/plane, circle/circle, and circle/cylinder analytic pairs."
        );
    }

    const gp_Circ circle = curve.Circle();
    const gp_Cylinder cylinder = surface.Cylinder();
    const gp_Dir circleAxis = circle.Axis().Direction();
    const gp_Dir cylinderAxis = cylinder.Axis().Direction();
    if (!circleAxis.IsParallel(cylinderAxis, Precision::Angular())) {
        return MakeRelationSuccess("none");
    }

    const gp_Lin cylinderAxisLine(cylinder.Location(), cylinderAxis);
    if (cylinderAxisLine.Distance(circle.Location()) > Precision::Confusion()
        || std::abs(circle.Radius() - cylinder.Radius()) > Precision::Confusion()) {
        return MakeRelationSuccess("none");
    }

    gp_Dir preferredX = ChooseStableReferenceDirection(circleAxis);
    const gp_Pnt anchorPoint = SampleEdgePoint(curve);
    TryMakeDirection(circle.Location(), anchorPoint, preferredX);

    OcctExactRelationResult result = MakeRelationSuccess("concentric");
    SetRelationCenter(result, circle.Location());
    SetRelationAxis(result, circleAxis);
    if (!SetRelationFrameFromNormalAndX(result, circle.Location(), circleAxis, preferredX)) {
        return MakeFailure<OcctExactRelationResult>(
            "insufficient-precision",
            "Exact concentric relation requires a stable working frame."
        );
    }
    AddRelationAnchor(result, "center", circle.Location());
    AddRelationAnchor(result, "anchor", anchorPoint);
    return result;
}

} // namespace

OcctExactGeometryTypeResult GetExactGeometryType(
    int exactModelId,
    int exactShapeHandle,
    const std::string& kind,
    int elementId)
{
    try {
        const std::string normalizedKind = NormalizeKind(kind);
        if (normalizedKind == "face") {
            TopoDS_Face face;
            OcctLifecycleResult lifecycle = ResolveFace(exactModelId, exactShapeHandle, elementId, face);
            if (!lifecycle.ok) {
                return ConvertFailure<OcctExactGeometryTypeResult>(lifecycle);
            }

            BRepAdaptor_Surface surface(face, false);
            OcctExactGeometryTypeResult result;
            result.ok = true;
            result.family = FamilyFromSurfaceType(surface.GetType());
            return result;
        }

        if (normalizedKind == "edge") {
            TopoDS_Edge edge;
            OcctLifecycleResult lifecycle = ResolveEdge(exactModelId, exactShapeHandle, elementId, edge);
            if (!lifecycle.ok) {
                return ConvertFailure<OcctExactGeometryTypeResult>(lifecycle);
            }

            BRepAdaptor_Curve curve(edge);
            OcctExactGeometryTypeResult result;
            result.ok = true;
            result.family = FamilyFromCurveType(curve.GetType());
            return result;
        }

        return MakeFailure<OcctExactGeometryTypeResult>("unsupported-kind", "Exact geometry type is only supported for face or edge refs.");
    } catch (const Standard_Failure& failure) {
        return MakeFailure<OcctExactGeometryTypeResult>(
            "internal-error",
            failure.GetMessageString() != nullptr ? failure.GetMessageString() : "OCCT exact geometry type query failed."
        );
    }
}

OcctExactRadiusResult MeasureExactRadius(
    int exactModelId,
    int exactShapeHandle,
    const std::string& kind,
    int elementId)
{
    try {
        const std::string normalizedKind = NormalizeKind(kind);
        if (normalizedKind == "edge") {
            TopoDS_Edge edge;
            OcctLifecycleResult lifecycle = ResolveEdge(exactModelId, exactShapeHandle, elementId, edge);
            if (!lifecycle.ok) {
                return ConvertFailure<OcctExactRadiusResult>(lifecycle);
            }

            BRepAdaptor_Curve curve(edge);
            if (curve.GetType() != GeomAbs_Circle) {
                return MakeFailure<OcctExactRadiusResult>("unsupported-geometry", "Exact radius is not supported for the requested element.");
            }

            gp_Circ circle = curve.Circle();
            const double midParam = 0.5 * (curve.FirstParameter() + curve.LastParameter());
            OcctExactRadiusResult result;
            result.ok = true;
            result.family = "circle";
            result.radius = circle.Radius();
            result.diameter = result.radius * 2.0;
            result.localCenter = ToArray(circle.Location());
            result.localAnchorPoint = ToArray(curve.Value(midParam));
            result.localAxisDirection = ToArray(circle.Axis().Direction());
            return result;
        }

        if (normalizedKind == "face") {
            TopoDS_Face face;
            OcctLifecycleResult lifecycle = ResolveFace(exactModelId, exactShapeHandle, elementId, face);
            if (!lifecycle.ok) {
                return ConvertFailure<OcctExactRadiusResult>(lifecycle);
            }

            BRepAdaptor_Surface surface(face, false);
            OcctExactRadiusResult result;
            if (surface.GetType() == GeomAbs_Cylinder) {
                gp_Cylinder cylinder = surface.Cylinder();
                result.ok = true;
                result.family = "cylinder";
                result.radius = cylinder.Radius();
                result.diameter = result.radius * 2.0;
                result.localCenter = ToArray(cylinder.Location());
                result.localAnchorPoint = ToArray(SampleFacePoint(face, surface));
                result.localAxisDirection = ToArray(cylinder.Axis().Direction());
                return result;
            }

            if (surface.GetType() == GeomAbs_Sphere) {
                gp_Sphere sphere = surface.Sphere();
                result.ok = true;
                result.family = "sphere";
                result.radius = sphere.Radius();
                result.diameter = result.radius * 2.0;
                result.localCenter = ToArray(sphere.Location());
                result.localAnchorPoint = ToArray(SampleFacePoint(face, surface));
                result.localAxisDirection = { 0.0, 0.0, 0.0 };
                return result;
            }

            return MakeFailure<OcctExactRadiusResult>("unsupported-geometry", "Exact radius is not supported for the requested element.");
        }

        return MakeFailure<OcctExactRadiusResult>("unsupported-kind", "Exact radius is only supported for face or edge refs.");
    } catch (const Standard_Failure& failure) {
        return MakeFailure<OcctExactRadiusResult>(
            "internal-error",
            failure.GetMessageString() != nullptr ? failure.GetMessageString() : "OCCT exact radius query failed."
        );
    }
}

OcctExactCenterResult MeasureExactCenter(
    int exactModelId,
    int exactShapeHandle,
    const std::string& kind,
    int elementId)
{
    try {
        const std::string normalizedKind = NormalizeKind(kind);
        if (normalizedKind == "edge") {
            TopoDS_Edge edge;
            OcctLifecycleResult lifecycle = ResolveEdge(exactModelId, exactShapeHandle, elementId, edge);
            if (!lifecycle.ok) {
                return ConvertFailure<OcctExactCenterResult>(lifecycle);
            }

            BRepAdaptor_Curve curve(edge);
            if (curve.GetType() != GeomAbs_Circle) {
                return MakeFailure<OcctExactCenterResult>("unsupported-geometry", "Exact center is not supported for the requested element.");
            }

            gp_Circ circle = curve.Circle();
            OcctExactCenterResult result;
            result.ok = true;
            result.family = "circle";
            result.localCenter = ToArray(circle.Location());
            result.localAxisDirection = ToArray(circle.Axis().Direction());
            return result;
        }

        if (normalizedKind == "face") {
            TopoDS_Face face;
            OcctLifecycleResult lifecycle = ResolveFace(exactModelId, exactShapeHandle, elementId, face);
            if (!lifecycle.ok) {
                return ConvertFailure<OcctExactCenterResult>(lifecycle);
            }

            BRepAdaptor_Surface surface(face, false);
            OcctExactCenterResult result;
            switch (surface.GetType()) {
                case GeomAbs_Cylinder: {
                    gp_Cylinder cylinder = surface.Cylinder();
                    result.ok = true;
                    result.family = "cylinder";
                    result.localCenter = ToArray(cylinder.Location());
                    result.localAxisDirection = ToArray(cylinder.Axis().Direction());
                    return result;
                }
                case GeomAbs_Sphere: {
                    gp_Sphere sphere = surface.Sphere();
                    result.ok = true;
                    result.family = "sphere";
                    result.localCenter = ToArray(sphere.Location());
                    result.localAxisDirection = { 0.0, 0.0, 0.0 };
                    return result;
                }
                case GeomAbs_Cone: {
                    gp_Cone cone = surface.Cone();
                    result.ok = true;
                    result.family = "cone";
                    result.localCenter = ToArray(cone.Apex());
                    result.localAxisDirection = ToArray(cone.Axis().Direction());
                    return result;
                }
                case GeomAbs_Torus: {
                    gp_Torus torus = surface.Torus();
                    result.ok = true;
                    result.family = "torus";
                    result.localCenter = ToArray(torus.Location());
                    result.localAxisDirection = ToArray(torus.Axis().Direction());
                    return result;
                }
                default:
                    return MakeFailure<OcctExactCenterResult>("unsupported-geometry", "Exact center is not supported for the requested element.");
            }
        }

        return MakeFailure<OcctExactCenterResult>("unsupported-kind", "Exact center is only supported for face or edge refs.");
    } catch (const Standard_Failure& failure) {
        return MakeFailure<OcctExactCenterResult>(
            "internal-error",
            failure.GetMessageString() != nullptr ? failure.GetMessageString() : "OCCT exact center query failed."
        );
    }
}

OcctExactEdgeLengthResult MeasureExactEdgeLength(
    int exactModelId,
    int exactShapeHandle,
    const std::string& kind,
    int elementId)
{
    try {
        const std::string normalizedKind = NormalizeKind(kind);
        if (normalizedKind != "edge") {
            return MakeUnsupportedKind<OcctExactEdgeLengthResult>("Exact edge length is only supported for edge refs.");
        }

        TopoDS_Edge edge;
        OcctLifecycleResult lifecycle = ResolveEdge(exactModelId, exactShapeHandle, elementId, edge);
        if (!lifecycle.ok) {
            return ConvertFailure<OcctExactEdgeLengthResult>(lifecycle);
        }

        GProp_GProps properties;
        BRepGProp::LinearProperties(edge, properties, false, false);

        BRepAdaptor_Curve curve(edge);
        OcctExactEdgeLengthResult result;
        result.ok = true;
        result.value = properties.Mass();
        result.localStartPoint = ToArray(curve.Value(curve.FirstParameter()));
        result.localEndPoint = ToArray(curve.Value(curve.LastParameter()));
        return result;
    } catch (const Standard_Failure& failure) {
        return MakeFailure<OcctExactEdgeLengthResult>(
            "internal-error",
            failure.GetMessageString() != nullptr ? failure.GetMessageString() : "OCCT exact edge length query failed."
        );
    }
}

OcctExactFaceAreaResult MeasureExactFaceArea(
    int exactModelId,
    int exactShapeHandle,
    const std::string& kind,
    int elementId)
{
    try {
        const std::string normalizedKind = NormalizeKind(kind);
        if (normalizedKind != "face") {
            return MakeUnsupportedKind<OcctExactFaceAreaResult>("Exact face area is only supported for face refs.");
        }

        TopoDS_Face face;
        OcctLifecycleResult lifecycle = ResolveFace(exactModelId, exactShapeHandle, elementId, face);
        if (!lifecycle.ok) {
            return ConvertFailure<OcctExactFaceAreaResult>(lifecycle);
        }

        GProp_GProps properties;
        BRepGProp::SurfaceProperties(face, properties, false, false);

        OcctExactFaceAreaResult result;
        result.ok = true;
        result.value = properties.Mass();
        result.localCentroid = ToArray(properties.CentreOfMass());
        return result;
    } catch (const Standard_Failure& failure) {
        return MakeFailure<OcctExactFaceAreaResult>(
            "internal-error",
            failure.GetMessageString() != nullptr ? failure.GetMessageString() : "OCCT exact face area query failed."
        );
    }
}

OcctExactFaceNormalResult EvaluateExactFaceNormal(
    int exactModelId,
    int exactShapeHandle,
    const std::string& kind,
    int elementId,
    const std::array<double, 3>& localQueryPoint)
{
    try {
        const std::string normalizedKind = NormalizeKind(kind);
        if (normalizedKind != "face") {
            return MakeUnsupportedKind<OcctExactFaceNormalResult>("Exact face normal is only supported for face refs.");
        }

        TopoDS_Face face;
        OcctLifecycleResult lifecycle = ResolveFace(exactModelId, exactShapeHandle, elementId, face);
        if (!lifecycle.ok) {
            return ConvertFailure<OcctExactFaceNormalResult>(lifecycle);
        }

        const gp_Pnt queryPoint = ToPoint(localQueryPoint);
        gp_Pnt projectedPoint;
        double u = 0.0;
        double v = 0.0;
        lifecycle = ResolveFaceProjection(face, queryPoint, projectedPoint, u, v);
        if (!lifecycle.ok) {
            return ConvertFailure<OcctExactFaceNormalResult>(lifecycle);
        }

        BRepAdaptor_Surface surface(face, false);
        BRepLProp_SLProps props(surface, u, v, 1, Precision::Confusion());
        if (!props.IsNormalDefined()) {
            return MakeFailure<OcctExactFaceNormalResult>("unsupported-geometry", "Exact face normal is not defined at the requested query point.");
        }

        gp_Dir normal = props.Normal();
        if (face.Orientation() == TopAbs_REVERSED) {
            normal.Reverse();
        }

        OcctExactFaceNormalResult result;
        result.ok = true;
        result.localPoint = ToArray(projectedPoint);
        result.localNormal = ToArray(normal);
        return result;
    } catch (const Standard_Failure& failure) {
        return MakeFailure<OcctExactFaceNormalResult>(
            "internal-error",
            failure.GetMessageString() != nullptr ? failure.GetMessageString() : "OCCT exact face normal query failed."
        );
    }
}

OcctExactDistanceResult MeasureExactDistance(
    int exactModelId,
    int exactShapeHandleA,
    const std::string& kindA,
    int elementIdA,
    int exactShapeHandleB,
    const std::string& kindB,
    int elementIdB,
    const gp_Trsf& transformA,
    const gp_Trsf& transformB)
{
    return MeasureExactDistanceCrossModel(
        exactModelId,
        exactShapeHandleA,
        kindA,
        elementIdA,
        transformA,
        exactModelId,
        exactShapeHandleB,
        kindB,
        elementIdB,
        transformB
    );
}

OcctExactDistanceResult MeasureExactDistanceCrossModel(
    int exactModelIdA,
    int exactShapeHandleA,
    const std::string& kindA,
    int elementIdA,
    const gp_Trsf& transformA,
    int exactModelIdB,
    int exactShapeHandleB,
    const std::string& kindB,
    int elementIdB,
    const gp_Trsf& transformB)
{
    try {
        TopoDS_Shape shapeA;
        OcctLifecycleResult lifecycle = ResolveElementShape(exactModelIdA, exactShapeHandleA, kindA, elementIdA, shapeA);
        if (!lifecycle.ok) {
            return ConvertFailure<OcctExactDistanceResult>(lifecycle);
        }

        TopoDS_Shape shapeB;
        lifecycle = ResolveElementShape(exactModelIdB, exactShapeHandleB, kindB, elementIdB, shapeB);
        if (!lifecycle.ok) {
            return ConvertFailure<OcctExactDistanceResult>(lifecycle);
        }

        shapeA = ApplyOccurrenceTransform(shapeA, transformA);
        shapeB = ApplyOccurrenceTransform(shapeB, transformB);

        double value = 0.0;
        gp_Pnt pointA;
        gp_Pnt pointB;
        lifecycle = MeasureMinimumDistance(shapeA, shapeB, value, pointA, pointB);
        if (!lifecycle.ok) {
            return ConvertFailure<OcctExactDistanceResult>(lifecycle);
        }
        gp_Dir workingPlaneNormal;

        OcctExactDistanceResult result;
        result.ok = true;
        result.value = value;
        result.pointA = ToArray(pointA);
        result.pointB = ToArray(pointB);
        result.workingPlaneOrigin = ToArray(Midpoint(pointA, pointB));
        if (TryMakeDirection(pointA, pointB, workingPlaneNormal)) {
            result.workingPlaneNormal = ToArray(workingPlaneNormal);
        }
        return result;
    } catch (const Standard_Failure& failure) {
        return MakeFailure<OcctExactDistanceResult>(
            "internal-error",
            failure.GetMessageString() != nullptr ? failure.GetMessageString() : "OCCT exact distance query failed."
        );
    }
}

OcctExactAngleResult MeasureExactAngle(
    int exactModelId,
    int exactShapeHandleA,
    const std::string& kindA,
    int elementIdA,
    int exactShapeHandleB,
    const std::string& kindB,
    int elementIdB,
    const gp_Trsf& transformA,
    const gp_Trsf& transformB)
{
    return MeasureExactAngleCrossModel(
        exactModelId,
        exactShapeHandleA,
        kindA,
        elementIdA,
        transformA,
        exactModelId,
        exactShapeHandleB,
        kindB,
        elementIdB,
        transformB
    );
}

OcctExactAngleResult MeasureExactAngleCrossModel(
    int exactModelIdA,
    int exactShapeHandleA,
    const std::string& kindA,
    int elementIdA,
    const gp_Trsf& transformA,
    int exactModelIdB,
    int exactShapeHandleB,
    const std::string& kindB,
    int elementIdB,
    const gp_Trsf& transformB)
{
    try {
        const std::string normalizedKindA = NormalizeKind(kindA);
        const std::string normalizedKindB = NormalizeKind(kindB);
        if (normalizedKindA == "edge" && normalizedKindB == "edge") {
            TopoDS_Edge edgeA;
            OcctLifecycleResult lifecycle = ResolveEdge(exactModelIdA, exactShapeHandleA, elementIdA, edgeA);
            if (!lifecycle.ok) {
                return ConvertFailure<OcctExactAngleResult>(lifecycle);
            }

            TopoDS_Edge edgeB;
            lifecycle = ResolveEdge(exactModelIdB, exactShapeHandleB, elementIdB, edgeB);
            if (!lifecycle.ok) {
                return ConvertFailure<OcctExactAngleResult>(lifecycle);
            }

            edgeA = TopoDS::Edge(ApplyOccurrenceTransform(edgeA, transformA));
            edgeB = TopoDS::Edge(ApplyOccurrenceTransform(edgeB, transformB));

            BRepAdaptor_Curve curveA(edgeA);
            BRepAdaptor_Curve curveB(edgeB);
            if (curveA.GetType() != GeomAbs_Line || curveB.GetType() != GeomAbs_Line) {
                return MakeFailure<OcctExactAngleResult>(
                    "unsupported-geometry",
                    "Exact angle only supports line/line or plane/plane pairs."
                );
            }

            const gp_Lin lineA = curveA.Line();
            const gp_Lin lineB = curveB.Line();
            if (lineA.Position().Direction().IsParallel(lineB.Position().Direction(), Precision::Angular())) {
                return MakeFailure<OcctExactAngleResult>(
                    lineA.Distance(lineB) <= Precision::Confusion() ? "coincident-geometry" : "parallel-geometry",
                    lineA.Distance(lineB) <= Precision::Confusion()
                        ? "Exact angle is not defined for coincident planar faces or overlapping linear edges."
                        : "Exact angle is not defined for parallel planar faces or collinear linear edges."
                );
            }

            double distanceValue = 0.0;
            gp_Pnt pointA;
            gp_Pnt pointB;
            lifecycle = MeasureMinimumDistance(edgeA, edgeB, distanceValue, pointA, pointB);
            if (!lifecycle.ok) {
                return ConvertFailure<OcctExactAngleResult>(lifecycle);
            }
            if (distanceValue > Precision::Confusion()) {
                return MakeFailure<OcctExactAngleResult>(
                    "non-intersecting-geometry",
                    "Exact angle is only defined for intersecting linear edges or nonparallel planar faces."
                );
            }

            gp_Dir workingPlaneNormal(gp_Vec(lineA.Direction().XYZ().Crossed(lineB.Direction().XYZ())));

            OcctExactAngleResult result;
            result.ok = true;
            result.value = CanonicalAngle(lineA.Angle(lineB));
            result.origin = ToArray(Midpoint(pointA, pointB));
            result.directionA = ToArray(lineA.Direction());
            result.directionB = ToArray(lineB.Direction());
            result.pointA = ToArray(pointA);
            result.pointB = ToArray(pointB);
            result.workingPlaneOrigin = ToArray(Midpoint(pointA, pointB));
            result.workingPlaneNormal = ToArray(workingPlaneNormal);
            return result;
        }

        if (normalizedKindA == "face" && normalizedKindB == "face") {
            TopoDS_Face faceA;
            OcctLifecycleResult lifecycle = ResolveFace(exactModelIdA, exactShapeHandleA, elementIdA, faceA);
            if (!lifecycle.ok) {
                return ConvertFailure<OcctExactAngleResult>(lifecycle);
            }

            TopoDS_Face faceB;
            lifecycle = ResolveFace(exactModelIdB, exactShapeHandleB, elementIdB, faceB);
            if (!lifecycle.ok) {
                return ConvertFailure<OcctExactAngleResult>(lifecycle);
            }

            faceA = TopoDS::Face(ApplyOccurrenceTransform(faceA, transformA));
            faceB = TopoDS::Face(ApplyOccurrenceTransform(faceB, transformB));

            BRepAdaptor_Surface surfaceA(faceA, false);
            BRepAdaptor_Surface surfaceB(faceB, false);
            if (surfaceA.GetType() != GeomAbs_Plane || surfaceB.GetType() != GeomAbs_Plane) {
                return MakeFailure<OcctExactAngleResult>(
                    "unsupported-geometry",
                    "Exact angle only supports line/line or plane/plane pairs."
                );
            }

            const gp_Pln planeA = surfaceA.Plane();
            const gp_Pln planeB = surfaceB.Plane();
            if (planeA.Axis().Direction().IsParallel(planeB.Axis().Direction(), Precision::Angular())) {
                return MakeFailure<OcctExactAngleResult>(
                    planeA.Distance(planeB) <= Precision::Confusion() ? "coincident-geometry" : "parallel-geometry",
                    planeA.Distance(planeB) <= Precision::Confusion()
                        ? "Exact angle is not defined for coincident planar faces or overlapping linear edges."
                        : "Exact angle is not defined for parallel planar faces or collinear linear edges."
                );
            }

            double distanceValue = 0.0;
            gp_Pnt pointA;
            gp_Pnt pointB;
            lifecycle = MeasureMinimumDistance(faceA, faceB, distanceValue, pointA, pointB);
            if (!lifecycle.ok) {
                return ConvertFailure<OcctExactAngleResult>(lifecycle);
            }

            gp_Dir workingPlaneNormal(gp_Vec(planeA.Axis().Direction().XYZ().Crossed(planeB.Axis().Direction().XYZ())));

            OcctExactAngleResult result;
            result.ok = true;
            result.value = CanonicalAngle(planeA.Axis().Direction().Angle(planeB.Axis().Direction()));
            result.origin = ToArray(Midpoint(pointA, pointB));
            result.directionA = ToArray(planeA.Axis().Direction());
            result.directionB = ToArray(planeB.Axis().Direction());
            result.pointA = ToArray(pointA);
            result.pointB = ToArray(pointB);
            result.workingPlaneOrigin = ToArray(Midpoint(pointA, pointB));
            result.workingPlaneNormal = ToArray(workingPlaneNormal);
            return result;
        }

        return MakeFailure<OcctExactAngleResult>(
            "unsupported-geometry",
            "Exact angle only supports line/line or plane/plane pairs."
        );
    } catch (const Standard_Failure& failure) {
        return MakeFailure<OcctExactAngleResult>(
            "internal-error",
            failure.GetMessageString() != nullptr ? failure.GetMessageString() : "OCCT exact angle query failed."
        );
    }
}

OcctExactThicknessResult MeasureExactThickness(
    int exactModelId,
    int exactShapeHandleA,
    const std::string& kindA,
    int elementIdA,
    int exactShapeHandleB,
    const std::string& kindB,
    int elementIdB,
    const gp_Trsf& transformA,
    const gp_Trsf& transformB)
{
    return MeasureExactThicknessCrossModel(
        exactModelId,
        exactShapeHandleA,
        kindA,
        elementIdA,
        transformA,
        exactModelId,
        exactShapeHandleB,
        kindB,
        elementIdB,
        transformB
    );
}

OcctExactThicknessResult MeasureExactThicknessCrossModel(
    int exactModelIdA,
    int exactShapeHandleA,
    const std::string& kindA,
    int elementIdA,
    const gp_Trsf& transformA,
    int exactModelIdB,
    int exactShapeHandleB,
    const std::string& kindB,
    int elementIdB,
    const gp_Trsf& transformB)
{
    try {
        const std::string normalizedKindA = NormalizeKind(kindA);
        const std::string normalizedKindB = NormalizeKind(kindB);
        if (normalizedKindA != "face" || normalizedKindB != "face") {
            return MakePairwiseUnsupported<OcctExactThicknessResult>();
        }

        TopoDS_Face faceA;
        OcctLifecycleResult lifecycle = ResolveFace(exactModelIdA, exactShapeHandleA, elementIdA, faceA);
        if (!lifecycle.ok) {
            return ConvertFailure<OcctExactThicknessResult>(lifecycle);
        }

        TopoDS_Face faceB;
        lifecycle = ResolveFace(exactModelIdB, exactShapeHandleB, elementIdB, faceB);
        if (!lifecycle.ok) {
            return ConvertFailure<OcctExactThicknessResult>(lifecycle);
        }

        faceA = TopoDS::Face(ApplyOccurrenceTransform(faceA, transformA));
        faceB = TopoDS::Face(ApplyOccurrenceTransform(faceB, transformB));

        BRepAdaptor_Surface surfaceA(faceA, false);
        BRepAdaptor_Surface surfaceB(faceB, false);
        if (surfaceA.GetType() != GeomAbs_Plane || surfaceB.GetType() != GeomAbs_Plane) {
            return MakePairwiseUnsupported<OcctExactThicknessResult>();
        }

        const gp_Pln planeA = surfaceA.Plane();
        const gp_Pln planeB = surfaceB.Plane();
        const gp_Dir normalA = planeA.Axis().Direction();
        if (!normalA.IsParallel(planeB.Axis().Direction(), Precision::Angular())) {
            return MakePairwiseUnsupported<OcctExactThicknessResult>();
        }

        const gp_Vec planeOffset(planeA.Location(), planeB.Location());
        const double signedDistance = planeOffset.Dot(gp_Vec(normalA));
        const double value = std::abs(signedDistance);
        if (value <= Precision::Confusion()) {
            return MakeFailure<OcctExactThicknessResult>(
                "coincident-geometry",
                "Exact thickness requires separated parallel planar face pairs."
            );
        }

        gp_Dir workingPlaneNormal = normalA;
        if (signedDistance < 0.0) {
            workingPlaneNormal.Reverse();
        }

        const gp_Pnt pointA = SampleFacePoint(faceA, surfaceA);
        const gp_Pnt pointB = pointA.Translated(gp_Vec(workingPlaneNormal) * value);

        OcctExactThicknessResult result;
        result.ok = true;
        result.value = value;
        result.pointA = ToArray(pointA);
        result.pointB = ToArray(pointB);
        result.workingPlaneOrigin = ToArray(Midpoint(pointA, pointB));
        result.workingPlaneNormal = ToArray(workingPlaneNormal);
        return result;
    } catch (const Standard_Failure& failure) {
        return MakeFailure<OcctExactThicknessResult>(
            "internal-error",
            failure.GetMessageString() != nullptr ? failure.GetMessageString() : "OCCT exact thickness query failed."
        );
    }
}

OcctExactRelationResult ClassifyExactRelation(
    int exactModelId,
    int exactShapeHandleA,
    const std::string& kindA,
    int elementIdA,
    int exactShapeHandleB,
    const std::string& kindB,
    int elementIdB,
    const gp_Trsf& transformA,
    const gp_Trsf& transformB)
{
    return ClassifyExactRelationCrossModel(
        exactModelId,
        exactShapeHandleA,
        kindA,
        elementIdA,
        transformA,
        exactModelId,
        exactShapeHandleB,
        kindB,
        elementIdB,
        transformB
    );
}

OcctExactRelationResult ClassifyExactRelationCrossModel(
    int exactModelIdA,
    int exactShapeHandleA,
    const std::string& kindA,
    int elementIdA,
    const gp_Trsf& transformA,
    int exactModelIdB,
    int exactShapeHandleB,
    const std::string& kindB,
    int elementIdB,
    const gp_Trsf& transformB)
{
    try {
        const std::string normalizedKindA = NormalizeKind(kindA);
        const std::string normalizedKindB = NormalizeKind(kindB);

        if (normalizedKindA == "edge" && normalizedKindB == "edge") {
            TopoDS_Edge edgeA;
            OcctLifecycleResult lifecycle = ResolveEdge(exactModelIdA, exactShapeHandleA, elementIdA, edgeA);
            if (!lifecycle.ok) {
                return ConvertFailure<OcctExactRelationResult>(lifecycle);
            }

            TopoDS_Edge edgeB;
            lifecycle = ResolveEdge(exactModelIdB, exactShapeHandleB, elementIdB, edgeB);
            if (!lifecycle.ok) {
                return ConvertFailure<OcctExactRelationResult>(lifecycle);
            }

            edgeA = TopoDS::Edge(ApplyOccurrenceTransform(edgeA, transformA));
            edgeB = TopoDS::Edge(ApplyOccurrenceTransform(edgeB, transformB));

            BRepAdaptor_Curve curveA(edgeA);
            BRepAdaptor_Curve curveB(edgeB);
            if (curveA.GetType() == GeomAbs_Line && curveB.GetType() == GeomAbs_Line) {
                return ClassifyLineLineRelation(edgeA, edgeB);
            }
            if (curveA.GetType() == GeomAbs_Circle && curveB.GetType() == GeomAbs_Circle) {
                return ClassifyCircleCircleRelation(edgeA, edgeB);
            }

            return MakeFailure<OcctExactRelationResult>(
                "unsupported-geometry",
                "Exact relation classification only supports line/line, plane/plane, circle/circle, and circle/cylinder analytic pairs."
            );
        }

        if (normalizedKindA == "face" && normalizedKindB == "face") {
            TopoDS_Face faceA;
            OcctLifecycleResult lifecycle = ResolveFace(exactModelIdA, exactShapeHandleA, elementIdA, faceA);
            if (!lifecycle.ok) {
                return ConvertFailure<OcctExactRelationResult>(lifecycle);
            }

            TopoDS_Face faceB;
            lifecycle = ResolveFace(exactModelIdB, exactShapeHandleB, elementIdB, faceB);
            if (!lifecycle.ok) {
                return ConvertFailure<OcctExactRelationResult>(lifecycle);
            }

            faceA = TopoDS::Face(ApplyOccurrenceTransform(faceA, transformA));
            faceB = TopoDS::Face(ApplyOccurrenceTransform(faceB, transformB));

            BRepAdaptor_Surface surfaceA(faceA, false);
            BRepAdaptor_Surface surfaceB(faceB, false);
            if (surfaceA.GetType() == GeomAbs_Plane && surfaceB.GetType() == GeomAbs_Plane) {
                return ClassifyPlanePlaneRelation(faceA, faceB);
            }

            return MakeFailure<OcctExactRelationResult>(
                "unsupported-geometry",
                "Exact relation classification only supports line/line, plane/plane, circle/circle, and circle/cylinder analytic pairs."
            );
        }

        if (normalizedKindA == "edge" && normalizedKindB == "face") {
            TopoDS_Edge edgeA;
            OcctLifecycleResult lifecycle = ResolveEdge(exactModelIdA, exactShapeHandleA, elementIdA, edgeA);
            if (!lifecycle.ok) {
                return ConvertFailure<OcctExactRelationResult>(lifecycle);
            }

            TopoDS_Face faceB;
            lifecycle = ResolveFace(exactModelIdB, exactShapeHandleB, elementIdB, faceB);
            if (!lifecycle.ok) {
                return ConvertFailure<OcctExactRelationResult>(lifecycle);
            }

            edgeA = TopoDS::Edge(ApplyOccurrenceTransform(edgeA, transformA));
            faceB = TopoDS::Face(ApplyOccurrenceTransform(faceB, transformB));
            return ClassifyCircleCylinderRelation(edgeA, faceB);
        }

        if (normalizedKindA == "face" && normalizedKindB == "edge") {
            TopoDS_Face faceA;
            OcctLifecycleResult lifecycle = ResolveFace(exactModelIdA, exactShapeHandleA, elementIdA, faceA);
            if (!lifecycle.ok) {
                return ConvertFailure<OcctExactRelationResult>(lifecycle);
            }

            TopoDS_Edge edgeB;
            lifecycle = ResolveEdge(exactModelIdB, exactShapeHandleB, elementIdB, edgeB);
            if (!lifecycle.ok) {
                return ConvertFailure<OcctExactRelationResult>(lifecycle);
            }

            faceA = TopoDS::Face(ApplyOccurrenceTransform(faceA, transformA));
            edgeB = TopoDS::Edge(ApplyOccurrenceTransform(edgeB, transformB));
            return ClassifyCircleCylinderRelation(edgeB, faceA);
        }

        return MakeFailure<OcctExactRelationResult>(
            "unsupported-geometry",
            "Exact relation classification only supports line/line, plane/plane, circle/circle, and circle/cylinder analytic pairs."
        );
    } catch (const Standard_Failure& failure) {
        return MakeFailure<OcctExactRelationResult>(
            "internal-error",
            failure.GetMessageString() != nullptr ? failure.GetMessageString() : "OCCT exact relation classification failed."
        );
    }
}

OcctExactPlacementResult SuggestExactDistancePlacement(
    int exactModelId,
    int exactShapeHandleA,
    const std::string& kindA,
    int elementIdA,
    int exactShapeHandleB,
    const std::string& kindB,
    int elementIdB,
    const gp_Trsf& transformA,
    const gp_Trsf& transformB)
{
    return SuggestExactDistancePlacementCrossModel(
        exactModelId,
        exactShapeHandleA,
        kindA,
        elementIdA,
        transformA,
        exactModelId,
        exactShapeHandleB,
        kindB,
        elementIdB,
        transformB
    );
}

OcctExactPlacementResult SuggestExactDistancePlacementCrossModel(
    int exactModelIdA,
    int exactShapeHandleA,
    const std::string& kindA,
    int elementIdA,
    const gp_Trsf& transformA,
    int exactModelIdB,
    int exactShapeHandleB,
    const std::string& kindB,
    int elementIdB,
    const gp_Trsf& transformB)
{
    const OcctExactDistanceResult distance = MeasureExactDistanceCrossModel(
        exactModelIdA,
        exactShapeHandleA,
        kindA,
        elementIdA,
        transformA,
        exactModelIdB,
        exactShapeHandleB,
        kindB,
        elementIdB,
        transformB
    );
    if (!distance.ok) {
        return ConvertPlacementFailure(distance);
    }

    gp_Dir measurementAxis;
    if (!TryMakeDirection(ToPoint(distance.pointA), ToPoint(distance.pointB), measurementAxis)) {
        return MakeFailure<OcctExactPlacementResult>(
            "coincident-geometry",
            "Exact distance placement requires geometry with a stable separation direction."
        );
    }

    OcctExactPlacementResult result;
    result.ok = true;
    result.kind = "distance";
    result.hasValue = true;
    result.value = distance.value;
    if (!BuildFrameFromXAxis(ToPoint(distance.workingPlaneOrigin), measurementAxis, result.frame)) {
        return MakeFailure<OcctExactPlacementResult>(
            "coincident-geometry",
            "Exact distance placement requires a stable working frame."
        );
    }
    AddPlacementAnchor(result, "attach", ToPoint(distance.pointA));
    AddPlacementAnchor(result, "attach", ToPoint(distance.pointB));
    return result;
}

OcctExactPlacementResult SuggestExactAnglePlacement(
    int exactModelId,
    int exactShapeHandleA,
    const std::string& kindA,
    int elementIdA,
    int exactShapeHandleB,
    const std::string& kindB,
    int elementIdB,
    const gp_Trsf& transformA,
    const gp_Trsf& transformB)
{
    return SuggestExactAnglePlacementCrossModel(
        exactModelId,
        exactShapeHandleA,
        kindA,
        elementIdA,
        transformA,
        exactModelId,
        exactShapeHandleB,
        kindB,
        elementIdB,
        transformB
    );
}

OcctExactPlacementResult SuggestExactAnglePlacementCrossModel(
    int exactModelIdA,
    int exactShapeHandleA,
    const std::string& kindA,
    int elementIdA,
    const gp_Trsf& transformA,
    int exactModelIdB,
    int exactShapeHandleB,
    const std::string& kindB,
    int elementIdB,
    const gp_Trsf& transformB)
{
    const OcctExactAngleResult angle = MeasureExactAngleCrossModel(
        exactModelIdA,
        exactShapeHandleA,
        kindA,
        elementIdA,
        transformA,
        exactModelIdB,
        exactShapeHandleB,
        kindB,
        elementIdB,
        transformB
    );
    if (!angle.ok) {
        return ConvertPlacementFailure(angle);
    }

    gp_Dir directionA;
    gp_Dir directionB;
    if (!TryMakeDirection(angle.directionA, directionA) || !TryMakeDirection(angle.directionB, directionB)) {
        return MakeFailure<OcctExactPlacementResult>(
            "unsupported-geometry",
            "Exact angle placement requires geometry with stable direction vectors."
        );
    }

    gp_Vec planeNormalVector(directionA.XYZ().Crossed(directionB.XYZ()));
    if (planeNormalVector.Magnitude() <= Precision::Confusion()) {
        return MakeFailure<OcctExactPlacementResult>(
            "parallel-geometry",
            "Exact angle placement requires non-parallel geometry."
        );
    }

    OcctExactPlacementResult result;
    result.ok = true;
    result.kind = "angle";
    result.hasValue = true;
    result.value = angle.value;
    result.hasDirectionA = true;
    result.directionA = ToArray(directionA);
    result.hasDirectionB = true;
    result.directionB = ToArray(directionB);
    if (!BuildFrameFromNormalAndX(ToPoint(angle.origin), gp_Dir(planeNormalVector), directionA, result.frame)) {
        return MakeFailure<OcctExactPlacementResult>(
            "unsupported-geometry",
            "Exact angle placement requires a stable working frame."
        );
    }
    AddPlacementAnchor(result, "anchor", ToPoint(angle.origin));
    AddPlacementAnchor(result, "attach", ToPoint(angle.pointA));
    AddPlacementAnchor(result, "attach", ToPoint(angle.pointB));
    return result;
}

OcctExactPlacementResult SuggestExactThicknessPlacement(
    int exactModelId,
    int exactShapeHandleA,
    const std::string& kindA,
    int elementIdA,
    int exactShapeHandleB,
    const std::string& kindB,
    int elementIdB,
    const gp_Trsf& transformA,
    const gp_Trsf& transformB)
{
    return SuggestExactThicknessPlacementCrossModel(
        exactModelId,
        exactShapeHandleA,
        kindA,
        elementIdA,
        transformA,
        exactModelId,
        exactShapeHandleB,
        kindB,
        elementIdB,
        transformB
    );
}

OcctExactPlacementResult SuggestExactThicknessPlacementCrossModel(
    int exactModelIdA,
    int exactShapeHandleA,
    const std::string& kindA,
    int elementIdA,
    const gp_Trsf& transformA,
    int exactModelIdB,
    int exactShapeHandleB,
    const std::string& kindB,
    int elementIdB,
    const gp_Trsf& transformB)
{
    const OcctExactThicknessResult thickness = MeasureExactThicknessCrossModel(
        exactModelIdA,
        exactShapeHandleA,
        kindA,
        elementIdA,
        transformA,
        exactModelIdB,
        exactShapeHandleB,
        kindB,
        elementIdB,
        transformB
    );
    if (!thickness.ok) {
        return ConvertPlacementFailure(thickness);
    }

    gp_Dir measurementAxis;
    if (!TryMakeDirection(ToPoint(thickness.pointA), ToPoint(thickness.pointB), measurementAxis)) {
        return MakeFailure<OcctExactPlacementResult>(
            "coincident-geometry",
            "Exact thickness placement requires geometry with a stable separation direction."
        );
    }

    OcctExactPlacementResult result;
    result.ok = true;
    result.kind = "thickness";
    result.hasValue = true;
    result.value = thickness.value;
    if (!BuildFrameFromXAxis(ToPoint(thickness.workingPlaneOrigin), measurementAxis, result.frame)) {
        return MakeFailure<OcctExactPlacementResult>(
            "coincident-geometry",
            "Exact thickness placement requires a stable working frame."
        );
    }
    AddPlacementAnchor(result, "attach", ToPoint(thickness.pointA));
    AddPlacementAnchor(result, "attach", ToPoint(thickness.pointB));
    return result;
}

OcctExactPlacementResult SuggestExactRadiusPlacement(
    int exactModelId,
    int exactShapeHandle,
    const std::string& kind,
    int elementId)
{
    try {
        double radiusValue = 0.0;
        gp_Pnt centerPoint;
        gp_Pnt anchorPoint;
        gp_Dir axisDirection;
        gp_Dir radialDirection;
        OcctLifecycleResult lifecycle = ResolveCircularPlacementSupport(
            exactModelId,
            exactShapeHandle,
            kind,
            elementId,
            radiusValue,
            centerPoint,
            anchorPoint,
            axisDirection,
            radialDirection
        );
        if (!lifecycle.ok) {
            return ConvertPlacementFailure(lifecycle);
        }

        OcctExactPlacementResult result;
        result.ok = true;
        result.kind = "radius";
        result.hasValue = true;
        result.value = radiusValue;
        result.hasAxisDirection = true;
        result.axisDirection = ToArray(axisDirection);
        if (!BuildFrameFromNormalAndX(centerPoint, axisDirection, radialDirection, result.frame)) {
            return MakeFailure<OcctExactPlacementResult>(
                "unsupported-geometry",
                "Exact radius placement requires a stable working frame."
            );
        }
        AddPlacementAnchor(result, "center", centerPoint);
        AddPlacementAnchor(result, "anchor", anchorPoint);
        return result;
    } catch (const Standard_Failure& failure) {
        return MakeFailure<OcctExactPlacementResult>(
            "internal-error",
            failure.GetMessageString() != nullptr ? failure.GetMessageString() : "OCCT exact radius placement query failed."
        );
    }
}

OcctExactPlacementResult SuggestExactDiameterPlacement(
    int exactModelId,
    int exactShapeHandle,
    const std::string& kind,
    int elementId)
{
    try {
        double radiusValue = 0.0;
        gp_Pnt centerPoint;
        gp_Pnt anchorPoint;
        gp_Dir axisDirection;
        gp_Dir radialDirection;
        OcctLifecycleResult lifecycle = ResolveCircularPlacementSupport(
            exactModelId,
            exactShapeHandle,
            kind,
            elementId,
            radiusValue,
            centerPoint,
            anchorPoint,
            axisDirection,
            radialDirection
        );
        if (!lifecycle.ok) {
            return ConvertPlacementFailure(lifecycle);
        }

        const gp_Pnt oppositePoint = centerPoint.Translated(gp_Vec(radialDirection) * (-radiusValue));

        OcctExactPlacementResult result;
        result.ok = true;
        result.kind = "diameter";
        result.hasValue = true;
        result.value = radiusValue * 2.0;
        result.hasAxisDirection = true;
        result.axisDirection = ToArray(axisDirection);
        if (!BuildFrameFromNormalAndX(centerPoint, axisDirection, radialDirection, result.frame)) {
            return MakeFailure<OcctExactPlacementResult>(
                "unsupported-geometry",
                "Exact diameter placement requires a stable working frame."
            );
        }
        AddPlacementAnchor(result, "center", centerPoint);
        AddPlacementAnchor(result, "anchor", anchorPoint);
        AddPlacementAnchor(result, "anchor", oppositePoint);
        return result;
    } catch (const Standard_Failure& failure) {
        return MakeFailure<OcctExactPlacementResult>(
            "internal-error",
            failure.GetMessageString() != nullptr ? failure.GetMessageString() : "OCCT exact diameter placement query failed."
        );
    }
}

OcctExactHoleResult DescribeExactHole(
    int exactModelId,
    int exactShapeHandle,
    const std::string& kind,
    int elementId)
{
    try {
        TopoDS_Shape geometryShape;
        TopoDS_Face holeFace;
        OcctLifecycleResult lifecycle = ResolveHoleCandidateFace(
            exactModelId,
            exactShapeHandle,
            kind,
            elementId,
            geometryShape,
            holeFace
        );
        if (!lifecycle.ok) {
            return MakeFailure<OcctExactHoleResult>(lifecycle.code, lifecycle.message);
        }

        HoleCylinderDescriptor descriptor;
        lifecycle = ResolveHoleCylinderDescriptor(geometryShape, holeFace, descriptor);
        if (!lifecycle.ok) {
            return MakeFailure<OcctExactHoleResult>(lifecycle.code, lifecycle.message);
        }

        return BuildHoleResult(descriptor);
    } catch (const Standard_Failure& failure) {
        return MakeFailure<OcctExactHoleResult>(
            "internal-error",
            failure.GetMessageString() != nullptr ? failure.GetMessageString() : "OCCT exact hole query failed."
        );
    }
}

OcctExactChamferResult DescribeExactChamfer(
    int exactModelId,
    int exactShapeHandle,
    const std::string& kind,
    int elementId)
{
    try {
        TopoDS_Shape geometryShape;
        TopoDS_Face chamferFace;
        OcctLifecycleResult lifecycle = ResolveChamferCandidateFace(
            exactModelId,
            exactShapeHandle,
            kind,
            elementId,
            geometryShape,
            chamferFace
        );
        if (!lifecycle.ok) {
            return MakeFailure<OcctExactChamferResult>(lifecycle.code, lifecycle.message);
        }

        BRepAdaptor_Surface chamferSurface(chamferFace, false);
        if (chamferSurface.GetType() != GeomAbs_Plane) {
            return MakeFailure<OcctExactChamferResult>(
                "unsupported-geometry",
                "Exact chamfer helper only supports planar chamfer face refs."
            );
        }

        const gp_Pln chamferPlane = chamferSurface.Plane();
        std::vector<ChamferSupportCandidate> supports;
        lifecycle = CollectChamferSupportCandidates(geometryShape, chamferFace, chamferPlane, supports);
        if (!lifecycle.ok) {
            return MakeFailure<OcctExactChamferResult>(lifecycle.code, lifecycle.message);
        }

        gp_Lin supportIntersection;
        if (!TryIntersectPlanes(supports[0].plane, supports[1].plane, supportIntersection)) {
            return MakeFailure<OcctExactChamferResult>(
                "unsupported-geometry",
                "Exact chamfer helper requires two support planes with a stable intersection edge."
            );
        }

        if (!supportIntersection.Direction().IsParallel(supports[0].boundaryLine.Direction(), Precision::Angular())
            || !supportIntersection.Direction().IsParallel(supports[1].boundaryLine.Direction(), Precision::Angular())) {
            return MakeFailure<OcctExactChamferResult>(
                "unsupported-geometry",
                "Exact chamfer helper requires support boundary edges parallel to the support-plane intersection edge."
            );
        }

        const double distanceA = supportIntersection.Distance(supports[0].boundaryLine);
        const double distanceB = supportIntersection.Distance(supports[1].boundaryLine);
        if (distanceA <= Precision::Confusion() || distanceB <= Precision::Confusion()) {
            return MakeFailure<OcctExactChamferResult>(
                "unsupported-geometry",
                "Selected planar face does not describe a separated chamfer offset from the support intersection edge."
            );
        }

        const gp_Pnt samplePoint = SampleFacePoint(chamferFace, chamferSurface);
        const gp_Pnt anchorA = ProjectPointOntoLine(supports[0].boundaryLine, samplePoint);
        const gp_Pnt anchorB = ProjectPointOntoLine(supports[1].boundaryLine, samplePoint);
        const gp_Pnt origin = Midpoint(anchorA, anchorB);
        const gp_Dir chamferNormal = chamferPlane.Axis().Direction();
        const gp_Dir edgeDirection = supportIntersection.Direction();
        const double supportAngle = CanonicalAngle(supports[0].normal.Angle(supports[1].normal));
        const double equalTolerance = std::max(
            Precision::Confusion() * 100.0,
            std::max(distanceA, distanceB) * 1e-6
        );

        OcctExactChamferResult result;
        result.ok = true;
        result.kind = "chamfer";
        result.profile = "planar";
        result.variant = std::abs(distanceA - distanceB) <= equalTolerance ? "equal-distance" : "two-distance";
        result.distanceA = distanceA;
        result.distanceB = distanceB;
        result.supportAngle = supportAngle;
        if (!BuildFrameFromNormalAndX(origin, chamferNormal, edgeDirection, result.frame)) {
            return MakeFailure<OcctExactChamferResult>(
                "unsupported-geometry",
                "Exact chamfer helper requires a stable working frame."
            );
        }
        result.hasFrame = true;
        AddChamferAnchor(result, "support-a", anchorA);
        AddChamferAnchor(result, "support-b", anchorB);
        result.edgeDirection = ToArray(edgeDirection);
        result.hasEdgeDirection = true;
        result.supportNormalA = ToArray(supports[0].normal);
        result.hasSupportNormalA = true;
        result.supportNormalB = ToArray(supports[1].normal);
        result.hasSupportNormalB = true;
        return result;
    } catch (const Standard_Failure& failure) {
        return MakeFailure<OcctExactChamferResult>(
            "internal-error",
            failure.GetMessageString() != nullptr ? failure.GetMessageString() : "OCCT exact chamfer query failed."
        );
    }
}

OcctExactCompoundHoleResult DescribeExactCompoundHole(
    int exactModelId,
    int exactShapeHandle,
    const std::string& kind,
    int elementId)
{
    try {
        TopoDS_Shape geometryShape;
        std::vector<TopoDS_Face> candidateFaces;
        OcctLifecycleResult lifecycle = CollectCompoundHoleCandidateFaces(
            exactModelId,
            exactShapeHandle,
            kind,
            elementId,
            geometryShape,
            candidateFaces
        );
        if (!lifecycle.ok) {
            return MakeFailure<OcctExactCompoundHoleResult>(lifecycle.code, lifecycle.message);
        }

        for (const auto& face : candidateFaces) {
            BRepAdaptor_Surface surface(face, false);
            if (surface.GetType() == GeomAbs_Cylinder) {
                OcctExactCompoundHoleResult counterbore;
                lifecycle = TryDescribeExactCounterboreFromCylinder(geometryShape, face, counterbore);
                if (lifecycle.ok) {
                    return counterbore;
                }
                if (lifecycle.code != "unsupported-geometry") {
                    return MakeFailure<OcctExactCompoundHoleResult>(lifecycle.code, lifecycle.message);
                }

                OcctExactCompoundHoleResult countersink;
                lifecycle = TryDescribeExactCountersinkFromCylinder(geometryShape, face, countersink);
                if (lifecycle.ok) {
                    return countersink;
                }
                if (lifecycle.code != "unsupported-geometry") {
                    return MakeFailure<OcctExactCompoundHoleResult>(lifecycle.code, lifecycle.message);
                }
                continue;
            }

            if (surface.GetType() == GeomAbs_Cone) {
                OcctExactCompoundHoleResult countersink;
                lifecycle = TryDescribeExactCountersinkFromCone(geometryShape, face, countersink);
                if (lifecycle.ok) {
                    return countersink;
                }
                if (lifecycle.code != "unsupported-geometry") {
                    return MakeFailure<OcctExactCompoundHoleResult>(lifecycle.code, lifecycle.message);
                }
            }
        }

        return MakeFailure<OcctExactCompoundHoleResult>(
            "unsupported-geometry",
            kCompoundHoleUnsupportedMessage
        );
    } catch (const Standard_Failure& failure) {
        return MakeFailure<OcctExactCompoundHoleResult>(
            "internal-error",
            failure.GetMessageString() != nullptr ? failure.GetMessageString() : "OCCT exact compound-hole query failed."
        );
    }
}
