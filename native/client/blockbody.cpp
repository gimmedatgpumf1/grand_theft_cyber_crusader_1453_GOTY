// The authored block body. See blockbody.h for why it exists.
//
// Everything below is measured in metres, soles at y = 0, crown at 1.8, facing
// +z, and converted into source units at the end. Each part is one rigid box
// welded to one bone, which is what makes the rig easy to reason about: a thigh
// cannot half-follow a hip, and the stream's exit point is a single vertex
// instead of a search through a decimated cap.

#include "client/blockbody.h"

#include "client/human_palette.h"
#include "client/raster.h"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace gtc::native {
namespace {

// Source units per metre. The runtime transform multiplies by `scale`, so the
// mesh is authored in these units and comes back out in metres.
constexpr double kSourcePerMetre = 1.0 / 0.108;

struct BoneSpec {
    std::string name;
    std::string parent;
    Vec3 head;
    Vec3 tail;
};

std::string mirrored(const std::string& name) {
    std::string result = name;
    const std::size_t at = result.find(".L");
    if (at != std::string::npos) result.replace(at, 2, ".R");
    return result;
}

// The left half is written once; the right half is generated from it so the two
// sides cannot drift apart, and parent order is preserved.
std::vector<BoneSpec> skeleton() {
    static const BoneSpec left[] = {
        {"root", "", {0, 0.94, 0}, {0, 1.00, 0}},
        {"spine01", "root", {0, 1.00, 0}, {0, 1.16, 0}},
        {"spine03", "spine01", {0, 1.16, 0}, {0, 1.31, 0}},
        {"spine05", "spine03", {0, 1.31, 0}, {0, 1.47, 0}},
        {"neck01", "spine05", {0, 1.47, 0}, {0, 1.50, 0}},
        {"neck02", "neck01", {0, 1.50, 0}, {0, 1.53, 0}},
        {"neck03", "neck02", {0, 1.53, 0}, {0, 1.55, 0}},
        {"head", "neck03", {0, 1.55, 0}, {0, 1.79, 0}},
        {"jaw", "head", {0, 1.60, 0.02}, {0, 1.57, 0.06}},
        {"eye.L", "head", {0.038, 1.675, 0.070}, {0.038, 1.675, 0.110}},
        {"clavicle.L", "spine05", {0.02, 1.44, 0}, {0.16, 1.44, 0}},
        // The arms are authored in the same A-pose the imported body is in: out
        // at thirty-eight degrees. The animation pulls them in by a fixed
        // rotation when a citizen is standing; a vertically authored arm would
        // be dragged across the chest by that rotation instead.
        {"upperarm01.L", "clavicle.L", {0.170, 1.430, 0}, {0.355, 1.194, 0}},
        {"upperarm02.L", "upperarm01.L", {0.355, 1.194, 0}, {0.417, 1.115, 0}},
        {"lowerarm01.L", "upperarm02.L", {0.417, 1.115, 0}, {0.515, 0.989, 0}},
        {"lowerarm02.L", "lowerarm01.L", {0.515, 0.989, 0}, {0.564, 0.926, 0}},
        {"wrist.L", "lowerarm02.L", {0.564, 0.926, 0}, {0.638, 0.831, 0}},
        {"pelvis.L", "root", {0.02, 0.94, 0}, {0.085, 0.92, 0}},
        {"upperleg01.L", "pelvis.L", {0.090, 0.92, 0}, {0.103, 0.54, 0}},
        {"upperleg02.L", "upperleg01.L", {0.103, 0.54, 0}, {0.110, 0.42, 0}},
        {"lowerleg01.L", "upperleg02.L", {0.110, 0.42, 0}, {0.118, 0.10, 0}},
        {"lowerleg02.L", "lowerleg01.L", {0.118, 0.10, 0}, {0.120, 0.06, 0}},
        {"foot.L", "lowerleg02.L", {0.120, 0.06, 0}, {0.120, 0.02, 0.10}},
        {"toe.L", "foot.L", {0.120, 0.02, 0.10}, {0.120, 0.02, 0.18}},
    };
    std::vector<BoneSpec> specs(left, left + sizeof(left) / sizeof(left[0]));
    const std::size_t centre = specs.size();
    for (std::size_t i = 0; i < centre; ++i) {
        if (specs[i].name.find(".L") == std::string::npos) continue;
        BoneSpec copy = specs[i];
        copy.name = mirrored(copy.name);
        copy.parent = mirrored(copy.parent);
        copy.head.x = -copy.head.x;
        copy.tail.x = -copy.tail.x;
        specs.push_back(copy);
    }
    return specs;
}

// Corner indices of a box: 0..7 with bit 0 = +x, bit 1 = +y, bit 2 = +z. Each
// face is listed as a quad that walks its corners in order, wound so the
// normal points out of the body. A bowtie order here draws two crossing
// triangles and leaves the face half empty, which is exactly the notch that
// showed up at the chest before this table was rewritten.
constexpr int kBoxFaces[6][4] = {
    {0, 2, 3, 1},  // -z
    {4, 5, 7, 6},  // +z
    {0, 1, 5, 4},  // -y
    {2, 6, 7, 3},  // +y
    {0, 4, 6, 2},  // -x
    {1, 3, 7, 5},  // +x
};

// Proportions that differ between the two bodies. Everything else is shared;
// the silhouette is meant to read as the same model at a glance.
struct Proportions {
    double shoulder = 0.19;
    double hip = 0.16;
    double waist = 0.155;
    double chestDepth = 0.115;
    double armX = 0.19;
    double thigh = 0.085;
};

class Builder {
public:
    Builder(bool male) : male_(male) {
        p_.shoulder = male ? 0.19 : 0.172;
        p_.hip = male ? 0.16 : 0.168;
        p_.waist = male ? 0.155 : 0.138;
        p_.chestDepth = male ? 0.115 : 0.105;
        p_.armX = male ? 0.19 : 0.168;
        p_.thigh = male ? 0.085 : 0.082;
    }

    HumanModel build();

private:
    int bone(const std::string& name) const {
        for (std::size_t i = 0; i < model_.rigBones.size(); ++i) {
            if (model_.rigBones[i].name == name) return static_cast<int>(i);
        }
        return -1;
    }

    int vertex(const Vec3& metres, int boneIndex) {
        model_.vertices.push_back(metres * kSourcePerMetre);
        model_.rigWeights.emplace_back();
        if (boneIndex >= 0) model_.rigWeights.back().emplace_back(boneIndex, 1.0);
        return static_cast<int>(model_.vertices.size()) - 1;
    }

    void triangle(int a, int b, int c, BodyMaterial material) {
        Triangle out;
        out.v[0] = a; out.v[1] = b; out.v[2] = c;
        out.uv[0] = out.uv[1] = out.uv[2] = 0;
        model_.triangles.push_back(out);
        model_.bodyMaterials.push_back(material);
    }

    void quad(const int corners[4], BodyMaterial material) {
        triangle(corners[0], corners[1], corners[2], material);
        triangle(corners[0], corners[2], corners[3], material);
    }

    // Axis-aligned block between two opposite corners.
    void block(const Vec3& low, const Vec3& high, BodyMaterial material, int boneIndex) {
        int corner[8];
        for (int index = 0; index < 8; ++index) {
            corner[index] = vertex({(index & 1) ? high.x : low.x,
                                    (index & 2) ? high.y : low.y,
                                    (index & 4) ? high.z : low.z}, boneIndex);
        }
        for (const auto& face : kBoxFaces) {
            const int ids[4] = {corner[face[0]], corner[face[1]], corner[face[2]], corner[face[3]]};
            quad(ids, material);
        }
    }

    // Block that runs from `from` to `to` with a rectangular cross-section,
    // which is how every limb is made: no taper, no twisting, no surprises.
    void limb(const Vec3& from, const Vec3& to, double halfWidth, double halfDepth,
              BodyMaterial material, int boneIndex) {
        const Vec3 axis = normalized(to - from);
        // `right` must be the sideways axis, so the depth reference is +z, not
        // +y: with an up reference the cross product came out along -z and the
        // width and depth of every limb were swapped.
        const Vec3 reference = std::abs(axis.z) > 0.9 ? Vec3{1, 0, 0} : Vec3{0, 0, 1};
        const Vec3 right = normalized(cross(reference, axis));
        const Vec3 depth = normalized(cross(axis, right));
        int corner[8];
        for (int index = 0; index < 8; ++index) {
            const Vec3 centre = (index & 2) ? to : from;
            const Vec3 offset = right * ((index & 1) ? halfWidth : -halfWidth) +
                                depth * ((index & 4) ? halfDepth : -halfDepth);
            corner[index] = vertex(centre + offset, boneIndex);
        }
        for (const auto& face : kBoxFaces) {
            const int ids[4] = {corner[face[0]], corner[face[1]], corner[face[2]], corner[face[3]]};
            quad(ids, material);
        }
    }

    // The anatomically explicit part of the mesh: a four-sided wedge with a
    // single apex vertex on the midline, so the stream has one unambiguous
    // exit point no matter how the pose is rotated.
    void genitalWedge(int boneIndex) {
        const Vec3 corners[4] = {{-0.030, 0.840, 0.102}, {0.030, 0.840, 0.102},
                                 {0.030, 0.920, 0.102}, {-0.030, 0.920, 0.102}};
        int back[4];
        for (int i = 0; i < 4; ++i) back[i] = vertex(corners[i], boneIndex);
        const int apex = vertex({0, 0.878, 0.152}, boneIndex);
        Triangle out;
        out.uv[0] = out.uv[1] = out.uv[2] = 0;
        for (int i = 0; i < 4; ++i) {
            out.v[0] = back[i]; out.v[1] = back[(i + 1) % 4]; out.v[2] = apex;
            model_.genitalTriangles.push_back(out);
        }
        out.v[0] = back[0]; out.v[1] = back[2]; out.v[2] = back[1];
        model_.genitalTriangles.push_back(out);
        out.v[0] = back[0]; out.v[1] = back[3]; out.v[2] = back[2];
        model_.genitalTriangles.push_back(out);
    }

    bool male_ = true;
    Proportions p_;
    HumanModel model_;
    std::vector<int> headVertices_;
};

HumanModel Builder::build() {
    model_.male = male_;
    model_.blockBody = true;
    model_.scale = 0.108;
    model_.ground = 0.0;
    model_.texcoords.push_back({0, 0});

    // Bones first: every part below needs its bone index, and a parent must
    // exist before its child can point at it.
    const std::vector<BoneSpec> specs = skeleton();
    for (const BoneSpec& spec : specs) {
        RigBone rig;
        rig.name = spec.name;
        rig.parent = spec.parent;
        rig.parentIndex = bone(spec.parent);
        model_.rigBones.push_back(std::move(rig));
    }

    const int root = bone("root"), spine01 = bone("spine01"), spine05 = bone("spine05");
    const int neck01 = bone("neck01"), head = bone("head"), jaw = bone("jaw");

    // Torso: hips, waist, chest. The chest is the widest of the three, which is
    // what gives the figure its shoulders without any extra geometry.
    block({-0.150, 0.86, -0.105}, {0.150, 1.00, 0.105}, BodyMaterial::Pants, root);
    block({-0.140, 1.00, -0.100}, {0.140, 1.18, 0.100}, BodyMaterial::Torso, spine01);
    block({-0.160, 1.18, -0.105}, {0.160, 1.47, 0.105}, BodyMaterial::Shirt, spine05);
    block({-0.050, 1.45, -0.050}, {0.050, 1.56, 0.050}, BodyMaterial::Skin, neck01);
    const std::size_t headStart = model_.vertices.size();
    // The skull sits centred on the neck, not pushed forward of it: an earlier
    // version offset the whole head nine centimetres towards the nose.
    block({-0.085, 1.55, -0.090}, {0.085, 1.75, 0.090}, BodyMaterial::Skin, head);
    for (std::size_t index = headStart; index < model_.vertices.size(); ++index) {
        headVertices_.push_back(static_cast<int>(index));
    }

    // Limbs. Arms are two boxes and a hand; legs two boxes and a boot. The arm
    // boxes sit outside the chest, which is what keeps them visible: at the old
    // widths they were swallowed by the torso silhouette.
    for (int side = 0; side < 2; ++side) {
        const double s = side == 0 ? 1.0 : -1.0;
        const std::string suffix = side == 0 ? ".L" : ".R";
        const int upperArm = bone("upperarm01" + suffix);
        const int lowerArm = bone("lowerarm01" + suffix);
        const int wrist = bone("wrist" + suffix);
        const int upperLeg = bone("upperleg01" + suffix);
        const int lowerLeg = bone("lowerleg01" + suffix);
        const int foot = bone("foot" + suffix);
        // Same A-pose as the bones above, so a sleeve is a rigid box on a
        // rigid bone and nothing has to be bent into place.
        limb({s * 0.170, 1.430, 0}, {s * 0.355, 1.194, 0}, 0.045, 0.048,
             BodyMaterial::Sleeve, upperArm);
        limb({s * 0.355, 1.194, 0}, {s * 0.515, 0.989, 0}, 0.038, 0.042,
             BodyMaterial::Sleeve, lowerArm);
        limb({s * 0.515, 0.989, 0}, {s * 0.638, 0.831, 0}, 0.036, 0.042,
             BodyMaterial::Skin, wrist);
        // The two thigh boxes must not touch in the middle: at the old width
        // they overlapped and the legs read as one column, which made the
        // trousers look like a long skirt.
        // Legs are splayed slightly: the thigh bones aim outwards so the two
        // trouser legs separate instead of fusing into one wide column.
        limb({s * 0.090, 0.90, 0}, {s * 0.103, 0.54, 0}, 0.062, 0.085,
             BodyMaterial::Pants, upperLeg);
        limb({s * 0.103, 0.54, 0}, {s * 0.118, 0.10, 0}, 0.052, 0.068,
             BodyMaterial::Pants, lowerLeg);
        block({s * 0.120 - 0.052, 0.0, -0.068}, {s * 0.120 + 0.052, 0.10, 0.150},
              BodyMaterial::Boots, foot);
    }

    if (male_) genitalWedge(root);

    // Joint markers. They are not part of any triangle; the pose maths reads
    // their positions through the bone's head/tail vertex lists.
    for (std::size_t index = 0; index < model_.rigBones.size(); ++index) {
        RigBone& rig = model_.rigBones[index];
        // The markers carry the *parent* bone, not their own. `pose()` turns a
        // bone around its head marker, so the marker has to move with the
        // parent chain; welded to its own bone it would swing around itself and
        // every joint below it would telescope.
        const int marker = rig.parentIndex >= 0 ? rig.parentIndex : static_cast<int>(index);
        rig.head = {vertex(specs[index].head, marker)};
        rig.tail = {vertex(specs[index].tail, marker)};
    }
    model_.eyeSource[0] = model_.rigBones[bone("eye.L")].head;
    model_.eyeSource[1] = model_.rigBones[bone("eye.R")].head;
    for (int side = 0; side < 2; ++side) {
        const int index = bone(side == 0 ? "eye.L" : "eye.R");
        model_.eyeDirection[side] = normalized(specs[static_cast<std::size_t>(index)].tail -
                                               specs[static_cast<std::size_t>(index)].head);
    }
    model_.headCutoff = 1.46;
    model_.headCenter = {0, 1.65, 0.0};
    model_.headVertices = headVertices_;
    for (const Triangle& triangle : model_.triangles) {
        double lowest = 1e9;
        for (int corner = 0; corner < 3; ++corner) lowest = std::min(lowest, model_.vertices[triangle.v[corner]].y);
        if (lowest >= 1.55 * kSourcePerMetre) model_.headTriangles.push_back(triangle);
    }
    (void)jaw;
    return model_;
}

// One camera-facing quad of a head accessory. The four corners are given in
// head space and mapped through the head bone before the normal is taken, so a
// turned head carries its sunglasses with it.
void headQuad(Rasterizer& rasterizer, const Camera& camera, const std::function<Vec3(Vec3)>& headPoint,
              const Vec3 corners[4], const Material& material) {
    Vec3 point[4];
    for (int i = 0; i < 4; ++i) point[i] = headPoint(corners[i]);
    const Vec3 normal = normalized(cross(point[1] - point[0], point[2] - point[0]));
    for (int i = 1; i + 1 < 4; ++i) {
        rasterizer.triangle({cameraVertex(camera, point[0], normal, point[0]),
                             cameraVertex(camera, point[i], normal, point[i]),
                             cameraVertex(camera, point[i + 1], normal, point[i + 1])}, material);
    }
}

}  // namespace

void drawBlockAccessories(Rasterizer& rasterizer, const Camera& camera,
                          const std::function<Vec3(Vec3)>& headPoint, const Character& character) {
    if (character.appearance.hair > 0) {
        Material hair;
        hair.color = hairColor(character.appearance.hairColor) * 1.15;
        hair.specular = 0.10;
        // The cap wraps the skull rather than sitting on top of it: the sides
        // drop to just above the eyes and the back covers the whole occiput, so
        // no band of bare skin shows between hair and face. It is drawn a
        // millimetre proud of the head block to avoid z-fighting.
        const double outer = 0.0885, proud = 0.0915;
        const Vec3 top[4] = {{-proud, 1.757, proud}, {proud, 1.757, proud},
                             {proud, 1.757, -proud}, {-proud, 1.757, -proud}};
        headQuad(rasterizer, camera, headPoint, top, hair);
        // Front: a straight fringe over the brow, then the sides and the back.
        const Vec3 fringe[4] = {{-proud, 1.757, proud}, {proud, 1.757, proud},
                                {proud, 1.690, proud}, {-proud, 1.690, proud}};
        headQuad(rasterizer, camera, headPoint, fringe, hair);
        for (int side = 0; side < 2; ++side) {
            const double x = side == 0 ? proud : -proud;
            const Vec3 flank[4] = {{x, 1.757, proud}, {x, 1.757, -proud},
                                   {x, 1.690, -proud}, {x, 1.690, proud}};
            headQuad(rasterizer, camera, headPoint, flank, hair);
        }
        const Vec3 back[4] = {{-proud, 1.757, -proud}, {proud, 1.757, -proud},
                              {proud, 1.640, -proud}, {-proud, 1.640, -proud}};
        headQuad(rasterizer, camera, headPoint, back, hair);
        // A short nape, so the back of the skull does not end in a hard line.
        const Vec3 nape[4] = {{-0.070, 1.640, -proud}, {0.070, 1.640, -proud},
                              {0.070, 1.585, -0.078}, {-0.070, 1.585, -0.078}};
        headQuad(rasterizer, camera, headPoint, nape, hair);
        (void)outer;
    }
    const double face = 0.0925;
    // Eyes first, then the shades over them: one wide band across the whole
    // face reads as a visor, not as eyes, so the two are separate panels with a
    // gap over the nose.
    Material eye;
    eye.color = colorHex("#20262b");
    eye.specular = 0.18;
    Material shades;
    shades.color = colorHex("#0c1214");
    shades.specular = 0.45;
    if (character.appearance.sunglasses) {
        const Vec3 band[4] = {{-0.090, 1.640, face}, {0.090, 1.640, face},
                              {0.090, 1.682, face}, {-0.090, 1.682, face}};
        headQuad(rasterizer, camera, headPoint, band, shades);
    } else {
        for (int side = 0; side < 2; ++side) {
            const double lowX = side == 0 ? 0.012 : -0.078;
            const double highX = side == 0 ? 0.078 : -0.012;
            const Vec3 panel[4] = {{lowX, 1.650, face}, {highX, 1.650, face},
                                   {highX, 1.678, face}, {lowX, 1.678, face}};
            headQuad(rasterizer, camera, headPoint, panel, eye);
        }
    }
    // Nose and mouth. Two quads and a shading panel are enough to read as a
    // face at arm's length, which is the range this body is meant to be seen
    // from; the imported scan's per-pore detail never survived it anyway.
    {
        Material nose;
        nose.color = skinColor(character.appearance.skin) * 0.88;
        const double tip = face + 0.020;
        const Vec3 bridge[4] = {{-0.012, 1.618, face}, {0.012, 1.618, face},
                                {0.012, 1.672, face}, {-0.012, 1.672, face}};
        headQuad(rasterizer, camera, headPoint, bridge, nose);
        for (int side = 0; side < 2; ++side) {
            const double x = side == 0 ? 0.012 : -0.012;
            const Vec3 flank[4] = {{x, 1.672, face}, {x, 1.672, tip},
                                   {x, 1.618, tip}, {x, 1.618, face}};
            headQuad(rasterizer, camera, headPoint, flank, nose);
        }
        Material mouth;
        mouth.color = colorHex("#6d3b34");
        const Vec3 lips[4] = {{-0.030, 1.576, face}, {0.030, 1.576, face},
                              {0.030, 1.588, face}, {-0.030, 1.588, face}};
        headQuad(rasterizer, camera, headPoint, lips, mouth);
    }
    if (character.appearance.beard > 0) {
        Material beard;
        beard.color = hairColor(character.appearance.hairColor) * 0.85;
        const Vec3 jaw[4] = {{-0.070, 1.556, face}, {0.070, 1.556, face},
                             {0.070, 1.634, face}, {-0.070, 1.634, face}};
        headQuad(rasterizer, camera, headPoint, jaw, beard);
    }
}

BlockProportions blockProportions(const HumanModel& model, const std::vector<double>& morphs) {
    BlockProportions proportions;
    const auto read = [&](const char* key, double normalised) {
        const int index = model.specIndex(key);
        if (index < 0 || static_cast<std::size_t>(index) >= morphs.size()) return 0.0;
        return std::clamp(morphs[static_cast<std::size_t>(index)] / 100.0, -1.0, 1.0) * normalised;
    };
    // Five per cent of stature and twelve per cent of head size at the ends of
    // the sliders: enough to read as different people, not enough to break the
    // clothing or the camera attachment.
    // There is no dedicated stature slider in the morph list, so height comes
    // from the two sliders that read as one: head height and leg width are not
    // it, but `neckWidth` and `headHeight` are what the creator exposes for a
    // taller-looking citizen. `headHeight` also drives the skull below.
    proportions.stature = 1.0 + read("headHeight", 0.03) + read("neckWidth", 0.01);
    proportions.head = 1.0 + read("headHeight", 0.12);
    return proportions;
}

void shapeBlockBody(const BlockProportions& proportions, std::vector<Vec3>& points) {
    if (proportions.stature == 1.0 && proportions.head == 1.0) return;
    for (Vec3& point : points) {
        const double y = point.y * proportions.stature;
        if (y > 1.50) {
            // The skull grows around the neck joint, not around the floor.
            point.y = 1.50 + (y - 1.50) * proportions.head;
            point.x *= proportions.head;
            point.z *= proportions.head;
        } else {
            point.y = y;
        }
    }
}

HumanModel blockBody(bool male) {
    return Builder(male).build();
}

}  // namespace gtc::native
