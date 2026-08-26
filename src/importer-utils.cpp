#include "importer-utils.hpp"

#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <Poly_Triangulation.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <gp_Trsf.hxx>
#include <TopAbs.hxx>
#include <Precision.hxx>
#include <UnitsMethods.hxx>
#include <TopoDS_Edge.hxx>
#include <Poly_PolygonOnTriangulation.hxx>
#include <Poly_Polygon3D.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopExp.hxx>
#include <TopoDS_Vertex.hxx>
#include <algorithm>

namespace {

UnitsMethods_LengthUnit LinearUnitToLengthUnit(ImportParams::LinearUnit linearUnit)
{
    switch (linearUnit) {
        case ImportParams::LinearUnit::Millimeter:
            return UnitsMethods_LengthUnit_Millimeter;
        case ImportParams::LinearUnit::Centimeter:
            return UnitsMethods_LengthUnit_Centimeter;
        case ImportParams::LinearUnit::Meter:
            return UnitsMethods_LengthUnit_Meter;
        case ImportParams::LinearUnit::Inch:
            return UnitsMethods_LengthUnit_Inch;
        case ImportParams::LinearUnit::Foot:
            return UnitsMethods_LengthUnit_Foot;
        default:
            return UnitsMethods_LengthUnit_Millimeter;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Triangulate a shape using BRepMesh_IncrementalMesh.
// Matches occt-import-js: supports both deflection modes and unit fallback.
// ---------------------------------------------------------------------------
bool TriangulateShape(TopoDS_Shape& shape, const ImportParams& params)
{
    Standard_Real linDeflection = params.linearDeflection;
    Standard_Real angDeflection = params.angularDeflection;

    if (params.linearDeflectionType == ImportParams::LinearDeflectionType::BoundingBoxRatio) {
        Bnd_Box boundingBox;
        BRepBndLib::Add(shape, boundingBox, false);
        if (boundingBox.IsVoid()) {
            return false;
        }

        Standard_Real xMin, yMin, zMin, xMax, yMax, zMax;
        boundingBox.Get(xMin, yMin, zMin, xMax, yMax, zMax);
        Standard_Real avgSize = ((xMax - xMin) + (yMax - yMin) + (zMax - zMin)) / 3.0;
        linDeflection = avgSize * params.linearDeflection;
        if (linDeflection < Precision::Confusion()) {
            // Use 1mm converted to the caller-selected linear unit.
            double mmToUnit = UnitsMethods::GetLengthUnitScale(
                UnitsMethods_LengthUnit_Millimeter,
                LinearUnitToLengthUnit(params.linearUnit));
            linDeflection = 1.0 * mmToUnit;
        }
    }

    BRepMesh_IncrementalMesh mesher(shape, linDeflection, Standard_False, angDeflection);
    return true;
}

// ---------------------------------------------------------------------------
// Extract mesh data from a single face.
// Matches occt-import-js: use only face's own location, call ComputeNormals.
// ---------------------------------------------------------------------------
static void ExtractFace(const TopoDS_Face& face,
                        OcctMeshData& mesh,
                        uint32_t& globalVertexOffset)
{
    TopLoc_Location faceLoc;
    Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation(face, faceLoc);

    if (triangulation.IsNull() || triangulation->NbNodes() == 0 || triangulation->NbTriangles() == 0) {
        return;
    }

    // Ensure normals are computed (critical — OCCT doesn't always store them).
    if (!triangulation->HasNormals()) {
        triangulation->ComputeNormals();
    }

    gp_Trsf trsf = faceLoc.Transformation();
    bool isReversed = (face.Orientation() == TopAbs_REVERSED);

    int nbNodes = triangulation->NbNodes();
    int nbTriangles = triangulation->NbTriangles();

    // --- Vertices (apply face location transform) ---
    for (int i = 1; i <= nbNodes; ++i) {
        gp_Pnt pt = triangulation->Node(i);
        pt.Transform(trsf);
        mesh.positions.push_back(static_cast<float>(pt.X()));
        mesh.positions.push_back(static_cast<float>(pt.Y()));
        mesh.positions.push_back(static_cast<float>(pt.Z()));
    }

    // --- Normals ---
    if (triangulation->HasNormals()) {
        for (int i = 1; i <= nbNodes; ++i) {
            gp_Dir normal = triangulation->Normal(i);
            normal.Transform(trsf);
            if (isReversed) {
                mesh.normals.push_back(static_cast<float>(-normal.X()));
                mesh.normals.push_back(static_cast<float>(-normal.Y()));
                mesh.normals.push_back(static_cast<float>(-normal.Z()));
            } else {
                mesh.normals.push_back(static_cast<float>(normal.X()));
                mesh.normals.push_back(static_cast<float>(normal.Y()));
                mesh.normals.push_back(static_cast<float>(normal.Z()));
            }
        }
    }

    // --- Triangles ---
    for (int i = 1; i <= nbTriangles; ++i) {
        Poly_Triangle tri = triangulation->Triangle(i);
        if (isReversed) {
            mesh.indices.push_back(globalVertexOffset + static_cast<uint32_t>(tri(1) - 1));
            mesh.indices.push_back(globalVertexOffset + static_cast<uint32_t>(tri(3) - 1));
            mesh.indices.push_back(globalVertexOffset + static_cast<uint32_t>(tri(2) - 1));
        } else {
            mesh.indices.push_back(globalVertexOffset + static_cast<uint32_t>(tri(1) - 1));
            mesh.indices.push_back(globalVertexOffset + static_cast<uint32_t>(tri(2) - 1));
            mesh.indices.push_back(globalVertexOffset + static_cast<uint32_t>(tri(3) - 1));
        }
    }

    globalVertexOffset += static_cast<uint32_t>(nbNodes);
}

// ---------------------------------------------------------------------------
// Extract mesh from a shape: full topology (faces, edges, vertices).
// ---------------------------------------------------------------------------
OcctMeshData ExtractMeshFromShape(const TopoDS_Shape& shape)
{
    OcctMeshData mesh;
    uint32_t globalVertexOffset = 0;

    // Build indexed maps for topology
    TopTools_IndexedMapOfShape faceMap, edgeMap, vertexMap;
    TopExp::MapShapes(shape, TopAbs_FACE, faceMap);
    TopExp::MapShapes(shape, TopAbs_EDGE, edgeMap);
    TopExp::MapShapes(shape, TopAbs_VERTEX, vertexMap);

    TopTools_IndexedDataMapOfShapeListOfShape edgeToFaces;
    TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, edgeToFaces);

    // Phase 1: Extract faces
    for (int fi = 1; fi <= faceMap.Extent(); ++fi) {
        const TopoDS_Face& face = TopoDS::Face(faceMap(fi));
        OcctFaceTopoData faceData;
        faceData.id = fi;
        faceData.firstIndex = static_cast<uint32_t>(mesh.indices.size());

        ExtractFace(face, mesh, globalVertexOffset);

        faceData.indexCount = static_cast<uint32_t>(mesh.indices.size()) - faceData.firstIndex;

        uint32_t triCount = faceData.indexCount / 3;
        for (uint32_t t = 0; t < triCount; ++t) {
            mesh.triangleToFaceMap.push_back(fi);
        }

        mesh.faces.push_back(std::move(faceData));
    }

    // Phase 2: Extract edges
    for (int ei = 1; ei <= edgeMap.Extent(); ++ei) {
        const TopoDS_Edge& edge = TopoDS::Edge(edgeMap(ei));
        OcctEdgeTopoData edgeData;
        edgeData.id = ei;

        for (const TopoDS_Shape& ownerFace : edgeToFaces.FindFromKey(edge)) {
            edgeData.ownerFaceIds.push_back(faceMap.FindIndex(ownerFace));
        }
        // The ancestor map does not dedupe, so a seam edge lists its face twice.
        std::sort(edgeData.ownerFaceIds.begin(), edgeData.ownerFaceIds.end());
        edgeData.ownerFaceIds.erase(
            std::unique(edgeData.ownerFaceIds.begin(), edgeData.ownerFaceIds.end()),
            edgeData.ownerFaceIds.end());

        if (BRep_Tool::Degenerated(edge)) {
            edgeData.isFreeEdge = edgeData.ownerFaceIds.empty();
            mesh.edges.push_back(std::move(edgeData));
            continue;
        }

        bool polylineFound = false;
        for (int fi : edgeData.ownerFaceIds) {
            const TopoDS_Face& face = TopoDS::Face(faceMap(fi));
            TopLoc_Location faceLoc;
            Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation(face, faceLoc);
            if (triangulation.IsNull()) {
                continue;
            }

            Handle(Poly_PolygonOnTriangulation) polyOnTri =
                BRep_Tool::PolygonOnTriangulation(edge, triangulation, faceLoc);
            if (polyOnTri.IsNull()) {
                continue;
            }

            gp_Trsf trsf = faceLoc.Transformation();
            const TColStd_Array1OfInteger& nodeIndices = polyOnTri->Nodes();
            for (int i = nodeIndices.Lower(); i <= nodeIndices.Upper(); ++i) {
                gp_Pnt pt = triangulation->Node(nodeIndices(i));
                pt.Transform(trsf);
                edgeData.points.push_back(static_cast<float>(pt.X()));
                edgeData.points.push_back(static_cast<float>(pt.Y()));
                edgeData.points.push_back(static_cast<float>(pt.Z()));
            }
            polylineFound = true;
            break;
        }

        if (!polylineFound) {
            TopLoc_Location loc;
            Handle(Poly_Polygon3D) poly3d = BRep_Tool::Polygon3D(edge, loc);
            if (!poly3d.IsNull()) {
                gp_Trsf trsf = loc.Transformation();
                const TColgp_Array1OfPnt& nodes = poly3d->Nodes();
                for (int i = nodes.Lower(); i <= nodes.Upper(); ++i) {
                    gp_Pnt p = nodes(i);
                    p.Transform(trsf);
                    edgeData.points.push_back(static_cast<float>(p.X()));
                    edgeData.points.push_back(static_cast<float>(p.Y()));
                    edgeData.points.push_back(static_cast<float>(p.Z()));
                }
            }
        }

        edgeData.isFreeEdge = edgeData.ownerFaceIds.empty();
        mesh.edges.push_back(std::move(edgeData));
    }

    // Phase 3: Backfill face edgeIndices
    for (int fi = 1; fi <= faceMap.Extent(); ++fi) {
        const TopoDS_Face& face = TopoDS::Face(faceMap(fi));
        OcctFaceTopoData& faceData = mesh.faces[fi - 1];
        for (TopExp_Explorer ex(face, TopAbs_EDGE); ex.More(); ex.Next()) {
            int edgeIdx = edgeMap.FindIndex(ex.Current());
            if (edgeIdx >= 1) {
                int zeroBasedIdx = edgeIdx - 1;
                if (std::find(faceData.edgeIndices.begin(), faceData.edgeIndices.end(), zeroBasedIdx) == faceData.edgeIndices.end()) {
                    faceData.edgeIndices.push_back(zeroBasedIdx);
                }
            }
        }
    }

    // Phase 4: Extract B-Rep vertices
    for (int vi = 1; vi <= vertexMap.Extent(); ++vi) {
        const TopoDS_Vertex& vertex = TopoDS::Vertex(vertexMap(vi));
        gp_Pnt pt = BRep_Tool::Pnt(vertex);
        OcctVertexTopoData vData;
        vData.id = vi;
        vData.position[0] = static_cast<float>(pt.X());
        vData.position[1] = static_cast<float>(pt.Y());
        vData.position[2] = static_cast<float>(pt.Z());
        mesh.vertices.push_back(std::move(vData));
    }

    return mesh;
}
