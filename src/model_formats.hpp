#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

// Original game resource decoding only. No PSP CPU, GU or OS execution.
namespace dokodemo::model {
using Bytes = std::vector<std::uint8_t>;
struct FormatError : std::runtime_error { using std::runtime_error::runtime_error; };
struct Range { std::size_t offset=0, bytes=0; };
struct Vec2 { float x=0,y=0; };
struct Vec3 { float x=0,y=0,z=0; };
struct TextureReference { Range source; unsigned roleBit=0; std::string name; };
struct Material { Range source; std::uint16_t flags=0; std::vector<TextureReference> textures; };
struct Bounds { Range source; float boxScale=0; std::array<Vec3,8> box{}; std::array<float,4> sphere{}; };
enum class Topology { TriangleStrip, TriangleFan, Triangles, LineStrip, Lines, Points };
struct Primitive { Range source; unsigned opcode=0; Topology topology{}; std::vector<std::uint16_t> indices; };
struct PrimitiveStream { Range source; std::vector<Primitive> primitives; };
struct Bone { Range source; std::uint32_t idWord=0; std::array<float,16> matrix{}; };
struct Geometry {
    Range source, positionsSource, weightsSource;
    std::uint8_t mode=0, unknownByte=0;
    std::int8_t smallSigned=0;
    std::uint16_t flags=0;
    int materialIndex=-1;
    std::size_t vertexCount=0;
    float positionScale=0;
    std::vector<Vec3> positions;
    std::vector<std::array<std::int16_t,3>> rawPositions;
    std::vector<std::array<std::int8_t,3>> normals;
    std::vector<std::array<std::uint8_t,4>> rgba;
    std::vector<Vec2> uv;
    Bounds bounds;
    std::vector<Bone> bones;
    // 24 bytes per vertex. Weight packing/bone application remains unresolved.
    Bytes weightRecords;
    std::vector<std::array<std::int16_t,4>> triangleRecords, secondaryTriangleRecords;
    std::array<int,2> secondaryCounts{};
    std::vector<PrimitiveStream> primitiveStreams;
};
// Historical type name retained for callers. Reached7F0B8/836F8/26B80:
// first=region selector,second=shape,scalar0/1=dimensions/radius.
// The earlier RTTI-only particle interpretation is superseded by this consumer.
struct ParticleEmitterParameters {
    std::uint16_t first=0,second=0;
    float scalar0=0,scalar1=0;
};
struct Node {
    Range source, commonSource, uninterpreted;
    int type=0;
    bool hasCommon=false, hasGeometry=false, unsupportedType=false;
    std::uint16_t commonFlags=0;
    std::array<std::int16_t,5> commonShorts{};
    // Preserved without assigning unproved TRS/hierarchy semantics.
    std::array<std::array<float,4>,4> commonVectors{};
    std::vector<float> optionalMatrix;
    Bytes typePayload; // retained node-specific bytes,never native pointers
    ParticleEmitterParameters emitter;
    Geometry geometry;
};
struct Dvs {
    Range source, materialPadding, postNodeTail;
    std::uint32_t materialCountWord=0;
    Bounds bounds;
    std::array<std::int16_t,4> nodeHeaderShorts{};
    std::vector<Material> materials;
    std::vector<Node> nodes;
};
struct DakSection {
    Range source, prefix, payload, proceduralTrailer;
    unsigned type=0, subtype=0;
    int count=0, components=0;
    bool supported=false;
    std::vector<std::uint32_t> keyWords;
};
struct DakObject {
    Range source, uninterpreted;
    int targetNodeIndex=-1, declaredSectionCount=0;
    std::vector<float> transform;
    std::vector<DakSection> sections;
};
struct Dak {
    Range source, postObjectTail;
    std::uint32_t flags=0, durationWord=0;
    std::vector<DakObject> objects;
};
struct AscName { std::size_t offset=0; std::string name; };
struct AscBlock { Range source, payload; std::uint32_t opaqueWord=0; std::vector<std::uint32_t> words; };
struct Asc { Range source, nameTable, postBlockTail; std::array<std::uint32_t,3> headerWords{}; std::vector<AscName> names; std::vector<AscBlock> blocks; };

Bytes readFile(const std::string& path);
Dvs parseDvs(const Bytes& data);
Dak parseDak(const Bytes& data);
Asc parseAsc(const Bytes& data);
// Checks the original animation's model-node references before a future player
// binds them. A negative target denotes an object outside the DVS node table.
void validateBinding(const Dvs& model, const Dak& animation);
// Derives local-space triangles from the primary render primitive stream.
// Node transforms, skinning, materials, visibility and DAK evaluation are separate.
std::vector<std::array<std::uint16_t,3>> triangles(const Geometry& geometry, std::size_t streamIndex=0);
} // namespace dokodemo::model
